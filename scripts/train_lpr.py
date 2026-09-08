#!/usr/bin/env python3

import argparse
import os
import shutil
import sys
from pathlib import Path


def parse_args():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description="Train the SmartPark YOLO license plate detector")
    parser.add_argument("--data", type=Path, default=root / "model" / "datasets" / "ccpd_yolo" / "ccpd.yaml")
    parser.add_argument("--weights", type=Path, default=root / "model" / "yolo11m.pt")
    parser.add_argument("--epochs", type=int, default=100)
    parser.add_argument("--imgsz", type=int, default=960)
    parser.add_argument("--batch", type=int, default=16)
    parser.add_argument("--device", default="auto")
    parser.add_argument("--project", type=Path, default=root / "model" / "runs")
    parser.add_argument("--name", default="license_plate")
    parser.add_argument("--workers", type=int, default=8, help="data-loader worker processes")
    parser.add_argument("--exist-ok", action="store_true", help="overwrite an existing run directory")
    parser.add_argument("--amp", dest="amp", action="store_true", default=True)
    parser.add_argument("--no-amp", dest="amp", action="store_false", help="disable mixed precision")
    return parser.parse_args()


def seed_amp_weights(root: Path, source: Path) -> None:
    """Keep AMP checks offline by reusing a local YOLO checkpoint as yolo26n.pt."""
    dest = root / "weights" / "yolo26n.pt"
    if dest.exists() or not source.is_file():
        return
    dest.parent.mkdir(parents=True, exist_ok=True)
    try:
        os.link(source, dest)
    except OSError:
        shutil.copy2(source, dest)


def main():
    args = parse_args()
    root = Path(__file__).resolve().parents[1]
    data = args.data.expanduser().resolve()
    weights = args.weights.expanduser().resolve()
    project = args.project.expanduser().resolve()
    project.mkdir(parents=True, exist_ok=True)
    seed_amp_weights(root, weights)
    os.environ.setdefault("YOLO_OFFLINE", "1")

    missing = [str(path) for path in (data, weights) if not path.is_file()]
    if missing:
        print("缺少训练资产：", file=sys.stderr)
        print("\n".join(missing), file=sys.stderr)
        print("请先准备 model/ 下的数据集配置和 YOLO 权重。", file=sys.stderr)
        return 2

    try:
        from ultralytics import YOLO
    except ImportError as exc:
        print(f"无法导入 ultralytics：{exc}", file=sys.stderr)
        print("请使用 conda 环境 smartpark-lpr，而不是系统 Python。", file=sys.stderr)
        return 2

    device = None if args.device == "auto" else args.device
    model = YOLO(str(weights))
    model.train(
        data=str(data),
        epochs=args.epochs,
        imgsz=args.imgsz,
        batch=args.batch,
        device=device,
        project=str(project),
        name=args.name,
        workers=args.workers,
        exist_ok=args.exist_ok,
        amp=args.amp,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
