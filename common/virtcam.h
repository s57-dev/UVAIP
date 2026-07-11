#ifndef VIRTCAM_H
#define VIRTCAM_H

#include <opencv2/opencv.hpp>
#include "ICallBack.h"
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stop_token>
#include <thread>

struct videoParams
{
    int width;
    int height;
    int frameRate;
};

struct VirtCamFrame
{
    int width = 0;
    int height = 0;
    std::shared_ptr<uint8_t[]> data;

    static std::shared_ptr<VirtCamFrame> create(int width, int height);
    cv::Mat toMat() const;
};

class VirtCam
{
private:
    cv::Mat frame;

    int frameIndex = 0;
    videoParams params{640, 480, 30};
    std::chrono::high_resolution_clock::time_point startTime;

    ICallback* callback = nullptr;

    mutable std::mutex lifecycleMutex_;

    void checkerBoard(int patternHeight, int patternWidth, int frameNumber,
                      const std::shared_ptr<VirtCamFrame>& frame);

    std::jthread worker;

    void captureLoop(std::stop_token stopToken);
    videoParams getParamsSnapshot() const;
    void stopWorker();
    void joinWorker();

public:
    void setCallback(ICallback* cb);

    void open();
    void join();
    void close();
    void stop();

    void changeResolution(int width, int height);

    void setVideoParams(const videoParams& videoParams);
    videoParams getVideoParams() const;
};

#endif
