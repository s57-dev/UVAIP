/*
 * Copyright 2026 S57 ApS
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

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