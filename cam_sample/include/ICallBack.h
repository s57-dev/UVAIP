#pragma once

#include <opencv2/opencv.hpp>

class ICallback
{
public:
    virtual ~ICallback() = default;

    virtual void onFrame(const cv::Mat& frame) = 0;
};