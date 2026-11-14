#pragma once

#include <array>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace ctm {

constexpr double kPi = 3.14159265358979323846;

// ---------- NTC ----------
struct NtcConfig {
    double r25 = 10000.0;      // Ом при 25 C
    double beta = 3950.0;      // K
    double t0_c = 25.0;
    double r_top = 10000.0;    // верхний резистор делителя
    double v_supply = 3.3;
};

class NtcChannel {
public:
    explicit NtcChannel(NtcConfig cfg = {});
    double resistance(double t_c) const;
    double voltage(double t_c) const;
    double temperature(double v) const;
private:
    NtcConfig cfg_;
};

// ---------- ADS1115 ----------
struct Ads1115Config {
    double vref = 4.096;
    int bits = 16;
    double inl_lsb = 1.0;
    double noise_uv = 8.0;
};

class Ads1115Model {
public:
    explicit Ads1115Model(Ads1115Config cfg = {}, std::uint32_t seed = 1);
    std::uint16_t convert(double v);
    double code_to_voltage(std::uint16_t code) const;
    double lsb_voltage() const;
private:
    Ads1115Config cfg_;
    std::mt19937 rng_;
    std::normal_distribution<double> gauss_{0.0, 1.0};
};

// ---------- tachometer ----------
struct TachConfig {
    double pulses_per_rev = 2.0;
    double clock_hz = 1e6;     // 1 мкс
    double jitter_us = 2.0;    // разброс задержки прерывания
    int avg_periods = 4;
    double timeout_s = 0.5;
};

class TachometerModel {
public:
    explicit TachometerModel(TachConfig cfg = {}, std::uint32_t seed = 2);
    double measure(double true_rpm);
private:
    TachConfig cfg_;
    std::mt19937 rng_;
    std::normal_distribution<double> gauss_{0.0, 1.0};
};

// ---------- thermal plant ----------
struct PlantConfig {
    double tau_s = 8.0;
    double t_start = 42.0;
    double t_low = 45.0;
    double t_high = 82.0;
    double load_period_s = 45.0;
    double ripple_c = 0.35;
    double ripple_hz = 0.12;
};

class ThermalPlant {
public:
    explicit ThermalPlant(PlantConfig cfg = {});
    double step(double t, double dt);
    double truth() const { return truth_; }
    double core() const { return core_; }
private:
    PlantConfig cfg_;
    double core_;
    double truth_ = 0.0;
};

struct FanProfile {
    double base_rpm = 1200.0;
    double load_rpm = 1800.0;
    double ripple_rpm = 90.0;
    double load_period_s = 45.0;
    double rpm_at(double t) const;
};

// ---------- frame ----------
#pragma pack(push, 1)
struct TelemetryFrame {
    std::uint16_t sync = 0xAA55;
    std::uint8_t  version = 1;
    std::uint8_t  flags = 0;
    std::uint32_t seq = 0;
    std::uint32_t timestamp_ms = 0;
    std::int16_t  t_in_c_x100 = 0;
    std::int16_t  t_out_c_x100 = 0;
    std::int16_t  t_amb_c_x100 = 0;
    std::uint16_t rpm1 = 0;
    std::uint16_t rpm2 = 0;
    std::uint16_t u_pump_mv = 0;
    std::uint16_t i_pump_ma = 0;
    std::uint8_t  pwm1 = 0;
    std::uint8_t  pwm2 = 0;
    std::uint16_t status = 0;
    std::uint16_t crc = 0;
};
#pragma pack(pop)
static_assert(sizeof(TelemetryFrame) == 32, "frame must be 32 bytes");

std::uint16_t crc16_ccitt(const std::uint8_t* data, std::size_t len);
void frame_set_crc(TelemetryFrame& f);
bool frame_check_crc(const TelemetryFrame& f);

// ---------- AES-256-GCM ----------
class Aead {
public:
    // Возвращает ciphertext || tag (16 байт).
    static std::vector<std::uint8_t> seal(const std::vector<std::uint8_t>& key,
                                          const std::uint8_t* nonce, std::size_t nonce_len,
                                          const std::uint8_t* aad, std::size_t aad_len,
                                          const std::uint8_t* pt, std::size_t pt_len);
    static bool open(const std::vector<std::uint8_t>& key,
                     const std::uint8_t* nonce, std::size_t nonce_len,
                     const std::uint8_t* aad, std::size_t aad_len,
                     const std::uint8_t* ct, std::size_t ct_len,
                     std::vector<std::uint8_t>& pt);
};

struct CipherFrame {
    std::array<std::uint8_t, 60> bytes{};
};

CipherFrame encode_frame(const TelemetryFrame& f, const std::vector<std::uint8_t>& key,
                         std::uint32_t boot_id, std::uint64_t counter);
bool decode_frame(const CipherFrame& cf, const std::vector<std::uint8_t>& key,
                  TelemetryFrame& out);

// ---------- filters ----------
class MovingAverage {
public:
    explicit MovingAverage(int window = 4);
    double process(double x);
    void reset();
private:
    int window_;
    double sum_ = 0.0;
    std::vector<double> history_;
};

class EmaFilter {
public:
    explicit EmaFilter(int period = 7);
    double process(double x);
    void reset();
    double alpha() const { return alpha_; }
private:
    int period_;
    double alpha_;
    double ema_ = 0.0;
    bool primed_ = false;
};

class FilterPipeline {
public:
    FilterPipeline(int fir_window = 4, int ema_period = 7);
    double process(double x);
    void reset();
private:
    MovingAverage fir_;
    EmaFilter ema_;
};

// ---------- board model ----------
struct BoardSample {
    double t = 0.0;
    double truth_c = 0.0;
    double t_in_true = 0.0, t_out_true = 0.0, t_amb_true = 0.0;
    double v_in = 0.0, v_out = 0.0, v_amb = 0.0;
    std::uint16_t code_in = 0, code_out = 0, code_amb = 0;
    double t_in_meas = 0.0, t_out_meas = 0.0, t_amb_meas = 0.0;
    double rpm1_true = 0.0, rpm2_true = 0.0, rpm1_meas = 0.0, rpm2_meas = 0.0;
    double u_pump = 12.0, i_pump = 0.0;
};

class BoardModel {
public:
    explicit BoardModel(std::uint32_t seed = 42);
    BoardSample sample(double t, double dt);
    TelemetryFrame make_frame(const BoardSample& s, std::uint32_t seq, std::uint32_t ts_ms);
    void set_fault(std::uint16_t flags) { fault_flags_ = flags; }
    const ThermalPlant& plant() const { return plant_; }
private:
    NtcChannel ntc_;
    Ads1115Model adc_in_, adc_out_, adc_amb_;
    TachometerModel tach1_, tach2_;
    ThermalPlant plant_;
    FanProfile fan_;
    std::mt19937 rng_;
    std::normal_distribution<double> gauss_{0.0, 1.0};
    std::uint16_t fault_flags_ = 0;
    double mains_phase_ = 0.0;
};

// ---------- latency ----------
struct LatencySample {
    double adc_ms = 0.0;
    double build_ms = 0.0;
    double crypto_ms = 0.0;
    double uart_ms = 0.0;
    double host_ms = 0.0;
    double queue_ms = 0.0;
    double total_ms = 0.0;
};

class LatencyModel {
public:
    explicit LatencyModel(std::uint32_t seed = 7);
    LatencySample next();
private:
    std::mt19937 rng_;
    std::normal_distribution<double> gauss_{0.0, 1.0};
    std::uniform_real_distribution<double> uni_{0.0, 1.0};
    std::exponential_distribution<double> exp_{1.0 / 0.15};
};

double analog_filter_gain_db(double f_hz, double fc_hz = 30.0, int order = 2);
double fir_gain_db(double f_hz, int n = 4, double fs = 100.0);
double ema_gain_db(double f_hz, int n = 7, double fs = 100.0);

} // namespace ctm
