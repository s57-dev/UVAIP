#include "CpuBufferPool.h"

#include <memory>
#include <stdexcept>
#include <utility>

namespace uvap::example
{

class CpuBufferPool::BufferMemory final : public uvap::IMemory
{
public:
    BufferMemory(std::shared_ptr<PoolState> pool, std::uint32_t index)
        : pool_(std::move(pool))
        , index_(index)
        , bufferState_(uvap::BufferState::Dequeued)
    {
    }

    ~BufferMemory() override
    {
        if (bufferState_ == uvap::BufferState::Dequeued && pool_)
        {
            pool_->returnFree(index_);
        }
    }

    BufferMemory(const BufferMemory&) = delete;
    BufferMemory& operator=(const BufferMemory&) = delete;

    std::size_t sizeBytes() const override
    {
        return pool_->buffers[index_].sizeBytes;
    }

    bool isCpuMapped() const override { return true; }

    void* handle() const override
    {
        return pool_->buffers[index_].data.get();
    }

    uvap::MemoryType type() const override
    {
        return uvap::MemoryType::CpuMapped;
    }

    uvap::BufferState state() const override { return bufferState_; }

private:
    std::shared_ptr<PoolState> pool_;
    std::uint32_t index_ = 0;
    uvap::BufferState bufferState_ = uvap::BufferState::Dequeued;
};

void CpuBufferPool::PoolState::returnFree(std::uint32_t index)
{
    std::lock_guard<std::mutex> lock(mutex);
    freeIndices.push_back(index);
}

std::size_t CpuBufferPool::computeSizeBytes(const uvap::FrameInfo& format)
{
    if (format.width <= 0 || format.height <= 0)
    {
        throw std::runtime_error("CpuBufferPool: invalid dimensions");
    }

    const std::size_t w = static_cast<std::size_t>(format.width);
    const std::size_t h = static_cast<std::size_t>(format.height);

    switch (format.pixelFormat)
    {
    case uvap::PixelFormat::Gray8:
        return w * h;
    case uvap::PixelFormat::Rgb24:
    case uvap::PixelFormat::Bgr24:
        return w * h * 3;
    case uvap::PixelFormat::Rgba32:
    case uvap::PixelFormat::Bgra32:
        return w * h * 4;
    case uvap::PixelFormat::Nv12:
    case uvap::PixelFormat::Yuv420:
        return w * h * 3 / 2;
    case uvap::PixelFormat::Yuv422:
    case uvap::PixelFormat::Nv16:
    case uvap::PixelFormat::Yuyv:
    case uvap::PixelFormat::Uyvy:
        return w * h * 2;
    }
    throw std::runtime_error("CpuBufferPool: unsupported pixel format");
}

CpuBufferPool::CpuBufferPool(uvap::FrameInfo format, int bufferCount)
    : format_(std::move(format))
    , state_(std::make_shared<PoolState>())
{
    if (bufferCount < 2)
    {
        throw std::runtime_error("CpuBufferPool: bufferCount must be >= 2");
    }

    const std::size_t sizeBytes = computeSizeBytes(format_);
    state_->buffers.resize(static_cast<std::size_t>(bufferCount));

    for (int i = 0; i < bufferCount; ++i)
    {
        state_->buffers[static_cast<std::size_t>(i)].data =
            std::make_unique<std::uint8_t[]>(sizeBytes);
        state_->buffers[static_cast<std::size_t>(i)].sizeBytes = sizeBytes;
        state_->freeIndices.push_back(static_cast<std::uint32_t>(i));
    }
}

bool CpuBufferPool::acquire(uvap::Frame& out)
{
    std::uint32_t index = 0;
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (state_->freeIndices.empty())
        {
            return false;
        }
        index = state_->freeIndices.front();
        state_->freeIndices.pop_front();
    }

    auto memory = std::make_shared<BufferMemory>(state_, index);
    uvap::FrameInfo info = format_;
    out = uvap::Frame{std::move(info), std::move(memory)};
    return true;
}

} // namespace uvap::example
