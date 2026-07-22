#pragma once

#include "IScaler.h"

namespace uvap::example
{

/**
 * Software IScaler: reads a CPU-mapped input Frame and writes the scaled
 * result into a CPU-mapped output Frame (typically a V4L2 sink buffer).
 *
 * Keeps the generic IScaler API: scale(input, output, callback) writes into
 * output.handle(), invokes callback, and returns output.
 */
class SoftwareScaler final : public uvap::IScaler
{
public:
    SoftwareScaler() = default;

    void configure(const uvap::ScaleConfig& config) override;
    uvap::ScaleConfig configuration() const override;

    uvap::Frame scale(const uvap::Frame& input,
                      const uvap::Frame& output,
                      const uvap::ScaleCallback& callback) override;

private:
    uvap::ScaleConfig config_{};
};

} // namespace uvap::example
