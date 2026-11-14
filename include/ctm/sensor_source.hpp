#pragma once

#include "ctm/types.hpp"

namespace ctm {

// Контракт источника данных.
//
// Идея простая: конвейеру всё равно, откуда взялось число — из
// математической модели, из COM-порта или из сетевого API. Он умеет
// только читать RawSample. Поэтому замена ModelSensor на HardwareSensor
// не требует переписывать буфер, дискретизацию и фильтры.
class ISensorSource {
public:
    virtual ~ISensorSource() = default;

    // Описание датчика: имя, единицы, диапазон, частота.
    virtual const SensorInfo& info() const = 0;

    // Блокирующее чтение очередного отсчёта. Возвращает false, если
    // источник остановлен или произошла ошибка чтения.
    virtual bool read(RawSample& out) = 0;

    // Запуск и остановка источника. Для математической модели это
    // сброс/установка внутренних часов; для железа — открытие порта.
    virtual void start() {}
    virtual void stop() {}
};

} // namespace ctm
