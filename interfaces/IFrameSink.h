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
 * Transport port with explicit buffer cycling (no copies).
 *
 * Typical producer loop:
 *   1. acquireFrame()  — take a free buffer from the sink pool
 *   2. fill frame memory (scaler / producer writes in place)
 *   3. pushFrame()     — hand the same buffer back to the transport
 *
 * Buffers return to the pool after the device releases them (DQBUF), ready
 * for the next acquireFrame().
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
     * Obtain a writable buffer owned by this sink.
     * Returns false on backpressure (all buffers in flight) or if not started.
     */
    virtual bool acquireFrame(Frame& out) = 0;

    /**
     * Queue a previously acquired frame to the transport (moves ownership).
     * The Frame must come from acquireFrame() on this sink; no pixel copy.
     * Returns false if the frame is not from this sink's pool.
     */
    virtual bool pushFrame(Frame frame) = 0;
};

} // namespace uvap
