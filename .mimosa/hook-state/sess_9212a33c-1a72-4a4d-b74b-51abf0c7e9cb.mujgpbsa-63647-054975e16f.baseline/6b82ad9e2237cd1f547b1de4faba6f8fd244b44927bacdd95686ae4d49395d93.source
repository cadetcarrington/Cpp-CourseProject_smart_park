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

# macOS 15 已移除 AGL.framework 的实际二进制，但部分 Qt 6 官方包
# 仍会通过 CMake 注入 `-framework AGL`。这里仅在缺失时生成本地兼容 stub。
prepare_macos_agl_stub() {
    [[ "$(uname -s)" == "Darwin" ]] || return 0
    [[ -f /System/Library/Frameworks/AGL.framework/Versions/A/AGL ]] && return 0

    local stub_root="$BUILD_DIR/macos-agl-stub"
    local framework="$stub_root/AGL.framework"
    local version_dir="$framework/Versions/A"
    mkdir -p "$version_dir/Headers" "$version_dir/Resources"

    if [[ ! -f "$version_dir/AGL" ]]; then
        local source_file="$stub_root/agl_stub.c"
        printf '%s\n' 'void __smartpark_agl_stub(void) {}' > "$source_file"
        cc -dynamiclib \
            -Wl,-install_name,@rpath/AGL.framework/Versions/A/AGL \
            -o "$version_dir/AGL" "$source_file"
        ln -sfn A "$framework/Versions/Current"
        ln -sfn Versions/Current/AGL "$framework/AGL"
        ln -sfn Versions/Current/Headers "$framework/Headers"
        ln -sfn Versions/Current/Resources "$framework/Resources"
    fi

    ARGS+=("-DWrapOpenGL_AGL=$framework")
    echo "Created AGL compatibility stub: $framework"
}
prepare_macos_agl_stub

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
