#pragma once

namespace uvap
{

/** Producer-side output cadence control (REQ-05). */
class IFrameRateController
{
public:
    virtual ~IFrameRateController() = default;

    virtual void setFrameRate(int framesPerSecond) = 0;
    virtual int frameRate() const = 0;
};

} // namespace uvap
