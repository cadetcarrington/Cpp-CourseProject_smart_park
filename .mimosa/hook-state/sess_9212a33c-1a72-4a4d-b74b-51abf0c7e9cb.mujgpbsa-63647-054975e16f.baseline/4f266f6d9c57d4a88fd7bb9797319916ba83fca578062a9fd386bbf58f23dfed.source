#!/usr/bin/env python3
"""Crop CCPD plates into a PaddleOCR recognition dataset (blue + green)."""

from __future__ import annotations

import argparse
import hashlib
import sys
from collections import Counter
from concurrent.futures import ProcessPoolExecutor, as_completed
from pathlib import Path

import cv2
import numpy as np

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
    parser.add_argument("--height", type=int, default=64)
    parser.add_argument("--min-width", type=int, default=160)
    parser.add_argument("--max-width", type=int, default=320)
    parser.add_argument("--expand", type=float, default=0.06, help="expand warped quad from its center")
    parser.add_argument("--workers", type=int, default=8)
    parser.add_argument("--green-repeat", type=int, default=5, help="oversample green plates in train.txt")
    parser.add_argument("--max-per-split", type=int, default=None)
    parser.add_argument("--no-download", action="store_true")
    parser.add_argument("--quality", type=int, default=95)
    return parser.parse_args()


def parse_plate(image: Path) -> tuple[str, np.ndarray] | None:
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
        return plate, np.asarray(vertices, dtype=np.float32)
    except (IndexError, ValueError):
        return None


def expand_quad(points: np.ndarray, ratio: float) -> np.ndarray:
    center = points.mean(axis=0, keepdims=True)
    return center + (1.0 + ratio) * (points - center)


def ordered_quad(points: np.ndarray) -> np.ndarray:
    # CCPD stores RB, LB, LT, RT clockwise. Fall back to a stable sort if needed.
    if points.shape == (4, 2):
        rb, lb, lt, rt = points
        ordered = np.stack([lt, rt, rb, lb], axis=0)
        if np.linalg.norm(ordered[0] - ordered[1]) > 1 and np.linalg.norm(ordered[0] - ordered[3]) > 1:
            return ordered.astype(np.float32)
    pts = points.astype(np.float32)
    total = pts.sum(axis=1)
    diff = np.diff(pts, axis=1).reshape(-1)
    lt = pts[np.argmin(total)]
    rb = pts[np.argmax(total)]
    rt = pts[np.argmin(diff)]
    lb = pts[np.argmax(diff)]
    return np.stack([lt, rt, rb, lb], axis=0)


def crop_plate(image: Path, height: int, min_width: int, max_width: int, expand: float, quality: int, destination: Path) -> tuple[str, str] | None:
    parsed = parse_plate(image)
    if parsed is None:
        return None
    plate, vertices = parsed
    source = cv2.imread(str(image), cv2.IMREAD_COLOR)
    if source is None:
        return None
    quad = ordered_quad(expand_quad(vertices, expand))
    width_top = float(np.linalg.norm(quad[1] - quad[0]))
    width_bottom = float(np.linalg.norm(quad[2] - quad[3]))
    plate_width = max(width_top, width_bottom, 1.0)
    plate_height = max(float(np.linalg.norm(quad[3] - quad[0])), float(np.linalg.norm(quad[2] - quad[1])), 1.0)
    dest_w = int(round(height * plate_width / plate_height))
    dest_w = max(min_width, min(max_width, dest_w))
    dest = np.array([[0, 0], [dest_w - 1, 0], [dest_w - 1, height - 1], [0, height - 1]], dtype=np.float32)
    matrix = cv2.getPerspectiveTransform(quad, dest)
    crop = cv2.warpPerspective(source, matrix, (dest_w, height), flags=cv2.INTER_CUBIC)
    if crop.size == 0:
        return None
    destination.parent.mkdir(parents=True, exist_ok=True)
    ok = cv2.imwrite(str(destination), crop, [int(cv2.IMWRITE_JPEG_QUALITY), quality])
    if not ok:
        return None
    return plate, str(destination)


def crop_job(payload: tuple) -> tuple[str, str, str, str] | tuple[str, str]:
    image_s, split, dest_s, height, min_width, max_width, expand, quality = payload
    image = Path(image_s)
    destination = Path(dest_s)
    result = crop_plate(image, height, min_width, max_width, expand, quality, destination)
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
            jobs.append((str(resolved), split, str(dest), args.height, args.min_width, args.max_width, args.expand, args.quality))

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
    rec_dir = Path(__file__).resolve().parent / "rec"
    rec_dir.mkdir(parents=True, exist_ok=True)
    write_plate_dict(rec_dir / "plate_dict.txt")

    summary = output / "SUMMARY.txt"
    lines = notes + [
        f"output: {output}",
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
