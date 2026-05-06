#!/usr/bin/env python3
"""Render YOLO labels on images for manual QA."""

from __future__ import annotations

import argparse
import random
from pathlib import Path

try:
    import cv2
except ImportError as exc:  # pragma: no cover
    raise SystemExit("OpenCV is required: pip install opencv-python") from exc


IMAGE_EXTS = {".jpg", ".jpeg", ".png", ".bmp", ".webp"}
CLASS_NAMES = {0: "ball"}


def iter_images(root: Path) -> list[Path]:
    return sorted(path for path in root.rglob("*") if path.is_file() and path.suffix.lower() in IMAGE_EXTS)


def draw_label(image, row: str) -> None:
    parts = row.strip().split()
    if len(parts) != 5:
        return
    class_id = int(float(parts[0]))
    cx, cy, bw, bh = [float(v) for v in parts[1:]]
    h, w = image.shape[:2]
    x1 = int((cx - bw / 2.0) * w)
    y1 = int((cy - bh / 2.0) * h)
    x2 = int((cx + bw / 2.0) * w)
    y2 = int((cy + bh / 2.0) * h)
    color = (0, 255, 255)
    cv2.rectangle(image, (x1, y1), (x2, y2), color, 2)
    cv2.putText(image, CLASS_NAMES.get(class_id, str(class_id)), (x1, max(20, y1 - 6)),
                cv2.FONT_HERSHEY_SIMPLEX, 0.7, color, 2, cv2.LINE_AA)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Visualize YOLO labels.")
    parser.add_argument("--images", required=True, type=Path)
    parser.add_argument("--labels", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--sample", type=int, default=200, help="Max images to render. 0 means all.")
    parser.add_argument("--seed", type=int, default=2026)
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    images = iter_images(args.images)
    if args.sample > 0 and len(images) > args.sample:
        rng = random.Random(args.seed)
        images = rng.sample(images, args.sample)

    rendered = 0
    for image_path in images:
        rel = image_path.relative_to(args.images)
        label_path = args.labels / rel.with_suffix(".txt")
        image = cv2.imread(str(image_path))
        if image is None:
            print(f"warning: failed to read image: {image_path}")
            continue
        if label_path.exists():
            for row in label_path.read_text(encoding="utf-8").splitlines():
                draw_label(image, row)
        output_path = args.output / rel
        output_path.parent.mkdir(parents=True, exist_ok=True)
        cv2.imwrite(str(output_path), image)
        rendered += 1

    print(f"rendered QA images: {rendered}")


if __name__ == "__main__":
    main()
