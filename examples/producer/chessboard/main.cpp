#include "ChessboardProducer.h"
#include "SoftwareScaler.h"
#include "V4l2Sink.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

namespace
{

struct Options
{
    std::string device = "/dev/video10";
    int producerWidth = 1280;
    int producerHeight = 720;
    int sinkWidth = 640;
    int sinkHeight = 480;
    int fps = 30;
    int producerPool = 4;
    int sinkPool = 4;
};

void printUsage(const char* argv0)
{
    std::cerr
        << "Usage: " << argv0
        << " [--device PATH] [--fps N]\n"
        << "  Synthetic chessboard producer → software scaler → V4L2 sink.\n"
        << "  Default device: /dev/video10 (v4l2loopback).\n";
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

} // namespace

int main(int argc, char** argv)
{
    try
    {
        const Options opt = parseArgs(argc, argv);

        uvap::FrameInfo producerFormat{};
        producerFormat.width = opt.producerWidth;
        producerFormat.height = opt.producerHeight;
        producerFormat.pixelFormat = uvap::PixelFormat::Bgr24;

        uvap::example::ChessboardProducer producer(
            producerFormat, opt.producerPool, opt.fps);

        uvap::ScaleConfig scaleConfig{};
        scaleConfig.width = opt.sinkWidth;
        scaleConfig.height = opt.sinkHeight;
        scaleConfig.outputFormat = uvap::PixelFormat::Bgr24;

        uvap::example::SoftwareScaler scaler;
        scaler.configure(scaleConfig);

        uvap::SinkConfig sinkConfig{};
        sinkConfig.format.width = opt.sinkWidth;
        sinkConfig.format.height = opt.sinkHeight;
        sinkConfig.format.pixelFormat = uvap::PixelFormat::Bgr24;
        sinkConfig.bufferCount = opt.sinkPool;

        uvap::v4l2::V4l2Sink sink(opt.device);
        sink.configure(sinkConfig);

        producer.start();
        sink.start();

        std::atomic<bool> quit{false};
        std::thread inputThread([&quit]() {
            std::cout << "chessboard_producer streaming to device. Press q then Enter to quit.\n";
            char c = 0;
            std::cin >> c;
            if (c == 'q')
            {
                quit = true;
            }
        });

        while (!quit)
        {
            uvap::Frame sinkFrame;
            if (!sink.acquireFrame(sinkFrame))
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            uvap::Frame producerFrame;
            if (!producer.getFrame(producerFrame, std::chrono::milliseconds(100)))
            {
                // Drop the acquired sink buffer back to the pool by destroying it.
                continue;
            }

            // Scale into the sink buffer; reassign so we keep a single owner.
            sinkFrame = scaler.scale(
                producerFrame,
                sinkFrame,
                [](const uvap::Frame&, const uvap::Frame&) {});

            producerFrame = uvap::Frame{};

            if (!sink.pushFrame(std::move(sinkFrame)))
            {
                std::cerr << "pushFrame failed\n";
            }
        }

        producer.stop();
        sink.stop();

        if (inputThread.joinable())
        {
            inputThread.join();
        }

        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << ex.what() << std::endl;
        return 1;
    }
}
