#!/usr/bin/env python3
"""车牌底色判定：蓝 / 绿 / 黄 / 白 / 黑，并给出是否新能源号牌。

只依赖 numpy，不引入新的模型或依赖：把裁剪后的车牌图转成 HSV，先看彩色像素
（背景底色）落在哪个色相区间；彩色像素太少时再按亮度区分白底与黑底。

新能源（绿牌/黄绿双拼）在中国的号牌规则里固定是 **8 位**，所以 `new_energy`
直接由号牌位数决定，不靠颜色猜——颜色只用来显示与留痕。这样即使底色判错，
也不会把 7 位的蓝牌误判成新能源。
"""
from __future__ import annotations

import numpy as np

COLORS = ("blue", "green", "yellow", "white", "black", "unknown")

# 色相区间（度，0=红）。绿牌与黄绿双拼都落在绿黄之间，统一按「绿/黄」家族报告。
_HUE_RANGES = (
    ("blue", 185.0, 265.0),
    ("green", 95.0, 185.0),
    ("yellow", 35.0, 95.0),
)

_SATURATED = 0.30      # 底色像素的最低饱和度
_MIN_VALUE = 0.30      # 底色像素的最低亮度（排除文字与阴影）
_COLORED_SHARE = 0.35  # 彩色像素占比达到它才按颜色判定
_WHITE_VALUE = 0.55    # 彩色不足时：亮度不低于它算白底像素
_BLACK_VALUE = 0.35    # 亮度不高于它算黑底像素
_MONO_SHARE = 0.50     # 白/黑判定要求这类像素占多数
_MIN_SIDE = 8          # 小于该尺寸的图不足以判定底色


def _hsv(rgb: np.ndarray) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """RGB(0..1) → (H 度, S 0..1, V 0..1)，不依赖 OpenCV。"""
    maximum = rgb.max(axis=-1)
    minimum = rgb.min(axis=-1)
    delta = maximum - minimum
    red, green, blue = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    safe = np.where(delta > 1e-6, delta, 1.0)
    hue = np.where(
        maximum == red,
        ((green - blue) / safe) % 6.0,
        np.where(maximum == green, ((blue - red) / safe) + 2.0, ((red - green) / safe) + 4.0),
    ) * 60.0
    hue = np.where(delta > 1e-6, hue % 360.0, 0.0)
    saturation = np.where(maximum > 1e-6, delta / np.maximum(maximum, 1e-6), 0.0)
    return hue, saturation, maximum


def classify(crop_bgr, plate_text: str | None = None) -> dict:
    """判定车牌底色。

    参数
    ----
    crop_bgr: numpy 数组（OpenCV 的 BGR 顺序）或 None。
    plate_text: 识别出的车牌文本，用于判断是否新能源号牌（8 位）。

    返回 ``{"color", "color_confidence", "new_energy"}``：
    - ``color``：blue / green / yellow / white / black / unknown
    - ``color_confidence``：0~1，彩色判定时是主色占比，白/黑判定时是亮度偏离度
    - ``new_energy``：号牌为 8 位（新能源）时为 True
    """
    result = {"color": "unknown", "color_confidence": 0.0, "new_energy": False}
    if isinstance(plate_text, str) and len(plate_text.strip()) == 8:
        result["new_energy"] = True
    if crop_bgr is None or not isinstance(crop_bgr, np.ndarray) or crop_bgr.size == 0:
        return result

    image = crop_bgr
    if image.ndim == 2:
        image = np.stack([image] * 3, axis=-1)
    if image.shape[-1] == 4:
        image = image[..., :3]
    if image.shape[-1] != 3:
        return result
    if min(int(image.shape[0]), int(image.shape[1])) < _MIN_SIDE:
        return result

    rgb = image.astype(np.float32) / 255.0
    rgb = rgb[..., ::-1]  # BGR → RGB
    hue, saturation, value = _hsv(rgb)

    colored = (saturation >= _SATURATED) & (value >= _MIN_VALUE)
    total = int(colored.sum())
    if total >= max(1, int(colored.size * _COLORED_SHARE)):
        counts = {}
        for name, low, high in _HUE_RANGES:
            counts[name] = int(np.count_nonzero(
                colored & (hue >= low) & (hue < high)))
        name, best = max(counts.items(), key=lambda item: item[1])
        if best > 0:
            result["color"] = name
            result["color_confidence"] = round(best / float(total), 4)
            return result
        # 有彩色像素但不属于任何号牌底色（例如红色车身反光）：按明暗回退。

    # 白底黑字 / 黑底白字：底色像素本身就是「亮」或「暗」的多数派。
    total_pixels = float(value.size)
    bright_share = float(np.count_nonzero(value >= _WHITE_VALUE)) / total_pixels
    dark_share = float(np.count_nonzero(value <= _BLACK_VALUE)) / total_pixels
    if bright_share >= dark_share and bright_share >= _MONO_SHARE:
        result["color"] = "white"
        result["color_confidence"] = round(bright_share, 4)
    elif dark_share > bright_share and dark_share >= _MONO_SHARE:
        result["color"] = "black"
        result["color_confidence"] = round(dark_share, 4)
    return result
