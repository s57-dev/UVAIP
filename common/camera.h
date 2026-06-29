#ifndef CAMERA_H
#define CAMERA_H

#include <opencv2/opencv.hpp>
#include "CameraConfiguration.h"
#include "ICallBack.h"
#include <chrono>
#include <mutex>
#include <stop_token>
#include <thread>
#include <vector>

class Camera
{
private:
    cv::VideoCapture camera;
    cv::Mat frame;

    int frameIndex;
    int deviceID_;
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
    Camera(int deviceID, const CameraConfiguration& config = CameraConfiguration{});

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
