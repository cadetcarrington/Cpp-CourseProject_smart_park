import sys
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from recognize_plate import parse_args, parse_paddle_result, valid_plate


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

    def test_dictionary_is_bundled(self):
        with patch.object(sys, "argv", ["recognize_plate.py", "example.jpg"]):
            self.assertTrue(parse_args().dictionary.is_file())


if __name__ == "__main__":
    unittest.main()
