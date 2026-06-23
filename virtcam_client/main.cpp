#include "camera.h"
#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <opencv2/opencv.hpp>

// Helper function to handle V4L2 device configuration
int setup_v4l2_output(const std::string& device, int width, int height) {
    int fd = open(device.c_str(), O_WRONLY);
    if (fd < 0) {
        std::cerr << "Error: Cannot open " << device << " for writing." << std::endl;
        return -1;
    }

    struct v4l2_format vid_format;
    memset(&vid_format, 0, sizeof(vid_format));
    vid_format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    vid_format.fmt.pix.width = width;
    vid_format.fmt.pix.height = height;
    vid_format.fmt.pix.pixelformat = V4L2_PIX_FMT_BGR24; // Standard OpenCV format
    vid_format.fmt.pix.sizeimage = width * height * 3;
    vid_format.fmt.pix.field = V4L2_FIELD_NONE;

    if (ioctl(fd, VIDIOC_S_FMT, &vid_format) < 0) {
        std::cerr << "Error: Failed to set V4L2 format details." << std::endl;
        close(fd);
        return -1;
    }
    return fd;
}

class FrameCapture : public ICallback
{
    int v4l2_fd;
  
    
public:
    FrameCapture(int fd){
        v4l2_fd = fd;
    }

    bool onFrameCapture(const cv::Mat& frame) override
    {
       // cv::imshow("Capture", frame);
        ssize_t bytes_written = write(v4l2_fd, frame.data, frame.total() * frame.elemSize());
        return cv::waitKey(1) != 'q';
    }

};

int main()
{

    int width = 640;
    int height = 480;
    Camera camera(-1);
     // Open and configure /dev/video10 for writing
     int v4l2_fd = setup_v4l2_output("/dev/video10", width, height);
     if (v4l2_fd < 0) {
         return -1;
     }

    FrameCapture capture(v4l2_fd);
    camera.setResolution(width, height);
    camera.setCallback(&capture);

    if (!camera.open())
    {
        return -1;
    }

    camera.run();
    camera.close();
    close(v4l2_fd);
    return 0;
}


