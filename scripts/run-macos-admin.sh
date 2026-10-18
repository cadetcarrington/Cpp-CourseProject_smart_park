#!/usr/bin/env bash
# 本地启动 macOS 管理端，并带上 --db / --layout。
#
# 为什么不用 `open --args`：本机 macOS 上 open 不会把 --args 交给 app
# （用一个最小 .app 验证过：进程收到 0 个参数）。参数丢失后 app 会退回默认库
# ~/Library/Application Support/smartpark/smartpark.db，而默认库通常是旧布局
# 建的，于是弹「布局不一致」拒绝开窗口——看起来就是「启动不了」。
# 直接执行 bundle 里的可执行文件，参数一定生效，日志也直接打在终端上。
#
# 用法：
#   scripts/run-macos-admin.sh                       # 默认用 build/demo/grid-demo.db
#   scripts/run-macos-admin.sh <库路径> [布局文件]
#   DB_RESET=1 scripts/run-macos-admin.sh            # 库与布局不一致时删掉重建
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$ROOT/build/macos/apps/macos/smartpark_admin_macos.app/Contents/MacOS/smartpark_admin_macos"
DB="${1:-$ROOT/build/demo/grid-demo.db}"
LAYOUT="${2:-$ROOT/data/garage-3tower.txt}"
CHECK_LOG="${TMPDIR:-/tmp}/smartpark-check.log"

if [ ! -x "$BIN" ]; then
    echo "✗ 找不到 app：$BIN" >&2
    echo "  先构建：scripts/build-macos.sh" >&2
    exit 1
fi
if [ ! -f "$LAYOUT" ]; then
    echo "✗ 布局文件不存在：$LAYOUT" >&2
    exit 1
fi
mkdir -p "$(dirname "$DB")"

# 参数只在「新起进程」时有效，先收掉已在跑的实例，避免你对着旧窗口纳闷。
# 只按进程名匹配（-x）：-f 会匹配整条命令行，若调用方命令行里也出现过这个
# 字符串，pkill 会把调用方自己一起杀掉。
if pkill -x smartpark_admin_macos >/dev/null 2>&1; then
    echo "· 已关闭在跑的管理端实例"
    sleep 0.5
fi

# 自检：这份库配这份布局能不能开。不一致是本地最常见的失败原因，提前说清楚。
if ! "$BIN" --check-db --db "$DB" --layout "$LAYOUT" >"$CHECK_LOG" 2>&1; then
    echo "✗ 这份库打不开："
    sed 's/^/    /' "$CHECK_LOG"
    echo "  库是用别的布局建的（布局每次调整，旧库就作废一次）。"
    if [ "${DB_RESET:-0}" = "1" ]; then
        echo "· DB_RESET=1：删除 $DB 并用当前布局重建"
        rm -f "$DB" "$DB-wal" "$DB-shm"
    else
        echo "  换个库路径，或者 DB_RESET=1 重跑（会清掉该库里的车位与记录）。"
        exit 1
    fi
fi

echo "→ 库：$DB"
echo "→ 布局：$LAYOUT"
exec "$BIN" --db "$DB" --layout "$LAYOUT"
