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

#include "config.h"
#include "ecg_weights_aami.h"
#include "normalize.h"
#include "r_peak_detector.h"
#include "inference.h"

#if DEMO_MODE
#include "fake_ecg.h"
#endif

// ── Globals ─────────────────────────────────────────────
WiFiClient    wifiClient;
PubSubClient  mqtt(wifiClient);

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
    Serial.printf("Connecting WiFi: %s", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500); Serial.print(".");
    }
    Serial.printf("\n WiFi OK! IP: %s\n", WiFi.localIP().toString().c_str());
}

void connectMQTT() {
    while (!mqtt.connected()) {
        Serial.print("Connecting MQTT...");
        if (mqtt.connect(MQTT_CLIENT)) {
            Serial.println(" OK!");
        } else {
            Serial.printf(" rc=%d, retry 3s\n", mqtt.state());
            delay(3000);
        }
    }
}

// ── Publishing ──────────────────────────────────────────
void publishResult(const InferenceResult& r, bool needs_cloud_check) {
    StaticJsonDocument<384> doc;
    doc["device_id"]        = MQTT_CLIENT;
    doc["timestamp"]        = millis();
    doc["label"]            = r.label_name;
    doc["label_index"]      = r.label_index;
    doc["confidence"]       = r.confidence;
    doc["leads_on"]         = leads_on;
    doc["needs_cloud_check"]= needs_cloud_check;

    // Tat ca 5 probabilities (debug + cloud co the dung)
    JsonArray probs = doc.createNestedArray("probs");
    for (int i = 0; i < 5; i++) probs.add(r.all_probs[i]);

    char buf[384];
    size_t n = serializeJson(doc, buf);
    mqtt.publish(TOPIC_RESULT, buf, n);

    Serial.printf("[MQTT->] %s (conf=%.2f) cloud=%s\n",
        r.label_name, r.confidence, needs_cloud_check ? "yes" : "no");
}

void publishRaw(const float* window) {
    StaticJsonDocument<2048> doc;
    doc["device_id"] = MQTT_CLIENT;
    doc["timestamp"] = millis();

    JsonArray arr = doc.createNestedArray("samples");
    for (int i = 0; i < SAMPLE_COUNT; i++) arr.add(window[i]);

    char buf[2048];
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
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== ECG ESP32 AAMI Edge Inference ===");

    pinMode(LO_PLUS,  INPUT);
    pinMode(LO_MINUS, INPUT);

    connectWiFi();
    mqtt.setServer(MQTT_BROKER, MQTT_PORT);
    mqtt.setBufferSize(2048);   // du cho payload raw
    connectMQTT();

    setupInference();

    Serial.println("Bat dau thu tin hieu ECG...\n");
}

// ── Main Loop ───────────────────────────────────────────
void loop() {
    // Giu MQTT connection
    if (!mqtt.connected()) connectMQTT();
    mqtt.loop();

#if DEMO_MODE
    // ── DEMO MODE: bypass leads check + dung fake ECG ──
    leads_on = true;
    float val = generate_fake_ecg();
#else
    // ── Production: doc tu AD8232 ──
    leads_on = !(digitalRead(LO_PLUS) || digitalRead(LO_MINUS));

    if (!leads_on) {
        sample_index = 0;
        unsigned long now = millis();
        if (now - last_leads_off_publish >= LEADS_OFF_INTERVAL_MS) {
            publishLeadsOff();
            last_leads_off_publish = now;
        }
        delay(100);
        return;
    }

    int   raw = analogRead(ECG_PIN);
    float val = raw / 4095.0f;
#endif

    ecg_buffer[sample_index++] = val;

    // Buffer day → process beat
    if (sample_index >= BUFFER_SIZE) {
        process_beat();

        // Sliding window: giu 270 samples cuoi, sample tiep tu day
        memmove(ecg_buffer, ecg_buffer + 270, 270 * sizeof(float));
        sample_index = 270;
    }

    // Sampling rate 360 Hz → moi sample cach nhau 2778 us
    delayMicroseconds(SAMPLE_INTERVAL_US);
}
