#!/bin/sh
# 车牌识别脚本的测试替身（供 tests/e2e_rest.sh 使用）。
#
# 用法：smartpark_server --lpr-command "sh <repo>/tests/lpr_stub.sh %1"
# 服务端会把 %1 换成临时图片路径，通过 /bin/sh -c 执行。
#
# 行为：
#   - 图片内容含 NOPLATE -> 打印 "No license plate detected" 并以 1 退出
#     （对应真实脚本「画面里没有车牌」的情况）；
#   - 其它 -> 打印一行识别结果 JSON；
#   - 两种情况都先 sleep 2：模拟真实脚本的耗时（s1 上约 13 秒），
#     好让「识别期间服务端是否被阻塞」可测。
set -u
image="${1:-}"
sleep 2
if [ -z "$image" ] || [ ! -f "$image" ]; then
    echo "image file not found" >&2
    exit 2
fi
if grep -q NOPLATE "$image" 2>/dev/null; then
    echo "No license plate detected"
    exit 1
fi
printf '{"plate":"京A88888","confidence":0.99,"source":"stub"}\n'
