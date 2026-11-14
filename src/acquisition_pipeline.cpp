#include "ctm/acquisition_pipeline.hpp"

#include <stdexcept>
#include <utility>

namespace ctm {

AcquisitionPipeline::AcquisitionPipeline(ISensorSource& source,
                                         FilterPipeline filters,
                                         double sample_rate_hz,
                                         Sink sink)
    : source_(source),
      filters_(std::move(filters)),
      sample_rate_hz_(sample_rate_hz),
      sink_(std::move(sink)),
      sampler_(sample_rate_hz) {
    if (sample_rate_hz_ <= 0.0) {
        throw std::invalid_argument("sample_rate_hz must be positive");
    }
}

AcquisitionPipeline::~AcquisitionPipeline() {
    stop();
}

void AcquisitionPipeline::start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        throw std::logic_error("AcquisitionPipeline is already running");
    }

    source_.start();
    sampler_.reset(Clock::now());
    producer_ = std::thread(&AcquisitionPipeline::producer_loop, this);
    consumer_ = std::thread(&AcquisitionPipeline::consumer_loop, this);
}

void AcquisitionPipeline::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    source_.stop();
    if (producer_.joinable()) {
        producer_.join();
    }
    if (consumer_.joinable()) {
        consumer_.join();
    }
}

void AcquisitionPipeline::producer_loop() {
    while (running_.load(std::memory_order_relaxed)) {
        RawSample sample;
        if (!source_.read(sample)) {
            break;
        }
        received_.fetch_add(1, std::memory_order_relaxed);
        if (buffer_.try_push(sample)) {
            pushed_.fetch_add(1, std::memory_order_relaxed);
        }
        // Если буфер полон, try_push уже увеличил overruns. Блокировать
        // поток приёма нельзя: иначе пропустим следующие отсчёты.
    }
}

void AcquisitionPipeline::consumer_loop() {
    while (running_.load(std::memory_order_relaxed)) {
        TimePoint tick;
        if (!sampler_.wait_next(tick)) {
            continue;
        }

        RawSample raw;
        if (!buffer_.pop_latest(raw)) {
            empty_.fetch_add(1, std::memory_order_relaxed);
            continue;
        }

        FilteredSample out;
        out.timestamp = raw.timestamp;
        out.raw = raw.value;
        out.filtered = filters_.process(raw.value);
        out.seq = raw.seq;

        if (sink_) {
            sink_(out);
        }
        processed_.fetch_add(1, std::memory_order_relaxed);
    }
}

PipelineStats AcquisitionPipeline::stats() const {
    PipelineStats s;
    s.received = received_.load(std::memory_order_relaxed);
    s.pushed = pushed_.load(std::memory_order_relaxed);
    s.processed = processed_.load(std::memory_order_relaxed);
    s.missed = sampler_.missed();
    s.overruns = buffer_.overruns();
    s.empty = empty_.load(std::memory_order_relaxed);
    return s;
}

} // namespace ctm
