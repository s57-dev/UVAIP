#pragma once

#include "IFrameSink.h"

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace uvap::v4l2
{

/**
 * Publishes CPU-mapped Frames to a V4L2 VIDEO_OUTPUT device (e.g. v4l2loopback)
 * using MMAP streaming buffers.
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

    bool pushFrame(const uvap::Frame& frame) override;

private:
    struct MappedBuffer
    {
        void* start = nullptr;
        std::size_t length = 0;
    };

    void releaseBuffers() noexcept;
    void setFormat();
    void requestAndMapBuffers();
    void streamOn();
    void streamOff() noexcept;

    std::string devicePath_;
    uvap::SinkConfig config_{};
    int fd_ = -1;
    bool streaming_ = false;
    std::vector<MappedBuffer> buffers_;
    std::deque<std::uint32_t> freeIndices_;
};

} // namespace uvap::v4l2
