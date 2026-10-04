#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace smartpark{
namespace gate{

// 离线事件队列：断线期间的入场/离场事件先落盘（JSONL），重连后补报。
class OfflineQueue{
public:
    explicit OfflineQueue(QString filePath);

    bool append(const QJsonObject &event);
    QJsonArray pending(int limit = 500) const;
    bool acknowledge(int count);
    int size() const;

private:
    QString filePath_;
    int cachedCount_{0};
};

// 离线补报的确认判定。
//
// 服务端 gate.replay 对每条事件都给出结论，并返回 applied / duplicate /
// skipped 三个计数（三者之和 == 提交条数）。**skipped 必须计入**：像
// 「追溯入场失败（已在场或无车位）」这种事件永远不会成功，若只认可
// applied+duplicate，队头这条被拒事件就会让整批永远无法确认——队列再也
// 排不空，后面所有离线事件全部堵死在它后面。
//
// 返回「已获明确结论的前缀条数」，即可以安全出队的条数。
// gate.replay 逐条独立处理并按顺序回传 results；某条没有结论（响应被截断或
// 格式异常）时，它以及它之后的都必须留在本地队列下次重报。这样一批 500 条
// 若在第 300 条断线，前 299 条已生效的不会被重复上报；没有 results 的旧响应
// 则退回按三个计数之和判定。
int replayHandledPrefix(const QJsonObject &result, int submitted);

// 整批都被处理完（前缀 == 提交条数）。
bool replayBatchHandled(const QJsonObject &result, int submitted);

// 前 limit 条里被丢弃的事件说明（车牌 + 原因），用于如实告知操作员。
QStringList replaySkippedDetails(const QJsonObject &result, int limit);

} // namespace gate
} // namespace smartpark
