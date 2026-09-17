/*
 * Copyright 2026 S57 ApS
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

#include "SoftwareScaler.h"

#include <opencv2/imgproc.hpp>

#include <stdexcept>

namespace uvap::example
{
namespace
{

int cvTypeFor(uvap::PixelFormat format)
{
    switch (format)
    {
    case uvap::PixelFormat::Gray8:
        return CV_8UC1;
    case uvap::PixelFormat::Bgr24:
    case uvap::PixelFormat::Rgb24:
        return CV_8UC3;
    case uvap::PixelFormat::Bgra32:
    case uvap::PixelFormat::Rgba32:
        return CV_8UC4;
    default:
        throw std::runtime_error("SoftwareScaler: unsupported pixel format for OpenCV view");
    }
}

cv::Mat asMat(const uvap::Frame& frame)
{
    if (!frame.isCpuMapped() || frame.data() == nullptr)
    {
        throw std::runtime_error("SoftwareScaler: frame is not CPU-mapped");
    }

    const auto& info = frame.info();
    const std::size_t step =
        info.strideBytes > 0 ? static_cast<std::size_t>(info.strideBytes) : cv::Mat::AUTO_STEP;
    return cv::Mat(
        info.height, info.width, cvTypeFor(info.pixelFormat), frame.data(), step);
}

} // namespace

void SoftwareScaler::configure(const uvap::ScaleConfig& config)
{
    if (config.width <= 0 || config.height <= 0)
    {
        throw std::runtime_error("SoftwareScaler: invalid output dimensions");
    }
    if (config.rotationDegrees != 0 && config.rotationDegrees != 90 &&
        config.rotationDegrees != 180 && config.rotationDegrees != 270)
    {
        throw std::runtime_error("SoftwareScaler: rotation must be 0, 90, 180, or 270");
    }
    config_ = config;
}

uvap::ScaleConfig SoftwareScaler::configuration() const
{
    return config_;
}

void SoftwareScaler::scale(const uvap::Frame& input, uvap::Frame& output)
{
    if (config_.width <= 0 || config_.height <= 0)
    {
        throw std::runtime_error("SoftwareScaler: configure() before scale()");
    }

    if (!input.isCpuMapped() || !output.isCpuMapped())
    {
        throw std::runtime_error("SoftwareScaler: input and output must be CPU-mapped");
    }

    if (output.info().width != config_.width ||
        output.info().height != config_.height ||
        output.info().pixelFormat != config_.outputFormat)
    {
        throw std::runtime_error("SoftwareScaler: output Frame does not match ScaleConfig");
    }

    cv::Mat src = asMat(input);
    cv::Mat dst = asMat(output);

    cv::Mat cropped = src;
    if (config_.cropW > 0 && config_.cropH > 0)
    {
        const cv::Rect roi(config_.cropX,
                           config_.cropY,
                           config_.cropW,
                           config_.cropH);
        if (roi.x < 0 || roi.y < 0 ||
            roi.x + roi.width > src.cols ||
            roi.y + roi.height > src.rows)
        {
            throw std::runtime_error("SoftwareScaler: crop ROI out of bounds");
        }
        cropped = src(roi);
    }

    cv::Mat converted = cropped;
    if (input.info().pixelFormat != config_.outputFormat)
    {
        if (input.info().pixelFormat == uvap::PixelFormat::Bgr24 &&
            config_.outputFormat == uvap::PixelFormat::Rgb24)
        {
            cv::cvtColor(cropped, converted, cv::COLOR_BGR2RGB);
        }
        else if (input.info().pixelFormat == uvap::PixelFormat::Rgb24 &&
                 config_.outputFormat == uvap::PixelFormat::Bgr24)
        {
            cv::cvtColor(cropped, converted, cv::COLOR_RGB2BGR);
        }
        else if (input.info().pixelFormat == uvap::PixelFormat::Gray8 &&
                 config_.outputFormat == uvap::PixelFormat::Bgr24)
        {
            cv::cvtColor(cropped, converted, cv::COLOR_GRAY2BGR);
        }
        else
        {
            throw std::runtime_error("SoftwareScaler: unsupported color conversion");
        }
    }

    cv::Mat rotated = converted;
    switch (config_.rotationDegrees)
    {
    case 0:
        break;
    case 90:
        cv::rotate(converted, rotated, cv::ROTATE_90_CLOCKWISE);
        break;
    case 180:
        cv::rotate(converted, rotated, cv::ROTATE_180);
        break;
    case 270:
        cv::rotate(converted, rotated, cv::ROTATE_90_COUNTERCLOCKWISE);
        break;
    default:
        throw std::logic_error("SoftwareScaler: invalid configured rotation");
    }

    cv::resize(rotated, dst, dst.size(), 0, 0, cv::INTER_LINEAR);

    output.info().timestampNs = input.info().timestampNs;
    output.info().frameIndex = input.info().frameIndex;
}

} // namespace uvap::example
