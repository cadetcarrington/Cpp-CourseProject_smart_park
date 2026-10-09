"""数据准备脚本的标签格式与"训练裁剪 == 推理裁剪"防漂移测试。"""

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))

try:
    import cv2
    import numpy as np

    import plate_geometry
    import prepare_ccpd
    import prepare_recognition
    from plate_geometry import CropRecipe

    HAS_VISION = True
except ImportError:  # 系统 python 没有 numpy/opencv 时只跳过本文件
    HAS_VISION = False

# CCPD 文件名：area-tilt-bbox-vertices-plate-brightness-blur-index
# 顶点顺序 RB, LB, LT, RT；车牌 5_10_30_29_25_26_1 → 晋L6512B
BLUE_STEM = "0261-8_2-194&383_408&485-408&454_202&485_194&414_400&383-5_10_30_29_25_26_1-100-19"


def write_frame(path: Path):
    """大色块图案：对比强但频率低，JPEG q95 往返误差可忽略，便于严格比对裁剪。"""
    height, width = 1160, 720
    ys, xs = np.mgrid[0:height, 0:width]
    frame = np.zeros((height, width, 3), np.uint8)
    frame[..., 0] = ((xs // 60) * 37 % 256).astype(np.uint8)
    frame[..., 1] = ((ys // 60) * 53 % 256).astype(np.uint8)
    frame[..., 2] = (((xs // 40) + (ys // 40)) * 29 % 256).astype(np.uint8)
    cv2.imwrite(str(path), frame)
    return cv2.imread(str(path))


@unittest.skipUnless(HAS_VISION, "需要 numpy/opencv（~/.smartpark/lpr 环境）")
class LabelFormatTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.image = Path(self.directory.name) / f"{BLUE_STEM}.jpg"
        self.frame = write_frame(self.image)

    def tearDown(self):
        self.directory.cleanup()

    def test_det_label_is_normalized_box(self):
        values = prepare_ccpd.yolo_label(self.image, "det").split()
        self.assertEqual(len(values), 5)
        self.assertEqual(values[0], "0")
        cx, cy, width, height = (float(value) for value in values[1:])
        for value in (cx, cy, width, height):
            self.assertGreaterEqual(value, 0.0)
            self.assertLessEqual(value, 1.0)
        self.assertAlmostEqual(width, (408 - 194) / 720, places=6)
        self.assertAlmostEqual(height, (485 - 383) / 1160, places=6)

    def test_obb_label_has_four_normalized_corners(self):
        values = prepare_ccpd.yolo_label(self.image, "obb").split()
        self.assertEqual(len(values), 9)
        points = np.array([float(value) for value in values[1:]]).reshape(4, 2)
        self.assertTrue(((points >= 0) & (points <= 1)).all())
        # 语义顺序 LT, RT, RB, LB：LT 在 RB 上方，RT 在 LB 上方
        self.assertLess(points[0][1], points[2][1])
        self.assertLess(points[1][1], points[3][1])
        self.assertLess(points[0][0], points[1][0])

    def test_pose_label_has_four_visible_keypoints(self):
        values = prepare_ccpd.yolo_label(self.image, "pose").split()
        self.assertEqual(len(values), 4 + 1 + 4 * 3)
        box = [float(value) for value in values[1:5]]
        self.assertTrue(all(0.0 <= value <= 1.0 for value in box))
        keypoints = np.array([float(value) for value in values[5:]]).reshape(4, 3)
        self.assertTrue((keypoints[:, 2] == 2.0).all())
        self.assertTrue(((keypoints[:, :2] >= 0) & (keypoints[:, :2] <= 1)).all())

    def test_det_label_keeps_working_without_vertices(self):
        stem = "0001-90_85-262&503_529&572"  # 老格式（无四角字段）
        image = self.image.with_name(f"{stem}.jpg")
        cv2.imwrite(str(image), self.frame)
        self.assertIsNotNone(prepare_ccpd.yolo_label(image, "det"))
        self.assertIsNone(prepare_ccpd.yolo_label(image, "obb"))

    def test_dataset_yaml_declares_keypoint_shape_for_pose(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            (output / "images").mkdir()
            det_yaml = prepare_ccpd.write_yaml(output, "det")
            pose_yaml = prepare_ccpd.write_yaml(output, "pose")
            self.assertEqual(det_yaml.name, "ccpd.yaml")
            self.assertEqual(pose_yaml.name, "ccpd_pose.yaml")
            self.assertNotIn("kpt_shape", det_yaml.read_text(encoding="utf-8"))
            self.assertIn("kpt_shape: [4, 3]", pose_yaml.read_text(encoding="utf-8"))


@unittest.skipUnless(HAS_VISION, "需要 numpy/opencv（~/.smartpark/lpr 环境）")
class RecognitionCropTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.root = Path(self.directory.name)
        self.image = self.root / f"{BLUE_STEM}.jpg"
        self.frame = write_frame(self.image)

    def tearDown(self):
        self.directory.cleanup()

    def plate_and_vertices(self):
        parsed = prepare_recognition.parse_plate(self.image)
        self.assertIsNotNone(parsed)
        plate, vertices, bounds = parsed
        return plate, vertices, bounds

    def test_quad_mode_matches_training_geometry(self):
        plate, vertices, _ = self.plate_and_vertices()
        self.assertEqual(plate, "晋L6512B")
        destination = self.root / "quad.jpg"
        result = prepare_recognition.crop_plate(self.image, CropRecipe(mode="quad"), 0.0, destination)
        self.assertEqual(result[0], "晋L6512B")
        crop = cv2.imread(str(destination))
        expected = plate_geometry.prepare_crop(self.frame, plate_geometry.ccpd_quad(vertices),
                                               CropRecipe(mode="quad"), mode="quad")
        self.assertEqual(crop.shape, expected.shape)
        self.assertEqual(crop.shape[0], 64)
        self.assertLess(float(np.abs(crop.astype(np.int16) - expected.astype(np.int16)).mean()), 3.0)

    def test_bbox_mode_equals_what_inference_feeds(self):
        _, _, bounds = self.plate_and_vertices()
        destination = self.root / "bbox.jpg"
        result = prepare_recognition.crop_plate(self.image, CropRecipe(mode="bbox"), 0.0, destination)
        self.assertEqual(result[0], "晋L6512B")
        crop = cv2.imread(str(destination))
        runtime = plate_geometry.crop_box(self.frame, bounds)
        self.assertEqual(crop.shape, runtime.shape)
        self.assertLess(float(np.abs(crop.astype(np.int16) - runtime.astype(np.int16)).mean()), 3.0)
        quad_crop = plate_geometry.prepare_crop(self.frame, plate_geometry.ccpd_quad(
            self.plate_and_vertices()[1]), CropRecipe(mode="quad"), mode="quad")
        self.assertNotEqual(crop.shape, quad_crop.shape)

    def test_bbox_jitter_is_deterministic_and_outward(self):
        _, _, bounds = self.plate_and_vertices()
        first = prepare_recognition.jitter_bounds(bounds, 0.05, "seed")
        second = prepare_recognition.jitter_bounds(bounds, 0.05, "seed")
        self.assertEqual(first, second)
        self.assertNotEqual(first, bounds)
        self.assertLessEqual(first[0], bounds[0])
        self.assertLessEqual(first[1], bounds[1])
        self.assertGreaterEqual(first[2], bounds[2])
        self.assertGreaterEqual(first[3], bounds[3])
        self.assertEqual(prepare_recognition.jitter_bounds(bounds, 0.0, "seed"), bounds)


if __name__ == "__main__":
    unittest.main()
