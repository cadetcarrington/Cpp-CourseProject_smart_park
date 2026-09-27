#pragma once
#include "core/service/AnalyticsEngine.h"

#include <functional>
#include <optional>
#include <string>

namespace smartpark{

struct RemoteAnalystConfig{
    std::string endpoint;              // OpenAI 兼容 chat/completions 地址
    std::string model{"glm-4-flash"};
    std::string apiKeyEnvVar{"SMARTPARK_ANALYST_API_KEY"};
    std::string systemPrompt{
        "你是停车场运营分析师。基于输入的聚合运营指标，用中文给出不超过三句的结论"
        "和不超过三条的建议，只输出 JSON："
        "{\"summary\":\"...\",\"recommendations\":[\"...\"]}。"
        "指标为聚合统计数据，不包含车牌等个人信息。"};
};

// 远程数据分析接口（预留）：把聚合运营快照交给 LLM API 生成自然语言结论，
// 输出与 AnalyticsEngine 的 AnalysisReport 同构，两者可互换。
// 传输层未注入时 analyze 返回 nullopt；网络层（P1）落地后注入 Transport
// （HTTP POST 实现）即可启用，核心代码无需改动。
class RemoteAnalystClient{
public:
    // transport(url, apiKey, requestJson) -> 响应体；返回 nullopt 表示请求失败。
    using Transport = std::function<std::optional<std::string>(
        const std::string &url, const std::string &apiKey,
        const std::string &requestJson)>;

    explicit RemoteAnalystClient(RemoteAnalystConfig config = {});

    void setTransport(Transport transport);
    bool configured() const noexcept;   // endpoint 已配置

    std::optional<AnalysisReport> analyze(
        const OperationalSnapshot &snapshot,
        ParkingRecord::TimePoint now = ParkingRecord::Clock::now()) const;

    static std::string buildRequestJson(const RemoteAnalystConfig &config,
                                        const OperationalSnapshot &snapshot);
    static std::optional<AnalysisReport> parseResponse(const std::string &responseJson,
                                                       const std::string &model,
                                                       ParkingRecord::TimePoint now);

private:
    static std::string buildMetricsJson(const OperationalSnapshot &snapshot);

    RemoteAnalystConfig config_;
    Transport transport_;
};

} // namespace smartpark
