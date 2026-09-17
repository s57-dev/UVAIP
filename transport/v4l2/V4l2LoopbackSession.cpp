/*
 * Copyright 2026 S57 ApS
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

#include "V4l2LoopbackSession.h"

#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/poll.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <limits>
#include <system_error>

namespace uvap::v4l2
{
namespace
{

int ioctlRetry(int fd, unsigned long request, void* argument)
{
    int rc = 0;
    do
    {
        rc = ::ioctl(fd, request, argument);
    } while (rc < 0 && errno == EINTR);
    return rc;
}

[[noreturn]] void throwSystemError(const char* operation)
{
    const int error = errno;
    throw std::system_error(error, std::generic_category(), operation);
}

} // namespace

void V4l2LoopbackSession::subscribeClientUsage()
{
    sink_.openDevice();

    v4l2_event_subscription subscription{};
    subscription.type = kV4l2EventPriClientUsage;
    subscription.flags = V4L2_EVENT_SUB_FL_SEND_INITIAL;
    if (ioctlRetry(sink_.nativeHandle(), VIDIOC_SUBSCRIBE_EVENT, &subscription) < 0)
    {
        throwSystemError("V4l2LoopbackSession: VIDIOC_SUBSCRIBE_EVENT CLIENT_USAGE");
    }
    subscribed_ = true;
}

std::optional<ClientUsageEvent> V4l2LoopbackSession::dequeueClientUsageEvent()
{
    if (!sink_.isOpen())
    {
        return std::nullopt;
    }

    v4l2_event event{};
    if (ioctlRetry(sink_.nativeHandle(), VIDIOC_DQEVENT, &event) < 0)
    {
        // Some v4l2loopback/kernel combinations report ENOENT for an empty
        // non-blocking event queue instead of EAGAIN.
        if (errno == EAGAIN || errno == ENOENT)
        {
            return std::nullopt;
        }
        throwSystemError("V4l2LoopbackSession: VIDIOC_DQEVENT");
    }

    if (event.type != kV4l2EventPriClientUsage)
    {
        return std::nullopt;
    }

    ClientUsageEvent result{};
    static_assert(sizeof(result.count) <= sizeof(event.u.data));
    std::memcpy(&result.count, event.u.data, sizeof(result.count));
    return result;
}

std::optional<ClientUsageEvent> V4l2LoopbackSession::pollClientUsage()
{
    return dequeueClientUsageEvent();
}

std::optional<ClientUsageEvent> V4l2LoopbackSession::waitClientUsage(
    std::chrono::milliseconds timeout)
{
    sink_.openDevice();
    if (!subscribed_)
    {
        subscribeClientUsage();
    }

    pollfd descriptor{};
    descriptor.fd = sink_.nativeHandle();
    descriptor.events = POLLPRI;

    const auto timeoutCount = timeout.count();
    const int timeoutMs =
        timeoutCount < 0
            ? -1
            : static_cast<int>(std::min<std::int64_t>(
                  timeoutCount, std::numeric_limits<int>::max()));
    const int rc = ::poll(&descriptor, 1, timeoutMs);
    if (rc < 0)
    {
        if (errno == EINTR)
        {
            return std::nullopt;
        }
        throwSystemError("V4l2LoopbackSession: poll CLIENT_USAGE");
    }
    if (rc == 0)
    {
        return std::nullopt;
    }
    if ((descriptor.revents & POLLPRI) == 0)
    {
        throw std::system_error(
            std::make_error_code(std::errc::io_error),
            "V4l2LoopbackSession: device poll returned without a priority event");
    }

    return dequeueClientUsageEvent();
}

} // namespace uvap::v4l2
