#pragma once

#include "Frame.h"

#include <chrono>

namespace uvap
{

/**
 * VAL-facing source of decoded or synthetic video frames.
 *
 * Pull-based: callers obtain frames via getFrame(). Implementations may wrap
 * a software decoder, synthetic generator, or a platform decode tap.
 */
class IFrameProducer
{
public:
    virtual ~IFrameProducer() = default;

    virtual void start() = 0;
    virtual void stop() = 0;

    /**
     * Wait up to timeout for the next frame.
     * Returns true and fills out on success; false on stop or timeout.
     */
    virtual bool getFrame(Frame& out, std::chrono::milliseconds timeout) = 0;

    /** Frame info of the frame produced by this producer. */
    virtual FrameInfo frameInfo() const = 0;
}; 

} // namespace uvap