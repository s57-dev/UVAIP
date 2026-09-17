/*
 * Copyright 2026 S57 ApS
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

#include "ChessboardProducer.h"

#include <stdexcept>
#include <thread>

namespace uvap::example
{

ChessboardProducer::ChessboardProducer(uvap::FrameInfo format, int bufferCount, int frameRate)
    : format_(std::move(format))
    , frameRate_(frameRate > 0 ? frameRate : 30)
    , pool_(std::make_unique<CpuBufferPool>(format_, bufferCount))
{
    if (format_.pixelFormat != uvap::PixelFormat::Bgr24)
    {
        throw std::runtime_error("ChessboardProducer: only Bgr24 is supported");
    }
}

ChessboardProducer::~ChessboardProducer()
{
    stop();
}

void ChessboardProducer::start()
{
    running_ = true;
    frameIndex_ = 0;
    nextFrameTime_ = std::chrono::steady_clock::now();
}

void ChessboardProducer::stop()
{
    running_ = false;
}

uvap::FrameInfo ChessboardProducer::frameInfo() const
{
    return format_;
}

void ChessboardProducer::setFrameRate(int frameRate)
{
    frameRate_ = frameRate > 0 ? frameRate : 30;
}

bool ChessboardProducer::getFrame(uvap::Frame& out, std::chrono::milliseconds timeout)
{
    if (!running_ || !pool_)
    {
        return false;
    }

    const auto frameInterval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double>(1.0 / static_cast<double>(frameRate_)));

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    if (nextFrameTime_ > deadline)
    {
        return false;
    }

    std::this_thread::sleep_until(nextFrameTime_);
    nextFrameTime_ += frameInterval;

    if (!running_)
    {
        return false;
    }

    if (!pool_->acquire(out))
    {
        return false;
    }

    drawChessboard(out, frameIndex_);
    out.info().frameIndex = frameIndex_;
    out.info().timestampNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                 std::chrono::steady_clock::now().time_since_epoch())
                                 .count();
    ++frameIndex_;
    return true;
}

void ChessboardProducer::drawChessboard(uvap::Frame& frame, std::uint64_t frameIndex)
{
    auto* data = static_cast<std::uint8_t*>(frame.data());
    if (data == nullptr)
    {
        throw std::runtime_error("ChessboardProducer: null frame handle");
    }

    const int width = frame.info().width;
    const int height = frame.info().height;
    constexpr int tile = 64;
    const int shift = static_cast<int>(frameIndex % static_cast<std::uint64_t>(tile));

    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const int cx = (x + shift) / tile;
            const int cy = (y + shift) / tile;
            const std::uint8_t value = ((cx + cy) % 2 == 0) ? 255 : 0;
            const std::size_t i =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                 static_cast<std::size_t>(x)) *
                3;
            data[i + 0] = value;
            data[i + 1] = value;
            data[i + 2] = value;
        }
    }
}

} // namespace uvap::example
