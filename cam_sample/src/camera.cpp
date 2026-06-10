#include "camera.h"

#include <iostream>

using namespace std;
using namespace cv;

Camera::Camera(int deviceID)
    : camera(), frameIndex(0), deviceID(deviceID)
{
}

bool Camera::open()
{
    if (!camera.open(deviceID))
    {
        cerr << "Could not open camera." << endl;
        return false;
    }

    startTime = chrono::high_resolution_clock::now();

    return true;
}

bool Camera::isOpen() const
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

void Camera::run()
{
    while (true)
    {
        camera >> frame;

        if (frame.empty())
        {
            cerr << "Error: Empty frame." << endl;
            break;
        }

        // Notify callback
        if (callback)
        {
            callback->onFrame(frame);
        }

        logFrameInfo();

        imshow("Camera Feed", frame);

        frameIndex++;

        if (waitKey(1) == 'q')
        {
            break;
        }
    }
}

void Camera::close()
{
    camera.release();
    destroyAllWindows();
}
