#!/usr/bin/env python3
"""Prepare a single-class YOLO dataset from a Roboflow YOLOv8 export.

The current detector is single-class: class 0 means `ball`.
For Roboflow exports that contain `football` and `others`, this script keeps
only the source football class and rewrites it to class 0.
"""

from __future__ import annotations

import argparse
import shutil
from pathlib import Path


IMAGE_EXTS = {".jpg", ".jpeg", ".png", ".bmp", ".webp"}


def iter_images(path: Path) -> list[Path]:
    return sorted(p for p in path.rglob("*") if p.is_file() and p.suffix.lower() in IMAGE_EXTS)


def rewrite_label(src_label: Path, dst_label: Path, keep_class: int) -> tuple[int, int]:
    dst_label.parent.mkdir(parents=True, exist_ok=True)
    kept = 0
    dropped = 0
    rows: list[str] = []
    if src_label.exists():
        for line in src_label.read_text(encoding="utf-8").splitlines():
            parts = line.strip().split()
            if len(parts) != 5:
                continue
            class_id = int(float(parts[0]))
            if class_id == keep_class:
                rows.append("0 " + " ".join(parts[1:]))
                kept += 1
            else:
                dropped += 1
    dst_label.write_text("\n".join(rows) + ("\n" if rows else ""), encoding="utf-8")
    return kept, dropped


def copy_split(source_root: Path, output_root: Path, split: str, keep_class: int) -> dict:
    src_split = "valid" if split == "val" else split
    src_images = source_root / src_split / "images"
    src_labels = source_root / src_split / "labels"
    dst_images = output_root / "images" / split
    dst_labels = output_root / "labels" / split

    images = iter_images(src_images)
    kept_boxes = 0
    dropped_boxes = 0
    for image in images:
        rel = image.relative_to(src_images)
        dst_image = dst_images / rel
        dst_image.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(image, dst_image)

        src_label = src_labels / rel.with_suffix(".txt")
        dst_label = dst_labels / rel.with_suffix(".txt")
        kept, dropped = rewrite_label(src_label, dst_label, keep_class)
        kept_boxes += kept
        dropped_boxes += dropped

    return {
        "split": split,
        "images": len(images),
        "kept_boxes": kept_boxes,
        "dropped_boxes": dropped_boxes,
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Convert Roboflow football/others dataset to single-class ball dataset.")
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--keep-class", type=int, default=0, help="Source class id to keep. Roboflow football is usually 0.")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    summaries = [copy_split(args.source, args.output, split, args.keep_class) for split in ("train", "val", "test")]
    (args.output / "data.yaml").write_text(
        "path: .\ntrain: images/train\nval: images/val\ntest: images/test\n\nnames:\n  0: ball\n",
        encoding="utf-8",
    )
    for item in summaries:
        print(item)


if __name__ == "__main__":
    main()
