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

#include "IFrameProducer.h"
#include "IFrameSink.h"
#include "IScaler.h"

#include <chrono>
#include <cstdint>

namespace uvap
{

enum class PumpResult
{
    FrameQueued,
    SourceTimeout,
    Backpressure,
};

struct PipelineStats
{
    std::uint64_t framesQueued = 0;
    std::uint64_t sourceTimeouts = 0;
    std::uint64_t backpressureEvents = 0;
    std::uint64_t errors = 0;
};

/**
 * Transport-independent producer → transform → sink data path.
 *
 * Session lifecycle, consumer presence, and renegotiation are policy owned by
 * the application or a platform session adapter.
 */
class FramePipeline
{
public:
    FramePipeline(IFrameProducer& producer, IScaler& scaler, IFrameSink& sink)
        : producer_(producer), scaler_(scaler), sink_(sink)
    {
    }

    PumpResult pump(std::chrono::milliseconds sourceTimeout);
    const PipelineStats& stats() const { return stats_; }
    void resetStats() { stats_ = {}; }

private:
    IFrameProducer& producer_;
    IScaler& scaler_;
    IFrameSink& sink_;
    PipelineStats stats_{};
};

} // namespace uvap
