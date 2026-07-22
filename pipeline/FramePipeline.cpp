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
