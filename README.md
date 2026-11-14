# Cooling Telemetry Monitor — ЛР2 и ЛР3

Учебный проект по дисциплине «Программное и аппаратное обеспечение информационных систем».
Это сквозной проект системы мониторинга, фильтрации и анализа аппаратной телеметрии
систем охлаждения вычислительного узла:

- **ЛР2** — программная часть: приём отсчётов, буферизация, дискретизация 100 Гц,
  КИХ + EMA и Qt GUI;
- **ЛР3** — аппаратная часть и её модель: NTC, ADS1115, ESP32, тахометрия, кадр
  и AES-256-GCM.

Проект продолжает ЛР1: архитектура, UML-диаграммы и схема базы данных остались в отчёте,
а здесь реализован исполняемый конвейер приёма данных. В ЛР3 тот же конвейер подключается
к аппаратной модели ESP32 + ADS1115 через `ISensorSource`.

## Что реализовано

- `ModelSensor` — математическая модель теплового процесса по закону Ньютона–Рихмана:
  прямоугольная нагрузка, инерция, шум измерения, сетевая наводка 49,5 Гц и квантование
  12-разрядного АЦП;
- `RingBuffer` — кольцевой буфер SPSC на 4096 отсчётов с диагностикой переполнений;
- `Sampler` — абсолютная сетка тактов 100 Гц с обнаружением пропущенных тактов;
- `MovingAverage` — КИХ-усреднение с окном N = 4 (нули АЧХ на 25 и 50 Гц);
- `EmaFilter` — экспоненциальное скользящее среднее с периодом N = 7 (alpha = 0,25);
- `AcquisitionPipeline` — два потока: источник 200 Гц и обработка 100 Гц;
- `CsvWriter` — выгрузка `time_s,raw,filtered,seq` для графиков и внешнего анализа;
- CLI `ctm_acq` с режимами обычного прогона и `--verify`;
- **Qt Widgets GUI** `cooling_telemetry_gui`: график, текущие значения, старт/стоп,
  экспорт CSV и безопасная передача отсчётов из потока обработки в GUI;
- **ЛР3** `lab3/hw_model`: NTC + ADS1115, тахометрия, `TelemetryFrame` 32/60 байт,
  CRC-16/CCITT, AES-256-GCM и 24 проверки `--selftest`;
- 27 атомарных проверок ЛР2 в `ctm_tests` (в отчёте они укрупнены до 18).

![Qt GUI](docs/gui-preview.png)

## Архитектура

```
поток источника (200 Гц)                 поток обработки (100 Гц)
────────────────────────                 ─────────────────────────────
ModelSensor::read()                      Sampler::wait_next()
        │                                        │
        └──> RingBuffer<RawSample, 4096> ────────┘
                 SPSC, атомарные индексы         │
                                                 ▼
                                     MovingAverage(N=4)
                                                 │
                                                 ▼
                                          EmaFilter(N=7)
                                                 │
                                                 ▼
                                     Sink(FilteredSample)
```

Почему два потока: источник работает на 200 Гц, обработка — на 100 Гц. Буфер сглаживает
разницу скоростей и короткие паузы планировщика. При переполнении новый отсчёт
отбрасывается, а счётчик `overruns` растёт: блокировать поток приёма нельзя. Потребитель
забирает самый свежий отсчёт и выбрасывает устаревшие — для мониторинга текущего
теплового состояния это важнее полной истории.

## Структура репозитория

```
include/ctm/
  types.hpp                 RawSample, FilteredSample, Clock, SensorInfo
  sensor_source.hpp         интерфейс ISensorSource
  model_sensor.hpp          математическая модель датчика
  ring_buffer.hpp           SPSC-буфер
  sampler.hpp               блок дискретизации
  filters.hpp               IFilter, MovingAverage, EmaFilter, FilterPipeline
  acquisition_pipeline.hpp  конвейер приёма и обработки
  csv_writer.hpp            выгрузка CSV
  platform.hpp              разрешение таймера Windows
src/                        реализации и CLI
gui/                        Qt Widgets-приложение
lab3/hw_model/              модель аппаратного канала ЛР3, прошивка и 24 теста
tests/test_main.cpp         27 проверок без внешних фреймворков
tools/plot_telemetry.py     SVG-график без matplotlib
docs/                       подробные пояснения по слоям ЛР2
lab3/docs/                  пояснения по ЛР3
data/sample_telemetry.csv   пример выгрузки
data/telemetry.svg          пример графика
```

## Быстрый старт

### Вариант 1: CMake

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build --output-on-failure
./build/ctm_acq --duration 30 --fs 100
```

### Вариант 2: g++ (Windows, MSYS2 UCRT64)

В системе может не быть CMake, но есть `g++`. Тогда:

```powershell
./build.ps1
./build/ctm_tests.exe
./build/ctm_acq.exe --duration 30 --fs 100
```

Или вручную:

```bash
g++ -std=c++17 -O2 -Wall -Wextra -Iinclude \
    src/model_sensor.cpp src/sampler.cpp src/filters.cpp \
    src/acquisition_pipeline.cpp src/csv_writer.cpp src/platform.cpp \
    src/main.cpp -o build/ctm_acq.exe -lwinmm -pthread
```

`-lwinmm` нужен для `timeBeginPeriod(1)`: без него квант планировщика Windows 15,6 мс
не позволяет выдержать период 10 мс.

## CLI

```text
ctm_acq [--duration SEC] [--fs HZ] [--seed N] [--csv FILE] [--verify]

--duration SEC  длительность прогона, секунды (по умолчанию 30)
--fs HZ         частота дискретизации, Гц (по умолчанию 100)
--seed N        seed математической модели (по умолчанию 42)
--csv FILE      выгрузить time_s,raw,filtered,seq
--verify        синхронный режим с расчётом СКО по истинной температуре
--help          справка
```

Примеры:

```bash
# обычный прогон конвейера
./build/ctm_acq.exe --duration 30 --fs 100

# проверка качества фильтрации и выгрузка CSV
./build/ctm_acq.exe --verify --duration 60 --csv data/sample_telemetry.csv

# график по CSV (без сторонних библиотек)
python tools/plot_telemetry.py data/sample_telemetry.csv data/telemetry.svg
```

`--verify` работает синхронно: читает отсчёты, сразу фильтрует и сравнивает с
`ModelSensor::last_truth()` — истинной температурой до шума и квантования. Такой режим
детерминирован и не зависит от планировщика, поэтому по нему удобно проверять формулы.

## Qt GUI

```powershell
cd gui
./build_gui.ps1
./release/cooling_telemetry_gui.exe
```

Скрипт собирает GUI без qmake/CMake: вызывает `moc`, компилирует ядро и окно
Qt-компилятором и копирует Qt DLL в `release/`. Альтернативы — `gui/gui.pro` для
Qt Creator, `gui/CMakeLists.txt` для CMake или корневой CMake с
`-DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/mingw_64`.

Что в окне: старт/стоп, текущая/сырая температура, шум, темп обработки, график
КИХ + EMA и экспорт CSV. Отсчёты из потока обработки передаются в GUI через
`QMetaObject::invokeMethod(..., Qt::QueuedConnection)` — виджеты не трогаются из
чужого потока. Подробнее: [docs/07-qt-gui.md](docs/07-qt-gui.md).

## ЛР3 — аппаратная часть

```powershell
cd lab3\hw_model
powershell -ExecutionPolicy Bypass -File .\build.ps1
.\build\ctm_hw.exe --selftest
```

Это модель измерительного модуля: NTC 10 кОм / B3950 → ADS1115 16 бит → ESP32 →
`TelemetryFrame` 32 байта → CRC-16/CCITT → AES-256-GCM → 60 байт по UART.
Требуется Windows (BCrypt/CNG) и g++/CMake.

`--selftest` — 24 проверки, включая вектор NIST для AES-256-GCM, подмену тега и
CRC. Другие режимы: `--simulate`, `--quant`, `--tach`, `--latency`, `--freq`,
`--demo-frame`. Прошивка ESP32 лежит в `lab3/hw_model/firmware_esp32.cpp`.

Подробности: [lab3/README.md](lab3/README.md), [docs/01](lab3/docs/01-hardware-model.md),
[docs/02](lab3/docs/02-frame-and-crypto.md), [docs/03](lab3/docs/03-tests-and-results.md).

## Результаты

Параметры модели совпадают с отчётом ЛР2:

| Параметр | Значение |
|---|---|
| Частота опроса источника | 200 Гц |
| Частота дискретизации | 100 Гц |
| Разрядность АЦП / шаг | 12 бит / 0,0293 °C |
| СКО шума | 0,35 °C |
| Сетевая наводка | 0,7 °C на 49,5 Гц |
| КИХ / EMA | N = 4 / N = 7, alpha = 0,25 |
| Групповая задержка | 4,5 отсчёта = 45 мс |

Ожидаемые результаты `--verify --duration 60`:

| Показатель | Значение |
|---|---|
| СКО до фильтрации | ≈ 0,605 °C |
| СКО после фильтрации | ≈ 0,126 °C |
| Ослабление шума | ≈ 13,6 дБ |
| Ослабление на 49,5 Гц | ≈ 53 дБ (цифровой тракт) |

В отчёте ЛР2 указаны 0,604 → 0,119 °C и 14,1 дБ. Небольшое расхождение (0,605 → 0,126 °C)
объясняется seed, фазой сетевой наводки и окном прогрева; порядок величин и выводы
сохраняются. Числа в таблице выше — фактический вывод `--verify --duration 60`.

## Тесты

`ctm_tests` не требует GoogleTest или Catch2 — это набор обычных функций с `check()`.
Проверяются:

- начальное состояние, FIFO, переполнение и `pop_latest` кольцевого буфера;
- среднее по окну, сброс, коэффициент alpha и сходимость EMA;
- порядок фильтров в цепочке и групповая задержка 1,5 + 3,0 отсчёта;
- ослабление КИХ на 49,5 Гц;
- квантование, диапазон и воспроизводимость модели;
- частота и пропуски блока дискретизации;
- формат CSV;
- совместная работа источника, буфера, дискретизации и фильтров.

Запуск:

```bash
./build/ctm_tests.exe
```

Ожидаемый финал: `27 passed, 0 failed`.

## Пояснения к коду

Код прокомментирован по существу: у каждого класса описана ответственность и инварианты,
у формул — физический смысл. Дополнительно есть документы:

- [docs/01-architecture.md](docs/01-architecture.md) — потоки, владение, буфер, политика отказов;
- [docs/02-signal-processing.md](docs/02-signal-processing.md) — модель тепла, квантование, КИХ и EMA;
- [docs/03-build-and-test.md](docs/03-build-and-test.md) — сборка, CLI, тесты, типовые ошибки;
- [docs/04-report-mapping.md](docs/04-report-mapping.md) — что из отчёта ЛР2 где лежит в коде;
- [docs/05-code-tour.md](docs/05-code-tour.md) — порядок чтения кода и вопросы для защиты;
- [docs/06-git-history.md](docs/06-git-history.md) — фактические хеши, даты и ветки;
- [docs/07-qt-gui.md](docs/07-qt-gui.md) — устройство и сборка Qt-интерфейса;
- [lab3/README.md](lab3/README.md) — аппаратная модель ЛР3;
- [lab3/docs/01-hardware-model.md](lab3/docs/01-hardware-model.md) — тракт и расчёты;
- [lab3/docs/02-frame-and-crypto.md](lab3/docs/02-frame-and-crypto.md) — кадр и AES-GCM;
- [lab3/docs/03-tests-and-results.md](lab3/docs/03-tests-and-results.md) — 24 теста и результаты.

## Публикация на GitHub

В `origin` уже прописан адрес из отчёта:

```bash
git remote -v
# origin  https://github.com/Fnifek/Software-and-hardware-for-an-information-system.git
```

Если репозиторий на GitHub уже создан и вы авторизованы:

```bash
git push -u origin main
git push origin develop
git push --tags
```

Если на GitHub уже есть история (например, старый `README`), сначала выполните
`git pull --rebase origin main`, разрешите конфликты и только потом пушьте. Токен
в репозиторий не коммитится: используйте Git Credential Manager или `gh auth login`.

Локальная копия истории лежит в `lab2_repo.bundle` (вне репозитория). Клонировать её
можно так:

```bash
git clone lab2_repo.bundle cooling-telemetry-monitor
```

## Ограничения

- Это учебная модель, а не драйвер реального датчика: GPIO, COM-порт и АЦП подменяются
  генератором.
- PostgreSQL/libpqxx в этом репозитории не реализованы: запись в БД — ЛР4. Интерфейс
  `Sink` оставлен точкой подключения криптомодуля, БД и GUI.
- `timeBeginPeriod` влияет на систему глобально; в production его нужно включать аккуратно.
- Дата/хеши коммитов в отчёте ЛР2 могут не совпадать с этим репозиторием: см.
  [docs/04-report-mapping.md](docs/04-report-mapping.md).

## Лицензия

Учебный проект, без явной лицензии. Код можно использовать для сдачи и доработки.
