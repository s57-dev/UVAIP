# UVAP

Unified Video AI Pipeline library and reference implementation: portable VAL contracts,
a Linux V4L2 producer transport, a synthetic producer, and sample consumers.

## Dependencies

- CMake ≥ 3.14
- C++20 compiler
- Linux V4L2 headers (usually via kernel headers)

Optional (examples):

- OpenCV development packages
- `libtensorflow-lite-dev`
- `libflatbuffers-dev`
- Qt5 Widgets (`qtbase5-dev`)

Ubuntu example:

```bash
sudo apt install cmake build-essential libopencv-dev
# optional (face_detect):
sudo apt install libtensorflow-lite-dev libflatbuffers-dev qtbase5-dev
```

## Build

From the repo root:

```bash
cmake -B build -DENABLE_FACE_DETECTION=OFF
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

Build and install only the reusable library, without OpenCV:

```bash
cmake -B build -DUVAP_BUILD_EXAMPLES=OFF -DBUILD_TESTING=ON
cmake --build build
cmake --install build --prefix /desired/prefix
```

With face detection (when TFLite + Qt are installed):

```bash
cmake -B build -DENABLE_FACE_DETECTION=ON
cmake --build build -j$(nproc)
```

Binaries land in `out/`:

- `chessboard_producer`
- `face_detect` (only if face detection is enabled)

Installed CMake targets:

- `uvap::core` — move-only frames, typed memory access, producer/scaler/sink contracts
- `uvap::v4l2_transport` — negotiated MMAP VIDEO_OUTPUT queues

`V4l2LoopbackSession` isolates the private `CLIENT_USAGE` extension from
`V4l2Sink`. Sink operations are single-thread confined; acquired frames are
exclusive move-only leases and must be returned before queue recycling or teardown.

## Chessboard producer (UVAP synthetic VAL)

Requires [v4l2loopback](https://github.com/umlaeute/v4l2loopback) (vendored under `v4l2loopback/`), e.g.:

```bash
sudo modprobe v4l2loopback devices=1 video_nr=10 exclusive_caps=1 \
  max_width=1280 max_height=720
```

Then:

```bash
cmake --build build --target chessboard_producer
./out/chessboard_producer --device /dev/video10
```

The producer STREAMONs a default size, watches `CLIENT_USAGE`, and on consumer reconnect reads `G_FMT`/`G_PARM` and reconfigures the scaler + sink to match.

In another terminal:

```bash
ffplay -f v4l2 -framerate 30 /dev/video10
# or face_detect (below)
```

Press `q` then Enter in the producer to quit.

Pipeline: producer CPU pool (1280×720 chessboard) → software scaler → V4L2 sink (consumer format) → v4l2loopback.

## Face detect example

```bash
cd build && cmake .. -DENABLE_FACE_DETECTION=ON && cmake --build . --target face_detect
cd ../out
./face_detect --device /dev/video10
./face_detect --device 0
```

Options: `--device PATH|INDEX`, `--model PATH`, `--width N`, `--height N`, `--fps N`.

GUI: Qt window with resolution combo, FPS spinbox, and **Apply**. On apply the client
closes capture, arms the new format on the loopback device, then reopens.

Run from `out/` so the default model at `out/models/face_detection_short_range.tflite` is found.

Typical demo: start `chessboard_producer`, start `face_detect --device /dev/video10`, change resolution/FPS, click **Apply**.
