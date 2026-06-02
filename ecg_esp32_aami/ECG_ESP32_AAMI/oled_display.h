#ifndef OLED_DISPLAY_H
#define OLED_DISPLAY_H

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <time.h>
#include "config.h"

// ── OLED config (2 màn trên 2 bus I2C riêng) ────────────
// 2 OLED cùng địa chỉ 0x3C → KHÔNG chung 1 bus được. ESP32 có 2 bus phần
// cứng: Wire (OLED sóng + MAX30102) và Wire1 (OLED số liệu).
#define OLED_WIDTH   128
#define OLED_HEIGHT   64
#define OLED_RESET    -1
#define OLED_ADDR    0x3C

// Bus 1 (Wire): OLED sóng ECG + MAX30102
#define I2C1_SDA 21   // D21
#define I2C1_SCL 22   // D22
// Bus 2 (Wire1): OLED số liệu (SpO2/HR/label)
#define I2C2_SDA 25   // D25
#define I2C2_SCL 26   // D26

static Adafruit_SSD1306 oledWave(OLED_WIDTH, OLED_HEIGHT, &Wire,  OLED_RESET);  // màn sóng
static Adafruit_SSD1306 oledInfo(OLED_WIDTH, OLED_HEIGHT, &Wire1, OLED_RESET);  // màn số liệu

// ── Waveform scroll buffer ───────────────────────────────
#define WAVE_Y0    0    // sóng chiếm gần hết màn sóng
#define WAVE_H     64   // full chiều cao màn 1
#define WAVE_COLS  OLED_WIDTH

static float wave_val[WAVE_COLS];
static int   wave_head = 0;

// ── Lấy chuỗi giờ:phút ──────────────────────────────────
static void _getTimeStr(char* buf, size_t len) {
    struct tm ti;
    if (getLocalTime(&ti)) snprintf(buf, len, "%02d:%02d", ti.tm_hour, ti.tm_min);
    else                   snprintf(buf, len, "--:--");
}

// ── Boot screen (hiện trên CẢ 2 màn) ────────────────────
void oledBoot() {
    for (int i = 0; i < 2; i++) {
        oledWave.clearDisplay(); oledInfo.clearDisplay();
        oledWave.fillRect(0,0,OLED_WIDTH,OLED_HEIGHT,SSD1306_WHITE);
        oledInfo.fillRect(0,0,OLED_WIDTH,OLED_HEIGHT,SSD1306_WHITE);
        oledWave.display(); oledInfo.display(); delay(100);
        oledWave.clearDisplay(); oledInfo.clearDisplay();
        oledWave.display(); oledInfo.display(); delay(70);
    }
    oledWave.clearDisplay();
    oledWave.setTextSize(1); oledWave.setTextColor(SSD1306_WHITE);
    oledWave.setCursor(20, 20); oledWave.print("ECG ESP32");
    oledWave.drawFastHLine(0, 34, OLED_WIDTH, SSD1306_WHITE);
    oledWave.setCursor(16, 42); oledWave.print("Initializing...");
    oledWave.display();

    oledInfo.clearDisplay();
    oledInfo.setTextSize(1); oledInfo.setTextColor(SSD1306_WHITE);
    oledInfo.setCursor(10, 28); oledInfo.print("Vital signs");
    oledInfo.display();
}

// ── WiFi OK / MQTT (hiện trên màn sóng) ─────────────────
void oledWifiOK(const char* ip) {
    oledWave.clearDisplay();
    oledWave.setCursor(28, 20); oledWave.print("WiFi  OK!");
    oledWave.setCursor(8, 38);  oledWave.print(ip);
    oledWave.display(); delay(1200);
}

static int _mqtt_anim = 0;
void oledMqttConnecting() {
    const char* frames[] = { "|", "/", "-", "\\" };
    oledWave.clearDisplay();
    oledWave.setCursor(20, 16); oledWave.print("Connecting MQTT");
    oledWave.setCursor(60, 34); oledWave.print(frames[_mqtt_anim % 4]);
    _mqtt_anim++;
    int bar_w = (_mqtt_anim * 6) % (OLED_WIDTH + 1);
    oledWave.drawFastHLine(0, 48, bar_w, SSD1306_WHITE);
    oledWave.display(); delay(150);
}

void oledMqttOK() {
    oledWave.clearDisplay();
    oledWave.setCursor(28, 28); oledWave.print("MQTT  OK!");
    oledWave.display(); delay(800);
}

// ── Khởi tạo 2 OLED trên 2 bus ──────────────────────────
bool setupOLED() {
    Wire.begin(I2C1_SDA, I2C1_SCL);     // bus 1: OLED sóng + MAX30102
    Wire1.begin(I2C2_SDA, I2C2_SCL);    // bus 2: OLED số liệu

    bool ok1 = oledWave.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
    bool ok2 = oledInfo.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
    if (!ok1) Serial.println("[OLED-Wave] Init FAILED (bus1 D21/D22)");
    if (!ok2) Serial.println("[OLED-Info] Init FAILED (bus2 D25/D26)");

    oledWave.setTextColor(SSD1306_WHITE);
    oledInfo.setTextColor(SSD1306_WHITE);
    for (int i = 0; i < WAVE_COLS; i++) wave_val[i] = 0.5f;
    oledBoot();
    return ok1 && ok2;
}

// ── Push 1 sample vào scroll buffer ─────────────────────
void oledPushSample(float val) {
    wave_val[wave_head] = val;
    wave_head = (wave_head + 1) % WAVE_COLS;
}

// Lay N mau gan nhat theo thu tu thoi gian (cu->moi) cho dashboard.
// Dung CHUNG buffer wave_val nen song dashboard == song OLED.
int oledCopyLatestSamples(float* dst, int maxN) {
    int n = (maxN > WAVE_COLS) ? WAVE_COLS : maxN;
    int start = (wave_head - n + WAVE_COLS) % WAVE_COLS;
    for (int i = 0; i < n; i++) {
        dst[i] = wave_val[(start + i) % WAVE_COLS];
    }
    return n;
}

// ── MÀN 1: vẽ sóng ECG (full màn, auto-scale) ───────────
void oledDrawWave() {
    oledWave.clearDisplay();
    float vmin = wave_val[0], vmax = wave_val[0];
    for (int i = 1; i < WAVE_COLS; i++) {
        if (wave_val[i] < vmin) vmin = wave_val[i];
        if (wave_val[i] > vmax) vmax = wave_val[i];
    }
    float range = vmax - vmin;
    if (range < 1e-4f) range = 1e-4f;
    auto mapY = [&](float v) -> int {
        int y = (int)((1.0f - (v - vmin) / range) * (WAVE_H - 1));
        if (y < 0) y = 0; if (y >= WAVE_H) y = WAVE_H - 1;
        return WAVE_Y0 + y;
    };
    // Lọc trung bình 5 điểm khi vẽ → làm mượt nhiễu nền còn sót sau notch 50Hz,
    // vẫn giữ đỉnh R. Cửa sổ rộng hơn (5) cho nền sạch hơn (3).
    auto smooth = [&](int col) -> float {
        float s = 0;
        for (int k = -2; k <= 2; k++)
            s += wave_val[(wave_head + col + k + 2 * WAVE_COLS) % WAVE_COLS];
        return s / 5.0f;
    };
    int prev_y = mapY(smooth(0));
    for (int col = 1; col < WAVE_COLS; col++) {
        int py = mapY(smooth(col));
        oledWave.drawLine(col - 1, prev_y, col, py, SSD1306_WHITE);
        prev_y = py;
    }
    oledWave.display();
}

// ── MÀN 2: số liệu (label da chot, HR, SpO2, dem nguoc) ──
// countdown: so giay con lai toi lan chot ket qua tiep theo (0-10)
void oledDrawInfo(const char* label, float conf, bool leads_on, int bpm, int spo2, int countdown) {
    oledInfo.clearDisplay();
    oledInfo.setTextSize(1);

    // Dong 0: GIO HH:MM:SS (trai) — gio Viet Nam GMT+7
    struct tm ti;
    oledInfo.setCursor(0, 0);
    if (getLocalTime(&ti)) oledInfo.printf("%02d:%02d:%02d", ti.tm_hour, ti.tm_min, ti.tm_sec);
    else                   oledInfo.print("--:--:--");
    // conf goc phai dong 0
    if (leads_on) {
        char c[8]; snprintf(c, sizeof(c), "%3d%%", (int)(conf * 100));
        oledInfo.setCursor(OLED_WIDTH - 24, 0); oledInfo.print(c);
    }

    // Dong 1: label da chot
    oledInfo.setCursor(0, 10);
    if (!leads_on) {
        oledInfo.print("!! LEADS OFF !!");
    } else {
        char s[16]; strncpy(s, label, 15); s[15] = '\0';
        oledInfo.print(s);
    }
    oledInfo.drawFastHLine(0, 20, OLED_WIDTH, SSD1306_WHITE);

    // HR lon
    oledInfo.setTextSize(1); oledInfo.setCursor(0, 24); oledInfo.print("HR");
    oledInfo.setTextSize(2); oledInfo.setCursor(0, 33);
    if (leads_on && bpm > 0) oledInfo.printf("%3d", bpm); else oledInfo.print("---");
    oledInfo.setTextSize(1); oledInfo.setCursor(64, 39); oledInfo.print("bpm");

    // SpO2 lon
    oledInfo.setTextSize(1); oledInfo.setCursor(0, 52); oledInfo.print("SpO2");
    oledInfo.setTextSize(2); oledInfo.setCursor(40, 49);
    if (spo2 > 0) oledInfo.printf("%2d%%", spo2); else oledInfo.print("--");

    // Dem nguoc (goc phai duoi)
    oledInfo.setTextSize(1);
    oledInfo.setCursor(OLED_WIDTH - 30, 56);
    oledInfo.printf("%2ds", countdown);

    oledInfo.display();
}

// ── Ước tính BPM từ khoảng cách giữa 2 R-peak ───────────
static unsigned long _last_beat_ms = 0;
static int           _last_bpm     = 0;
int oledUpdateBPM() {
    unsigned long now = millis();
    if (_last_beat_ms > 0 && (now - _last_beat_ms) > 200) {
        _last_bpm = (int)(60000UL / (now - _last_beat_ms));
        if (_last_bpm < 30 || _last_bpm > 220) _last_bpm = 0;
    }
    _last_beat_ms = now;
    return _last_bpm;
}

#endif // OLED_DISPLAY_H
