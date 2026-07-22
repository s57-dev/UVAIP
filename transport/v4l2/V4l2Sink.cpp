#include "V4l2Sink.h"

#include "PixelFormatMap.h"

#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>

namespace uvap::v4l2
{
namespace
{

std::string errnoMessage(const std::string& prefix)
{
    return prefix + ": " + std::strerror(errno);
}

} // namespace

V4l2Sink::V4l2Sink(std::string devicePath)
    : devicePath_(std::move(devicePath))
{
}

V4l2Sink::~V4l2Sink()
{
    stop();
}

void V4l2Sink::configure(const uvap::SinkConfig& config)
{
    if (streaming_)
    {
        throw std::runtime_error("V4l2Sink: cannot configure while streaming");
    }

    if (config.format.width <= 0 || config.format.height <= 0)
    {
        throw std::runtime_error("V4l2Sink: invalid frame dimensions");
    }

    if (!toV4l2Fourcc(config.format.pixelFormat))
    {
        throw std::runtime_error("V4l2Sink: unsupported pixel format");
    }

    if (config.bufferCount < 2)
    {
        throw std::runtime_error("V4l2Sink: bufferCount must be >= 2");
    }

    config_ = config;
}

uvap::SinkConfig V4l2Sink::configuration() const
{
    return config_;
}

void V4l2Sink::start()
{
    if (streaming_)
    {
        return;
    }

    if (config_.format.width <= 0 || config_.format.height <= 0)
    {
        throw std::runtime_error("V4l2Sink: configure() before start()");
    }

    fd_ = ::open(devicePath_.c_str(), O_RDWR | O_NONBLOCK);
    if (fd_ < 0)
    {
        throw std::runtime_error(errnoMessage("V4l2Sink: open " + devicePath_));
    }

    try
    {
        setFormat();
        requestAndMapBuffers();
        streamOn();
        streaming_ = true;
    }
    catch (...)
    {
        releaseBuffers();
        if (fd_ >= 0)
        {
            ::close(fd_);
            fd_ = -1;
        }
        throw;
    }
}

void V4l2Sink::stop()
{
    if (!streaming_ && fd_ < 0 && buffers_.empty())
    {
        return;
    }

    streamOff();
    releaseBuffers();

    if (fd_ >= 0)
    {
        ::close(fd_);
        fd_ = -1;
    }

    streaming_ = false;
}

bool V4l2Sink::pushFrame(const uvap::Frame& frame)
{
    if (!streaming_ || fd_ < 0)
    {
        return false;
    }

    if (!frame.isCpuMapped() || frame.handle() == nullptr)
    {
        return false;
    }

    const uvap::FrameInfo& info = frame.info();
    if (info.width != config_.format.width ||
        info.height != config_.format.height ||
        info.pixelFormat != config_.format.pixelFormat)
    {
        return false;
    }

    std::uint32_t index = 0;
    if (!freeIndices_.empty())
    {
        index = freeIndices_.front();
        freeIndices_.pop_front();
    }
    else
    {
        v4l2_buffer dq{};
        dq.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        dq.memory = V4L2_MEMORY_MMAP;

        if (::ioctl(fd_, VIDIOC_DQBUF, &dq) < 0)
        {
            if (errno == EAGAIN)
            {
                return false; // backpressure: no free buffer
            }
            throw std::runtime_error(errnoMessage("V4l2Sink: VIDIOC_DQBUF"));
        }

        if (dq.index >= buffers_.size())
        {
            throw std::runtime_error("V4l2Sink: DQBUF returned invalid index");
        }
        index = dq.index;
    }

    const std::size_t copyBytes = std::min(frame.sizeBytes(), buffers_[index].length);
    std::memcpy(buffers_[index].start, frame.handle(), copyBytes);

    v4l2_buffer buf{};
    buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = index;
    buf.bytesused = static_cast<std::uint32_t>(copyBytes);
    if (info.timestampNs > 0)
    {
        buf.timestamp.tv_sec = static_cast<long>(info.timestampNs / 1000000000LL);
        buf.timestamp.tv_usec =
            static_cast<long>((info.timestampNs % 1000000000LL) / 1000LL);
    }

    if (::ioctl(fd_, VIDIOC_QBUF, &buf) < 0)
    {
        freeIndices_.push_front(index);
        throw std::runtime_error(errnoMessage("V4l2Sink: VIDIOC_QBUF"));
    }

    return true;
}

void V4l2Sink::setFormat()
{
    const auto fourcc = toV4l2Fourcc(config_.format.pixelFormat);
    if (!fourcc)
    {
        throw std::runtime_error("V4l2Sink: unsupported pixel format");
    }

    const std::size_t sizeImage =
        imageSizeBytes(config_.format.pixelFormat,
                       config_.format.width,
                       config_.format.height);

    v4l2_format fmt{};
    fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    fmt.fmt.pix.width = static_cast<__u32>(config_.format.width);
    fmt.fmt.pix.height = static_cast<__u32>(config_.format.height);
    fmt.fmt.pix.pixelformat = *fourcc;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;
    fmt.fmt.pix.sizeimage = static_cast<__u32>(sizeImage);
    if (config_.format.strideBytes > 0)
    {
        fmt.fmt.pix.bytesperline = static_cast<__u32>(config_.format.strideBytes);
    }

    if (::ioctl(fd_, VIDIOC_S_FMT, &fmt) < 0)
    {
        throw std::runtime_error(errnoMessage("V4l2Sink: VIDIOC_S_FMT"));
    }
}

void V4l2Sink::requestAndMapBuffers()
{
    v4l2_requestbuffers req{};
    req.count = static_cast<__u32>(config_.bufferCount);
    req.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    req.memory = V4L2_MEMORY_MMAP;

    if (::ioctl(fd_, VIDIOC_REQBUFS, &req) < 0)
    {
        throw std::runtime_error(errnoMessage("V4l2Sink: VIDIOC_REQBUFS"));
    }

    if (req.count < 2)
    {
        throw std::runtime_error("V4l2Sink: driver returned fewer than 2 buffers");
    }

    buffers_.resize(req.count);

    for (__u32 i = 0; i < req.count; ++i)
    {
        v4l2_buffer buf{};
        buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (::ioctl(fd_, VIDIOC_QUERYBUF, &buf) < 0)
        {
            throw std::runtime_error(errnoMessage("V4l2Sink: VIDIOC_QUERYBUF"));
        }

        void* start = ::mmap(nullptr,
                             buf.length,
                             PROT_READ | PROT_WRITE,
                             MAP_SHARED,
                             fd_,
                             buf.m.offset);
        if (start == MAP_FAILED)
        {
            throw std::runtime_error(errnoMessage("V4l2Sink: mmap"));
        }

        buffers_[i].start = start;
        buffers_[i].length = buf.length;
        freeIndices_.push_back(i);
    }
}

void V4l2Sink::streamOn()
{
    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    if (::ioctl(fd_, VIDIOC_STREAMON, &type) < 0)
    {
        throw std::runtime_error(errnoMessage("V4l2Sink: VIDIOC_STREAMON"));
    }
}

void V4l2Sink::streamOff() noexcept
{
    if (fd_ < 0)
    {
        return;
    }

    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    ::ioctl(fd_, VIDIOC_STREAMOFF, &type);
}

void V4l2Sink::releaseBuffers() noexcept
{
    for (MappedBuffer& buffer : buffers_)
    {
        if (buffer.start != nullptr && buffer.length > 0)
        {
            ::munmap(buffer.start, buffer.length);
        }
        buffer = {};
    }
    buffers_.clear();
    freeIndices_.clear();
}

} // namespace uvap::v4l2
