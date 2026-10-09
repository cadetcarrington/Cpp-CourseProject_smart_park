#!/usr/bin/env python3
"""Lightweight stdlib bridge from --lpr-command to the resident LPR service."""
from __future__ import annotations

import argparse
import json
import socket
import sys
from pathlib import Path


def request(image, address, timeout=55):
    payload = (json.dumps({"image": str(Path(image).expanduser().resolve())}) + "\n").encode()
    if len(payload) > 8192:
        raise ValueError("Image path is too long")
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
        connection.settimeout(timeout)
        connection.connect(str(Path(address).expanduser().absolute()))
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
    args = parser.parse_args()
    try:
        print(json.dumps(request(args.image, args.socket)["result"], ensure_ascii=False))
        return 0
    except (OSError, ValueError, RuntimeError) as error:
        print(str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
