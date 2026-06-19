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
    int width = 640;
    int height = 480;
    int frameRate = 30; 

    ICallback* callback = nullptr;

    void logFrameInfo();

public:
    Camera(int deviceID = 0);

    void setCallback(ICallback* cb);

    bool open();
    void run();
    void close();
    bool isOpen() const;
    void setResolution(int w, int h);
    void setFrameRate(int fr);
    int getWidth() const;
    int getHeight() const;
    int getFrameRate() const;
    int getDeviceID() const;

    void checkerBoard(int height, int width, int frameNumber, uint8_t* buffer);
};

#endif
