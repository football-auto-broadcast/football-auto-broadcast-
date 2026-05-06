#!/usr/bin/env python3
"""Convert common annotation formats to YOLO detection labels.

Supported input:
  - labelme JSON files with rectangle shapes
  - COCO instance JSON with bbox annotations

Only the football class is exported by default. Empty label files are created
for images without a valid ball annotation so YOLO can train with negatives.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Iterable

try:
    from PIL import Image
except ImportError as exc:  # pragma: no cover
    raise SystemExit("Pillow is required: pip install pillow") from exc


IMAGE_EXTS = {".jpg", ".jpeg", ".png", ".bmp", ".webp"}
DEFAULT_CLASS_MAP = {"ball": 0, "football": 0, "soccer_ball": 0}


def clamp(value: float, low: float, high: float) -> float:
    return max(low, min(high, value))


def bbox_to_yolo(x: float, y: float, w: float, h: float, image_w: int, image_h: int) -> tuple[float, float, float, float]:
    x = clamp(x, 0, image_w - 1)
    y = clamp(y, 0, image_h - 1)
    w = clamp(w, 1, image_w - x)
    h = clamp(h, 1, image_h - y)
    return (
        (x + w / 2.0) / image_w,
        (y + h / 2.0) / image_h,
        w / image_w,
        h / image_h,
    )


def image_size(path: Path) -> tuple[int, int]:
    with Image.open(path) as image:
        return image.size


def iter_images(root: Path) -> Iterable[Path]:
    for path in sorted(root.rglob("*")):
        if path.is_file() and path.suffix.lower() in IMAGE_EXTS:
            yield path


def write_label(label_path: Path, rows: list[str]) -> None:
    label_path.parent.mkdir(parents=True, exist_ok=True)
    label_path.write_text("\n".join(rows) + ("\n" if rows else ""), encoding="utf-8")


def convert_labelme(images_root: Path, annotations_root: Path, labels_root: Path, class_map: dict[str, int]) -> None:
    converted = 0
    for image_path in iter_images(images_root):
        rel = image_path.relative_to(images_root)
        ann_path = annotations_root / rel.with_suffix(".json")
        label_path = labels_root / rel.with_suffix(".txt")
        rows: list[str] = []
        image_w, image_h = image_size(image_path)

        if ann_path.exists():
            data = json.loads(ann_path.read_text(encoding="utf-8"))
            image_w = int(data.get("imageWidth") or image_w)
            image_h = int(data.get("imageHeight") or image_h)
            for shape in data.get("shapes", []):
                label = str(shape.get("label", "")).lower()
                if label not in class_map:
                    continue
                points = shape.get("points", [])
                if len(points) < 2:
                    continue
                xs = [float(p[0]) for p in points]
                ys = [float(p[1]) for p in points]
                x0, x1 = min(xs), max(xs)
                y0, y1 = min(ys), max(ys)
                cx, cy, bw, bh = bbox_to_yolo(x0, y0, x1 - x0, y1 - y0, image_w, image_h)
                rows.append(f"{class_map[label]} {cx:.6f} {cy:.6f} {bw:.6f} {bh:.6f}")
        write_label(label_path, rows)
        converted += 1
    print(f"labelme converted images: {converted}")


def convert_coco(coco_json: Path, images_root: Path, labels_root: Path, class_map: dict[str, int]) -> None:
    data = json.loads(coco_json.read_text(encoding="utf-8"))
    categories = {int(cat["id"]): str(cat["name"]).lower() for cat in data.get("categories", [])}
    images = {int(img["id"]): img for img in data.get("images", [])}
    rows_by_image: dict[int, list[str]] = {image_id: [] for image_id in images}

    for ann in data.get("annotations", []):
        category = categories.get(int(ann.get("category_id", -1)), "")
        if category not in class_map:
            continue
        image = images.get(int(ann.get("image_id", -1)))
        if not image:
            continue
        x, y, w, h = [float(v) for v in ann.get("bbox", [0, 0, 0, 0])]
        cx, cy, bw, bh = bbox_to_yolo(x, y, w, h, int(image["width"]), int(image["height"]))
        rows_by_image[int(image["id"])].append(f"{class_map[category]} {cx:.6f} {cy:.6f} {bw:.6f} {bh:.6f}")

    for image_id, image in images.items():
        image_path = images_root / image["file_name"]
        label_path = labels_root / Path(image["file_name"]).with_suffix(".txt")
        if not image_path.exists():
            print(f"warning: image missing for COCO entry: {image_path}")
        write_label(label_path, rows_by_image.get(image_id, []))
    print(f"coco converted images: {len(images)}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Convert football annotations to YOLO labels.")
    parser.add_argument("--format", choices=["labelme", "coco"], required=True)
    parser.add_argument("--images", required=True, type=Path)
    parser.add_argument("--annotations", required=True, type=Path, help="LabelMe root or COCO json file.")
    parser.add_argument("--output-labels", required=True, type=Path)
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    args.output_labels.mkdir(parents=True, exist_ok=True)
    if args.format == "labelme":
        convert_labelme(args.images, args.annotations, args.output_labels, DEFAULT_CLASS_MAP)
    else:
        convert_coco(args.annotations, args.images, args.output_labels, DEFAULT_CLASS_MAP)


if __name__ == "__main__":
    main()
