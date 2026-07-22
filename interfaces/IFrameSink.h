#pragma once

#include "Frame.h"

namespace uvap
{

struct SinkConfig
{
    FrameInfo format{};
    // Transport hint (e.g. V4L2 REQBUFS count).
    int bufferCount = 4;
};

/**
 * Transport port: publish frames to a consumer-facing delivery path.
 * V4L2 loopback is one driver; others may be in-process queues, etc.
 */
class IFrameSink
{
public:
    virtual ~IFrameSink() = default;

    virtual void configure(const SinkConfig& config) = 0;
    virtual SinkConfig configuration() const = 0;

    virtual void start() = 0;
    virtual void stop() = 0;

    /**
     * Publish one frame.
     * Returns false on backpressure, not started, or incompatible memory.
     */
    virtual bool pushFrame(const Frame& frame) = 0;
};

} // namespace uvap
