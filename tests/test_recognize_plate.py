import io
import json
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import MagicMock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import recognize_plate
from recognize_plate import detection_quad, parse_args, parse_paddle_result, valid_plate

try:
    import cv2
    import numpy as np

    HAS_VISION = True
except ImportError:  # 系统 python 没有 numpy/opencv 时跳过裁剪相关用例
    HAS_VISION = False


class Boxes:
    """最小化的 ultralytics Boxes 替身：只用到 xyxy / conf / len。"""

    def __init__(self, xyxy, conf):
        self.xyxy = np.asarray(xyxy, dtype=np.float32)
        self.conf = np.asarray(conf, dtype=np.float32)

    def __len__(self):
        return len(self.conf)


class PlateRecognitionTest(unittest.TestCase):
    def test_valid_plate(self):
        self.assertTrue(valid_plate("京A12345"))
        self.assertTrue(valid_plate("粤AD12345"))
        self.assertFalse(valid_plate("粤I12345"))
        self.assertFalse(valid_plate("京A12O45 "))
        self.assertFalse(valid_plate("XX12345"))

    def test_parse_paddle_result(self):
        crop = Path("/tmp/crop.jpg")
        self.assertEqual(parse_paddle_result("/tmp/crop.jpg\t京A12345\t0.93\n", crop),
                         ("京A12345", 0.93))
        with self.assertRaises(ValueError):
            parse_paddle_result("/tmp/other.jpg\t京A12345\t0.93\n", crop)

    def test_ocr_python_requires_explicit_configuration(self):
        with patch.object(sys, "argv", ["recognize_plate.py", "example.jpg"]):
            self.assertIsNone(parse_args().ocr_python)
            self.assertEqual(parse_args().image, Path("example.jpg"))

    def test_crop_options_default_and_validation(self):
        with patch.object(sys, "argv", ["recognize_plate.py", "example.jpg"]):
            args = parse_args()
        self.assertEqual(args.crop, "auto")
        self.assertIsNone(args.crop_recipe)
        self.assertFalse(args.flip_check)
        with patch.object(sys, "argv", ["recognize_plate.py", "example.jpg", "--crop", "diagonal"]):
            with redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as error:
                parse_args()
        self.assertEqual(error.exception.code, 2)

    def test_batch_options_are_rejected(self):
        for extra in (["other.jpg"], ["--ocr-workers", "2"], ["--no-warmup"]):
            with self.subTest(extra=extra):
                with patch.object(sys, "argv", ["recognize_plate.py", "example.jpg", *extra]):
                    with redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as error:
                        parse_args()
                self.assertEqual(error.exception.code, 2)

    @unittest.skipUnless(HAS_VISION, "裁剪几何需要 numpy/opencv（~/.smartpark/lpr 环境）")
    def test_single_image_runs_ocr_with_venv_symlink(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            image = root / "image.jpg"
            detector = root / "detector.pt"
            recognizer = root / "recognizer.pdparams"
            config = root / "config.yml"
            dictionary = root / "dict.txt"
            paddleocr = root / "PaddleOCR"
            (paddleocr / "tools").mkdir(parents=True)
            for path in (image, detector, recognizer, config, dictionary,
                         paddleocr / "tools/infer_rec.py"):
                path.touch()
            venv_python = root / "venv" / "bin" / "python"
            venv_python.parent.mkdir(parents=True)
            venv_python.symlink_to(Path(sys.executable))
            args = SimpleNamespace(image=image, detector=detector, recognizer=recognizer,
                                   config=config, paddleocr=paddleocr, ocr_python=venv_python,
                                   dictionary=dictionary, confidence=0.25, crop="auto",
                                   crop_recipe=None, flip_check=False)
            confidence = MagicMock()
            confidence.argmax.return_value.item.return_value = 0
            confidence.__getitem__.return_value.item.return_value = 0.88
            boxes = MagicMock()
            boxes.__len__.return_value = 1
            boxes.conf = confidence
            boxes.xyxy[0].tolist.return_value = [2, 3, 10, 9]
            detection = SimpleNamespace(boxes=boxes)
            frame = MagicMock()
            frame.shape = (12, 16, 3)
            cv2 = MagicMock()
            cv2.IMREAD_COLOR = 1
            cv2.IMWRITE_JPEG_QUALITY = 1
            cv2.imread.return_value = frame
            cv2.imwrite.return_value = True
            yolo = MagicMock()
            yolo.return_value.predict.return_value = [detection]

            def fake_ocr(command, environment, cwd):
                self.assertEqual(command[0], str(venv_python.absolute()))
                self.assertNotEqual(command[0], str(venv_python.resolve()))
                self.assertEqual(cwd, paddleocr.resolve())
                self.assertEqual(environment["CUDA_VISIBLE_DEVICES"], "")
                self.assertEqual(environment["PYTHONDONTWRITEBYTECODE"], "1")
                self.assertEqual(command[-2].split("=", 1)[0], "Global.infer_img")
                self.assertEqual(command[-1].split("=", 1)[0], "Global.save_res_path")
                crop = Path(command[-2].split("=", 1)[1])
                results = Path(command[-1].split("=", 1)[1])
                results.write_text(f"{crop}\t京A12345\t0.93\n", encoding="utf-8")
                return subprocess.CompletedProcess(command, 0, "")

            with patch.dict(sys.modules, {"cv2": cv2, "ultralytics": SimpleNamespace(YOLO=yolo)}), \
                 patch.object(recognize_plate, "DEFAULT_RECIPE_PATH", root / "missing.json"), \
                 patch.object(recognize_plate, "run_ocr", side_effect=fake_ocr) as run_ocr:
                result = recognize_plate.recognize(args)
            self.assertEqual(run_ocr.call_count, 1)
            self.assertEqual(result, {"plate": "京A12345", "detection_confidence": 0.88,
                                      "recognition_confidence": 0.93,
                                      "bounding_box": [2, 3, 10, 9], "valid": True,
                                      "crop": "bbox", "quad_source": "bbox",
                                      "flip_checked": False,
                                      "recipe": "quad:h64:w160-320:e0.06"})

    def test_main_prints_one_result_or_one_error(self):
        output, errors = io.StringIO(), io.StringIO()
        with patch.object(recognize_plate, "parse_args", return_value=object()), \
             patch.object(recognize_plate, "recognize", return_value={"plate": "京A12345"}), \
             redirect_stdout(output), redirect_stderr(errors):
            self.assertEqual(recognize_plate.main(), 0)
        self.assertEqual(json.loads(output.getvalue()), {"plate": "京A12345"})
        self.assertEqual(len(output.getvalue().splitlines()), 1)
        self.assertEqual(errors.getvalue(), "")

        output, errors = io.StringIO(), io.StringIO()
        with patch.object(recognize_plate, "parse_args", return_value=object()), \
             patch.object(recognize_plate, "recognize", side_effect=ValueError("No license plate detected")), \
             redirect_stdout(output), redirect_stderr(errors):
            self.assertEqual(recognize_plate.main(), 1)
        self.assertEqual(output.getvalue(), "")
        self.assertEqual(errors.getvalue().strip(), "No license plate detected")

    def test_dictionary_is_bundled(self):
        with patch.object(sys, "argv", ["recognize_plate.py", "example.jpg"]):
            self.assertTrue(parse_args().dictionary.is_file())


class DetectionQuadTest(unittest.TestCase):
    """四角解析只依赖 torch/numpy 的 tolist 协议，用普通 list 就能覆盖。"""

    OBB = [[[10, 20], [30, 22], [31, 40], [11, 38]]]
    KEYPOINTS = [[[10, 20], [30, 22], [31, 40], [11, 38]]]

    def test_obb_corners_are_reported_as_obb(self):
        detection = SimpleNamespace(obb=SimpleNamespace(xyxyxyxy=self.OBB))
        self.assertEqual(detection_quad(detection, 0),
                         ([(10.0, 20.0), (30.0, 22.0), (31.0, 40.0), (11.0, 38.0)], "obb"))

    def test_keypoints_are_reported_as_semantic_order(self):
        detection = SimpleNamespace(keypoints=SimpleNamespace(xy=self.KEYPOINTS))
        self.assertEqual(detection_quad(detection, 0)[1], "keypoints")

    def test_low_confidence_keypoints_fall_back(self):
        detection = SimpleNamespace(keypoints=SimpleNamespace(
            xy=self.KEYPOINTS, conf=[[0.9, 0.4, 0.9, 0.9]]))
        self.assertIsNone(detection_quad(detection, 0))

    def test_detector_without_corners_returns_none(self):
        detection = SimpleNamespace(boxes=SimpleNamespace())
        self.assertIsNone(detection_quad(detection, 0))

    def test_degenerate_keypoints_are_rejected(self):
        detection = SimpleNamespace(keypoints=SimpleNamespace(
            xy=[[[0, 0], [0, 0], [0, 0], [0, 0]]]))
        self.assertIsNone(detection_quad(detection, 0))


@unittest.skipUnless(HAS_VISION, "需要 numpy/opencv（~/.smartpark/lpr 环境）")
class QuadCropIntegrationTest(unittest.TestCase):
    """用真实几何 + 打桩检测器验证裁剪模式选择；OCR 子进程被打桩。"""

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        root = Path(self.directory.name)
        self.root = root
        self.image = root / "image.jpg"
        frame = np.zeros((400, 600, 3), np.uint8)
        ys, xs = np.mgrid[0:400, 0:600]
        frame[..., 0] = (xs % 256).astype(np.uint8)
        frame[..., 1] = (ys % 256).astype(np.uint8)
        frame[..., 2] = ((xs + ys) % 256).astype(np.uint8)
        cv2.imwrite(str(self.image), frame)
        self.detector = root / "detector.pt"
        self.recognizer = root / "recognizer.pdparams"
        self.config = root / "config.yml"
        self.dictionary = root / "dict.txt"
        self.paddleocr = root / "PaddleOCR"
        (self.paddleocr / "tools").mkdir(parents=True)
        for path in (self.detector, self.recognizer, self.config, self.dictionary,
                     self.paddleocr / "tools/infer_rec.py"):
            path.touch()
        self.ocr_python = root / "ocr" / "bin" / "python"
        self.ocr_python.parent.mkdir(parents=True)
        self.ocr_python.symlink_to(Path(sys.executable))
        self.corners = [[[200, 200], [400, 230], [390, 280], [190, 250]]]
        self.crops = []

    def tearDown(self):
        self.directory.cleanup()

    def build_args(self, **overrides):
        values = dict(image=self.image, detector=self.detector, recognizer=self.recognizer,
                      config=self.config, paddleocr=self.paddleocr, ocr_python=self.ocr_python,
                      dictionary=self.dictionary, confidence=0.25, crop="auto",
                      crop_recipe=None, flip_check=False)
        values.update(overrides)
        return SimpleNamespace(**values)

    def run_recognize(self, args, boxes=None, obb=None, keypoints=None, replies=None):
        detection = SimpleNamespace(
            boxes=boxes or Boxes([[195, 195, 405, 285]], [0.9]),
            **({"obb": obb} if obb is not None else {}),
            **({"keypoints": keypoints} if keypoints is not None else {}))
        yolo = MagicMock()
        yolo.return_value.predict.return_value = [detection]

        def fake_ocr(command, environment, cwd):
            crop = Path(command[-2].split("=", 1)[1])
            image = cv2.imread(str(crop))
            self.crops.append((crop.name, None if image is None else image.shape))
            plate, score = (replies or {}).get(crop.name, ("京A12345", 0.9))
            Path(command[-1].split("=", 1)[1]).write_text(f"{crop}\t{plate}\t{score}\n",
                                                          encoding="utf-8")
            return subprocess.CompletedProcess(command, 0, "")

        with patch.dict(sys.modules, {"ultralytics": SimpleNamespace(YOLO=yolo)}), \
             patch.object(recognize_plate, "DEFAULT_RECIPE_PATH", self.root / "missing.json"), \
             patch.object(recognize_plate, "run_ocr", side_effect=fake_ocr):
            return recognize_plate.recognize(args)

    def test_obb_detector_is_rectified_to_training_geometry(self):
        result = self.run_recognize(self.build_args(),
                                    obb=SimpleNamespace(xyxyxyxy=np.asarray(self.corners, np.float32)))
        self.assertEqual(result["crop"], "quad")
        self.assertEqual(result["quad_source"], "obb")
        self.assertFalse(result["flip_checked"])
        name, shape = self.crops[0]
        self.assertEqual(name, "plate.jpg")
        self.assertEqual(shape[0], 64)          # 训练同款高度
        self.assertLessEqual(shape[1], 320)

    def test_pose_keypoints_keep_semantic_order(self):
        keypoints = SimpleNamespace(xy=np.asarray(self.corners, np.float32),
                                    conf=np.asarray([[0.9, 0.9, 0.9, 0.9]], np.float32))
        result = self.run_recognize(self.build_args(), keypoints=keypoints)
        self.assertEqual(result["quad_source"], "keypoints")
        self.assertEqual(result["crop"], "quad")

    def test_auto_falls_back_to_bbox_without_corners(self):
        result = self.run_recognize(self.build_args())
        self.assertEqual(result["crop"], "bbox")
        self.assertEqual(result["quad_source"], "bbox")
        self.assertEqual(self.crops[0][1], (90, 210, 3))

    def test_forced_quad_without_corners_fails_loudly(self):
        with self.assertRaises(ValueError) as error:
            self.run_recognize(self.build_args(crop="quad"))
        self.assertIn("四角", str(error.exception))

    def test_recipe_bbox_mode_ignores_corners(self):
        recipe = self.root / "recipe.json"
        recipe.write_text(json.dumps({"mode": "bbox"}), encoding="utf-8")
        result = self.run_recognize(self.build_args(crop_recipe=recipe),
                                    obb=SimpleNamespace(xyxyxyxy=np.asarray(self.corners, np.float32)))
        self.assertEqual(result["crop"], "bbox")
        self.assertEqual(self.crops[-1][1], (90, 210, 3))

    def test_flip_check_prefers_valid_format(self):
        replies = {"plate.jpg": ("京A1234", 0.99), "plate-flipped.jpg": ("皖AMJ570", 0.60)}
        result = self.run_recognize(
            self.build_args(flip_check=True),
            obb=SimpleNamespace(xyxyxyxy=np.asarray(self.corners, np.float32)), replies=replies)
        self.assertTrue(result["flip_checked"])
        self.assertEqual([name for name, _ in self.crops], ["plate.jpg", "plate-flipped.jpg"])
        self.assertEqual(result["plate"], "皖AMJ570")
        self.assertEqual(result["recognition_confidence"], 0.60)

    def test_flip_check_keeps_original_without_ambiguity(self):
        replies = {"plate.jpg": ("皖AMJ570", 0.80), "plate-flipped.jpg": ("京A1234", 0.95)}
        result = self.run_recognize(
            self.build_args(flip_check=True),
            obb=SimpleNamespace(xyxyxyxy=np.asarray(self.corners, np.float32)), replies=replies)
        self.assertTrue(result["flip_checked"])
        self.assertEqual(result["plate"], "皖AMJ570")
        self.assertEqual(len(self.crops), 2)


if __name__ == "__main__":
    unittest.main()
