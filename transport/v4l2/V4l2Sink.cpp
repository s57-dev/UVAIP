/*
 * Copyright 2026 S57 ApS
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

#include "V4l2Sink.h"

#include "PixelFormatMap.h"

#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace uvap::v4l2
{
namespace
{

int ioctlRetry(int fd, unsigned long request, void* argument)
{
    int rc = 0;
    do
    {
        rc = ::ioctl(fd, request, argument);
    } while (rc < 0 && errno == EINTR);
    return rc;
}

[[noreturn]] void throwSystemError(const std::string& operation)
{
    const int error = errno;
    throw std::system_error(error, std::generic_category(), operation);
}

} // namespace

/**
 * IMemory specialization for one V4L2 MMAP OUTPUT buffer.
 *
 * State machine (producer / transport side):
 *   Dequeued — after acquireFrame; producer may fill
 *   Queued   — after pushFrame / QBUF; device owns until DQBUF
 * Destroying a dequeued lease returns the slot to Free in the pool.
 */
class V4l2Sink::BufferMemory final : public uvap::IMemory
{
public:
    BufferMemory(std::shared_ptr<BufferPool> pool, std::uint32_t index)
        : pool_(std::move(pool))
        , index_(index)
    {
    }

    ~BufferMemory() override
    {
        if (pool_)
        {
            pool_->releaseLease(index_);
        }
    }

    BufferMemory(const BufferMemory&) = delete;
    BufferMemory& operator=(const BufferMemory&) = delete;

    std::size_t sizeBytes() const override
    {
        return pool_->slots[index_].mapping.length;
    }

    bool isCpuMapped() const override { return true; }

    void* data() const override
    {
        return pool_->slots[index_].mapping.start;
    }

    uvap::MemoryType type() const override
    {
        return uvap::MemoryType::CpuMapped;
    }

    uvap::BufferState state() const override
    {
        std::lock_guard<std::mutex> lock(pool_->mutex);
        return pool_->slots[index_].state == BufferPool::SlotState::Queued
                   ? uvap::BufferState::Queued
                   : uvap::BufferState::Dequeued;
    }

    std::uint32_t index() const { return index_; }

    bool belongsTo(const BufferPool* pool) const
    {
        return pool_.get() == pool;
    }

private:
    std::shared_ptr<BufferPool> pool_;
    std::uint32_t index_ = 0;
};

V4l2Sink::BufferPool::~BufferPool()
{
    for (Slot& slot : slots)
    {
        if (slot.mapping.start != nullptr && slot.mapping.length > 0)
        {
            ::munmap(slot.mapping.start, slot.mapping.length);
        }
    }
}

bool V4l2Sink::BufferPool::acquireFree(std::uint32_t& index)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (freeIndices.empty())
    {
        return false;
    }
    index = freeIndices.front();
    freeIndices.pop_front();
    if (index >= slots.size() || slots[index].state != SlotState::Free)
    {
        throw std::logic_error("V4l2Sink: corrupt free-buffer queue");
    }
    slots[index].state = SlotState::Dequeued;
    return true;
}

bool V4l2Sink::BufferPool::transition(std::uint32_t index,
                                      SlotState from,
                                      SlotState to)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (index >= slots.size() || slots[index].state != from)
    {
        return false;
    }
    slots[index].state = to;
    if (to == SlotState::Free)
    {
        freeIndices.push_back(index);
    }
    return true;
}

void V4l2Sink::BufferPool::releaseLease(std::uint32_t index) noexcept
{
    std::lock_guard<std::mutex> lock(mutex);
    if (index < slots.size() && slots[index].state == SlotState::Dequeued)
    {
        slots[index].state = SlotState::Free;
        freeIndices.push_back(index);
    }
}

void V4l2Sink::BufferPool::reclaimAllQueued()
{
    std::lock_guard<std::mutex> lock(mutex);
    freeIndices.clear();
    for (std::uint32_t i = 0; i < slots.size(); ++i)
    {
        if (slots[i].state == SlotState::Dequeued)
        {
            throw std::logic_error("V4l2Sink: cannot reclaim an outstanding frame");
        }
        slots[i].state = SlotState::Free;
        freeIndices.push_back(i);
    }
}

bool V4l2Sink::BufferPool::hasDequeued() const
{
    std::lock_guard<std::mutex> lock(mutex);
    for (const Slot& slot : slots)
    {
        if (slot.state == SlotState::Dequeued)
        {
            return true;
        }
    }
    return false;
}

V4l2Sink::V4l2Sink(std::string devicePath)
    : devicePath_(std::move(devicePath))
{
}

V4l2Sink::~V4l2Sink()
{
    try
    {
        stop();
    }
    catch (...)
    {
        // Outstanding Frames retain their BufferPool and mappings. Closing the
        // device retires the queue; the final lease unmaps the pool safely.
    }
    closeDevice();
}

void V4l2Sink::openDevice()
{
    if (fd_ >= 0)
    {
        return;
    }

    fd_ = ::open(devicePath_.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd_ < 0)
    {
        throwSystemError("V4l2Sink: open " + devicePath_);
    }

    try
    {
        validateCapabilities();
    }
    catch (...)
    {
        ::close(fd_);
        fd_ = -1;
        throw;
    }
}

void V4l2Sink::validateCapabilities()
{
    v4l2_capability capability{};
    if (ioctlRetry(fd_, VIDIOC_QUERYCAP, &capability) < 0)
    {
        throwSystemError("V4l2Sink: VIDIOC_QUERYCAP");
    }

    const std::uint32_t caps =
        (capability.capabilities & V4L2_CAP_DEVICE_CAPS) != 0
            ? capability.device_caps
            : capability.capabilities;
    if ((caps & V4L2_CAP_VIDEO_OUTPUT) == 0)
    {
        throw std::runtime_error("V4l2Sink: device lacks single-plane VIDEO_OUTPUT");
    }
    if ((caps & V4L2_CAP_STREAMING) == 0)
    {
        throw std::runtime_error("V4l2Sink: device lacks streaming I/O");
    }
}

void V4l2Sink::closeDevice() noexcept
{
    streamOffNoexcept();
    releaseMappedBuffers();
    streaming_ = false;
    negotiatedSizeImage_ = 0;
    if (fd_ >= 0)
    {
        ::close(fd_);
        fd_ = -1;
    }
}

uvap::FrameInfo V4l2Sink::getFormat() const
{
    if (fd_ < 0)
    {
        throw std::runtime_error("V4l2Sink: getFormat requires open device");
    }

    v4l2_format fmt{};
    fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    if (ioctlRetry(fd_, VIDIOC_G_FMT, &fmt) < 0)
    {
        throwSystemError("V4l2Sink: VIDIOC_G_FMT");
    }

    uvap::FrameInfo info{};
    info.width = static_cast<int>(fmt.fmt.pix.width);
    info.height = static_cast<int>(fmt.fmt.pix.height);
    info.strideBytes = static_cast<int>(fmt.fmt.pix.bytesperline);
    const auto mapped = fromV4l2Fourcc(fmt.fmt.pix.pixelformat);
    if (!mapped)
    {
        throw std::runtime_error("V4l2Sink: driver returned unsupported pixel format");
    }
    info.pixelFormat = *mapped;
    return info;
}

StreamParm V4l2Sink::getStreamParm() const
{
    if (fd_ < 0)
    {
        throw std::runtime_error("V4l2Sink: getStreamParm requires open device");
    }

    v4l2_streamparm parm{};
    parm.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    if (ioctlRetry(fd_, VIDIOC_G_PARM, &parm) < 0)
    {
        throwSystemError("V4l2Sink: VIDIOC_G_PARM");
    }

    StreamParm out{};
    out.fpsDenominator = static_cast<int>(parm.parm.output.timeperframe.numerator);
    out.fpsNumerator = static_cast<int>(parm.parm.output.timeperframe.denominator);
    if (out.fpsDenominator <= 0)
    {
        out.fpsDenominator = 1;
    }
    if (out.fpsNumerator <= 0)
    {
        out.fpsNumerator = 30;
    }
    return out;
}

void V4l2Sink::setStreamParm(const StreamParm& parm)
{
    openDevice();

    v4l2_streamparm sp{};
    sp.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    if (ioctlRetry(fd_, VIDIOC_G_PARM, &sp) < 0)
    {
        throwSystemError("V4l2Sink: VIDIOC_G_PARM");
    }

    sp.parm.output.timeperframe.numerator =
        static_cast<__u32>(parm.fpsDenominator > 0 ? parm.fpsDenominator : 1);
    sp.parm.output.timeperframe.denominator =
        static_cast<__u32>(parm.fpsNumerator > 0 ? parm.fpsNumerator : 30);

    if (ioctlRetry(fd_, VIDIOC_S_PARM, &sp) < 0)
    {
        throwSystemError("V4l2Sink: VIDIOC_S_PARM");
    }
}

void V4l2Sink::yield()
{
    requireNoOutstandingFrames("yield");
    streamOff();
    if (pool_)
    {
        pool_->reclaimAllQueued();
    }
    releaseMappedBuffers();
    reqbufsZero();
    negotiatedSizeImage_ = 0;
}

void V4l2Sink::recycleBuffers()
{
    if (!pool_ || fd_ < 0)
    {
        return;
    }

    requireNoOutstandingFrames("recycleBuffers");
    streamOff();
    pool_->reclaimAllQueued();
    try
    {
        streamOn();
        streaming_ = true;
    }
    catch (...)
    {
        streaming_ = false;
        throw;
    }
}

void V4l2Sink::requireNoOutstandingFrames(const char* operation) const
{
    if (pool_ && pool_->hasDequeued())
    {
        throw std::logic_error(
            std::string("V4l2Sink: cannot ") + operation + " with outstanding Frames");
    }
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

    if (config.queueDepth < 2)
    {
        throw std::runtime_error("V4l2Sink: queueDepth must be >= 2");
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

    openDevice();

    try
    {
        setFormat();
        requestAndMapBuffers();
        streamOn();
        streaming_ = true;
    }
    catch (...)
    {
        releaseMappedBuffers();
        reqbufsZeroNoexcept();
        streaming_ = false;
        throw;
    }
}

void V4l2Sink::stop()
{
    yield();
}

bool V4l2Sink::acquireFrame(uvap::Frame& out)
{
    if (!streaming_ || !pool_)
    {
        return false;
    }

    std::uint32_t index = 0;
    if (!takeBufferIndex(index))
    {
        return false;
    }

    out = makeFrame(index);
    return true;
}

bool V4l2Sink::pushFrame(uvap::Frame frame)
{
    if (!streaming_ || !pool_)
    {
        throw std::logic_error("V4l2Sink: pushFrame requires a streaming sink");
    }

    if (!frame.hasMemory())
    {
        throw std::invalid_argument("V4l2Sink: pushFrame received an empty frame");
    }

    auto* mem = dynamic_cast<BufferMemory*>(frame.memory().get());
    if (mem == nullptr || !mem->belongsTo(pool_.get()))
    {
        throw std::invalid_argument("V4l2Sink: frame was not acquired from this sink");
    }

    const uvap::FrameInfo& info = frame.info();
    if (info.width != config_.format.width ||
        info.height != config_.format.height ||
        info.pixelFormat != config_.format.pixelFormat)
    {
        throw std::invalid_argument(
            "V4l2Sink: frame format does not match negotiated sink format");
    }

    const std::uint32_t index = mem->index();
    const std::size_t bytesUsed = negotiatedSizeImage_;
    const std::size_t mappedLength = pool_->slots[index].mapping.length;
    if (bytesUsed == 0 || bytesUsed > mappedLength ||
        bytesUsed > std::numeric_limits<__u32>::max())
    {
        throw std::runtime_error("V4l2Sink: negotiated payload exceeds mapped buffer");
    }

    v4l2_buffer buf{};
    buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = index;
    buf.bytesused = static_cast<__u32>(bytesUsed);

    if (info.timestampNs > 0)
    {
        buf.timestamp.tv_sec = static_cast<long>(info.timestampNs / 1000000000LL);
        buf.timestamp.tv_usec =
            static_cast<long>((info.timestampNs % 1000000000LL) / 1000LL);
    }

    if (!pool_->transition(index,
                           BufferPool::SlotState::Dequeued,
                           BufferPool::SlotState::Queued))
    {
        throw std::logic_error("V4l2Sink: frame lease is not dequeued");
    }

    if (ioctlRetry(fd_, VIDIOC_QBUF, &buf) < 0)
    {
        const int error = errno;
        pool_->transition(index,
                          BufferPool::SlotState::Queued,
                          BufferPool::SlotState::Free);
        if (error == EAGAIN || error == EFAULT)
        {
            return false;
        }
        throw std::system_error(error, std::generic_category(), "V4l2Sink: VIDIOC_QBUF");
    }

    return true;
}

bool V4l2Sink::takeBufferIndex(std::uint32_t& index)
{
    if (!streaming_ || !pool_ || fd_ < 0)
    {
        return false;
    }

    {
        if (pool_->acquireFree(index))
        {
            return true;
        }
    }

    v4l2_buffer dq{};
    dq.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    dq.memory = V4L2_MEMORY_MMAP;

    if (ioctlRetry(fd_, VIDIOC_DQBUF, &dq) < 0)
    {
        // v4l2loopback may report EFAULT for an empty/disconnected OUTPUT list.
        if (errno == EAGAIN || errno == EFAULT)
        {
            return false;
        }
        throwSystemError("V4l2Sink: VIDIOC_DQBUF");
    }

    if (dq.index >= pool_->slots.size())
    {
        throw std::runtime_error("V4l2Sink: DQBUF returned invalid index");
    }

    if (!pool_->transition(dq.index,
                           BufferPool::SlotState::Queued,
                           BufferPool::SlotState::Dequeued))
    {
        throw std::runtime_error("V4l2Sink: DQBUF returned a buffer not owned by device");
    }
    index = dq.index;
    return true;
}

uvap::Frame V4l2Sink::makeFrame(std::uint32_t index)
{
    uvap::FrameInfo info = config_.format;
    auto memory = std::make_unique<BufferMemory>(pool_, index);
    return uvap::Frame{std::move(info), std::move(memory)};
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
                       config_.format.height,
                       config_.format.strideBytes);
    if (sizeImage == 0 || sizeImage > std::numeric_limits<__u32>::max())
    {
        throw std::runtime_error("V4l2Sink: invalid or oversized frame layout");
    }

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

    if (ioctlRetry(fd_, VIDIOC_S_FMT, &fmt) < 0)
    {
        throwSystemError("V4l2Sink: VIDIOC_S_FMT");
    }

    const auto negotiatedFormat = fromV4l2Fourcc(fmt.fmt.pix.pixelformat);
    if (!negotiatedFormat)
    {
        throw std::runtime_error("V4l2Sink: driver negotiated unsupported pixel format");
    }
    if (fmt.fmt.pix.width > static_cast<__u32>(std::numeric_limits<int>::max()) ||
        fmt.fmt.pix.height > static_cast<__u32>(std::numeric_limits<int>::max()) ||
        fmt.fmt.pix.bytesperline >
            static_cast<__u32>(std::numeric_limits<int>::max()))
    {
        throw std::runtime_error("V4l2Sink: negotiated geometry exceeds API limits");
    }

    config_.format.width = static_cast<int>(fmt.fmt.pix.width);
    config_.format.height = static_cast<int>(fmt.fmt.pix.height);
    config_.format.pixelFormat = *negotiatedFormat;
    config_.format.strideBytes = static_cast<int>(fmt.fmt.pix.bytesperline);

    const std::size_t minimumSize =
        imageSizeBytes(config_.format.pixelFormat,
                       config_.format.width,
                       config_.format.height,
                       config_.format.strideBytes);
    if (minimumSize == 0)
    {
        throw std::runtime_error("V4l2Sink: driver negotiated invalid frame layout");
    }
    negotiatedSizeImage_ =
        fmt.fmt.pix.sizeimage > 0 ? static_cast<std::size_t>(fmt.fmt.pix.sizeimage)
                                  : minimumSize;
    if (negotiatedSizeImage_ < minimumSize)
    {
        throw std::runtime_error("V4l2Sink: driver sizeimage is smaller than frame layout");
    }
}

void V4l2Sink::requestAndMapBuffers()
{
    // Ensure any previous buffer set is released before reallocating.
    reqbufsZero();

    v4l2_requestbuffers req{};
    req.count = static_cast<__u32>(config_.queueDepth);
    req.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    req.memory = V4L2_MEMORY_MMAP;

    if (ioctlRetry(fd_, VIDIOC_REQBUFS, &req) < 0)
    {
        throwSystemError("V4l2Sink: VIDIOC_REQBUFS");
    }

    if (req.count < 2)
    {
        throw std::runtime_error("V4l2Sink: driver returned fewer than 2 buffers");
    }
    config_.queueDepth = static_cast<int>(req.count);

    pool_ = std::make_shared<BufferPool>();
    pool_->slots.resize(req.count);

    for (__u32 i = 0; i < req.count; ++i)
    {
        v4l2_buffer buf{};
        buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (ioctlRetry(fd_, VIDIOC_QUERYBUF, &buf) < 0)
        {
            throwSystemError("V4l2Sink: VIDIOC_QUERYBUF");
        }
        if (buf.length < negotiatedSizeImage_)
        {
            throw std::runtime_error(
                "V4l2Sink: mapped buffer is smaller than negotiated sizeimage");
        }

        void* start = ::mmap(nullptr,
                             buf.length,
                             PROT_READ | PROT_WRITE,
                             MAP_SHARED,
                             fd_,
                             buf.m.offset);
        if (start == MAP_FAILED)
        {
            throwSystemError("V4l2Sink: mmap");
        }

        pool_->slots[i].mapping.start = start;
        pool_->slots[i].mapping.length = buf.length;
        pool_->slots[i].state = BufferPool::SlotState::Free;
        pool_->freeIndices.push_back(i);
    }
}

void V4l2Sink::streamOn()
{
    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    if (ioctlRetry(fd_, VIDIOC_STREAMON, &type) < 0)
    {
        throwSystemError("V4l2Sink: VIDIOC_STREAMON");
    }
}

void V4l2Sink::streamOff()
{
    if (fd_ < 0 || !streaming_)
    {
        return;
    }

    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    if (ioctlRetry(fd_, VIDIOC_STREAMOFF, &type) < 0)
    {
        throwSystemError("V4l2Sink: VIDIOC_STREAMOFF");
    }
    streaming_ = false;
}

void V4l2Sink::streamOffNoexcept() noexcept
{
    if (fd_ < 0 || !streaming_)
    {
        return;
    }
    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    ioctlRetry(fd_, VIDIOC_STREAMOFF, &type);
    streaming_ = false;
}

void V4l2Sink::reqbufsZero()
{
    if (fd_ < 0)
    {
        return;
    }

    v4l2_requestbuffers req{};
    req.count = 0;
    req.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    req.memory = V4L2_MEMORY_MMAP;
    if (ioctlRetry(fd_, VIDIOC_REQBUFS, &req) < 0)
    {
        throwSystemError("V4l2Sink: VIDIOC_REQBUFS(0)");
    }
}

void V4l2Sink::reqbufsZeroNoexcept() noexcept
{
    if (fd_ < 0)
    {
        return;
    }
    v4l2_requestbuffers request{};
    request.count = 0;
    request.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    request.memory = V4L2_MEMORY_MMAP;
    ioctlRetry(fd_, VIDIOC_REQBUFS, &request);
}

void V4l2Sink::releaseMappedBuffers() noexcept
{
    pool_.reset();
}

} // namespace uvap::v4l2
