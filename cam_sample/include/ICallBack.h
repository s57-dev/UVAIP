#pragma once

#include <opencv2/opencv.hpp>

class ICallback
{
public:
    virtual ~ICallback() = default;

    virtual bool onFrameCapture(const cv::Mat& frame) = 0;
};