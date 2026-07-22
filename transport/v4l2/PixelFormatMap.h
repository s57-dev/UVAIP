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

/** Tightly packed image size in bytes for the given format and geometry. */
std::size_t imageSizeBytes(uvap::PixelFormat format, int width, int height);

} // namespace uvap::v4l2
