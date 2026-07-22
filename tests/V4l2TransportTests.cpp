#include "Frame.h"
#include "FramePipeline.h"
#include "PixelFormatMap.h"

#include <array>
#include <cstddef>
#include <iostream>
#include <memory>
#include <type_traits>

namespace
{

bool expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
    }
    return condition;
}

class TestMemory final : public uvap::IMemory
{
public:
    std::size_t sizeBytes() const override { return bytes_.size(); }
    bool isCpuMapped() const override { return true; }
    void* data() const override
    {
        return const_cast<std::byte*>(bytes_.data());
    }
    uvap::MemoryType type() const override { return uvap::MemoryType::CpuMapped; }
    uvap::BufferState state() const override { return uvap::BufferState::Dequeued; }

private:
    std::array<std::byte, 12> bytes_{};
};

uvap::Frame testFrame()
{
    uvap::FrameInfo info{};
    info.width = 2;
    info.height = 2;
    info.pixelFormat = uvap::PixelFormat::Bgr24;
    return uvap::Frame{info, std::make_unique<TestMemory>()};
}

class TestProducer final : public uvap::IFrameProducer
{
public:
    void start() override {}
    void stop() override {}
    bool getFrame(uvap::Frame& out, std::chrono::milliseconds) override
    {
        ++calls;
        if (!available)
        {
            return false;
        }
        out = testFrame();
        return true;
    }
    uvap::FrameInfo frameInfo() const override { return testFrameInfo; }

    bool available = true;
    int calls = 0;
    uvap::FrameInfo testFrameInfo{2, 2, uvap::PixelFormat::Bgr24};
};

class TestScaler final : public uvap::IScaler
{
public:
    void configure(const uvap::ScaleConfig& config) override { config_ = config; }
    uvap::ScaleConfig configuration() const override { return config_; }
    void scale(const uvap::Frame&, uvap::Frame&) override { ++calls; }

    int calls = 0;

private:
    uvap::ScaleConfig config_{};
};

class TestSink final : public uvap::IFrameSink
{
public:
    void configure(const uvap::SinkConfig& config) override { config_ = config; }
    uvap::SinkConfig configuration() const override { return config_; }
    void start() override {}
    void stop() override {}
    bool acquireFrame(uvap::Frame& out) override
    {
        ++acquireCalls;
        if (!available)
        {
            return false;
        }
        out = testFrame();
        return true;
    }
    bool pushFrame(uvap::Frame frame) override
    {
        ++pushCalls;
        return frame.hasMemory();
    }

    bool available = true;
    int acquireCalls = 0;
    int pushCalls = 0;

private:
    uvap::SinkConfig config_{};
};

} // namespace

int main()
{
    static_assert(!std::is_copy_constructible_v<uvap::Frame>);
    static_assert(!std::is_copy_assignable_v<uvap::Frame>);
    static_assert(std::is_nothrow_move_constructible_v<uvap::Frame>);

    bool passed = true;
    passed &= expect(
        uvap::v4l2::imageSizeBytes(uvap::PixelFormat::Bgr24, 640, 480) ==
            640U * 480U * 3U,
        "BGR24 tightly packed size");
    passed &= expect(
        uvap::v4l2::imageSizeBytes(uvap::PixelFormat::Bgr24, 640, 480, 2048) ==
            2048U * 480U,
        "BGR24 padded stride size");
    passed &= expect(
        uvap::v4l2::imageSizeBytes(uvap::PixelFormat::Bgr24, 640, 480, 100) == 0,
        "reject undersized packed stride");
    passed &= expect(
        uvap::v4l2::imageSizeBytes(uvap::PixelFormat::Nv12, 640, 480) ==
            640U * 480U * 3U / 2U,
        "NV12 even geometry size");
    passed &= expect(
        uvap::v4l2::imageSizeBytes(uvap::PixelFormat::Nv12, 641, 480) == 0,
        "reject odd NV12 width");
    passed &= expect(
        uvap::v4l2::imageSizeBytes(uvap::PixelFormat::Yuv420, 640, 481) == 0,
        "reject odd YUV420 height");
    passed &= expect(
        uvap::v4l2::imageSizeBytes(uvap::PixelFormat::Yuyv, 639, 480) == 0,
        "reject odd packed 4:2:2 width");

    constexpr std::array formats{
        uvap::PixelFormat::Rgb24,
        uvap::PixelFormat::Bgr24,
        uvap::PixelFormat::Gray8,
        uvap::PixelFormat::Yuv420,
        uvap::PixelFormat::Nv12,
        uvap::PixelFormat::Yuv422,
        uvap::PixelFormat::Nv16,
        uvap::PixelFormat::Yuyv,
        uvap::PixelFormat::Uyvy,
    };
    for (const uvap::PixelFormat format : formats)
    {
        const auto fourcc = uvap::v4l2::toV4l2Fourcc(format);
        passed &= expect(fourcc.has_value(), "format maps to V4L2");
        if (fourcc)
        {
            passed &= expect(
                uvap::v4l2::fromV4l2Fourcc(*fourcc) == format,
                "V4L2 format round-trip");
        }
    }
    passed &= expect(
        !uvap::v4l2::toV4l2Fourcc(uvap::PixelFormat::Rgba32).has_value(),
        "reject ambiguous legacy RGBA32 mapping");
    passed &= expect(
        !uvap::v4l2::toV4l2Fourcc(uvap::PixelFormat::Bgra32).has_value(),
        "reject ambiguous legacy BGRA32 mapping");

    TestProducer producer;
    TestScaler scaler;
    TestSink sink;
    uvap::FramePipeline pipeline(producer, scaler, sink);
    passed &= expect(
        pipeline.pump(std::chrono::milliseconds(0)) == uvap::PumpResult::FrameQueued,
        "pipeline queues a transformed frame");
    passed &= expect(
        producer.calls == 1 && scaler.calls == 1 && sink.pushCalls == 1,
        "pipeline invokes each stage exactly once");

    sink.available = false;
    passed &= expect(
        pipeline.pump(std::chrono::milliseconds(0)) == uvap::PumpResult::Backpressure,
        "pipeline reports sink backpressure");
    passed &= expect(producer.calls == 1, "pipeline does not pull source under backpressure");

    sink.available = true;
    producer.available = false;
    passed &= expect(
        pipeline.pump(std::chrono::milliseconds(0)) == uvap::PumpResult::SourceTimeout,
        "pipeline reports source timeout");
    passed &= expect(
        pipeline.stats().framesQueued == 1 &&
            pipeline.stats().backpressureEvents == 1 &&
            pipeline.stats().sourceTimeouts == 1,
        "pipeline exposes basic delivery telemetry");

    return passed ? 0 : 1;
}
