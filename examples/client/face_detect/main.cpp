#include "FaceDetector.h"
#include "camera/camera.h"
#include "camera/CameraError.h"
#include "camera/FrameQueue.h"
#include "camera/ICallBack.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

namespace
{

struct Options
{
    std::string device = "0";
    std::string modelPath = "models/face_detection_short_range.tflite";
    int width = 640;
    int height = 480;
    int fps = 30;
};

void printUsage(const char* argv0)
{
    std::cerr
        << "Usage: " << argv0
        << " [--device PATH|INDEX] [--model PATH] [--width N] [--height N] [--fps N]\n"
        << "  Face detection client over an OpenCV/V4L2 capture device.\n"
        << "  Examples:\n"
        << "    " << argv0 << " --device 0\n"
        << "    " << argv0 << " --device /dev/video10\n";
}

Options parseArgs(int argc, char** argv)
{
    Options opt;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h")
        {
            printUsage(argv[0]);
            std::exit(0);
        }
        if (arg == "--device" && i + 1 < argc)
        {
            opt.device = argv[++i];
            continue;
        }
        if (arg == "--model" && i + 1 < argc)
        {
            opt.modelPath = argv[++i];
            continue;
        }
        if (arg == "--width" && i + 1 < argc)
        {
            opt.width = std::stoi(argv[++i]);
            continue;
        }
        if (arg == "--height" && i + 1 < argc)
        {
            opt.height = std::stoi(argv[++i]);
            continue;
        }
        if (arg == "--fps" && i + 1 < argc)
        {
            opt.fps = std::stoi(argv[++i]);
            continue;
        }
        std::cerr << "Unknown argument: " << arg << "\n";
        printUsage(argv[0]);
        std::exit(1);
    }
    return opt;
}

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

} // namespace

int main(int argc, char** argv)
{
    try
    {
        const Options opt = parseArgs(argc, argv);

        FrameQueue frameQueue;
        std::atomic<bool> quit{false};

        std::thread inputThread([&quit, &opt]() {
            std::cout << "Face detect on " << opt.device
                      << ". Press q then Enter to quit." << std::endl;
            char c = 0;
            std::cin >> c;
            if (c == 'q')
            {
                quit = true;
            }
        });

        CameraConfiguration config;
        config.width = opt.width;
        config.height = opt.height;
        config.frameRate = opt.fps;

        FaceDetector faceDetector(opt.modelPath);

        Camera camera(opt.device, config);
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

                const int faceCount = faceDetector.countFaces(latestFrame);

                std::cout << "Queue size: " << frameQueue.size()
                          << " | Latest frame: " << latestFrame.cols
                          << "x" << latestFrame.rows
                          << " | Faces: " << faceCount << std::endl;

                cv::putText(latestFrame, "Faces: " + std::to_string(faceCount),
                            cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 1.0,
                            cv::Scalar(0, 255, 0), 2);

                cv::imshow("Face Detect", latestFrame);
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
    catch (const std::exception& e)
    {
        std::cerr << e.what() << std::endl;
        return 1;
    }
}
