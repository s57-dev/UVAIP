#pragma once

#include "Frame.h"

namespace uvap
{

/**
 * Configurable frame transform: scale, color convert, crop, rotate (REQ-04).
 */
struct ScaleConfig
{
    int width = 0;
    int height = 0;
    PixelFormat outputFormat = PixelFormat::Rgb24;
    // Crop rectangle in source coordinates; all zeros means full frame.
    int cropX = 0;
    int cropY = 0;
    int cropW = 0;
    int cropH = 0;

    // Supported values: 0, 90, 180, 270.
    int rotationDegrees = 0;
};

/**
 * Interface for a frame scaler.
 */
class IScaler
{
public:
    virtual ~IScaler() = default;

    virtual void configure(const ScaleConfig& config) = 0;
    virtual ScaleConfig configuration() const = 0;

    /** Transform input according to the current ScaleConfig into output. */
    virtual void scale(const Frame& input, Frame& output) = 0;
};

} // namespace uvap
