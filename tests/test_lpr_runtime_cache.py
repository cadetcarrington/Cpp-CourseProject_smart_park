"""Asset caching tests use tiny fake models; no inference/native build required."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import lpr_runtime_cache as cache


class RuntimeCacheTests(unittest.TestCase):
    def setUp(self):
        # Explicit workspace-local temporary directories, never ~/.smartpark.
        self.temporary = tempfile.TemporaryDirectory(prefix=".runtime-cache-test-", dir=Path(__file__).resolve().parents[1])
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.vendor = self.root / "project/third_party/PaddleOCR"
        assets = {
            "tools/infer_rec.py": b"from tools import program\n",
            "tools/program.py": b"VERSION = 'current local vendor'\n",
            "tools/__init__.py": b"",
            "ppocr/__init__.py": b"# ppocr\n",
            "ppocr/model.py": b"MODEL = 1\n",
            "ppocr/utils/dict/chars.txt": "京沪皖\n".encode(),
            "configs/rec/base.yml": b"Global: {}\n",
            "root_helper.py": b"HELPER = 1\n",
            "docs/example.py": b"excluded\n",
            "ppocr/__pycache__/ignore.py": b"excluded\n",
            "datasets/sample.txt": b"excluded\n",
            "pretrain_models/unused.pdparams": b"excluded\n",
            ".git/private.txt": b"excluded\n",
        }
        for name, data in assets.items():
            self.put(self.vendor / name, data)
        weights = self.root / "project/model/weights"
        self.args = SimpleNamespace(
            detector=weights / "detector.pt", recognizer=weights / "recognizer.pdparams",
            dictionary=self.root / "project/scripts/rec/dict.txt", config=weights / "selected.yml",
            paddleocr=self.vendor, confidence=0.25)
        self.put(self.args.detector, b"\x00exact-detector\xff")
        self.put(self.args.recognizer, b"\x00exact-recognizer\xff")
        self.put(self.args.dictionary, "京沪皖\n".encode())
        self.put(self.args.config, b"_BASE_: [../base.yml]\nGlobal: {}\n")
        self.put(weights.parent / "base.yml", b"_BASE_:\n  - nested/second.yaml\n")
        self.put(weights.parent / "nested/second.yaml", b"Global:\n  use_gpu: false\n")
        self.cache_root = self.root / "physical-cache"

    def put(self, path, data):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)

    def prepare(self):
        return cache.prepare(self.args, self.cache_root)

    def test_exact_bytes_copied_args_physical_tree_and_relative_configs(self):
        prepared = self.prepare()
        self.assertIsNot(prepared, self.args)
        self.assertEqual(prepared.confidence, self.args.confidence)
        for name in ("detector", "recognizer", "dictionary", "config"):
            source, target = getattr(self.args, name), getattr(prepared, name)
            self.assertNotEqual(source, target)
            self.assertTrue(target.resolve().is_relative_to(self.cache_root))
            self.assertFalse(target.is_symlink())
            self.assertEqual(target.read_bytes(), source.read_bytes())
        for name in ("tools/infer_rec.py", "tools/program.py", "ppocr/model.py", "ppocr/utils/dict/chars.txt", "root_helper.py"):
            self.assertEqual((prepared.paddleocr / name).read_bytes(), (self.vendor / name).read_bytes())
        self.assertEqual((prepared.config.parent / "../base.yml").read_bytes(), b"_BASE_:\n  - nested/second.yaml\n")
        self.assertEqual((prepared.config.parent / "../nested/second.yaml").read_bytes(), b"Global:\n  use_gpu: false\n")
        for name in (".git", "docs", "datasets", "pretrain_models", "ppocr/__pycache__"):
            self.assertFalse((prepared.paddleocr / name).exists())

    def test_stable_call_never_opens_sources_or_hashes_models_or_calls_git(self):
        prepared = self.prepare()
        source_root = self.root / "project"
        original_open = Path.open
        def checked_open(path, *args, **kwargs):
            if path.is_relative_to(source_root):
                raise AssertionError(f"Stable cache opened source: {path}")
            return original_open(path, *args, **kwargs)
        model_stats = [getattr(prepared, name).stat() for name in ("detector", "recognizer")]
        with patch.object(Path, "open", checked_open), patch.object(cache._GitBlobs, "_load", side_effect=AssertionError("Stable cache read Git index")):
            again = self.prepare()
        self.assertEqual(vars(again), vars(prepared))
        for name, before in zip(("detector", "recognizer"), model_stats):
            after = getattr(again, name).stat()
            self.assertEqual((before.st_ino, before.st_mtime_ns), (after.st_ino, after.st_mtime_ns))

    def test_only_changed_source_is_refreshed_and_untracked_file_is_added(self):
        prepared = self.prepare()
        unchanged = prepared.paddleocr / "tools/infer_rec.py"
        before = unchanged.stat()
        changed = self.vendor / "tools/program.py"
        self.put(changed, b"VERSION = 'modified local file'\n")
        self.put(self.vendor / "ppocr/new_module.py", b"NEW = True\n")
        self.put(self.args.recognizer, b"replacement-exact-model")
        again = self.prepare()
        self.assertEqual(again.recognizer.read_bytes(), b"replacement-exact-model")
        self.assertEqual((again.paddleocr / "tools/program.py").read_bytes(), changed.read_bytes())
        self.assertEqual((again.paddleocr / "ppocr/new_module.py").read_bytes(), b"NEW = True\n")
        self.assertEqual(unchanged.stat().st_ino, before.st_ino)
        self.assertEqual(unchanged.stat().st_mtime_ns, before.st_mtime_ns)

    def test_concurrent_preparations_copy_models_only_once(self):
        from concurrent.futures import ThreadPoolExecutor
        from contextlib import contextmanager
        import threading

        first_copy = threading.Event()
        release_copy = threading.Event()
        second_attempt = threading.Event()
        copied_models = []
        original_copy, original_lock = cache.shutil.copyfileobj, cache._lock

        def controlled_copy(source, target, **kwargs):
            if Path(source.name) == self.args.recognizer:
                copied_models.append(source.name)
                first_copy.set()
                if not release_copy.wait(5):
                    raise AssertionError("Concurrent test did not release first writer")
            return original_copy(source, target, **kwargs)

        @contextmanager
        def observed_lock(root):
            if first_copy.is_set():
                second_attempt.set()
            with original_lock(root):
                yield

        with patch.object(cache.shutil, "copyfileobj", controlled_copy), patch.object(cache, "_lock", observed_lock):
            with ThreadPoolExecutor(max_workers=2) as executor:
                first = executor.submit(self.prepare)
                try:
                    self.assertTrue(first_copy.wait(5), "First writer did not start")
                    second = executor.submit(self.prepare)
                    self.assertTrue(second_attempt.wait(5), "Second writer did not attempt lock")
                    self.assertFalse(second.done())
                finally:
                    release_copy.set()
                a, b = first.result(timeout=5), second.result(timeout=5)
        self.assertEqual(a.recognizer, b.recognizer)
        self.assertEqual(copied_models, [str(self.args.recognizer)])
        self.assertEqual(b.recognizer.read_bytes(), self.args.recognizer.read_bytes())

    def test_removed_vendor_module_is_removed_from_owned_cache(self):
        prepared = self.prepare()
        source = (self.vendor / "ppocr/model.py").resolve()
        self.assertTrue(source.is_relative_to(self.root))
        source.unlink()
        self.prepare()
        self.assertFalse((prepared.paddleocr / "ppocr/model.py").exists())

    @unittest.skipUnless(shutil.which("git"), "Git is required for indexed cloud-safe test")
    def test_git_index_blobs_avoid_source_reads_and_preserve_local_changes(self):
        # The real nested repository uses a .git file rather than a directory.
        # This fixture's ignored fake directory is first verified and removed.
        fake_git = (self.vendor / ".git").resolve()
        self.assertTrue(fake_git.is_relative_to(self.root))
        shutil.rmtree(fake_git)
        def git(*arguments):
            return subprocess.run(["git", "-C", str(self.vendor), *arguments], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        git("init", "-q")
        git("add", "tools", "ppocr", "configs", "root_helper.py")
        # Staged bytes must be used even without commits/HEAD, and an unstaged
        # modification must win over the blob. Retain indexed files' stat times.
        modified = self.vendor / "tools/program.py"
        previous = modified.stat()
        self.put(modified, b"MODIFIED = 'read actual working tree'\n")
        os.utime(modified, ns=(previous.st_atime_ns, previous.st_mtime_ns + 10_000_000))
        untracked = self.vendor / "ppocr/untracked.py"
        self.put(untracked, b"UNTRACKED = True\n")
        allowed = {modified, untracked}
        original_open = Path.open
        opened = []
        def checked_open(path, *args, **kwargs):
            if path.is_relative_to(self.vendor):
                opened.append(path)
                if path not in allowed:
                    raise AssertionError(f"Indexed cloud file opened: {path}")
            return original_open(path, *args, **kwargs)
        with patch.object(Path, "open", checked_open):
            prepared = self.prepare()
        self.assertEqual(set(opened), allowed)
        self.assertEqual((prepared.paddleocr / "tools/infer_rec.py").read_bytes(), b"from tools import program\n")
        self.assertEqual((prepared.paddleocr / "tools/program.py").read_bytes(), modified.read_bytes())
        self.assertEqual((prepared.paddleocr / "ppocr/untracked.py").read_bytes(), untracked.read_bytes())

    def test_missing_cached_file_is_repaired_and_corrupt_manifest_rebuilt(self):
        prepared = self.prepare()
        target = prepared.dictionary.resolve()
        self.assertTrue(target.is_relative_to(self.cache_root.resolve()))
        target.unlink()
        self.assertEqual(self.prepare().dictionary.read_bytes(), self.args.dictionary.read_bytes())
        manifests = list(self.cache_root.glob("*/manifest.json"))
        self.assertEqual(len(manifests), 1)
        manifests[0].write_text("invalid json", encoding="utf-8")
        self.assertEqual(self.prepare().detector.read_bytes(), self.args.detector.read_bytes())
        self.assertEqual(json.loads(manifests[0].read_text())["version"], cache._VERSION)

    def test_atomic_failure_keeps_old_file_and_does_not_publish_manifest(self):
        prepared = self.prepare()
        before = prepared.recognizer.read_bytes()
        self.put(self.args.recognizer, b"a newly selected version of weights")
        manifests = list(self.cache_root.glob("*/manifest.json"))
        manifest_before = manifests[0].read_bytes()
        original_copy = cache.shutil.copyfileobj
        def failing_copy(source, target, **kwargs):
            if Path(source.name) == self.args.recognizer:
                target.write(b"partial")
                raise OSError("simulated copy failure")
            return original_copy(source, target, **kwargs)
        with patch.object(cache.shutil, "copyfileobj", failing_copy):
            with self.assertRaisesRegex(OSError, "simulated"):
                self.prepare()
        self.assertEqual(prepared.recognizer.read_bytes(), before)
        self.assertEqual(manifests[0].read_bytes(), manifest_before)
        self.assertFalse(list(prepared.recognizer.parent.glob(".asset-*")))
        self.assertEqual(self.prepare().recognizer.read_bytes(), self.args.recognizer.read_bytes())

    def test_cache_symlink_destination_is_rejected(self):
        prepared = self.prepare()
        target = prepared.detector.resolve()
        self.assertTrue(target.is_relative_to(self.cache_root.resolve()))
        target.unlink()
        target.symlink_to(self.args.detector)
        with self.assertRaisesRegex(ValueError, "escapes|physical"):
            self.prepare()
        self.assertEqual(self.args.detector.read_bytes(), b"\x00exact-detector\xff")


if __name__ == "__main__":
    unittest.main()
