#!/usr/bin/env python3
"""Split YOLO images/labels into train/val/test folders."""

from __future__ import annotations

import argparse
import random
import shutil
from pathlib import Path


IMAGE_EXTS = {".jpg", ".jpeg", ".png", ".bmp", ".webp"}


def list_images(path: Path) -> list[Path]:
    return sorted(p for p in path.rglob("*") if p.is_file() and p.suffix.lower() in IMAGE_EXTS)


def copy_pair(image_path: Path, label_root: Path, output_root: Path, split: str, source_root: Path) -> None:
    rel = image_path.relative_to(source_root)
    label_rel = rel.with_suffix(".txt")
    label_path = label_root / label_rel

    target_image = output_root / "images" / split / rel
    target_label = output_root / "labels" / split / label_rel
    target_image.parent.mkdir(parents=True, exist_ok=True)
    target_label.parent.mkdir(parents=True, exist_ok=True)

    shutil.copy2(image_path, target_image)
    if label_path.exists():
        shutil.copy2(label_path, target_label)
    else:
        target_label.write_text("", encoding="utf-8")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Create YOLO train/val/test split.")
    parser.add_argument("--images", required=True, type=Path, help="Source image root.")
    parser.add_argument("--labels", required=True, type=Path, help="Source YOLO label root.")
    parser.add_argument("--output", required=True, type=Path, help="Output YOLO dataset root.")
    parser.add_argument("--train", type=float, default=0.8)
    parser.add_argument("--val", type=float, default=0.15)
    parser.add_argument("--test", type=float, default=0.05)
    parser.add_argument("--seed", type=int, default=2026)
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    total_ratio = args.train + args.val + args.test
    if abs(total_ratio - 1.0) > 1e-6:
        raise SystemExit("train + val + test must equal 1.0")

    images = list_images(args.images)
    rng = random.Random(args.seed)
    rng.shuffle(images)

    train_end = int(len(images) * args.train)
    val_end = train_end + int(len(images) * args.val)
    splits = {
        "train": images[:train_end],
        "val": images[train_end:val_end],
        "test": images[val_end:],
    }

    for split, split_images in splits.items():
        for image_path in split_images:
            copy_pair(image_path, args.labels, args.output, split, args.images)

    print({split: len(items) for split, items in splits.items()})


if __name__ == "__main__":
    main()
