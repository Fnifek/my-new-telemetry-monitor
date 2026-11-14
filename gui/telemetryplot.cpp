#include "telemetryplot.h"

#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPen>

#include <algorithm>
#include <cmath>

namespace {
constexpr int kLeft = 64;
constexpr int kRight = 24;
constexpr int kTop = 24;
constexpr int kBottom = 54;

QString formatValue(double value) {
    return QString::number(value, 'f', 1);
}
} // namespace

TelemetryPlot::TelemetryPlot(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(260);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAutoFillBackground(false);
}

void TelemetryPlot::setSamples(const QVector<QPointF>& raw,
                               const QVector<QPointF>& filtered) {
    raw_ = raw;
    filtered_ = filtered;
    update();
}

void TelemetryPlot::clear() {
    raw_.clear();
    filtered_.clear();
    update();
}

void TelemetryPlot::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF full = rect();
    const QRectF plot(kLeft, kTop, full.width() - kLeft - kRight,
                      full.height() - kTop - kBottom);

    painter.fillRect(full, QColor("#ffffff"));

    if (filtered_.isEmpty()) {
        painter.setPen(QColor("#7a8494"));
        painter.setFont(QFont("Segoe UI", 11));
        painter.drawText(full, Qt::AlignCenter,
                         QString::fromUtf8("Нажмите «Старт», чтобы увидеть телеметрию"));
        return;
    }

    double min_y = filtered_.front().y();
    double max_y = min_y;
    double max_x = 0.0;
    for (const QPointF& p : filtered_) {
        min_y = std::min(min_y, p.y());
        max_y = std::max(max_y, p.y());
        max_x = std::max(max_x, p.x());
    }
    if (max_x < 1.0) {
        max_x = 1.0;
    } else {
        max_x *= 1.05;
    }
    const double pad = std::max(0.5, (max_y - min_y) * 0.12);
    min_y -= pad;
    max_y += pad;

    auto sx = [&](double x) {
        return plot.left() + (max_x > 0 ? x / max_x : 0.0) * plot.width();
    };
    auto sy = [&](double y) {
        return plot.bottom() - (y - min_y) / (max_y - min_y) * plot.height();
    };

    // Сетка и подписи
    painter.setFont(QFont("Segoe UI", 8));
    const int x_divs = 6;
    const int y_divs = 5;
    for (int i = 0; i <= x_divs; ++i) {
        const double value = max_x * i / x_divs;
        const double x = sx(value);
        painter.setPen(QPen(QColor("#e6eaf0"), 1));
        painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        painter.setPen(QColor("#6b7280"));
        painter.drawText(QRectF(x - 30, plot.bottom() + 8, 60, 18),
                         Qt::AlignHCenter | Qt::AlignTop, formatValue(value));
    }
    for (int i = 0; i <= y_divs; ++i) {
        const double value = min_y + (max_y - min_y) * i / y_divs;
        const double y = sy(value);
        painter.setPen(QPen(QColor("#e6eaf0"), 1));
        painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        painter.setPen(QColor("#6b7280"));
        painter.drawText(QRectF(4, y - 9, kLeft - 10, 18),
                         Qt::AlignRight | Qt::AlignVCenter, formatValue(value));
    }

    // Оси
    painter.setPen(QPen(QColor("#9aa3af"), 1.2));
    painter.drawLine(plot.bottomLeft(), plot.bottomRight());
    painter.drawLine(plot.topLeft(), plot.bottomLeft());

    painter.setPen(QColor("#374151"));
    painter.setFont(QFont("Segoe UI", 9));
    painter.drawText(QRectF(plot.left(), plot.bottom() + 30, plot.width(), 18),
                     Qt::AlignHCenter, QString::fromUtf8("Время, с"));
    painter.drawText(QRectF(plot.left(), 0, plot.width(), 18),
                     Qt::AlignLeft | Qt::AlignVCenter, QString::fromUtf8("Температура, °C"));

    painter.save();
    painter.setClipRect(plot.adjusted(-1, -1, 1, 1));

    auto drawSeries = [&](const QVector<QPointF>& series, const QColor& color, double width) {
        if (series.size() < 2) {
            return;
        }
        QPainterPath path;
        path.moveTo(sx(series.front().x()), sy(series.front().y()));
        for (int i = 1; i < series.size(); ++i) {
            path.lineTo(sx(series[i].x()), sy(series[i].y()));
        }
        painter.setPen(QPen(color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(path);
    };

    drawSeries(raw_, QColor("#aeb6c2"), 1.0);
    drawSeries(filtered_, QColor("#1a73e8"), 2.0);
    painter.restore();

    // Легенда
    const QRectF legend(plot.right() - 250, plot.top() + 8, 240, 48);
    painter.setPen(QPen(QColor("#e1e6ee"), 1));
    painter.setBrush(QColor(255, 255, 255, 235));
    painter.drawRoundedRect(legend, 6, 6);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor("#aeb6c2"), 1.6));
    painter.drawLine(QPointF(legend.left() + 12, legend.top() + 16),
                     QPointF(legend.left() + 44, legend.top() + 16));
    painter.setPen(QColor("#374151"));
    painter.setFont(QFont("Segoe UI", 8));
    painter.drawText(QPointF(legend.left() + 52, legend.top() + 20),
                     QString::fromUtf8("Сырой сигнал"));
    painter.setPen(QPen(QColor("#1a73e8"), 2.2));
    painter.drawLine(QPointF(legend.left() + 12, legend.top() + 34),
                     QPointF(legend.left() + 44, legend.top() + 34));
    painter.setPen(QColor("#374151"));
    painter.drawText(QPointF(legend.left() + 52, legend.top() + 38),
                     QString::fromUtf8("После КИХ + EMA"));
}
