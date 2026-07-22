#include "FaceDetector.h"
#include "camera/camera.h"
#include "camera/CameraError.h"
#include "camera/FrameQueue.h"
#include "camera/ICallBack.h"

#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace
{

struct Options
{
    std::string device = "/dev/video10";
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
        << "  Keys: a=apply resolution/FPS from trackbars, q=quit.\n";
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

/** Unique WxH entries from Camera::getAvailableCameraConfigs(). */
std::vector<CameraConfiguration> uniqueResolutions(const Camera& camera)
{
    std::vector<CameraConfiguration> out;
    for (const CameraConfiguration& cfg : camera.getAvailableCameraConfigs())
    {
        bool seen = false;
        for (const CameraConfiguration& existing : out)
        {
            if (existing.width == cfg.width && existing.height == cfg.height)
            {
                seen = true;
                break;
            }
        }
        if (!seen)
        {
            out.push_back(cfg);
        }
    }
    return out;
}

int indexOfResolution(const std::vector<CameraConfiguration>& modes, int width, int height)
{
    for (int i = 0; i < static_cast<int>(modes.size()); ++i)
    {
        if (modes[static_cast<std::size_t>(i)].width == width &&
            modes[static_cast<std::size_t>(i)].height == height)
        {
            return i;
        }
    }
    return 0;
}

bool isV4l2DevicePath(const std::string& device)
{
    return device.find("/dev/video") == 0;
}

void armLoopbackFormat(const std::string& device, int width, int height, int fps)
{
    const int fd = ::open(device.c_str(), O_RDWR | O_NONBLOCK);
    if (fd < 0)
    {
        throw CameraError(std::string("armLoopbackFormat: open ") + device + ": " +
                          std::strerror(errno));
    }

    v4l2_format fmt{};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (::ioctl(fd, VIDIOC_G_FMT, &fmt) < 0)
    {
        const int err = errno;
        ::close(fd);
        throw CameraError(std::string("armLoopbackFormat: G_FMT: ") + std::strerror(err));
    }

    fmt.fmt.pix.width = static_cast<__u32>(width);
    fmt.fmt.pix.height = static_cast<__u32>(height);
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_BGR24;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;
    fmt.fmt.pix.bytesperline = static_cast<__u32>(width * 3);
    fmt.fmt.pix.sizeimage = static_cast<__u32>(width * height * 3);

    if (::ioctl(fd, VIDIOC_S_FMT, &fmt) < 0)
    {
        const int err = errno;
        ::close(fd);
        throw CameraError(std::string("armLoopbackFormat: S_FMT: ") + std::strerror(err));
    }

    v4l2_streamparm parm{};
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (::ioctl(fd, VIDIOC_G_PARM, &parm) == 0)
    {
        parm.parm.capture.timeperframe.numerator = 1;
        parm.parm.capture.timeperframe.denominator =
            static_cast<__u32>(fps > 0 ? fps : 30);
        ::ioctl(fd, VIDIOC_S_PARM, &parm);
    }

    ::close(fd);
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
        Options opt = parseArgs(argc, argv);

        FrameQueue frameQueue;
        FaceDetector faceDetector(opt.modelPath);
        FrameCapture capture(frameQueue);

        CameraConfiguration config;
        config.width = opt.width;
        config.height = opt.height;
        config.frameRate = opt.fps > 0 ? opt.fps : 30;

        auto camera = std::make_unique<Camera>(opt.device, config);
        const std::vector<CameraConfiguration> modes = uniqueResolutions(*camera);
        if (modes.empty())
        {
            throw CameraError("No camera resolutions available");
        }

        int modeIndex = indexOfResolution(modes, opt.width, opt.height);
        int fps = config.frameRate;
        config.width = modes[static_cast<std::size_t>(modeIndex)].width;
        config.height = modes[static_cast<std::size_t>(modeIndex)].height;

        camera->setConfiguration(config);
        camera->setCallback(&capture);
        camera->open();

        const std::string windowName = "Face Detect";
        cv::namedWindow(windowName, cv::WINDOW_AUTOSIZE);
        cv::createTrackbar(
            "mode", windowName, &modeIndex, static_cast<int>(modes.size()) - 1);
        cv::createTrackbar("fps", windowName, &fps, 60);
        cv::setTrackbarMin("fps", windowName, 1);

        std::cout << "Face detect on " << opt.device
                  << ". mode trackbar = Camera resolutions, a=apply, q=quit.\n";
        for (std::size_t i = 0; i < modes.size(); ++i)
        {
            std::cout << "  [" << i << "] " << modes[i].width << "x" << modes[i].height
                      << "\n";
        }

        cv::Mat latestFrame;
        bool running = true;
        while (running)
        {
            const int key = cv::waitKey(1);
            if (key == 'q' || key == 'Q' || key == 27)
            {
                running = false;
                break;
            }

            if (key == 'a' || key == 'A')
            {
                if (modeIndex < 0)
                {
                    modeIndex = 0;
                }
                if (modeIndex >= static_cast<int>(modes.size()))
                {
                    modeIndex = static_cast<int>(modes.size()) - 1;
                }
                if (fps < 1)
                {
                    fps = 30;
                }

                const CameraConfiguration& mode =
                    modes[static_cast<std::size_t>(modeIndex)];
                std::cout << "Applying " << mode.width << "x" << mode.height << " @ " << fps
                          << " fps\n";

                camera->stop();
                camera->join();
                camera->close();
                camera.reset();

                cv::Mat discard;
                while (frameQueue.tryPop(discard))
                {
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                if (isV4l2DevicePath(opt.device))
                {
                    armLoopbackFormat(opt.device, mode.width, mode.height, fps);
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                }

                config.width = mode.width;
                config.height = mode.height;
                config.frameRate = fps;
                camera = std::make_unique<Camera>(opt.device, config);
                camera->setCallback(&capture);
                camera->open();
            }

            cv::Mat frame;
            if (frameQueue.waitPop(frame, std::chrono::milliseconds(33)))
            {
                latestFrame = std::move(frame);
                const int faceCount = faceDetector.countFaces(latestFrame);

                const int mi = std::max(
                    0, std::min(modeIndex, static_cast<int>(modes.size()) - 1));
                const auto& mode = modes[static_cast<std::size_t>(mi)];

                std::cout << "Frame: " << latestFrame.cols << "x" << latestFrame.rows
                          << " | Faces: " << faceCount << " | selected " << mode.width
                          << "x" << mode.height << " @" << fps << " [a]=apply\n";

                cv::putText(latestFrame,
                            "Faces: " + std::to_string(faceCount),
                            cv::Point(10, 30),
                            cv::FONT_HERSHEY_SIMPLEX,
                            1.0,
                            cv::Scalar(0, 255, 0),
                            2);
                cv::imshow(windowName, latestFrame);
            }
            else if (!latestFrame.empty())
            {
                cv::imshow(windowName, latestFrame);
            }
        }

        if (camera)
        {
            camera->stop();
            camera->join();
            camera->close();
        }
        cv::destroyAllWindows();
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
