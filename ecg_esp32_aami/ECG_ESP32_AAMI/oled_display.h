#ifndef OLED_DISPLAY_H
#define OLED_DISPLAY_H

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "config.h"

// ── OLED config ─────────────────────────────────────────
#define OLED_WIDTH   128
#define OLED_HEIGHT   64
#define OLED_RESET    -1   // dùng chung reset với ESP32

// I2C pins ESP32 mặc định: SDA=21, SCL=22
// Đổi nếu dùng pin khác: Wire.begin(SDA_PIN, SCL_PIN) trong setupOLED()
#define OLED_SDA 21 // GPIO 21
#define OLED_SCL 22 // GPIO 22

static Adafruit_SSD1306 oled(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);

// ── Waveform scroll buffer ───────────────────────────────
// 128 cột → 128 điểm, mỗi điểm là 1 sample thu nhỏ vào [0, WAVE_H-1]
#define WAVE_Y0    16   // vị trí Y bắt đầu vẽ sóng (bên dưới header)
#define WAVE_H     32   // chiều cao vùng sóng (pixel)
#define WAVE_COLS  OLED_WIDTH  // 128 cột

static uint8_t wave_col[WAVE_COLS];   // giá trị Y của từng cột
static int     wave_head = 0;         // con trỏ vòng tròn

// ── Hiển thị boot screen ────────────────────────────────
void oledBoot() {
    oled.clearDisplay();
    oled.setTextSize(1);
    oled.setTextColor(SSD1306_WHITE);
    oled.setCursor(20, 10);
    oled.print("ECG ESP32 AAMI");
    oled.setCursor(28, 26);
    oled.print("Connecting...");
    oled.display();
}

// ── Khởi tạo OLED ───────────────────────────────────────
bool setupOLED() {
    Wire.begin(OLED_SDA, OLED_SCL);
    if (!oled.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println("[OLED] Init FAILED — check wiring/address");
        return false;
    }
    oled.setTextColor(SSD1306_WHITE);
    memset(wave_col, WAVE_H / 2, sizeof(wave_col));
    oledBoot();
    return true;
}

// ── Push 1 sample vào scroll buffer ─────────────────────
// val: float trong khoảng [0.0, 1.0] (raw ADC đã normalize)
void oledPushSample(float val) {
    // Map val → pixel row trong vùng sóng (đảo ngược: 0.0 → đáy, 1.0 → đỉnh)
    int y = (int)((1.0f - val) * (WAVE_H - 1));
    if (y < 0) y = 0;
    if (y >= WAVE_H) y = WAVE_H - 1;

    wave_col[wave_head] = (uint8_t)y;
    wave_head = (wave_head + 1) % WAVE_COLS;
}

// ── Vẽ toàn bộ màn hình ─────────────────────────────────
// label      : "Normal", "Ventricular", ... hoặc "Leads Off"
// conf       : confidence [0..1]
// leads_on   : trạng thái dây điện cực
// bpm        : nhịp tim ước tính (0 nếu chưa có)
void oledDraw(const char* label, float conf, bool leads_on, int bpm) {
    oled.clearDisplay();

    // ── Hàng 1: label + confidence ──────────────────────
    oled.setTextSize(1);
    oled.setCursor(0, 0);
    if (!leads_on) {
        oled.print("!! LEADS OFF !!");
    } else {
        // Rút ngắn label nếu dài (Supraventricular → Supra)
        char short_label[12];
        strncpy(short_label, label, 11);
        short_label[11] = '\0';
        oled.print(short_label);

        // Confidence ở cạnh phải
        char conf_str[8];
        snprintf(conf_str, sizeof(conf_str), "%3d%%", (int)(conf * 100));
        oled.setCursor(OLED_WIDTH - 24, 0);
        oled.print(conf_str);
    }

    // ── Hàng 2: HR (nhịp tim) ───────────────────────────
    oled.setCursor(0, 9);
    if (leads_on && bpm > 0) {
        oled.printf("HR: %3d bpm", bpm);
    } else if (leads_on) {
        oled.print("HR: --- bpm");
    }

    // ── Đường kẻ ngang phân cách ────────────────────────
    oled.drawFastHLine(0, WAVE_Y0 - 1, OLED_WIDTH, SSD1306_WHITE);

    // ── Vẽ sóng ECG (scroll) ────────────────────────────
    for (int col = 0; col < WAVE_COLS; col++) {
        // Lấy dữ liệu theo thứ tự thời gian (oldest → newest = trái → phải)
        int idx = (wave_head + col) % WAVE_COLS;
        int py  = WAVE_Y0 + wave_col[idx];
        oled.drawPixel(col, py, SSD1306_WHITE);
    }

    // ── Hàng 4: timestamp (ms) ──────────────────────────
    oled.setCursor(0, 57);
    oled.printf("t=%lums", millis());

    oled.display();
}

// ── Ước tính BPM từ khoảng cách giữa 2 lần process_beat ─
// Gọi mỗi khi detect được R-peak mới
static unsigned long _last_beat_ms = 0;
static int           _last_bpm     = 0;

int oledUpdateBPM() {
    unsigned long now = millis();
    if (_last_beat_ms > 0 && (now - _last_beat_ms) > 200) {  // tránh nhiễu
        _last_bpm = (int)(60000UL / (now - _last_beat_ms));
        // Clamp physiological range
        if (_last_bpm < 30 || _last_bpm > 220) _last_bpm = 0;
    }
    _last_beat_ms = now;
    return _last_bpm;
}

#endif // OLED_DISPLAY_H
