/*
 * Copyright 2026 S57 ApS
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

#pragma once

#include "IFrameSink.h"

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace uvap::v4l2
{

struct StreamParm
{
    int fpsNumerator = 30;
    int fpsDenominator = 1;

    double fps() const
    {
        if (fpsDenominator <= 0)
        {
            return 0.0;
        }
        return static_cast<double>(fpsNumerator) / static_cast<double>(fpsDenominator);
    }
};

/**
 * Zero-copy V4L2 VIDEO_OUTPUT sink (e.g. v4l2loopback).
 *
 * Buffers are MMAP'd per start(), then cycled: acquireFrame → fill → pushFrame →
 * (device) → DQBUF → acquireFrame again.
 *
 * This type is single-thread confined. Buffer leases may be destroyed on
 * another thread, but all sink operations must run on one owner thread.
 */
class V4l2Sink final : public uvap::IFrameSink
{
public:
    explicit V4l2Sink(std::string devicePath);
    ~V4l2Sink() override;

    V4l2Sink(const V4l2Sink&) = delete;
    V4l2Sink& operator=(const V4l2Sink&) = delete;

    /** Open the device if needed (idempotent). */
    void openDevice();

    uvap::FrameInfo getFormat() const;
    StreamParm getStreamParm() const;
    void setStreamParm(const StreamParm& parm);

    /**
     * STREAMOFF + REQBUFS(0) + unmap. Keeps the device fd open and event
     * subscription so a consumer can S_FMT before the next start().
     */
    void yield();

    /**
     * STREAMOFF, return every MMAP slot to the free list, STREAMON.
     * Use when a consumer attaches so the OUTPUT queue is not stuck after
     * idle / backpressure (v4l2loopback DQBUF returns EFAULT on empty list).
     */
    void recycleBuffers();

    void configure(const uvap::SinkConfig& config) override;
    uvap::SinkConfig configuration() const override;

    void start() override;
    void stop() override;

    bool acquireFrame(uvap::Frame& out) override;
    bool pushFrame(uvap::Frame frame) override;

    /** Borrowed native handle for tightly scoped platform adapters. */
    int nativeHandle() const { return fd_; }
    bool isOpen() const { return fd_ >= 0; }
    bool isStreaming() const { return streaming_; }

private:
    struct MappedBuffer
    {
        void* start = nullptr;
        std::size_t length = 0;
    };

    /** Queue generation shared by the sink and its outstanding buffer leases. */
    struct BufferPool
    {
        enum class SlotState
        {
            Free,
            Dequeued,
            Queued,
        };

        struct Slot
        {
            MappedBuffer mapping;
            SlotState state = SlotState::Free;
        };

        ~BufferPool();

        std::vector<Slot> slots;
        std::deque<std::uint32_t> freeIndices;
        mutable std::mutex mutex;

        bool acquireFree(std::uint32_t& index);
        bool transition(std::uint32_t index, SlotState from, SlotState to);
        void releaseLease(std::uint32_t index) noexcept;
        void reclaimAllQueued();
        bool hasDequeued() const;
    };

    class BufferMemory;

    void closeDevice() noexcept;
    void validateCapabilities();
    void releaseMappedBuffers() noexcept;
    void setFormat();
    void requestAndMapBuffers();
    void streamOn();
    void streamOff();
    void streamOffNoexcept() noexcept;
    void reqbufsZero();
    void reqbufsZeroNoexcept() noexcept;
    bool takeBufferIndex(std::uint32_t& index);
    uvap::Frame makeFrame(std::uint32_t index);
    void requireNoOutstandingFrames(const char* operation) const;

    std::string devicePath_;
    int fd_ = -1;
    uvap::SinkConfig config_{};
    std::size_t negotiatedSizeImage_ = 0;
    bool streaming_ = false;
    std::shared_ptr<BufferPool> pool_;
};

} // namespace uvap::v4l2
