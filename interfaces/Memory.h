#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

namespace uvap
{

enum class MemoryType
{
    CpuMapped,
    DmaBuf,
    IonHandle,
};

/**
 * Ownership / lifecycle of a buffer in the producer–transport–consumer cycle.
 * Aligns with AOSP BufferQueue-style naming; not every backend uses every state.
 *
 * Free      — available in the pool (typically no live IMemory)
 * Dequeued  — owned by the producer after acquire / DQBUF; may be filled
 * Queued    — owned by the transport/device after push / QBUF
 * Acquired  — owned by a consumer (future consumer-side path)
 */
enum class BufferState
{
    Free,
    Dequeued,
    Queued,
    Acquired,
};

/**
 * Single memory interface for a frame payload.
 *
 * One opaque handle. It may or may not be CPU-mapped:
 * when isCpuMapped() is true, handle() is a CPU-accessible pointer (uint8_t*);
 * otherwise handle() is a platform-native identifier for zero-copy import
 * (dma-buf fd as intptr_t, ION handle, etc.).
 *
 * Concrete backends specialize this interface outside of this header.
 */
class IMemory
{
public:
    virtual ~IMemory() = default;

    /** Size of the memory in bytes. */
    virtual std::size_t sizeBytes() const = 0;

    /** True when handle() is a CPU-accessible pointer. */
    virtual bool isCpuMapped() const = 0;

    /** Opaque resource handle (CPU pointer or platform-native id). */
    virtual void* handle() const = 0;

    /** Type of the memory. */
    virtual MemoryType type() const = 0;

    /** Current ownership state in the buffer cycle. */
    virtual BufferState state() const = 0;
};

using MemoryPtr = std::shared_ptr<IMemory>;

} // namespace uvap
