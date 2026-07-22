#pragma once

#include "Frame.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

namespace uvap::example
{

/**
 * Simple CPU-mapped buffer pool for the synthetic producer.
 * Checkout → fill → Frame; destruction while Dequeued returns the slot to Free.
 */
class CpuBufferPool
{
public:
    CpuBufferPool(uvap::FrameInfo format, int bufferCount);
    ~CpuBufferPool() = default;

    CpuBufferPool(const CpuBufferPool&) = delete;
    CpuBufferPool& operator=(const CpuBufferPool&) = delete;

    /** Obtain a Dequeued frame; returns false if all buffers are in use. */
    bool acquire(uvap::Frame& out);

    const uvap::FrameInfo& format() const { return format_; }
    int bufferCount() const
    {
        return state_ ? static_cast<int>(state_->buffers.size()) : 0;
    }

private:
    struct Slot
    {
        std::unique_ptr<std::uint8_t[]> data;
        std::size_t sizeBytes = 0;
    };

    class BufferMemory;

    struct PoolState
    {
        std::vector<Slot> buffers;
        std::deque<std::uint32_t> freeIndices;
        std::mutex mutex;

        void returnFree(std::uint32_t index);
    };

    static std::size_t computeSizeBytes(const uvap::FrameInfo& format);

    uvap::FrameInfo format_{};
    std::shared_ptr<PoolState> state_;
};

} // namespace uvap::example
