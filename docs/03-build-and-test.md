# Сборка, запуск и тесты

## 1. Требования

- компилятор с C++17: g++ 9+ или MSVC 2019+;
- Windows: MSYS2 UCRT64 или Visual Studio;
- CMake 3.16+ — опционально;
- Python 3.8+ — только для `tools/plot_telemetry.py` (стандартная библиотека, без matplotlib).

Внешних библиотек нет. `-lwinmm` нужен только на Windows.

## 2. Сборка через CMake

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

Появятся:

- `build/ctm_acq` — CLI;
- `build/ctm_tests` — тесты;
- `ctm_core` — статическая библиотека.

## 3. Сборка через g++

### PowerShell

```powershell
./build.ps1
```

### Вручную

```bash
mkdir -p build

g++ -std=c++17 -O2 -Wall -Wextra -Iinclude \
    src/model_sensor.cpp src/sampler.cpp src/filters.cpp \
    src/acquisition_pipeline.cpp src/csv_writer.cpp src/platform.cpp \
    src/main.cpp -o build/ctm_acq.exe -lwinmm -pthread

g++ -std=c++17 -O2 -Wall -Wextra -Iinclude \
    src/model_sensor.cpp src/sampler.cpp src/filters.cpp \
    src/acquisition_pipeline.cpp src/csv_writer.cpp src/platform.cpp \
    tests/test_main.cpp -o build/ctm_tests.exe -lwinmm -pthread
```

На Linux `-lwinmm` не нужен, `platform.cpp` компилируется в no-op.

## 4. Запуск

```bash
# 30 секунд обычного конвейера
./build/ctm_acq.exe --duration 30 --fs 100

# проверка фильтрации + CSV
./build/ctm_acq.exe --verify --duration 60 --csv data/sample_telemetry.csv

# график
python tools/plot_telemetry.py data/sample_telemetry.csv data/telemetry.svg
```

Пример вывода `--verify`:

```text
=== ctm_acq: verify ===
отсчётов:           5988
в статистике:       4994 (после прогрева 10 с)
СКО до фильтра:     0.6054 °C
СКО после фильтра:  0.1264 °C
ослабление шума:    13.61 дБ
макс. ошибка до:    ...
макс. ошибка после: ...
групповая задержка: 4.50 отсчёта (45.00 мс)
CSV:                data/sample_telemetry.csv
```

Числа могут немного отличаться из-за seed и длительности, но порядок сохраняется.

## 5. Тесты

```bash
./build/ctm_tests.exe
```

Ожидаемый финал:

```text
=== 27 passed, 0 failed ===
```

Тесты занимают около секунды: блок дискретизации проверяется 300 мс, конвейер — 250 мс,
модель — несколько сотен миллисекунд.

Если тест `Sampler: 25…32 такта за 300 мс` падает:

- проверьте, что `TimerResolution` включён (он есть в тесте);
- закройте тяжёлые приложения, которые грузят планировщик;
- на виртуальной машине допускается 20–32 такта — это всё ещё рабочий режим, но
  задержки будут выше.

## 6. Типовые ошибки

| Симптом | Причина | Решение |
|---|---|---|
| `timeBeginPeriod` не найден | не подключена `winmm` | добавьте `-lwinmm` / `target_link_libraries(... winmm)` |
| `std::thread` не линкуется | нет pthread | добавьте `-pthread` / `Threads::Threads` |
| `ctm_acq` показывает 20–30 тактов вместо 300 за 3 с | квант таймера 15,6 мс | проверьте `TimerResolution`, запустите без отладчика |
| `--verify` идёт дольше `--duration` | `read()` спит по 5 мс | это нормально: длительность считается по циклу, а последний отсчёт ждёт своего такта |
| CSV пустой | не было отсчётов или неверный путь | проверьте права и `--duration` |

## 7. Структура сборки

```
build/
  ctm_acq.exe
  ctm_tests.exe
data/
  sample_telemetry.csv
  telemetry.svg
```

`build/` и сгенерированные CSV исключены через `.gitignore`; пример
`data/sample_telemetry.csv` оставлен специально.
