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

#include <opencv2/opencv.hpp>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <queue>

class FrameQueue
{
public:
    static constexpr size_t kMaxSize = 30;

    void push(const cv::Mat& frame)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (frames_.size() >= kMaxSize)
        {
            frames_.pop();
        }
        frames_.push(frame.clone());
        cv_.notify_one();
    }

    bool tryPop(cv::Mat& out)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (frames_.empty())
        {
            return false;
        }

        out = std::move(frames_.front());
        frames_.pop();
        return true;
    }

    bool waitPop(cv::Mat& out, std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!cv_.wait_for(lock, timeout, [this] { return !frames_.empty(); }))
        {
            return false;
        }

        out = std::move(frames_.front());
        frames_.pop();
        return true;
    }

    size_t size() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return frames_.size();
    }

    bool empty() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return frames_.empty();
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<cv::Mat> frames_;
};
