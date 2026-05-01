#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "config.h"
#include "inference.h"

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

float ecg_buffer[SAMPLE_COUNT]
int   sample_index = 0;
bool leads_on = false;
String device_id = MQTT_CLIENT;

void connectWiFI() {
    Serial.printf("connecting WiFi: %s", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500); Serial.print(".");
    }
    Serial.Printf("\n WiFi Ok! IP: %s\n",
        WiFi.LocalIP().toString().c_str());
}

void connectMQTT() {
    while (!mqtt.connected()) {
        Serial.print("Connecting MQTT...");
        if (mqtt.connect(MQTT_CLIENT)) {
            Serial.println(" OK!");
        } else {
            Serial.println(" rc=%d, retry 3s\n", mqtt.State());
            delay(3000);
        }
    }
}

// Publish kết quả
void publishResult(InferenceResult result) {
    StaticJsonDocument<256> doc;
    doc["device_id"] = device_id;
    doc["timestamp"] = millis();
    doc["label"]     = result.label_name;
    doc["label_idx"] = result.label_idx;
    doc["leads_on"]  = leads_on;

    char buf[256];
    serializeJson(doc, buf);
    mqtt.publish(TOPIC_RESULT, buf);

    Serial.printf("[MQTT->] %s | conf=%.2f\n"
        result.label_name, result.confidence);

    // cảnh bảo nết bất thường 
    if (result.label_index != 0) {
        Serial.printf(" ABNORMAL: %s!\n", result.label_name);
    }
}


void publishRaw(float* buffer) {
    StaticJsonDocument<1024> doc;
    doc["device_id"] = device_id;
    doc["timestamp"] = millis();
    JsonArray arr = doc.createNestedArray("signal");
    for (int i = 0; i < SAMPLE_COUNT; i++) {
        arr.add(buffer[i]);
    }

    char buf[1024];
    serializeJson(doc, buf);
    mqtt.publish(TOPIC_RAW, buf);
}

void setup() {
    Serial.begin(115200);
    Serial.println("\n=== ECG ESP32 Starting ===");

    pinMode(LO_PLUS,  INPUT);
    pinMode(LO_MINUS, INPUT);

    connectWiFi();

    mqtt.setServer(MQTT_BROKER, MQTT_PORT);
    mqtt.setBufferSize(1024);  // Tăng buffer cho raw signal
    connectMQTT();

    setupInference();

    Serial.println("🚀 Đang thu tín hiệu ECG...");
}

// ── Loop ─────────────────────────────────────────────
void loop() {
    // Giữ kết nối MQTT
    if (!mqtt.connected()) connectMQTT();
    mqtt.loop();

    // Kiểm tra điện cực
    leads_on = !(digitalRead(LO_PLUS) || digitalRead(LO_MINUS));
    if (!leads_on) {
        Serial.println("⚠️  Điện cực chưa gắn!");
        delay(1000);
        return;
    }

    // Đọc ADC và normalize về [0, 1]
    int   raw = analogRead(ECG_PIN);
    float val = raw / 4095.0f;
    ecg_buffer[sample_index++] = val;

    // Đủ 187 mẫu → inference
    if (sample_index >= SAMPLE_COUNT) {
        sample_index = 0;

        // Chạy inference
        InferenceResult result = runInference(ecg_buffer);

        // Publish lên Gateway
        publishResult(result);
        publishRaw(ecg_buffer);
    }

    // Sampling rate 200Hz = 5000 microseconds/sample
    delayMicroseconds(5000);
}