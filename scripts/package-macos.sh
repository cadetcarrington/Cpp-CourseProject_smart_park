#!/usr/bin/env bash
# 打包 macOS 管理端为可分发 DMG。
#
# 关键点：仓库位于 iCloud 同步的 ~/Documents 下，bird 守护进程会给 bundle
# 内新建的 framework 目录异步补挂 com.apple.FinderInfo，导致 codesign 报
# “resource fork / Finder information detritus not allowed”，且清理后数秒
# 内即复发。因此签名、校验与 DMG 组装全部在 iCloud 之外的临时目录完成，
# 只把最终 DMG 拷回 build/pack/。
#
# 用法：scripts/package-macos.sh   （SKIP_BUILD=1 跳过重新编译）
set -euo pipefail

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "macOS packaging requires macOS." >&2
    exit 1
fi

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build/macos-release}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
QT_PREFIX="${QT_PREFIX:-}"

if [[ -z "$QT_PREFIX" ]]; then
    if command -v qmake6 >/dev/null 2>&1; then
        QT_PREFIX="$(qmake6 -query QT_INSTALL_PREFIX)"
    elif command -v qmake >/dev/null 2>&1; then
        QT_PREFIX="$(qmake -query QT_INSTALL_PREFIX)"
    elif [[ -d "$HOME/Qt/6.8.3/macos" ]]; then
        QT_PREFIX="$HOME/Qt/6.8.3/macos"
    elif command -v brew >/dev/null 2>&1; then
        QT_PREFIX="$(brew --prefix qt)"
    fi
fi
MACDEPLOYQT="$QT_PREFIX/bin/macdeployqt"

SRC_APP="$BUILD_DIR/apps/macos/smartpark_admin_macos.app"
OUT_DIR="$ROOT/build/pack"

echo "== [1/6] 构建 =="
if [[ "${SKIP_BUILD:-0}" != "1" ]]; then
    BUILD_DIR="$BUILD_DIR" BUILD_TYPE="$BUILD_TYPE" "$ROOT/scripts/build-macos.sh"
fi
[[ -d "$SRC_APP" ]] || { echo "App bundle not found: $SRC_APP（先去掉 SKIP_BUILD）" >&2; exit 1; }
[[ -x "$MACDEPLOYQT" ]] || { echo "macdeployqt not found under QT_PREFIX=$QT_PREFIX" >&2; exit 1; }

WORK="$(mktemp -d "${TMPDIR:-/tmp}/smartpark-pack.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
APP="$WORK/SmartPark Admin.app"

echo "== [2/6] 复制到非 iCloud 工作区：$WORK =="
cp -R "$SRC_APP" "$APP"
xattr -cr "$APP"

echo "== [3/6] macdeployqt 收集 Qt frameworks =="
# 应用只用 QSQLITE；其余驱动插件引用本机 Postgres/libiodbc 等外部库，
# 分发包必须裁掉。macdeployqt 收尾自带的 codesign 遇脏属性可能失败，但
# frameworks 届时已拷贝完成，真正的质量门在 [4/6] 的签名校验。
if ! "$MACDEPLOYQT" "$APP" -always-overwrite; then
    echo "warn: macdeployqt 以非零码退出（可选驱动缺失/内部签名告警），继续" >&2
fi
DRIVERS_DIR="$APP/Contents/PlugIns/sqldrivers"
if [[ -d "$DRIVERS_DIR" ]]; then
    for drv in "$DRIVERS_DIR"/libqsql*.dylib; do
        name="$(basename "$drv")"
        [[ "$name" == "libqsqlite.dylib" ]] || rm "$drv"
    done
fi
xattr -cr "$APP"

echo "== [4/6] ad-hoc 签名与校验 =="
codesign --force --deep --sign - "$APP"
codesign --verify --deep --strict --verbose=2 "$APP"

echo "== [5/6] 冒烟启动（6 秒） =="
"$APP/Contents/MacOS/smartpark_admin_macos" > "$WORK/smoke.log" 2>&1 & SMOKE_PID=$!
sleep 6
if kill -0 "$SMOKE_PID" 2>/dev/null; then
    kill "$SMOKE_PID" || true
    echo "smoke: 进程存活 6 秒，视为启动成功"
else
    echo "error: 打包后应用启动即退出，日志：" >&2
    cat "$WORK/smoke.log" >&2
    exit 1
fi

rm -f "$WORK/smoke.log"

echo "== [6/6] 组装 DMG =="
VERSION="$(/usr/libexec/PlistBuddy -c 'Print CFBundleShortVersionString' \
    "$APP/Contents/Info.plist")"
COMMIT="$(git -C "$ROOT" rev-parse --short HEAD)"
ARCH="$(lipo -info "$APP/Contents/MacOS/smartpark_admin_macos" | awk '{print $NF}')"

cat > "$WORK/使用说明.txt" <<EOF
SmartPark Admin for macOS（v${VERSION}，${ARCH}）
==================================================

本镜像包含 SmartPark 原生 macOS 管理端（AppKit + Qt 6.8）。安装：
将「SmartPark Admin.app」拖入「应用程序」或任意目录。

首次打开
--------
应用为 ad-hoc 签名、未做公证。若被 Gatekeeper 拦截（“无法验证开发者”）：
  • 右键点击 App →「打开」→ 再点「打开」；或
  • 终端执行：xattr -cr "/Applications/SmartPark Admin.app"

运行模式
--------
• 本地模式：直接启动，打开/新建本地 SQLite 停车场库（可配对自定义布局图纸）。
• 远程模式：登录界面选择远程并填服务器地址（默认端口 9527）；服务端部署见
  仓库 README 与 docs/deploy-server.md（REST/WS 网关端口默认 18080/18081）。

车牌识别（可选）
--------------
本地识别依赖外部 Python 环境（~/.smartpark/lpr、~/.smartpark/ocr），缺失时
自动降级为低置信度/人工确认流程，不影响其余功能。

系统要求：macOS 12 及以上，Apple Silicon（arm64）。
构建来源：Release 构建，源码 main@${COMMIT}。
EOF

mkdir -p "$OUT_DIR"
DMG="$OUT_DIR/SmartParkAdmin-${VERSION}-macOS-${ARCH}.dmg"
rm -f "$DMG"
hdiutil create -volname "SmartPark Admin" -srcfolder "$WORK" \
    -ov -format UDZO "$DMG" >/dev/null

echo "Package OK: $DMG"
shasum -a 256 "$DMG"
