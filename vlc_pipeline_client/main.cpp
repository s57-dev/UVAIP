#include "CameraError.h"
#include "FrameQueue.h"
#include "shared_frame_channel.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <linux/videodev2.h>
#include <opencv2/opencv.hpp>
#include <string>
#include <sys/ioctl.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace
{
int setup_v4l2_output(const std::string& device, int width, int height)
{
    const int fd = open(device.c_str(), O_WRONLY);
    if (fd < 0)
    {
        throw CameraError("Cannot open " + device + " for writing");
    }

    struct v4l2_format vid_format{};
    vid_format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    vid_format.fmt.pix.width = width;
    vid_format.fmt.pix.height = height;
    vid_format.fmt.pix.pixelformat = V4L2_PIX_FMT_BGR24;
    vid_format.fmt.pix.sizeimage = width * height * 3;
    vid_format.fmt.pix.field = V4L2_FIELD_NONE;

    if (ioctl(fd, VIDIOC_S_FMT, &vid_format) < 0)
    {
        close(fd);
        throw CameraError("Failed to set V4L2 format on " + device);
    }

    return fd;
}

void write_frame(int fd, const uint8_t* data, size_t bytes)
{
    const ssize_t written = write(fd, data, bytes);
    if (written < 0)
    {
        throw CameraError("Failed to write frame to loopback device");
    }
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        std::string shm_name = SFC_DEFAULT_SHM_NAME;
        std::string v4l2_device = "/dev/video10";
        int output_width = 640;
        int output_height = 480;

        for (int i = 1; i < argc; ++i)
        {
            const std::string arg = argv[i];
            if (arg == "--shm" && i + 1 < argc)
            {
                shm_name = argv[++i];
            }
            else if (arg == "--device" && i + 1 < argc)
            {
                v4l2_device = argv[++i];
            }
            else if (arg == "--width" && i + 1 < argc)
            {
                output_width = std::stoi(argv[++i]);
            }
            else if (arg == "--height" && i + 1 < argc)
            {
                output_height = std::stoi(argv[++i]);
            }
        }

        const int v4l2_fd = setup_v4l2_output(v4l2_device, output_width, output_height);
        const size_t frame_bytes = static_cast<size_t>(output_width) * output_height * 3;
        std::vector<uint8_t> black_frame(frame_bytes, 0);
        std::vector<uint8_t> frame_buffer(SFC_SLOT_DATA_BYTES);

        std::atomic<bool> quit{false};
        std::thread inputThread([&quit]() {
            std::cout << "Pipeline client running on loopback. Press q then Enter to quit."
                      << std::endl;
            char c = 0;
            std::cin >> c;
            if (c == 'q')
            {
                quit = true;
            }
        });

        sfc_consumer_t consumer{};
        bool shm_connected = false;
        auto last_wait_message = std::chrono::steady_clock::now();
        cv::Mat latestFrame;

        std::cout << "Writing " << output_width << "x" << output_height
                  << " to " << v4l2_device << ". Waiting for VLC shared memory '"
                  << shm_name << "'..." << std::endl;
        std::cout << "Start VLC with: vlc --video-filter=frame_tap your_video.mp4"
                  << std::endl;

        while (!quit)
        {
            if (!shm_connected)
            {
                if (sfc_consumer_open(&consumer, shm_name.c_str()) == 0)
                {
                    shm_connected = true;
                    std::cout << "Connected to shared memory '" << shm_name << "'." << std::endl;
                }
                else
                {
                    const auto now = std::chrono::steady_clock::now();
                    if (now - last_wait_message >= std::chrono::seconds(3))
                    {
                        std::cout << "Still waiting for VLC (frame_tap filter). "
                                  << "Loopback signal is active; camera_app can connect."
                                  << std::endl;
                        last_wait_message = now;
                    }
                }

                write_frame(v4l2_fd, black_frame.data(), frame_bytes);
                std::this_thread::sleep_for(std::chrono::milliseconds(33));
                continue;
            }

            uint32_t width = 0;
            uint32_t height = 0;
            uint32_t stride = 0;
            const int read_result =
                sfc_consumer_read_latest_bgr24(&consumer, frame_buffer.data(), frame_buffer.size(),
                                               &width, &height, &stride);

            if (read_result == 1)
            {
                cv::Mat frame(height, width, CV_8UC3, frame_buffer.data(), stride);

                if (frame.cols != output_width || frame.rows != output_height)
                {
                    cv::Mat resized;
                    cv::resize(frame, resized, cv::Size(output_width, output_height));
                    latestFrame = std::move(resized);
                }
                else
                {
                    latestFrame = frame.clone();
                }

                write_frame(v4l2_fd, latestFrame.data,
                            latestFrame.total() * latestFrame.elemSize());

                std::cout << "VLC frame " << width << "x" << height << " -> " << v4l2_device
                          << std::endl;
            }
            else
            {
                write_frame(v4l2_fd, black_frame.data(), frame_bytes);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }

        close(v4l2_fd);
        if (shm_connected)
        {
            sfc_consumer_close(&consumer);
        }

        if (inputThread.joinable())
        {
            inputThread.join();
        }

        return 0;
    }
    catch (const CameraError& e)
    {
        std::cerr << e.what() << std::endl;
        return 1;
    }
}
