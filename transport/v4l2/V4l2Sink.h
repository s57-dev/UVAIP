#pragma once

#include "IFrameSink.h"

#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace uvap::v4l2
{

/**
 * Private v4l2loopback event: capture client STREAMON/STREAMOFF usage count.
 * Must match v4l2loopback/v4l2loopback.c.
 */
inline constexpr std::uint32_t kV4l2EventPriClientUsage =
    static_cast<std::uint32_t>(0x08000000) + 0x08E00000 + 1;

struct ClientUsageEvent
{
    std::uint32_t count = 0;
};

struct StreamParm
{
    int fpsNumerator = 30;
    int fpsDenominator = 1;

    double fps() const
    {
        if (fpsDenominator <= 0)
        {
            return 0.0;
        }
        return static_cast<double>(fpsNumerator) / static_cast<double>(fpsDenominator);
    }
};

/**
 * Zero-copy V4L2 VIDEO_OUTPUT sink (e.g. v4l2loopback).
 *
 * The device fd stays open across stop/yield/configure/start so the producer can
 * subscribe to CLIENT_USAGE and renegotiate format between consumer sessions.
 *
 * Buffers are MMAP'd per start(), then cycled: acquireFrame → fill → pushFrame →
 * (device) → DQBUF → acquireFrame again.
 */
class V4l2Sink final : public uvap::IFrameSink
{
public:
    explicit V4l2Sink(std::string devicePath);
    ~V4l2Sink() override;

    V4l2Sink(const V4l2Sink&) = delete;
    V4l2Sink& operator=(const V4l2Sink&) = delete;

    /** Open the device if needed (idempotent). */
    void openDevice();

    /** Subscribe to V4L2_EVENT_PRI_CLIENT_USAGE (SEND_INITIAL). */
    void subscribeClientUsage();

    /**
     * Wait for a CLIENT_USAGE event.
     * Returns nullopt on timeout; throws on device errors.
     */
    std::optional<ClientUsageEvent> waitClientUsage(
        std::chrono::milliseconds timeout);

    /** Non-blocking read of pending CLIENT_USAGE events; nullopt if none. */
    std::optional<ClientUsageEvent> pollClientUsage();

    uvap::FrameInfo getFormat() const;
    StreamParm getStreamParm() const;
    void setStreamParm(const StreamParm& parm);

    /**
     * STREAMOFF + REQBUFS(0) + unmap. Keeps the device fd open and event
     * subscription so a consumer can S_FMT before the next start().
     */
    void yield();

    /**
     * STREAMOFF, return every MMAP slot to the free list, STREAMON.
     * Use when a consumer attaches so the OUTPUT queue is not stuck after
     * idle / backpressure (v4l2loopback DQBUF returns EFAULT on empty list).
     */
    void recycleBuffers();

    void configure(const uvap::SinkConfig& config) override;
    uvap::SinkConfig configuration() const override;

    void start() override;
    void stop() override;

    bool acquireFrame(uvap::Frame& out) override;
    bool pushFrame(uvap::Frame frame) override;

    int fd() const { return fd_; }
    bool isOpen() const { return fd_ >= 0; }
    bool isStreaming() const { return streaming_; }

private:
    struct MappedBuffer
    {
        void* start = nullptr;
        std::size_t length = 0;
    };

    /** Shared pool so outstanding Frames remain valid if the sink stops carefully. */
    struct BufferPool
    {
        int fd = -1;
        std::vector<MappedBuffer> buffers;
        std::deque<std::uint32_t> freeIndices;
        std::mutex mutex;

        void returnFree(std::uint32_t index);
    };

    class BufferMemory;

    void closeDevice() noexcept;
    void releaseMappedBuffers() noexcept;
    void setFormat();
    void requestAndMapBuffers();
    void streamOn();
    void streamOff() noexcept;
    void reqbufsZero() noexcept;
    bool takeBufferIndex(std::uint32_t& index);
    uvap::Frame makeFrame(std::uint32_t index);
    std::optional<ClientUsageEvent> dequeueClientUsageEvent(bool nonblock);

    std::string devicePath_;
    int fd_ = -1;
    bool clientUsageSubscribed_ = false;
    uvap::SinkConfig config_{};
    bool streaming_ = false;
    std::shared_ptr<BufferPool> pool_;
};

} // namespace uvap::v4l2
