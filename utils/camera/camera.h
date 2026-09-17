/*
 * Copyright 2026 S57 ApS
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

#ifndef CAMERA_H
#define CAMERA_H

#include <opencv2/opencv.hpp>
#include "CameraConfiguration.h"
#include "ICallBack.h"
#include <chrono>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

class Camera
{
private:
    cv::VideoCapture camera;
    cv::Mat frame;

    int frameIndex;
    std::string device_;
    CameraConfiguration config_;
    std::chrono::high_resolution_clock::time_point startTime;

    ICallback* callback = nullptr;

    mutable std::mutex lifecycleMutex_;

    void logFrameInfo();
    std::jthread worker;

    void captureLoop(std::stop_token stopToken);
    void applyConfiguration();
    CameraConfiguration getConfigSnapshot() const;
    void stopWorker();
    void joinWorker();

public:
    explicit Camera(int deviceID, const CameraConfiguration& config = CameraConfiguration{});
    explicit Camera(std::string device, const CameraConfiguration& config = CameraConfiguration{});

    void setCallback(ICallback* cb);

    void setConfiguration(const CameraConfiguration& config);
    CameraConfiguration getConfiguration() const;
    std::vector<CameraConfiguration> getAvailableCameraConfigs() const;

    void open();
    void join();
    void close();
    bool isRunning() const;
    void stop();
};

#endif
