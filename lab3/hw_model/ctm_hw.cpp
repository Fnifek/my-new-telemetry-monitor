#include "ctm_hw.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

#include <windows.h>
#include <bcrypt.h>

namespace ctm {

// ---------------- NTC ----------------
NtcChannel::NtcChannel(NtcConfig cfg) : cfg_(cfg) {}

double NtcChannel::resistance(double t_c) const {
    const double t_k = t_c + 273.15;
    const double t0_k = cfg_.t0_c + 273.15;
    return cfg_.r25 * std::exp(cfg_.beta * (1.0 / t_k - 1.0 / t0_k));
}

double NtcChannel::voltage(double t_c) const {
    const double r = resistance(t_c);
    return cfg_.v_supply * r / (cfg_.r_top + r);
}

double NtcChannel::temperature(double v) const {
    const double eps = 1e-6;
    v = std::clamp(v, eps, cfg_.v_supply - eps);
    const double r = cfg_.r_top * v / (cfg_.v_supply - v);
    const double inv_t = 1.0 / (cfg_.t0_c + 273.15) + std::log(r / cfg_.r25) / cfg_.beta;
    return 1.0 / inv_t - 273.15;
}

// ---------------- ADS1115 ----------------
Ads1115Model::Ads1115Model(Ads1115Config cfg, std::uint32_t seed)
    : cfg_(cfg), rng_(seed) {}

double Ads1115Model::lsb_voltage() const {
    return cfg_.vref / static_cast<double>(1u << cfg_.bits);
}

double Ads1115Model::code_to_voltage(std::uint16_t code) const {
    return static_cast<double>(code) * lsb_voltage();
}

std::uint16_t Ads1115Model::convert(double v) {
    const double lsb = lsb_voltage();
    double code = v / lsb;
    // интегральная нелинейность: ±INL LSB по периоду шкалы
    code += cfg_.inl_lsb * std::sin(2.0 * kPi * code / static_cast<double>(1u << cfg_.bits));
    // приведённый шум входа
    code += (cfg_.noise_uv * 1e-6 / lsb) * gauss_(rng_);
    long q = std::lround(code);
    q = std::clamp<long>(q, 0, (1 << cfg_.bits) - 1);
    return static_cast<std::uint16_t>(q);
}

// ---------------- tachometer ----------------
TachometerModel::TachometerModel(TachConfig cfg, std::uint32_t seed)
    : cfg_(cfg), rng_(seed) {}

double TachometerModel::measure(double true_rpm) {
    if (true_rpm <= 1e-6) return 0.0;
    const double period_s = 60.0 / (true_rpm * cfg_.pulses_per_rev);
    double sum = 0.0;
    for (int i = 0; i < cfg_.avg_periods; ++i) {
        const double jitter_s = (cfg_.jitter_us * 1e-6) * gauss_(rng_);
        double ticks = std::round((period_s + jitter_s) * cfg_.clock_hz);
        ticks = std::max(1.0, ticks);
        sum += ticks / cfg_.clock_hz;
    }
    const double avg = sum / cfg_.avg_periods;
    return 60.0 / (avg * cfg_.pulses_per_rev);
}

// ---------------- thermal plant ----------------
ThermalPlant::ThermalPlant(PlantConfig cfg) : cfg_(cfg), core_(cfg.t_start) {}

double ThermalPlant::step(double t, double dt) {
    const double phase = std::fmod(t, 2.0 * cfg_.load_period_s);
    const double target = (phase < cfg_.load_period_s) ? cfg_.t_low : cfg_.t_high;
    core_ += (target - core_) * (1.0 - std::exp(-dt / cfg_.tau_s));
    truth_ = core_ + cfg_.ripple_c * std::sin(2.0 * kPi * cfg_.ripple_hz * t);
    return truth_;
}

double FanProfile::rpm_at(double t) const {
    const double phase = std::fmod(t, 2.0 * load_period_s);
    const double load = (phase < load_period_s) ? 0.0 : 1.0;
    return base_rpm + load_rpm * load + ripple_rpm * std::sin(2.0 * kPi * 0.05 * t);
}

// ---------------- frame ----------------
std::uint16_t crc16_ccitt(const std::uint8_t* data, std::size_t len) {
    std::uint16_t crc = 0xFFFF;
    for (std::size_t i = 0; i < len; ++i) {
        crc ^= static_cast<std::uint16_t>(data[i]) << 8;
        for (int b = 0; b < 8; ++b) {
            crc = (crc & 0x8000) ? static_cast<std::uint16_t>((crc << 1) ^ 0x1021)
                                 : static_cast<std::uint16_t>(crc << 1);
        }
    }
    return crc;
}

void frame_set_crc(TelemetryFrame& f) {
    f.crc = crc16_ccitt(reinterpret_cast<const std::uint8_t*>(&f),
                        sizeof(TelemetryFrame) - sizeof(std::uint16_t));
}

bool frame_check_crc(const TelemetryFrame& f) {
    return f.crc == crc16_ccitt(reinterpret_cast<const std::uint8_t*>(&f),
                                sizeof(TelemetryFrame) - sizeof(std::uint16_t));
}

// ---------------- AES-256-GCM (Windows CNG) ----------------
namespace {

struct AlgHandle {
    BCRYPT_ALG_HANDLE alg = nullptr;
    DWORD object_len = 0;
    AlgHandle() {
        if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, nullptr, 0) != 0)
            throw std::runtime_error("BCryptOpenAlgorithmProvider failed");
        if (BCryptSetProperty(alg, BCRYPT_CHAINING_MODE,
                              reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
                              sizeof(BCRYPT_CHAIN_MODE_GCM), 0) != 0)
            throw std::runtime_error("BCryptSetProperty(GCM) failed");
        DWORD res = 0;
        if (BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH,
                              reinterpret_cast<PUCHAR>(&object_len), sizeof(object_len), &res, 0) != 0)
            throw std::runtime_error("BCryptGetProperty failed");
    }
    ~AlgHandle() {
        if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    }
};

AlgHandle& alg() {
    static AlgHandle h;
    return h;
}

} // namespace

std::vector<std::uint8_t> Aead::seal(const std::vector<std::uint8_t>& key,
                                     const std::uint8_t* nonce, std::size_t nonce_len,
                                     const std::uint8_t* aad, std::size_t aad_len,
                                     const std::uint8_t* pt, std::size_t pt_len) {
    if (key.size() != 32) throw std::runtime_error("AES-256 key must be 32 bytes");
    BCRYPT_KEY_HANDLE hkey = nullptr;
    std::vector<std::uint8_t> obj(alg().object_len);
    if (BCryptGenerateSymmetricKey(alg().alg, &hkey, obj.data(), static_cast<ULONG>(obj.size()),
                                   const_cast<PUCHAR>(key.data()), static_cast<ULONG>(key.size()), 0) != 0)
        throw std::runtime_error("BCryptGenerateSymmetricKey failed");

    std::vector<std::uint8_t> out(pt_len + 16);
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
    BCRYPT_INIT_AUTH_MODE_INFO(info);
    info.pbNonce = const_cast<PUCHAR>(nonce);
    info.cbNonce = static_cast<ULONG>(nonce_len);
    info.pbAuthData = const_cast<PUCHAR>(aad);
    info.cbAuthData = static_cast<ULONG>(aad_len);
    info.pbTag = out.data() + pt_len;
    info.cbTag = 16;

    ULONG written = 0;
    const NTSTATUS st = BCryptEncrypt(hkey, const_cast<PUCHAR>(pt), static_cast<ULONG>(pt_len),
                                      &info, nullptr, 0, out.data(), static_cast<ULONG>(pt_len),
                                      &written, 0);
    BCryptDestroyKey(hkey);
    if (st != 0) throw std::runtime_error("BCryptEncrypt(GCM) failed");
    out.resize(written + 16);
    return out;
}

bool Aead::open(const std::vector<std::uint8_t>& key,
                const std::uint8_t* nonce, std::size_t nonce_len,
                const std::uint8_t* aad, std::size_t aad_len,
                const std::uint8_t* ct, std::size_t ct_len,
                std::vector<std::uint8_t>& pt) {
    if (key.size() != 32 || ct_len < 16) return false;
    BCRYPT_KEY_HANDLE hkey = nullptr;
    std::vector<std::uint8_t> obj(alg().object_len);
    if (BCryptGenerateSymmetricKey(alg().alg, &hkey, obj.data(), static_cast<ULONG>(obj.size()),
                                   const_cast<PUCHAR>(key.data()), static_cast<ULONG>(key.size()), 0) != 0)
        return false;

    pt.assign(ct_len - 16, 0);
    std::vector<std::uint8_t> tag(ct + ct_len - 16, ct + ct_len);
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
    BCRYPT_INIT_AUTH_MODE_INFO(info);
    info.pbNonce = const_cast<PUCHAR>(nonce);
    info.cbNonce = static_cast<ULONG>(nonce_len);
    info.pbAuthData = const_cast<PUCHAR>(aad);
    info.cbAuthData = static_cast<ULONG>(aad_len);
    info.pbTag = tag.data();
    info.cbTag = static_cast<ULONG>(tag.size());

    ULONG written = 0;
    const NTSTATUS st = BCryptDecrypt(hkey, const_cast<PUCHAR>(ct), static_cast<ULONG>(ct_len - 16),
                                      &info, nullptr, 0, pt.data(), static_cast<ULONG>(pt.size()),
                                      &written, 0);
    BCryptDestroyKey(hkey);
    if (st != 0) return false;
    pt.resize(written);
    return true;
}

CipherFrame encode_frame(const TelemetryFrame& f, const std::vector<std::uint8_t>& key,
                         std::uint32_t boot_id, std::uint64_t counter) {
    CipherFrame cf{};
    std::memcpy(cf.bytes.data(), &f, 8);                    // sync, version, flags, seq
    std::uint8_t nonce[12];
    for (int i = 0; i < 4; ++i) nonce[i] = static_cast<std::uint8_t>((boot_id >> (8 * i)) & 0xFF);
    for (int i = 0; i < 8; ++i) nonce[4 + i] = static_cast<std::uint8_t>((counter >> (8 * i)) & 0xFF);
    std::memcpy(cf.bytes.data() + 8, nonce, sizeof(nonce));
    const auto* plain = reinterpret_cast<const std::uint8_t*>(&f) + 8;
    const auto sealed = Aead::seal(key, nonce, sizeof(nonce), cf.bytes.data(), 8, plain, 24);
    std::memcpy(cf.bytes.data() + 20, sealed.data(), sealed.size());
    return cf;
}

bool decode_frame(const CipherFrame& cf, const std::vector<std::uint8_t>& key,
                  TelemetryFrame& out) {
    std::memcpy(static_cast<void*>(&out), cf.bytes.data(), 8);
    if (out.sync != 0xAA55 || out.version != 1) return false;
    std::vector<std::uint8_t> plain;
    if (!Aead::open(key, cf.bytes.data() + 8, 12, cf.bytes.data(), 8,
                    cf.bytes.data() + 20, 40, plain))
        return false;
    if (plain.size() != 24) return false;
    std::memcpy(reinterpret_cast<std::uint8_t*>(&out) + 8, plain.data(), 24);
    return frame_check_crc(out);
}

// ---------------- filters ----------------
MovingAverage::MovingAverage(int window) : window_(std::max(1, window)) {}

double MovingAverage::process(double x) {
    history_.push_back(x);
    sum_ += x;
    if (static_cast<int>(history_.size()) > window_) {
        sum_ -= history_.front();
        history_.erase(history_.begin());
    }
    return sum_ / static_cast<double>(history_.size());
}

void MovingAverage::reset() {
    history_.clear();
    sum_ = 0.0;
}

EmaFilter::EmaFilter(int period)
    : period_(std::max(1, period)), alpha_(2.0 / (std::max(1, period) + 1.0)) {}

double EmaFilter::process(double x) {
    if (!primed_) {
        ema_ = x;
        primed_ = true;
        return ema_;
    }
    ema_ = x * alpha_ + ema_ * (1.0 - alpha_);
    return ema_;
}

void EmaFilter::reset() {
    ema_ = 0.0;
    primed_ = false;
}

FilterPipeline::FilterPipeline(int fir_window, int ema_period)
    : fir_(fir_window), ema_(ema_period) {}

double FilterPipeline::process(double x) {
    return ema_.process(fir_.process(x));
}

void FilterPipeline::reset() {
    fir_.reset();
    ema_.reset();
}

// ---------------- board model ----------------
BoardModel::BoardModel(std::uint32_t seed)
    : adc_in_({}, seed + 1), adc_out_({}, seed + 2), adc_amb_({}, seed + 3),
      tach1_({}, seed + 4), tach2_({}, seed + 5), plant_({}), fan_(), rng_(seed) {}

BoardSample BoardModel::sample(double t, double dt) {
    BoardSample s;
    s.t = t;
    s.truth_c = plant_.step(t, dt);

    const double load_phase = std::fmod(t, 90.0);
    const double load = (load_phase < 45.0) ? 0.0 : 1.0;
    s.rpm1_true = fan_.rpm_at(t);
    s.rpm2_true = 900.0 + 0.55 * (s.rpm1_true - 900.0);

    // три тепловые точки: вход, выход и воздух на входе
    s.t_in_true = s.truth_c;
    s.t_out_true = s.truth_c + 3.5 * load + 0.4 * std::sin(2.0 * kPi * 0.08 * t);
    s.t_amb_true = 24.0 + 1.5 * std::sin(2.0 * kPi * 0.003 * t);

    s.v_in = ntc_.voltage(s.t_in_true);
    s.v_out = ntc_.voltage(s.t_out_true);
    s.v_amb = ntc_.voltage(s.t_amb_true);

    const double n1 = 0.35 * gauss_(rng_);
    const double n2 = 0.35 * gauss_(rng_);
    const double n3 = 0.25 * gauss_(rng_);
    mains_phase_ += 2.0 * kPi * 49.5 * dt;
    const double mains = 0.7 * std::sin(mains_phase_);
    s.v_in = ntc_.voltage(s.t_in_true + n1 + mains);
    s.v_out = ntc_.voltage(s.t_out_true + n2 + mains);
    s.v_amb = ntc_.voltage(s.t_amb_true + n3);

    s.code_in = adc_in_.convert(s.v_in);
    s.code_out = adc_out_.convert(s.v_out);
    s.code_amb = adc_amb_.convert(s.v_amb);

    s.t_in_meas = ntc_.temperature(adc_in_.code_to_voltage(s.code_in));
    s.t_out_meas = ntc_.temperature(adc_out_.code_to_voltage(s.code_out));
    s.t_amb_meas = ntc_.temperature(adc_amb_.code_to_voltage(s.code_amb));

    s.rpm1_meas = tach1_.measure(s.rpm1_true);
    s.rpm2_meas = tach2_.measure(s.rpm2_true);

    s.u_pump = 12.0 + 0.05 * gauss_(rng_);
    s.i_pump = 0.42 + 0.18 * load + 0.01 * std::sin(2.0 * kPi * 0.11 * t) + 0.004 * gauss_(rng_);
    return s;
}

TelemetryFrame BoardModel::make_frame(const BoardSample& s, std::uint32_t seq, std::uint32_t ts_ms) {
    TelemetryFrame f;
    f.seq = seq;
    f.timestamp_ms = ts_ms;
    f.flags = 0;
    f.t_in_c_x100 = static_cast<std::int16_t>(std::lround(s.t_in_meas * 100.0));
    f.t_out_c_x100 = static_cast<std::int16_t>(std::lround(s.t_out_meas * 100.0));
    f.t_amb_c_x100 = static_cast<std::int16_t>(std::lround(s.t_amb_meas * 100.0));
    f.rpm1 = static_cast<std::uint16_t>(std::clamp<long>(std::lround(s.rpm1_meas), 0, 65535));
    f.rpm2 = static_cast<std::uint16_t>(std::clamp<long>(std::lround(s.rpm2_meas), 0, 65535));
    f.u_pump_mv = static_cast<std::uint16_t>(std::clamp<long>(std::lround(s.u_pump * 1000.0), 0, 65535));
    f.i_pump_ma = static_cast<std::uint16_t>(std::clamp<long>(std::lround(s.i_pump * 1000.0), 0, 65535));
    f.pwm1 = static_cast<std::uint8_t>(std::clamp<long>(std::lround((s.rpm1_true - 800.0) / 24.0), 0, 100));
    f.pwm2 = static_cast<std::uint8_t>(std::clamp<long>(std::lround((s.rpm2_true - 700.0) / 24.0), 0, 100));
    f.status = fault_flags_;
    frame_set_crc(f);
    return f;
}

// ---------------- latency ----------------
LatencyModel::LatencyModel(std::uint32_t seed) : rng_(seed) {}

LatencySample LatencyModel::next() {
    LatencySample s;
    s.adc_ms = 3.5 * uni_(rng_);                                  // возраст отсчёта ADS1115
    s.build_ms = std::max(0.05, 0.15 + 0.02 * gauss_(rng_));      // сборка кадра
    s.crypto_ms = std::max(0.005, 0.030 + 0.005 * gauss_(rng_));  // AES-256-GCM на ESP32
    s.uart_ms = std::max(0.60, 0.651 + 0.005 * gauss_(rng_));     // 60 байт при 921600
    s.host_ms = std::max(0.01, 0.050 + 0.010 * gauss_(rng_));     // разбор и распаковка
    s.queue_ms = 0.05 + exp_(rng_);                               // очередь потребителя
    s.total_ms = s.adc_ms + s.build_ms + s.crypto_ms + s.uart_ms + s.host_ms + s.queue_ms;
    return s;
}

// ---------------- frequency response ----------------
double analog_filter_gain_db(double f_hz, double fc_hz, int order) {
    return -10.0 * std::log10(1.0 + std::pow(f_hz / fc_hz, 2.0 * order));
}

double fir_gain_db(double f_hz, int n, double fs) {
    const double pi = kPi;
    const double num = std::sin(pi * f_hz * n / fs);
    const double den = n * std::sin(pi * f_hz / fs);
    if (std::abs(den) < 1e-12) return 0.0;
    return 20.0 * std::log10(std::abs(num / den) + 1e-12);
}

double ema_gain_db(double f_hz, int n, double fs) {
    const double alpha = 2.0 / (n + 1.0);
    const double w = 2.0 * kPi * f_hz / fs;
    const double re = 1.0 - (1.0 - alpha) * std::cos(w);
    const double im = (1.0 - alpha) * std::sin(w);
    return 20.0 * std::log10(alpha / std::sqrt(re * re + im * im));
}

} // namespace ctm
