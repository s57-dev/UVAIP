#!/usr/bin/env bash
# Feed a video file into /dev/video10 (v4l2loopback) using ffmpeg.
# Use this when VLC/frame_tap is unavailable. Leave running, then start camera_app.

set -euo pipefail

DEVICE="${1:-/dev/video10}"
VIDEO="${2:-$HOME/development/camera/camera_app/my_video.mp4}"
WIDTH="${3:-640}"
HEIGHT="${4:-480}"

if [[ ! -e "$DEVICE" ]]; then
  echo "Missing $DEVICE. Run: sudo modprobe v4l2loopback video_nr=10"
  exit 1
fi

if [[ ! -f "$VIDEO" ]]; then
  echo "Missing video file: $VIDEO"
  exit 1
fi

echo "Feeding $VIDEO -> $DEVICE at ${WIDTH}x${HEIGHT} (Ctrl+C to stop)"
exec ffmpeg -hide_banner -loglevel warning -re -stream_loop -1 \
  -i "$VIDEO" \
  -vf "scale=${WIDTH}:${HEIGHT}" \
  -pix_fmt bgr24 \
  -f v4l2 \
  "$DEVICE"
