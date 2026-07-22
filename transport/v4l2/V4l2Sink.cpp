#include "V4l2Sink.h"

#include "PixelFormatMap.h"

#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/poll.h>
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
    closeDevice();
}

void V4l2Sink::openDevice()
{
    if (fd_ >= 0)
    {
        return;
    }

    fd_ = ::open(devicePath_.c_str(), O_RDWR | O_NONBLOCK);
    if (fd_ < 0)
    {
        throw std::runtime_error(errnoMessage("V4l2Sink: open " + devicePath_));
    }
}

void V4l2Sink::closeDevice() noexcept
{
    releaseMappedBuffers();
    streaming_ = false;
    clientUsageSubscribed_ = false;
    if (fd_ >= 0)
    {
        ::close(fd_);
        fd_ = -1;
    }
}

void V4l2Sink::subscribeClientUsage()
{
    openDevice();

    v4l2_event_subscription sub{};
    sub.type = kV4l2EventPriClientUsage;
    sub.flags = V4L2_EVENT_SUB_FL_SEND_INITIAL;
    if (::ioctl(fd_, VIDIOC_SUBSCRIBE_EVENT, &sub) < 0)
    {
        throw std::runtime_error(errnoMessage("V4l2Sink: SUBSCRIBE_EVENT CLIENT_USAGE"));
    }
    clientUsageSubscribed_ = true;
}

std::optional<ClientUsageEvent> V4l2Sink::dequeueClientUsageEvent(bool nonblock)
{
    if (fd_ < 0)
    {
        return std::nullopt;
    }

    if (!nonblock)
    {
        // Caller already waited via poll.
    }

    v4l2_event ev{};
    if (::ioctl(fd_, VIDIOC_DQEVENT, &ev) < 0)
    {
        // Empty queue: EAGAIN is the usual non-blocking code; some
        // v4l2loopback/kernel combos return ENOENT instead.
        if (errno == EAGAIN || errno == ENOENT)
        {
            return std::nullopt;
        }
        throw std::runtime_error(errnoMessage("V4l2Sink: VIDIOC_DQEVENT"));
    }

    if (ev.type != kV4l2EventPriClientUsage)
    {
        return std::nullopt;
    }

    ClientUsageEvent out{};
    static_assert(sizeof(out.count) <= sizeof(ev.u.data));
    std::memcpy(&out.count, ev.u.data, sizeof(out.count));
    return out;
}

std::optional<ClientUsageEvent> V4l2Sink::pollClientUsage()
{
    return dequeueClientUsageEvent(true);
}

std::optional<ClientUsageEvent> V4l2Sink::waitClientUsage(std::chrono::milliseconds timeout)
{
    openDevice();
    if (!clientUsageSubscribed_)
    {
        subscribeClientUsage();
    }

    pollfd pfd{};
    pfd.fd = fd_;
    pfd.events = POLLPRI;

    const int timeoutMs = timeout.count() < 0 ? -1 : static_cast<int>(timeout.count());
    const int rc = ::poll(&pfd, 1, timeoutMs);
    if (rc < 0)
    {
        if (errno == EINTR)
        {
            return std::nullopt;
        }
        throw std::runtime_error(errnoMessage("V4l2Sink: poll CLIENT_USAGE"));
    }
    if (rc == 0)
    {
        return std::nullopt;
    }

    return dequeueClientUsageEvent(false);
}

uvap::FrameInfo V4l2Sink::getFormat() const
{
    if (fd_ < 0)
    {
        throw std::runtime_error("V4l2Sink: getFormat requires open device");
    }

    v4l2_format fmt{};
    fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    if (::ioctl(fd_, VIDIOC_G_FMT, &fmt) < 0)
    {
        throw std::runtime_error(errnoMessage("V4l2Sink: VIDIOC_G_FMT"));
    }

    uvap::FrameInfo info{};
    info.width = static_cast<int>(fmt.fmt.pix.width);
    info.height = static_cast<int>(fmt.fmt.pix.height);
    info.strideBytes = static_cast<int>(fmt.fmt.pix.bytesperline);
    const auto mapped = fromV4l2Fourcc(fmt.fmt.pix.pixelformat);
    info.pixelFormat = mapped.value_or(uvap::PixelFormat::Bgr24);
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
    if (::ioctl(fd_, VIDIOC_G_PARM, &parm) < 0)
    {
        throw std::runtime_error(errnoMessage("V4l2Sink: VIDIOC_G_PARM"));
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
    if (::ioctl(fd_, VIDIOC_G_PARM, &sp) < 0)
    {
        throw std::runtime_error(errnoMessage("V4l2Sink: VIDIOC_G_PARM"));
    }

    sp.parm.output.timeperframe.numerator =
        static_cast<__u32>(parm.fpsDenominator > 0 ? parm.fpsDenominator : 1);
    sp.parm.output.timeperframe.denominator =
        static_cast<__u32>(parm.fpsNumerator > 0 ? parm.fpsNumerator : 30);

    if (::ioctl(fd_, VIDIOC_S_PARM, &sp) < 0)
    {
        throw std::runtime_error(errnoMessage("V4l2Sink: VIDIOC_S_PARM"));
    }
}

void V4l2Sink::yield()
{
    // Stop acquire/push immediately so callers do not DQBUF during teardown.
    streaming_ = false;
    streamOff();
    releaseMappedBuffers();
    reqbufsZero();
}

void V4l2Sink::recycleBuffers()
{
    if (!pool_ || fd_ < 0)
    {
        return;
    }

    streamOff();
    {
        std::lock_guard<std::mutex> lock(pool_->mutex);
        pool_->freeIndices.clear();
        for (std::uint32_t i = 0; i < pool_->buffers.size(); ++i)
        {
            pool_->freeIndices.push_back(i);
        }
    }
    streamOn();
    streaming_ = true;
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
        reqbufsZero();
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
        return false;
    }

    if (!frame.hasMemory() || frame.memory().use_count() != 1)
    {
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

    mem->transitionTo(uvap::BufferState::Queued);
    frame = uvap::Frame{};

    if (::ioctl(fd_, VIDIOC_QBUF, &buf) < 0)
    {
        pool_->returnFree(index);
        if (errno == EAGAIN || errno == EFAULT || errno == EIO ||
            errno == ENODEV || errno == EINVAL)
        {
            return false;
        }
        throw std::runtime_error(errnoMessage("V4l2Sink: VIDIOC_QBUF"));
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

    if (::ioctl(fd_, VIDIOC_DQBUF, &dq) < 0)
    {
        // EAGAIN: nothing ready yet (O_NONBLOCK).
        // EFAULT/EIO/ENODEV/EINVAL: common around consumer disconnect /
        // STREAMOFF on v4l2loopback — treat as transient backpressure.
        if (errno == EAGAIN || errno == EFAULT || errno == EIO ||
            errno == ENODEV || errno == EINVAL)
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

    if (::ioctl(fd_, VIDIOC_S_FMT, &fmt) < 0)
    {
        throw std::runtime_error(errnoMessage("V4l2Sink: VIDIOC_S_FMT"));
    }
}

void V4l2Sink::requestAndMapBuffers()
{
    // Ensure any previous buffer set is released before reallocating.
    reqbufsZero();

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

    pool_ = std::make_shared<BufferPool>();
    pool_->fd = fd_;
    pool_->buffers.resize(req.count);

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

        pool_->buffers[i].start = start;
        pool_->buffers[i].length = buf.length;
        pool_->freeIndices.push_back(i);
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

void V4l2Sink::reqbufsZero() noexcept
{
    if (fd_ < 0)
    {
        return;
    }

    v4l2_requestbuffers req{};
    req.count = 0;
    req.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    req.memory = V4L2_MEMORY_MMAP;
    ::ioctl(fd_, VIDIOC_REQBUFS, &req);
}

void V4l2Sink::releaseMappedBuffers() noexcept
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
    pool_->fd = -1;
    pool_.reset();
}

} // namespace uvap::v4l2
