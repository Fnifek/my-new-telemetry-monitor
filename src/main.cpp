#include "ctm/acquisition_pipeline.hpp"
#include "ctm/csv_writer.hpp"
#include "ctm/filters.hpp"
#include "ctm/model_sensor.hpp"
#include "ctm/platform.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

using ctm::EmaFilter;
using ctm::FilterPipeline;
using ctm::MovingAverage;
using ctm::TimerResolution;

struct Options {
    double duration_s = 30.0;
    double sample_rate_hz = 100.0;
    std::uint32_t seed = 42;
    std::string csv_path;
    bool verify = false;
};

void print_help() {
    std::cout
        << "ctm_acq — приём и предварительная обработка телеметрии охлаждения\n\n"
        << "Использование:\n"
        << "  ctm_acq [--duration SEC] [--fs HZ] [--seed N] [--csv FILE] [--verify]\n\n"
        << "Опции:\n"
        << "  --duration SEC  длительность прогона, секунды (по умолчанию 30)\n"
        << "  --fs HZ         частота дискретизации, Гц (по умолчанию 100)\n"
        << "  --seed N        seed математической модели (по умолчанию 42)\n"
        << "  --csv FILE      выгрузить time_s,raw,filtered,seq в CSV\n"
        << "  --verify        синхронный режим с расчётом СКО по истинной температуре\n"
        << "  --help          показать эту справку\n\n"
        << "Примеры:\n"
        << "  ctm_acq --duration 30 --fs 100\n"
        << "  ctm_acq --verify --duration 60 --csv telemetry.csv\n";
}

double parse_double(const std::string& text, const char* option) {
    try {
        std::size_t pos = 0;
        const double value = std::stod(text, &pos);
        if (pos != text.size()) {
            throw std::invalid_argument("trailing characters");
        }
        return value;
    } catch (const std::exception&) {
        throw std::invalid_argument(std::string("invalid value for ") + option + ": " + text);
    }
}

std::uint32_t parse_u32(const std::string& text, const char* option) {
    try {
        std::size_t pos = 0;
        const unsigned long value = std::stoul(text, &pos);
        if (pos != text.size()) {
            throw std::invalid_argument("trailing characters");
        }
        return static_cast<std::uint32_t>(value);
    } catch (const std::exception&) {
        throw std::invalid_argument(std::string("invalid value for ") + option + ": " + text);
    }
}

Options parse_options(int argc, char** argv) {
    Options opt;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto need_value = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                throw std::invalid_argument(std::string("missing value for ") + name);
            }
            return argv[++i];
        };

        if (arg == "--help" || arg == "-h") {
            print_help();
            std::exit(0);
        } else if (arg == "--verify") {
            opt.verify = true;
        } else if (arg == "--duration") {
            opt.duration_s = parse_double(need_value("--duration"), "--duration");
        } else if (arg == "--fs") {
            opt.sample_rate_hz = parse_double(need_value("--fs"), "--fs");
        } else if (arg == "--seed") {
            opt.seed = parse_u32(need_value("--seed"), "--seed");
        } else if (arg == "--csv") {
            opt.csv_path = need_value("--csv");
        } else {
            throw std::invalid_argument("unknown option: " + arg);
        }
    }

    if (opt.duration_s <= 0.0) {
        throw std::invalid_argument("--duration must be positive");
    }
    if (opt.sample_rate_hz <= 0.0) {
        throw std::invalid_argument("--fs must be positive");
    }
    return opt;
}

FilterPipeline make_filters() {
    FilterPipeline filters;
    filters.add(std::make_unique<MovingAverage>(4));
    filters.add(std::make_unique<EmaFilter>(7));
    return filters;
}

void run_pipeline(const Options& opt) {
    TimerResolution timer;

    ctm::ModelConfig cfg;
    cfg.seed = opt.seed;
    ctm::ModelSensor sensor(cfg);

    std::unique_ptr<ctm::CsvWriter> csv;
    if (!opt.csv_path.empty()) {
        csv = std::make_unique<ctm::CsvWriter>(opt.csv_path);
    }

    std::uint64_t count = 0;
    double min_filtered = 1e9;
    double max_filtered = -1e9;
    auto sink = [&](const ctm::FilteredSample& sample) {
        ++count;
        min_filtered = std::min(min_filtered, sample.filtered);
        max_filtered = std::max(max_filtered, sample.filtered);
        if (csv) {
            csv->write(sample);
        }
    };

    ctm::AcquisitionPipeline pipeline(sensor, make_filters(), opt.sample_rate_hz, sink);
    std::cout << "=== ctm_acq: приём и фильтрация ===\n"
              << "источник: " << sensor.info().name << ", "
              << sensor.info().sample_rate_hz << " Гц\n"
              << "дискретизация: " << opt.sample_rate_hz << " Гц\n"
              << "фильтры: КИХ N=4 -> EMA N=7\n"
              << "длительность: " << opt.duration_s << " с\n\n";

    pipeline.start();
    std::this_thread::sleep_for(std::chrono::duration<double>(opt.duration_s));
    pipeline.stop();

    const ctm::PipelineStats stats = pipeline.stats();
    std::cout << "принято источником: " << stats.received << "\n"
              << "положено в буфер:   " << stats.pushed << "\n"
              << "обработано:         " << stats.processed << "\n"
              << "пропущено тактов:   " << stats.missed << "\n"
              << "переполнений:       " << stats.overruns << "\n"
              << "пустых тактов:      " << stats.empty << "\n";
    if (count > 0) {
        std::cout << "диапазон фильтра:   " << std::fixed << std::setprecision(2)
                  << min_filtered << " … " << max_filtered << " °C\n";
    }
    if (csv) {
        std::cout << "CSV:                " << csv->path() << "\n";
    }
}

void run_verify(const Options& opt) {
    TimerResolution timer;

    ctm::ModelConfig cfg;
    cfg.seed = opt.seed;
    ctm::ModelSensor sensor(cfg);
    FilterPipeline filters = make_filters();

    std::unique_ptr<ctm::CsvWriter> csv;
    if (!opt.csv_path.empty()) {
        csv = std::make_unique<ctm::CsvWriter>(opt.csv_path);
    }

    sensor.start();
    const auto started = ctm::Clock::now();
    const double source_rate = sensor.info().sample_rate_hz;
    const auto stride = std::max<long>(1, std::lround(source_rate / opt.sample_rate_hz));
    constexpr double kWarmupS = 10.0;
    std::uint64_t source_index = 0;
    double sum_raw = 0.0;
    double sum_filtered = 0.0;
    double max_raw = 0.0;
    double max_filtered = 0.0;
    std::uint64_t count = 0;
    std::uint64_t evaluated = 0;

    while (std::chrono::duration<double>(ctm::Clock::now() - started).count() <
           opt.duration_s) {
        ctm::RawSample raw;
        if (!sensor.read(raw)) {
            break;
        }
        // Источник работает на 200 Гц, фильтры в отчёте — на 100 Гц.
        // Поэтому в verify обрабатываем каждый stride-й отсчёт, иначе
        // КИХ с окном 4 окажется настроенным на другую частоту.
        if (source_index++ % static_cast<std::uint64_t>(stride) != 0) {
            continue;
        }
        const double elapsed =
            std::chrono::duration<double>(ctm::Clock::now() - started).count();
        const double filtered = filters.process(raw.value);
        const double truth = sensor.last_truth();
        const double err_raw = raw.value - truth;
        const double err_filtered = filtered - truth;

        ++count;
        if (elapsed >= kWarmupS) {
            sum_raw += err_raw * err_raw;
            sum_filtered += err_filtered * err_filtered;
            max_raw = std::max(max_raw, std::abs(err_raw));
            max_filtered = std::max(max_filtered, std::abs(err_filtered));
            ++evaluated;
        }

        if (csv) {
            ctm::FilteredSample out;
            out.timestamp = raw.timestamp;
            out.raw = raw.value;
            out.filtered = filtered;
            out.seq = raw.seq;
            csv->write(out);
        }
    }

    if (evaluated == 0) {
        throw std::runtime_error("no samples after warm-up");
    }

    const double sko_raw = std::sqrt(sum_raw / static_cast<double>(evaluated));
    const double sko_filtered = std::sqrt(sum_filtered / static_cast<double>(evaluated));
    const double attenuation_db = 20.0 * std::log10(sko_raw / sko_filtered);
    const double delay_samples = filters.group_delay_samples();
    const double delay_ms = delay_samples / opt.sample_rate_hz * 1000.0;

    std::cout << "=== ctm_acq: verify ===\n"
              << "отсчётов:            " << count << "\n"
              << "в статистике:        " << evaluated << " (после прогрева 10 с)\n"
              << "СКО до фильтра:      " << std::fixed << std::setprecision(4)
              << sko_raw << " °C\n"
              << "СКО после фильтра:   " << sko_filtered << " °C\n"
              << "ослабление шума:     " << std::setprecision(2) << attenuation_db << " дБ\n"
              << "макс. ошибка до:     " << std::setprecision(4) << max_raw << " °C\n"
              << "макс. ошибка после:  " << max_filtered << " °C\n"
              << "групповая задержка:  " << std::setprecision(2) << delay_samples
              << " отсчёта (" << delay_ms << " мс)\n";
    if (csv) {
        std::cout << "CSV:                 " << csv->path() << "\n";
    }
}

} // namespace

int main(int argc, char** argv) {
    ctm::enable_utf8_console();
    try {
        const Options opt = parse_options(argc, argv);
        if (opt.verify) {
            run_verify(opt);
        } else {
            run_pipeline(opt);
        }
    } catch (const std::exception& e) {
        std::cerr << "ошибка: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
