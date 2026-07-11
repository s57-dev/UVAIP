#include "FaceDetector.h"

#include "CameraError.h"

#include <tensorflow/lite/interpreter.h>
#include <tensorflow/lite/kernels/register.h>
#include <tensorflow/lite/model_builder.h>

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
constexpr int kNumAnchors = 896;
constexpr int kNumCoords = 16;
constexpr int kInputSize = 128;
constexpr float kXScale = 128.0f;
constexpr float kYScale = 128.0f;
constexpr float kWScale = 128.0f;
constexpr float kHScale = 128.0f;
constexpr float kScoreClip = 100.0f;
constexpr float kNmsThreshold = 0.3f;

struct Anchor
{
    float x_center;
    float y_center;
    float w;
    float h;
};

struct Detection
{
    float ymin;
    float xmin;
    float ymax;
    float xmax;
    float score;
};

float sigmoid(float value)
{
    value = std::clamp(value, -kScoreClip, kScoreClip);
    return 1.0f / (1.0f + std::exp(-value));
}

float intersectionOverUnion(const Detection& a, const Detection& b)
{
    const float ymin = std::max(a.ymin, b.ymin);
    const float xmin = std::max(a.xmin, b.xmin);
    const float ymax = std::min(a.ymax, b.ymax);
    const float xmax = std::min(a.xmax, b.xmax);

    const float intersectionWidth = std::max(0.0f, xmax - xmin);
    const float intersectionHeight = std::max(0.0f, ymax - ymin);
    const float intersectionArea = intersectionWidth * intersectionHeight;

    const float areaA = std::max(0.0f, a.ymax - a.ymin) * std::max(0.0f, a.xmax - a.xmin);
    const float areaB = std::max(0.0f, b.ymax - b.ymin) * std::max(0.0f, b.xmax - b.xmin);
    const float unionArea = areaA + areaB - intersectionArea;

    return unionArea > 0.0f ? intersectionArea / unionArea : 0.0f;
}

std::vector<Anchor> generateBlazeFaceAnchors()
{
    const int numLayers = 4;
    const int inputWidth = kInputSize;
    const int inputHeight = kInputSize;
    const float minScale = 0.1484375f;
    const float maxScale = 0.75f;
    const float anchorOffsetX = 0.5f;
    const float anchorOffsetY = 0.5f;
    const float interpolatedScaleAspectRatio = 1.0f;
    const std::vector<int> strides = {8, 16, 16, 16};
    const std::vector<float> aspectRatios = {1.0f};

    auto calculateScale = [&](int strideIndex) {
        return minScale + (maxScale - minScale) * static_cast<float>(strideIndex) /
                              static_cast<float>(numLayers - 1);
    };

    std::vector<Anchor> anchors;
    anchors.reserve(kNumAnchors);

    int layerId = 0;
    while (layerId < numLayers)
    {
        std::vector<float> anchorHeights;
        std::vector<float> anchorWidths;
        std::vector<float> scales;
        std::vector<float> layerAspectRatios;

        int lastSameStrideLayer = layerId;
        while (lastSameStrideLayer < numLayers &&
               strides[lastSameStrideLayer] == strides[layerId])
        {
            const float scale = calculateScale(lastSameStrideLayer);

            for (const float aspectRatio : aspectRatios)
            {
                layerAspectRatios.push_back(aspectRatio);
                scales.push_back(scale);
            }

            if (interpolatedScaleAspectRatio > 0.0f)
            {
                const float scaleNext = (lastSameStrideLayer == numLayers - 1)
                                            ? 1.0f
                                            : calculateScale(lastSameStrideLayer + 1);
                scales.push_back(std::sqrt(scale * scaleNext));
                layerAspectRatios.push_back(interpolatedScaleAspectRatio);
            }

            ++lastSameStrideLayer;
        }

        for (size_t i = 0; i < layerAspectRatios.size(); ++i)
        {
            const float ratioSqrt = std::sqrt(layerAspectRatios[i]);
            anchorHeights.push_back(scales[i] / ratioSqrt);
            anchorWidths.push_back(scales[i] * ratioSqrt);
        }

        const int stride = strides[layerId];
        const int featureMapHeight =
            static_cast<int>(std::ceil(static_cast<float>(inputHeight) / stride));
        const int featureMapWidth =
            static_cast<int>(std::ceil(static_cast<float>(inputWidth) / stride));

        for (int y = 0; y < featureMapHeight; ++y)
        {
            for (int x = 0; x < featureMapWidth; ++x)
            {
                for (size_t anchorId = 0; anchorId < anchorHeights.size(); ++anchorId)
                {
                    Anchor anchor{};
                    anchor.x_center =
                        (x + anchorOffsetX) / static_cast<float>(featureMapWidth);
                    anchor.y_center =
                        (y + anchorOffsetY) / static_cast<float>(featureMapHeight);
                    anchor.w = 1.0f;
                    anchor.h = 1.0f;
                    anchors.push_back(anchor);
                }
            }
        }

        layerId = lastSameStrideLayer;
    }

    if (static_cast<int>(anchors.size()) != kNumAnchors)
    {
        throw CameraError("Unexpected anchor count: " + std::to_string(anchors.size()));
    }

    return anchors;
}

std::vector<Detection> decodeDetections(const float* regressors,
                                        const float* scores,
                                        const std::vector<Anchor>& anchors,
                                        float confidenceThreshold)
{
    std::vector<Detection> detections;

    for (int i = 0; i < kNumAnchors; ++i)
    {
        const float score = sigmoid(scores[i]);
        if (score < confidenceThreshold)
        {
            continue;
        }

        const float* rawBox = regressors + i * kNumCoords;
        const Anchor& anchor = anchors[i];

        const float xCenter =
            rawBox[0] / kXScale * anchor.w + anchor.x_center;
        const float yCenter =
            rawBox[1] / kYScale * anchor.h + anchor.y_center;
        const float width = rawBox[2] / kWScale * anchor.w;
        const float height = rawBox[3] / kHScale * anchor.h;

        Detection detection{};
        detection.ymin = yCenter - height / 2.0f;
        detection.xmin = xCenter - width / 2.0f;
        detection.ymax = yCenter + height / 2.0f;
        detection.xmax = xCenter + width / 2.0f;
        detection.score = score;
        detections.push_back(detection);
    }

    return detections;
}

std::vector<Detection> suppressOverlaps(std::vector<Detection> detections)
{
    std::sort(detections.begin(), detections.end(),
              [](const Detection& a, const Detection& b) { return a.score > b.score; });

    std::vector<Detection> kept;
    std::vector<bool> removed(detections.size(), false);

    for (size_t i = 0; i < detections.size(); ++i)
    {
        if (removed[i])
        {
            continue;
        }

        kept.push_back(detections[i]);

        for (size_t j = i + 1; j < detections.size(); ++j)
        {
            if (!removed[j] &&
                intersectionOverUnion(detections[i], detections[j]) > kNmsThreshold)
            {
                removed[j] = true;
            }
        }
    }

    return kept;
}

void preprocessFrame(const cv::Mat& frame, float* inputTensor)
{
    cv::Mat resized;
    cv::resize(frame, resized, cv::Size(kInputSize, kInputSize));

    cv::Mat rgb;
    if (resized.channels() == 1)
    {
        cv::cvtColor(resized, rgb, cv::COLOR_GRAY2RGB);
    }
    else if (resized.channels() == 4)
    {
        cv::cvtColor(resized, rgb, cv::COLOR_BGRA2RGB);
    }
    else
    {
        cv::cvtColor(resized, rgb, cv::COLOR_BGR2RGB);
    }

    for (int y = 0; y < kInputSize; ++y)
    {
        for (int x = 0; x < kInputSize; ++x)
        {
            const cv::Vec3b pixel = rgb.at<cv::Vec3b>(y, x);
            const int index = (y * kInputSize + x) * 3;
            inputTensor[index + 0] = static_cast<float>(pixel[0]) / 127.5f - 1.0f;
            inputTensor[index + 1] = static_cast<float>(pixel[1]) / 127.5f - 1.0f;
            inputTensor[index + 2] = static_cast<float>(pixel[2]) / 127.5f - 1.0f;
        }
    }
}
} // namespace

struct FaceDetector::Impl
{
    std::unique_ptr<tflite::FlatBufferModel> model;
    std::unique_ptr<tflite::Interpreter> interpreter;
    std::vector<Anchor> anchors;

    explicit Impl(const std::string& modelPath)
    {
        model = tflite::FlatBufferModel::BuildFromFile(modelPath.c_str());
        if (!model)
        {
            throw CameraError("Failed to load face detection model: " + modelPath);
        }

        tflite::ops::builtin::BuiltinOpResolver resolver;
        if (tflite::InterpreterBuilder(*model, resolver)(&interpreter) != kTfLiteOk ||
            !interpreter)
        {
            throw CameraError("Failed to create TFLite interpreter");
        }

        if (interpreter->AllocateTensors() != kTfLiteOk)
        {
            throw CameraError("Failed to allocate TFLite tensors");
        }

        if (interpreter->inputs().size() != 1 || interpreter->outputs().size() != 2)
        {
            throw CameraError("Unexpected face detection model I/O layout");
        }

        anchors = generateBlazeFaceAnchors();
    }

    int countFaces(const cv::Mat& frame, float confidenceThreshold)
    {
        if (frame.empty())
        {
            return 0;
        }

        float* inputTensor = interpreter->typed_input_tensor<float>(0);
        if (!inputTensor)
        {
            throw CameraError("Face detection model input tensor is not float32");
        }

        preprocessFrame(frame, inputTensor);

        if (interpreter->Invoke() != kTfLiteOk)
        {
            throw CameraError("TFLite inference failed");
        }

        const float* regressors = interpreter->typed_output_tensor<float>(0);
        const float* scores = interpreter->typed_output_tensor<float>(1);
        if (!regressors || !scores)
        {
            throw CameraError("Face detection model output tensors are not float32");
        }

        auto detections = decodeDetections(regressors, scores, anchors, confidenceThreshold);
        detections = suppressOverlaps(std::move(detections));
        return static_cast<int>(detections.size());
    }
};

FaceDetector::FaceDetector(const std::string& modelPath)
    : impl_(std::make_unique<Impl>(modelPath))
{
}

FaceDetector::~FaceDetector() = default;

int FaceDetector::countFaces(const cv::Mat& frame, float confidenceThreshold)
{
    return impl_->countFaces(frame, confidenceThreshold);
}
