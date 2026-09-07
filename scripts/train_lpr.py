#!/usr/bin/env python3

import argparse
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
    return parser.parse_args()


def main():
    args = parse_args()
    data = args.data.expanduser().resolve()
    weights = args.weights.expanduser().resolve()

    missing = [str(path) for path in (data, weights) if not path.is_file()]
    if missing:
        print("缺少训练资产：", file=sys.stderr)
        print("\n".join(missing), file=sys.stderr)
        print("请先准备 model/ 下的数据集配置和 YOLO 权重。", file=sys.stderr)
        return 2

    try:
        from ultralytics import YOLO
    except ImportError:
        print("未安装 ultralytics，请在 Python 环境中安装项目训练依赖。", file=sys.stderr)
        return 2

    device = None if args.device == "auto" else args.device
    model = YOLO(str(weights))
    model.train(
        data=str(data),
        epochs=args.epochs,
        imgsz=args.imgsz,
        batch=args.batch,
        device=device,
        project=str(args.project),
        name=args.name,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
