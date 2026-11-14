#pragma once

#include "ctm/types.hpp"

namespace ctm {

// RAII-обёртка над разрешением системного таймера Windows.
//
// По умолчанию квант планировщика Windows — около 15,6 мс. При периоде
// дискретизации 10 мс это означает, что sleep_until() может просыпаться
// заметно позже, и вместо 300 тактов за 3 секунды мы получим 20–30.
// timeBeginPeriod(1) просит систему поднять разрешение до 1 мс.
//
// На других платформах обёртка ничего не делает. Это нормально:
// конвейер не должен зависеть от Windows-специфики.
class TimerResolution {
public:
    TimerResolution();
    ~TimerResolution();

    TimerResolution(const TimerResolution&) = delete;
    TimerResolution& operator=(const TimerResolution&) = delete;

    bool active() const noexcept { return active_; }

private:
    bool active_ = false;
};

// Включает UTF-8 в консоли Windows, чтобы русские сообщения CLI
// не превращались в «кракозябры». На других платформах — no-op.
void enable_utf8_console();

// Точное ожидание до момента target.
//
// Даже с timeBeginPeriod(1) обычный sleep_for может просыпаться на
// 1–2 мс позже. Для периода 10 мс это уже заметно. Поэтому на Windows
// используется waitable timer с флагом HIGH_RESOLUTION, а остаток до
// целевого момента добирается коротким spin-циклом.
void sleep_until_accurate(TimePoint target);

} // namespace ctm
