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

#include "Frame.h"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace uvap::v4l2
{

/** Map uvap pixel format to a V4L2 fourcc; nullopt if unsupported. */
std::optional<std::uint32_t> toV4l2Fourcc(uvap::PixelFormat format);

/** Map a V4L2 fourcc to uvap pixel format; nullopt if unsupported. */
std::optional<uvap::PixelFormat> fromV4l2Fourcc(std::uint32_t fourcc);

/**
 * Minimum single-planar image size for the format and geometry.
 * strideBytes is the luma/packed row stride; 0 selects a tightly packed row.
 * Returns 0 for invalid geometry, layout, or arithmetic overflow.
 */
std::size_t imageSizeBytes(uvap::PixelFormat format,
                           int width,
                           int height,
                           int strideBytes = 0);

} // namespace uvap::v4l2
