// Прошивка измерительного модуля ESP32-WROOM-32 (фрагмент)
// Сборка: PlatformIO, framework = arduino, board = esp32dev
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include <Adafruit_INA219.h>
#include <mbedtls/gcm.h>
#include <esp_timer.h>

namespace ctm {

// В учебной модели ключ задаётся константой; в production он хранится
// в eFuse/NVS и не попадает в исходный код.
static const unsigned char kKey[32] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F
};

constexpr uint8_t  kPinTach1 = 34;
constexpr uint8_t  kPinTach2 = 35;
constexpr uint8_t  kPinPwm1  = 25;
constexpr uint8_t  kPinPwm2  = 26;
constexpr uint32_t kFramePeriodUs = 10000;   // 100 Гц

#pragma pack(push, 1)
struct TelemetryFrame {
    uint16_t sync = 0xAA55;
    uint8_t  version = 1;
    uint8_t  flags = 0;
    uint32_t seq = 0;
    uint32_t timestamp_ms = 0;
    int16_t  t_in_c_x100 = 0;
    int16_t  t_out_c_x100 = 0;
    int16_t  t_amb_c_x100 = 0;
    uint16_t rpm1 = 0;
    uint16_t rpm2 = 0;
    uint16_t u_pump_mv = 0;
    uint16_t i_pump_ma = 0;
    uint8_t  pwm1 = 0;
    uint8_t  pwm2 = 0;
    uint16_t status = 0;
    uint16_t crc = 0;
};
#pragma pack(pop)

Adafruit_ADS1115 ads;
Adafruit_INA219 ina(0x40);
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
volatile uint32_t tach1_us = 0, tach2_us = 0;
volatile uint32_t tach1_dt = 0, tach2_dt = 0;

uint16_t crc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (int b = 0; b < 8; ++b)
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
    }
    return crc;
}

void IRAM_ATTR onTach1() {
    const uint32_t now = micros();
    portENTER_CRITICAL_ISR(&mux);
    tach1_dt = now - tach1_us;
    tach1_us = now;
    portEXIT_CRITICAL_ISR(&mux);
}

void IRAM_ATTR onTach2() {
    const uint32_t now = micros();
    portENTER_CRITICAL_ISR(&mux);
    tach2_dt = now - tach2_us;
    tach2_us = now;
    portEXIT_CRITICAL_ISR(&mux);
}

float ntcTemperature(float volts) {
    constexpr float kR25 = 10000.0f, kB = 3950.0f, kRtop = 10000.0f, kVs = 3.3f;
    const float r = kRtop * volts / (kVs - volts);
    const float inv = 1.0f / 298.15f + logf(r / kR25) / kB;
    return 1.0f / inv - 273.15f;
}

uint16_t rpmFromPeriod(uint32_t dt_us, uint8_t pulses_per_rev = 2) {
    if (dt_us == 0 || dt_us > 500000UL) return 0;   // стоянка / таймаут 0,5 с
    return static_cast<uint16_t>(60000000UL / (static_cast<uint32_t>(pulses_per_rev) * dt_us));
}

} // namespace ctm

using namespace ctm;

void setup() {
    Serial.begin(921600);
    Wire.begin(21, 22, 400000);
    ads.begin();
    ads.setGain(GAIN_ONE);            // ±4,096 В, LSB 125 мкВ
    ads.setDataRate(RATE_ADS1115_860SPS);
    ina.begin();
    pinMode(kPinTach1, INPUT_PULLUP);
    pinMode(kPinTach2, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(kPinTach1), onTach1, FALLING);
    attachInterrupt(digitalPinToInterrupt(kPinTach2), onTach2, FALLING);
    ledcSetup(0, 25000, 8);
    ledcSetup(1, 25000, 8);
    ledcAttachPin(kPinPwm1, 0);
    ledcAttachPin(kPinPwm2, 1);
}

void loop() {
    static uint32_t seq = 0;
    static uint64_t nonce_counter = 0;
    static int64_t next_us = esp_timer_get_time();

    next_us += kFramePeriodUs;
    const int64_t now = esp_timer_get_time();
    if (now > next_us + kFramePeriodUs / 2) next_us = now;

    TelemetryFrame f;
    f.seq = seq++;
    f.timestamp_ms = static_cast<uint32_t>(millis());
    f.t_in_c_x100  = static_cast<int16_t>(ntcTemperature(ads.readADC_SingleEnded(0) * 0.125e-3f) * 100);
    f.t_out_c_x100 = static_cast<int16_t>(ntcTemperature(ads.readADC_SingleEnded(1) * 0.125e-3f) * 100);
    f.t_amb_c_x100 = static_cast<int16_t>(ntcTemperature(ads.readADC_SingleEnded(2) * 0.125e-3f) * 100);

    portENTER_CRITICAL(&mux);
    const uint32_t dt1 = tach1_dt, dt2 = tach2_dt;
    portEXIT_CRITICAL(&mux);
    f.rpm1 = rpmFromPeriod(dt1);
    f.rpm2 = rpmFromPeriod(dt2);

    f.u_pump_mv = static_cast<uint16_t>(ina.getBusVoltage_V() * 1000.0f);
    f.i_pump_ma = static_cast<uint16_t>(ina.getCurrent_mA());
    f.pwm1 = static_cast<uint8_t>(ledcRead(0) * 100U / 255U);
    f.pwm2 = static_cast<uint8_t>(ledcRead(1) * 100U / 255U);
    f.crc = crc16(reinterpret_cast<const uint8_t*>(&f), sizeof(f) - 2);

    uint8_t header[8];
    memcpy(header, &f, 8);
    uint8_t nonce[12];
    const uint32_t boot_id = 0x01020304;
    memcpy(nonce, &boot_id, 4);
    memcpy(nonce + 4, &nonce_counter, 8);
    ++nonce_counter;

    uint8_t sealed[40];
    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, kKey, 256);
    mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, 24, nonce, 12,
                              header, 8, reinterpret_cast<uint8_t*>(&f) + 8,
                              sealed, 16, sealed + 24);
    mbedtls_gcm_free(&gcm);

    Serial.write(header, 8);
    Serial.write(nonce, 12);
    Serial.write(sealed, 40);
    Serial.flush();

    while (esp_timer_get_time() < next_us) { delayMicroseconds(200); }
}
