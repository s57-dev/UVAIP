#include "camera.h"
#include "ICallBack.h"
#include <iostream>

class FrameLogger : public ICallback
{
public:
    void onFrame(const cv::Mat& frame) override
    {
        std::cout << "Frame received\n";
    }
};

int main()
{
    Camera camera(10);

    FrameLogger logger;

    camera.setCallback(&logger);

    if (!camera.open())
    {
        return -1;
    }

    camera.run();
    camera.close();

    return 0;
}