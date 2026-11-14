#include "ctm/filters.hpp"

#include <algorithm>

namespace ctm {

MovingAverage::MovingAverage(int window) : window_(std::max(1, window)) {}

double MovingAverage::process(double x) {
    history_.push_back(x);
    sum_ += x;
    if (static_cast<int>(history_.size()) > window_) {
        sum_ -= history_.front();
        history_.pop_front();
    }
    return sum_ / static_cast<double>(history_.size());
}

void MovingAverage::reset() {
    history_.clear();
    sum_ = 0.0;
}

double MovingAverage::group_delay_samples() const noexcept {
    return 0.5 * static_cast<double>(window_ - 1);
}

EmaFilter::EmaFilter(int period)
    : period_(std::max(1, period)), alpha_(2.0 / (std::max(1, period) + 1.0)) {}

double EmaFilter::process(double x) {
    if (!primed_) {
        // Первый отсчёт задаёт начальное значение. Если начать с нуля,
        // фильтр будет «ползти» к рабочей температуре несколько секунд
        // и испортит первые секунды графика.
        ema_ = x;
        primed_ = true;
        return ema_;
    }
    ema_ = alpha_ * x + (1.0 - alpha_) * ema_;
    return ema_;
}

void EmaFilter::reset() {
    ema_ = 0.0;
    primed_ = false;
}

double EmaFilter::group_delay_samples() const noexcept {
    return (1.0 - alpha_) / alpha_;
}

void FilterPipeline::add(std::unique_ptr<IFilter> filter) {
    if (filter) {
        filters_.push_back(std::move(filter));
    }
}

double FilterPipeline::process(double x) {
    for (auto& filter : filters_) {
        x = filter->process(x);
    }
    return x;
}

void FilterPipeline::reset() {
    for (auto& filter : filters_) {
        filter->reset();
    }
}

double FilterPipeline::group_delay_samples() const noexcept {
    double total = 0.0;
    for (const auto& filter : filters_) {
        total += filter->group_delay_samples();
    }
    return total;
}

} // namespace ctm
