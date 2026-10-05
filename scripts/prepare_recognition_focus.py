#!/usr/bin/env python3
"""Build a province-focused recognition fine-tune set on top of an existing ccpd_rec dataset.

两种模式：

* 单省重点（默认）：``--province 晋 --target-share 0.10`` —— 只给目标省过采样到 epoch 的 10%，
  适合"部署车流以某省为主"；实测晋牌 92% → 98%，代价是非晋省份被压回皖。
* 全非皖均衡（``--balance``）：给池子里每个非皖省份都补到 ``--per-province-share``（默认 1.2%
  的基线行数），``--province`` 里列出的省份再乘 ``--bias-factor``（默认 1.5，即"微微偏向"）。
  池子里样本不够的省用满并重复（倍数上限 ``--max-multiplier``），够的省按路径 sha256
  确定性抽样，避免把稀缺省份重复到过拟合。

两种模式都会排除 ``--exclude-manifest`` 里出现过的图片，保证评测集没有进训练。

例：
  # 全非皖均衡、晋再 x2
  "$SMARTPARK_LPR_PY" scripts/prepare_recognition_focus.py --balance \
      --province 晋 --bias-factor 2.0 --per-province-share 0.012 \
      --exclude-manifest examples/plates/manifest-provinces.csv \
      --base model/datasets/ccpd_rec --output model/datasets/ccpd_rec_balance
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import math
import shutil
import sys
from collections import Counter
from concurrent.futures import ProcessPoolExecutor, as_completed
from pathlib import Path

from plate_geometry import CropRecipe
from prepare_ccpd import images_under, output_name
from prepare_recognition import PROVINCES, crop_plate


def parse_args() -> argparse.Namespace:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--province", nargs="+", default=["晋"],
                        help="目标省份字（可多个）；均衡模式下作为加权名单")
    parser.add_argument("--balance", action="store_true",
                        help="对所有非皖省份均衡过采样（默认只做单个省的重点过采样）")
    parser.add_argument("--per-province-share", type=float, default=0.012,
                        help="均衡模式下每个省份目标占基线行数的比例（默认 0.012）")
    parser.add_argument("--bias-factor", type=float, default=1.5,
                        help="均衡模式下 --province 名单的加权倍数（默认 1.5）")
    parser.add_argument("--target-share", type=float, default=0.10,
                        help="单省模式下的目标占比（默认 0.10）")
    parser.add_argument("--ccpd2019", type=Path, default=root / "model/datasets/CCPD2019")
    parser.add_argument("--base", type=Path, default=root / "model/datasets/ccpd_rec",
                        help="既有识别数据集（提供基线 train.txt/val.txt 与裁剪图）")
    parser.add_argument("--output", type=Path, default=None)
    parser.add_argument("--exclude-manifest", type=Path, default=None,
                        help="评测集 manifest（取 source 列的文件名排除，避免训练见过评测图）")
    parser.add_argument("--crop-mode", choices=("quad", "bbox"), default="quad",
                        help="与要微调的识别权重训练分布一致；出厂权重是 quad")
    parser.add_argument("--max-multiplier", type=int, default=60,
                        help="过采样倍数上限，避免极稀少省份被过度重复")
    parser.add_argument("--workers", type=int, default=8)
    parser.add_argument("--quality", type=int, default=95)
    return parser.parse_args()


def province_index(province: str) -> int:
    if province not in PROVINCES:
        raise SystemExit(f"未知省份字：{province}")
    return PROVINCES.index(province)


def census_images(root: Path) -> dict[int, list[Path]]:
    """一趟遍历 CCPD 池，按省份索引归类（缺四角字段的图直接跳过）。"""
    table: dict[int, list[Path]] = {}
    for image in images_under(root):
        fields = image.stem.split("-")
        if len(fields) < 5:
            continue
        try:
            index = int(fields[4].split("_")[0])
        except (IndexError, ValueError):
            continue
        table.setdefault(index, []).append(image)
    return {index: sorted(paths) for index, paths in table.items()}


def load_exclusions(manifest: Path | None) -> set[str]:
    if manifest is None:
        return set()
    names: set[str] = set()
    with manifest.open(encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            source = (row.get("source") or "").strip()
            if source:
                names.add(Path(source).name)
    return names


def deterministic_sample(images: list[Path], count: int) -> list[Path]:
    """按路径 sha256 排序取前 count 张：可复现、不依赖 PRNG（与项目其它抽样一致）。"""
    ordered = sorted(images, key=lambda path: hashlib.sha256(str(path.resolve()).encode()).hexdigest())
    return ordered[: max(0, count)]


def crop_job(payload: tuple) -> str | None:
    image_s, destination_s, recipe = payload
    result = crop_plate(Path(image_s), recipe, 0.0, Path(destination_s))
    if result is None:
        return None
    plate, written = result
    return f"images/focus/{Path(written).name}\t{plate}"


def rewrite_base_line(line: str) -> str:
    """基线标签 images/train/xxx.jpg → 软链路径 images/base/xxx.jpg（车牌不变）。"""
    relative, _, plate = line.partition("\t")
    return "\t".join([f"images/base/{Path(relative).name}", plate])


def plan_single(pool: dict[int, list[Path]], base_lines: int, province: str, share: float,
                cap: int) -> dict[int, tuple[list[Path], int]]:
    index = province_index(province)
    images = pool.get(index, [])
    if not images:
        raise SystemExit(f"池子里没有 {province} 的样本")
    multiplier = max(1, math.ceil(share * base_lines / (len(images) * (1.0 - share))))
    return {index: (images, min(cap, multiplier))}


def plan_balance(pool: dict[int, list[Path]], base_counts: Counter, base_lines: int,
                 focus: set[str], per_share: float, bias: float,
                 cap: int) -> dict[int, tuple[list[Path], int]]:
    plan: dict[int, tuple[list[Path], int]] = {}
    wan = PROVINCES.index("皖")
    for index, images in sorted(pool.items()):
        if index == wan or index >= len(PROVINCES):
            continue
        name = PROVINCES[index]
        target = per_share * base_lines * (bias if name in focus else 1.0)
        extra = target - base_counts.get(name, 0)
        if extra <= 1 or not images:
            continue
        if extra >= len(images):
            multiplier = min(cap, max(1, math.ceil(extra / len(images))))
            selected = images
        else:
            multiplier = 1
            selected = deterministic_sample(images, int(round(extra)))
        plan[index] = (selected, multiplier)
    return plan


def main() -> int:
    args = parse_args()
    root = Path(__file__).resolve().parents[1]
    base = args.base.expanduser().resolve()
    output = (args.output or (root / "model" / "datasets" / "ccpd_rec_focus")).expanduser().resolve()
    if not (base / "train.txt").is_file():
        raise SystemExit(f"基线数据集缺少 train.txt：{base}")

    exclusions = load_exclusions(args.exclude_manifest.expanduser().resolve()
                                 if args.exclude_manifest else None)
    pool_raw = census_images(args.ccpd2019.expanduser().resolve())
    excluded = 0
    pool: dict[int, list[Path]] = {}
    for index, images in pool_raw.items():
        kept = [image for image in images if image.name not in exclusions]
        excluded += len(images) - len(kept)
        if kept:
            pool[index] = kept

    base_lines = [line for line in (base / "train.txt").read_text(encoding="utf-8").splitlines()
                  if line.strip()]
    base_counts = Counter(line.rsplit("\t", 1)[-1][:1] for line in base_lines)
    if args.balance:
        plan = plan_balance(pool, base_counts, len(base_lines), set(args.province),
                            args.per_province_share, args.bias_factor, args.max_multiplier)
        mode_note = (f"balance per_province_share={args.per_province_share} "
                     f"bias={args.bias_factor}x{args.province}")
    else:
        plan = plan_single(pool, len(base_lines), args.province[0], args.target_share,
                           args.max_multiplier)
        mode_note = f"single province={args.province[0]} target_share={args.target_share}"

    print(f"池子可解析 {sum(len(v) for v in pool_raw.values())} 张，"
          f"排除评测集 {excluded} 张；计划覆盖 {len(plan)} 个省份", flush=True)
    if not plan:
        raise SystemExit("没有需要补充的省份，检查 --balance/--per-province-share")

    if output.exists():
        shutil.rmtree(output)
    (output / "images" / "focus").mkdir(parents=True, exist_ok=True)
    (output / "images" / "base").symlink_to((base / "images" / "train").resolve(), target_is_directory=True)
    (output / "images" / "val").symlink_to((base / "images" / "val").resolve(), target_is_directory=True)

    recipe = CropRecipe(mode=args.crop_mode, quality=args.quality)
    jobs = []
    for images, _multiplier in plan.values():
        for image in images:
            destination = output / "images" / "focus" / output_name(image)
            if destination.suffix.lower() not in {".jpg", ".jpeg"}:
                destination = destination.with_suffix(".jpg")
            jobs.append((str(image), str(destination), recipe))

    lines_by_image: dict[str, str] = {}
    with ProcessPoolExecutor(max_workers=max(1, args.workers)) as pool_exec:
        futures = {pool_exec.submit(crop_job, job): job for job in jobs}
        for done, future in enumerate(as_completed(futures), 1):
            line = future.result()
            if line is not None:
                lines_by_image[futures[future][0]] = line
            if done % 2000 == 0 or done == len(jobs):
                print(f"cropped {done}/{len(jobs)}", flush=True)

    train_lines = [rewrite_base_line(line) for line in base_lines]
    table = []
    for index, (images, multiplier) in sorted(plan.items()):
        name = PROVINCES[index]
        added = [lines_by_image[str(image)] for image in images if str(image) in lines_by_image]
        train_lines.extend(added * multiplier)
        table.append((name, len(pool_raw.get(index, [])), len(images), multiplier, len(added) * multiplier))
    (output / "train.txt").write_text("\n".join(train_lines) + "\n", encoding="utf-8")
    shutil.copy2(base / "val.txt", output / "val.txt")
    if (base / "val_green.txt").is_file():
        shutil.copy2(base / "val_green.txt", output / "val_green.txt")

    province_share = Counter(line.rsplit("\t", 1)[-1][:1] for line in train_lines)
    non_wan = sum(count for name, count in province_share.items() if name != "皖")
    summary = [
        f"mode: {'balance' if args.balance else 'single'}",
        f"params: {mode_note}",
        f"excluded_by_manifest: {excluded}",
        f"base_train_lines: {len(base_lines)}",
        f"train_lines_total: {len(train_lines)}",
        f"non_wan_share: {non_wan / len(train_lines):.3f}",
        f"focus_share: {sum(row[4] for row in table) / len(train_lines):.3f}",
        "",
        f"{'prov':>4s} {'池中':>7s} {'选用':>7s} {'重复':>5s} {'最终行':>8s} {'占比':>7s}",
    ]
    for name, pool_count, used, multiplier, final in table:
        summary.append(f"{name:>4s} {pool_count:7d} {used:7d} {multiplier:5d} {final:8d} "
                       f"{final / len(train_lines):6.2%}")
    (output / "SUMMARY.txt").write_text("\n".join(summary) + "\n", encoding="utf-8")
    print("\n".join(summary))
    recipe.save(output / "crop_recipe.json", note="focus 微调集使用的裁剪配方")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
