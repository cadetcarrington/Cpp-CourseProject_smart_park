#pragma once

#include <string>
#include <vector>

// Qt 无关的原生 macOS 窗口通知横幅：毛玻璃 NSPanel（NSVisualEffectView +
// PingFang 字体），出现在父窗口右上角内侧，数秒后自动淡出，点击可提前关闭。
// 文本一律 UTF-8。非 macOS 平台不编译本模块。
namespace macnotify{

// parentNSWindow 为父窗口的 NSWindow*（仅用于定位，可为 nullptr，此时贴主屏）。
void showExitBanner(const void *parentNSWindow, const char *title,
                    const std::vector<std::string> &lines);

} // namespace macnotify
