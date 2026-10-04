#!/usr/bin/env python3
"""Run the frozen YOLO detector and PaddleOCR recognizer on one image.

裁剪配方与训练侧共用 `scripts/plate_geometry.py`：识别权重是按四角透视矫正的
裁剪训练的，所以推理默认也走同一条几何（`--crop auto` 跟随配方；检测器只给出
轴对齐框时自动回退到 `bbox`，并在输出的 `crop` 字段里说明用了哪一种）。
"""

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
DEFAULT_RECIPE_PATH = ROOT / "model/weights/smartpark_plate_crop_recipe.json"
CROP_MODES = ("auto", "quad", "bbox")


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
    parser.add_argument("--crop", choices=CROP_MODES, default="auto",
                        help="auto=按配方（默认 quad，检测器无四角时回退 bbox）；quad=强制四角矫正；bbox=强制轴对齐框")
    parser.add_argument("--crop-recipe", type=Path, default=None,
                        help="裁剪配方 JSON，缺省读 model/weights/smartpark_plate_crop_recipe.json")
    parser.add_argument("--flip-check", action="store_true",
                        help="四角来自 OBB（无法区分上下）时，对裁剪图与其 180° 各识别一次取更可信的结果")
    return parser.parse_args()


def as_nested_list(value):
    """torch tensor / numpy array → 嵌套 list；其它类型原样返回。"""
    if hasattr(value, "tolist"):
        return value.tolist()
    for attribute in ("detach", "cpu", "tolist"):
        if hasattr(value, attribute):
            value = getattr(value, attribute)()
    return value


def detection_quad(detection, index: int) -> tuple[list[tuple[float, float]], str] | None:
    """从检测结果里取车牌四角：OBB 四角优先，其次 4 关键点；都没有则 None。

    返回 (四点, 来源)：来源 "keypoints" 表示顺序自带语义（0=左上…），必须原样使用；
    "obb" 只是几何四角，调用方需要自己排序，且无法区分上下（可用 --flip-check）。
    """
    obb = getattr(detection, "obb", None)
    corners = getattr(obb, "xyxyxyxy", None) if obb is not None else None
    if corners is not None and len(corners) > index:
        points = as_nested_list(corners[index])
        if isinstance(points, (list, tuple)) and len(points) == 4:
            parsed = []
            for point in points:
                if not isinstance(point, (list, tuple)) or len(point) < 2:
                    return None
                parsed.append((float(point[0]), float(point[1])))
            if all(math.isfinite(x) and math.isfinite(y) for x, y in parsed):
                return parsed, "obb"

    keypoints = getattr(detection, "keypoints", None)
    xy = getattr(keypoints, "xy", None) if keypoints is not None else None
    if xy is not None and len(xy) > index:
        points = as_nested_list(xy[index])
        if isinstance(points, (list, tuple)) and len(points) == 4:
            confidences = getattr(keypoints, "conf", None)
            if confidences is not None and len(confidences) > index:
                scores = as_nested_list(confidences[index])
                if isinstance(scores, (list, tuple)) and len(scores) == 4 and min(scores) < 0.5:
                    return None
            parsed = []
            for point in points:
                if not isinstance(point, (list, tuple)) or len(point) < 2:
                    return None
                parsed.append((float(point[0]), float(point[1])))
            if all(math.isfinite(x) and math.isfinite(y) for x, y in parsed) and any(
                    x or y for x, y in parsed):
                return parsed, "keypoints"
    return None


def resolve_recipe(explicit: Path | None):
    """读裁剪配方；显式路径优先，其次随权重发布的默认配方，最后是内置默认值。"""
    import plate_geometry

    if explicit is not None:
        path = explicit.expanduser().resolve()
        if not path.is_file():
            raise FileNotFoundError(path)
        return plate_geometry.CropRecipe.load(path)
    if DEFAULT_RECIPE_PATH.is_file():
        return plate_geometry.CropRecipe.load(DEFAULT_RECIPE_PATH)
    return plate_geometry.CropRecipe()


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


def recognize_crop(crop, name: str, context: dict) -> tuple[str, float]:
    """把一张裁剪图交给 PaddleOCR 识别，返回 (车牌, 识别置信度)。"""
    import plate_geometry

    directory = context["directory"]
    crop_path = directory / f"{name}.jpg"
    results_path = directory / f"{name}.txt"
    if not plate_geometry.write_crop(crop_path, crop, context["recipe"]):
        raise ValueError("Could not save plate crop")
    command = [
        str(context["ocr_python"]), "-B", str(context["paddleocr"] / "tools/infer_rec.py"),
        "-c", str(context["config"]), "-o",
        f"Global.pretrained_model={context['recognizer']}", "Global.use_gpu=False",
        "Global.distributed=False", f"Global.character_dict_path={context['dictionary']}",
        f"Global.infer_img={crop_path}", f"Global.save_res_path={results_path}",
    ]
    environment = os.environ.copy()
    environment["CUDA_VISIBLE_DEVICES"] = ""
    environment["PYTHONDONTWRITEBYTECODE"] = "1"
    completed = run_ocr(command, environment, context["paddleocr"])
    if completed.returncode != 0:
        raise RuntimeError(f"PaddleOCR failed: {completed.stdout[-1500:]}")
    return parse_paddle_result(results_path.read_text(encoding="utf-8"), crop_path)


def pick_flipped(first: tuple[str, float], second: tuple[str, float]) -> int:
    """OBB 上下歧义：格式合法优先，其次识别置信度高者。"""
    first_valid, second_valid = valid_plate(first[0]), valid_plate(second[0])
    if first_valid != second_valid:
        return 0 if first_valid else 1
    return 0 if first[1] >= second[1] else 1


def recognize(args: argparse.Namespace) -> dict:
    from ultralytics import YOLO
    import cv2

    import plate_geometry

    image = args.image.expanduser().resolve()
    detector = args.detector.expanduser().resolve()
    recognizer = args.recognizer.expanduser().resolve()
    config = args.config.expanduser().resolve()
    paddleocr = args.paddleocr.expanduser().resolve()
    ocr_python = args.ocr_python or os.environ.get("SMARTPARK_OCR_PY")
    if not ocr_python:
        raise ValueError("Set SMARTPARK_OCR_PY or pass --ocr-python")
    # 只展开为绝对路径，不能 resolve()：uv/venv 的 bin/python 是指向底层
    # CPython 的符号链接，resolve 后会绕过 pyvenv.cfg，丢失 venv 依赖。
    ocr_python = Path(ocr_python).expanduser().absolute()
    dictionary = args.dictionary.expanduser().resolve()
    for path in (image, detector, recognizer, config, dictionary,
                 paddleocr / "tools/infer_rec.py", ocr_python):
        if not path.is_file():
            raise FileNotFoundError(path)
    if not math.isfinite(args.confidence) or not 0 <= args.confidence <= 1:
        raise ValueError("confidence must be between 0 and 1")
    recipe = resolve_recipe(args.crop_recipe)

    frame = cv2.imread(str(image), cv2.IMREAD_COLOR)
    if frame is None:
        raise ValueError(f"Cannot decode image: {image}")
    detection = YOLO(str(detector)).predict(frame, imgsz=960, conf=args.confidence,
                                            device="cpu", verbose=False)[0]
    if len(detection.boxes) == 0:
        raise ValueError("No license plate detected")
    best = int(detection.boxes.conf.argmax().item())
    confidence = float(detection.boxes.conf[best].item())
    bounds = plate_geometry.clip_bounds(
        as_nested_list(detection.boxes.xyxy[best]), frame.shape[1], frame.shape[0])

    corners = detection_quad(detection, best)
    mode = recipe.mode if args.crop == "auto" else args.crop
    if mode == "quad" and corners is None:
        if args.crop == "quad":
            raise ValueError("检测器没有输出车牌四角（OBB/4 关键点）；"
                             "请换四角检测权重或改用 --crop bbox")
        mode = "bbox"  # auto：没有四角时按轴对齐框裁剪，并在结果里说明
    quad = None
    quad_source = "bbox"
    if corners is not None:
        quad = corners[0] if corners[1] == "keypoints" else plate_geometry.order_quad(corners[0])
        quad_source = corners[1]
    if mode == "quad":
        crop = plate_geometry.prepare_crop(frame, quad, recipe, mode="quad")
    else:
        crop = plate_geometry.crop_box(frame, bounds)

    context = {"directory": None, "recipe": recipe, "ocr_python": ocr_python, "paddleocr": paddleocr,
               "config": config, "recognizer": recognizer, "dictionary": dictionary}
    flip_checked = False
    with tempfile.TemporaryDirectory(prefix="smartpark-lpr-") as directory:
        context["directory"] = Path(directory)
        plate, recognition_confidence = recognize_crop(crop, "plate", context)
        if args.flip_check and mode == "quad" and quad_source == "obb":
            flipped = cv2.rotate(crop, cv2.ROTATE_180)
            other = recognize_crop(flipped, "plate-flipped", context)
            flip_checked = True
            if pick_flipped((plate, recognition_confidence), other) == 1:
                plate, recognition_confidence = other

    return {
        "plate": plate,
        "detection_confidence": confidence,
        "recognition_confidence": recognition_confidence,
        "bounding_box": list(bounds),
        "valid": valid_plate(plate),
        "crop": mode,
        "quad_source": quad_source,
        "flip_checked": flip_checked,
        "recipe": f"{recipe.mode}:h{recipe.height}:w{recipe.min_width}-{recipe.max_width}:e{recipe.expand}",
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
