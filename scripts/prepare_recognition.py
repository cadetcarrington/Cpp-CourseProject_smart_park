#!/usr/bin/env python3
"""Crop CCPD plates into a PaddleOCR recognition dataset (blue + green).

两种裁剪模式对应两种识别权重，推理侧按同名 crop_recipe.json 复现：

* ``--crop-mode quad``（默认）：按四角透视矫正，与现有权重一致；
* ``--crop-mode bbox``：直接裁检测框（运行时原始裁剪），用于按运行分布重训的权重。
"""

from __future__ import annotations

import argparse
import hashlib
import random
import sys
from collections import Counter
from concurrent.futures import ProcessPoolExecutor, as_completed
from pathlib import Path

import cv2
import numpy as np

import plate_geometry
from plate_geometry import CropRecipe
from prepare_ccpd import SPLITS, ccpd2019_sources, ccpd2020_sources, output_name

PROVINCES = [
    "皖", "沪", "津", "渝", "冀", "晋", "蒙", "辽", "吉", "黑",
    "苏", "浙", "京", "闽", "赣", "鲁", "豫", "鄂", "湘", "粤",
    "桂", "琼", "川", "贵", "云", "藏", "陕", "甘", "青", "宁",
    "新", "警", "学", "O",
]
ALPHABETS = list("ABCDEFGHJKLMNPQRSTUVWXYZO")
ADS = list("ABCDEFGHJKLMNPQRSTUVWXYZ0123456789O")

PLATE_CHARS = (
    "皖沪津渝冀晋蒙辽吉黑苏浙京闽赣鲁豫鄂湘粤桂琼川贵云藏陕甘青宁新"
    "ABCDEFGHJKLMNPQRSTUVWXYZ0123456789"
    "警学港澳使领挂O"
)


def parse_args() -> argparse.Namespace:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ccpd2019", type=Path, default=root / "model/datasets/CCPD2019")
    parser.add_argument("--ccpd2020", type=Path, default=root / "model/datasets/CCPD2020")
    parser.add_argument("--output", type=Path, default=root / "model/datasets/ccpd_rec")
    parser.add_argument("--height", type=int, default=plate_geometry.DEFAULT_HEIGHT)
    parser.add_argument("--min-width", type=int, default=plate_geometry.DEFAULT_MIN_WIDTH)
    parser.add_argument("--max-width", type=int, default=plate_geometry.DEFAULT_MAX_WIDTH)
    parser.add_argument("--expand", type=float, default=plate_geometry.DEFAULT_EXPAND,
                        help="expand warped quad from its center")
    parser.add_argument("--crop-mode", choices=plate_geometry.CROP_MODES, default="quad",
                        help="quad=四角透视矫正（现有权重）；bbox=检测框原始裁剪（运行时分布）")
    parser.add_argument("--bbox-jitter", type=float, default=0.0,
                        help="bbox 模式下按确定性哈希把框向外抖动 0..该比例，模拟检测框松紧")
    parser.add_argument("--workers", type=int, default=8)
    parser.add_argument("--green-repeat", type=int, default=5, help="oversample green plates in train.txt")
    parser.add_argument("--max-per-split", type=int, default=None)
    parser.add_argument("--no-download", action="store_true")
    parser.add_argument("--quality", type=int, default=plate_geometry.DEFAULT_QUALITY)
    return parser.parse_args()


def parse_plate(image: Path) -> tuple[str, np.ndarray, tuple[float, float, float, float]] | None:
    fields = image.stem.split("-")
    if len(fields) < 5:
        return None
    try:
        vertices = []
        for pair in fields[3].split("_"):
            x_text, y_text = pair.split("&", 1)
            vertices.append((float(x_text), float(y_text)))
        if len(vertices) != 4:
            return None
        first, second = fields[2].split("_", 1)
        x1, y1 = (float(value) for value in first.split("&", 1))
        x2, y2 = (float(value) for value in second.split("&", 1))
        indices = [int(value) for value in fields[4].split("_")]
        if len(indices) == 7:
            plate = (
                PROVINCES[indices[0]]
                + ALPHABETS[indices[1]]
                + "".join(ADS[index] for index in indices[2:])
            )
        elif len(indices) == 8:
            plate = (
                PROVINCES[indices[0]]
                + ALPHABETS[indices[1]]
                + "".join(ADS[index] for index in indices[2:])
            )
        else:
            return None
        if any(char not in PLATE_CHARS for char in plate):
            return None
        bounds = (min(x1, x2), min(y1, y2), max(x1, x2), max(y1, y2))
        return plate, np.asarray(vertices, dtype=np.float32), bounds
    except (IndexError, ValueError):
        return None


def jitter_bounds(bounds: tuple[float, float, float, float], ratio: float, seed_text: str):
    """按确定性哈希向外抖动检测框，模拟运行时检测框比标注框松紧不一。"""
    if ratio <= 0:
        return bounds
    digest = hashlib.sha256(("ccpd-bbox-jitter|" + seed_text).encode()).hexdigest()
    generator = random.Random(int(digest[:16], 16))
    x1, y1, x2, y2 = bounds
    width, height = x2 - x1, y2 - y1
    return (x1 - width * generator.uniform(0.0, ratio), y1 - height * generator.uniform(0.0, ratio),
            x2 + width * generator.uniform(0.0, ratio), y2 + height * generator.uniform(0.0, ratio))


def crop_plate(image: Path, recipe: CropRecipe, jitter: float, destination: Path) -> tuple[str, str] | None:
    parsed = parse_plate(image)
    if parsed is None:
        return None
    plate, vertices, bounds = parsed
    source = cv2.imread(str(image), cv2.IMREAD_COLOR)
    if source is None:
        return None
    if recipe.mode == "quad":
        quad = plate_geometry.ccpd_quad(vertices)
        crop = plate_geometry.prepare_crop(source, quad, recipe, mode="quad")
    else:
        crop = plate_geometry.crop_box(source, jitter_bounds(bounds, jitter, str(image.resolve())))
    if crop.size == 0:
        return None
    destination.parent.mkdir(parents=True, exist_ok=True)
    if not plate_geometry.write_crop(destination, crop, recipe):
        return None
    return plate, str(destination)


def crop_job(payload: tuple) -> tuple[str, str, str, str, str] | tuple[str, str]:
    image_s, split, dest_s, recipe, jitter, = payload
    image = Path(image_s)
    destination = Path(dest_s)
    result = crop_plate(image, recipe, jitter, destination)
    if result is None:
        return ("skip", split)
    plate, written = result
    rel = Path(written).name
    kind = "green" if "ccpd_green" in image.parts or "CCPD2020" in image.parts else "blue"
    return ("ok", split, f"images/{split}/{rel}", plate, kind)


def write_plate_dict(path: Path) -> None:
    chars = []
    seen = set()
    for char in PLATE_CHARS:
        if char not in seen:
            chars.append(char)
            seen.add(char)
    path.write_text("\n".join(chars) + "\n", encoding="utf-8")


def main() -> int:
    args = parse_args()
    if args.max_per_split is not None and args.max_per_split < 1:
        raise SystemExit("--max-per-split must be at least 1")
    if args.bbox_jitter < 0:
        raise SystemExit("--bbox-jitter must not be negative")
    recipe = CropRecipe(mode=args.crop_mode, height=args.height, min_width=args.min_width,
                        max_width=args.max_width, expand=args.expand, quality=args.quality)
    output = args.output.expanduser().resolve()
    sources: dict[str, list[Path]] = {name: [] for name in SPLITS}
    notes: list[str] = []

    root2019 = args.ccpd2019.expanduser().resolve()
    if root2019.is_dir():
        split_sources, note = ccpd2019_sources(root2019, not args.no_download)
        notes.append(f"CCPD2019: {note}")
        for split in SPLITS:
            sources[split].extend(split_sources[split])
    else:
        print(f"warning: CCPD2019 directory not found: {root2019}", file=sys.stderr)

    root2020 = args.ccpd2020.expanduser().resolve()
    if root2020.is_dir():
        split_sources = ccpd2020_sources(root2020)
        notes.append("CCPD2020: official ccpd_green train/val/test directories")
        for split in SPLITS:
            sources[split].extend(split_sources[split])
    else:
        print(f"warning: CCPD2020 directory not found: {root2020}", file=sys.stderr)

    if output.exists():
        import shutil
        shutil.rmtree(output)
    for split in SPLITS:
        (output / "images" / split).mkdir(parents=True, exist_ok=True)

    jobs = []
    seen: set[Path] = set()
    counts = Counter()
    for split in SPLITS:
        for image in sorted(sources[split]):
            resolved = image.resolve()
            if resolved in seen:
                continue
            seen.add(resolved)
            if args.max_per_split is not None and counts[split] >= args.max_per_split:
                continue
            counts[split] += 1
            dest = output / "images" / split / output_name(image)
            if dest.suffix.lower() not in {".jpg", ".jpeg"}:
                dest = dest.with_suffix(".jpg")
            jobs.append((str(resolved), split, str(dest), recipe, args.bbox_jitter))

    recs: dict[str, list[tuple[str, str, str]]] = {name: [] for name in SPLITS}
    skipped = Counter()
    kinds = Counter()
    done = 0
    with ProcessPoolExecutor(max_workers=max(1, args.workers)) as pool:
        futures = [pool.submit(crop_job, job) for job in jobs]
        for future in as_completed(futures):
            item = future.result()
            done += 1
            if done % 5000 == 0 or done == len(futures):
                print(f"cropped {done}/{len(futures)}", flush=True)
            if item[0] != "ok":
                skipped[item[1]] += 1
                continue
            _, split, rel, plate, kind = item
            recs[split].append((rel, plate, kind))
            kinds[f"{split}:{kind}"] += 1

    def write_list(path: Path, rows: list[tuple[str, str]], repeat_green: int = 1) -> int:
        lines = []
        for rel, plate, kind in rows:
            n = repeat_green if kind == "green" else 1
            lines.extend([f"{rel}\t{plate}"] * n)
        path.write_text("\n".join(lines) + ("\n" if lines else ""), encoding="utf-8")
        return len(lines)

    for split in SPLITS:
        recs[split].sort()
        repeat = args.green_repeat if split == "train" else 1
        write_list(output / f"{split}.txt", recs[split], repeat_green=repeat)
    green_test = [(rel, plate, kind) for rel, plate, kind in recs["test"] if kind == "green"]
    green_val = [(rel, plate, kind) for rel, plate, kind in recs["val"] if kind == "green"]
    write_list(output / "test_green.txt", green_test)
    write_list(output / "val_green.txt", green_val)
    write_plate_dict(output / "plate_dict.txt")
    recipe.save(output / "crop_recipe.json",
                note="识别推理 scripts/recognize_plate.py --crop-recipe 读同一份配方，"
                     "保证训练裁剪与推理裁剪一致")
    rec_dir = Path(__file__).resolve().parent / "rec"
    rec_dir.mkdir(parents=True, exist_ok=True)
    write_plate_dict(rec_dir / "plate_dict.txt")

    summary = output / "SUMMARY.txt"
    lines = notes + [
        f"output: {output}",
        f"crop_mode: {recipe.mode} (recipe: {output / 'crop_recipe.json'})",
        f"crop_geometry: height={recipe.height} width={recipe.min_width}..{recipe.max_width} "
        f"expand={recipe.expand} quality={recipe.quality} bbox_jitter={args.bbox_jitter}",
        "crops: " + ", ".join(f"{split}={len(recs[split])}" for split in SPLITS),
        "skipped: " + ", ".join(f"{split}={skipped[split]}" for split in SPLITS),
        "kinds: " + ", ".join(f"{k}={v}" for k, v in sorted(kinds.items())),
        f"train_list_green_repeat: {args.green_repeat}",
        "label format: relative_path<TAB>plate_text",
    ]
    summary.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("\n".join(lines))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
