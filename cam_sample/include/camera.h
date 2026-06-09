#ifndef CAMERA_H
#define CAMERA_H

#include <opencv2/opencv.hpp>
#include <chrono>

class Camera
{
private:
    cv::VideoCapture camera;
    cv::Mat frame;

    int frameIndex;
    int deviceID;
    std::chrono::high_resolution_clock::time_point startTime;

    void logFrameInfo();

public:
    Camera(int deviceID = 0);

    bool open();
    void run();
    void close();

    bool isOpen() const;
};

#endif
