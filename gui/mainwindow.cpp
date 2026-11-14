#include "mainwindow.h"

#include "telemetryplot.h"

#include "ctm/acquisition_pipeline.hpp"
#include "ctm/filters.hpp"
#include "ctm/model_sensor.hpp"
#include "ctm/platform.hpp"

#include <QFile>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QMetaObject>
#include <QPointF>
#include <QPushButton>
#include <QStringConverter>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>
#include <QVector>

#include <algorithm>
#include <cmath>

namespace {

QLabel* makeCaption(const QString& text) {
    auto* label = new QLabel(text);
    label->setStyleSheet("color:#6b7280; font-size:11px;");
    return label;
}

QLabel* makeValue(const QString& text) {
    auto* label = new QLabel(text);
    label->setStyleSheet("color:#111827; font-size:22px; font-weight:600;");
    return label;
}

QFrame* makeCard(const QString& caption, QLabel*& valueOut) {
    auto* card = new QFrame;
    card->setObjectName("statCard");
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(14, 10, 14, 10);
    layout->setSpacing(2);
    layout->addWidget(makeCaption(caption));
    valueOut = makeValue("—");
    layout->addWidget(valueOut);
    return card;
}

} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    buildUi();

    uiTimer_ = new QTimer(this);
    uiTimer_->setInterval(100);
    connect(uiTimer_, &QTimer::timeout, this, &MainWindow::refreshUi);

    setRunningUi(false);
    statusLabel_->setText(QString::fromUtf8("Готов к запуску"));
}

MainWindow::~MainWindow() {
    if (pipeline_) {
        pipeline_->stop();
    }
}

void MainWindow::buildUi() {
    setWindowTitle(QString::fromUtf8("Cooling Telemetry Monitor — ЛР2"));
    resize(1120, 760);

    auto* central = new QWidget(this);
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(22, 18, 22, 18);
    root->setSpacing(14);

    auto* title = new QLabel(QString::fromUtf8("Мониторинг телеметрии систем охлаждения"));
    title->setObjectName("title");
    auto* subtitle = new QLabel(QString::fromUtf8(
        "ЛР2 · математическая модель датчика 200 Гц · дискретизация 100 Гц · КИХ N=4 + EMA N=7"));
    subtitle->setObjectName("subtitle");
    root->addWidget(title);
    root->addWidget(subtitle);

    auto* controls = new QHBoxLayout;
    controls->setSpacing(10);
    startButton_ = new QPushButton(QString::fromUtf8("Старт"));
    stopButton_ = new QPushButton(QString::fromUtf8("Стоп"));
    exportButton_ = new QPushButton(QString::fromUtf8("Экспорт CSV"));
    startButton_->setObjectName("primaryButton");
    stopButton_->setObjectName("secondaryButton");
    exportButton_->setObjectName("secondaryButton");
    connect(startButton_, &QPushButton::clicked, this, &MainWindow::startAcquisition);
    connect(stopButton_, &QPushButton::clicked, this, &MainWindow::stopAcquisition);
    connect(exportButton_, &QPushButton::clicked, this, &MainWindow::exportCsv);
    controls->addWidget(startButton_);
    controls->addWidget(stopButton_);
    controls->addWidget(exportButton_);
    controls->addSpacing(16);

    timerLabel_ = new QLabel(QString::fromUtf8("00:00"));
    timerLabel_->setObjectName("timer");
    controls->addWidget(timerLabel_);
    controls->addStretch(1);
    root->addLayout(controls);

    auto* cards = new QHBoxLayout;
    cards->setSpacing(12);
    cards->addWidget(makeCard(QString::fromUtf8("Текущая температура, °C"), currentValue_));
    cards->addWidget(makeCard(QString::fromUtf8("Сырой отсчёт, °C"), rawValue_));
    cards->addWidget(makeCard(QString::fromUtf8("Шум (raw − фильтр), °C"), filteredValue_));
    cards->addWidget(makeCard(QString::fromUtf8("Темп обработки, Гц"), rateValue_));
    root->addLayout(cards);

    plot_ = new TelemetryPlot(this);
    root->addWidget(plot_, 1);

    auto* footer = new QHBoxLayout;
    statusLabel_ = new QLabel;
    statusLabel_->setObjectName("status");
    statsLabel_ = new QLabel(QString::fromUtf8("принято: 0 · обработано: 0 · пропусков: 0"));
    statsLabel_->setObjectName("stats");
    footer->addWidget(statusLabel_);
    footer->addStretch(1);
    footer->addWidget(statsLabel_);
    root->addLayout(footer);

    setCentralWidget(central);

    setStyleSheet(R"(
        QMainWindow { background: #f3f5f9; }
        QLabel#title { color: #111827; font-size: 24px; font-weight: 700; }
        QLabel#subtitle { color: #6b7280; font-size: 12px; }
        QLabel#timer { color: #1f2937; font-size: 18px; font-weight: 600; padding-left: 8px; }
        QLabel#status { color: #6b7280; font-size: 12px; }
        QLabel#stats { color: #6b7280; font-size: 12px; }
        QFrame#statCard {
            background: #ffffff;
            border: 1px solid #e3e8ef;
            border-radius: 10px;
        }
        QPushButton#primaryButton {
            background: #1a73e8;
            color: #ffffff;
            border: none;
            border-radius: 7px;
            padding: 9px 22px;
            font-size: 13px;
            font-weight: 600;
        }
        QPushButton#primaryButton:hover { background: #1765cc; }
        QPushButton#primaryButton:disabled { background: #b9c7db; }
        QPushButton#secondaryButton {
            background: #ffffff;
            color: #1f2937;
            border: 1px solid #cfd7e3;
            border-radius: 7px;
            padding: 9px 18px;
            font-size: 13px;
        }
        QPushButton#secondaryButton:hover { background: #eef3fb; }
        QPushButton#secondaryButton:disabled { color: #aab3c0; }
    )");
}

void MainWindow::startAcquisition() {
    if (running_) {
        return;
    }

    samples_.clear();
    plot_->clear();
    currentValue_->setText(QString::fromUtf8("—"));
    rawValue_->setText(QString::fromUtf8("—"));
    filteredValue_->setText(QString::fromUtf8("—"));
    rateValue_->setText(QString::fromUtf8("—"));
    statsLabel_->setText(QString::fromUtf8("принято: 0 · обработано: 0 · пропусков: 0"));
    elapsed_.start();

    timerResolution_ = std::make_unique<ctm::TimerResolution>();
    sensor_ = std::make_unique<ctm::ModelSensor>();
    sensor_->start();

    ctm::FilterPipeline filters;
    filters.add(std::make_unique<ctm::MovingAverage>(4));
    filters.add(std::make_unique<ctm::EmaFilter>(7));

    pipeline_ = std::make_unique<ctm::AcquisitionPipeline>(
        *sensor_, std::move(filters), 100.0,
        [this](const ctm::FilteredSample& sample) {
            QMetaObject::invokeMethod(
                this, [this, sample]() { onSample(sample); }, Qt::QueuedConnection);
        });

    pipeline_->start();
    running_ = true;
    uiTimer_->start();
    setRunningUi(true);
    statusLabel_->setText(QString::fromUtf8("Приём и фильтрация запущены"));
}

void MainWindow::stopAcquisition() {
    if (!running_ && !pipeline_) {
        return;
    }

    ctm::PipelineStats stats;
    if (pipeline_) {
        pipeline_->stop();
        stats = pipeline_->stats();
        pipeline_.reset();
    }
    if (sensor_) {
        sensor_->stop();
        sensor_.reset();
    }
    timerResolution_.reset();

    running_ = false;
    uiTimer_->stop();
    setRunningUi(false);
    statusLabel_->setText(QString::fromUtf8("Остановлено"));
    statsLabel_->setText(QString::fromUtf8("принято: %1 · обработано: %2 · пропусков: %3")
                             .arg(stats.received)
                             .arg(stats.processed)
                             .arg(stats.missed));
}

void MainWindow::setRunningUi(bool running) {
    startButton_->setEnabled(!running);
    stopButton_->setEnabled(running);
    exportButton_->setEnabled(!samples_.empty());
}

void MainWindow::onSample(const ctm::FilteredSample& sample) {
    samples_.push_back(sample);
    while (samples_.size() > 12000) {
        samples_.pop_front();
    }
}

void MainWindow::refreshUi() {
    if (samples_.empty()) {
        return;
    }

    const ctm::FilteredSample& last = samples_.back();
    currentValue_->setText(QString::number(last.filtered, 'f', 2));
    rawValue_->setText(QString::number(last.raw, 'f', 2));
    filteredValue_->setText(QString::number(last.raw - last.filtered, 'f', 2));

    const double seconds = std::max(0.1, elapsed_.elapsed() / 1000.0);
    rateValue_->setText(QString::number(samples_.size() / seconds, 'f', 1));

    const int total = static_cast<int>(seconds);
    timerLabel_->setText(QString("%1:%2")
                             .arg(total / 60, 2, 10, QChar('0'))
                             .arg(total % 60, 2, 10, QChar('0')));

    QVector<QPointF> rawPoints;
    QVector<QPointF> filteredPoints;
    rawPoints.reserve(static_cast<int>(samples_.size()));
    filteredPoints.reserve(static_cast<int>(samples_.size()));
    const auto t0 = samples_.front().timestamp;
    for (const auto& s : samples_) {
        const double t = std::chrono::duration<double>(s.timestamp - t0).count();
        rawPoints.push_back(QPointF(t, s.raw));
        filteredPoints.push_back(QPointF(t, s.filtered));
    }
    plot_->setSamples(rawPoints, filteredPoints);

    if (pipeline_) {
        const ctm::PipelineStats stats = pipeline_->stats();
        statsLabel_->setText(QString::fromUtf8("принято: %1 · обработано: %2 · пропусков: %3 · переполнений: %4")
                                 .arg(stats.received)
                                 .arg(stats.processed)
                                 .arg(stats.missed)
                                 .arg(stats.overruns));
    }
    exportButton_->setEnabled(!samples_.empty());
}

void MainWindow::exportCsv() {
    if (samples_.empty()) {
        QMessageBox::information(this, QString::fromUtf8("Экспорт"),
                                 QString::fromUtf8("Пока нет данных для экспорта."));
        return;
    }

    const QString path = QFileDialog::getSaveFileName(
        this, QString::fromUtf8("Сохранить телеметрию"), "telemetry.csv",
        QString::fromUtf8("CSV (*.csv)"));
    if (path.isEmpty()) {
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, QString::fromUtf8("Экспорт"),
                             QString::fromUtf8("Не удалось открыть файл на запись."));
        return;
    }

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << "time_s,raw,filtered,seq\n";
    const auto t0 = samples_.front().timestamp;
    for (const auto& s : samples_) {
        const double t = std::chrono::duration<double>(s.timestamp - t0).count();
        out << QString::number(t, 'f', 4) << ','
            << QString::number(s.raw, 'f', 6) << ','
            << QString::number(s.filtered, 'f', 6) << ','
            << s.seq << '\n';
    }
    statusLabel_->setText(QString::fromUtf8("CSV сохранён: %1").arg(path));
}
