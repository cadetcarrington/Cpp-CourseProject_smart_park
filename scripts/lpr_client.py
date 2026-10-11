#!/usr/bin/env python3
"""Lightweight stdlib bridge from --lpr-command to the resident LPR service."""
from __future__ import annotations

import argparse
import errno
import json
import math
import os
import socket
import sys
import time
from pathlib import Path


def connect(address, timeout, wait_ready=0, service_pid=None):
    if not math.isfinite(wait_ready) or wait_ready < 0:
        raise ValueError("Startup wait must be finite and nonnegative")
    deadline = time.monotonic() + wait_ready
    while True:
        connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        connection.settimeout(timeout)
        try:
            connection.connect(str(Path(address).expanduser().absolute()))
            return connection
        except OSError as error:
            connection.close()
            if error.errno not in (errno.ENOENT, errno.ECONNREFUSED) or time.monotonic() >= deadline:
                raise
            if service_pid is not None:
                try:
                    os.kill(service_pid, 0)
                except ProcessLookupError:
                    raise RuntimeError("Local inference service exited during startup") from error
            time.sleep(min(0.05, max(0, deadline - time.monotonic())))


def request(image, address, timeout=55, *, wait_ready=0, service_pid=None):
    payload = (json.dumps({"image": str(Path(image).expanduser().resolve())}) + "\n").encode()
    if len(payload) > 8192:
        raise ValueError("Image path is too long")
    with connect(address, timeout, wait_ready, service_pid) as connection:
        connection.sendall(payload)
        with connection.makefile("rb") as stream:
            line = stream.readline(65537)
    if not line or len(line) > 65536 or not line.endswith(b"\n"):
        raise RuntimeError("Incomplete or oversized resident response")
    response = json.loads(line)
    if not isinstance(response, dict):
        raise RuntimeError("Unexpected resident response")
    if "error" in response:
        raise RuntimeError(response["error"])
    result = response.get("result")
    if not isinstance(result, dict) or not isinstance(result.get("plate"), str) or not result["plate"]:
        raise RuntimeError("Resident response has no plate")
    return response


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("--socket", required=True, type=Path)
    parser.add_argument("--wait-ready", type=float, default=0)
    parser.add_argument("--service-pid", type=int)
    args = parser.parse_args()
    try:
        print(json.dumps(request(args.image, args.socket, wait_ready=args.wait_ready,
                                 service_pid=args.service_pid)["result"], ensure_ascii=False))
        return 0
    except (OSError, ValueError, RuntimeError) as error:
        print(str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
