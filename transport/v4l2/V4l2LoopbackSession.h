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

#include "V4l2Sink.h"

#include <chrono>
#include <cstdint>
#include <optional>

namespace uvap::v4l2
{

/**
 * Private v4l2loopback event carrying the capture STREAMON usage count.
 * This extension is deliberately isolated from the generic frame-sink API.
 */
inline constexpr std::uint32_t kV4l2EventPriClientUsage =
    static_cast<std::uint32_t>(0x08000000) + 0x08E00000 + 1;

struct ClientUsageEvent
{
    std::uint32_t count = 0;
};

/**
 * v4l2loopback consumer-presence adapter.
 *
 * The adapter borrows a V4l2Sink and must be used on the sink's owner thread.
 * It does not own the device or choose pipeline reconfiguration policy.
 */
class V4l2LoopbackSession
{
public:
    explicit V4l2LoopbackSession(V4l2Sink& sink) : sink_(sink) {}

    void subscribeClientUsage();
    std::optional<ClientUsageEvent> waitClientUsage(
        std::chrono::milliseconds timeout);
    std::optional<ClientUsageEvent> pollClientUsage();

private:
    std::optional<ClientUsageEvent> dequeueClientUsageEvent();

    V4l2Sink& sink_;
    bool subscribed_ = false;
};

} // namespace uvap::v4l2
