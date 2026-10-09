#pragma once
#include <QColor>
#include <QEvent>
#include <QPainter>
#include <QTimer>
#include <QVector>
#include <QWidget>

namespace smartpark_ui{

// 大屏弹幕 overlay：业务事件（入场/离场/预约/扣费）以横向滚动条幅掠过顶部。
// 仅读事件流做展示，不拦截鼠标；毛玻璃主题下以半透明胶囊绘制。
class DanmakuOverlay : public QWidget{
    Q_OBJECT

public:
    explicit DanmakuOverlay(QWidget *parent = nullptr)
        : QWidget(parent){
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_TranslucentBackground);
        setAutoFillBackground(false);
        connect(&tick_, &QTimer::timeout, this, [this]{ step(); });
        tick_.setInterval(33);   // ~30fps
        if (parent != nullptr){
            parent->installEventFilter(this);
            setGeometry(0, 0, parent->width(), kLaneHeight * kLaneCount + 6);
        }
        raise();
    }

    void push(const QString &text, const QColor &color){
        Banner banner;
        banner.text = text;
        banner.color = color;
        banner.x = width() + 12;
        banner.lane = static_cast<int>(banners_.size() % kLaneCount);
        banners_.append(banner);
        if (!tick_.isActive()){
            tick_.start();
        }
        raise();
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override{
        if (watched == parent() && event->type() == QEvent::Resize){
            setGeometry(0, 0, parentWidget()->width(),
                        kLaneHeight * kLaneCount + 6);
        }
        return QWidget::eventFilter(watched, event);
    }

    void paintEvent(QPaintEvent *) override{
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        for (const Banner &banner : banners_){
            QFontMetrics metrics(font());
            const int textWidth = metrics.horizontalAdvance(banner.text);
            const qreal padX = 14;
            const qreal pillWidth = textWidth + padX * 2;
            const qreal pillHeight = kLaneHeight - 12;
            const qreal y = banner.lane * kLaneHeight + 6;
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(15, 27, 45, 172));
            painter.drawRoundedRect(QRectF(banner.x, y, pillWidth, pillHeight),
                                    pillHeight / 2, pillHeight / 2);
            painter.setPen(QPen(banner.color));
            painter.drawText(QRectF(banner.x, y, pillWidth, pillHeight),
                             Qt::AlignCenter, banner.text);
        }
    }

private:
    struct Banner{
        QString text;
        QColor color;
        qreal x{0};
        int lane{0};
    };

    void step(){
        const qreal speed = 2.6;
        for (Banner &banner : banners_){
            banner.x -= speed;
        }
        QFontMetrics metrics(font());
        banners_.erase(std::remove_if(banners_.begin(), banners_.end(),
                                      [this, &metrics](Banner &banner){
            return banner.x + metrics.horizontalAdvance(banner.text) + 28 < 0;
        }), banners_.end());
        if (banners_.isEmpty()){
            tick_.stop();
        }
        update();
    }

    static constexpr int kLaneHeight = 38;
    static constexpr int kLaneCount = 3;
    QVector<Banner> banners_;
    QTimer tick_;
};

} // namespace smartpark_ui
