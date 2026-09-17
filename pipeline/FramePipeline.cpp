/*
 * Copyright 2026 S57 ApS
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

#include "FramePipeline.h"

#include <utility>

namespace uvap
{

PumpResult FramePipeline::pump(std::chrono::milliseconds sourceTimeout)
{
    try
    {
        Frame output;
        if (!sink_.acquireFrame(output))
        {
            ++stats_.backpressureEvents;
            return PumpResult::Backpressure;
        }

        Frame input;
        if (!producer_.getFrame(input, sourceTimeout))
        {
            ++stats_.sourceTimeouts;
            return PumpResult::SourceTimeout;
        }

        scaler_.scale(input, output);
        if (!sink_.pushFrame(std::move(output)))
        {
            ++stats_.backpressureEvents;
            return PumpResult::Backpressure;
        }

        ++stats_.framesQueued;
        return PumpResult::FrameQueued;
    }
    catch (...)
    {
        ++stats_.errors;
        throw;
    }
}

} // namespace uvap
