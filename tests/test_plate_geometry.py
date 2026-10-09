"""plate_geometry 的几何不变量与"与历史训练裁剪一致"的黄金测试。

黄金值取自重构前的 scripts/prepare_recognition.py（ordered_quad + expand_quad +
getPerspectiveTransform + warpPerspective，height=64 / 160..320 / expand=0.06），
合成图是确定性图案，所以这里可以逐像素比对，防止训练/推理两侧几何各自漂移。
"""

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))

try:
    import cv2
    import numpy as np

    import plate_geometry
    from plate_geometry import CropRecipe, ccpd_quad, crop_box, expand_quad, order_quad

    HAS_VISION = True
except ImportError:  # 系统 python 没有 numpy/opencv 时只跳过本文件
    HAS_VISION = False

# CCPD 标注顺序 RB, LB, LT, RT（与 prepare_recognition.py 一致）
CPPD_CASES = {
    "upright": [(400, 580), (100, 580), (100, 500), (400, 500)],
    "tilted": [(520, 560), (260, 610), (200, 500), (460, 450)],
    "extreme": [(600, 700), (300, 760), (80, 360), (420, 300)],
}
# 重构前实现（同一合成图）的实测值：shape / mean / std / 三个采样像素
GOLDEN = {
    "upright": {"shape": (64, 240, 3), "mean": 127.28461372, "std": 74.91517516,
                "px": [[125, 184, 230], [218, 143, 70], [47, 96, 158]]},
    "tilted": {"shape": (64, 160, 3), "mean": 127.61868490, "std": 72.74698888,
               "px": [[53, 187, 18], [226, 94, 116], [123, 249, 202]]},
    "extreme": {"shape": (64, 160, 3), "mean": 127.61236979, "std": 72.79387774,
                "px": [[191, 213, 217], [218, 165, 156], [209, 223, 136]]},
}


def synthetic_frame():
    height, width = 1160, 720
    ys, xs = np.mgrid[0:height, 0:width]
    frame = np.zeros((height, width, 3), np.uint8)
    frame[..., 0] = (xs * 7 % 256).astype(np.uint8)
    frame[..., 1] = (ys * 5 % 256).astype(np.uint8)
    frame[..., 2] = ((xs + ys) * 3 % 256).astype(np.uint8)
    return frame


def rotated_quad(angle: float):
    base = np.array([[-60, -12], [60, -12], [60, 12], [-60, 12]], np.float32)
    radians = np.deg2rad(angle)
    rotate = np.array([[np.cos(radians), -np.sin(radians)],
                       [np.sin(radians), np.cos(radians)]], np.float32)
    return base @ rotate.T + np.array([300, 400], np.float32)


@unittest.skipUnless(HAS_VISION, "需要 numpy/opencv（~/.smartpark/lpr 环境）")
class GeometryTest(unittest.TestCase):
    def test_rectify_matches_legacy_training_crop(self):
        # 形状必须完全一致；像素/均值只允许 OpenCV 4 与 5 的插值舍入差（实测 ≤1 灰阶、
        # 均值差 ≤0.01），几何一旦改动就会远超这个量级。
        frame = synthetic_frame()
        for name, vertices in CPPD_CASES.items():
            with self.subTest(case=name):
                quad = ccpd_quad(vertices)
                crop = plate_geometry.rectify(frame, expand_quad(quad, 0.06), CropRecipe())
                expected = GOLDEN[name]
                self.assertEqual(crop.shape, expected["shape"])
                self.assertLess(abs(float(crop.mean()) - expected["mean"]), 0.05)
                self.assertLess(abs(float(crop.std()) - expected["std"]), 0.05)
                samples = [crop[0, 0].tolist(), crop[32, crop.shape[1] // 2].tolist(),
                           crop[-1, -1].tolist()]
                for got, want in zip(samples, expected["px"]):
                    self.assertLessEqual(max(abs(a - b) for a, b in zip(got, want)), 2,
                                         f"{name}: {got} != {want}")

    def test_rectified_size_clamps_to_recipe(self):
        wide = np.array([[0, 0], [1000, 0], [1000, 50], [0, 50]], np.float32)
        tall = np.array([[0, 0], [50, 0], [50, 1000], [0, 1000]], np.float32)
        self.assertEqual(plate_geometry.rectified_size(wide, CropRecipe())[0], 320)
        self.assertEqual(plate_geometry.rectified_size(tall, CropRecipe())[0], 160)

    def test_order_quad_axis_aligned_corners(self):
        for angle in (0, 180):
            with self.subTest(angle=angle):
                ordered = order_quad(np.roll(rotated_quad(angle), 2, axis=0))
                expected = np.array([[240, 388], [360, 388], [360, 412], [240, 412]], np.float32)
                self.assertTrue(np.allclose(ordered, expected, atol=1e-4))

    def test_order_quad_keeps_long_edge_as_width(self):
        for angle in (15, -30, 45, 120, 200):
            with self.subTest(angle=angle):
                ordered = order_quad(np.roll(rotated_quad(angle), 1, axis=0))
                top = (ordered[0] + ordered[1]) / 2.0
                bottom = (ordered[2] + ordered[3]) / 2.0
                self.assertLessEqual(top[1], bottom[1] + 1e-6)
                self.assertGreater(np.linalg.norm(ordered[1] - ordered[0]),
                                   np.linalg.norm(ordered[3] - ordered[0]))

    def test_ccpd_quad_falls_back_to_geometric_order(self):
        degenerate = np.array([[10, 10], [10, 10], [10, 10], [10, 10]], np.float32)
        self.assertEqual(ccpd_quad(degenerate).shape, (4, 2))

    def test_recipe_roundtrip_and_validation(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "crop_recipe.json"
            recipe = CropRecipe(mode="bbox", height=48, min_width=96, max_width=288,
                                expand=0.05, quality=92)
            recipe.save(path, note="test")
            self.assertEqual(CropRecipe.load(path), recipe)
        for bad in ({"mode": "diagonal"}, {"height": 0}, {"min_width": 300, "max_width": 200},
                    {"expand": -1}, {"quality": 0}, {"surprise": 1}):
            with self.subTest(bad=bad):
                with self.assertRaises(ValueError):
                    CropRecipe.from_mapping(bad)

    def test_bbox_mode_matches_runtime_axis_aligned_crop(self):
        frame = synthetic_frame()
        quad = np.array([[530, 545], [255, 620], [205, 505], [480, 430]], np.float32)
        expected = frame[430:620, 205:530]
        self.assertTrue(np.array_equal(
            plate_geometry.prepare_crop(frame, quad, CropRecipe(mode="bbox")), expected))
        self.assertTrue(np.array_equal(crop_box(frame, plate_geometry.quad_bounds(quad)), expected))
        rectified = plate_geometry.prepare_crop(frame, quad, CropRecipe(mode="quad"))
        self.assertEqual(rectified.shape[0], 64)
        self.assertNotEqual(rectified.shape[1], expected.shape[1])
        with self.assertRaises(ValueError):
            plate_geometry.prepare_crop(frame, None, CropRecipe(mode="quad"))

    def test_clip_bounds_rejects_degenerate(self):
        with self.assertRaises(ValueError):
            plate_geometry.clip_bounds((10, 10, 10, 40), 720, 1160)
        self.assertEqual(plate_geometry.clip_bounds((-5, -5, 700, 1200), 720, 1160),
                         (0, 0, 700, 1160))

    def test_write_crop_uses_recipe_quality(self):
        frame = synthetic_frame()
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "crop.jpg"
            self.assertTrue(plate_geometry.write_crop(path, frame[:100, :300], CropRecipe(quality=95)))
            self.assertEqual(cv2.imread(str(path)).shape, (100, 300, 3))


if __name__ == "__main__":
    unittest.main()
