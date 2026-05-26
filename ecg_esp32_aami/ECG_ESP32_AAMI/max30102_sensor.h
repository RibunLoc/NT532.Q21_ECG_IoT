#ifndef MAX30102_SENSOR_H
#define MAX30102_SENSOR_H

// ───────────────────────────────────────────────────────────
// MAX30102 — SpO2 + nhịp tim (PPG) qua I2C, chung bus với OLED
//
// Thư viện: SparkFun MAX3010x  (Library Manager: "SparkFun MAX3010x")
//
// Kiến trúc: ĐƠN GIẢN — đọc trong loop chính, KHÔNG dùng FreeRTOS task,
// KHÔNG mutex. Cùng một luồng với ECG/OLED nên không có tranh chấp bus
// hay watchdog crash. Đánh đổi: SpO2 cập nhật chậm hơn (vài giây/lần) vì
// loop bận sampling ECG, nhưng ổn định.
//
// Cách dùng:
//   - setupMAX30102()  : gọi 1 lần trong setup(), SAU Wire.begin() của OLED
//   - max30102Poll()   : gọi mỗi vòng loop(). Chỉ HÚT mẫu sẵn có trong FIFO
//                        rồi return ngay (non-blocking). Đủ 100 mẫu → tính.
//   - readSpO2HR(...)  : lấy kết quả SpO2/HR mới nhất
// ───────────────────────────────────────────────────────────

#include <Wire.h>
#include <math.h>
#include "MAX30105.h"
// KHÔNG dùng "spo2_algorithm.h" của SparkFun: hàm maxim_* ghi tràn mảng global
// nội bộ (an_x[100]/an_y[100], off-by-one ở valley loc) trong một số điều kiện
// tín hiệu → corrupt RAM → đè wifiClient global → crash LoadProhibited ở
// mqtt.connected(). Đã xác nhận: tắt MAX hết crash, stack/heap đều dư.
// Thay bằng ratio-of-ratios tự tính: nhẹ, không mảng global, không crash.

#define MAX_BUFFER_LEN  100   // cửa sổ tính SpO2: 100 mẫu @25Hz = 4 giây

static MAX30105 _maxSensor;
static bool     _maxPresent = false;

static uint32_t _irBuffer[MAX_BUFFER_LEN];
static uint32_t _redBuffer[MAX_BUFFER_LEN];
static int      _maxBufIdx = 0;

static int  _spo2  = 0;
static int  _hr    = 0;
static bool _valid = false;

// ── Init ─────────────────────────────────────────────────
bool setupMAX30102() {
    if (!_maxSensor.begin(Wire, I2C_SPEED_FAST)) {
        Serial.println("[MAX30102] Init FAILED — check wiring/3V3/address 0x57");
        _maxPresent = false;
        return false;
    }

    // QUAN TRỌNG: spo2_algorithm.h của SparkFun dùng FreqS=25Hz, BUFFER_SIZE=100
    // (= 4 giây @25Hz). PHẢI cho FIFO ra ĐÚNG 25Hz, nếu không thuật toán tính
    // valley locations sai → index rác → an_x[rác] ghi ngoài biên → corrupt RAM
    // → crash ở mqtt.connected() (đối tượng global kế bên bị đè). Đây là lý do
    // crash trước đó, KHÔNG phải MAX30102 hỏng.
    // FIFO = sampleRate/sampleAverage = 100/4 = 25Hz.
    byte ledBrightness = 0x24;   // ~36 (tránh IR bão hoà)
    byte sampleAverage = 4;
    byte ledMode       = 2;      // 2 = Red + IR (bắt buộc cho SpO2)
    int  sampleRate    = 100;    // Hz (FIFO ra 100/4 = 25Hz khớp FreqS thư viện)
    int  pulseWidth    = 411;    // µs
    int  adcRange      = 4096;

    _maxSensor.setup(ledBrightness, sampleAverage, ledMode,
                     sampleRate, pulseWidth, adcRange);
    _maxSensor.setPulseAmplitudeGreen(0);   // MAX30102 không có Green

    _maxPresent = true;
    Serial.println("[MAX30102] Init OK (0x57)");
    return true;
}

// ── Tính SpO2 bằng ratio-of-ratios (tự viết, không thư viện) ────
// R = (AC_red/DC_red) / (AC_ir/DC_ir);  SpO2 ≈ 110 - 25*R (hiệu chuẩn phổ biến)
// DC = trung bình, AC = RMS quanh trung bình (đại diện biên độ nhịp đập).
static void _computeSpO2() {
    // DC (trung bình) của IR và Red
    uint64_t irSum = 0, redSum = 0;
    uint32_t irMin = 0xFFFFFFFF, irMax = 0;
    for (int i = 0; i < MAX_BUFFER_LEN; i++) {
        irSum  += _irBuffer[i];
        redSum += _redBuffer[i];
        if (_irBuffer[i] < irMin) irMin = _irBuffer[i];
        if (_irBuffer[i] > irMax) irMax = _irBuffer[i];
    }
    double irDC  = (double)irSum  / MAX_BUFFER_LEN;
    double redDC = (double)redSum / MAX_BUFFER_LEN;
    uint32_t irAC_pp = irMax - irMin;   // biên độ đỉnh-đỉnh để lọc

    // AC (RMS) của IR và Red quanh DC
    double irAcc = 0, redAcc = 0;
    for (int i = 0; i < MAX_BUFFER_LEN; i++) {
        double di = (double)_irBuffer[i]  - irDC;
        double dr = (double)_redBuffer[i] - redDC;
        irAcc  += di * di;
        redAcc += dr * dr;
    }
    double irAC  = sqrt(irAcc  / MAX_BUFFER_LEN);
    double redAC = sqrt(redAcc / MAX_BUFFER_LEN);

    // Ratio-of-ratios → SpO2.  Hệ số empirical phổ biến cho MAX30102.
    int spo2 = 0;
    double R = 0;
    bool clean = irAC_pp > irDC / 300 && irAC_pp < irDC / 5;   // dải nhịp đập thật
    // Tính R luôn (kể cả chưa clean) để debug; chỉ nhận SpO2 khi clean.
    if (irDC > 50000 && irAC > 1 && redDC > 1) {
        R = (redAC / redDC) / (irAC / irDC);
        if (clean) {
            // Hiệu chuẩn cho module này: đo R≈1.40 lúc tay yên, người khỏe ~98%
            // → offset 121.8. Độ dốc 17 (chuẩn sinh lý). SpO2 = 121.8 - 17*R.
            spo2 = (int)lround(121.8 - 17.0 * R);
            if (spo2 > 100) spo2 = 100;   // clamp trần
        }
    }

    bool ok = spo2 >= 70 && spo2 <= 100;
    _valid = ok;
    _spo2  = ok ? spo2 : 0;
    // HR: dùng BPM từ ECG R-peak (chính xác hơn PPG); để 0 ở đây.
    _hr    = 0;

    Serial.printf("[MAX30102] irDC=%.0f redDC=%.0f irAC=%.1f redAC=%.1f R=%.3f SpO2=%d -> %s\n",
        irDC, redDC, irAC, redAC, R, spo2, ok ? "OK" : "skip");
}

// ── Poll non-blocking: gọi mỗi vòng loop() ──────────────
// CHỈ hút mẫu đang có trong FIFO rồi return ngay — KHÔNG spin-wait, KHÔNG
// chặn loop ECG. Đủ 100 mẫu → tính SpO2 → sliding window giữ 75 mẫu cuối.
void max30102Poll() {
    if (!_maxPresent) return;

    _maxSensor.check();   // nạp FIFO về thư viện (1 lần, không lặp)
    while (_maxSensor.available() && _maxBufIdx < MAX_BUFFER_LEN) {
        _redBuffer[_maxBufIdx] = _maxSensor.getRed();
        _irBuffer[_maxBufIdx]  = _maxSensor.getIR();
        _maxSensor.nextSample();
        _maxBufIdx++;
    }

    if (_maxBufIdx >= MAX_BUFFER_LEN) {
        _computeSpO2();
        // Sliding window: giữ 75 mẫu cuối, lấy 25 mẫu mới ở vòng sau.
        memmove(_irBuffer,  _irBuffer  + 25, 75 * sizeof(uint32_t));
        memmove(_redBuffer, _redBuffer + 25, 75 * sizeof(uint32_t));
        _maxBufIdx = 75;
    }
}

// ── Đọc kết quả mới nhất ────────────────────────────────
void readSpO2HR(int& spo2, int& hr, bool& valid) {
    spo2  = _spo2;
    hr    = _hr;
    valid = _valid;
}

// ── Phát hiện có ngón tay đặt trên cảm biến ─────────────
bool max30102FingerPresent() {
    if (!_maxPresent) return false;
    return _maxSensor.getIR() > 50000;
}

#endif // MAX30102_SENSOR_H
