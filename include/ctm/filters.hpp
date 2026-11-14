#pragma once

#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace ctm {

// Общий интерфейс цифрового фильтра.
//
// Фильтры намеренно не знают ни про буфер, ни про потоки: каждый умеет
// только принять число и вернуть число. Благодаря этому их можно
// собирать в цепочку в любом порядке и тестировать по отдельности.
class IFilter {
public:
    virtual ~IFilter() = default;

    virtual double process(double x) = 0;
    virtual void reset() = 0;
    virtual const char* name() const noexcept = 0;

    // Групповая задержка в отсчётах. Нужна, чтобы понимать, насколько
    // отфильтрованное значение отстаёт от реального процесса.
    virtual double group_delay_samples() const noexcept = 0;
};

// КИХ-фильтр равномерного усреднения по окну N:
//     y[n] = (x[n] + x[n-1] + ... + x[n-N+1]) / N
//
// При fs = 100 Гц и N = 4 нули АЧХ попадают на 25 и 50 Гц, поэтому
// сетевая наводка 49,5 Гц ослабляется сильнее всего. Реализация хранит
// скользящую сумму, поэтому каждое новое значение обрабатывается за O(1).
class MovingAverage final : public IFilter {
public:
    explicit MovingAverage(int window = 4);

    double process(double x) override;
    void reset() override;
    const char* name() const noexcept override { return "moving_average"; }
    double group_delay_samples() const noexcept override;

    int window() const noexcept { return window_; }

private:
    int window_;
    double sum_ = 0.0;
    std::deque<double> history_;
};

// Экспоненциальное скользящее среднее:
//     EMA[n] = alpha * x[n] + (1 - alpha) * EMA[n-1],
//     alpha = 2 / (N + 1).
//
// EMA не требует хранить историю и даёт меньшее запаздывание, чем
// простое среднее, но не имеет нулей АЧХ: высокие частоты она давит
// монотонно. Поэтому в конвейере сначала стоит КИХ, а EMA добирает
// остаточный шум.
class EmaFilter final : public IFilter {
public:
    explicit EmaFilter(int period = 7);

    double process(double x) override;
    void reset() override;
    const char* name() const noexcept override { return "ema"; }
    double group_delay_samples() const noexcept override;

    double alpha() const noexcept { return alpha_; }
    int period() const noexcept { return period_; }

private:
    int period_;
    double alpha_;
    double ema_ = 0.0;
    bool primed_ = false;
};

// Последовательная цепочка фильтров. Порядок задаётся снаружи, чтобы
// можно было сравнить «только КИХ», «только EMA» и «КИХ + EMA» без
// изменения кода конвейера.
class FilterPipeline {
public:
    FilterPipeline() = default;

    void add(std::unique_ptr<IFilter> filter);
    double process(double x);
    void reset();

    std::size_t size() const noexcept { return filters_.size(); }
    double group_delay_samples() const noexcept;

private:
    std::vector<std::unique_ptr<IFilter>> filters_;
};

} // namespace ctm
