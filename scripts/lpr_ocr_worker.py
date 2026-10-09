#!/usr/bin/env python3
"""Keep upstream PaddleOCR's exact infer_rec path alive across crops.

Only the four model/operator initializers are memoized; image decoding,
transforms, FP32 forward, CTC decoding and result serialization remain upstream.
A worker has one immutable configuration and cannot change weights per request.
"""
from __future__ import annotations

import argparse
import json
import logging
import os
import sys
from pathlib import Path


def cache_once(factory):
    missing = object()
    value = missing

    def cached(*args, **kwargs):
        nonlocal value
        if value is missing:
            value = factory(*args, **kwargs)
        return value

    return cached


def cache_initializers(module):
    for name in ("build_post_process", "build_model", "load_model", "create_operators"):
        setattr(module, name, cache_once(getattr(module, name)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("paddleocr", "config", "recognizer", "dictionary"):
        parser.add_argument("--" + name, required=True, type=Path)
    args = parser.parse_args()
    # Native libraries and upstream logging must not contaminate JSON stdout.
    protocol = os.fdopen(os.dup(sys.stdout.fileno()), "w", buffering=1)
    os.dup2(sys.stderr.fileno(), sys.stdout.fileno())
    sys.stdout = sys.stderr
    paddleocr = args.paddleocr.expanduser().resolve()
    os.chdir(paddleocr)
    sys.path.insert(0, str(paddleocr))
    from tools import infer_rec

    initialized = False
    protocol.write(json.dumps({"ready": True}) + "\n")
    for line in sys.stdin:
        try:
            request = json.loads(line)
            if set(request) != {"crop", "results"}:
                raise ValueError("Expected crop and results paths")
            # Keep the lexical path used by the parent and original CLI: on
            # macOS /var resolves to /private/var, but TSV matching is literal.
            crop = Path(request["crop"]).absolute()
            results = Path(request["results"]).absolute()
            if not initialized:
                sys.argv = [str(paddleocr / "tools/infer_rec.py"), "-c",
                            str(args.config.expanduser().resolve()), "-o",
                            f"Global.pretrained_model={args.recognizer.expanduser().resolve()}",
                            "Global.use_gpu=False", "Global.distributed=False",
                            f"Global.character_dict_path={args.dictionary.expanduser().resolve()}",
                            f"Global.infer_img={crop}", f"Global.save_res_path={results}"]
                infer_rec.config, infer_rec.device, infer_rec.logger, _ = infer_rec.program.preprocess()
                infer_rec.logger.setLevel(logging.WARNING)
                cache_initializers(infer_rec)
                initialized = True
            infer_rec.config["Global"]["infer_img"] = str(crop)
            infer_rec.config["Global"]["save_res_path"] = str(results)
            infer_rec.main()
            # Use the same TSV parser in the parent as the original CLI.
            response = {"content": results.read_text(encoding="utf-8")}
        except Exception as error:
            response = {"error": f"{type(error).__name__}: {error}"}
        protocol.write(json.dumps(response, ensure_ascii=False) + "\n")
    protocol.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
