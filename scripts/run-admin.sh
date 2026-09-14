#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build/qt}"

if [[ "$(uname -s)" == "Darwin" ]]; then
    APP="$BUILD_DIR/apps/admin/smartpark_admin.app/Contents/MacOS/smartpark_admin"
else
    APP="$BUILD_DIR/apps/admin/smartpark_admin"
fi

if [[ ! -x "$APP" ]]; then
    echo "Admin GUI not built: $APP" >&2
    echo "Run scripts/build-admin.sh first." >&2
    exit 1
fi

exec "$APP" "$@"
