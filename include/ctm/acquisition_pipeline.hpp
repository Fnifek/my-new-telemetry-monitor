#pragma once

#include "ctm/filters.hpp"
#include "ctm/ring_buffer.hpp"
#include "ctm/sampler.hpp"
#include "ctm/sensor_source.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <thread>

namespace ctm {

// Диагностика конвейера. Все счётчики только растут; потребитель
// (CLI, GUI, тест) читает их в конце прогона.
struct PipelineStats {
    std::uint64_t received = 0;   // сколько отсчётов отдал источник
    std::uint64_t pushed = 0;     // сколько попало в кольцевой буфер
    std::uint64_t processed = 0;  // сколько дошло до фильтров и приёмника
    std::uint64_t missed = 0;     // сколько тактов дискретизации пропущено
    std::uint64_t overruns = 0;   // сколько раз буфер был полон
    std::uint64_t empty = 0;      // сколько тактов потребитель не нашёл данных
};

// Конвейер приёма и предварительной обработки.
//
// Схема потоков:
//
//   поток источника            поток обработки
//   ------------------         --------------------------------
//   source.read()  ---->  RingBuffer  ---->  Sampler 100 Гц
//                                               |
//                                         FIR N=4 -> EMA N=7
//                                               |
//                                          Sink(FilteredSample)
//
// Разделение на два потока нужно потому, что источник работает на
// 200 Гц, а обработка — на 100 Гц. Буфер сглаживает разницу скоростей
// и короткие паузы планировщика.
//
// Политика при переполнении: новый отсчёт отбрасывается, счётчик
// overruns растёт. Политика потребителя: забрать самый свежий отсчёт,
// устаревшие выбросить. Для мониторинга текущего теплового состояния
// это правильнее, чем обрабатывать всю очередь с задержкой.
class AcquisitionPipeline {
public:
    using Sink = std::function<void(const FilteredSample&)>;

    AcquisitionPipeline(ISensorSource& source,
                        FilterPipeline filters,
                        double sample_rate_hz,
                        Sink sink);
    ~AcquisitionPipeline();

    AcquisitionPipeline(const AcquisitionPipeline&) = delete;
    AcquisitionPipeline& operator=(const AcquisitionPipeline&) = delete;

    void start();
    void stop();

    PipelineStats stats() const;

private:
    void producer_loop();
    void consumer_loop();

    ISensorSource& source_;
    FilterPipeline filters_;
    double sample_rate_hz_;
    Sink sink_;

    RingBuffer<RawSample, 4096> buffer_;
    Sampler sampler_;

    std::thread producer_;
    std::thread consumer_;
    std::atomic<bool> running_{false};

    std::atomic<std::uint64_t> received_{0};
    std::atomic<std::uint64_t> pushed_{0};
    std::atomic<std::uint64_t> processed_{0};
    std::atomic<std::uint64_t> empty_{0};
};

} // namespace ctm
