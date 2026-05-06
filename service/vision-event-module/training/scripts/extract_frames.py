#!/usr/bin/env python3
"""Extract training frames from cam_01/cam_02 videos.

Examples:
  python scripts/extract_frames.py --input datasets/football/videos/cam_01 --output datasets/football/frames_unlabeled/cam_01 --fps 2 --camera-id cam_01
  python scripts/extract_frames.py --input datasets/football/videos --output datasets/football/frames_unlabeled --every-n 25
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Iterable

try:
    import cv2
except ImportError as exc:  # pragma: no cover
    raise SystemExit("OpenCV is required: pip install opencv-python") from exc


VIDEO_EXTS = {".mp4", ".mov", ".avi", ".mkv", ".m4v"}


def iter_videos(path: Path) -> Iterable[Path]:
    if path.is_file() and path.suffix.lower() in VIDEO_EXTS:
        yield path
        return
    for item in sorted(path.rglob("*")):
        if item.is_file() and item.suffix.lower() in VIDEO_EXTS:
            yield item


def infer_camera_id(video_path: Path, explicit_camera_id: str | None) -> str:
    if explicit_camera_id:
        return explicit_camera_id
    parts = {part.lower() for part in video_path.parts}
    if "cam_02" in parts or "cam02" in parts:
        return "cam_02"
    return "cam_01"


def extract_video(video_path: Path, output_root: Path, args: argparse.Namespace) -> dict:
    camera_id = infer_camera_id(video_path, args.camera_id)
    output_dir = output_root / camera_id
    output_dir.mkdir(parents=True, exist_ok=True)

    cap = cv2.VideoCapture(str(video_path))
    if not cap.isOpened():
        raise RuntimeError(f"failed to open video: {video_path}")

    source_fps = cap.get(cv2.CAP_PROP_FPS) or 25.0
    frame_count = int(cap.get(cv2.CAP_PROP_FRAME_COUNT) or 0)
    if args.every_n:
        stride = max(1, args.every_n)
    else:
        stride = max(1, round(source_fps / max(args.fps, 0.1)))

    saved = 0
    index = 0
    while True:
        ok, frame = cap.read()
        if not ok:
            break
        if index % stride == 0:
            timestamp_ms = int(index * 1000.0 / source_fps)
            stem = f"{camera_id}_{video_path.stem}_{index:08d}_{timestamp_ms:010d}ms"
            image_path = output_dir / f"{stem}.jpg"
            cv2.imwrite(str(image_path), frame, [int(cv2.IMWRITE_JPEG_QUALITY), args.quality])
            saved += 1
        index += 1

    cap.release()
    return {
        "video": str(video_path),
        "camera_id": camera_id,
        "source_fps": source_fps,
        "frame_count": frame_count,
        "stride": stride,
        "saved_frames": saved,
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Extract frames for football detector labeling.")
    parser.add_argument("--input", required=True, type=Path, help="Video file or directory.")
    parser.add_argument("--output", required=True, type=Path, help="Output frame root directory.")
    parser.add_argument("--camera-id", choices=["cam_01", "cam_02"], default=None)
    parser.add_argument("--fps", type=float, default=2.0, help="Target extraction FPS when --every-n is not set.")
    parser.add_argument("--every-n", type=int, default=0, help="Save one frame every N source frames.")
    parser.add_argument("--quality", type=int, default=92, help="JPEG quality.")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    summaries = [extract_video(video, args.output, args) for video in iter_videos(args.input)]
    manifest_path = args.output / "extract_manifest.json"
    manifest_path.write_text(json.dumps(summaries, ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"extracted {sum(item['saved_frames'] for item in summaries)} frames from {len(summaries)} videos")
    print(f"manifest: {manifest_path}")


if __name__ == "__main__":
    main()
