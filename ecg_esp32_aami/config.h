#ifndef CONFIG_H
#define CONFIG_H

#define WIFI_SSID "ThaoNhi T3"
#define WIFI_PASSWORD "31121997"

#define MQTT_BROKER  "192.168.1.3"
#define MQTT_PORT 1883
#define MQTT_CLIENT "ecg-device-001" // Phải khớp trên cloud Dynamodb

#define TOPIC_RESULT "ecg/result"
#define TOPIC_RAW "ecg/raw"

#define ECG_PIN  34 // Analog Input
#define LO_PLUS  32 // Leads-off Detection +
#define LO_MINUS 33 // Lead-off Detection -

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