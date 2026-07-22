#include "V4l2Sink.h"

#include "PixelFormatMap.h"

#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace uvap::v4l2
{
namespace
{

std::string errnoMessage(const std::string& prefix)
{
    return prefix + ": " + std::strerror(errno);
}

} // namespace

/**
 * IMemory specialization for one V4L2 MMAP OUTPUT buffer.
 *
 * State machine (producer / transport side):
 *   Dequeued — after acquireFrame; producer may fill
 *   Queued   — after pushFrame / QBUF; device owns until DQBUF
 * Destroying while Dequeued returns the slot to Free in the pool.
 */
class V4l2Sink::BufferMemory final : public uvap::IMemory
{
public:
    BufferMemory(std::shared_ptr<BufferPool> pool, std::uint32_t index)
        : pool_(std::move(pool))
        , index_(index)
        , state_(uvap::BufferState::Dequeued)
    {
    }

    ~BufferMemory() override
    {
        if (state_ == uvap::BufferState::Dequeued && pool_)
        {
            pool_->returnFree(index_);
        }
    }

    BufferMemory(const BufferMemory&) = delete;
    BufferMemory& operator=(const BufferMemory&) = delete;

    std::size_t sizeBytes() const override
    {
        return pool_->buffers[index_].length;
    }

    bool isCpuMapped() const override { return true; }

    void* handle() const override
    {
        return pool_->buffers[index_].start;
    }

    uvap::MemoryType type() const override
    {
        return uvap::MemoryType::CpuMapped;
    }

    uvap::BufferState state() const override { return state_; }

    std::uint32_t index() const { return index_; }

    bool belongsTo(const BufferPool* pool) const
    {
        return pool_.get() == pool;
    }

    void transitionTo(uvap::BufferState next)
    {
        state_ = next;
    }

private:
    std::shared_ptr<BufferPool> pool_;
    std::uint32_t index_ = 0;
    uvap::BufferState state_ = uvap::BufferState::Dequeued;
};

void V4l2Sink::BufferPool::returnFree(std::uint32_t index)
{
    std::lock_guard<std::mutex> lock(mutex);
    freeIndices.push_back(index);
}

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

    pool_ = std::make_shared<BufferPool>();
    pool_->fd = ::open(devicePath_.c_str(), O_RDWR | O_NONBLOCK);
    if (pool_->fd < 0)
    {
        pool_.reset();
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
        throw;
    }
}

void V4l2Sink::stop()
{
    if (!streaming_ && !pool_)
    {
        return;
    }

    streamOff();
    releaseBuffers();
    streaming_ = false;
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
        return false;
    }

    if (!frame.hasMemory() || frame.memory().use_count() != 1)
    {
        // Must be the sole owner of a sink buffer (no shared copies).
        return false;
    }

    auto* mem = dynamic_cast<BufferMemory*>(frame.memory().get());
    if (mem == nullptr || !mem->belongsTo(pool_.get()))
    {
        return false;
    }

    if (mem->state() != uvap::BufferState::Dequeued)
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

    const std::uint32_t index = mem->index();
    const std::size_t bytesUsed =
        imageSizeBytes(info.pixelFormat, info.width, info.height);

    v4l2_buffer buf{};
    buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = index;
    buf.bytesused = static_cast<__u32>(
        bytesUsed > 0 ? bytesUsed : pool_->buffers[index].length);

    if (info.timestampNs > 0)
    {
        buf.timestamp.tv_sec = static_cast<long>(info.timestampNs / 1000000000LL);
        buf.timestamp.tv_usec =
            static_cast<long>((info.timestampNs % 1000000000LL) / 1000LL);
    }

    // Transfer ownership to the driver before QBUF so Frame dtor does not
    // treat the buffer as Dequeued and return it to the free list.
    mem->transitionTo(uvap::BufferState::Queued);
    frame = uvap::Frame{};

    if (::ioctl(pool_->fd, VIDIOC_QBUF, &buf) < 0)
    {
        // QBUF failed: put the slot back so it can be acquired again.
        pool_->returnFree(index);
        throw std::runtime_error(errnoMessage("V4l2Sink: VIDIOC_QBUF"));
    }

    return true;
}

bool V4l2Sink::takeBufferIndex(std::uint32_t& index)
{
    {
        std::lock_guard<std::mutex> lock(pool_->mutex);
        if (!pool_->freeIndices.empty())
        {
            index = pool_->freeIndices.front();
            pool_->freeIndices.pop_front();
            return true;
        }
    }

    v4l2_buffer dq{};
    dq.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    dq.memory = V4L2_MEMORY_MMAP;

    if (::ioctl(pool_->fd, VIDIOC_DQBUF, &dq) < 0)
    {
        if (errno == EAGAIN)
        {
            return false;
        }
        throw std::runtime_error(errnoMessage("V4l2Sink: VIDIOC_DQBUF"));
    }

    if (dq.index >= pool_->buffers.size())
    {
        throw std::runtime_error("V4l2Sink: DQBUF returned invalid index");
    }

    index = dq.index;
    return true;
}

uvap::Frame V4l2Sink::makeFrame(std::uint32_t index)
{
    uvap::FrameInfo info = config_.format;
    auto memory = std::make_shared<BufferMemory>(pool_, index);
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

    if (::ioctl(pool_->fd, VIDIOC_S_FMT, &fmt) < 0)
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

    if (::ioctl(pool_->fd, VIDIOC_REQBUFS, &req) < 0)
    {
        throw std::runtime_error(errnoMessage("V4l2Sink: VIDIOC_REQBUFS"));
    }

    if (req.count < 2)
    {
        throw std::runtime_error("V4l2Sink: driver returned fewer than 2 buffers");
    }

    pool_->buffers.resize(req.count);

    for (__u32 i = 0; i < req.count; ++i)
    {
        v4l2_buffer buf{};
        buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (::ioctl(pool_->fd, VIDIOC_QUERYBUF, &buf) < 0)
        {
            throw std::runtime_error(errnoMessage("V4l2Sink: VIDIOC_QUERYBUF"));
        }

        void* start = ::mmap(nullptr,
                             buf.length,
                             PROT_READ | PROT_WRITE,
                             MAP_SHARED,
                             pool_->fd,
                             buf.m.offset);
        if (start == MAP_FAILED)
        {
            throw std::runtime_error(errnoMessage("V4l2Sink: mmap"));
        }

        pool_->buffers[i].start = start;
        pool_->buffers[i].length = buf.length;
        pool_->freeIndices.push_back(i);
    }
}

void V4l2Sink::streamOn()
{
    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    if (::ioctl(pool_->fd, VIDIOC_STREAMON, &type) < 0)
    {
        throw std::runtime_error(errnoMessage("V4l2Sink: VIDIOC_STREAMON"));
    }
}

void V4l2Sink::streamOff() noexcept
{
    if (!pool_ || pool_->fd < 0)
    {
        return;
    }

    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    ::ioctl(pool_->fd, VIDIOC_STREAMOFF, &type);
}

void V4l2Sink::releaseBuffers() noexcept
{
    if (!pool_)
    {
        return;
    }

    for (MappedBuffer& buffer : pool_->buffers)
    {
        if (buffer.start != nullptr && buffer.length > 0)
        {
            ::munmap(buffer.start, buffer.length);
        }
        buffer = {};
    }
    pool_->buffers.clear();
    pool_->freeIndices.clear();

    if (pool_->fd >= 0)
    {
        ::close(pool_->fd);
        pool_->fd = -1;
    }

    pool_.reset();
}

} // namespace uvap::v4l2
