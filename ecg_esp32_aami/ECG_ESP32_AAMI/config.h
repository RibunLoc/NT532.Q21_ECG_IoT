#ifndef CONFIG_H
#define CONFIG_H

#define WIFI_SSID "XemThua"
#define WIFI_PASSWORD "thanhloc123"

#define MQTT_BROKER  "192.168.137.1"
#define MQTT_PORT 1883
#define MQTT_CLIENT "ecg-device-001" // Phải khớp trên cloud Dynamodb

#define TOPIC_RESULT "ecg/result"
#define TOPIC_RAW "ecg/raw"

#define ECG_PIN  35 // Analog Input (doi 34->35 loai tru D34 loi; D35 cung ADC1 input-only)
#define LO_PLUS  32 // Leads-off Detection +
#define LO_MINUS 33 // Lead-off Detection -

// ── 3 LED bao trang thai phan loai (anode qua tro 220-330 ohm -> GPIO, cathode -> GND) ──
#define LED_GREEN  27 // D27 — Normal (binh thuong)
#define LED_YELLOW 13 // D13 — SVE / Fusion (luu y)
#define LED_RED    14 // D14 — VEB / Unknown (nguy hiem)

// ── 2 coi bao dong ──
#define BUZZER_DANGER 4  // D4  — 9056-TS PASSIVE (dung tone()) — bip nhip tim + bao VEB
#define BUZZER_WARN   19 // D19 — 1206 ACTIVE — bao SVE/luu y (D15 strapping ->keu khi boot; D19 an toan)
#define BUZZER_DANGER_FREQ 2500 // Hz — tan so coi nguy hiem (tit choi tai)
#define BEEP_FREQ          2000 // Hz — tan so bip nhip tim (nhe hon)
#define BEEP_DURATION_MS     60 // do dai 1 tieng bip nhip tim

#define SAMPLE_COUNT 187
#define SAMPLE_RATE 360 // Hz => delay 540 microseccond
#define SAMPLE_INTERVAL_US 2778 // 1_000_000 / 360

// Buffer dài hơn detect R-peak rồi cắt +-93 quanh peak
#define BUFFER_SIZE 540 // ~1.5 giây = đủ chứa 1-2 nhịp tim
#define HALF_WINDOW 93 // 187/2, dùng để cắt window quanh R-peak

#define EDGE_CONFIDENCE_THRESHOLD 0.8f // Edge confident nếu conf >= 0.8
#define VEB_THRESHOLD 0.2f // Threshold tuning cho VEB (từ notebook)

// ── DEMO MODE — test khi chưa có AD8232 ──
// 1: bỏ qua check leads_off + dùng fake ECG signal
// 0: dùng AD8232 thật
#define DEMO_MODE 1

#endif