/*
 * Copyright 2026 S57 ApS
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

#include "PixelFormatMap.h"

#include <linux/videodev2.h>

#include <limits>

namespace uvap::v4l2
{
namespace
{

bool checkedMultiply(std::size_t a, std::size_t b, std::size_t& out)
{
    if (a != 0 && b > std::numeric_limits<std::size_t>::max() / a)
    {
        return false;
    }
    out = a * b;
    return true;
}

bool checkedAdd(std::size_t a, std::size_t b, std::size_t& out)
{
    if (b > std::numeric_limits<std::size_t>::max() - a)
    {
        return false;
    }
    out = a + b;
    return true;
}

std::size_t packedSize(std::size_t width,
                       std::size_t height,
                       std::size_t bytesPerPixel,
                       int strideBytes)
{
    std::size_t minimumStride = 0;
    if (!checkedMultiply(width, bytesPerPixel, minimumStride))
    {
        return 0;
    }
    const std::size_t stride =
        strideBytes > 0 ? static_cast<std::size_t>(strideBytes) : minimumStride;
    if (stride < minimumStride)
    {
        return 0;
    }
    std::size_t size = 0;
    return checkedMultiply(stride, height, size) ? size : 0;
}

} // namespace

std::optional<std::uint32_t> toV4l2Fourcc(uvap::PixelFormat format)
{
    switch (format)
    {
    case uvap::PixelFormat::Bgr24:
        return V4L2_PIX_FMT_BGR24;
    case uvap::PixelFormat::Rgb24:
        return V4L2_PIX_FMT_RGB24;
    case uvap::PixelFormat::Rgba32:
    case uvap::PixelFormat::Bgra32:
        // V4L2's legacy RGB32/BGR32 aliases do not specify the same
        // byte-order/alpha contract as these UVAP formats.
        return std::nullopt;
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

std::size_t imageSizeBytes(uvap::PixelFormat format,
                           int width,
                           int height,
                           int strideBytes)
{
    if (width <= 0 || height <= 0 || strideBytes < 0)
    {
        return 0;
    }

    const std::size_t w = static_cast<std::size_t>(width);
    const std::size_t h = static_cast<std::size_t>(height);

    switch (format)
    {
    case uvap::PixelFormat::Gray8:
        return packedSize(w, h, 1, strideBytes);
    case uvap::PixelFormat::Rgb24:
    case uvap::PixelFormat::Bgr24:
        return packedSize(w, h, 3, strideBytes);
    case uvap::PixelFormat::Rgba32:
    case uvap::PixelFormat::Bgra32:
        return packedSize(w, h, 4, strideBytes);
    case uvap::PixelFormat::Nv12:
    {
        if ((width % 2) != 0 || (height % 2) != 0)
        {
            return 0;
        }
        const std::size_t stride =
            strideBytes > 0 ? static_cast<std::size_t>(strideBytes) : w;
        if (stride < w)
        {
            return 0;
        }
        std::size_t y = 0;
        std::size_t uv = 0;
        std::size_t total = 0;
        return checkedMultiply(stride, h, y) &&
                       checkedMultiply(stride, h / 2, uv) &&
                       checkedAdd(y, uv, total)
                   ? total
                   : 0;
    }
    case uvap::PixelFormat::Yuv420:
    {
        if ((width % 2) != 0 || (height % 2) != 0)
        {
            return 0;
        }
        const std::size_t stride =
            strideBytes > 0 ? static_cast<std::size_t>(strideBytes) : w;
        if (stride < w || (stride % 2) != 0)
        {
            return 0;
        }
        std::size_t y = 0;
        std::size_t chromaPlane = 0;
        std::size_t chroma = 0;
        std::size_t total = 0;
        return checkedMultiply(stride, h, y) &&
                       checkedMultiply(stride / 2, h / 2, chromaPlane) &&
                       checkedMultiply(chromaPlane, 2, chroma) &&
                       checkedAdd(y, chroma, total)
                   ? total
                   : 0;
    }
    case uvap::PixelFormat::Nv16:
    {
        if ((width % 2) != 0)
        {
            return 0;
        }
        const std::size_t stride =
            strideBytes > 0 ? static_cast<std::size_t>(strideBytes) : w;
        if (stride < w)
        {
            return 0;
        }
        std::size_t plane = 0;
        std::size_t total = 0;
        return checkedMultiply(stride, h, plane) &&
                       checkedMultiply(plane, 2, total)
                   ? total
                   : 0;
    }
    case uvap::PixelFormat::Yuv422:
    {
        if ((width % 2) != 0)
        {
            return 0;
        }
        const std::size_t stride =
            strideBytes > 0 ? static_cast<std::size_t>(strideBytes) : w;
        if (stride < w || (stride % 2) != 0)
        {
            return 0;
        }
        std::size_t y = 0;
        std::size_t chromaPlane = 0;
        std::size_t chroma = 0;
        std::size_t total = 0;
        return checkedMultiply(stride, h, y) &&
                       checkedMultiply(stride / 2, h, chromaPlane) &&
                       checkedMultiply(chromaPlane, 2, chroma) &&
                       checkedAdd(y, chroma, total)
                   ? total
                   : 0;
    }
    case uvap::PixelFormat::Yuyv:
    case uvap::PixelFormat::Uyvy:
        if ((width % 2) != 0)
        {
            return 0;
        }
        return packedSize(w, h, 2, strideBytes);
    }
    return 0;
}

} // namespace uvap::v4l2
