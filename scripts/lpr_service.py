#!/usr/bin/env python3
"""Opt-in CPU FP32 resident LPR service over a private Unix socket."""
from __future__ import annotations

import argparse
import copy
import json
import os
import select
import signal
import socketserver
import subprocess
import sys
import tempfile
import time
from pathlib import Path

import recognize_plate


class PaddleWorker:
    def __init__(self, args, timeout=60):
        self.timeout = timeout
        self.diagnostics = tempfile.TemporaryFile()
        self.process = None
        interpreter = args.ocr_python or os.environ.get("SMARTPARK_OCR_PY")
        if not interpreter:
            self.diagnostics.close()
            raise ValueError("Set SMARTPARK_OCR_PY or pass --ocr-python")
        command = [str(Path(interpreter).expanduser().absolute()), "-B",
                   str(Path(__file__).with_name("lpr_ocr_worker.py")),
                   "--paddleocr", str(args.paddleocr.expanduser().resolve()),
                   "--config", str(args.config.expanduser().resolve()),
                   "--recognizer", str(args.recognizer),
                   "--dictionary", str(args.dictionary.expanduser().resolve())]
        environment = dict(os.environ, CUDA_VISIBLE_DEVICES="", PYTHONDONTWRITEBYTECODE="1")
        try:
            self.process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                            stderr=self.diagnostics, env=environment,
                                            start_new_session=True)
            if self.receive() != {"ready": True}:
                raise RuntimeError("Unexpected OCR worker readiness response")
        except BaseException:
            self.close()
            raise

    def receive(self):
        if not select.select([self.process.stdout], [], [], self.timeout)[0]:
            raise TimeoutError("Resident OCR worker timed out")
        line = self.process.stdout.readline(65537)
        if not line:
            self.diagnostics.seek(0, os.SEEK_END)
            size = self.diagnostics.tell()
            self.diagnostics.seek(max(0, size - 1500))
            detail = self.diagnostics.read().decode(errors="replace")
            raise RuntimeError("Resident OCR worker exited: " + detail)
        if len(line) > 65536 or not line.endswith(b"\n"):
            raise RuntimeError("Oversized or incomplete OCR response")
        return json.loads(line)

    def recognize(self, crop, results):
        self.process.stdin.write((json.dumps({"crop": str(crop), "results": str(results)}) + "\n").encode())
        self.process.stdin.flush()
        response = self.receive()
        if "error" in response:
            raise RuntimeError(response["error"])
        return recognize_plate.parse_paddle_result(response["content"], crop)

    def close(self):
        process = self.process
        if process is not None:
            if process.stdin:
                try:
                    process.stdin.close()
                except OSError:
                    pass
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()
            if process.stdout:
                process.stdout.close()
            self.process = None
        self.diagnostics.close()


class ResidentEngine:
    def __init__(self, args):
        from ultralytics import YOLO

        self.args = copy.copy(args)
        # Freeze model selection for the process lifetime; no silent fallback.
        self.args.detector = (args.detector or recognize_plate.default_detector()).expanduser().resolve()
        self.args.recognizer = (args.recognizer or recognize_plate.default_recognizer()).expanduser().resolve()
        self.model = YOLO(str(self.args.detector))
        self.worker = None
        self.worker = PaddleWorker(self.args)

    def recognize_crop(self, crop, results):
        if self.worker is None:
            self.worker = PaddleWorker(self.args)
        try:
            return self.worker.recognize(crop, results)
        except (OSError, ValueError, RuntimeError, KeyError):
            self.worker.close()
            self.worker = None
            raise

    def recognize(self, image):
        args = copy.copy(self.args)
        args.image = Path(image)
        return recognize_plate.recognize(args, detector_model=self.model,
                                         crop_recognizer=self.recognize_crop)

    def close(self):
        if self.worker is not None:
            self.worker.close()
            self.worker = None


class RequestHandler(socketserver.StreamRequestHandler):
    def handle(self):
        self.request.settimeout(60)
        try:
            line = self.rfile.readline(8193)
            if len(line) > 8192 or not line.endswith(b"\n"):
                raise ValueError("Oversized or incomplete request")
            request = json.loads(line)
            if set(request) != {"image"} or not isinstance(request["image"], str):
                raise ValueError("Expected one image path")
            started = time.perf_counter()
            result = self.server.engine.recognize(request["image"])
            response = {"result": result, "seconds": time.perf_counter() - started}
        except Exception as error:
            response = {"error": f"{type(error).__name__}: {error}"}
        try:
            self.wfile.write((json.dumps(response, ensure_ascii=False) + "\n").encode())
        except OSError:
            pass  # Timed-out clients must not stop the next request.


def serve(address, engine):
    # Bind fails if the path exists: never delete another process's socket/file.
    address = Path(address).expanduser().absolute()
    old_umask = os.umask(0o077)
    try:
        server = socketserver.UnixStreamServer(str(address), RequestHandler)
    finally:
        os.umask(old_umask)
    identity = address.stat()
    server.engine = engine
    print(json.dumps({"ready": True, "socket": str(address)}), flush=True)
    try:
        with server:
            server.serve_forever(poll_interval=0.2)
    finally:
        # Remove only the exact socket inode created by this server.
        try:
            current = address.lstat()
            if (current.st_dev, current.st_ino) == (identity.st_dev, identity.st_ino):
                address.unlink()
        except FileNotFoundError:
            pass


def main():
    parser = argparse.ArgumentParser(description=__doc__, add_help=False)
    parser.add_argument("--socket", required=True, type=Path)
    options, remaining = parser.parse_known_args()
    args = recognize_plate.parse_args(["<resident-request>", *remaining])
    # Same deployment defaults; no changes to precision or math settings.
    os.environ.setdefault("YOLO_OFFLINE", "true")

    def stop(_signum, _frame):
        raise KeyboardInterrupt

    previous = {sig: signal.signal(sig, stop) for sig in (signal.SIGINT, signal.SIGTERM)}
    engine = None
    try:
        engine = ResidentEngine(args)
        serve(options.socket, engine)
    except KeyboardInterrupt:
        return 0
    except (OSError, ValueError, RuntimeError) as error:
        print(str(error), file=sys.stderr)
        return 1
    finally:
        if engine is not None:
            engine.close()
        for sig, handler in previous.items():
            signal.signal(sig, handler)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
