#!/usr/bin/env python3
"""Launch PP-OCRv5 server rec fine-tuning on CCPD plate crops."""

from __future__ import annotations

import argparse
import os
import random
import subprocess
import sys
from pathlib import Path


def parse_args() -> argparse.Namespace:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--paddleocr", type=Path, default=root / "third_party" / "PaddleOCR")
    parser.add_argument("--config", type=Path, default=root / "scripts" / "rec" / "PP-OCRv5_server_rec_plate.yml")
    parser.add_argument("--pretrained", type=Path, default=root / "model" / "PP-OCRv5_server_rec_pretrained.pdparams")
    parser.add_argument("--data", type=Path, default=root / "model" / "datasets" / "ccpd_rec")
    parser.add_argument("--output", type=Path, default=root / "model" / "runs" / "plate_rec")
    parser.add_argument("--gpus", default="0,1")
    parser.add_argument("--epochs", type=int, default=20)
    parser.add_argument("--batch", type=int, default=64)
    parser.add_argument("--lr", type=float, default=0.0002)
    parser.add_argument("--eval-step", type=int, default=1000)
    parser.add_argument("--workers", type=int, default=4)
    parser.add_argument("--max-eval", type=int, default=8000, help="subsample val.txt to this many lines for in-training eval")
    parser.add_argument("--eval-list", type=Path, default=None, help="override eval label list; default val_eval.txt or auto-subsample")
    parser.add_argument("--resume", action="store_true", help="resume from latest checkpoint in --output if present")
    parser.add_argument("--checkpoints", type=Path, default=None, help="explicit PaddleOCR checkpoint prefix (no .pdparams)")
    return parser.parse_args()


def resolve_eval_list(data: Path, eval_list: Path | None, max_eval: int) -> Path:
    if eval_list is not None:
        path = eval_list.expanduser().resolve()
        if not path.exists():
            raise FileNotFoundError(f"eval list not found: {path}")
        return path
    cached = data / "val_eval.txt"
    if cached.exists() and cached.stat().st_size > 0:
        return cached
    val = data / "val.txt"
    green = data / "val_green.txt"
    lines = [ln for ln in val.read_text(encoding="utf-8").splitlines() if ln.strip()]
    green_lines = []
    if green.exists():
        green_lines = [ln for ln in green.read_text(encoding="utf-8").splitlines() if ln.strip()]
    if len(lines) <= max_eval:
        return val
    green_set = set(green_lines)
    rest = [ln for ln in lines if ln not in green_set]
    rng = random.Random(42)
    rng.shuffle(rest)
    keep_n = max(0, max_eval - len(green_lines))
    selected = green_lines + rest[:keep_n]
    cached.write_text("\n".join(selected) + "\n", encoding="utf-8")
    print(f"wrote subsampled eval list {cached} ({len(selected)} lines, green={len(green_lines)})", flush=True)
    return cached


def latest_checkpoint(output: Path) -> Path | None:
    latest = output / "latest.pdparams"
    if latest.exists():
        return output / "latest"
    epochs = sorted(output.glob("iter_epoch_*.pdparams"))
    if epochs:
        return epochs[-1].with_suffix("")
    best = output / "best_accuracy.pdparams"
    if best.exists():
        return output / "best_accuracy"
    return None


def main() -> int:
    args = parse_args()
    paddleocr = args.paddleocr.expanduser().resolve()
    config = args.config.expanduser().resolve()
    pretrained = args.pretrained.expanduser().resolve()
    data = args.data.expanduser().resolve()
    output = args.output.expanduser().resolve()
    train_py = paddleocr / "tools" / "train.py"
    eval_list = resolve_eval_list(data, args.eval_list, args.max_eval) if (data / "val.txt").exists() else data / "val.txt"
    missing = [
        str(path)
        for path in (paddleocr, config, pretrained, data / "train.txt", eval_list, train_py)
        if not path.exists()
    ]
    if missing:
        print("缺少训练资产：", file=sys.stderr)
        print("\n".join(missing), file=sys.stderr)
        return 2
    output.mkdir(parents=True, exist_ok=True)
    gpu_list = [item.strip() for item in args.gpus.split(",") if item.strip()]
    checkpoint = args.checkpoints.expanduser().resolve() if args.checkpoints else None
    if checkpoint is None and args.resume:
        checkpoint = latest_checkpoint(output)
    overrides = [
        f"Global.pretrained_model={pretrained}",
        f"Global.save_model_dir={output}",
        f"Global.epoch_num={args.epochs}",
        f"Global.eval_batch_step=[0, {args.eval_step}]",
        f"Optimizer.lr.learning_rate={args.lr}",
        f"Train.dataset.data_dir={data}",
        f"Train.dataset.label_file_list=[{data / 'train.txt'}]",
        f"Train.sampler.first_bs={args.batch}",
        f"Train.loader.batch_size_per_card={args.batch}",
        f"Train.loader.num_workers={args.workers}",
        f"Eval.dataset.data_dir={data}",
        f"Eval.dataset.label_file_list=[{eval_list}]",
        f"Eval.loader.batch_size_per_card={args.batch}",
        f"Eval.loader.num_workers={max(1, args.workers // 2)}",
    ]
    if checkpoint is not None:
        overrides.append(f"Global.checkpoints={checkpoint}")
        print(f"resuming from {checkpoint}", flush=True)
    env = os.environ.copy()
    env["FLAGS_allocator_strategy"] = "auto_growth"
    env["FLAGS_conv_workspace_size_limit"] = "4096"
    env["CUDA_VISIBLE_DEVICES"] = ",".join(gpu_list)
    if len(gpu_list) > 1:
        cmd = [
            sys.executable, "-m", "paddle.distributed.launch",
            "--gpus", ",".join(gpu_list),
            str(train_py),
            "-c", str(config),
            "-o", *overrides,
        ]
    else:
        cmd = [sys.executable, str(train_py), "-c", str(config), "-o", *overrides]
    print("cwd:", paddleocr)
    print("eval_list:", eval_list)
    print("cmd:", " ".join(cmd), flush=True)
    return subprocess.call(cmd, cwd=str(paddleocr), env=env)


if __name__ == "__main__":
    raise SystemExit(main())
