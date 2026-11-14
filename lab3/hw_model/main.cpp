#include "ctm_hw.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

using namespace ctm;

namespace {

int g_pass = 0;
int g_fail = 0;

void check(bool ok, const std::string& name, const std::string& detail = {}) {
    std::printf("[%s] %-46s %s\n", ok ? "PASS" : "FAIL", name.c_str(), detail.c_str());
    if (ok) ++g_pass; else ++g_fail;
}

std::string hex(const std::uint8_t* p, std::size_t n) {
    std::ostringstream os;
    for (std::size_t i = 0; i < n; ++i)
        os << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(p[i]);
    return os.str();
}

std::vector<std::uint8_t> test_key() {
    std::vector<std::uint8_t> k(32);
    for (int i = 0; i < 32; ++i) k[i] = static_cast<std::uint8_t>(i);
    return k;
}

void selftest() {
    std::printf("=== ctm_hw self-test ===\n");
    NtcChannel ntc;

    const double r25 = ntc.resistance(25.0);
    check(std::abs(r25 - 10000.0) < 1.0, "NTC: R(25 C) = 10 kOhm",
          "R=" + std::to_string(r25));

    double max_rt = 0.0;
    for (int t = -10; t <= 110; ++t) {
        const double back = ntc.temperature(ntc.voltage(t));
        max_rt = std::max(max_rt, std::abs(back - t));
    }
    check(max_rt < 0.01, "NTC: voltage-temperature roundtrip", "max=" + std::to_string(max_rt) + " C");

    Ads1115Model adc({}, 5);
    const double lsb = adc.lsb_voltage();
    check(std::abs(lsb - 62.5e-6) < 1e-9, "ADS1115: LSB = 62.5 uV", "lsb=" + std::to_string(lsb));

    const auto code = adc.convert(1.65);
    check(std::abs(static_cast<double>(code) - 26400.0) <= 2.0, "ADS1115: code(1.65 V) = 26400",
          "code=" + std::to_string(code));

    double max_q = 0.0;
    for (int i = 0; i < 1000; ++i) {
        const double v = 0.2 + 3.0 * i / 999.0;
        const auto c = adc.convert(v);
        max_q = std::max(max_q, std::abs(adc.code_to_voltage(c) - v));
    }
    check(max_q < 4.0 * lsb, "ADS1115: quantization error < 4 LSB",
          "max=" + std::to_string(max_q / lsb) + " LSB");

    const char* digits = "123456789";
    const auto crc = crc16_ccitt(reinterpret_cast<const std::uint8_t*>(digits), 9);
    check(crc == 0x29B1, "CRC-16/CCITT: check value 0x29B1",
          "crc=0x" + hex(reinterpret_cast<const std::uint8_t*>(&crc), 2));

    check(sizeof(TelemetryFrame) == 32, "Frame: size = 32 bytes",
          "size=" + std::to_string(sizeof(TelemetryFrame)));

    BoardModel board(11);
    BoardSample s = board.sample(12.0, 0.01);
    TelemetryFrame f = board.make_frame(s, 100, 12000);
    check(frame_check_crc(f), "Frame: CRC accepted after build");

    int detected = 0;
    for (int i = 0; i < 1000; ++i) {
        TelemetryFrame g = f;
        reinterpret_cast<std::uint8_t*>(&g)[i % 30] ^= static_cast<std::uint8_t>(1u << (i % 8));
        if (!frame_check_crc(g)) ++detected;
    }
    check(detected == 1000, "Frame: CRC detects 1000 single-bit errors",
          "detected=" + std::to_string(detected));

    // NIST AES-256-GCM vector: key=0^32, nonce=0^12, pt=0^16
    std::vector<std::uint8_t> zero_key(32, 0), zero_iv(12, 0), zero_pt(16, 0);
    auto sealed = Aead::seal(zero_key, zero_iv.data(), zero_iv.size(), nullptr, 0,
                             zero_pt.data(), zero_pt.size());
    const std::string ct_hex = hex(sealed.data(), 16);
    const std::string tag_hex = hex(sealed.data() + 16, 16);
    check(ct_hex == "cea7403d4d606b6e074ec5d3baf39d18", "AES-256-GCM: NIST ciphertext", ct_hex);
    check(tag_hex == "d0d1c8a799996bf0265b98b5d48ab919", "AES-256-GCM: NIST tag", tag_hex);

    std::vector<std::uint8_t> opened;
    const bool ok_open = Aead::open(zero_key, zero_iv.data(), zero_iv.size(), nullptr, 0,
                                    sealed.data(), sealed.size(), opened);
    check(ok_open && opened == zero_pt, "AES-256-GCM: open returns plaintext");

    auto tampered = sealed;
    tampered[0] ^= 0x01;
    const bool ok_tamper = Aead::open(zero_key, zero_iv.data(), zero_iv.size(), nullptr, 0,
                                      tampered.data(), tampered.size(), opened);
    check(!ok_tamper, "AES-256-GCM: tampered tag rejected");

    const auto key = test_key();
    const auto cf = encode_frame(f, key, 0x12345678, 1);
    TelemetryFrame decoded;
    check(decode_frame(cf, key, decoded), "Envelope: frame decode accepted");
    check(decoded.seq == f.seq && decoded.t_in_c_x100 == f.t_in_c_x100, "Envelope: fields preserved");

    auto broken = cf;
    broken.bytes[25] ^= 0x10;
    check(!decode_frame(broken, key, decoded), "Envelope: tampered ciphertext rejected");

    TachometerModel tach({}, 9);
    double tach_max = 0.0;
    for (int rpm = 300; rpm <= 6000; rpm += 100)
        tach_max = std::max(tach_max, std::abs(tach.measure(rpm) - rpm));
    check(tach_max < 2.0, "Tachometer: error < 2 RPM in 300..6000",
          "max=" + std::to_string(tach_max) + " RPM");
    check(tach.measure(0.0) == 0.0, "Tachometer: zero RPM on timeout");

    MovingAverage ma(4);
    const double a = ma.process(1), b = ma.process(2), c = ma.process(3), d = ma.process(4);
    (void)a; (void)b; (void)c;
    check(std::abs(d - 2.5) < 1e-12, "FIR: mean(1..4) = 2.5", "y=" + std::to_string(d));

    EmaFilter ema(7);
    check(std::abs(ema.alpha() - 0.25) < 1e-12, "EMA: alpha = 0.25 for N=7");
    for (int i = 0; i < 200; ++i) ema.process(50.0);
    check(std::abs(ema.process(50.0) - 50.0) < 1e-6, "EMA: converges to 50 C");

    const double fir_db = fir_gain_db(49.5, 4, 100.0);
    check(fir_db < -30.0, "FIR: attenuation at 49.5 Hz < -30 dB",
          "H=" + std::to_string(fir_db) + " dB");
    const double total_db = analog_filter_gain_db(49.5) + fir_db + ema_gain_db(49.5);
    check(total_db < -55.0, "Channel: total attenuation at 49.5 Hz < -55 dB",
          "H=" + std::to_string(total_db) + " dB");

    // end-to-end noise
    BoardModel sim(123);
    FilterPipeline fp;
    double se_raw = 0.0, se_filt = 0.0;
    int n = 0;
    for (int i = 0; i < 12000; ++i) {
        const double t = i * 0.01;
        const BoardSample x = sim.sample(t, 0.01);
        const double y = fp.process(x.t_in_meas);
        if (i > 1000) {
            se_raw += (x.t_in_meas - x.t_in_true) * (x.t_in_meas - x.t_in_true);
            se_filt += (y - x.t_in_true) * (y - x.t_in_true);
            ++n;
        }
    }
    const double sko_raw = std::sqrt(se_raw / n);
    const double sko_filt = std::sqrt(se_filt / n);
    check(sko_filt < 0.15, "End-to-end: SKO after filtering < 0.15 C",
          "raw=" + std::to_string(sko_raw) + " filt=" + std::to_string(sko_filt));

    std::printf("=== %d passed, %d failed ===\n", g_pass, g_fail);
}

void simulate(double seconds, const std::string& csv) {
    BoardModel board(123);
    FilterPipeline fp;
    const double dt = 0.01;
    const int n = static_cast<int>(seconds * 100.0);
    std::ofstream os(csv);
    os << "t,truth,raw,filtered,rpm_true,rpm_meas,quant_err\n";
    double se_raw = 0.0, se_filt = 0.0, max_raw = 0.0, max_filt = 0.0;
    int cnt = 0;
    std::uint32_t seq = 0;
    for (int i = 0; i < n; ++i) {
        const double t = i * dt;
        const BoardSample s = board.sample(t, dt);
        const TelemetryFrame f = board.make_frame(s, seq++, static_cast<std::uint32_t>(t * 1000.0));
        const double y = fp.process(s.t_in_meas);
        if (i > 1000) {
            se_raw += (s.t_in_meas - s.t_in_true) * (s.t_in_meas - s.t_in_true);
            se_filt += (y - s.t_in_true) * (y - s.t_in_true);
            max_raw = std::max(max_raw, std::abs(s.t_in_meas - s.t_in_true));
            max_filt = std::max(max_filt, std::abs(y - s.t_in_true));
            ++cnt;
        }
        os << std::fixed << std::setprecision(4) << t << ',' << s.t_in_true << ','
           << s.t_in_meas << ',' << y << ',' << s.rpm1_true << ',' << s.rpm1_meas << ','
           << (s.t_in_meas - s.t_in_true) << '\n';
        (void)f;
    }
    const double sko_raw = std::sqrt(se_raw / cnt);
    const double sko_filt = std::sqrt(se_filt / cnt);
    std::printf("simulate: %d samples, %.0f s\n", n, seconds);
    std::printf("raw:      SKO %.4f C, max %.4f C\n", sko_raw, max_raw);
    std::printf("filtered: SKO %.4f C, max %.4f C\n", sko_filt, max_filt);
    std::printf("noise attenuation: %.2f dB\n", 20.0 * std::log10(sko_raw / sko_filt));
    std::printf("csv: %s\n", csv.c_str());
}

void quant_sweep(const std::string& csv) {
    NtcChannel ntc;
    Ads1115Config cfg;
    cfg.noise_uv = 0.0;
    cfg.inl_lsb = 0.0;
    std::ofstream os(csv);
    os << "t,lsb_c,quant_err\n";
    double max_lsb = 0.0, max_err = 0.0, sum2 = 0.0;
    int n = 0;
    for (double t = 0.0; t <= 110.0; t += 1.0) {
        Ads1115Model adc(cfg, 1);
        const double v = ntc.voltage(t);
        const auto code = adc.convert(v);
        const double meas = ntc.temperature(adc.code_to_voltage(code));
        const double lsb_c = std::abs(ntc.temperature(v + adc.lsb_voltage()) -
                                      ntc.temperature(v));
        const double err = meas - t;
        max_lsb = std::max(max_lsb, lsb_c);
        max_err = std::max(max_err, std::abs(err));
        sum2 += err * err;
        ++n;
        os << std::fixed << std::setprecision(5) << t << ',' << lsb_c << ',' << err << '\n';
    }
    std::printf("quantization: max LSB %.4f C, max error %.4f C, RMS %.4f C\n",
                max_lsb, max_err, std::sqrt(sum2 / n));
    std::printf("csv: %s\n", csv.c_str());
}

void tach_sweep(const std::string& csv) {
    std::ofstream os(csv);
    os << "rpm,err_mean,err_absmax,err_rms\n";
    double worst = 0.0;
    for (int rpm = 0; rpm <= 6000; rpm += 100) {
        TachometerModel tach({}, 9);
        double sum = 0.0, maxe = 0.0, sum2 = 0.0;
        const int trials = 500;
        for (int i = 0; i < trials; ++i) {
            const double m = tach.measure(rpm);
            const double e = m - rpm;
            sum += e;
            sum2 += e * e;
            maxe = std::max(maxe, std::abs(e));
        }
        const double mean = sum / trials;
        const double rms = std::sqrt(sum2 / trials);
        worst = std::max(worst, maxe);
        os << rpm << ',' << std::fixed << std::setprecision(4) << mean << ',' << maxe << ',' << rms << '\n';
    }
    std::printf("tachometer: worst |error| = %.3f RPM in 0..6000\n", worst);
    std::printf("csv: %s\n", csv.c_str());
}

void latency_run(int frames, const std::string& csv) {
    LatencyModel lm(7);
    std::vector<double> v;
    v.reserve(frames);
    double sum_adc = 0, sum_build = 0, sum_crypto = 0, sum_uart = 0, sum_host = 0, sum_queue = 0;
    for (int i = 0; i < frames; ++i) {
        const LatencySample s = lm.next();
        v.push_back(s.total_ms);
        sum_adc += s.adc_ms; sum_build += s.build_ms; sum_crypto += s.crypto_ms;
        sum_uart += s.uart_ms; sum_host += s.host_ms; sum_queue += s.queue_ms;
    }
    std::sort(v.begin(), v.end());
    auto pct = [&](double p) { return v[static_cast<std::size_t>(p * (v.size() - 1))]; };
    std::printf("latency: n=%d p50=%.3f p95=%.3f p99=%.3f max=%.3f ms\n",
                frames, pct(0.50), pct(0.95), pct(0.99), v.back());
    std::printf("components mean: adc=%.3f build=%.3f crypto=%.3f uart=%.3f host=%.3f queue=%.3f ms\n",
                sum_adc / frames, sum_build / frames, sum_crypto / frames,
                sum_uart / frames, sum_host / frames, sum_queue / frames);
    std::printf("measurement latency with DSP group delay: p50=%.1f ms\n",
                pct(0.50) + 7.5 + 45.0);

    const double bin = 0.1;
    const double lo = 0.0, hi = 12.0;
    const int nb = static_cast<int>((hi - lo) / bin);
    std::vector<int> hist(nb, 0);
    for (double x : v) {
        int b = static_cast<int>((x - lo) / bin);
        if (b >= 0 && b < nb) ++hist[b];
    }
    std::ofstream os(csv);
    os << "bin_ms,count\n";
    for (int i = 0; i < nb; ++i)
        os << std::fixed << std::setprecision(2) << (lo + (i + 0.5) * bin) << ',' << hist[i] << '\n';
    std::printf("csv: %s\n", csv.c_str());
}

void freq_report() {
    const double a = analog_filter_gain_db(49.5);
    const double f = fir_gain_db(49.5);
    const double e = ema_gain_db(49.5);
    std::printf("analog 2nd order fc=30 Hz: %.2f dB at 49.5 Hz\n", a);
    std::printf("FIR N=4 fs=100 Hz:        %.2f dB\n", f);
    std::printf("EMA N=7 fs=100 Hz:        %.2f dB\n", e);
    std::printf("total:                    %.2f dB\n", a + f + e);
    std::printf("group delay: analog %.2f ms, FIR %.1f ms, EMA %.1f ms, total %.1f ms\n",
                7.5, 15.0, 30.0, 52.5);
}

void demo_frame() {
    BoardModel board(11);
    const BoardSample s = board.sample(30.0, 0.01);
    TelemetryFrame f = board.make_frame(s, 1234, 30000);
    const auto key = test_key();
    const auto cf = encode_frame(f, key, 0x01020304, 42);
    std::printf("frame: %s\n", hex(cf.bytes.data(), cf.bytes.size()).c_str());
    std::printf("plain: T_in=%.2f C T_out=%.2f C T_amb=%.2f C RPM1=%.1f RPM2=%.1f U=%.3f V I=%.3f A\n",
                f.t_in_c_x100 / 100.0, f.t_out_c_x100 / 100.0, f.t_amb_c_x100 / 100.0,
                static_cast<double>(f.rpm1), static_cast<double>(f.rpm2),
                f.u_pump_mv / 1000.0, f.i_pump_ma / 1000.0);
    std::printf("cipher frame size: %zu bytes (payload 24 + tag 16 + nonce 12 + header 8)\n",
                cf.bytes.size());
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2) {
            std::printf("usage: ctm_hw --selftest | --simulate SEC [--csv FILE] | --quant [--csv FILE] | --tach [--csv FILE] | --latency N [--csv FILE] | --freq | --demo-frame\n");
            return 1;
        }
        const std::string cmd = argv[1];
        std::string csv = "out.csv";
        for (int i = 2; i + 1 < argc; ++i)
            if (std::string(argv[i]) == "--csv") csv = argv[i + 1];

        if (cmd == "--selftest") selftest();
        else if (cmd == "--simulate") simulate(argc > 2 ? std::atof(argv[2]) : 120.0, csv);
        else if (cmd == "--quant") quant_sweep(csv);
        else if (cmd == "--tach") tach_sweep(csv);
        else if (cmd == "--latency") latency_run(argc > 2 ? std::atoi(argv[2]) : 360000, csv);
        else if (cmd == "--freq") freq_report();
        else if (cmd == "--demo-frame") demo_frame();
        else {
            std::printf("unknown command: %s\n", cmd.c_str());
            return 1;
        }
    } catch (const std::exception& e) {
        std::printf("error: %s\n", e.what());
        return 2;
    }
    return g_fail == 0 ? 0 : 3;
}
