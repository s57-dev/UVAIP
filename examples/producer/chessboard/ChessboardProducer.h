#pragma once

#include "CpuBufferPool.h"
#include "IFrameProducer.h"
#include "IFrameRateController.h"

#include <chrono>
#include <cstdint>
#include <memory>

namespace uvap::example
{

/**
 * Synthetic IFrameProducer: animated Bgr24 chessboard from a CPU buffer pool.
 */
class ChessboardProducer final : public uvap::IFrameProducer,
                                 public uvap::IFrameRateController
{
public:
    ChessboardProducer(uvap::FrameInfo format, int bufferCount, int frameRate);
    ~ChessboardProducer() override;

    void start() override;
    void stop() override;

    bool getFrame(uvap::Frame& out, std::chrono::milliseconds timeout) override;
    uvap::FrameInfo frameInfo() const override;

    void setFrameRate(int frameRate) override;
    int frameRate() const override { return frameRate_; }

private:
    static void drawChessboard(uvap::Frame& frame, std::uint64_t frameIndex);

    uvap::FrameInfo format_{};
    int frameRate_ = 30;
    std::unique_ptr<CpuBufferPool> pool_;
    bool running_ = false;
    std::uint64_t frameIndex_ = 0;
    std::chrono::steady_clock::time_point nextFrameTime_{};
};

} // namespace uvap::example
