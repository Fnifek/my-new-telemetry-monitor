#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace ctm {

// Кольцевой буфер SPSC (single producer / single consumer).
//
// Почему именно SPSC: поток источника пишет, поток обработки читает.
// Мьютекс здесь не нужен и только добавил бы непредсказуемость в горячий
// путь. Индексы публикуются атомарно: запись — с release, чтение — с
// acquire, поэтому потребитель всегда видит полностью записанный элемент.
//
// Ёмкость должна быть степенью двойки: тогда замена индекса делается
// через маску, а не через деление по модулю.
//
// Один слот сознательно оставляется пустым: это позволяет отличать
// «буфер пуст» от «буфер полон» без отдельного счётчика.
template <typename T, std::size_t Capacity>
class RingBuffer {
    static_assert(Capacity >= 2, "capacity must be at least 2");
    static_assert((Capacity & (Capacity - 1)) == 0,
                  "capacity must be a power of two");

public:
    RingBuffer() = default;
    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;

    // Пытается положить элемент. Если буфер полон, элемент отбрасывается:
    // блокировать поток приёма нельзя, иначе начнём терять время на
    // ожидание потребителя и пропустим следующие отсчёты.
    bool try_push(const T& item) noexcept {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t next = (head + 1) & mask_;
        if (next == tail_.load(std::memory_order_acquire)) {
            overruns_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        slots_[head] = item;
        head_.store(next, std::memory_order_release);
        return true;
    }

    // Извлекает самый старый элемент (FIFO). Нужен для тестов и для
    // сценариев, где важна полная история.
    bool try_pop(T& out) noexcept {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) {
            return false;
        }
        out = slots_[tail];
        tail_.store((tail + 1) & mask_, std::memory_order_release);
        return true;
    }

    // Забирает самый свежий элемент, выбрасывая накопленную очередь.
    // Для мониторинга это правильнее, чем обрабатывать устаревшие
    // значения: важно текущее тепловое состояние, а не архив.
    bool pop_latest(T& out) noexcept {
        bool got = false;
        T tmp{};
        while (try_pop(tmp)) {
            out = tmp;
            got = true;
        }
        return got;
    }

    // Оценка заполнения. В многопоточном режиме это именно оценка:
    // пока читаем head и tail, производитель может добавить элемент.
    std::size_t size() const noexcept {
        const std::size_t head = head_.load(std::memory_order_acquire);
        const std::size_t tail = tail_.load(std::memory_order_acquire);
        return (head - tail) & mask_;
    }

    bool empty() const noexcept { return size() == 0; }

    // Счётчик переполнений — диагностика, а не ошибка: политика
    // «отбросить новый отсчёт» выбрана осознанно.
    std::uint64_t overruns() const noexcept {
        return overruns_.load(std::memory_order_relaxed);
    }

    static constexpr std::size_t capacity() noexcept { return Capacity - 1; }

private:
    std::array<T, Capacity> slots_{};
    std::atomic<std::size_t> head_{0};
    std::atomic<std::size_t> tail_{0};
    std::atomic<std::uint64_t> overruns_{0};
    static constexpr std::size_t mask_ = Capacity - 1;
};

} // namespace ctm
