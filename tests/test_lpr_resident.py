import json
import socket
import socketserver
import sys
import tempfile
import threading
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import Mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import lpr_client
import lpr_service
from lpr_ocr_worker import cache_initializers, cache_once


class InitializerTests(unittest.TestCase):
    def test_initializers_reuse_models_but_still_process_each_image(self):
        module = SimpleNamespace(
            build_post_process=Mock(return_value=lambda prediction: str(prediction)),
            build_model=Mock(return_value=lambda image: image * 7),
            load_model=Mock(return_value=None),
            create_operators=Mock(return_value=lambda image: image + 1))
        original = {name: value for name, value in vars(module).items()}
        cache_initializers(module)
        results = []
        for image in (1, 2, 3):
            decode = module.build_post_process({}, {})
            model = module.build_model({})
            module.load_model({}, model)
            transform = module.create_operators([], {})
            results.append(decode(model(transform(image))))
        self.assertEqual(results, ["14", "21", "28"])
        for factory in original.values():
            self.assertEqual(factory.call_count, 1)

    def test_failed_initialization_is_not_cached(self):
        factory = Mock(side_effect=[RuntimeError("load failed"), object()])
        cached = cache_once(factory)
        with self.assertRaises(RuntimeError):
            cached()
        model = cached()
        self.assertIs(cached(), model)
        self.assertEqual(factory.call_count, 2)


class ComparisonTests(unittest.TestCase):
    def test_different_error_reasons_are_not_exact(self):
        from benchmark_lpr import compare
        before = {"status": "error", "error": "ValueError: No license plate detected"}
        after = {"status": "error", "error": "ValueError: Cannot decode image"}
        self.assertEqual(compare(before, after), (["failure reason"], False))
        self.assertEqual(compare(before, before), ([], True))

    def test_nonfinite_confidence_is_a_regression(self):
        from benchmark_lpr import compare
        before = {"status": "ok", "plate": "皖AMJ570", "recognition_confidence": 0.99}
        after = dict(before, recognition_confidence=float("nan"))
        self.assertEqual(compare(before, after), (["recognition_confidence"], False))


class FakeEngine:
    def __init__(self):
        self.images = []

    def recognize(self, image):
        if image.endswith("missing.jpg"):
            raise FileNotFoundError(image)
        self.images.append(image)
        return {"plate": "皖AMJ570", "recognition_confidence": 0.99,
                "valid": True, "crop": "quad"}


class WorkerProcessTests(unittest.TestCase):
    def test_symlink_crop_paths_and_repeated_worker_requests(self):
        import subprocess
        import textwrap
        from recognize_plate import parse_paddle_result

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            actual = root / "actual"
            actual.mkdir()
            alias = root / "alias"
            alias.symlink_to(actual, target_is_directory=True)
            tools = root / "paddleocr" / "tools"
            tools.mkdir(parents=True)
            (tools / "__init__.py").touch()
            (tools / "infer_rec.py").write_text(textwrap.dedent('''\
                import logging, sys
                from pathlib import Path
                from types import SimpleNamespace
                print("upstream import log")
                def preprocess():
                    flags = sys.argv[sys.argv.index("-o") + 1:]
                    global_config = dict(flag.split("=", 1)[0].split(".")[1:] + [flag.split("=", 1)[1]] for flag in flags)
                    return {"Global": global_config}, None, logging.getLogger("test-worker"), None
                program = SimpleNamespace(preprocess=preprocess)
                def initialize(*args):
                    print("initializer called")
                    return None
                build_post_process = initialize
                build_model = initialize
                load_model = initialize
                create_operators = initialize
                def main():
                    build_post_process(); build_model(); load_model(); create_operators()
                    path = config["Global"]["infer_img"]
                    text = "皖AMJ570" if "first" in path else "京A12345"
                    Path(config["Global"]["save_res_path"]).write_text(path + "\\t" + text + "\\t0.99\\n")
                '''), encoding="utf-8")
            command = [sys.executable, "-B", str(Path(lpr_service.__file__).with_name("lpr_ocr_worker.py")),
                       "--paddleocr", str(tools.parent), "--config", str(root / "config.yml"),
                       "--recognizer", str(root / "weights"), "--dictionary", str(root / "dict")]
            requests = [{"crop": str(alias / (name + ".jpg")),
                         "results": str(alias / (name + ".txt"))} for name in ("first", "second")]
            completed = subprocess.run(command, input="".join(json.dumps(row) + "\n" for row in requests),
                                       capture_output=True, text=True, timeout=10)
            self.assertEqual(completed.returncode, 0, completed.stderr)
            replies = [json.loads(line) for line in completed.stdout.splitlines()]
            self.assertEqual(replies[0], {"ready": True})
            self.assertEqual(len(replies), 3)
            self.assertEqual(completed.stderr.count("initializer called"), 4)
            for request, reply, expected in zip(requests, replies[1:], ("皖AMJ570", "京A12345")):
                crop = Path(request["crop"])
                self.assertNotEqual(crop, crop.resolve())
                self.assertEqual(parse_paddle_result(reply["content"], crop), (expected, 0.99))


class ProtocolTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.address = Path(self.directory.name) / "lpr.sock"
        self.engine = FakeEngine()
        self.server = socketserver.UnixStreamServer(str(self.address), lpr_service.RequestHandler)
        self.server.engine = self.engine
        self.thread = threading.Thread(target=self.server.serve_forever,
                                       kwargs={"poll_interval": 0.01}, daemon=True)
        self.thread.start()

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=2)
        self.assertFalse(self.thread.is_alive())
        self.directory.cleanup()

    def test_multiple_requests_do_not_reuse_a_previous_plate_result(self):
        for name in ("first.jpg", "second.jpg"):
            response = lpr_client.request(Path(self.directory.name) / name, self.address)
            self.assertEqual(response["result"]["plate"], "皖AMJ570")
            self.assertGreaterEqual(response["seconds"], 0)
        self.assertEqual([Path(image).name for image in self.engine.images],
                         ["first.jpg", "second.jpg"])

    def test_failure_remains_failure_and_next_request_succeeds(self):
        with self.assertRaisesRegex(RuntimeError, "FileNotFoundError"):
            lpr_client.request(Path(self.directory.name) / "missing.jpg", self.address)
        response = lpr_client.request(Path(self.directory.name) / "good.jpg", self.address)
        self.assertEqual(response["result"]["plate"], "皖AMJ570")
        self.assertEqual(len(self.engine.images), 1)

    def test_malformed_request_is_rejected_without_running_model(self):
        with socket.socket(socket.AF_UNIX) as connection:
            connection.connect(str(self.address))
            connection.sendall(b'{"image":42}\n')
            response = json.loads(connection.makefile("rb").readline())
        self.assertIn("error", response)
        self.assertEqual(self.engine.images, [])

    def test_oversized_request_is_rejected(self):
        with socket.socket(socket.AF_UNIX) as connection:
            connection.connect(str(self.address))
            connection.sendall(b"x" * 8193)
            response = json.loads(connection.makefile("rb").readline())
        self.assertIn("error", response)
        self.assertEqual(self.engine.images, [])

    def test_disconnected_client_does_not_stop_service(self):
        with socket.socket(socket.AF_UNIX) as connection:
            connection.connect(str(self.address))
            connection.sendall(b'{"image":"abandoned.jpg"}\n')
        response = lpr_client.request("next.jpg", self.address)
        self.assertEqual(response["result"]["plate"], "皖AMJ570")

    def test_client_does_not_fall_back_when_service_is_unavailable(self):
        with self.assertRaises(OSError):
            lpr_client.request("image.jpg", Path(self.directory.name) / "absent.sock")


if __name__ == "__main__":
    unittest.main()
