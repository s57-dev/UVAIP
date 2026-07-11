#include "virtcam.h"
#include "CameraError.h"
#include "FrameQueue.h"
#include <atomic>
#include <chrono>
#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <opencv2/opencv.hpp>
#include <thread>

int setup_v4l2_output(const std::string& device, int width, int height) {
    int fd = open(device.c_str(), O_WRONLY);
    if (fd < 0) {
        throw CameraError("Cannot open " + device + " for writing");
    }

    struct v4l2_format vid_format;
    memset(&vid_format, 0, sizeof(vid_format));
    vid_format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    vid_format.fmt.pix.width = width;
    vid_format.fmt.pix.height = height;
    vid_format.fmt.pix.pixelformat = V4L2_PIX_FMT_BGR24;
    vid_format.fmt.pix.sizeimage = width * height * 3;
    vid_format.fmt.pix.field = V4L2_FIELD_NONE;

    if (ioctl(fd, VIDIOC_S_FMT, &vid_format) < 0) {
        close(fd);
        throw CameraError("Failed to set V4L2 format on " + device);
    }
    return fd;
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

int main()
{
    try
    {
        const videoParams params = {640, 480, 30};

        int v4l2_fd = setup_v4l2_output("/dev/video10", params.width, params.height);

        FrameQueue frameQueue;
        std::atomic<bool> quit{false};

        std::thread inputThread([&quit]() {
            std::cout << "Streaming frames from queue to /dev/video10. Press q then Enter to quit." << std::endl;
            char c = 0;
            std::cin >> c;
            if (c == 'q')
            {
                quit = true;
            }
        });

        VirtCam virtcam;
        FrameCapture capture(frameQueue);

        virtcam.setVideoParams(params);
        virtcam.setCallback(&capture);
        virtcam.open();

        cv::Mat latestFrame;
        while (!quit)
        {
            cv::Mat frame;
            if (frameQueue.waitPop(frame, std::chrono::milliseconds(33)))
            {
                latestFrame = std::move(frame);
                write(v4l2_fd, latestFrame.data, latestFrame.total() * latestFrame.elemSize());
                //cv::imshow("Latest Frame", latestFrame);
                cv::waitKey(1);
                std::cout << "Queue size: " << frameQueue.size()
                          << " | Latest frame: " << latestFrame.cols
                          << "x" << latestFrame.rows << std::endl;
            }
        }


        virtcam.stop();
        virtcam.join();
        virtcam.close();
        close(v4l2_fd);

        if (inputThread.joinable())
        {
            //join
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
