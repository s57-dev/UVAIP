#include "camera.h"
#include "ICallBack.h"
#include <iostream>

class FrameCapture : public ICallback
{
public:
    bool onFrameCapture(const cv::Mat& frame) override
    {
        cv::imshow("Capture", frame);
        return cv::waitKey(1) != 'q';
    }
};

int main()
{

    Camera camera(10);

    FrameCapture capture;

    camera.setCallback(&capture);

    if (!camera.open())
    {
        return -1;
    }

    camera.run();
    camera.close();

    return 0;
}