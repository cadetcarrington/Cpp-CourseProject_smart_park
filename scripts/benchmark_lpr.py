#!/usr/bin/env python3
"""Run resident LPR on labelled images and compare complete baseline outputs.

Pass the original recognize_plate model/config flags after benchmark options.
The comparison exits nonzero for changed text/geometry/validity, newly failed
images, or a confidence drift larger than 1e-6. Exact float equality is reported.
"""
from __future__ import annotations

import argparse
import csv
import json
import math
import statistics
import time
from pathlib import Path

import recognize_plate
from lpr_service import ResidentEngine

RESULT_KEYS = ("plate", "detection_confidence", "recognition_confidence", "bounding_box",
               "valid", "crop", "quad_source", "flip_checked", "recipe")
FLOAT_KEYS = ("detection_confidence", "recognition_confidence")


def compare(before, after):
    if before["status"] == "error" or after["status"] == "error":
        if before["status"] != after["status"]:
            return ["failure status"], False
        first = before.get("error", "")
        second = after.get("error", "")
        return ([] if first == second else ["failure reason"]), first == second
    changes = []
    exact = True
    for key in RESULT_KEYS:
        a, b = before.get(key), after.get(key)
        if a != b:
            exact = False
            if (key not in FLOAT_KEYS or a is None or b is None
                    or not math.isfinite(a) or not math.isfinite(b) or abs(a - b) > 1e-6):
                changes.append(key)
    return changes, exact


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--baseline", type=Path)
    options, remaining = parser.parse_known_args()
    args = recognize_plate.parse_args(["<benchmark-image>", *remaining])
    items = list(csv.DictReader(options.manifest.open(encoding="utf-8")))
    baseline = {}
    if options.baseline:
        baseline = {row["file"]: row for row in
                    (json.loads(line) for line in options.baseline.read_text().splitlines())}
        if set(baseline) != {item["file"] for item in items}:
            raise ValueError("Baseline must contain exactly the same image manifest")
    records, regressions, exact = [], [], 0
    startup = time.perf_counter()
    engine = ResidentEngine(args)
    startup = time.perf_counter() - startup
    try:
        with options.out.open("w", encoding="utf-8") as output:
            for index, item in enumerate(items, 1):
                expected = item.get("plate", item.get("expected_plate"))
                record = {"file": item["file"], "expected": expected}
                started = time.perf_counter()
                try:
                    result = engine.recognize(options.manifest.parent / item["file"])
                    record.update(result)
                    record["status"] = "ok" if result["plate"] == expected else "wrong"
                except Exception as error:
                    record.update(status="error", error=f"{type(error).__name__}: {error}")
                record["seconds"] = time.perf_counter() - started
                if baseline:
                    changed, identical = compare(baseline[item["file"]], record)
                    exact += identical
                    if changed:
                        regressions.append({"file": item["file"], "changed": changed})
                output.write(json.dumps(record, ensure_ascii=False) + "\n")
                output.flush()
                records.append(record)
                if index % 20 == 0:
                    print(f"{index}/{len(items)} changes={len(regressions)}", flush=True)
    finally:
        engine.close()
    warm = [row["seconds"] for row in records[1:] if row["status"] != "error"]
    summary = {
        "images": len(records), "correct": sum(row["status"] == "ok" for row in records),
        "errors": sum(row["status"] == "error" for row in records),
        "startup_seconds": startup, "first_request_seconds": records[0]["seconds"] if records else None,
        "warm_median_seconds": statistics.median(warm) if warm else None,
        "warm_p95_seconds": sorted(warm)[min(len(warm)-1, int(len(warm)*0.95))] if warm else None,
        "baseline_exact_outputs": exact, "changed_outputs": regressions,
        "comparison_confidence_tolerance": 1e-6,
    }
    summary_path = options.out.with_suffix(".summary.json")
    summary_path.write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n")
    print(json.dumps(summary, ensure_ascii=False, indent=2))
    return 1 if regressions else 0


if __name__ == "__main__":
    raise SystemExit(main())
