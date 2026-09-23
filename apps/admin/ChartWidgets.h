#pragma once

#include <QWidget>
#include <QVector>
#include <QString>
#include <QColor>
#include <QPainter>
#include <QPainterPath>
#include <QFontMetrics>
#include <QFont>
#include <algorithm>
#include <cmath>

// 轻量自绘图表，不依赖 Qt Charts 模块，避免额外依赖并保证在受限 Qt 安装下也能编译。
// 这些控件没有 Q_OBJECT，仅重写 paintEvent，因此不需要 MOC。

struct BarSlice{
    QString label;
    double value{0.0};
    QColor color{QColor(29, 78, 137)};
    QString valueText;
};

class BarChartWidget : public QWidget{
public:
    explicit BarChartWidget(QWidget *parent = nullptr)
        : QWidget(parent){
        setMinimumSize(240, 170);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    void setUnit(const QString &unit){
        unit_ = unit;
        update();
    }

    void setBars(const QVector<BarSlice> &bars){
        bars_ = bars;
        update();
    }

    QSize sizeHint() const override{
        return QSize(360, 220);
    }

protected:
    void paintEvent(QPaintEvent *) override{
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QRect area = rect().adjusted(10, 10, -10, -10);
        if (area.width() <= 40 || area.height() <= 24){
            return;
        }
        if (bars_.isEmpty()){
            painter.setPen(QColor(82, 96, 109));
            painter.drawText(area, Qt::AlignCenter, tr("暂无数据"));
            return;
        }

        QFont labelFont = font();
        labelFont.setPointSize(9);
        QFont valueFont = font();
        valueFont.setPointSize(9);
        valueFont.setBold(true);
        const QFontMetrics labelMetrics(labelFont);
        const QFontMetrics valueMetrics(valueFont);

        int maxLabelWidth = 24;
        for (const BarSlice &slice : bars_){
            maxLabelWidth = std::max(maxLabelWidth,
                                     labelMetrics.horizontalAdvance(slice.label));
        }
        const int labelColumn = std::min(maxLabelWidth + 18,
                                         std::max(48, area.width() / 4));

        int maxValueWidth = 24;
        for (const BarSlice &slice : bars_){
            const QString text = valueTextFor(slice);
            maxValueWidth = std::max(maxValueWidth, valueMetrics.horizontalAdvance(text));
        }
        const int valueColumn = maxValueWidth + 14;

        const int availableBarWidth = std::max(40, area.width() - labelColumn - valueColumn);
        double maxValue = 0.0;
        for (const BarSlice &slice : bars_){
            maxValue = std::max(maxValue, std::fabs(slice.value));
        }
        if (maxValue <= 0.0){
            maxValue = 1.0;
        }

        const int count = bars_.size();
        const int rowHeight = std::min(40, area.height() / count);
        const int barHeight = std::max(10, std::min(20, rowHeight - 12));
        const int top = area.top() + std::max(0, (area.height() - count * rowHeight) / 2);

        painter.setFont(labelFont);
        for (int i = 0; i < count; ++i){
            const BarSlice &slice = bars_[i];
            const int y = top + i * rowHeight;
            const int centerY = y + rowHeight / 2;

            const QRect labelRect(area.left(), centerY - labelMetrics.height() / 2,
                                  labelColumn - 8, labelMetrics.height());
            painter.setPen(QColor(51, 65, 85));
            painter.drawText(labelRect, Qt::AlignLeft | Qt::AlignVCenter,
                             labelMetrics.elidedText(slice.label, Qt::ElideRight,
                                                    labelRect.width()));

            const int barLeft = area.left() + labelColumn;
            const int barRight = area.right() - valueColumn;
            const int barWidth = std::max(4, barRight - barLeft);
            const int barTop = centerY - barHeight / 2;
            const double ratio = std::min(1.0, std::max(0.0,
                std::fabs(slice.value) / maxValue));
            const int fillWidth = std::max(2, static_cast<int>(std::round(ratio * barWidth)));

            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(226, 232, 240));
            painter.drawRoundedRect(QRect(barLeft, barTop, barWidth, barHeight),
                                    barHeight / 2.0, barHeight / 2.0);

            if (fillWidth > 0){
                QColor fillColor = slice.color;
                fillColor.setAlpha(235);
                painter.setBrush(fillColor);
                const int drawWidth = std::min(fillWidth, barWidth);
                painter.drawRoundedRect(QRect(barLeft, barTop, drawWidth, barHeight),
                                        barHeight / 2.0, barHeight / 2.0);
            }

            painter.setFont(valueFont);
            const QString valueText = valueTextFor(slice);
            const QRect valueRect(barRight + 8, centerY - valueMetrics.height() / 2,
                                  valueColumn - 8, valueMetrics.height());
            painter.setPen(QColor(71, 85, 105));
            painter.drawText(valueRect, Qt::AlignLeft | Qt::AlignVCenter, valueText);
        }
    }

private:
    QString valueTextFor(const BarSlice &slice) const{
        if (!slice.valueText.isEmpty()){
            return slice.valueText;
        }
        return QString::number(slice.value, 'f', 1) + unit_;
    }

    QVector<BarSlice> bars_;
    QString unit_{QStringLiteral("%")};
};

struct DonutSlice{
    QString label;
    double value{0.0};
    QColor color{QColor(29, 78, 137)};
};

class DonutChartWidget : public QWidget{
public:
    explicit DonutChartWidget(QWidget *parent = nullptr)
        : QWidget(parent){
        setMinimumSize(240, 190);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    void setSlices(const QVector<DonutSlice> &slices){
        slices_ = slices;
        update();
    }

    void setCenterTitle(const QString &title){
        centerTitle_ = title;
        update();
    }

    void setCenterValue(const QString &value){
        centerValue_ = value;
        update();
    }

    QSize sizeHint() const override{
        return QSize(320, 230);
    }

protected:
    void paintEvent(QPaintEvent *) override{
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QRect area = rect().adjusted(8, 8, -8, -8);
        if (area.width() <= 40 || area.height() <= 40){
            return;
        }

        double total = 0.0;
        for (const DonutSlice &slice : slices_){
            total += std::max(0.0, slice.value);
        }

        const int legendRows = std::max(1, static_cast<int>(slices_.size()));
        const int legendHeight = std::min(96, legendRows * 20 + 6);
        const QRect donutArea = area.adjusted(0, 0, 0, -legendHeight);

        if (donutArea.width() <= 40 || donutArea.height() <= 40){
            return;
        }

        const int side = std::min(donutArea.width(), donutArea.height());
        const QRect donutRect(donutArea.center().x() - side / 2,
                              donutArea.center().y() - side / 2, side, side);
        const int ring = std::max(12, side / 5);

        if (total <= 0.0 || slices_.isEmpty()){
            painter.setPen(QPen(QColor(219, 226, 236), ring, Qt::SolidLine, Qt::FlatCap));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(donutRect);
        } else{
            double angle = 90.0 * 16.0;
            for (const DonutSlice &slice : slices_){
                const double sweep = -std::max(0.0, slice.value) / total * 360.0 * 16.0;
                if (std::fabs(sweep) < 0.001){
                    continue;
                }
                painter.setPen(QPen(slice.color, ring, Qt::SolidLine, Qt::FlatCap));
                painter.setBrush(Qt::NoBrush);
                painter.drawArc(donutRect, static_cast<int>(angle),
                                static_cast<int>(sweep));
                angle += sweep;
            }
        }

        QFont centerTitleFont = font();
        centerTitleFont.setPointSize(9);
        QFont centerValueFont = font();
        centerValueFont.setPointSize(16);
        centerValueFont.setBold(true);
        painter.setFont(centerTitleFont);
        painter.setPen(QColor(82, 96, 109));
        painter.drawText(donutRect.adjusted(ring, ring, -ring, -ring).adjusted(0, -8, 0, -8),
                         Qt::AlignHCenter | Qt::AlignBottom, centerTitle_);
        painter.setFont(centerValueFont);
        painter.setPen(QColor(16, 42, 67));
        painter.drawText(donutRect.adjusted(ring, ring, -ring, -ring).adjusted(0, 10, 0, 10),
                         Qt::AlignHCenter | Qt::AlignTop, centerValue_);

        // 图例：分两列展示，避免在较窄卡片内挤压。
        QFont legendFont = font();
        legendFont.setPointSize(9);
        painter.setFont(legendFont);
        const QFontMetrics metrics(legendFont);
        const int columnWidth = area.width() / 2;
        const int rowHeight = 20;
        for (int i = 0; i < slices_.size(); ++i){
            const DonutSlice &slice = slices_[i];
            const int column = i % 2;
            const int row = i / 2;
            const int x = area.left() + column * columnWidth;
            const int y = area.bottom() - legendHeight + 4 + row * rowHeight;
            painter.setPen(Qt::NoPen);
            painter.setBrush(slice.color);
            painter.drawRoundedRect(QRect(x, y + 3, 12, 12), 3, 3);
            const QString text = QStringLiteral("%1 %2")
                .arg(slice.label)
                .arg(QString::number(std::max(0.0, slice.value), 'f', 0));
            painter.setPen(QColor(51, 65, 85));
            painter.drawText(QRect(x + 18, y, columnWidth - 22, rowHeight),
                             Qt::AlignLeft | Qt::AlignVCenter,
                             metrics.elidedText(text, Qt::ElideRight,
                                                columnWidth - 22));
        }
    }

private:
    QVector<DonutSlice> slices_;
    QString centerTitle_{QStringLiteral("总计")};
    QString centerValue_{QStringLiteral("0")};
};

struct LinePoint{
    QString label;
    double value{0.0};
};

struct LineSeries{
    QString name;
    QColor color{QColor(29, 78, 137)};
    QVector<LinePoint> points;
};

class LineChartWidget : public QWidget{
public:
    explicit LineChartWidget(QWidget *parent = nullptr)
        : QWidget(parent){
        setMinimumSize(240, 190);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    void setUnit(const QString &unit){
        unit_ = unit;
        update();
    }

    void setSeries(const QVector<LineSeries> &series){
        series_ = series;
        update();
    }

    void setYRange(double minimum, double maximum){
        yMin_ = minimum;
        yMax_ = maximum;
        autoScale_ = false;
        update();
    }

    void setAutoScale(bool enabled = true){
        autoScale_ = enabled;
        update();
    }

    QSize sizeHint() const override{
        return QSize(340, 230);
    }

protected:
    void paintEvent(QPaintEvent *) override{
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QRect area = rect().adjusted(12, 10, -12, -12);
        if (area.width() <= 60 || area.height() <= 40){
            return;
        }
        if (series_.isEmpty()){
            painter.setPen(QColor(82, 96, 109));
            painter.drawText(area, Qt::AlignCenter, tr("暂无数据"));
            return;
        }

        double low = yMin_;
        double high = yMax_;
        if (autoScale_){
            low = 0.0;
            high = 1.0;
            bool found = false;
            for (const LineSeries &series : series_){
                for (const LinePoint &point : series.points){
                    if (!found){
                        low = point.value;
                        high = point.value;
                        found = true;
                    } else{
                        low = std::min(low, point.value);
                        high = std::max(high, point.value);
                    }
                }
            }
            if (!found){
                low = 0.0;
                high = 1.0;
            }
            if (low > 0.0){
                low = 0.0;
            }
            if (high <= low){
                high = low + 1.0;
            }
            high = high * 1.15;
        } else if (high <= low){
            high = low + 1.0;
        }

        int maxPoints = 0;
        for (const LineSeries &series : series_){
            maxPoints = std::max(maxPoints, static_cast<int>(series.points.size()));
        }

        QFont tickFont = font();
        tickFont.setPointSize(8);
        QFont labelFont = font();
        labelFont.setPointSize(8);
        QFont legendFont = font();
        legendFont.setPointSize(8);
        const QFontMetrics tickMetrics(tickFont);
        const QFontMetrics labelMetrics(labelFont);
        const QFontMetrics legendMetrics(legendFont);

        const int yLabelWidth = 46;
        const int xLabelHeight = 20;
        const int legendHeight = series_.size() > 1 ? 22 : 0;
        const QRect plot(area.left() + yLabelWidth, area.top() + 4,
                         area.width() - yLabelWidth - 4,
                         area.height() - 4 - xLabelHeight - legendHeight - 2);
        if (plot.width() <= 40 || plot.height() <= 24){
            return;
        }

        painter.setFont(tickFont);
        painter.setPen(QPen(QColor(226, 232, 240), 1.0));
        for (int i = 0; i <= 4; ++i){
            const double ratio = static_cast<double>(i) / 4.0;
            const int y = plot.bottom() - static_cast<int>(std::round(ratio * plot.height()));
            painter.drawLine(plot.left(), y, plot.right(), y);
            const double value = low + (high - low) * ratio;
            const QString text = QString::number(value, 'f', 0) + unit_;
            painter.setPen(QColor(82, 96, 109));
            painter.drawText(QRect(area.left(), y - tickMetrics.height() / 2,
                                   yLabelWidth - 6, tickMetrics.height()),
                             Qt::AlignRight | Qt::AlignVCenter, text);
            painter.setPen(QPen(QColor(226, 232, 240), 1.0));
        }

        const auto mapX = [&](int index){
            if (maxPoints <= 1){
                return plot.center().x();
            }
            return plot.left()
                   + static_cast<int>(std::round(static_cast<double>(index)
                                                 * plot.width() / (maxPoints - 1)));
        };
        const auto mapY = [&](double value){
            const double clamped = std::max(low, std::min(high, value));
            const double ratio = (clamped - low) / (high - low);
            return plot.bottom() - static_cast<int>(std::round(ratio * plot.height()));
        };

        painter.setFont(labelFont);
        const LineSeries &labelSeries = series_.first();
        for (int i = 0; i < maxPoints; ++i){
            const int x = mapX(i);
            QString label;
            if (i < labelSeries.points.size()){
                label = labelSeries.points.at(i).label;
            }
            const QRect labelRect(x - 32, plot.bottom() + 4, 64, xLabelHeight - 2);
            painter.setPen(QColor(82, 96, 109));
            painter.drawText(labelRect, Qt::AlignHCenter | Qt::AlignTop,
                             labelMetrics.elidedText(label, Qt::ElideRight, 62));
        }

        for (const LineSeries &series : series_){
            if (series.points.isEmpty()){
                continue;
            }
            QPainterPath path;
            bool started = false;
            for (int i = 0; i < series.points.size(); ++i){
                const int x = mapX(i);
                const int y = mapY(series.points.at(i).value);
                if (!started){
                    path.moveTo(x, y);
                    started = true;
                } else{
                    path.lineTo(x, y);
                }
            }
            painter.setPen(QPen(series.color, 2.2, Qt::SolidLine, Qt::RoundCap,
                                Qt::RoundJoin));
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(path);

            painter.setPen(Qt::NoPen);
            painter.setBrush(series.color);
            for (int i = 0; i < series.points.size(); ++i){
                painter.drawEllipse(QPointF(mapX(i), mapY(series.points.at(i).value)), 3.4, 3.4);
            }

            if (series_.size() == 1 && maxPoints <= 8){
                painter.setFont(tickFont);
                painter.setPen(QColor(51, 65, 85));
                for (int i = 0; i < series.points.size(); ++i){
                    const QString text = QString::number(series.points.at(i).value, 'f', 0)
                                         + unit_;
                    const QRect valueRect(mapX(i) - 34, mapY(series.points.at(i).value) - 16,
                                          68, 14);
                    painter.drawText(valueRect, Qt::AlignHCenter | Qt::AlignBottom, text);
                }
            }
        }

        if (series_.size() > 1){
            painter.setFont(legendFont);
            int cursorX = plot.left();
            const int swatch = 10;
            const int rowY = area.bottom() - legendHeight + 2;
            for (const LineSeries &series : series_){
                const int textWidth = legendMetrics.horizontalAdvance(series.name) + 8;
                painter.setPen(Qt::NoPen);
                painter.setBrush(series.color);
                painter.drawRoundedRect(QRect(cursorX, rowY + 3, swatch, swatch), 3, 3);
                painter.setPen(QColor(51, 65, 85));
                painter.drawText(QRect(cursorX + swatch + 4, rowY, textWidth, legendHeight - 4),
                                 Qt::AlignLeft | Qt::AlignVCenter, series.name);
                cursorX += swatch + 6 + textWidth;
            }
        }
    }

private:
    QVector<LineSeries> series_;
    QString unit_{QStringLiteral("%")};
    double yMin_{0.0};
    double yMax_{0.0};
    bool autoScale_{true};
};
