#include "camera.h"
#include "CameraError.h"

#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using namespace std;
using namespace cv;

namespace
{
const std::vector<CameraConfiguration>& supportedCameraConfigs()
{
    static const std::vector<CameraConfiguration> configs = {
        {160, 120, 30},
        {320, 240, 30},
        {640, 480, 30},
        {800, 600, 30},
        {1280, 720, 30},
        {1280, 720, 60},
        {1366, 768, 30},
        {1920, 1080, 30},
        {1920, 1080, 60},
        {2560, 1440, 30},
        {2560, 1440, 60},
        {3840, 2160, 30},
        {3840, 2160, 60},
    };
    return configs;
}
}

Camera::Camera(int deviceID, const CameraConfiguration& config)
    : camera(), frameIndex(0), deviceID_(deviceID), config_(config)
{
}

CameraConfiguration Camera::getConfigSnapshot() const
{
    std::lock_guard<std::mutex> lock(lifecycleMutex_);
    return config_;
}

void Camera::setConfiguration(const CameraConfiguration& config)
{
    std::lock_guard<std::mutex> lock(lifecycleMutex_);
    config_ = config;
}

CameraConfiguration Camera::getConfiguration() const
{
    return getConfigSnapshot();
}

std::vector<CameraConfiguration> Camera::getAvailableCameraConfigs() const
{
    return supportedCameraConfigs();
}

void Camera::applyConfiguration()
{
    const CameraConfiguration config = getConfigSnapshot();

    if (config.width > 0)
    {
        camera.set(CAP_PROP_FRAME_WIDTH, config.width);
    }

    if (config.height > 0)
    {
        camera.set(CAP_PROP_FRAME_HEIGHT, config.height);
    }

    if (config.frameRate > 0)
    {
        camera.set(CAP_PROP_FPS, config.frameRate);
    }
}

void Camera::stopWorker()
{
    if (worker.joinable())
    {
        worker.request_stop();
    }
}

void Camera::joinWorker()
{
    if (worker.joinable())
    {
        worker.join();
    }
}

void Camera::open()
{
    if (!camera.open(deviceID_, cv::CAP_V4L2))
    {
        throw CameraError("Could not open camera device " + std::to_string(deviceID_) +
                          " (is the loopback feed running on /dev/video" +
                          std::to_string(deviceID_) + "?)");
    }

    applyConfiguration();
    startTime = chrono::high_resolution_clock::now();

    std::lock_guard<std::mutex> lock(lifecycleMutex_);
    if (!worker.joinable())
    {
        worker = std::jthread([this](std::stop_token token) { captureLoop(token); });
    }
}

void Camera::stop()
{
    stopWorker();
}

void Camera::join()
{
    stopWorker();
    joinWorker();
}

bool Camera::isRunning() const
{
    return camera.isOpened();
}

void Camera::logFrameInfo()
{
    auto now = chrono::high_resolution_clock::now();

    double timestamp =
        chrono::duration<double>(now - startTime).count();

    double fps = 0;

    if (timestamp > 0)
    {
        fps = frameIndex / timestamp;
    }

    cout
        << "Frame: " << frameIndex
        << " | Resolution: "
        << frame.cols << "x" << frame.rows
        << " | Timestamp: " << timestamp << " sec"
        << " | FPS: " << fps
        << endl;
}

void Camera::setCallback(ICallback* cb)
{
    callback = cb;
}

void Camera::captureLoop(std::stop_token stopToken)
{
    while (!stopToken.stop_requested())
    {
        camera >> frame;

        const CameraConfiguration config = getConfigSnapshot();
        cout << "Resolution: " << frame.cols << "x" << frame.rows
             << " | Frame rate: " << config.frameRate << " fps" << endl;

        if (frame.empty())
        {
            static auto last_log = std::chrono::steady_clock::now();
            const auto now = std::chrono::steady_clock::now();
            if (now - last_log >= std::chrono::seconds(3))
            {
                cerr << "Empty frame from device " << deviceID_
                     << " (start vlc_pipeline_client, then VLC with frame_tap)" << endl;
                last_log = now;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        if (callback && !callback->onFrameCapture(frame))
        {
            break;
        }

        frameIndex++;
    }
}

void Camera::close()
{
    stopWorker();
    joinWorker();

    camera.release();
    destroyAllWindows();
}
