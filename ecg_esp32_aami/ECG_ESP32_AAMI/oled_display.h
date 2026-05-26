#ifndef OLED_DISPLAY_H
#define OLED_DISPLAY_H

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <time.h>
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
// 128 cột → 128 điểm. Lưu GIÁ TRỊ RAW (float), auto-scale lúc vẽ theo
// min/max của cửa sổ → sóng luôn lấp đầy 32px dù biên độ tín hiệu nhỏ.
#define WAVE_Y0    16   // vị trí Y bắt đầu vẽ sóng (bên dưới header)
#define WAVE_H     32   // chiều cao vùng sóng (pixel)
#define WAVE_COLS  OLED_WIDTH  // 128 cột

static float wave_val[WAVE_COLS];   // giá trị raw của từng cột
static int   wave_head = 0;         // con trỏ vòng tròn

// ── Lấy chuỗi giờ:phút hiện tại ────────────────────────
static void _getTimeStr(char* buf, size_t len) {
    struct tm ti;
    if (getLocalTime(&ti)) {
        snprintf(buf, len, "%02d:%02d", ti.tm_hour, ti.tm_min);
    } else {
        snprintf(buf, len, "--:--");
    }
}

// ── Hiển thị boot screen ────────────────────────────────
void oledBoot() {
    // Blink 3 lần khi power on
    for (int i = 0; i < 3; i++) {
        oled.clearDisplay();
        oled.fillRect(0, 0, OLED_WIDTH, OLED_HEIGHT, SSD1306_WHITE);
        oled.display();
        delay(120);
        oled.clearDisplay();
        oled.display();
        delay(80);
    }

    // Hiện title + đường kẻ
    oled.clearDisplay();
    oled.setTextSize(1);
    oled.setTextColor(SSD1306_WHITE);
    oled.setCursor(20, 8);
    oled.print("ECG ESP32");
    oled.drawFastHLine(0, 18, OLED_WIDTH, SSD1306_WHITE);
    oled.display();
    delay(400);

    // Hiệu ứng "Initializing . . ." chấm nhấp nháy
    for (int dots = 1; dots <= 3; dots++) {
        oled.fillRect(0, 28, OLED_WIDTH, 10, SSD1306_BLACK);
        oled.setCursor(20, 28);
        oled.print("Initializing");
        for (int d = 0; d < dots; d++) oled.print(" .");
        oled.display();
        delay(400);
    }

    // Chuyển sang "Connecting WiFi"
    oled.fillRect(0, 28, OLED_WIDTH, 10, SSD1306_BLACK);
    oled.setCursor(16, 28);
    oled.print("Connecting WiFi");
    oled.display();
}

// ── Hiển thị WiFi OK ────────────────────────────────────
void oledWifiOK(const char* ip) {
    oled.fillRect(0, 28, OLED_WIDTH, 20, SSD1306_BLACK);
    oled.setCursor(28, 28);
    oled.print("WiFi  OK!");
    oled.setCursor(16, 40);
    oled.print(ip);
    oled.display();
    delay(1500);
}

// ── Hiển thị MQTT connecting (1 frame hoạt ảnh) ─────────
static int _mqtt_anim = 0;
void oledMqttConnecting() {
    const char* frames[] = { "|", "/", "-", "\\" };
    oled.fillRect(0, 0, OLED_WIDTH, OLED_HEIGHT, SSD1306_BLACK);

    // Title
    oled.setCursor(20, 8);
    oled.print("ECG ESP32");
    oled.drawFastHLine(0, 18, OLED_WIDTH, SSD1306_WHITE);

    // Giờ góc phải title
    char tstr[6];
    _getTimeStr(tstr, sizeof(tstr));
    oled.setCursor(98, 8);
    oled.print(tstr);

    // Text connecting
    oled.setCursor(16, 26);
    oled.print("Connecting MQTT");

    // Spinner giữa màn hình
    oled.setCursor(60, 38);
    oled.print(frames[_mqtt_anim % 4]);
    _mqtt_anim++;

    // Thanh tiến trình chạy từ trái sang rồi reset
    int bar_w = (_mqtt_anim * 6) % (OLED_WIDTH + 1);
    oled.drawFastHLine(0, 50, bar_w, SSD1306_WHITE);

    oled.display();
    delay(150);
}

// ── Hiển thị MQTT OK ────────────────────────────────────
void oledMqttOK() {
    oled.fillRect(0, 28, OLED_WIDTH, 20, SSD1306_BLACK);
    oled.setCursor(28, 28);
    oled.print("MQTT  OK!");
    oled.display();
    delay(1000);
}

// ── Khởi tạo OLED ───────────────────────────────────────
bool setupOLED() {
    Wire.begin(OLED_SDA, OLED_SCL);
    if (!oled.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println("[OLED] Init FAILED — check wiring/address");
        return false;
    }
    oled.setTextColor(SSD1306_WHITE);
    for (int i = 0; i < WAVE_COLS; i++) wave_val[i] = 0.5f;   // baseline
    oledBoot();
    return true;
}

// ── Push 1 sample vào scroll buffer ─────────────────────
// val: giá trị raw bất kỳ (vd ADC/4095). Việc scale để lúc vẽ tự lo.
void oledPushSample(float val) {
    wave_val[wave_head] = val;
    wave_head = (wave_head + 1) % WAVE_COLS;
}

// ── Vẽ toàn bộ màn hình ─────────────────────────────────
// label      : "Normal", "Ventricular", ... hoặc "Leads Off"
// conf       : confidence [0..1]
// leads_on   : trạng thái dây điện cực
// bpm        : nhịp tim ước tính (0 nếu chưa có)
// spo2       : SpO2 % từ MAX30102 (0 nếu chưa đặt ngón tay / chưa hợp lệ)
void oledDraw(const char* label, float conf, bool leads_on, int bpm, int spo2) {
    oled.clearDisplay();

    // ── Hàng 1: label + giờ ─────────────────────────────
    oled.setTextSize(1);
    oled.setCursor(0, 0);
    if (!leads_on) {
        oled.print("!! LEADS OFF !!");
    } else {
        char short_label[12];
        strncpy(short_label, label, 11);
        short_label[11] = '\0';
        oled.print(short_label);

        // Confidence
        char conf_str[8];
        snprintf(conf_str, sizeof(conf_str), "%3d%%", (int)(conf * 100));
        oled.setCursor(OLED_WIDTH - 24, 0);
        oled.print(conf_str);
    }

    // ── Hàng 2: HR + giờ ────────────────────────────────
    oled.setCursor(0, 9);
    if (leads_on && bpm > 0) {
        oled.printf("HR: %3d bpm", bpm);
    } else if (leads_on) {
        oled.print("HR: --- bpm");
    }

    // Giờ:phút góc phải hàng 2
    char tstr[6];
    _getTimeStr(tstr, sizeof(tstr));
    oled.setCursor(OLED_WIDTH - 30, 9);
    oled.print(tstr);

    // ── Đường kẻ ngang phân cách ────────────────────────
    oled.drawFastHLine(0, WAVE_Y0 - 1, OLED_WIDTH, SSD1306_WHITE);

    // ── Vẽ sóng ECG (scroll) với auto-scale ─────────────
    // Tìm min/max của cửa sổ 128 cột → giãn sóng lấp đầy 32px. Nhờ vậy ECG
    // biên độ nhỏ (dao động hẹp quanh giữa thang ADC) vẫn hiện rõ dạng sóng,
    // không bị nén phẳng như khi map cố định [0,1].
    float vmin = wave_val[0], vmax = wave_val[0];
    for (int i = 1; i < WAVE_COLS; i++) {
        if (wave_val[i] < vmin) vmin = wave_val[i];
        if (wave_val[i] > vmax) vmax = wave_val[i];
    }
    float range = vmax - vmin;
    if (range < 1e-4f) range = 1e-4f;   // tránh chia 0 khi tín hiệu phẳng

    // Map 1 giá trị raw → pixel row (đảo: max→đỉnh, min→đáy)
    auto mapY = [&](float v) -> int {
        int y = (int)((1.0f - (v - vmin) / range) * (WAVE_H - 1));
        if (y < 0) y = 0;
        if (y >= WAVE_H) y = WAVE_H - 1;
        return WAVE_Y0 + y;
    };

    // Nối điểm bằng drawLine (oscilloscope-style), tránh chấm li ti.
    int prev_y = mapY(wave_val[wave_head % WAVE_COLS]);
    for (int col = 1; col < WAVE_COLS; col++) {
        int idx = (wave_head + col) % WAVE_COLS;
        int py  = mapY(wave_val[idx]);
        oled.drawLine(col - 1, prev_y, col, py, SSD1306_WHITE);
        prev_y = py;
    }

    // ── Hàng dưới cùng: SpO2 (trái) + giờ:phút:giây (phải) ──
    oled.setCursor(0, 57);
    if (spo2 > 0) {
        oled.printf("SpO2:%d%%", spo2);
    } else {
        oled.print("SpO2:--");
    }

    struct tm ti;
    oled.setCursor(OLED_WIDTH - 48, 57);
    if (getLocalTime(&ti)) {
        oled.printf("%02d:%02d:%02d", ti.tm_hour, ti.tm_min, ti.tm_sec);
    } else {
        oled.print("--:--:--");
    }

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
