#!/usr/bin/env python3
"""Run the frozen YOLO detector and PaddleOCR recognizer on one image."""

from __future__ import annotations

import argparse
import json
import math
import os
import re
import signal
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PROVINCES = "京津沪渝冀豫云辽黑湘皖鲁新苏浙赣鄂桂甘晋蒙陕吉闽贵粤青藏川宁琼"
PLATE_PATTERN = re.compile(rf"^[{PROVINCES}][A-HJ-NP-Z][A-HJ-NP-Z0-9]{{5,6}}$")


def valid_plate(text: str) -> bool:
    return PLATE_PATTERN.fullmatch(text) is not None


def parse_paddle_result(content: str, image: Path) -> tuple[str, float]:
    for line in content.splitlines():
        fields = line.split("\t")
        if len(fields) == 3 and Path(fields[0]) == image:
            return fields[1].strip(), float(fields[2])
    raise ValueError("PaddleOCR did not return a result for the detected crop")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("--detector", type=Path, default=ROOT / "model/weights/smartpark_plate_yolo11m_best.pt")
    parser.add_argument("--recognizer", type=Path, default=ROOT / "model/weights/smartpark_plate_ppocrv5_best.pdparams")
    parser.add_argument("--config", type=Path, default=ROOT / "model/weights/smartpark_plate_ppocrv5_config.yml")
    parser.add_argument("--paddleocr", type=Path, default=ROOT / "third_party/PaddleOCR")
    parser.add_argument("--ocr-python", type=Path, default=None)
    parser.add_argument("--dictionary", type=Path, default=ROOT / "scripts/rec/ppocrv5_dict.txt")
    parser.add_argument("--confidence", type=float, default=0.25)
    return parser.parse_args()


def run_ocr(command: list[str], environment: dict[str, str], paddleocr: Path) -> subprocess.CompletedProcess:
    options = {"cwd": paddleocr, "env": environment, "text": True,
               "stdout": subprocess.PIPE, "stderr": subprocess.STDOUT}
    if os.name != "nt":
        options["start_new_session"] = True
    with subprocess.Popen(command, **options) as process:
        def terminate_child():
            if os.name != "nt":
                os.killpg(process.pid, signal.SIGKILL)
            else:
                process.kill()

        def stop_child(_signum, _frame):
            terminate_child()
            raise SystemExit(1)

        previous = signal.signal(signal.SIGTERM, stop_child)
        try:
            try:
                output, _ = process.communicate(timeout=180)
            except subprocess.TimeoutExpired:
                terminate_child()
                process.communicate()
                raise
        finally:
            signal.signal(signal.SIGTERM, previous)
        return subprocess.CompletedProcess(command, process.returncode, output)


def recognize(args: argparse.Namespace) -> dict:
    from ultralytics import YOLO
    import cv2

    image = args.image.expanduser().resolve()
    detector = args.detector.expanduser().resolve()
    recognizer = args.recognizer.expanduser().resolve()
    config = args.config.expanduser().resolve()
    paddleocr = args.paddleocr.expanduser().resolve()
    ocr_python = args.ocr_python or os.environ.get("SMARTPARK_OCR_PY")
    if not ocr_python:
        raise ValueError("Set SMARTPARK_OCR_PY or pass --ocr-python")
    ocr_python = Path(ocr_python).expanduser().resolve()
    dictionary = args.dictionary.expanduser().resolve()
    for path in (image, detector, recognizer, config, dictionary,
                 paddleocr / "tools/infer_rec.py", ocr_python):
        if not path.is_file():
            raise FileNotFoundError(path)
    if not math.isfinite(args.confidence) or not 0 <= args.confidence <= 1:
        raise ValueError("confidence must be between 0 and 1")

    frame = cv2.imread(str(image), cv2.IMREAD_COLOR)
    if frame is None:
        raise ValueError(f"Cannot decode image: {image}")
    detection = YOLO(str(detector)).predict(frame, imgsz=960, conf=args.confidence,
                                            device="cpu", verbose=False)[0]
    if len(detection.boxes) == 0:
        raise ValueError("No license plate detected")
    best = int(detection.boxes.conf.argmax().item())
    confidence = float(detection.boxes.conf[best].item())
    x1, y1, x2, y2 = (int(value) for value in detection.boxes.xyxy[best].tolist())
    x1, x2 = max(0, x1), min(frame.shape[1], x2)
    y1, y2 = max(0, y1), min(frame.shape[0], y2)
    if x2 <= x1 or y2 <= y1:
        raise ValueError("Invalid detected plate bounds")

    with tempfile.TemporaryDirectory(prefix="smartpark-lpr-") as directory:
        crop = Path(directory) / "plate.jpg"
        results = Path(directory) / "results.txt"
        if not cv2.imwrite(str(crop), frame[y1:y2, x1:x2]):
            raise ValueError("Could not save plate crop")
        command = [
            str(ocr_python), "-B", str(paddleocr / "tools/infer_rec.py"), "-c", str(config), "-o",
            f"Global.pretrained_model={recognizer}", "Global.use_gpu=False",
            "Global.distributed=False", f"Global.character_dict_path={dictionary}",
            f"Global.infer_img={crop}", f"Global.save_res_path={results}",
        ]
        environment = os.environ.copy()
        environment["CUDA_VISIBLE_DEVICES"] = ""
        environment["PYTHONDONTWRITEBYTECODE"] = "1"
        completed = run_ocr(command, environment, paddleocr)
        if completed.returncode != 0:
            raise RuntimeError(f"PaddleOCR failed: {completed.stdout[-1500:]}")
        plate, recognition_confidence = parse_paddle_result(results.read_text(encoding="utf-8"), crop)

    return {
        "plate": plate,
        "detection_confidence": confidence,
        "recognition_confidence": recognition_confidence,
        "bounding_box": [x1, y1, x2, y2],
        "valid": valid_plate(plate),
    }


def main() -> int:
    try:
        print(json.dumps(recognize(parse_args()), ensure_ascii=False))
        return 0
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
