"""plate_color 的底色判定测试：合成号牌逐色验证 + 报文字段约定。

合成图是纯底色块加黑色/白色"文字条"，确定性图案，因此不需要模型或权重；
numpy 缺失时整体跳过（与 test_plate_geometry.py 同一套跳过策略）。
"""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))

try:
    import numpy as np

    import plate_color

    HAS_NUMPY = True
except ImportError:  # 系统 python 没有 numpy 时只跳过本文件
    HAS_NUMPY = False

# OpenCV 习惯的 BGR：(底色, 文字色)
PLATES = {
    "blue": ((200, 120, 30), (250, 250, 250)),
    "green": ((80, 175, 95), (20, 30, 25)),
    "yellow": ((70, 205, 235), (20, 30, 25)),
    "yellow_green": ((70, 200, 180), (20, 30, 25)),  # 大型新能源双拼：黄绿之间
    "white": ((245, 245, 245), (20, 20, 20)),
    "black": ((30, 30, 30), (245, 245, 245)),
}


def make_plate(background, text, width=240, height=64, bars=7):
    """底色块 + 等距文字条：文字条只占少部分像素，底色仍是主色。"""
    image = np.zeros((height, width, 3), dtype=np.uint8)
    image[:, :] = background
    bar_width = max(2, width // (bars * 3))
    for index in range(bars):
        left = int((index + 0.5) * width / bars)
        image[height // 4: height * 3 // 4, left: left + bar_width] = text
    return image


@unittest.skipUnless(HAS_NUMPY, "需要 numpy")
class PlateColorTests(unittest.TestCase):
    def test_each_standard_color(self):
        for name, (background, text) in PLATES.items():
            with self.subTest(color=name):
                verdict = plate_color.classify(make_plate(background, text))
                expected = "green" if name == "yellow_green" else name
                self.assertIn(verdict["color"], {expected, "yellow"} if name == "yellow_green" else {expected})
                self.assertGreaterEqual(verdict["color_confidence"], 0.5)
                self.assertLessEqual(verdict["color_confidence"], 1.0)

    def test_text_bars_do_not_flip_the_verdict(self):
        """文字条再粗也不能改判：底色像素始终占多数。"""
        for bars in (4, 7, 10):
            with self.subTest(bars=bars):
                verdict = plate_color.classify(make_plate((200, 120, 30), (250, 250, 250), bars=bars))
                self.assertEqual(verdict["color"], "blue")

    def test_new_energy_follows_plate_length(self):
        crop = make_plate((80, 175, 95), (20, 30, 25))
        self.assertTrue(plate_color.classify(crop, "晋AD12345")["new_energy"])   # 8 位 = 新能源
        self.assertFalse(plate_color.classify(crop, "晋A12345")["new_energy"])   # 7 位 = 普通号牌
        # 没有文本时不能凭颜色断言新能源：蓝牌也可能拍到绿色车身反光。
        self.assertFalse(plate_color.classify(crop)["new_energy"])

    def test_blue_plate_is_never_new_energy(self):
        verdict = plate_color.classify(make_plate((200, 120, 30), (250, 250, 250)), "晋A12345")
        self.assertEqual(verdict["color"], "blue")
        self.assertFalse(verdict["new_energy"])

    def test_missing_or_broken_input_is_unknown(self):
        for crop in (None, np.zeros((0, 0, 3), dtype=np.uint8), np.zeros((4, 4), dtype=np.uint8)):
            with self.subTest(shape=getattr(crop, "shape", None)):
                verdict = plate_color.classify(crop)
                self.assertEqual(verdict["color"], "unknown")
                self.assertEqual(verdict["color_confidence"], 0.0)
        # 灰度图按三通道处理，纯灰应判白底而不是崩掉。
        self.assertEqual(plate_color.classify(np.full((32, 96), 250, dtype=np.uint8))["color"], "white")

    def test_mid_gray_is_unknown(self):
        """既不够亮也不够暗（糊成一团的灰底）：不猜，报 unknown。"""
        verdict = plate_color.classify(np.full((64, 240, 3), 128, dtype=np.uint8))
        self.assertEqual(verdict["color"], "unknown")
        self.assertEqual(verdict["color_confidence"], 0.0)

    def test_grayscale_and_alpha_inputs(self):
        rgba = np.dstack([np.full((32, 96, 3), (200, 120, 30), dtype=np.uint8),
                          np.full((32, 96, 1), 255, dtype=np.uint8)])
        self.assertEqual(plate_color.classify(rgba)["color"], "blue")


if __name__ == "__main__":
    unittest.main(verbosity=2)
