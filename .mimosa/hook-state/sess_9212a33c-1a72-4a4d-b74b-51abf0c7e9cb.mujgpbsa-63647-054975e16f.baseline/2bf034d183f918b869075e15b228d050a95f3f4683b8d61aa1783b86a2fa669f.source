#include "core/service/RemoteAnalystClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cstdlib>

namespace smartpark{
namespace{
std::string qstringToString(const QString &text){
    return text.toStdString();
}
} // namespace

RemoteAnalystClient::RemoteAnalystClient(RemoteAnalystConfig config)
    : config_(std::move(config)){
}

void RemoteAnalystClient::setTransport(Transport transport){
    transport_ = std::move(transport);
}

bool RemoteAnalystClient::configured() const noexcept{
    return !config_.endpoint.empty();
}

std::string RemoteAnalystClient::buildMetricsJson(const OperationalSnapshot &snapshot){
    QJsonObject metrics;
    metrics.insert(QStringLiteral("capacity"), snapshot.capacity);
    metrics.insert(QStringLiteral("occupied"), snapshot.occupied);
    metrics.insert(QStringLiteral("occupancyRate"),
                   QString::number(snapshot.occupancyRate, 'f', 3).toDouble());
    metrics.insert(QStringLiteral("closedRecords"), snapshot.closedRecords);
    metrics.insert(QStringLiteral("totalRevenue"),
                   QString::number(snapshot.totalRevenue, 'f', 2).toDouble());
    metrics.insert(QStringLiteral("averageDurationHours"),
                   QString::number(snapshot.averageDurationHours, 'f', 2).toDouble());
    metrics.insert(QStringLiteral("peakEntryHourUtc"), snapshot.peakEntryHour);
    metrics.insert(QStringLiteral("openBookings"), snapshot.openBookings);
    metrics.insert(QStringLiteral("noShowBookings"), snapshot.noShowBookings);
    metrics.insert(QStringLiteral("reservationForfeited"),
                   QString::number(snapshot.reservationForfeited, 'f', 2).toDouble());
    QJsonArray zones;
    for (const ZoneLoadStat &zone : snapshot.zones){
        QJsonObject item;
        item.insert(QStringLiteral("zone"), QString::fromStdString(zone.zone));
        item.insert(QStringLiteral("total"), zone.total);
        item.insert(QStringLiteral("load"),
                    QString::number(zone.load, 'f', 3).toDouble());
        zones.append(item);
    }
    metrics.insert(QStringLiteral("zones"), zones);
    QJsonArray series;
    for (const OccupancySample &sample : snapshot.occupancySeries){
        series.append(QString::number(sample.occupancyRate, 'f', 3).toDouble());
    }
    metrics.insert(QStringLiteral("occupancySeriesLast72h"), series);
    return QJsonDocument(metrics).toJson(QJsonDocument::Compact).toStdString();
}

std::string RemoteAnalystClient::buildRequestJson(
    const RemoteAnalystConfig &config, const OperationalSnapshot &snapshot){
    QJsonObject body;
    body.insert(QStringLiteral("model"), QString::fromStdString(config.model));
    body.insert(QStringLiteral("temperature"), 0.2);
    QJsonArray messages;
    QJsonObject system;
    system.insert(QStringLiteral("role"), QStringLiteral("system"));
    system.insert(QStringLiteral("content"),
                  QString::fromStdString(config.systemPrompt));
    messages.append(system);
    QJsonObject user;
    user.insert(QStringLiteral("role"), QStringLiteral("user"));
    user.insert(QStringLiteral("content"),
                QStringLiteral("运营指标 JSON：")
                    + QString::fromStdString(buildMetricsJson(snapshot)));
    messages.append(user);
    body.insert(QStringLiteral("messages"), messages);
    return QJsonDocument(body).toJson(QJsonDocument::Compact).toStdString();
}

std::optional<AnalysisReport> RemoteAnalystClient::parseResponse(
    const std::string &responseJson, const std::string &model,
    ParkingRecord::TimePoint now){
    const QJsonDocument document = QJsonDocument::fromJson(
        QByteArray::fromStdString(responseJson));
    if (!document.isObject()){
        return std::nullopt;
    }
    const QJsonArray choices = document.object()
                                   .value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty()){
        return std::nullopt;
    }
    const QString content = choices.first().toObject()
                                .value(QStringLiteral("message"))
                                .toObject()
                                .value(QStringLiteral("content")).toString();
    if (content.isEmpty()){
        return std::nullopt;
    }
    AnalysisReport report;
    report.model = model;
    report.generatedAt = now;
    // 模型输出优先按 JSON 解析；解析失败时整段文本作为结论。
    const QJsonDocument conclusion = QJsonDocument::fromJson(content.toUtf8());
    if (conclusion.isObject()){
        report.summary = qstringToString(
            conclusion.object().value(QStringLiteral("summary")).toString());
        const QJsonArray recommendations =
            conclusion.object().value(QStringLiteral("recommendations")).toArray();
        for (const QJsonValue &item : recommendations){
            if (item.isString() && !item.toString().isEmpty()){
                report.recommendations.push_back(qstringToString(item.toString()));
            }
        }
    } else{
        report.summary = qstringToString(content);
    }
    if (report.summary.empty()){
        return std::nullopt;
    }
    report.findings.push_back({AnalysisFinding::Category::Forecast,
                               "远程模型结论",
                               report.summary});
    return report;
}

std::optional<AnalysisReport> RemoteAnalystClient::analyze(
    const OperationalSnapshot &snapshot, ParkingRecord::TimePoint now) const{
    if (!configured() || !transport_){
        return std::nullopt;
    }
    const char *apiKey = config_.apiKeyEnvVar.empty()
        ? ""
        : std::getenv(config_.apiKeyEnvVar.c_str());
    const auto response = transport_(config_.endpoint, apiKey != nullptr ? apiKey : "",
                                     buildRequestJson(config_, snapshot));
    if (!response){
        return std::nullopt;
    }
    return parseResponse(*response, config_.model, now);
}

} // namespace smartpark
