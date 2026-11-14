#pragma once

#include "ctm/types.hpp"

#include <QElapsedTimer>
#include <QMainWindow>

#include <deque>
#include <memory>

class QLabel;
class QPushButton;
class QTimer;
class TelemetryPlot;

namespace ctm {
class AcquisitionPipeline;
class ModelSensor;
class TimerResolution;
} // namespace ctm

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

public slots:
    void startAcquisition();
    void stopAcquisition();
    void exportCsv();

private:
    void buildUi();
    void refreshUi();
    void onSample(const ctm::FilteredSample& sample);
    void setRunningUi(bool running);

    TelemetryPlot* plot_ = nullptr;
    QLabel* currentValue_ = nullptr;
    QLabel* rawValue_ = nullptr;
    QLabel* filteredValue_ = nullptr;
    QLabel* rateValue_ = nullptr;
    QLabel* statsLabel_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QLabel* timerLabel_ = nullptr;
    QPushButton* startButton_ = nullptr;
    QPushButton* stopButton_ = nullptr;
    QPushButton* exportButton_ = nullptr;
    QTimer* uiTimer_ = nullptr;

    std::unique_ptr<ctm::ModelSensor> sensor_;
    std::unique_ptr<ctm::AcquisitionPipeline> pipeline_;
    std::unique_ptr<ctm::TimerResolution> timerResolution_;
    std::deque<ctm::FilteredSample> samples_;
    QElapsedTimer elapsed_;
    bool running_ = false;
};
