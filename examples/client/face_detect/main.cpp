#include "FaceDetectWindow.h"
#include "FaceDetector.h"
#include "camera/camera.h"
#include "camera/CameraError.h"
#include "camera/FrameQueue.h"
#include "camera/ICallBack.h"

#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>

#include <QApplication>
#include <QImage>
#include <QTimer>

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
        << " [--device PATH|INDEX] [--model PATH] [--width N] [--height N] [--fps N]\n";
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

/**
 * After the producer yields, exclusive_caps devices only accept OUTPUT ioctls
 * until OUTPUT is streaming again. Arm the shared pix_format via OUTPUT S_FMT.
 */
void armLoopbackFormat(const std::string& device, int width, int height, int fps)
{
    const int fd = ::open(device.c_str(), O_RDWR | O_NONBLOCK);
    if (fd < 0)
    {
        throw CameraError(std::string("armLoopbackFormat: open ") + device + ": " +
                          std::strerror(errno));
    }

    v4l2_format fmt{};
    fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
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
    parm.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    if (::ioctl(fd, VIDIOC_G_PARM, &parm) == 0)
    {
        parm.parm.output.timeperframe.numerator = 1;
        parm.parm.output.timeperframe.denominator =
            static_cast<__u32>(fps > 0 ? fps : 30);
        ::ioctl(fd, VIDIOC_S_PARM, &parm);
    }

    ::close(fd);
}

QImage matToImage(const cv::Mat& bgr)
{
    if (bgr.empty())
    {
        return {};
    }
    cv::Mat rgb;
    cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
    return QImage(rgb.data,
                  rgb.cols,
                  rgb.rows,
                  static_cast<int>(rgb.step),
                  QImage::Format_RGB888)
        .copy();
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

        QApplication app(argc, argv);

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
        config.width = modes[static_cast<std::size_t>(modeIndex)].width;
        config.height = modes[static_cast<std::size_t>(modeIndex)].height;

        camera->setConfiguration(config);
        camera->setCallback(&capture);
        camera->open();

        FaceDetectWindow window(modes, modeIndex, config.frameRate);
        window.show();

        QObject::connect(&window, &FaceDetectWindow::quitRequested, &app, &QApplication::quit);

        QObject::connect(
            &window,
            &FaceDetectWindow::applyRequested,
            &window,
            [&](int newModeIndex, int newFps) {
                if (newModeIndex < 0 ||
                    newModeIndex >= static_cast<int>(modes.size()))
                {
                    return;
                }
                if (newFps < 1)
                {
                    newFps = 30;
                }

                const CameraConfiguration& mode =
                    modes[static_cast<std::size_t>(newModeIndex)];
                window.setStatus(QStringLiteral("Applying %1×%2 @ %3 fps…")
                                     .arg(mode.width)
                                     .arg(mode.height)
                                     .arg(newFps));

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
                    armLoopbackFormat(opt.device, mode.width, mode.height, newFps);
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                }

                config.width = mode.width;
                config.height = mode.height;
                config.frameRate = newFps;
                camera = std::make_unique<Camera>(opt.device, config);
                camera->setCallback(&capture);
                camera->open();
                modeIndex = newModeIndex;
                window.setStatus(QStringLiteral("Streaming %1×%2 @ %3 fps")
                                     .arg(mode.width)
                                     .arg(mode.height)
                                     .arg(newFps));
            });

        QTimer timer;
        QObject::connect(&timer, &QTimer::timeout, &window, [&]() {
            cv::Mat frame;
            if (!frameQueue.tryPop(frame))
            {
                return;
            }

            const int faceCount = faceDetector.countFaces(frame);
            cv::putText(frame,
                        "Faces: " + std::to_string(faceCount),
                        cv::Point(10, 30),
                        cv::FONT_HERSHEY_SIMPLEX,
                        1.0,
                        cv::Scalar(0, 255, 0),
                        2);

            window.setVideoFrame(matToImage(frame));
            window.setStatus(QStringLiteral("Frame %1×%2 | Faces: %3")
                                 .arg(frame.cols)
                                 .arg(frame.rows)
                                 .arg(faceCount));
        });
        timer.start(33);

        const int rc = app.exec();

        if (camera)
        {
            camera->stop();
            camera->join();
            camera->close();
        }
        return rc;
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
