#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build/qt}"
BUILD_TYPE="${BUILD_TYPE:-Debug}"
GENERATOR="${GENERATOR:-Ninja}"
CMAKE_BIN="${CMAKE_BIN:-cmake}"
QT_PREFIX="${QT_PREFIX:-}"

if [[ -z "$QT_PREFIX" ]]; then
    if [[ -n "${CONDA_PREFIX:-}" ]]; then
        QT_PREFIX="$CONDA_PREFIX"
    elif command -v qmake6 >/dev/null 2>&1; then
        QT_PREFIX="$(dirname "$(dirname "$(command -v qmake6)")")"
    elif command -v qmake >/dev/null 2>&1; then
        QT_PREFIX="$(dirname "$(dirname "$(command -v qmake)")")"
    elif [[ "$(uname -s)" == "Darwin" ]] && command -v brew >/dev/null 2>&1; then
        QT_PREFIX="$(brew --prefix qt)"
    fi
fi

ARGS=(
    -S "$ROOT"
    -B "$BUILD_DIR"
    -G "$GENERATOR"
    "-DCMAKE_BUILD_TYPE=$BUILD_TYPE"
    -DSMARTPARK_BUILD_ADMIN=ON
    -DBUILD_TESTING=ON
)
if [[ -n "$QT_PREFIX" ]]; then
    ARGS+=("-DCMAKE_PREFIX_PATH=$QT_PREFIX")
    echo "Using Qt prefix: $QT_PREFIX"
fi

"$CMAKE_BIN" "${ARGS[@]}"
"$CMAKE_BIN" --build "$BUILD_DIR" --parallel

if [[ "$(uname -s)" == "Darwin" ]]; then
    APP="$BUILD_DIR/apps/admin/smartpark_admin.app/Contents/MacOS/smartpark_admin"
else
    APP="$BUILD_DIR/apps/admin/smartpark_admin"
fi

if [[ -x "$APP" ]]; then
    echo "Build OK: $APP"
else
    echo "Error: built executable not found: $APP" >&2
    exit 1
fi
