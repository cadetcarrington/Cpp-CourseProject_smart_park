#pragma once
#include <QtGlobal>

#include <optional>
#include <string>

class QString;

// macOS 原生系统桥接：菜单栏余位、通知中心、中文语音、PDF 导出、
// NSURLSession 同步 POST（填入 RemoteAnalystClient 的 Transport 接缝）。
// 仅 smartpark_admin 主目标定义 SMARTPARK_MACOS_NATIVE 并编译
// MacSystemBridge.mm；其他目标使用下方内联空实现。
namespace macbridge{

#if defined(Q_OS_MAC) && defined(SMARTPARK_MACOS_NATIVE)
// 菜单栏常驻图标（NSStatusItem），常驻文本如“SmartPark 余位 42”。
void setMenuBarStatus(const QString &text);

// 请求通知授权（幂等，弹窗异步返回）并发送一条本地通知。
void postNotification(const QString &title, const QString &body);

// 中文语音播报（AVSpeechSynthesizer，zh-CN，无中文语音时用默认语音）。
void speakChinese(const QString &text);

// 把标题+正文渲染为 A4 PDF 写入 path（PDFKit），成功返回 true。
bool exportTextToPdf(const QString &title, const QString &body,
                     const QString &pdfPath);

// 在 Finder 中定位文件（NSWorkspace）。
bool revealInFinder(const QString &path);

// NSURLSession 同步 POST JSON；成功返回响应体，失败/超时返回 nullopt。
// 阻塞至超时，调用方应在非主线程使用。
std::optional<std::string> httpPostJson(const std::string &url,
                                        const std::string &apiKey,
                                        const std::string &requestJson,
                                        int timeoutSeconds);
#else
inline void setMenuBarStatus(const QString &) {}
inline void postNotification(const QString &, const QString &) {}
inline void speakChinese(const QString &) {}
inline bool exportTextToPdf(const QString &, const QString &, const QString &){
    return false;
}
inline bool revealInFinder(const QString &){
    return false;
}
inline std::optional<std::string> httpPostJson(const std::string &,
                                               const std::string &,
                                               const std::string &, int){
    return std::nullopt;
}
#endif

} // namespace macbridge
