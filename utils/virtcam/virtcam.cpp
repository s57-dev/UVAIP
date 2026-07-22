#include "virtcam/virtcam.h"
#include "camera/CameraError.h"

#include <iostream>
#include <memory>
#include <string>
#include <thread>

using namespace std;
using namespace cv;

std::shared_ptr<VirtCamFrame> VirtCamFrame::create(int width, int height)
{
    if (width <= 0 || height <= 0)
    {
        throw CameraError(
            "Invalid frame dimensions: " + std::to_string(width) + "x" + std::to_string(height));
    }

    auto frame = std::make_shared<VirtCamFrame>();
    frame->width = width;
    frame->height = height;
    frame->data = std::shared_ptr<uint8_t[]>(new uint8_t[static_cast<size_t>(width) * height]);
    return frame;
}

cv::Mat VirtCamFrame::toMat() const
{
    if (!data)
    {
        throw CameraError("Frame data is null");
    }

    if (width <= 0 || height <= 0)
    {
        throw CameraError("Invalid frame dimensions for conversion");
    }

    return cv::Mat(height, width, CV_8UC1, data.get()).clone();
}

videoParams VirtCam::getParamsSnapshot() const
{
    std::lock_guard<std::mutex> lock(lifecycleMutex_);
    return params;
}

void VirtCam::stopWorker()
{
    if (worker.joinable())
    {
        worker.request_stop();
    }
}

void VirtCam::joinWorker()
{
    if (worker.joinable())
    {
        worker.join();
    }
}

void VirtCam::setCallback(ICallback* cb)
{
    callback = cb;
}

void VirtCam::open()
{
    std::lock_guard<std::mutex> lock(lifecycleMutex_);

    if (worker.joinable())
    {
        return;
    }

    startTime = chrono::high_resolution_clock::now();
    worker = std::jthread([this](std::stop_token token) { captureLoop(token); });
}

void VirtCam::stop()
{
    std::lock_guard<std::mutex> lock(lifecycleMutex_);
    stopWorker();
}

void VirtCam::join()
{
    stopWorker();
    joinWorker();
}

void VirtCam::close()
{
    stopWorker();
    joinWorker();

    std::lock_guard<std::mutex> lock(lifecycleMutex_);
    frame.release();
    destroyAllWindows();
}

void VirtCam::changeResolution(int width, int height)
{
    VirtCamFrame::create(width, height);

    bool wasRunning = false;

    {
        std::lock_guard<std::mutex> lock(lifecycleMutex_);

        if (width == params.width && height == params.height)
        {
            return;
        }

        wasRunning = worker.joinable();
        stopWorker();
    }

    joinWorker();

    std::lock_guard<std::mutex> lock(lifecycleMutex_);

    params.width = width;
    params.height = height;
    frame.release();
    frameIndex = 0;

    if (wasRunning)
    {
        startTime = chrono::high_resolution_clock::now();
        worker = std::jthread([this](std::stop_token token) { captureLoop(token); });
    }
}

void VirtCam::setVideoParams(const videoParams& videoParams)
{
    changeResolution(videoParams.width, videoParams.height);

    std::lock_guard<std::mutex> lock(lifecycleMutex_);
    params.frameRate = videoParams.frameRate;
}

videoParams VirtCam::getVideoParams() const
{
    return getParamsSnapshot();
}

void VirtCam::captureLoop(std::stop_token stopToken)
{
    auto nextFrameTime = chrono::high_resolution_clock::now();

    while (!stopToken.stop_requested())
    {
        const videoParams localParams = getParamsSnapshot();
        const int fps = localParams.frameRate > 0 ? localParams.frameRate : 30;
        const auto frameInterval = chrono::duration_cast<chrono::high_resolution_clock::duration>(
            chrono::duration<double>(1.0 / fps));

        this_thread::sleep_until(nextFrameTime);
        nextFrameTime += frameInterval;

        if (stopToken.stop_requested())
        {
            break;
        }

        auto virtFrame = VirtCamFrame::create(localParams.width, localParams.height);
        checkerBoard(localParams.height, localParams.width, frameIndex, virtFrame);

        cv::Mat capturedFrame = virtFrame->toMat();

        if (callback && !callback->onFrameCapture(capturedFrame))
        {
            break;
        }

        {
            std::lock_guard<std::mutex> lock(lifecycleMutex_);
            frame = std::move(capturedFrame);
        }

        frameIndex++;
    }
}

void VirtCam::checkerBoard(int patternHeight, int patternWidth, int frameNumber,
                           const std::shared_ptr<VirtCamFrame>& frame)
{
    if (!frame)
    {
        throw CameraError("Frame is null");
    }

    if (!frame->data)
    {
        throw CameraError("Frame data is null");
    }

    if (patternHeight > frame->height || patternWidth > frame->width)
    {
        throw CameraError(
            "Pattern dimensions (" + std::to_string(patternWidth) + "x" + std::to_string(patternHeight) +
            ") exceed frame size (" + std::to_string(frame->width) + "x" + std::to_string(frame->height) + ")");
    }

    const int tile = 64;
    const int shift = frameNumber % tile;

    for (int y = 0; y < patternHeight; ++y)
    {
        for (int x = 0; x < patternWidth; ++x)
        {
            const int cx = (x + shift) / tile;
            const int cy = (y + shift) / tile;

            frame->data[y * frame->width + x] = ((cx + cy) % 2 == 0) ? 255 : 0;
        }
    }
}
