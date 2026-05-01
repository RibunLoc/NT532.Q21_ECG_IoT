// ============================================================
// ECG ESP32 — TEST MODE (Pure C Inference)
// Không cần AD8232, không cần TFLite
// Test pipeline: ESP32 → Local Gateway → AWS IoT Core
// ============================================================

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "config.h"
#include "ecg_samples.h"    // Mẫu ECG từ Kaggle
#include "inference.h"      // Pure C inference

// ── Danh sách test ───────────────────────────────────────
struct TestCase {
    const float* data;
    const char*  expected_label;
};

const TestCase TEST_CASES[] = {
    {SAMPLE_NORMAL,           "Normal"},
    {SAMPLE_VENTRICULAR,      "Ventricular"},
    {SAMPLE_SUPRAVENTRICULAR, "Supraventricular"},
    {SAMPLE_FUSION,           "Fusion"},
    {SAMPLE_UNKNOWN,          "Unknown"},
};
const int NUM_TESTS = 5;

// ── Globals ───────────────────────────────────────────────
WiFiClient   wifiClient;
PubSubClient mqtt(wifiClient);
int          test_index   = 0;
int          pass_count   = 0;
unsigned long last_send   = 0;
const int    SEND_INTERVAL = 3000;

// ── WiFi ──────────────────────────────────────────────────
void connectWiFi() {
    Serial.printf("\nConnecting WiFi: %s", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries < 20) {
        delay(500); Serial.print("."); tries++;
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\n✅ WiFi OK — IP: %s\n",
            WiFi.localIP().toString().c_str());
    } else {
        Serial.println("\n❌ WiFi FAILED");
    }
}

// ── MQTT ──────────────────────────────────────────────────
void connectMQTT() {
    int tries = 0;
    while (!mqtt.connected() && tries < 5) {
        Serial.printf("Connecting MQTT %s:%d ...", MQTT_BROKER, MQTT_PORT);
        if (mqtt.connect(MQTT_CLIENT)) {
            Serial.println(" ✅ OK!");
        } else {
            Serial.printf(" ❌ rc=%d, retry...\n", mqtt.state());
            delay(2000); tries++;
        }
    }
}

// ── Publish kết quả ──────────────────────────────────────
void publishResult(InferenceResult result, const char* expected) {
    StaticJsonDocument<512> doc;
    doc["device_id"]  = MQTT_CLIENT;
    doc["timestamp"]  = millis();
    doc["label"]      = result.label_name;
    doc["label_idx"]  = result.label_index;
    doc["confidence"] = result.confidence;
    doc["expected"]   = expected;
    doc["test_pass"]  = (strcmp(result.label_name, expected) == 0);
    doc["test_index"] = test_index;
    doc["mode"]       = "TEST";

    char buf[512];
    serializeJson(doc, buf);
    bool ok = mqtt.publish(TOPIC_RESULT, buf, true);

    Serial.println("─────────────────────────────────────");
    Serial.printf("Test #%d/%d\n", test_index + 1, NUM_TESTS);
    Serial.printf("  Expected   : %s\n", expected);
    Serial.printf("  Predicted  : %s (%.1f%%)\n",
        result.label_name, result.confidence * 100);
    Serial.printf("  MQTT       : %s\n", ok ? "✅ OK" : "❌ FAIL");

    // In xác suất từng class
    printAllProbs(result);

    bool pass = strcmp(result.label_name, expected) == 0;
    if (pass) {
        Serial.println("  Result     : ✅ PASS");
        pass_count++;
    } else {
        Serial.println("  Result     : ❌ FAIL (inference sai)");
    }
}

void publishRaw(const float* buffer) {
    StaticJsonDocument<2048> doc;
    doc["device_id"] = MQTT_CLIENT;
    doc["timestamp"] = millis();
    doc["mode"]      = "TEST";
    JsonArray arr    = doc.createNestedArray("signal");
    for (int i = 0; i < SAMPLE_COUNT; i++) arr.add(buffer[i]);
    char buf[2048];
    serializeJson(doc, buf);
    mqtt.publish(TOPIC_RAW, buf);
}

// ── Setup ─────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n╔══════════════════════════════════╗");
    Serial.println("║  ECG ESP32 — Pure C Inference    ║");
    Serial.println("║  No TFLite dependency            ║");
    Serial.println("╚══════════════════════════════════╝");

    Serial.println("\n[1/3] WiFi...");
    connectWiFi();

    Serial.println("\n[2/3] MQTT...");
    mqtt.setServer(MQTT_BROKER, MQTT_PORT);
    mqtt.setBufferSize(2048);
    connectMQTT();

    Serial.println("\n[3/3] Inference Engine...");
    setupInference();

    // Benchmark inference speed
    Serial.println("\n⏱  Benchmark inference speed...");
    float dummy[INPUT_LEN];
    for (int i = 0; i < INPUT_LEN; i++) dummy[i] = 0.5f;

    unsigned long t_start = micros();
    InferenceResult r = runInference(dummy);
    unsigned long t_elapsed = micros() - t_start;
    Serial.printf("   Inference time: %lu microseconds (%.2f ms)\n",
        t_elapsed, t_elapsed / 1000.0f);

    Serial.println("\n🚀 Starting test pipeline...");
    Serial.printf("   Will send %d samples, every %d sec\n\n",
        NUM_TESTS, SEND_INTERVAL / 1000);
}

// ── Loop ──────────────────────────────────────────────────
void loop() {
    if (!mqtt.connected()) connectMQTT();
    mqtt.loop();

    // Đã test xong
    if (test_index >= NUM_TESTS) {
        static bool printed = false;
        if (!printed) {
            Serial.println("\n╔══════════════════════════════════╗");
            Serial.println("║        TEST COMPLETE             ║");
            Serial.println("╚══════════════════════════════════╝");
            Serial.printf("  Inference: %d/%d PASS\n", pass_count, NUM_TESTS);
            Serial.println("\n  Verify:");
            Serial.println("  1. Gateway log → nhận được messages");
            Serial.println("  2. DynamoDB → có data trong ecg_results");
            Serial.println("  3. Email → nhận cảnh báo Ventricular");
            printed = true;
        }
        delay(1000);
        return;
    }

    // Gửi mỗi SEND_INTERVAL
    if (millis() - last_send >= SEND_INTERVAL) {
        last_send = millis();

        const TestCase& tc = TEST_CASES[test_index];

        // Copy sang buffer float
        float buf[INPUT_LEN];
        for (int i = 0; i < INPUT_LEN; i++) buf[i] = tc.data[i];

        // Inference
        InferenceResult result = runInference(buf);

        // Publish
        publishResult(result, tc.expected_label);
        publishRaw(buf);

        test_index++;
    }
}
