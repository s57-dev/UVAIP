#pragma once

#include <opencv2/core.hpp>

#include <memory>
#include <string>

enum class ColorFormat
{
    Gray,
    Bgr,
    Rgb,
    Bgra,
    Rgba,
};

class FaceDetector
{
public:
    explicit FaceDetector(const std::string& modelPath);
    ~FaceDetector();

    FaceDetector(const FaceDetector&) = delete;
    FaceDetector& operator=(const FaceDetector&) = delete;

    int countFaces(const cv::Mat& frame,
                   ColorFormat colorFormat = ColorFormat::Bgr,
                   float confidenceThreshold = 0.5f);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
