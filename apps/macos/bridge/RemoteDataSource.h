#pragma once

#include "ParkingDataSource.h"
#include "network/ServerSession.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

#include <functional>
#include <memory>

namespace smartpark{

// 远程模式数据源：通过协议 v1 连接服务端。
//
// 数据来源是 admin.snapshot（全量快照）——服务端把它渲染车位图所需的
// 布局几何、车位明细和概览计数一次给出，因此本机不需要第二个 ParkingService。
// 快照到手后解析成与本地模式同样的 ParkingLayout / ParkingSpot，界面代码
// 无需区分模式。
//
// 刷新策略：收到任一广播事件（parking.entered/exited、reservation.*、
// gate.replayed）后重新拉一次快照。服务端事件带的是增量语义，而快照本身
// 很小，重新拉取比在客户端复刻一遍状态机更不容易出错；代价是事件密集时
// 请求偏多，对单场演示规模可以接受。
//
// 协议里没有对应 action 的能力（预约、停车记录、布局编辑、车型更正、
// 应急入场）由 capabilities() 声明为 false，界面据此隐藏入口。
class RemoteDataSource : public QObject, public ParkingDataSource{
    Q_OBJECT

public:
    explicit RemoteDataSource(QObject *parent = nullptr);
    ~RemoteDataSource() override;

    // 建立会话并登录；结果通过 onConnectionChanged 回调。
    void start(const QString &host, quint16 port, const QString &user,
               const QString &password);
    void stop();

    // 连接/登录状态变化，以及每次快照刷新后触发（用于驱动界面重绘）。
    std::function<void(bool online, const QString &detail)> onConnectionChanged;
    std::function<void()> onSnapshotRefreshed;

    bool online() const;
    const QString &host() const noexcept { return host_; }
    quint16 port() const noexcept { return port_; }
    // 快照生成时间（服务端本地时钟），界面用于说明数据新鲜度。
    qint64 snapshotGeneratedAtMs() const noexcept { return generatedAtMs_; }
    // 服务端 analytics.report 的摘要与结论（远程模式没有本地洞察）。
    const QJsonObject &analyticsReport() const noexcept { return analytics_; }
    // 近 7 日已结算收入（快照 dailyRevenue，服务端已按同样口径算好）。
    double dailyRevenueTotal() const noexcept;
    // 逐日明细：{date: yyyy-MM-dd, fee}，从早到晚固定 7 项。
    QJsonArray dailyRevenueSeries() const noexcept { return dailyRevenue_; }

    // ---- ParkingDataSource ----
    Capabilities capabilities() const override;
    bool ready() const noexcept override;
    const std::string &lastError() const noexcept override;

    const ParkingLayout &layout() const noexcept override { return layout_; }
    const std::vector<ParkingSpot> &spots() const noexcept override { return spots_; }
    const std::vector<ParkingRecord> &records() const noexcept override { return records_; }
    const std::vector<Booking> &bookings() const noexcept override { return bookings_; }
    ParkingInsights insights() const noexcept override { return ParkingInsights{}; }
    double totalRevenue() const noexcept override { return dailyRevenueTotal(); }

    std::optional<AllocationResult> enterVehicle(
        const std::string &plate, VehicleType type) override;
    std::optional<AllocationResult> emergencyEnter(
        const std::string &, VehicleType) override { return std::nullopt; }
    std::optional<ParkingRecord> leaveVehicle(const std::string &plate) override;
    bool updateVehicleType(const std::string &, VehicleType) override { return false; }

    const std::string &layoutDescription() const noexcept override { return layoutText_; }
    bool memoryOnly() const noexcept override { return false; }

private:
    void requestSnapshot();
    void requestAnalytics();
    // ServerSession 是异步的，而 macOS 界面的写操作是同步取结果的（按一下
    // 按钮就要知道成功还是失败）。这里用局部事件循环等应答，与 Gate 端
    // TcpClient::request 的做法一致；超时按失败返回，绝不假装成功。
    bool awaitRequest(const QString &action, const QJsonObject &payload,
                      QJsonObject *result, QString *error, int timeoutMs = 5000);
    bool applySnapshot(const QJsonObject &snapshot, std::string *error);
    void setError(const QString &text);

    std::unique_ptr<ServerSession> session_;
    ParkingLayout layout_;
    std::vector<ParkingSpot> spots_;
    std::vector<ParkingRecord> records_;   // 远程模式不提供，恒为空
    std::vector<Booking> bookings_;        // 同上
    QJsonObject analytics_;
    // 快照随带的 7 日收入。单独存一份：analytics.report 的应答会整体替换
    // analytics_，混在一起会在报告到达后把收入数据抹掉。
    QJsonArray dailyRevenue_;
    std::string layoutText_;               // 快照只有几何，没有描述文本
    std::string lastError_;
    QString host_;
    quint16 port_{0};
    qint64 generatedAtMs_{0};
    bool snapshotReady_{false};
};

} // namespace smartpark
