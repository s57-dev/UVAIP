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
    //check if deviceID is equal to -1
    if(deviceID != -1)
    {
        if (!camera.open(deviceID))
        {
            cerr << "Could not open camera." << endl;
            return false;
        }
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
    if(deviceID != -1)
    {
        while (true)
        {
            camera >> frame;

            if (frame.empty())
            {
                cerr << "Error: Empty frame." << endl;
                break;
            }

            if (callback && !callback->onFrameCapture(frame))
            {
                break;
            }

            //logFrameInfo();

            frameIndex++;
        }
    }
    else
    {
        while (true)
        {
            frame = cv::Mat(height, width, CV_8UC1);
            checkerBoard(height, width, frameIndex, frame.data);

            if (callback && !callback->onFrameCapture(frame))
            {
                break;
            }

            //logFrameInfo();

            frameIndex++;
        }
    }
}

void Camera::close()
{
    camera.release();
    destroyAllWindows();
}

void Camera::checkerBoard(int height, int width, int frameNumber, uint8_t* buffer)
{
    const int tile = 64;
    const int shift = frameNumber % tile;

    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const int cx = (x + shift) / tile;
            const int cy = (y + shift) / tile;

            buffer[y * width + x] = ((cx + cy) % 2 == 0) ? 255 : 0;
        }
    }
}

void Camera::setResolution(int w, int h)
{
    width = w;
    height = h;
}

void Camera::setFrameRate (int fr)
{
    frameRate = fr;
}

int Camera::getWidth() const
{
    return width;
}

int Camera::getHeight() const
{
    return height;
}

int Camera::getFrameRate() const
{
    return frameRate;
}

int Camera::getDeviceID() const
{
    return deviceID;
}