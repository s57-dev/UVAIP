#pragma once

#include "IScaler.h"

namespace uvap::example
{

/**
 * Software IScaler: reads a CPU-mapped input Frame and writes the scaled
 * result into a CPU-mapped output Frame (typically a V4L2 sink buffer).
 *
 * The caller retains the exclusive output lease and moves it to the sink after
 * scaling completes.
 */
class SoftwareScaler final : public uvap::IScaler
{
public:
    SoftwareScaler() = default;

    void configure(const uvap::ScaleConfig& config) override;
    uvap::ScaleConfig configuration() const override;

    void scale(const uvap::Frame& input, uvap::Frame& output) override;

private:
    uvap::ScaleConfig config_{};
};

} // namespace uvap::example
