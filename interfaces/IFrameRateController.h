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
