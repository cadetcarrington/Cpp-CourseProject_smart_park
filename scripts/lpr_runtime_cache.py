"""Physical, opt-in LPR assets cache; importing this module loads only stdlib.

prepare(args, cache_root=None) returns a shallow copy with the five runtime
paths redirected to a private, physical cache (default ~/.smartpark/runtime).
Source paths, sizes and nanosecond mtimes invalidate entries without reading or
hashing models on subsequent calls. Do not edit the managed cache directly.
"""
from __future__ import annotations

import copy
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import tempfile
from contextlib import contextmanager

_TEXT_SUFFIXES = {".py", ".yml", ".yaml", ".json", ".txt", ".cfg", ".ini", ".toml", ".csv", ".xml"}
_IGNORED = {".git", "__pycache__", "datasets", "docs", "doc", "pretrain_models", "pretrained", "weights"}
_VERSION = 1


def _metadata(path):
    info = path.stat()
    if not stat.S_ISREG(info.st_mode):
        raise ValueError(f"Runtime asset is not a regular file: {path}")
    return {"size": info.st_size, "mtime_ns": info.st_mtime_ns}


def _owned(root, target):
    """Verify the resolved destination before every replacement or deletion."""
    resolved = target.resolve()
    if resolved == root or not resolved.is_relative_to(root):
        raise ValueError(f"Cache destination escapes its owned root: {target}")
    if target.is_symlink() or resolved != target.absolute():
        raise ValueError(f"Cache destination must be a physical file: {target}")
    return target


@contextmanager
def _lock(root):
    # flock is released by the kernel on process exit, including crashes. Keep
    # the inode in place: unlinking a lock would let two owners lock different
    # inodes. PID is diagnostic only; possession of flock establishes ownership.
    import fcntl

    path = _owned(root, root / ".prepare.lock")
    fd = os.open(path, os.O_CREAT | os.O_RDWR | getattr(os, "O_NOFOLLOW", 0), 0o600)
    with os.fdopen(fd, "r+") as handle:
        fcntl.flock(handle, fcntl.LOCK_EX)
        handle.seek(0)
        handle.truncate()
        handle.write(str(os.getpid()) + "\n")
        handle.flush()
        try:
            yield
        finally:
            fcntl.flock(handle, fcntl.LOCK_UN)


def _atomic(root, target, writer):
    _owned(root, target)
    target.parent.mkdir(parents=True, exist_ok=True)
    _owned(root, target)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=target.parent, prefix=".asset-", delete=False) as handle:
            temporary = Path(handle.name)
            writer(handle)
            handle.flush()
            os.fsync(handle.fileno())
        _owned(root, temporary)
        _owned(root, target)
        os.replace(temporary, target)
    finally:
        if temporary is not None and temporary.exists():
            _owned(root, temporary).unlink()


def _text_files(directory):
    if not directory.is_dir():
        return
    for parent, directories, names in os.walk(directory):
        directories[:] = sorted(name for name in directories if name not in _IGNORED and not name.startswith("."))
        for name in sorted(names):
            path = Path(parent) / name
            if not name.startswith(".") and path.suffix.lower() in _TEXT_SUFFIXES:
                yield path


class _GitBlobs:
    """Read this working tree's index, never another checkout or a HEAD tree."""

    def __init__(self, vendor):
        self.vendor = vendor
        self.entries = None
        self.process = None
        self.root = None

    def _load(self):
        self.entries = {}
        environment = dict(os.environ, GIT_OPTIONAL_LOCKS="0")
        try:
            top = subprocess.run(["git", "-C", str(self.vendor), "rev-parse", "--show-toplevel"],
                                 check=True, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, env=environment)
            self.root = Path(os.fsdecode(top.stdout.rstrip(b"\n"))).resolve()
            prefix = self.vendor.relative_to(self.root)
            listing = subprocess.run(["git", "-C", str(self.root), "ls-files", "--stage", "--debug", "-z", "--", str(prefix)],
                                     check=True, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, env=environment)
        except (OSError, ValueError, subprocess.CalledProcessError):
            return  # Source archives without Git remain supported.
        # -z preserves arbitrary paths. --debug metadata follows the path's NUL.
        pattern = re.compile(rb"([0-7]+) ([0-9a-f]+) ([0-3])\t([^\x00]+)\x00"
                             rb"  ctime: (\d+):(\d+)\n  mtime: (\d+):(\d+)\n"
                             rb"  dev: [^\n]+\n  uid: [^\n]+\n  size: (\d+)\tflags: ([0-9a-f]+)\n")
        for match in pattern.finditer(listing.stdout):
            mode, oid, stage, name, _, _, seconds, nanos, size, flags = match.groups()
            # Conflicts, symlinks, intent-to-add and assume-unchanged/skip-worktree
            # entries are not reliable candidates for cloud-safe blob reuse.
            if stage != b"0" or mode not in (b"100644", b"100755") or int(flags, 16):
                continue
            path = self.root / os.fsdecode(name)
            self.entries[str(path)] = (oid, {"size": int(size), "mtime_ns": int(seconds) * 1_000_000_000 + int(nanos)})

    def read(self, source, metadata):
        if self.entries is None:
            self._load()
        entry = self.entries.get(str(source))
        if entry is None or entry[1] != metadata or source.is_symlink():
            return None
        if self.process is None:
            self.process = subprocess.Popen(["git", "-C", str(self.root), "cat-file", "--batch"],
                                            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                                            env=dict(os.environ, GIT_OPTIONAL_LOCKS="0"))
        self.process.stdin.write(entry[0] + b"\n")
        self.process.stdin.flush()
        header = self.process.stdout.readline().split()
        if len(header) != 3 or header[1] != b"blob":
            raise RuntimeError(f"Cannot read indexed runtime blob: {source}")
        size = int(header[2])
        data = self.process.stdout.read(size)
        if len(data) != size or self.process.stdout.read(1) != b"\n" or size != metadata["size"]:
            raise RuntimeError(f"Incomplete indexed runtime blob: {source}")
        return data

    def close(self):
        if self.process is not None:
            self.process.stdin.close()
            self.process.stdout.close()
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()


def _yaml_references(cached):
    # No PyYAML/model import: retain relative YAML files in _BASE_, !include,
    # quoted strings, inline lists and ordinary YAML path values. Copy bytes
    # verbatim; resolving references against the containing file preserves ../.
    text = cached.read_text(encoding="utf-8")
    return re.findall(r"(?:^|[\s\[,:\"'])([^\s\[\],\"'#]+\.ya?ml)(?=$|[\s\],\"'#])", text)


def prepare(args, cache_root: Path | None = None):
    """Cache exact selected assets and current PaddleOCR tree; return copied args.

    Requires detector/recognizer/config/dictionary/paddleocr attributes. None
    detector or recognizer selects recognize_plate's existing deployment default.
    Errors propagate so callers never silently use stale or alternative models.
    Only cache_root is written. Passing a local temporary root is useful in tests.
    """
    prepared = copy.copy(args)
    selections = {}
    for name in ("detector", "recognizer", "dictionary", "config", "paddleocr"):
        value = getattr(args, name)
        if value is None and name in ("detector", "recognizer"):
            # recognize_plate itself imports only stdlib; defer even this import.
            import recognize_plate
            value = getattr(recognize_plate, "default_" + name)()
        selections[name] = Path(value).expanduser().resolve()
    vendor = selections["paddleocr"]
    for name in ("tools/infer_rec.py", "tools/program.py", "ppocr/__init__.py"):
        _metadata(vendor / name)
    identity = hashlib.sha256(json.dumps({key: str(value) for key, value in selections.items()}, sort_keys=True).encode()).hexdigest()[:24]
    root = (Path(cache_root).expanduser() if cache_root is not None else Path.home() / ".smartpark/runtime").resolve()
    root.mkdir(parents=True, exist_ok=True, mode=0o700)
    namespace = _owned(root, root / identity)
    namespace.mkdir(exist_ok=True, mode=0o700)
    manifest_path = _owned(root, namespace / "manifest.json")

    def destination(source):
        # Mirroring absolute source paths keeps every relative include intact,
        # including a config outside the vendor directory and ../base.yml.
        return _owned(root, namespace / "tree" / str(source).lstrip(os.sep))

    with _lock(root):
        try:
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            old = manifest["files"] if manifest.get("version") == _VERSION else {}
            if not isinstance(old, dict):
                old = {}
        except (FileNotFoundError, ValueError, KeyError):
            old = {}
        files = {}
        blobs = _GitBlobs(vendor)

        def ensure(source):
            source = source.absolute()
            key = str(source)
            target = destination(source)
            if key in files:
                return target
            metadata = _metadata(source)
            previous = old.get(key, {})
            try:
                cached_metadata = _metadata(target)
            except FileNotFoundError:
                cached_metadata = None
            if previous.get("source") != metadata or previous.get("cached") != cached_metadata:
                data = blobs.read(source, metadata) if source.is_relative_to(vendor) else None
                if data is not None:
                    _atomic(root, target, lambda handle: handle.write(data))
                else:
                    def transfer(handle):
                        with source.open("rb") as original:
                            shutil.copyfileobj(original, handle, length=1024 * 1024)
                    _atomic(root, target, transfer)
                if _metadata(source) != metadata:
                    raise RuntimeError(f"Runtime asset changed while caching: {source}")
                cached_metadata = _metadata(target)
            files[key] = {"source": metadata, "cached": cached_metadata}
            return target

        try:
            for directory in ("tools", "ppocr", "configs"):
                for source in _text_files(vendor / directory):
                    ensure(source)
            # Preserve root-level Python modules without copying docs/weights.
            for source in sorted(vendor.glob("*.py")):
                ensure(source)
            for name in ("detector", "recognizer", "dictionary", "config"):
                setattr(prepared, name, ensure(selections[name]))
            # Also retain sibling configs and recursively referenced parent
            # configs. This covers base/include layouts without changing YAML.
            configs = {source for source in _text_files(selections["config"].parent)
                       if source.suffix.lower() in {".yml", ".yaml"}}
            configs.add(selections["config"])
            seen = set()
            while configs:
                source = configs.pop().absolute()
                if source in seen:
                    continue
                seen.add(source)
                for reference in _yaml_references(ensure(source)):
                    relative = Path(reference)
                    if not relative.is_absolute():
                        included = (source.parent / relative).resolve()
                        if included.is_file() and included not in seen:
                            configs.add(included)
            setattr(prepared, "paddleocr", destination(vendor))
            # Remove only files owned by the previous manifest that disappeared;
            # stale Python modules must not survive deletion in the working tree.
            for key in old.keys() - files.keys():
                source = Path(key)
                if not source.is_absolute():
                    raise ValueError("Invalid source path in runtime cache manifest")
                stale = destination(source)
                if stale.exists():
                    _owned(root, stale).unlink()
            if files != old:
                content = json.dumps({"version": _VERSION, "files": files}, sort_keys=True).encode()
                _atomic(root, manifest_path, lambda handle: handle.write(content))
        finally:
            blobs.close()
    return prepared
