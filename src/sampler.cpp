#include "ctm/sampler.hpp"
#include "ctm/platform.hpp"

#include <thread>

namespace ctm {

Sampler::Sampler(double rate_hz, TimePoint start)
    : rate_hz_(rate_hz), period_(1.0 / rate_hz), next_(start) {
    if (rate_hz <= 0.0) {
        rate_hz_ = 100.0;
        period_ = std::chrono::duration<double>(1.0 / rate_hz_);
    }
}

void Sampler::reset(TimePoint start) {
    next_ = start;
    ticks_ = 0;
    missed_ = 0;
}

bool Sampler::wait_next(TimePoint& tick) {
    // Абсолютная сетка: следующий такт = предыдущий + период.
    // Не «sleep(period) от текущего момента», иначе ошибка планировщика
    // накапливается и частота постепенно уплывает.
    next_ += std::chrono::duration_cast<Clock::duration>(period_);
    sleep_until_accurate(next_);

    const TimePoint now = Clock::now();
    const auto half = std::chrono::duration_cast<Clock::duration>(period_ / 2.0);
    if (now > next_ + half) {
        // Проснулись слишком поздно: такт считаем пропущенным, а сетку
        // восстанавливаем от текущего момента, чтобы не бежать за
        // расписанием пачкой догоняющих тактов.
        ++missed_;
        next_ = now;
        return false;
    }

    tick = next_;
    ++ticks_;
    return true;
}

} // namespace ctm
