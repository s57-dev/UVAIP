#include "PixelFormatMap.h"

#include <linux/videodev2.h>

namespace uvap::v4l2
{

std::optional<std::uint32_t> toV4l2Fourcc(uvap::PixelFormat format)
{
    switch (format)
    {
    case uvap::PixelFormat::Bgr24:
        return V4L2_PIX_FMT_BGR24;
    case uvap::PixelFormat::Rgb24:
        return V4L2_PIX_FMT_RGB24;
    case uvap::PixelFormat::Rgba32:
        return V4L2_PIX_FMT_RGB32;
    case uvap::PixelFormat::Bgra32:
        return V4L2_PIX_FMT_BGR32;
    case uvap::PixelFormat::Gray8:
        return V4L2_PIX_FMT_GREY;
    case uvap::PixelFormat::Nv12:
        return V4L2_PIX_FMT_NV12;
    case uvap::PixelFormat::Yuv420:
        return V4L2_PIX_FMT_YUV420;
    case uvap::PixelFormat::Yuv422:
        return V4L2_PIX_FMT_YUV422P;
    case uvap::PixelFormat::Nv16:
        return V4L2_PIX_FMT_NV16;
    case uvap::PixelFormat::Yuyv:
        return V4L2_PIX_FMT_YUYV;
    case uvap::PixelFormat::Uyvy:
        return V4L2_PIX_FMT_UYVY;
    }
    return std::nullopt;
}

std::optional<uvap::PixelFormat> fromV4l2Fourcc(std::uint32_t fourcc)
{
    switch (fourcc)
    {
    case V4L2_PIX_FMT_BGR24:
        return uvap::PixelFormat::Bgr24;
    case V4L2_PIX_FMT_RGB24:
        return uvap::PixelFormat::Rgb24;
    case V4L2_PIX_FMT_RGB32:
        return uvap::PixelFormat::Rgba32;
    case V4L2_PIX_FMT_BGR32:
        return uvap::PixelFormat::Bgra32;
    case V4L2_PIX_FMT_GREY:
        return uvap::PixelFormat::Gray8;
    case V4L2_PIX_FMT_NV12:
        return uvap::PixelFormat::Nv12;
    case V4L2_PIX_FMT_YUV420:
        return uvap::PixelFormat::Yuv420;
    case V4L2_PIX_FMT_YUV422P:
        return uvap::PixelFormat::Yuv422;
    case V4L2_PIX_FMT_NV16:
        return uvap::PixelFormat::Nv16;
    case V4L2_PIX_FMT_YUYV:
        return uvap::PixelFormat::Yuyv;
    case V4L2_PIX_FMT_UYVY:
        return uvap::PixelFormat::Uyvy;
    default:
        return std::nullopt;
    }
}

std::size_t imageSizeBytes(uvap::PixelFormat format, int width, int height)
{
    if (width <= 0 || height <= 0)
    {
        return 0;
    }

    const std::size_t w = static_cast<std::size_t>(width);
    const std::size_t h = static_cast<std::size_t>(height);

    switch (format)
    {
    case uvap::PixelFormat::Gray8:
        return w * h;
    case uvap::PixelFormat::Rgb24:
    case uvap::PixelFormat::Bgr24:
        return w * h * 3;
    case uvap::PixelFormat::Rgba32:
    case uvap::PixelFormat::Bgra32:
        return w * h * 4;
    case uvap::PixelFormat::Nv12:
    case uvap::PixelFormat::Yuv420:
        return w * h * 3 / 2;
    case uvap::PixelFormat::Yuv422:
    case uvap::PixelFormat::Nv16:
    case uvap::PixelFormat::Yuyv:
    case uvap::PixelFormat::Uyvy:
        return w * h * 2;
    }
    return 0;
}

} // namespace uvap::v4l2
