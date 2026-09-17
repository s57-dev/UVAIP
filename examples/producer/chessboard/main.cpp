/*
 * Copyright 2026 S57 ApS
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

#include "ChessboardProducer.h"
#include "FramePipeline.h"
#include "SoftwareScaler.h"
#include "V4l2LoopbackSession.h"
#include "V4l2Sink.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
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

struct DetachOnExit
{
    std::thread& thread;
    std::atomic<bool>& quit;

    ~DetachOnExit()
    {
        quit = true;
        if (thread.joinable())
        {
            thread.detach();
        }
    }
};

void printUsage(const char* argv0)
{
    std::cerr
        << "Usage: " << argv0
        << " [--device PATH] [--fps N]\n"
        << "  Chessboard → scaler → V4L2 sink. Follows consumer G_FMT/G_PARM on reconnect.\n";
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

int resolveFps(const uvap::v4l2::StreamParm& parm, int fallback)
{
    const int fps = static_cast<int>(parm.fps() + 0.5);
    return fps > 0 ? fps : (fallback > 0 ? fallback : 30);
}

void configurePipeline(uvap::v4l2::V4l2Sink& sink,
                       uvap::example::SoftwareScaler& scaler,
                       uvap::example::ChessboardProducer& producer,
                       int width,
                       int height,
                       int fps,
                       int sinkPool)
{
    uvap::ScaleConfig scaleConfig{};
    scaleConfig.width = width;
    scaleConfig.height = height;
    scaleConfig.outputFormat = uvap::PixelFormat::Bgr24;
    scaler.configure(scaleConfig);

    uvap::SinkConfig sinkConfig{};
    sinkConfig.format.width = width;
    sinkConfig.format.height = height;
    sinkConfig.format.pixelFormat = uvap::PixelFormat::Bgr24;
    sinkConfig.queueDepth = sinkPool;
    sink.configure(sinkConfig);

    producer.setFrameRate(fps);
}

void applyNegotiatedSinkFormat(uvap::v4l2::V4l2Sink& sink,
                               uvap::example::SoftwareScaler& scaler,
                               int& width,
                               int& height)
{
    const uvap::FrameInfo& format = sink.configuration().format;
    if (format.pixelFormat != uvap::PixelFormat::Bgr24)
    {
        throw std::runtime_error(
            "chessboard_producer: sink did not negotiate Bgr24");
    }

    width = format.width;
    height = format.height;
    uvap::ScaleConfig scaleConfig{};
    scaleConfig.width = width;
    scaleConfig.height = height;
    scaleConfig.outputFormat = format.pixelFormat;
    scaler.configure(scaleConfig);
}

bool streamUntilIdle(uvap::v4l2::V4l2Sink& sink,
                     uvap::v4l2::V4l2LoopbackSession& loopback,
                     uvap::FramePipeline& pipeline,
                     uvap::example::ChessboardProducer& producer,
                     std::atomic<bool>& quit)
{
    std::uint32_t clientCount = 1;
    while (!quit && clientCount > 0)
    {
        while (auto ev = loopback.pollClientUsage())
        {
            clientCount = ev->count;
        }
        if (clientCount == 0)
        {
            break;
        }

        try
        {
            const int fps = resolveFps(sink.getStreamParm(), producer.frameRate());
            if (fps != producer.frameRate())
            {
                producer.setFrameRate(fps);
            }

            if (pipeline.pump(std::chrono::milliseconds(100)) !=
                uvap::PumpResult::FrameQueued)
            {
                if (auto ev = loopback.waitClientUsage(std::chrono::milliseconds(20)))
                {
                    clientCount = ev->count;
                }
            }
        }
        catch (const std::exception& ex)
        {
            std::cerr << "chessboard_producer: stream error: " << ex.what() << "\n";
            break;
        }
    }
    return !quit;
}

} // namespace

int main(int argc, char** argv)
{
    std::atomic<bool> quit{false};
    std::thread inputThread;
    DetachOnExit joinGuard{inputThread, quit};

    try
    {
        const Options opt = parseArgs(argc, argv);

        uvap::FrameInfo producerFormat{};
        producerFormat.width = opt.producerWidth;
        producerFormat.height = opt.producerHeight;
        producerFormat.pixelFormat = uvap::PixelFormat::Bgr24;

        uvap::example::ChessboardProducer producer(
            producerFormat, opt.producerPool, opt.fps);
        uvap::example::SoftwareScaler scaler;
        uvap::v4l2::V4l2Sink sink(opt.device);
        uvap::v4l2::V4l2LoopbackSession loopback(sink);
        uvap::FramePipeline pipeline(producer, scaler, sink);

        sink.openDevice();
        loopback.subscribeClientUsage();
        while (loopback.pollClientUsage())
        {
        }

        inputThread = std::thread([&quit]() {
            std::cout << "chessboard_producer. Press q then Enter to quit.\n";
            char c = 0;
            std::cin >> c;
            if (c == 'q')
            {
                quit = true;
            }
        });

        producer.start();

        int sinkWidth = opt.sinkWidth;
        int sinkHeight = opt.sinkHeight;
        int fps = opt.fps;

        configurePipeline(sink, scaler, producer, sinkWidth, sinkHeight, fps, opt.sinkPool);
        sink.start();
        applyNegotiatedSinkFormat(sink, scaler, sinkWidth, sinkHeight);
        sink.setStreamParm({fps, 1});
        std::cout << "chessboard_producer: streaming " << sinkWidth << "x" << sinkHeight
                  << " @ " << fps << " fps\n";

        while (!quit)
        {
            std::uint32_t clientCount = 0;
            try
            {
                if (auto ev = loopback.waitClientUsage(std::chrono::milliseconds(100)))
                {
                    clientCount = ev->count;
                }
                while (auto ev = loopback.pollClientUsage())
                {
                    clientCount = ev->count;
                }
            }
            catch (const std::exception& ex)
            {
                std::cerr << "chessboard_producer: event wait: " << ex.what() << "\n";
                continue;
            }

            if (quit)
            {
                break;
            }

            if (clientCount == 0)
            {
                // Keep OUTPUT STREAMON so capture clients can open, but do not
                // fill the v4l2loopback queue while idle (DQBUF then returns
                // EFAULT and production stalls).
                continue;
            }

            std::cout << "chessboard_producer: client attached\n";
            sink.recycleBuffers();
            if (!streamUntilIdle(sink, loopback, pipeline, producer, quit))
            {
                break;
            }

            std::cout << "chessboard_producer: client detached; reconfigure from G_FMT\n";
            try
            {
                sink.yield();
                std::this_thread::sleep_for(std::chrono::milliseconds(400));

                if (quit)
                {
                    break;
                }

                const uvap::FrameInfo fmt = sink.getFormat();
                if (fmt.width > 0 && fmt.height > 0)
                {
                    sinkWidth = fmt.width;
                    sinkHeight = fmt.height;
                }
                fps = resolveFps(sink.getStreamParm(), fps);

                configurePipeline(
                    sink, scaler, producer, sinkWidth, sinkHeight, fps, opt.sinkPool);
                sink.start();
                applyNegotiatedSinkFormat(sink, scaler, sinkWidth, sinkHeight);
                sink.setStreamParm({fps, 1});
                std::cout << "chessboard_producer: restreaming " << sinkWidth << "x"
                          << sinkHeight << " @ " << fps << " fps\n";
            }
            catch (const std::exception& ex)
            {
                std::cerr << "chessboard_producer: reconfigure failed: " << ex.what()
                          << "\n";
                try
                {
                    if (sink.isStreaming())
                    {
                        sink.yield();
                    }
                    configurePipeline(
                        sink, scaler, producer, sinkWidth, sinkHeight, fps, opt.sinkPool);
                    sink.start();
                    applyNegotiatedSinkFormat(sink, scaler, sinkWidth, sinkHeight);
                }
                catch (const std::exception& recoverEx)
                {
                    std::cerr << "chessboard_producer: recover failed: " << recoverEx.what()
                              << "\n";
                    return 1;
                }
            }
        }

        producer.stop();
        sink.stop();
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << ex.what() << std::endl;
        return 1;
    }
}
