# camera_app

C++ camera / UVAP experimentation: webcam capture, virtual camera client, UVAP interfaces, and a V4L2 producer transport.

## Dependencies

- CMake ≥ 3.14
- C++20 compiler
- OpenCV development packages
- Linux V4L2 headers (usually via kernel headers)

Optional (face detect example):

- `libtensorflow-lite-dev`
- `libflatbuffers-dev`

Ubuntu example:

```bash
sudo apt install cmake build-essential libopencv-dev
# optional:
sudo apt install libtensorflow-lite-dev libflatbuffers-dev
```

## Build

From the repo root:

```bash
cmake -B build -DENABLE_FACE_DETECTION=OFF
cmake --build build -j$(nproc)
```

With face detection (when TFLite is installed):

```bash
cmake -B build -DENABLE_FACE_DETECTION=ON
cmake --build build -j$(nproc)
```

Binaries land in `out/`:

- `camera_app`
- `virtcam_client`
- `face_detect` (only if face detection is enabled)

The V4L2 transport library is built as `v4l2_transport` inside the build tree (`transport/v4l2`).

## Tests

```bash
cd build && ctest --output-on-failure
```

## Face detect example

```bash
cd out
./face_detect
```

Run from `out/` so the model at `out/models/face_detection_short_range.tflite` is found. Press `q` then Enter to quit.
