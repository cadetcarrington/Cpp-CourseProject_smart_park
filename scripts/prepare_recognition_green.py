#!/usr/bin/env python3
"""Rebuild a recognition train list with a higher green (新能源) repeat factor.

新能源绿牌在 CCPD2020 只有 5,769 张训练整图（绿牌底盘、8 位号牌），在现有 train.txt 里
虽然已经按 ``--green-repeat 5`` 重复过，但仍是最弱的子集；而且评测集 33 张绿牌里有 17 张
的裁剪图混在训练列表里，指标虚高。本脚本：

1. 用 CCPD2020 train 池的整图 → ``output_name`` 映射，找出 train.txt 里的绿牌行；
2. 剔除 ``--exclude-manifest`` 命中的图（评测集那 17 张就此出训练集，评测才干净）；
3. 按 ``--target-share`` 重写绿牌重复倍数（上限 ``--max-repeat``），其余行原样保留。

例：
  "$SMARTPARK_LPR_PY" scripts/prepare_recognition_green.py \
      --base model/datasets/ccpd_rec_balance \
      --exclude-manifest examples/plates/manifest-provinces.csv \
      --target-share 0.30 --output model/datasets/ccpd_rec_green
"""

from __future__ import annotations

import argparse
import csv
import math
import shutil
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from prepare_ccpd import images_under, output_name


def parse_args() -> argparse.Namespace:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--base", type=Path, default=root / "model/datasets/ccpd_rec_balance",
                        help="基线数据集（提供 train.txt/val.txt，裁剪图沿用其中的 images/train）")
    parser.add_argument("--ccpd2020", type=Path, default=root / "model/datasets/CCPD2020/ccpd_green/train",
                        help="CCPD2020 绿牌训练整图目录（用于反查哪些行是绿牌）")
    parser.add_argument("--output", type=Path, default=root / "model/datasets/ccpd_rec_green")
    parser.add_argument("--exclude-manifest", type=Path, default=None,
                        help="评测集 manifest：命中的整图对应的绿牌行会被剔除，避免训练见过评测图")
    parser.add_argument("--target-share", type=float, default=0.30, help="绿牌行目标占比")
    parser.add_argument("--max-repeat", type=int, default=20, help="绿牌重复倍数上限")
    return parser.parse_args()


def load_exclusions(manifest: Path | None) -> set[str]:
    if manifest is None:
        return set()
    with manifest.open(encoding="utf-8") as handle:
        return {Path(row["source"]).name for row in csv.DictReader(handle) if row.get("source")}


def main() -> int:
    args = parse_args()
    base = args.base.expanduser().resolve()
    output = args.output.expanduser().resolve()
    train_list = base / "train.txt"
    if not train_list.is_file():
        raise SystemExit(f"基线数据集缺少 train.txt：{base}")

    exclusions = load_exclusions(args.exclude_manifest.expanduser().resolve()
                                 if args.exclude_manifest else None)
    pool = list(images_under(args.ccpd2020.expanduser().resolve()))
    if not pool:
        raise SystemExit(f"绿牌整图池为空：{args.ccpd2020}")
    # 整图 → 裁剪图文件名（prepare_recognition.output_name 是确定性的）
    green_names = {output_name(image): image for image in pool}

    lines = [line for line in train_list.read_text(encoding="utf-8").splitlines() if line.strip()]
    green_lines, other_lines, dropped = [], [], 0
    for line in lines:
        name = Path(line.split("\t")[0]).name
        if name not in green_names:
            other_lines.append(line)
            continue
        if green_names[name].name in exclusions:
            dropped += 1
            continue
        green_lines.append(line)

    # 基线里绿牌已经是重复过的：先按唯一裁剪图去重，再按目标占比算重复倍数
    unique_green: dict[str, str] = {}
    for line in green_lines:
        unique_green.setdefault(line.split("\t")[0], line)
    duplicates = len(green_lines) / max(1, len(unique_green))
    green_share = args.target_share
    repeat = (math.ceil(green_share * len(other_lines) / (len(unique_green) * (1.0 - green_share)))
              if unique_green else 0)
    repeat = max(1, min(args.max_repeat, repeat))
    rebuilt_green = list(unique_green.values()) * repeat
    train_out = other_lines + rebuilt_green

    if output.exists():
        shutil.rmtree(output)
    (output / "images").mkdir(parents=True, exist_ok=True)
    # 基线列表同时引用 images/base（原 ccpd_rec 裁剪）与 images/focus（均衡微调新增裁剪），
    # 两者都要挂进来，否则训练时报 "xxx is not found"。
    for name in ("base", "focus", "val"):
        source = base / "images" / name
        if source.exists():
            (output / "images" / name).symlink_to(source.resolve(), target_is_directory=True)
    (output / "train.txt").write_text("\n".join(train_out) + "\n", encoding="utf-8")
    shutil.copy2(base / "val.txt", output / "val.txt")
    if (base / "val_green.txt").is_file():
        shutil.copy2(base / "val_green.txt", output / "val_green.txt")

    summary = [
        f"base: {base}",
        f"ccpd_green_train_pool: {len(pool)}",
        f"excluded_green_images: {len({output_name(i) for i in pool if i.name in exclusions})}",
        f"green_source_images: {len(green_names)}",
        f"green_lines_before: {len(green_lines) + dropped}",
        f"green_lines_kept: {len(green_lines)} (unique crops {len(unique_green)}, 原重复 {duplicates:.1f}x)",
        f"dropped_leaked_lines: {dropped}",
        f"green_repeat: {repeat}",
        f"other_lines: {len(other_lines)}",
        f"train_lines_total: {len(train_out)}",
        f"green_share: {len(rebuilt_green) / len(train_out):.3f}",
    ]
    (output / "SUMMARY.txt").write_text("\n".join(summary) + "\n", encoding="utf-8")
    print("\n".join(summary))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
