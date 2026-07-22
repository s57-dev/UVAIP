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
};

/**
 * Observable ownership state of a buffer lease.
 *
 * Free      — available in the pool (typically no live IMemory)
 * Dequeued  — owned by the producer after acquire / DQBUF; may be filled
 * Queued    — owned by the transport/device after push / QBUF
 */
enum class BufferState
{
    Free,
    Dequeued,
    Queued,
};

/**
 * Single memory interface for a frame payload.
 *
 * Native handles are exposed through typed accessors. CPU mappings and
 * dma-buf file descriptors must not be encoded into the same void pointer.
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

    /** CPU mapping, or nullptr when this memory is not CPU-accessible. */
    virtual void* data() const = 0;

    /** Borrowed dma-buf fd, or -1 when this is not dma-buf backed. */
    virtual int dmaBufFd() const { return -1; }

    /** Type of the memory. */
    virtual MemoryType type() const = 0;

    /** Current ownership state in the buffer cycle. */
    virtual BufferState state() const = 0;
};

using MemoryPtr = std::unique_ptr<IMemory>;

} // namespace uvap
