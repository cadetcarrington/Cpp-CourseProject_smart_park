#!/usr/bin/env python3
"""Evaluate scripts/recognize_plate.py on a labelled manifest (整牌精确匹配).

两种四角来源：

* 默认：用 `recognize_plate.py` 的真实检测权重跑完整两阶段流程；
* `--oracle-corners`：把 CCPD 文件名里的标注四角当作"完美四角检测器"喂进去，
  用来衡量"裁剪几何对齐之后识别器还能考多少分"（只对 CCPD 命名的整图有效）。

manifest 需要 `file` 列与车牌列（`plate` 或 `expected_plate`），可选 `source`
列用于按子集汇总，例如 examples/plates/manifest-provinces.csv。

用法：
  ~/.smartpark/lpr/bin/python scripts/evaluate_plates.py \
      --manifest examples/plates/manifest-provinces.csv --workers 4
  ~/.smartpark/lpr/bin/python scripts/evaluate_plates.py \
      --manifest examples/plates/manifest-provinces.csv --oracle-corners --workers 4
"""

from __future__ import annotations

import argparse
import csv
import json
import os
import sys
import time
from multiprocessing import Pool
from pathlib import Path
from types import SimpleNamespace

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))


class Boxes:
    """最小化的 ultralytics Boxes 替身（oracle 模式用）。"""

    def __init__(self, xyxy, conf):
        self.xyxy = np.asarray(xyxy, dtype=np.float32)
        self.conf = np.asarray(conf, dtype=np.float32)

    def __len__(self) -> int:
        return len(self.conf)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--manifest", type=Path, default=ROOT / "examples/plates/manifest-provinces.csv")
    parser.add_argument("--base", type=Path, default=ROOT / "examples/plates")
    parser.add_argument("--out", type=Path, default=None, help="逐图 JSONL 输出，缺省写到临时目录")
    parser.add_argument("--workers", type=int, default=4)
    parser.add_argument("--limit", type=int, default=0, help="只跑前 N 张（冒烟用）")
    parser.add_argument("--oracle-corners", action="store_true",
                        help="用 CCPD 标注四角替代检测器，衡量几何对齐后的识别上限")
    parser.add_argument("--crop", choices=("auto", "quad", "bbox"), default="auto")
    parser.add_argument("--flip-check", action="store_true")
    return parser.parse_args()


def ccpd_geometry(image: Path):
    """CCPD 文件名 → (语义四角 LT,RT,RB,LB, 轴对齐框)。"""
    fields = image.stem.split("-")
    vertices = []
    for pair in fields[3].split("_"):
        x_text, y_text = pair.split("&", 1)
        vertices.append((float(x_text), float(y_text)))
    first, second = fields[2].split("_", 1)
    x1, y1 = (float(value) for value in first.split("&", 1))
    x2, y2 = (float(value) for value in second.split("&", 1))
    bounds = [min(x1, x2), min(y1, y2), max(x1, x2), max(y1, y2)]
    return vertices, bounds


def evaluate_one(item):
    relative, expected, subset = item
    import plate_geometry

    import recognize_plate

    image = ROOT / "examples/plates" / relative
    args = SimpleNamespace(
        image=image,
        detector=ROOT / "model/weights/smartpark_plate_yolo11m_best.pt",
        recognizer=ROOT / "model/weights/smartpark_plate_ppocrv5_best.pdparams",
        config=ROOT / "model/weights/smartpark_plate_ppocrv5_config.yml",
        paddleocr=ROOT / "third_party/PaddleOCR",
        ocr_python=Path(os.environ.get("SMARTPARK_OCR_PY",
                                       os.path.expanduser("~/.smartpark/ocr/bin/python"))),
        dictionary=ROOT / "scripts/rec/ppocrv5_dict.txt",
        confidence=0.25, crop=ARGS.crop, crop_recipe=None, flip_check=ARGS.flip_check)

    if ARGS.oracle_corners:
        vertices, bounds = ccpd_geometry(image)
        quad = plate_geometry.ccpd_quad(vertices)          # 语义顺序，等价于 pose 检测器
        detection = SimpleNamespace(boxes=Boxes([bounds], [0.9]),
                                    keypoints=SimpleNamespace(
                                        xy=quad[None, ...],
                                        conf=np.full((1, 4), 0.95, dtype=np.float32)))
        stub = lambda *a, **k: SimpleNamespace(predict=lambda *a, **k: [detection])  # noqa: E731
        sys.modules["ultralytics"] = SimpleNamespace(YOLO=stub)

    record = {"file": relative, "expected": expected, "subset": subset,
              "geometry": "oracle" if ARGS.oracle_corners else "detector"}
    started = time.time()
    try:
        result = recognize_plate.recognize(args)
        record.update(result)
        record["status"] = "ok" if result["plate"] == expected else "wrong"
    except Exception as error:  # noqa: BLE001 - 评测脚本要记录所有失败
        record["status"] = "error"
        record["error"] = f"{type(error).__name__}: {error}"[:300]
    record["seconds"] = round(time.time() - started, 1)
    return record


ARGS: argparse.Namespace = None  # type: ignore[assignment]


def init_worker(args: argparse.Namespace) -> None:
    global ARGS
    ARGS = args


def main() -> int:
    global ARGS
    ARGS = parse_args()
    if not ARGS.manifest.is_file():
        print(f"manifest not found: {ARGS.manifest}", file=sys.stderr)
        return 2
    with ARGS.manifest.open(encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        label_column = "plate" if "plate" in reader.fieldnames else "expected_plate"
        items = []
        for row in reader:
            parts = row.get("source", "").split("/")
            subset = "/".join(parts[2:4]) if len(parts) > 3 else row.get("dataset", "all")
            items.append((row["file"], row[label_column], subset))
    if ARGS.limit:
        items = items[: ARGS.limit]
    geometry = "oracle" if ARGS.oracle_corners else "detector"
    out = ARGS.out or Path(f"/tmp/smartpark-eval-{geometry}-{ARGS.crop}.jsonl")

    print(f"images={len(items)} geometry={geometry} crop={ARGS.crop} -> {out}", flush=True)
    results = []
    with out.open("w", encoding="utf-8") as handle, Pool(ARGS.workers, initializer=init_worker,
                                                         initargs=(ARGS,)) as pool:
        for index, record in enumerate(pool.imap_unordered(evaluate_one, items, chunksize=1), 1):
            results.append(record)
            handle.write(json.dumps(record, ensure_ascii=False) + "\n")
            handle.flush()
            if index % 20 == 0:
                good = sum(r["status"] == "ok" for r in results)
                print(f"{index}/{len(items)} exact={good} ({good / index:.1%})", flush=True)

    groups: dict[str, list] = {}
    for record in results:
        groups.setdefault(record["subset"], []).append(record)
    print()
    for subset, rows in sorted(groups.items()):
        good = sum(r["status"] == "ok" for r in rows)
        print(f"{subset:28s} {good:3d}/{len(rows):3d} = {good / len(rows):5.1%}")
    good = sum(r["status"] == "ok" for r in results)
    errors = sum(r["status"] == "error" for r in results)
    crops = sorted({r.get("crop", "-") for r in results})
    print(f"{'TOTAL':28s} {good:3d}/{len(results):3d} = {good / len(results):5.1%} "
          f"(errors={errors}, crop={crops})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
