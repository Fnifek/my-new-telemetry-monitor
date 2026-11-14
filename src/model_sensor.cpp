#include "ctm/model_sensor.hpp"
#include "ctm/platform.hpp"

#include <algorithm>
#include <cmath>
#include <thread>

namespace ctm {
namespace {
constexpr double kPi = 3.14159265358979323846;
}

ModelSensor::ModelSensor(ModelConfig cfg)
    : cfg_(cfg), rng_(cfg.seed), temperature_(cfg.t_start_c), truth_(cfg.t_start_c) {
    info_.name = "model_thermal_sensor";
    info_.unit = "C";
    info_.kind = SensorKind::Model;
    info_.min_value = cfg_.adc_min_c;
    info_.max_value = cfg_.adc_max_c;
    info_.sample_rate_hz = cfg_.source_rate_hz;
    jitter_ = std::uniform_real_distribution<double>(-cfg_.jitter_s, cfg_.jitter_s);
}

void ModelSensor::start() {
    t0_ = Clock::now();
    next_ = t0_;
    model_time_s_ = 0.0;
    temperature_ = cfg_.t_start_c;
    truth_ = temperature_;
    seq_ = 0;
    running_ = true;
}

void ModelSensor::stop() {
    running_ = false;
}

double ModelSensor::target_temperature(double t) const {
    // Прямоугольная нагрузка: половину периода — «покой», половину —
    // «полная нагрузка». Это простой, но наглядный тест для фильтра:
    // на фронте нагрузки видно и запаздывание, и сглаживание.
    const double phase = std::fmod(t, 2.0 * cfg_.load_period_s);
    return (phase < cfg_.load_period_s) ? cfg_.t_low_c : cfg_.t_high_c;
}

double ModelSensor::quantize(double value, double min_value, double max_value, int bits) {
    if (bits <= 0) {
        return value;
    }
    // Шаг считаем как диапазон / 2^bits, как в отчёте ЛР2:
    // 120 / 4096 = 0,0293 °C. Это чуть консервативнее, чем /(2^bits - 1),
    // зато совпадает с таблицей 1 отчёта.
    const double levels = static_cast<double>(1u << bits);
    const double step = (max_value - min_value) / levels;
    const double clamped = std::clamp(value, min_value, max_value);
    const double code = std::round((clamped - min_value) / step);
    return std::clamp(min_value + code * step, min_value, max_value);
}

bool ModelSensor::read(RawSample& out) {
    if (!running_) {
        return false;
    }

    // Абсолютная сетка источника: следующий момент = предыдущий + период
    // плюс небольшой джиттер, имитирующий неравномерность драйвера.
    const double period_s = 1.0 / cfg_.source_rate_hz + jitter_(rng_);
    next_ += std::chrono::duration_cast<Clock::duration>(
        std::chrono::duration<double>(period_s));

    const TimePoint now = Clock::now();
    if (next_ < now) {
        // Поток отстал (например, из-за загрузки ОС). Не пытаемся
        // «догнать» расписание пачкой мгновенных отсчётов.
        next_ = now;
    }
    sleep_until_accurate(next_);

    // Модельное время — это номер отсчёта, умноженный на номинальный
    // период. Оно не зависит от того, насколько поздно проснулся поток:
    // иначе планировщик менял бы не только задержку, но и сам сигнал.
    model_time_s_ += 1.0 / cfg_.source_rate_hz;
    const double t = model_time_s_;
    const double target = target_temperature(t);

    // Закон Ньютона–Рихмана в дискретном виде. Постоянная времени 8 с
    // даёт плавный переходный процесс, который хорошо видно на графике.
    const double dt = 1.0 / cfg_.source_rate_hz;
    temperature_ += (target - temperature_) * (1.0 - std::exp(-dt / cfg_.tau_s));

    // Низкочастотные колебания оборотов кулера — медленный «горб»,
    // который не должен полностью исчезать после фильтрации.
    const double ripple = cfg_.ripple_c * std::sin(2.0 * kPi * cfg_.ripple_hz * t);

    // Сетевая наводка 49,5 Гц. Частота специально чуть ниже 50 Гц:
    // КИХ с окном 4 имеет нуль на 50 Гц, и наводка попадает рядом с ним.
    const double mains = cfg_.mains_amplitude_c *
                         std::sin(2.0 * kPi * cfg_.mains_frequency_hz * t);

    truth_ = temperature_ + ripple;
    const double noisy = truth_ + cfg_.noise_c * noise_(rng_) + mains;

    out.timestamp = next_;
    out.value = quantize(noisy, cfg_.adc_min_c, cfg_.adc_max_c, cfg_.adc_bits);
    out.seq = seq_++;
    return true;
}

} // namespace ctm
