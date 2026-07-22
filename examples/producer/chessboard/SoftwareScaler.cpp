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
    if (!frame.isCpuMapped() || frame.handle() == nullptr)
    {
        throw std::runtime_error("SoftwareScaler: frame is not CPU-mapped");
    }

    const auto& info = frame.info();
    return cv::Mat(info.height,
                   info.width,
                   cvTypeFor(info.pixelFormat),
                   frame.handle());
}

} // namespace

void SoftwareScaler::configure(const uvap::ScaleConfig& config)
{
    if (config.width <= 0 || config.height <= 0)
    {
        throw std::runtime_error("SoftwareScaler: invalid output dimensions");
    }
    if (config.rotationDegrees != 0)
    {
        throw std::runtime_error("SoftwareScaler: rotation is not supported in v1");
    }
    config_ = config;
}

uvap::ScaleConfig SoftwareScaler::configuration() const
{
    return config_;
}

uvap::Frame SoftwareScaler::scale(const uvap::Frame& input,
                                  const uvap::Frame& output,
                                  const uvap::ScaleCallback& callback)
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

    cv::resize(converted, dst, dst.size(), 0, 0, cv::INTER_LINEAR);

    // Preserve timing / index metadata on the output buffer.
    uvap::Frame result = output;
    result.info().timestampNs = input.info().timestampNs;
    result.info().frameIndex = input.info().frameIndex;

    if (callback)
    {
        callback(input, result);
    }

    return result;
}

} // namespace uvap::example
