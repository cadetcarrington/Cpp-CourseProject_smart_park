#!/usr/bin/env python3

import argparse
import os
import shutil
import sys
from pathlib import Path


TASKS = {
    # --task: (ultralytics 任务名, 数据集 yaml 名, 建议初始权重)
    "det": ("detect", "ccpd.yaml", "yolo11m.pt"),
    "obb": ("obb", "ccpd_obb.yaml", "yolo11m-obb.pt"),
    "pose": ("pose", "ccpd_pose.yaml", "yolo11m-pose.pt"),
}


def parse_args():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description="Train the SmartPark YOLO license plate detector")
    parser.add_argument("--task", choices=sorted(TASKS), default="det",
                        help="det=轴对齐框（现有权重）；obb=四角旋转框；pose=四关键点")
    parser.add_argument("--data", type=Path, default=None,
                        help="数据集 yaml，缺省按 --task 取 model/datasets/ccpd_yolo/<task yaml>")
    parser.add_argument("--weights", type=Path, default=None,
                        help="初始权重，缺省按 --task 取 model/<建议权重>")
    parser.add_argument("--epochs", type=int, default=100)
    parser.add_argument("--imgsz", type=int, default=960)
    parser.add_argument("--batch", type=int, default=16)
    parser.add_argument("--device", default="auto")
    parser.add_argument("--project", type=Path, default=root / "model" / "runs")
    parser.add_argument("--name", default="license_plate")
    parser.add_argument("--workers", type=int, default=8, help="data-loader worker processes")
    parser.add_argument("--resume", action="store_true",
                        help="从 <project>/<name>/weights/last.pt 续跑（4 小时分区上限下必需）")
    parser.add_argument("--exist-ok", action="store_true", help="overwrite an existing run directory")
    parser.add_argument("--amp", dest="amp", action="store_true", default=True)
    parser.add_argument("--no-amp", dest="amp", action="store_false", help="disable mixed precision")
    return parser.parse_args()


def resolve_task_paths(args, root: Path) -> tuple[Path, Path]:
    """默认数据/权重按任务取名，与 prepare_ccpd.py 的输出目录约定一致。"""
    _task, yaml_name, weight_name = TASKS[args.task]
    directory = "ccpd_yolo" if args.task == "det" else f"ccpd_yolo_{args.task}"
    data = args.data or (root / "model" / "datasets" / directory / yaml_name)
    weights = args.weights or (root / "model" / weight_name)
    return data, weights


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
    data, weights = (path.expanduser().resolve() for path in resolve_task_paths(args, root))
    expected_task, _, _ = TASKS[args.task]
    project = args.project.expanduser().resolve()
    project.mkdir(parents=True, exist_ok=True)
    checkpoint = project / args.name / "weights" / "last.pt"

    missing = [] if args.resume else [str(path) for path in (data, weights) if not path.is_file()]
    if args.resume and not checkpoint.is_file():
        missing.append(str(checkpoint))
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

    if args.resume:
        # 4 小时分区上限：从上次中断处续跑（ultralytics 要求 resume=True，重投新命令不算续跑）
        print(f"resuming from {checkpoint}", flush=True)
        YOLO(str(checkpoint)).train(resume=True)
        return 0

    seed_amp_weights(root, weights)
    os.environ.setdefault("YOLO_OFFLINE", "1")
    device = None if args.device == "auto" else args.device
    model = YOLO(str(weights))
    if model.task != expected_task:
        print(f"权重 {weights.name} 是 {model.task} 模型，与 --task {args.task} 不符；"
              f"请改用 {TASKS[args.task][2]}。", file=sys.stderr)
        return 2
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
