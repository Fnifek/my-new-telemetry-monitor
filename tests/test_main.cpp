#include "ctm/acquisition_pipeline.hpp"
#include "ctm/csv_writer.hpp"
#include "ctm/filters.hpp"
#include "ctm/model_sensor.hpp"
#include "ctm/platform.hpp"
#include "ctm/ring_buffer.hpp"
#include "ctm/sampler.hpp"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

int passed = 0;
int failed = 0;

void check(bool ok, const std::string& name, const std::string& detail = {}) {
    std::cout << (ok ? "[PASS] " : "[FAIL] ") << name;
    if (!detail.empty()) {
        std::cout << " — " << detail;
    }
    std::cout << "\n";
    if (ok) {
        ++passed;
    } else {
        ++failed;
    }
}

void test_ring_buffer() {
    ctm::RingBuffer<int, 4> buffer;
    check(buffer.empty() && buffer.size() == 0, "RingBuffer: начальное состояние пусто");

    buffer.try_push(1);
    buffer.try_push(2);
    buffer.try_push(3);
    int value = 0;
    check(buffer.try_pop(value) && value == 1, "RingBuffer: FIFO, первый элемент");
    check(buffer.try_pop(value) && value == 2, "RingBuffer: FIFO, второй элемент");
    check(buffer.try_pop(value) && value == 3, "RingBuffer: FIFO, третий элемент");

    buffer.try_push(1);
    buffer.try_push(2);
    buffer.try_push(3);
    check(!buffer.try_push(4), "RingBuffer: четвёртый push отвергнут");
    check(buffer.overruns() == 1, "RingBuffer: счётчик overruns = 1");

    // После проверки переполнения буфер всё ещё полон (1, 2, 3).
    // Освобождаем его, иначе следующие push(4..6) тоже будут отвергнуты.
    while (buffer.try_pop(value)) {
    }
    buffer.try_push(4);
    buffer.try_push(5);
    buffer.try_push(6);
    check(buffer.pop_latest(value) && value == 6,
          "RingBuffer: pop_latest возвращает самый свежий");
    check(buffer.empty(), "RingBuffer: после pop_latest буфер пуст");
}

void test_filters() {
    ctm::MovingAverage ma(4);
    ma.process(1.0);
    ma.process(2.0);
    ma.process(3.0);
    ma.process(4.0);
    const double mean = ma.process(5.0);
    check(std::abs(mean - 3.5) < 1e-12, "FIR: среднее 2..5 = 3.5");

    ma.reset();
    check(std::abs(ma.process(10.0) - 10.0) < 1e-12, "FIR: reset сбрасывает историю");

    ctm::EmaFilter ema(10);
    check(std::abs(ema.alpha() - 2.0 / 11.0) < 1e-12,
          "EMA: alpha = 2/11 для N = 10");

    ctm::EmaFilter converge(7);
    for (int i = 0; i < 200; ++i) {
        converge.process(50.0);
    }
    check(std::abs(converge.process(50.0) - 50.0) < 1e-6,
          "EMA: сходится к 50 °C");

    converge.reset();
    check(std::abs(converge.process(10.0) - 10.0) < 1e-12,
          "EMA: reset начинает заново");

    ctm::FilterPipeline pipeline;
    pipeline.add(std::make_unique<ctm::MovingAverage>(2));
    pipeline.add(std::make_unique<ctm::EmaFilter>(3));
    pipeline.process(1.0);
    pipeline.process(2.0);
    const double y = pipeline.process(3.0);
    check(std::abs(y - 1.875) < 1e-12,
          "FilterPipeline: порядок КИХ -> EMA");

    const double delay_fir = ctm::MovingAverage(4).group_delay_samples();
    const double delay_ema = ctm::EmaFilter(7).group_delay_samples();
    check(std::abs(delay_fir - 1.5) < 1e-12 && std::abs(delay_ema - 3.0) < 1e-12,
          "Фильтры: групповая задержка 1.5 + 3.0 отсчёта");

    // АЧХ КИХ на частоте сетевой наводки: sin(pi*f*N/fs)/(N*sin(pi*f/fs)).
    constexpr double kPi = 3.14159265358979323846;
    const double f = 49.5, fs = 100.0, n = 4.0;
    const double h = std::abs(std::sin(kPi * f * n / fs) /
                              (n * std::sin(kPi * f / fs)));
    const double db = 20.0 * std::log10(h);
    check(db < -30.0, "FIR: ослабление на 49.5 Гц < -30 дБ",
          "H = " + std::to_string(db) + " дБ");
}

void test_model() {
    const double step = 120.0 / 4096.0;
    const double q = ctm::ModelSensor::quantize(25.0, 0.0, 120.0, 12);
    const double code = q / step;
    check(std::abs(code - std::round(code)) < 1e-9 && q >= 0.0 && q <= 120.0,
          "ModelSensor: квантование кратно шагу АЦП",
          "q = " + std::to_string(q));

    ctm::ModelConfig cfg;
    cfg.seed = 7;
    ctm::ModelSensor sensor(cfg);
    sensor.start();
    bool range_ok = true;
    bool seq_ok = true;
    for (std::uint32_t i = 0; i < 50; ++i) {
        ctm::RawSample sample;
        if (!sensor.read(sample)) {
            break;
        }
        range_ok = range_ok && sample.value >= 0.0 && sample.value <= 120.0;
        seq_ok = seq_ok && sample.seq == i;
    }
    check(range_ok, "ModelSensor: значения в диапазоне 0…120 °C");
    check(seq_ok, "ModelSensor: seq растёт без пропусков");

    ctm::ModelSensor a(cfg);
    ctm::ModelSensor b(cfg);
    a.start();
    b.start();
    bool same = true;
    for (int i = 0; i < 10; ++i) {
        ctm::RawSample sa, sb;
        a.read(sa);
        b.read(sb);
        same = same && sa.value == sb.value;
    }
    check(same, "ModelSensor: одинаковый seed даёт одинаковые отсчёты");
}

void test_sampler() {
    ctm::TimerResolution timer;
    ctm::Sampler sampler(100.0);
    const auto start = ctm::Clock::now();
    int ticks = 0;
    while (std::chrono::duration<double>(ctm::Clock::now() - start).count() < 0.3) {
        ctm::TimePoint tick;
        if (sampler.wait_next(tick)) {
            ++ticks;
        }
    }
    check(ticks >= 25 && ticks <= 32, "Sampler: 25…32 такта за 300 мс",
          "ticks = " + std::to_string(ticks));
    check(sampler.missed() == 0, "Sampler: пропусков нет",
          "missed = " + std::to_string(sampler.missed()));
}

void test_csv() {
    const std::string path = "test_telemetry.csv";
    {
        ctm::CsvWriter writer(path);
        ctm::FilteredSample a;
        a.timestamp = ctm::Clock::now();
        a.raw = 50.0;
        a.filtered = 49.5;
        a.seq = 1;
        writer.write(a);

        ctm::FilteredSample b = a;
        b.timestamp += std::chrono::milliseconds(10);
        b.raw = 51.0;
        b.filtered = 50.2;
        b.seq = 2;
        writer.write(b);
    }

    std::ifstream in(path);
    std::string header;
    std::getline(in, header);
    std::string row1, row2;
    std::getline(in, row1);
    std::getline(in, row2);
    in.close();
    std::remove(path.c_str());

    check(header == "time_s,raw,filtered,seq" && !row1.empty() && !row2.empty(),
          "CsvWriter: заголовок и две строки");
    check(row1.find("50.000000") != std::string::npos &&
              row2.find("51.000000") != std::string::npos,
          "CsvWriter: значения raw записаны");
}

void test_pipeline() {
    ctm::TimerResolution timer;
    ctm::ModelConfig cfg;
    cfg.seed = 11;
    ctm::ModelSensor sensor(cfg);

    ctm::FilterPipeline filters;
    filters.add(std::make_unique<ctm::MovingAverage>(4));
    filters.add(std::make_unique<ctm::EmaFilter>(7));

    std::uint64_t count = 0;
    ctm::AcquisitionPipeline pipeline(
        sensor, std::move(filters), 100.0,
        [&](const ctm::FilteredSample&) { ++count; });

    pipeline.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    pipeline.stop();

    const ctm::PipelineStats stats = pipeline.stats();
    check(stats.received > 0 && stats.processed > 5,
          "Pipeline: источник и обработчик работают",
          "received=" + std::to_string(stats.received) +
              ", processed=" + std::to_string(stats.processed));
    check(stats.overruns == 0, "Pipeline: переполнений нет",
          "overruns=" + std::to_string(stats.overruns));
    check(count == stats.processed, "Pipeline: счётчик sink совпадает со stats");
}

} // namespace

int main() {
    std::cout << "=== ctm_tests: модульные проверки ЛР2 ===\n";
    test_ring_buffer();
    test_filters();
    test_model();
    test_sampler();
    test_csv();
    test_pipeline();
    std::cout << "\n=== " << passed << " passed, " << failed << " failed ===\n";
    return failed == 0 ? 0 : 1;
}
