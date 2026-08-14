# VLC frame tap plugin

This plugin intercepts decoded video frames inside VLC and publishes them as
BGR24 frames to a POSIX shared memory channel. The `vlc_pipeline_client` reads
those frames and writes them to a V4L2 loopback device (for example `/dev/video10`).

## Build

Install VLC development files:

```bash
sudo apt install vlc-plugin-base libvlc-dev
```

Clone VLC 3.0.x source for plugin headers (not included in libvlc-dev):

```bash
git clone --depth 1 --branch 3.0.21 https://github.com/videolan/vlc.git /tmp/vlc-3.0.21
```

Configure and build:

```bash
cmake -B build -DENABLE_VLC_PLUGIN=ON -DVLC_SOURCE_DIR=/tmp/vlc-3.0.21
cmake --build build
```

Outputs:

- `out/vlc_pipeline_client`
- `out/vlc/plugins/video_filter/libframe_tap_plugin.so`

## Usage

1. Start the pipeline client:

```bash
./out/vlc_pipeline_client --shm camera_app_vlc_frames --device /dev/video10
```

2. Play a video in VLC with the frame tap filter enabled:

```bash
vlc --video-filter=frame_tap your_video.mp4
```

Optional plugin option:

```bash
vlc --video-filter=frame_tap --frame-tap-shm=camera_app_vlc_frames your_video.mp4
```

3. Open another app on `/dev/video10` to consume the loopback feed.

## Notes

- The plugin supports common planar YUV (`I420`) and `BGR24` frames.
- Shared memory name must match between VLC and `vlc_pipeline_client`.
- On Linux the shared memory object appears as `/dev/shm/camera_app_vlc_frames`.
