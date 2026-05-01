#ifndef CONFIG_H
#define CONFIG_H

#define WIFI_SSID "ThaoNhi T2"
#define WIFI_PASSWORD "31121997"

#define MQTT_BROKER  "192.168.1.10"
#define MQTT_PORT 1883
#define MQTT_CLIENT "ESP_ECG_001"

#define TOPIC_RESULT "ecg/result"
#define TOPIC_RAW "ecg/raw"

#define ECG_PIN  34 // Analog Input
#define LO_PLUS  32 // Leads-off Detection +
#define LO_MINUS 33 // Lead-off Detection -

#define SAMPLE_COUNT 187
#define SAMPLE_RATE 200 // Hz => delay 5000 microseccond

#endif