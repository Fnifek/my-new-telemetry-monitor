#pragma once

#include <QPointF>
#include <QVector>
#include <QWidget>

// График телеметрии на QPainter.
//
// Специально не используем QtCharts: для двух линий и сетки хватает
// обычного paintEvent, а зависимостей меньше. Виджет не знает, откуда
// взялись данные; ему просто передают два массива точек.
class TelemetryPlot : public QWidget {
    Q_OBJECT

public:
    explicit TelemetryPlot(QWidget* parent = nullptr);

    void setSamples(const QVector<QPointF>& raw, const QVector<QPointF>& filtered);
    void clear();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QVector<QPointF> raw_;
    QVector<QPointF> filtered_;
};
