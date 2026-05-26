// ───────────────────────────────────────────────────────────
// ECG ESP32 — AAMI Edge Inference + Cascade Publishing
//
// Pipeline:
//   ADC (360Hz) → buffer 540 → z-score → R-peak detect →
//   cat window 187 → CNN inference → publish MQTT
//
// Cascade logic:
//   Normal & conf >= 0.8  → publish 'ecg/result' (light, no raw)
//   Abnormal | conf < 0.8 → publish 'ecg/result' + 'ecg/raw'
//   Leads off             → publish 'ecg/result' status, no inference
// ───────────────────────────────────────────────────────────

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <string.h>           // memmove, memcpy
#include <time.h>
#include "soc/soc.h"           // tắt brownout detector
#include "soc/rtc_cntl_reg.h"

#include "config.h"
#include "ecg_weights_aami.h"
#include "normalize.h"
#include "r_peak_detector.h"
#include "inference.h"
#include "oled_display.h"

// ── MAX30102 (SpO2/HR). Đặt 0 để tắt khi debug. ──
#define ENABLE_MAX30102 0
#if ENABLE_MAX30102
#include "max30102_sensor.h"
#endif

#if DEMO_MODE
#include "fake_ecg.h"
#endif

// ── Globals ─────────────────────────────────────────────
WiFiClient    wifiClient;
PubSubClient  mqtt(wifiClient);

// OLED state (cập nhật mỗi khi có kết quả inference mới)
static const char* oled_label = "---";
static float       oled_conf  = 0.0f;
static int         oled_bpm   = 0;

// SpO2 + HR từ MAX30102 (cập nhật ~1 lần/giây qua max30102Poll)
static int  oled_spo2  = 0;
static int  oled_hr_ppg = 0;
static bool spo2_valid  = false;

// Refresh OLED mỗi ~100ms độc lập với sampling rate
static unsigned long last_oled_ms = 0;
const unsigned long OLED_REFRESH_MS = 40;   // 25 fps — sóng trôi mượt
// (100ms → mỗi lần vẽ nhảy ~36 cột/giật; 40ms → ~14 cột, đều hơn)

// Sliding buffer: sau moi inference, shift 270 samples cuoi len dau
static float  ecg_buffer[BUFFER_SIZE];        // 540 floats
static float  buf_copy[BUFFER_SIZE];          // copy de normalize, giu raw
static float  beat_window[SAMPLE_COUNT];      // 187 samples cho CNN
static int    sample_index = 0;
static bool   leads_on = false;

// Track thoi gian de publish leads_off khong qua nhanh
static unsigned long last_leads_off_publish = 0;
const unsigned long LEADS_OFF_INTERVAL_MS = 1000;   // 1 lan/giay

// ── Network Setup ───────────────────────────────────────
void connectWiFi() {
    Serial.printf("Connecting WiFi: %s\n", WIFI_SSID);
    Serial.println(">> wifi: mode STA");
    WiFi.persistent(false);     // không ghi config WiFi vào flash mỗi lần
    WiFi.mode(WIFI_STA);
    Serial.println(">> wifi: disconnect+reset radio");
    WiFi.disconnect(true);      // reset radio sạch
    delay(100);
    Serial.println(">> wifi: begin()");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.println(">> wifi: begin() returned, waiting...");

    // Timeout 20s thay vì kẹt vĩnh viễn. In trạng thái để chẩn đoán.
    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) {
        delay(500);
        Serial.printf(".(status=%d)", WiFi.status());
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\n WiFi OK! IP: %s  RSSI: %d dBm\n",
            WiFi.localIP().toString().c_str(), WiFi.RSSI());
    } else {
        // Không nối được — KHÔNG kẹt, vẫn chạy tiếp (chế độ offline).
        // status: 1=NO_SSID(không thấy mạng/5GHz), 4=CONNECT_FAILED(sai pass), 6=DISCONNECTED
        Serial.printf("\n[WiFi] FAIL status=%d — chay offline (OLED/ECG van chay, khong MQTT)\n",
            WiFi.status());
    }
}

void connectMQTT() {
    while (!mqtt.connected()) {
        Serial.print("Connecting MQTT...");
        // Chạy hoạt ảnh trong lúc chờ connect
        unsigned long t0 = millis();
        bool connected = false;
        while (millis() - t0 < 3000) {
            oledMqttConnecting();
            if (mqtt.connect(MQTT_CLIENT)) {
                connected = true;
                break;
            }
        }
        if (connected) {
            Serial.println(" OK!");
            oledMqttOK();
        } else {
            Serial.printf(" rc=%d, retry\n", mqtt.state());
        }
    }
}

// ── Publishing ──────────────────────────────────────────
void publishResult(const InferenceResult& r, bool needs_cloud_check) {
    // Đủ chỗ cho 5 probs + 47 samples downsampled (~600B)
    // static: tránh chiếm stack loopTask (xem chú thích publishRaw).
    static StaticJsonDocument<768> doc;
    doc.clear();
    doc["device_id"]        = MQTT_CLIENT;
    doc["timestamp"]        = millis();
    doc["label"]            = r.label_name;
    doc["label_index"]      = r.label_index;
    doc["confidence"]       = r.confidence;
    doc["leads_on"]         = leads_on;
    doc["needs_cloud_check"]= needs_cloud_check;

    // SpO2 + HR từ MAX30102 (0 nếu chưa đặt ngón tay / chưa hợp lệ)
    doc["spo2"]             = oled_spo2;
    doc["hr_ppg"]           = oled_hr_ppg;

    JsonArray probs = doc.createNestedArray("probs");
    for (int i = 0; i < 5; i++) probs.add(r.all_probs[i]);

    // Downsample beat_window 187→47 (mỗi 4 sample lấy 1) để dashboard vẽ sóng
    // Normal beat cũng cần waveform — không chỉ abnormal mới có
    JsonArray wave = doc.createNestedArray("samples");
    for (int i = 0; i < SAMPLE_COUNT; i += 4) wave.add(beat_window[i]);

    // static: đưa buffer 768B khỏi stack loopTask (8KB) — tránh tràn stack
    // đè lên global (vd wifiClient) → crash LoadProhibited ở mqtt.connected().
    static char buf[768];
    size_t n = serializeJson(doc, buf);
    mqtt.publish(TOPIC_RESULT, buf, n);

    Serial.printf("[MQTT->] %s (conf=%.2f) cloud=%s\n",
        r.label_name, r.confidence, needs_cloud_check ? "yes" : "no");
}

void publishRaw(const float* window) {
    // static: doc 2KB + buf 2KB = 4KB — KHÔNG để trên stack loopTask (8KB),
    // cộng CNN/MAX30102 dễ tràn stack → đè wifiClient global → crash
    // LoadProhibited ở mqtt.connected(). Đây là nguyên nhân crash chính.
    static StaticJsonDocument<2048> doc;
    doc.clear();
    doc["device_id"] = MQTT_CLIENT;
    doc["timestamp"] = millis();
    doc["spo2"]      = oled_spo2;
    doc["hr_ppg"]    = oled_hr_ppg;

    JsonArray arr = doc.createNestedArray("samples");
    for (int i = 0; i < SAMPLE_COUNT; i++) arr.add(window[i]);

    static char buf[2048];
    size_t n = serializeJson(doc, buf);
    mqtt.publish(TOPIC_RAW, buf, n);
    Serial.printf("[MQTT->] raw waveform (%d bytes)\n", (int)n);
}

void publishLeadsOff() {
    StaticJsonDocument<128> doc;
    doc["device_id"]  = MQTT_CLIENT;
    doc["timestamp"]  = millis();
    doc["label"]      = "Leads_Off";
    doc["confidence"] = 0.0f;
    doc["leads_on"]   = false;

    char buf[128];
    size_t n = serializeJson(doc, buf);
    mqtt.publish(TOPIC_RESULT, buf, n);
    Serial.println("[MQTT->] leads_off status");
}

// ── Process 1 nhip tim ──────────────────────────────────
void process_beat() {
    // 1. Copy raw buffer ra buf_copy (normalize in-place se modify)
    memcpy(buf_copy, ecg_buffer, sizeof(buf_copy));

    // 2. Z-score normalize toan bo 540 samples
    zscore_normalize(buf_copy, BUFFER_SIZE);

    // 3. Detect R-peak
    int peak_idx = detect_r_peak(buf_copy, BUFFER_SIZE);
    if (peak_idx < 0) {
        Serial.println("[!] No R-peak found, skip");
        return;
    }

    // 4. Cat window 187 samples quanh peak
    if (!extract_window(buf_copy, peak_idx, beat_window)) {
        Serial.printf("[!] Peak too close to edge (idx=%d), skip\n", peak_idx);
        return;
    }

    // 5. CNN inference
    unsigned long t0 = micros();
    InferenceResult r = runInference(beat_window);
    unsigned long dt = micros() - t0;
    Serial.printf("[CNN] %s conf=%.2f (%lu us)\n", r.label_name, r.confidence, dt);

    // Cập nhật OLED state sau mỗi inference
    oled_label = r.label_name;
    oled_conf  = r.confidence;
    oled_bpm   = oledUpdateBPM();

    // 6. Cascade decision
    bool is_normal_confident = (r.label_index == 0 &&
                                 r.confidence >= EDGE_CONFIDENCE_THRESHOLD);

    if (is_normal_confident) {
        publishResult(r, false);                  // light publish
    } else {
        publishResult(r, true);                   // need cloud check
        publishRaw(beat_window);                  // gui raw cho cloud
    }
}

// ── Setup ───────────────────────────────────────────────
void setup() {
    // Tắt brownout detector: code reset lặp ở connectWiFi do sụt áp khi WiFi
    // bật (dòng vọt ~300mA). Tắt để ESP không tự reset vì dip điện áp ngắn.
    // LƯU Ý: đây là giảm nhẹ triệu chứng — vẫn nên cấp nguồn 5V đủ mạnh.
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== ECG ESP32 AAMI Edge Inference ===");

    pinMode(LO_PLUS,  INPUT);
    pinMode(LO_MINUS, INPUT);

    Serial.println(">> step: setupOLED");
    setupOLED();   // Khởi tạo OLED trước khi kết nối WiFi (hiện boot screen)
#if ENABLE_MAX30102
    Serial.println(">> step: setupMAX30102");
    setupMAX30102();  // SpO2/HR — chung I2C bus với OLED, gọi SAU setupOLED()
#endif

    Serial.println(">> step: connectWiFi");
    connectWiFi();
    Serial.println(">> step: oledWifiOK");
    oledWifiOK(WiFi.localIP().toString().c_str());
    configTime(7 * 3600, 0, "pool.ntp.org", "time.google.com");
    mqtt.setServer(MQTT_BROKER, MQTT_PORT);
    mqtt.setBufferSize(2048);   // du cho payload raw
    Serial.println(">> step: connectMQTT");
    connectMQTT();

    Serial.println(">> step: setupInference");
    setupInference();

    Serial.println(">> step: setup DONE — bat dau thu tin hieu ECG\n");
}

// ── Main Loop ───────────────────────────────────────────
void loop() {
    // Giu WiFi TRUOC mqtt: neu WiFi rot ma goi mqtt.connected() thi
    // PubSubClient doc con tro WiFiClient da hong -> LoadProhibited crash.
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WiFi] mat ket noi, reconnect...");
        WiFi.disconnect();
        WiFi.reconnect();
        unsigned long t0 = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - t0 < 5000) {
            delay(200);
        }
        return;   // bo qua vong nay, lan sau WiFi on roi moi cham mqtt
    }

    // Giu MQTT connection (chi goi khi WiFi da on)
    if (!mqtt.connected()) connectMQTT();
    mqtt.loop();

#if ENABLE_MAX30102
    // Poll MAX30102 non-blocking mỗi vòng (vài chục µs, không phá 360Hz ECG)
    max30102Poll();
    readSpO2HR(oled_spo2, oled_hr_ppg, spo2_valid);
#endif

#if DEMO_MODE
    // ── DEMO MODE: bypass leads check + dung fake ECG ──
    leads_on = true;
    float val = generate_fake_ecg();
#else
    // ── Production: doc tu AD8232 ──
    leads_on = !(digitalRead(LO_PLUS) || digitalRead(LO_MINUS));

    if (!leads_on) {
        sample_index = 0;
        oled_label = "Leads Off";
        oled_conf  = 0.0f;
        unsigned long now = millis();
        if (now - last_leads_off_publish >= LEADS_OFF_INTERVAL_MS) {
            publishLeadsOff();
            last_leads_off_publish = now;
        }
        if (now - last_oled_ms >= OLED_REFRESH_MS) {
            oledDraw(oled_label, oled_conf, false, 0, oled_spo2);
            last_oled_ms = now;
        }
        // Không delay(100): loop phải chạy nhanh để hút FIFO MAX30102 (max 32
        // mẫu) kịp, nếu không FIFO tràn → mất mẫu → SpO2 không tính được.
        // delay nhỏ ~3ms cho gần sample rate 100Hz của MAX30102.
        delay(3);
        return;
    }

    int   raw = analogRead(ECG_PIN);
    float val = raw / 4095.0f;
#endif

    ecg_buffer[sample_index++] = val;
    oledPushSample(val);   // đẩy từng sample vào scroll buffer

    // Buffer day → process beat
    if (sample_index >= BUFFER_SIZE) {
        process_beat();

        // Sliding window: giu 270 samples cuoi, sample tiep tu day
        memmove(ecg_buffer, ecg_buffer + 270, 270 * sizeof(float));
        sample_index = 270;
    }

    // Refresh OLED mỗi 100ms (không block sampling)
    unsigned long now_ms = millis();
    if (now_ms - last_oled_ms >= OLED_REFRESH_MS) {
        oledDraw(oled_label, oled_conf, leads_on, oled_bpm, oled_spo2);
        last_oled_ms = now_ms;
    }

    // Sampling rate 360 Hz → moi sample cach nhau 2778 us.
    // delayMicroseconds() là busy-wait, KHÔNG nhả CPU cho RTOS → nếu loop chỉ
    // dùng nó (nhánh leads-on) thì loopTask watchdog không được feed → sau
    // vài giây ESP32 panic restart. yield() nhả CPU 1 nhịp để feed watchdog.
    yield();
    delayMicroseconds(SAMPLE_INTERVAL_US);
}
