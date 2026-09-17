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

#include <opencv2/opencv.hpp>

class ICallback
{
public:
    virtual ~ICallback() = default;

    virtual bool onFrameCapture(const cv::Mat& frame) = 0;
};