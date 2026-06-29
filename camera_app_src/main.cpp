#include "camera.h"
#include "CameraError.h"
#include "FrameQueue.h"
#include "ICallBack.h"
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

class FrameCapture : public ICallback
{
public:
    explicit FrameCapture(FrameQueue& queue) : frameQueue(queue) {}

    bool onFrameCapture(const cv::Mat& frame) override
    {
        cv::Mat bgr;
        if (frame.channels() == 1)
        {
            cv::cvtColor(frame, bgr, cv::COLOR_GRAY2BGR);
        }
        else
        {
            bgr = frame;
        }

        frameQueue.push(bgr);
        return true;
    }

private:
    FrameQueue& frameQueue;
};

int main()
{
    try
    {
        FrameQueue frameQueue;
        std::atomic<bool> quit{false};

        std::thread inputThread([&quit]() {
            std::cout << "Capturing frames into queue. Press q then Enter to quit." << std::endl;
            char c = 0;
            std::cin >> c;
            if (c == 'q')
            {
                quit = true;
            }
        });

        CameraConfiguration config;
        config.width = 640;
        config.height = 480;
        config.frameRate = 30;

        Camera camera(10, config);
        FrameCapture capture(frameQueue);

        camera.setCallback(&capture);
        camera.open();

        cv::Mat latestFrame;
        while (!quit)
        {
            cv::Mat frame;
            if (frameQueue.waitPop(frame, std::chrono::milliseconds(33)))
            {
                latestFrame = std::move(frame);
                std::cout << "Queue size: " << frameQueue.size()
                          << " | Latest frame: " << latestFrame.cols
                          << "x" << latestFrame.rows << std::endl;
            
                        cv::imshow("Latest Frame", latestFrame);
                        cv::waitKey(1);
            }
        }

        camera.stop();
        camera.join();
        camera.close();

        if (inputThread.joinable())
        {
            inputThread.join();
        }

        std::cout << "Stopped. Remaining frames in queue: " << frameQueue.size() << std::endl;

        return 0;
    }
    catch (const CameraError& e)
    {
        std::cerr << e.what() << std::endl;
        return 1;
    }
}
