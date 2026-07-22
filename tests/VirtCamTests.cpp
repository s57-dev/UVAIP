#include <gtest/gtest.h>
#include "virtcam/virtcam.h"
#include "camera/ICallBack.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace
{
class ResolutionTestCallback : public ICallback
{
public:
    bool onFrameCapture(const cv::Mat& frame) override
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            lastWidth_ = frame.cols;
            lastHeight_ = frame.rows;
            ++frameCount_;
        }
        cv_.notify_all();
        return running_;
    }

    bool waitForResolution(int width, int height, std::chrono::milliseconds timeout)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        std::unique_lock<std::mutex> lock(mutex_);

        while (std::chrono::steady_clock::now() < deadline)
        {
            if (lastWidth_ == width && lastHeight_ == height)
            {
                return true;
            }

            cv_.wait_until(lock, deadline);
        }

        return lastWidth_ == width && lastHeight_ == height;
    }

    int frameCount() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return frameCount_;
    }

    void stop()
    {
        running_ = false;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    int lastWidth_ = 0;
    int lastHeight_ = 0;
    int frameCount_ = 0;
    std::atomic<bool> running_{true};
};
}

class VirtCamCheckerPatternTest : public ::testing::Test
{
protected:
    static constexpr int initialWidth = 640;
    static constexpr int initialHeight = 480;
    static constexpr int updatedWidth = 1280;
    static constexpr int updatedHeight = 720;

    void SetUp() override
    {
        videoParams params{initialWidth, initialHeight, 30};
        virtcam.setVideoParams(params);
        virtcam.setCallback(&callback);
    }

    void TearDown() override
    {
        callback.stop();
        virtcam.stop();
        virtcam.join();
        virtcam.close();
    }

    void startVirtCam()
    {
        virtcam.open();
    }

    VirtCam virtcam;
    ResolutionTestCallback callback;
};

TEST_F(VirtCamCheckerPatternTest, StartsCheckerPatternAtGivenResolution)
{
    startVirtCam();

    const videoParams params = virtcam.getVideoParams();
    EXPECT_EQ(params.width, initialWidth);
    EXPECT_EQ(params.height, initialHeight);
}

TEST_F(VirtCamCheckerPatternTest, ReceivesFramesViaCallback)
{
    startVirtCam();

    ASSERT_TRUE(callback.waitForResolution(initialWidth, initialHeight, std::chrono::seconds(5)))
        << "Expected checkerboard frames at "
        << initialWidth << "x" << initialHeight;

    EXPECT_GT(callback.frameCount(), 0);
}

TEST_F(VirtCamCheckerPatternTest, ChangesResolutionViaControlInterface)
{
    startVirtCam();

    ASSERT_TRUE(callback.waitForResolution(initialWidth, initialHeight, std::chrono::seconds(5)));

    virtcam.changeResolution(updatedWidth, updatedHeight);

    const videoParams params = virtcam.getVideoParams();
    EXPECT_EQ(params.width, updatedWidth);
    EXPECT_EQ(params.height, updatedHeight);
}

TEST_F(VirtCamCheckerPatternTest, ReturnedFramesHaveCorrectResolutionAfterChange)
{
    startVirtCam();

    ASSERT_TRUE(callback.waitForResolution(initialWidth, initialHeight, std::chrono::seconds(5)));

    virtcam.changeResolution(updatedWidth, updatedHeight);

    ASSERT_TRUE(callback.waitForResolution(updatedWidth, updatedHeight, std::chrono::seconds(5)))
        << "Expected checkerboard frames at "
        << updatedWidth << "x" << updatedHeight;
}
