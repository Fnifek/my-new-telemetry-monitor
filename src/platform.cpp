#include "ctm/platform.hpp"

#include <atomic>
#include <chrono>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#include <timeapi.h>
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
#endif

namespace ctm {
namespace {

#ifdef _WIN32
// Windows 11 и MinGW иногда игнорируют timeBeginPeriod: sleep_for(5ms)
// может просыпаться через 15,6 мс. Waitable timer с флагом
// HIGH_RESOLUTION даёт точность порядка 0,5 мс, чего достаточно для
// периодов 5 и 10 мс.
class HighResolutionWaiter {
public:
    HighResolutionWaiter() {
        handle_ = CreateWaitableTimerExW(nullptr, nullptr,
                                         CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                         TIMER_ALL_ACCESS);
        if (!handle_) {
            // Fallback для старых версий Windows.
            handle_ = CreateWaitableTimerW(nullptr, FALSE, nullptr);
        }
    }

    ~HighResolutionWaiter() {
        if (handle_) {
            CloseHandle(handle_);
        }
    }

    void wait_until(TimePoint target) {
        if (!handle_) {
            std::this_thread::sleep_until(target);
            return;
        }

        const TimePoint now = Clock::now();
        if (target <= now) {
            return;
        }

        const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(target - now).count();
        LARGE_INTEGER due;
        // Отрицательное значение — относительный интервал в единицах 100 нс.
        due.QuadPart = -static_cast<LONGLONG>((ns + 99) / 100);
        if (due.QuadPart == 0) {
            due.QuadPart = -1;
        }
        SetWaitableTimer(handle_, &due, 0, nullptr, nullptr, FALSE);
        WaitForSingleObject(handle_, INFINITE);

        // Таймер может проснуться на доли миллисекунды раньше; добираем
        // остаток коротким spin-циклом.
        while (Clock::now() < target) {
            std::atomic_signal_fence(std::memory_order_seq_cst);
        }
    }

private:
    HANDLE handle_ = nullptr;
};
#endif

} // namespace

TimerResolution::TimerResolution() {
#ifdef _WIN32
    // 1 мс — минимальное разрешение, которое разрешает Windows.
    // Без этого квант планировщика 15,6 мс не даёт выдержать период 10 мс.
    active_ = (timeBeginPeriod(1) == TIMERR_NOERROR);
#endif
}

TimerResolution::~TimerResolution() {
#ifdef _WIN32
    if (active_) {
        timeEndPeriod(1);
    }
#endif
}

void enable_utf8_console() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
}

void sleep_until_accurate(TimePoint target) {
#ifdef _WIN32
    static thread_local HighResolutionWaiter waiter;
    waiter.wait_until(target);
#else
    std::this_thread::sleep_until(target);
#endif
}

} // namespace ctm
