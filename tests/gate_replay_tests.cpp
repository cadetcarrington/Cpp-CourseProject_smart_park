// Gate 断线补报「部分确认」的专项测试。
//
// 覆盖真实链路里必须成立的四件事：
// 1. 一批 5 条只拿回前 3 条结论时，只出队这 3 条；剩下 2 条跨实例（进程重启）
//    仍在，下一轮只重报这 2 条，已确认的不会被重复上报。
// 2. 队头就没有结论（响应被截断 / 格式异常）时一条都不出队，本地队列原样保留；
//    被服务端判定无法追溯的事件（skipped）有结论，仍然出队并报出丢弃明细，
//    否则队头一条被拒事件会永久堵死队列。
// 3. 兼容不返回 results 的旧响应：三个计数之和等于提交条数才整批出队。
// 4. 协议层：应答帧被截断时只报 NeedMore；配合进程内 mock 服务端「只发半帧
//    就断开」，客户端拿不到应答，一个事件都不会被误出队（失败安全）。
#include "OfflineQueue.h"
#include "network/Protocol.h"
#include "network/TcpClient.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>

#include <iostream>
#include <memory>
#include <string>

namespace{
using smartpark::gate::OfflineQueue;

int failures = 0;
void check(bool condition, const std::string &what){
    std::cout << (condition ? "  ok  " : "  FAIL ") << what << '\n';
    if (!condition){
        ++failures;
    }
}

QJsonObject offlineEvent(const QString &kind, const QString &plate, qint64 ts){
    return QJsonObject{{QStringLiteral("kind"), kind},
                       {QStringLiteral("plate"), plate},
                       {QStringLiteral("vehicleType"), QStringLiteral("car")},
                       {QStringLiteral("ts"), ts}};
}

QJsonObject verdict(const QString &plate, const QString &kind, bool ok,
                    const QString &error = QString()){
    QJsonObject entry{{QStringLiteral("plate"), plate},
                      {QStringLiteral("kind"), kind},
                      {QStringLiteral("ok"), ok}};
    if (!error.isEmpty()){
        entry.insert(QStringLiteral("error"), error);
    }
    return entry;
}

QJsonObject replayResponse(const QJsonArray &results, int applied, int duplicate,
                           int skipped){
    return QJsonObject{{QStringLiteral("applied"), applied},
                       {QStringLiteral("duplicate"), duplicate},
                       {QStringLiteral("skipped"), skipped},
                       {QStringLiteral("results"), results}};
}

QStringList platesOf(const QJsonArray &events){
    QStringList plates;
    for (const QJsonValue &item : events){
        plates << item.toObject().value(QStringLiteral("plate")).toString();
    }
    return plates;
}

// 1 + 2 + 3：真实 OfflineQueue 上的多轮部分确认。
void runQueueRounds(const QString &directory){
    const QString queuePath = directory + QStringLiteral("/gate-queue.jsonl");
    const QStringList plates{QStringLiteral("晋G10001"), QStringLiteral("晋G10002"),
                             QStringLiteral("晋G10003"), QStringLiteral("晋G10004"),
                             QStringLiteral("晋G10005")};
    const QStringList kinds{QStringLiteral("enter"), QStringLiteral("exit"),
                            QStringLiteral("enter"), QStringLiteral("exit"),
                            QStringLiteral("enter")};
    OfflineQueue queue(queuePath);
    const qint64 base = QDateTime::currentMSecsSinceEpoch() - 60 * 60 * 1000LL;
    for (int i = 0; i < plates.size(); ++i){
        check(queue.append(offlineEvent(kinds.at(i), plates.at(i),
                                        base + i * 1000LL)),
              QStringLiteral("offline event %1 is written to the queue")
                  .arg(plates.at(i)).toStdString());
    }
    check(queue.size() == 5 && queue.pending().size() == 5,
          "all five offline events wait in the queue");

    // 第一轮：服务端只对前 3 条给出结论，第 4 条起响应被截断。
    const QJsonObject firstResponse = replayResponse(
        QJsonArray{verdict(plates.at(0), kinds.at(0), true),
                   verdict(plates.at(1), kinds.at(1), true),
                   verdict(plates.at(2), kinds.at(2), false,
                           QStringLiteral("追溯入场失败（可能已在场或无车位）"))},
        2, 0, 1);
    const int firstPrefix = smartpark::gate::replayHandledPrefix(firstResponse, 5);
    check(firstPrefix == 3, "only the acknowledged prefix is confirmed (3 of 5)");
    check(!smartpark::gate::replayBatchHandled(firstResponse, 5),
          "a truncated response is not a completed batch");
    check(queue.acknowledge(firstPrefix), "the confirmed prefix can be dequeued");
    check(queue.size() == 2, "the two unconfirmed events stay queued");
    const QStringList firstDropped =
        smartpark::gate::replaySkippedDetails(firstResponse, firstPrefix);
    check(firstDropped.size() == 1
              && firstDropped.first().contains(plates.at(2))
              && firstDropped.first().contains(QStringLiteral("追溯入场失败")),
          "a rejected event inside the prefix is reported with plate and reason");

    // 进程重启：未确认后缀必须仍在，且下一轮只重报它。
    OfflineQueue restarted(queuePath);
    check(restarted.size() == 2, "the unconfirmed suffix survives a restart");
    const QJsonArray secondBatch = restarted.pending();
    check(secondBatch.size() == 2
              && platesOf(secondBatch)
                     == QStringList{plates.at(3), plates.at(4)},
          "the next round re-sends only the unconfirmed suffix");

    // 第二轮：两条都有结论（一条成功、一条永远无法追溯）。
    const QJsonObject secondResponse = replayResponse(
        QJsonArray{verdict(plates.at(3), kinds.at(3), true),
                   verdict(plates.at(4), kinds.at(4), false,
                           QStringLiteral("追溯入场失败（可能已在场或无车位）"))},
        1, 0, 1);
    check(smartpark::gate::replayHandledPrefix(secondResponse, 2) == 2,
          "a fully answered batch confirms every event");
    check(smartpark::gate::replayBatchHandled(secondResponse, 2),
          "the second round completes the batch");
    check(restarted.acknowledge(2) && restarted.size() == 0
              && restarted.pending().isEmpty(),
          "the queue drains completely, skipped events do not clog it");
    check(smartpark::gate::replaySkippedDetails(secondResponse, 2).size() == 1,
          "the permanently rejected event is still reported when dequeued");

    // 队头没有结论：一条都不能出队。
    const QString headlessPath = directory + QStringLiteral("/headless.jsonl");
    OfflineQueue headless(headlessPath);
    for (int i = 0; i < 3; ++i){
        headless.append(offlineEvent(QStringLiteral("enter"),
                                     QStringLiteral("晋G1001%1").arg(i), base + i));
    }
    const QJsonObject headlessResponse = replayResponse(
        QJsonArray{QJsonObject{{QStringLiteral("plate"), QStringLiteral("晋G10010")},
                               {QStringLiteral("kind"), QStringLiteral("enter")}}},
        1, 0, 0);
    const int headlessPrefix =
        smartpark::gate::replayHandledPrefix(headlessResponse, 3);
    check(headlessPrefix == 0,
          "a verdict-less entry stops the prefix at zero");
    check(headless.size() == 3 && headless.pending().size() == 3,
          "nothing is dequeued when the response has no verdict");

    // 旧格式（没有 results）：只认计数之和。
    const QJsonObject legacy{{QStringLiteral("applied"), 3},
                             {QStringLiteral("duplicate"), 1},
                             {QStringLiteral("skipped"), 1}};
    check(smartpark::gate::replayHandledPrefix(legacy, 5) == 5,
          "a legacy response without results falls back to the counters");
    check(smartpark::gate::replayHandledPrefix(legacy, 4) == 0,
          "legacy counters that do not add up confirm nothing");
    check(smartpark::gate::replaySkippedDetails(legacy, 5).isEmpty(),
          "a legacy response has no per-event skipped details to report");
}

// 4：协议层截断 + 半帧应答的失败安全。
void runTruncatedFrame(const QString &directory){
    const QByteArray full = smartpark::protocol::encodeFrame(
        smartpark::protocol::makeResponse(
            QStringLiteral("c1"), true,
            replayResponse(QJsonArray{verdict(QStringLiteral("晋G10001"),
                                              QStringLiteral("enter"), true)},
                           1, 0, 0)));
    QJsonObject decoded;
    QByteArray partial = full.left(full.size() - 1);
    check(smartpark::protocol::tryDecodeFrame(partial, &decoded)
              == smartpark::protocol::FrameStatus::NeedMore
              && decoded.isEmpty(),
          "a truncated response frame only reports NeedMore");
    QByteArray whole = full;
    check(smartpark::protocol::tryDecodeFrame(whole, &decoded)
              == smartpark::protocol::FrameStatus::Ok
              && !decoded.isEmpty(),
          "the complete frame decodes into the response");

    QTcpServer mock;
    const bool listening = mock.listen(QHostAddress::LocalHost, 0);
    check(listening, "the mock replay server listens");
    if (!listening){
        return;
    }
    const auto answered = std::make_shared<bool>(false);
    QObject::connect(&mock, &QTcpServer::newConnection, [&mock, answered]{
        QTcpSocket *peer = mock.nextPendingConnection();
        QObject::connect(peer, &QTcpSocket::readyRead, [peer, answered]{
            if (*answered){
                return;
            }
            *answered = true;
            peer->readAll();   // 请求内容不是断言对象，只确认请求已到达。
            const QByteArray answer = smartpark::protocol::encodeFrame(
                smartpark::protocol::makeResponse(
                    QStringLiteral("c1"), true,
                    QJsonObject{{QStringLiteral("applied"), 1}}));
            // 只发送应答帧的前三分之一就断开：客户端永远凑不齐一帧。
            peer->write(answer.left(answer.size() / 3));
            peer->flush();
            peer->disconnectFromHost();
        });
    });

    smartpark::TcpClient client;
    check(client.connectToHost(QStringLiteral("127.0.0.1"), mock.serverPort()),
          "the gate client connects to the mock server");
    const QString guardedPath = directory + QStringLiteral("/guarded.jsonl");
    OfflineQueue guarded(guardedPath);
    const qint64 base = QDateTime::currentMSecsSinceEpoch() - 60 * 60 * 1000LL;
    for (int i = 0; i < 5; ++i){
        guarded.append(offlineEvent(QStringLiteral("enter"),
                                    QStringLiteral("晋G1002%1").arg(i), base + i));
    }
    const auto response = client.request(
        QStringLiteral("gate.replay"),
        QJsonObject{{QStringLiteral("events"), guarded.pending()}}, 400);
    check(!response.has_value(),
          "a half-written response frame never reaches the client as a response");

    // 与 apps/gate/main.cpp 相同的判定路径：没有应答就不出队。
    int handled = 0;
    if (response && response->value(QStringLiteral("ok")).toBool()){
        handled = smartpark::gate::replayHandledPrefix(
            response->value(QStringLiteral("payload")).toObject(), 5);
    }
    check(handled == 0 && guarded.size() == 5,
          "a missing response leaves every queued event in place");
}
} // namespace

int main(int argc, char **argv){
    QCoreApplication app(argc, argv);
    QTemporaryDir directory;
    if (!directory.isValid()){
        std::cerr << "FAIL: cannot create temp dir\n";
        return 1;
    }
    runQueueRounds(directory.path());
    runTruncatedFrame(directory.path());

    std::cout << (failures == 0 ? "GATE REPLAY TESTS: PASS"
                                : "GATE REPLAY TESTS: FAIL")
              << " (" << failures << " failure(s))\n";
    return failures == 0 ? 0 : 1;
}
