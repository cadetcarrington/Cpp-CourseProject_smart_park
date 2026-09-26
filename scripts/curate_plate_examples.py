#!/usr/bin/env python3
"""Materialize and verify the 40 curated CCPD full-frame examples."""

from __future__ import annotations

import argparse
import csv
import hashlib
import shutil
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
EXAMPLES = ROOT / "examples/plates"
DATASETS = ROOT / "model/datasets"
YOLO_VALIDATION = DATASETS / "ccpd_yolo/images/val"
PROVINCES = "皖沪津渝冀晋蒙辽吉黑苏浙京闽赣鲁豫鄂湘粤桂琼川贵云藏陕甘青宁新警学O"
LETTERS = "ABCDEFGHJKLMNPQRSTUVWXYZO"
ALPHANUMERIC = "ABCDEFGHJKLMNPQRSTUVWXYZ0123456789O"
SEED = b"smartpark-examples-v1:"
EXISTING = {
    "blue": {
        "00241379310345-90_90-301&482_388&518-390&519_307&519_306&481_389&481-0_0_15_10_27_26_25-115-17.jpg": "blue-arl321.jpg",
        "002344348659-90_84-429&369_530&406-525&405_425&398_428&364_528&371-0_0_17_26_30_24_8-105-11.jpg": "blue-at260j.jpg",
    },
    "green": {
        "0312109375-93_258-223&449_529&552-529&552_234&527_223&449_526&467-0_0_3_24_31_25_32_24-86-29.jpg": "green-ad07180.jpg",
        "0240625-95_264-206&378_437&483-437&483_207&450_206&378_435&402-0_0_3_24_30_25_29_25-163-89.jpg": "green-ad06151.jpg",
    },
}


def annotated_plate(filename: str) -> str:
    indices = [int(value) for value in Path(filename).stem.split("-")[4].split("_")]
    if len(indices) not in (7, 8):
        raise ValueError(f"Unexpected plate annotation: {filename}")
    return PROVINCES[indices[0]] + LETTERS[indices[1]] + "".join(
        ALPHANUMERIC[index] for index in indices[2:]
    )


def source_path(pool: str, filename: str) -> Path:
    stem, suffix = Path(filename).stem.rsplit("_", 1)
    original_name = f"{stem}.jpg" if len(suffix) == 12 else filename
    if pool == "blue":
        return DATASETS / "CCPD2019/ccpd_base" / original_name
    return DATASETS / "CCPD2020/ccpd_green/val" / original_name


def selected_entries() -> list[tuple[str, str, str]]:
    result = []
    green_manifest = DATASETS / "ccpd_rec/val_green.txt"
    blue_manifest = DATASETS / "ccpd_rec/val.txt"
    for pool, manifest in (("blue", blue_manifest), ("green", green_manifest)):
        existing = EXISTING[pool]
        candidates: dict[str, str] = {}
        for line in manifest.read_text(encoding="utf-8").splitlines():
            relative, label = line.split("\t", 1)
            filename = Path(relative).name
            if filename in existing:
                continue
            source = YOLO_VALIDATION / filename
            if not source.is_file() or source.resolve().parent != source_path(pool, filename).parent:
                continue
            if annotated_plate(filename) != label:
                continue
            candidates[filename] = label
        ranked = sorted(candidates, key=lambda name: hashlib.sha256(SEED + name.encode()).digest())
        if len(ranked) < 18:
            raise ValueError(f"Not enough {pool} validation frames")
        for filename, target in existing.items():
            result.append((pool, filename, target))
        for index, filename in enumerate(ranked[:18], start=1):
            result.append((pool, filename, f"{pool}-{index:02d}.jpg"))
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="verify samples and CSV without writing")
    args = parser.parse_args()
    entries = selected_entries()
    rows = []
    seen_hashes = set()
    for pool, filename, target_name in entries:
        source = source_path(pool, filename)
        target = EXAMPLES / target_name
        if not source.is_file():
            raise FileNotFoundError(source)
        source_digest = hashlib.sha256(source.read_bytes()).hexdigest()
        if source_digest in seen_hashes:
            raise ValueError(f"Duplicate image: {source}")
        seen_hashes.add(source_digest)
        if args.check:
            if not target.is_file() or hashlib.sha256(target.read_bytes()).hexdigest() != source_digest:
                raise ValueError(f"Sample missing or changed: {target}")
        elif target.exists():
            if hashlib.sha256(target.read_bytes()).hexdigest() != source_digest:
                raise ValueError(f"Refusing to replace changed sample: {target}")
        else:
            shutil.copyfile(source, target)
        with Image.open(target) as image:
            image.verify()
        with Image.open(target) as image:
            width, height = image.size
        rows.append((target_name, annotated_plate(filename), f"CCPD{'2019' if pool == 'blue' else '2020'}",
                     "val", source.name, width, height, source_digest))
    if len(rows) != 40 or len({row[0] for row in rows}) != 40:
        raise ValueError("Expected exactly 40 unique example names")
    if {path.name for path in EXAMPLES.glob("*.jpg")} != {row[0] for row in rows}:
        raise ValueError("Example directory contains missing or extra JPEG files")
    manifest = EXAMPLES / "manifest.csv"
    with manifest.open("r", newline="", encoding="utf-8") if args.check else manifest.open(
        "w", newline="", encoding="utf-8"
    ) as stream:
        if args.check:
            actual = list(csv.reader(stream))
            expected = [["file", "expected_plate", "dataset", "split", "original_filename", "width", "height", "sha256"]]
            expected.extend([list(map(str, row)) for row in rows])
            if actual != expected:
                raise ValueError("Manifest does not match source images")
        else:
            writer = csv.writer(stream, lineterminator="\n")
            writer.writerow(("file", "expected_plate", "dataset", "split", "original_filename", "width", "height", "sha256"))
            writer.writerows(rows)
    print(f"Verified {len(rows)} examples (20 blue, 20 green)")


if __name__ == "__main__":
    main()
