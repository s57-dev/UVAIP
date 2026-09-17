/*
 * Copyright 2026 S57 ApS
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

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
