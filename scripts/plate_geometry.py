#!/usr/bin/env python3
"""车牌四角几何：排序、外扩、透视矫正与裁剪配方。

训练裁剪（`scripts/prepare_recognition.py`）与推理裁剪（`scripts/recognize_plate.py`）
共用本模块，保证"训练看到的裁剪"与"推理送进识别器的裁剪"是同一套几何，
避免再次出现"训练用四角透视矫正、推理直接用检测框"这样的分布漂移。
"""

from __future__ import annotations

import json
from dataclasses import asdict, dataclass, fields
from pathlib import Path

import numpy as np

# OpenCV 只在真正裁剪/写盘时导入：prepare_ccpd.py 生成标签只需要几何计算。
DEFAULT_HEIGHT = 64
DEFAULT_MIN_WIDTH = 160
DEFAULT_MAX_WIDTH = 320
DEFAULT_EXPAND = 0.06
DEFAULT_QUALITY = 95
CROP_MODES = ("quad", "bbox")
EXTRA_RECIPE_KEYS = ("note", "source")


@dataclass(frozen=True)
class CropRecipe:
    """一次裁剪的完整配方。

    mode="quad"：按四角透视矫正（训练侧 `prepare_recognition.py` 的默认做法，
    也是现有权重的训练分布）；
    mode="bbox"：直接用轴对齐检测框裁剪（运行时的原始裁剪，供按运行分布重训的
    识别权重使用）。
    """

    mode: str = "quad"
    height: int = DEFAULT_HEIGHT
    min_width: int = DEFAULT_MIN_WIDTH
    max_width: int = DEFAULT_MAX_WIDTH
    expand: float = DEFAULT_EXPAND
    quality: int = DEFAULT_QUALITY

    def __post_init__(self) -> None:
        if self.mode not in CROP_MODES:
            raise ValueError(f"crop mode 必须是 {CROP_MODES} 之一: {self.mode!r}")
        if self.height < 1:
            raise ValueError(f"height 必须为正整数: {self.height}")
        if not 0 < self.min_width <= self.max_width:
            raise ValueError(f"min_width/max_width 非法: {self.min_width}/{self.max_width}")
        if self.expand < 0:
            raise ValueError(f"expand 不能为负: {self.expand}")
        if not 1 <= self.quality <= 100:
            raise ValueError(f"quality 必须在 1..100: {self.quality}")

    @classmethod
    def from_mapping(cls, data: dict) -> "CropRecipe":
        known = {field.name for field in fields(cls)}
        unknown = sorted(set(data) - known - set(EXTRA_RECIPE_KEYS))
        if unknown:
            raise ValueError(f"未知的裁剪配方字段: {unknown}")
        values = {key: data[key] for key in known if key in data}
        try:
            return cls(**values)
        except TypeError as error:  # 字段类型不对时给出可读错误
            raise ValueError(f"裁剪配方字段非法: {error}") from error

    @classmethod
    def load(cls, path: Path) -> "CropRecipe":
        try:
            data = json.loads(Path(path).read_text(encoding="utf-8"))
        except json.JSONDecodeError as error:
            raise ValueError(f"裁剪配方不是合法 JSON: {path}") from error
        if not isinstance(data, dict):
            raise ValueError(f"裁剪配方必须是 JSON 对象: {path}")
        return cls.from_mapping(data)

    def save(self, path: Path, note: str = "") -> None:
        payload = asdict(self)
        if note:
            payload["note"] = note
        Path(path).parent.mkdir(parents=True, exist_ok=True)
        Path(path).write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n",
                              encoding="utf-8")


def order_quad(points) -> np.ndarray:
    """任意循环顺序的四个角点 → (LT, RT, RB, LB)。

    YOLO-OBB 的 `xyxyxyxy` 只保证是绕行一圈的四个角，不保证从哪个角开始；先按
    绕质心的角度排成顺时针，再取最长边作上边，最后按"上边中点更靠上"的常识消除
    180° 歧义（OBB 无法从几何上唯一确定上下，必要时由调用方再做翻转校验）。
    """
    pts = np.asarray(points, dtype=np.float32).reshape(4, 2)
    centre = pts.mean(axis=0, keepdims=True)
    cyclic = pts[np.argsort(np.arctan2(pts[:, 1] - centre[0, 1], pts[:, 0] - centre[0, 0]))]
    edges = np.linalg.norm(np.roll(cyclic, -1, axis=0) - cyclic, axis=1)
    start = int(np.argmax(edges))
    ordered = np.stack([cyclic[(start + index) % 4] for index in range(4)])
    top = (ordered[0] + ordered[1]) / 2.0
    bottom = (ordered[2] + ordered[3]) / 2.0
    if top[1] > bottom[1]:
        ordered = np.stack([ordered[2], ordered[3], ordered[0], ordered[1]])
    return ordered.astype(np.float32)


def ccpd_quad(points) -> np.ndarray:
    """CCPD 文件名的四角标注顺序是 RB, LB, LT, RT（见 prepare_recognition.py），
    该顺序带语义，可直接还原成 (LT, RT, RB, LB)；退化标注回退到几何排序。
    """
    pts = np.asarray(points, dtype=np.float32).reshape(4, 2)
    rb, lb, lt, rt = pts
    ordered = np.stack([lt, rt, rb, lb], axis=0).astype(np.float32)
    if np.linalg.norm(ordered[0] - ordered[1]) > 1 and np.linalg.norm(ordered[0] - ordered[3]) > 1:
        return ordered
    return order_quad(pts)


def expand_quad(quad, ratio: float) -> np.ndarray:
    """以中心为原点按比例外扩，给车牌四周留出与训练一致的边距。"""
    points = np.asarray(quad, dtype=np.float32).reshape(4, 2)
    centre = points.mean(axis=0, keepdims=True)
    return (centre + (1.0 + ratio) * (points - centre)).astype(np.float32)


def rectified_size(quad, recipe: CropRecipe = CropRecipe()) -> tuple[int, int]:
    """按长边比与高度算矫正后的 (width, height)，宽度夹在 min_width..max_width。"""
    points = np.asarray(quad, dtype=np.float32).reshape(4, 2)
    width_top = float(np.linalg.norm(points[1] - points[0]))
    width_bottom = float(np.linalg.norm(points[2] - points[3]))
    plate_width = max(width_top, width_bottom, 1.0)
    plate_height = max(float(np.linalg.norm(points[3] - points[0])),
                       float(np.linalg.norm(points[2] - points[1])), 1.0)
    dest_w = int(round(recipe.height * plate_width / plate_height))
    return max(recipe.min_width, min(recipe.max_width, dest_w)), recipe.height


def rectify(frame, quad, recipe: CropRecipe = CropRecipe()) -> np.ndarray:
    """四角透视矫正到 recipe.height 高的正视图（训练侧同款插值）。"""
    import cv2

    points = np.asarray(quad, dtype=np.float32).reshape(4, 2)
    dest_w, dest_h = rectified_size(points, recipe)
    destination = np.array([[0, 0], [dest_w - 1, 0], [dest_w - 1, dest_h - 1], [0, dest_h - 1]],
                           dtype=np.float32)
    matrix = cv2.getPerspectiveTransform(points, destination)
    return cv2.warpPerspective(frame, matrix, (dest_w, dest_h), flags=cv2.INTER_CUBIC)


def quad_bounds(quad) -> tuple[float, float, float, float]:
    """四角的轴对齐外接框 (x1, y1, x2, y2)，用于回退裁剪与 GUI 画框。"""
    points = np.asarray(quad, dtype=np.float32).reshape(4, 2)
    return (float(points[:, 0].min()), float(points[:, 1].min()),
            float(points[:, 0].max()), float(points[:, 1].max()))


def clip_bounds(bounds, width: int, height: int) -> tuple[int, int, int, int]:
    """把浮点框裁剪到图像范围内并取整；越界或退化时抛 ValueError。"""
    x1, y1, x2, y2 = bounds
    left = max(0, min(int(x1), width))
    top = max(0, min(int(y1), height))
    right = max(0, min(int(x2), width))
    bottom = max(0, min(int(y2), height))
    if right <= left or bottom <= top:
        raise ValueError("Invalid detected plate bounds")
    return left, top, right, bottom


def crop_box(frame, bounds) -> np.ndarray:
    """轴对齐裁剪（运行时的原始裁剪，也是 mode="bbox" 的训练裁剪）。"""
    left, top, right, bottom = clip_bounds(bounds, frame.shape[1], frame.shape[0])
    return frame[top:bottom, left:right]


def prepare_crop(frame, quad, recipe: CropRecipe = CropRecipe(), mode: str | None = None) -> np.ndarray:
    """按配方裁剪：mode="quad" 做透视矫正，mode="bbox" 用四角的外接框。

    两种模式都需要四角；只拿到检测框的调用方直接用 `crop_box`。
    """
    chosen = mode or recipe.mode
    if chosen not in CROP_MODES:
        raise ValueError(f"crop mode 必须是 {CROP_MODES} 之一: {chosen!r}")
    if chosen == "quad":
        if quad is None:
            raise ValueError("缺少车牌四角，无法按 quad 配方裁剪")
        return rectify(frame, expand_quad(quad, recipe.expand), recipe)
    if quad is None:
        raise ValueError("缺少车牌四角，无法按 bbox 配方裁剪")
    return crop_box(frame, quad_bounds(quad))


def write_crop(path: Path, image, recipe: CropRecipe = CropRecipe()) -> bool:
    """写出裁剪图；JPEG 质量与训练侧一致，避免编码差异引入分布偏移。"""
    import cv2

    return bool(cv2.imwrite(str(path), image, [int(cv2.IMWRITE_JPEG_QUALITY), recipe.quality]))
