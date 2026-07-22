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
- `chessboard_producer`
- `face_detect` (only if face detection is enabled)

The V4L2 transport library is built as `v4l2_transport` inside the build tree (`transport/v4l2`).

## Tests

```bash
cd build && ctest --output-on-failure
```

## Chessboard producer (UVAP synthetic VAL)

Requires [v4l2loopback](https://github.com/umlaeute/v4l2loopback), e.g.:

```bash
sudo modprobe v4l2loopback devices=1 video_nr=10 exclusive_caps=1
```

Then:

```bash
cmake --build build --target chessboard_producer
./out/chessboard_producer --device /dev/video10
```

In another terminal, consume the virtual camera:

```bash
ffplay -f v4l2 /dev/video10
# or: opencv VideoCapture on device 10
```

Press `q` then Enter in the producer to quit.

Pipeline: producer CPU pool (1280×720 chessboard) → software scaler → V4L2 sink pool (640×480) → v4l2loopback.

## Face detect example

```bash
cd out
./face_detect --device 0
./face_detect --device /dev/video10
```

Options: `--device PATH|INDEX`, `--model PATH`, `--width N`, `--height N`, `--fps N`.

Run from `out/` so the default model at `out/models/face_detection_short_range.tflite` is found. Press `q` then Enter to quit.
