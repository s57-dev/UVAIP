#ifndef CAMERA_H
#define CAMERA_H

#include <opencv2/opencv.hpp>
#include "ICallBack.h"
#include <chrono>

class Camera
{
private:
    cv::VideoCapture camera;
    cv::Mat frame;

    int frameIndex;
    int deviceID;
    std::chrono::high_resolution_clock::time_point startTime;

    ICallback* callback = nullptr;

    void logFrameInfo();

public:
    Camera(int deviceID = 0);

    void setCallback(ICallback* cb);

    bool open();
    void run();
    void close();
    bool isOpen() const;
};

#endif
