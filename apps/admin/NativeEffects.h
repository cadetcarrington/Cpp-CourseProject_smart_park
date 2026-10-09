#pragma once

#include <QtGlobal>
#include <QStringList>
#include <QVector>

class QWidget;
class QComboBox;

// macOS 原生毛玻璃封装：NSVisualEffectView 对窗口背后内容做高斯模糊。
// 调用前窗口需设置 WA_TranslucentBackground；非 macOS 平台返回 false，
// 调用方回退到 Theme::auroraBackdrop()。

namespace smartpark_ui{
    #if defined(Q_OS_MAC) && defined(SMARTPARK_HAS_NATIVE_VIBRANCY)
    // 仅 smartpark_admin 主目标编译 NativeEffects.mm 并定义该宏。
    bool applyNativeVibrancy(QWidget *window, bool darkAppearance = true);
    bool applyNativeSidebarVibrancy(QWidget *sidebar);
    bool applyNativeHeaderVibrancy(QWidget *header);
    bool applyNativeContentVibrancy(QWidget *content);

    // 在 QComboBox 上叠加 macOS 原生 NSPopUpButton（真正原生下拉控件）。
    bool applyNativePopupButton(QComboBox *combo);

    // 用原生 NSScrollView + NSTableView 渲染只读多列表格。
    // host 为占位 QWidget，columns 为表头；成功返回非空句柄，失败返回 nullptr。
    // 句柄仅供 setNativeRecordTableRows 使用，生命周期跟随 host，无需手动释放。
    void *applyNativeRecordTable(QWidget *host, const QStringList &columns);
    void setNativeRecordTableRows(void *handle, const QVector<QStringList> &rows);

    // 车辆离场原生窗口通知：毛玻璃 NSPanel 横幅（PingFang 字体），显示在
    // window 右上角内侧，数秒后自动淡出，点击可提前关闭；window 可为 nullptr。
    void showExitNotification(QWidget *window, const QString &title,
                              const QStringList &lines);

    #else
    inline bool applyNativeVibrancy(QWidget *, bool = true){
        return false;
    }
    inline bool applyNativeSidebarVibrancy(QWidget *){
        return false;
    }
    inline bool applyNativeHeaderVibrancy(QWidget *){
        return false;
    }
    inline bool applyNativeContentVibrancy(QWidget *){
        return false;
    }
    inline bool applyNativePopupButton(QComboBox *){
        return false;
    }
    inline void *applyNativeRecordTable(QWidget *, const QStringList &){
        return nullptr;
    }
    inline void setNativeRecordTableRows(void *, const QVector<QStringList> &){
    }
    inline void showExitNotification(QWidget *, const QString &, const QStringList &){
    }
    #endif
} // namespace smartpark_ui
