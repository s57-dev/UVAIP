#pragma once

#include "Memory.h"

#include <cstdint>
#include <utility>

namespace uvap
{

enum class PixelFormat
{
    Rgb24,
    Bgr24,
    Rgba32,
    Bgra32,
    Gray8,

    // 4:2:0
    Yuv420, // planar I420
    Nv12,   // semi-planar

    // 4:2:2
    Yuv422, // planar I422
    Nv16,   // semi-planar
    Yuyv,   // packed YUYV / YUY2
    Uyvy,   // packed UYVY
};

struct FrameInfo
{
    int width = 0;
    int height = 0;
    PixelFormat pixelFormat = PixelFormat::Rgb24;
    // Row pitch in bytes; 0 means tightly packed.
    int strideBytes = 0;
    // Presentation/capture time in nanoseconds since an arbitrary epoch; 0 if unknown.
    std::int64_t timestampNs = 0;
    // Monotonic sequence number from the producer; 0 if unused.
    std::uint64_t frameIndex = 0;
};

/**
 * Transport-agnostic video frame: metadata plus an IMemory backing store.
 * Frames are cheap to copy: they share the MemoryPtr.
 */
class Frame
{
public:
    Frame() = default;

    Frame(FrameInfo info, MemoryPtr memory)
        : info_(std::move(info))
        , memory_(std::move(memory))
    {
    }

    const FrameInfo& info() const { return info_; }
    FrameInfo& info() { return info_; }

    const MemoryPtr& memory() const { return memory_; }

    bool empty() const
    {
        return info_.width <= 0 || info_.height <= 0;
    }

    bool hasMemory() const { return static_cast<bool>(memory_); }

    std::size_t sizeBytes() const
    {
        return memory_ ? memory_->sizeBytes() : 0;
    }

    bool isCpuMapped() const
    {
        return memory_ && memory_->isCpuMapped();
    }

    void* handle() const
    {
        return memory_ ? memory_->handle() : nullptr;
    }

private:
    FrameInfo info_{};
    MemoryPtr memory_;
};

} // namespace uvap
