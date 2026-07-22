#pragma once

#include "IFrameSink.h"

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace uvap::v4l2
{

/**
 * Zero-copy V4L2 VIDEO_OUTPUT sink (e.g. v4l2loopback).
 *
 * Buffers are MMAP'd once, then cycled: acquireFrame → fill → pushFrame →
 * (device) → DQBUF → acquireFrame again. No CPU memcpy on the publish path.
 */
class V4l2Sink final : public uvap::IFrameSink
{
public:
    explicit V4l2Sink(std::string devicePath);
    ~V4l2Sink() override;

    V4l2Sink(const V4l2Sink&) = delete;
    V4l2Sink& operator=(const V4l2Sink&) = delete;

    void configure(const uvap::SinkConfig& config) override;
    uvap::SinkConfig configuration() const override;

    void start() override;
    void stop() override;

    bool acquireFrame(uvap::Frame& out) override;
    bool pushFrame(uvap::Frame frame) override;

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

    void releaseBuffers() noexcept;
    void setFormat();
    void requestAndMapBuffers();
    void streamOn();
    void streamOff() noexcept;
    bool takeBufferIndex(std::uint32_t& index);
    uvap::Frame makeFrame(std::uint32_t index);

    std::string devicePath_;
    uvap::SinkConfig config_{};
    bool streaming_ = false;
    std::shared_ptr<BufferPool> pool_;
};

} // namespace uvap::v4l2
