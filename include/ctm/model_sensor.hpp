#pragma once

#include "ctm/sensor_source.hpp"

#include <random>

namespace ctm {

// Параметры математической модели датчика.
//
// Модель повторяет то, что было в ЛР1/ЛР2: тепловой процесс по закону
// Ньютона–Рихмана, прямоугольная нагрузка, шум измерения, сетевая
// наводка и квантование АЦП. Числа подобраны под отчёт ЛР2, чтобы
// результаты фильтрации совпадали с таблицами 4–5.
struct ModelConfig {
    double source_rate_hz = 200.0;   // частота опроса источника
    double tau_s = 8.0;              // постоянная времени охлаждения
    double t_start_c = 42.0;
    double t_low_c = 45.0;           // температура при малой нагрузке
    double t_high_c = 82.0;          // температура при полной нагрузке
    double load_period_s = 45.0;     // половина периода прямоугольной нагрузки
    double ripple_c = 0.35;          // низкочастотные колебания оборотов
    double ripple_hz = 0.12;
    double noise_c = 0.35;           // СКО белого шума измерения
    double mains_amplitude_c = 0.7;  // амплитуда сетевой наводки
    double mains_frequency_hz = 49.5;
    double jitter_s = 0.003;         // джиттер момента опроса, с
    double adc_min_c = 0.0;
    double adc_max_c = 120.0;
    int adc_bits = 12;
    std::uint32_t seed = 42;
};

// Математический источник температуры.
//
// read() блокируется до следующего момента опроса, поэтому его можно
// ставить в отдельный поток и не крутить busy-loop в конвейере.
// Внутри — три слоя:
//   1) тепловая инерция (экспоненциальное приближение к цели);
//   2) физические помехи (колебания кулера, белый шум, наводка 49,5 Гц);
//   3) квантование АЦП (12 бит, диапазон 0…120 °C).
//
// last_truth() возвращает «истинную» температуру до шума и квантования.
// Она нужна только режиму --verify и тестам; в обычном конвейере
// потребитель её не видит.
class ModelSensor final : public ISensorSource {
public:
    explicit ModelSensor(ModelConfig cfg = {});

    const SensorInfo& info() const override { return info_; }
    bool read(RawSample& out) override;
    void start() override;
    void stop() override;

    double last_truth() const noexcept { return truth_; }
    const ModelConfig& config() const noexcept { return cfg_; }

    // Квантование по шагу АЦП. Вынесено в статический метод, чтобы
    // тесты могли проверить его отдельно от генератора.
    static double quantize(double value, double min_value, double max_value, int bits);

private:
    double target_temperature(double t) const;

    ModelConfig cfg_;
    SensorInfo info_;
    std::mt19937 rng_;
    std::normal_distribution<double> noise_{0.0, 1.0};
    std::uniform_real_distribution<double> jitter_{-1.0, 1.0};

    TimePoint t0_{};
    TimePoint next_{};
    double model_time_s_ = 0.0;
    double temperature_ = 0.0;
    double truth_ = 0.0;
    std::uint32_t seq_ = 0;
    bool running_ = false;
};

} // namespace ctm
