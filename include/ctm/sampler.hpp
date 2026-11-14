#pragma once

#include "ctm/types.hpp"

#include <chrono>
#include <cstdint>

namespace ctm {

// Блок дискретизации.
//
// Задача — выдавать такты строго по абсолютной сетке:
//     t(k) = t0 + k / fs
// а не «подождать период от текущего момента». Второй вариант
// накапливает ошибку планировщика: каждый такт стартует чуть позже
// предыдущего, и эффективная частота постепенно уплывает.
//
// Если поток проснулся позже половины периода, такт считается
// пропущенным, счётчик missed растёт, а сетка восстанавливается от
// текущего момента. Это лучше, чем пытаться «догнать» расписание
// пачкой подряд идущих тактов.
class Sampler {
public:
    explicit Sampler(double rate_hz, TimePoint start = Clock::now());

    // Ждёт очередной такт. Возвращает false, если такт пропущен.
    bool wait_next(TimePoint& tick);

    void reset(TimePoint start = Clock::now());

    double rate_hz() const noexcept { return rate_hz_; }
    std::chrono::duration<double> period() const noexcept { return period_; }

    std::uint64_t ticks() const noexcept { return ticks_; }
    std::uint64_t missed() const noexcept { return missed_; }

private:
    double rate_hz_;
    std::chrono::duration<double> period_;
    TimePoint next_{};
    std::uint64_t ticks_ = 0;
    std::uint64_t missed_ = 0;
};

} // namespace ctm
