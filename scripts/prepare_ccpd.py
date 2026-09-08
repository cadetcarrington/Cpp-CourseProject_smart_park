#!/usr/bin/env python3
"""Convert CCPD filename annotations into a symlinked one-class YOLO dataset."""

from __future__ import annotations

import argparse
import hashlib
import shutil
import sys
import urllib.request
from collections import Counter
from pathlib import Path
from typing import Iterable

from PIL import Image

SPLITS = ("train", "val", "test")
IMAGE_EXTENSIONS = {".jpg", ".jpeg", ".png"}
DIFFICULTY_SUBSETS = ("ccpd_db", "ccpd_blur", "ccpd_rotate", "ccpd_tilt", "ccpd_fn", "ccpd_challenge", "ccpd_weather")
OFFICIAL_SPLIT_URLS = {
    "train": "https://raw.githubusercontent.com/detectRecog/CCPD/master/split/train.txt",
    "val": "https://raw.githubusercontent.com/detectRecog/CCPD/master/split/val.txt",
}


def parse_args() -> argparse.Namespace:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ccpd2019", type=Path, default=root / "model/datasets/CCPD2019")
    parser.add_argument("--ccpd2020", type=Path, default=root / "model/datasets/CCPD2020")
    parser.add_argument("--output", type=Path, default=root / "model/datasets/ccpd_yolo")
    parser.add_argument("--max-per-split", type=int, default=None, metavar="N",
                        help="limit each split for a fast smoke conversion")
    parser.add_argument("--green-only", action="store_true", help="convert CCPD2020 green only")
    parser.add_argument("--no-download", action="store_true", help="do not fetch official CCPD2019 split lists")
    return parser.parse_args()


def images_under(directory: Path) -> list[Path]:
    if not directory.is_dir():
        return []
    return sorted(
        path for path in directory.rglob("*")
        if path.is_file() and path.suffix.lower() in IMAGE_EXTENSIONS and "ccpd_np" not in path.parts
    )


def parse_split_lines(lines: Iterable[str], root: Path, base: Path) -> list[Path]:
    found: list[Path] = []
    seen: set[Path] = set()
    for raw in lines:
        value = raw.strip().replace("\\", "/")
        if not value or value.startswith("#"):
            continue
        # CCPD lists have appeared both as basenames and ccpd_base-relative paths.
        candidates = [root / value.lstrip("/"), base / value, base / Path(value).name]
        path = next((candidate for candidate in candidates if candidate.is_file()), None)
        if path is not None and path not in seen:
            found.append(path)
            seen.add(path)
    return found


def local_or_downloaded_split(name: str, root: Path, base: Path, allow_download: bool) -> tuple[list[Path], str]:
    local = sorted(root.rglob(f"{name}.txt"))
    for split_file in local:
        items = parse_split_lines(split_file.read_text(errors="replace").splitlines(), root, base)
        if items:
            return items, str(split_file)
    if allow_download:
        try:
            with urllib.request.urlopen(OFFICIAL_SPLIT_URLS[name], timeout=30) as response:
                content = response.read().decode("utf-8", "replace")
            items = parse_split_lines(content.splitlines(), root, base)
            if items:
                return items, OFFICIAL_SPLIT_URLS[name]
        except Exception as exc:  # Network is optional; deterministic fallback below.
            print(f"warning: could not download {name} split: {exc}", file=sys.stderr)
    return [], ""


def stable_hash_split(path: Path) -> str:
    bucket = int(hashlib.sha1(str(path).encode()).hexdigest()[:8], 16) % 10
    return "train" if bucket < 8 else "val" if bucket == 8 else "test"


def ccpd2019_sources(root: Path, allow_download: bool) -> tuple[dict[str, list[Path]], str]:
    result: dict[str, list[Path]] = {name: [] for name in SPLITS}
    base = root / "ccpd_base"
    if not base.is_dir():
        candidates = [path for path in root.rglob("ccpd_base") if path.is_dir()]
        if candidates:
            base = candidates[0]
    base_images = images_under(base)
    if base_images:
        train, train_origin = local_or_downloaded_split("train", root, base, allow_download)
        val, val_origin = local_or_downloaded_split("val", root, base, allow_download)
        if train and val:
            selected = set(train) | set(val)
            result["train"].extend(train)
            result["val"].extend(val)
            # CCPD's supplied URLs enumerate train/val; use its remaining base images as held-out test.
            result["test"].extend(path for path in base_images if path not in selected)
            source_note = f"official train={train_origin}, val={val_origin}; remaining ccpd_base=test"
        else:
            for path in base_images:
                result[stable_hash_split(path)].append(path)
            source_note = "deterministic SHA-1 8:1:1 split of ccpd_base"
    else:
        source_note = "ccpd_base not found"

    for subset in DIFFICULTY_SUBSETS:
        locations = [root / subset] + [path for path in root.rglob(subset) if path.is_dir()]
        for location in dict.fromkeys(locations):
            result["test"].extend(images_under(location))
    return result, source_note


def ccpd2020_sources(root: Path) -> dict[str, list[Path]]:
    result: dict[str, list[Path]] = {name: [] for name in SPLITS}
    # Typical archive layout is CCPD2020/ccpd_green/{train,val,test}; support nesting variations.
    for split in SPLITS:
        candidates = [root / "ccpd_green" / split, root / split]
        candidates.extend(path for path in root.rglob(split) if path.is_dir() and "ccpd_green" in path.parts)
        for candidate in dict.fromkeys(candidates):
            result[split].extend(images_under(candidate))
    return result


def yolo_label(image: Path) -> str | None:
    fields = image.stem.split("-")
    if len(fields) < 3:
        return None
    try:
        first, second = fields[2].split("_", 1)
        x1, y1 = (float(value) for value in first.split("&", 1))
        x2, y2 = (float(value) for value in second.split("&", 1))
        with Image.open(image) as source:
            width, height = source.size
        if width <= 0 or height <= 0:
            return None
        x1, x2 = sorted((max(0.0, min(x1, width)), max(0.0, min(x2, width))))
        y1, y2 = sorted((max(0.0, min(y1, height)), max(0.0, min(y2, height))))
        if x2 <= x1 or y2 <= y1:
            return None
        cx, cy = (x1 + x2) / (2 * width), (y1 + y2) / (2 * height)
        box_w, box_h = (x2 - x1) / width, (y2 - y1) / height
        values = [max(0.0, min(1.0, value)) for value in (cx, cy, box_w, box_h)]
        return "0 " + " ".join(f"{value:.8f}" for value in values) + "\n"
    except (OSError, ValueError, IndexError):
        return None


def reset_output(output: Path) -> None:
    if output.exists():
        shutil.rmtree(output)
    for split in SPLITS:
        (output / "images" / split).mkdir(parents=True, exist_ok=True)
        (output / "labels" / split).mkdir(parents=True, exist_ok=True)


def output_name(image: Path) -> str:
    digest = hashlib.sha1(str(image.resolve()).encode()).hexdigest()[:12]
    return f"{image.stem}_{digest}{image.suffix.lower()}"


def write_yaml(output: Path) -> None:
    (output / "ccpd.yaml").write_text(
        f"path: {output.resolve()}\ntrain: images/train\nval: images/val\ntest: images/test\nnames:\n  0: license_plate\n"
    )


def main() -> int:
    args = parse_args()
    if args.max_per_split is not None and args.max_per_split < 1:
        raise SystemExit("--max-per-split must be at least 1")
    output = args.output.expanduser().resolve()
    sources: dict[str, list[Path]] = {name: [] for name in SPLITS}
    notes: list[str] = []

    if not args.green_only:
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
        for split in SPLITS:
            sources[split].extend(split_sources[split])
    else:
        print(f"warning: CCPD2020 directory not found: {root2020}", file=sys.stderr)

    reset_output(output)
    counts: Counter[str] = Counter()
    skipped: Counter[str] = Counter()
    for split in SPLITS:
        seen: set[Path] = set()
        for image in sorted(sources[split]):
            resolved = image.resolve()
            if resolved in seen:
                continue
            seen.add(resolved)
            if args.max_per_split is not None and counts[split] >= args.max_per_split:
                break
            label = yolo_label(image)
            if label is None:
                skipped[split] += 1
                continue
            destination = output / "images" / split / output_name(image)
            destination.symlink_to(resolved)
            (output / "labels" / split / f"{destination.stem}.txt").write_text(label)
            counts[split] += 1
    write_yaml(output)

    for note in notes:
        print(note)
    print(f"wrote {output / 'ccpd.yaml'}")
    print("images/labels: " + ", ".join(f"{split}={counts[split]}" for split in SPLITS))
    print("skipped malformed/unreadable: " + ", ".join(f"{split}={skipped[split]}" for split in SPLITS))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
