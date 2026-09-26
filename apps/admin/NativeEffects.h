#pragma once

#include <QtGlobal>

class QWidget;

// macOS 原生毛玻璃封装：NSVisualEffectView 对窗口背后内容做高斯模糊。
// 调用前窗口需设置 WA_TranslucentBackground；非 macOS 平台返回 false，
// 调用方回退到 Theme::auroraBackdrop()。
namespace smartpark_ui{
#if defined(Q_OS_MAC) && defined(SMARTPARK_HAS_NATIVE_VIBRANCY)
// 仅 smartpark_admin 主目标编译 NativeEffects.mm 并定义该宏。
bool applyNativeVibrancy(QWidget *window, bool darkAppearance = true);
#else
inline bool applyNativeVibrancy(QWidget *, bool = true){
    return false;
}
#endif
} // namespace smartpark_ui
