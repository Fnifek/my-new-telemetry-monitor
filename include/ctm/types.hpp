#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace ctm {

// Единые часы для всего проекта. steady_clock не зависит от перевода
// системного времени и не прыгает при синхронизации с NTP.
using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;

// Откуда приходят данные. В ЛР1 были допустимы четыре варианта источника;
// в ЛР2 выбран математический генератор, но интерфейс оставлен общим.
enum class SensorKind {
    HardwareAnalog,
    HardwareDigital,
    Model,
    RemoteApi
};

// Паспорт датчика. Нужен потребителю, чтобы знать единицы измерения,
// диапазон и частоту, не зная конкретной реализации источника.
struct SensorInfo {
    std::string name;
    std::string unit;
    SensorKind kind = SensorKind::Model;
    double min_value = 0.0;
    double max_value = 0.0;
    double sample_rate_hz = 0.0;
};

// Один «сырой» отсчёт: время чтения, значение в физических единицах
// и порядковый номер. Номер позволяет замечать потери в тракте.
struct RawSample {
    TimePoint timestamp{};
    double value = 0.0;
    std::uint32_t seq = 0;
};

// Отсчёт после предварительной обработки. Храним и сырое, и
// отфильтрованное значение: это удобно для графиков и для проверки.
struct FilteredSample {
    TimePoint timestamp{};
    double raw = 0.0;
    double filtered = 0.0;
    std::uint32_t seq = 0;
};

} // namespace ctm
