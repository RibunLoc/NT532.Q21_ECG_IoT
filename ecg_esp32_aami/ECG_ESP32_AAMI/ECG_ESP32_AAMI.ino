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
#include "fake_real_beats.h"   // 4 beat that — dung cho BYPASS TEST

// ── MAX30102 (SpO2/HR). Đặt 0 để tắt khi debug. ──
#define ENABLE_MAX30102 1
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

// ── Majority vote 10s: gom nhieu nhip -> chot 1 ket qua on dinh ──
// Moi nhip CNN bo phieu cho 1 trong 5 lop + cong confidence. Sau 10s lay lop
// nhieu phieu nhat (uu tien tong conf) lam ket qua chot, roi reset.
#define VOTE_WINDOW_MS 10000
static int   vote_count[5] = {0,0,0,0,0};   // so phieu moi lop
static float vote_conf[5]  = {0,0,0,0,0};   // tong conf moi lop
static unsigned long vote_start_ms = 0;
static const char* LABEL_NAMES[5] = {"Normal","Supraventricular","Ventricular","Fusion","Unknown"};

// ── Bat LED + COI theo lop da chot ─────────────────────
// 0 Normal->xanh | 1 SVE, 3 Fusion->vang + coi luu y | 2 VEB, 4 Unknown->do + coi nguy hiem
void updateLeds(int label_index) {
    bool g = (label_index == 0);
    bool y = (label_index == 1 || label_index == 3);
    bool r = (label_index == 2 || label_index == 4);
    digitalWrite(LED_GREEN,  g ? HIGH : LOW);
    digitalWrite(LED_YELLOW, y ? HIGH : LOW);
    digitalWrite(LED_RED,    r ? HIGH : LOW);

    // Coi 1206 (active): set co "luu y" -> warnService() beeep cach quang
    extern bool warn_active;
    warn_active = y;
    if (!y) digitalWrite(BUZZER_WARN, LOW);   // het vang -> tat ngay

    // Coi 9056-TS (passive): khi nguy hiem -> tit lien tuc (uu tien hon bip nhip).
    extern bool danger_active;
    extern unsigned long _beep_off_ms;
    danger_active = r;
    if (r) {
        tone(BUZZER_DANGER, BUZZER_DANGER_FREQ);
    } else {
        // Het nguy hiem -> tat tone bao dong, reset bip nhip (tranh tieng dinh nhau
        // luc chuyen trang thai). beepOnHeartbeat se phat lai tu nhip moi.
        noTone(BUZZER_DANGER);
        _beep_off_ms = 0;
    }
}

// ── Bip nhip tim tren coi 9056 (passive), non-blocking ──
// danger_active: khi VEB -> ngung bip nhip, nhuong coi cho bao dong.
bool danger_active = false;
bool warn_active   = false;              // dang o trang thai luu y (den vang)
unsigned long _beep_off_ms = 0;          // thoi diem tat tieng bip dang phat (khong static -> extern duoc)

// ── Coi luu y (1206 active): bip NGAT QUANG 0.2s keu / 0.5s nghi ──
void warnService() {
    if (!warn_active) {                  // khong luu y -> dam bao TAT
        digitalWrite(BUZZER_WARN, LOW);
        return;
    }
    // 1 tieng dai 0.4s moi 2.5s: "beeep ... (nghi 2.1s) ... beeep"
    unsigned long t = millis() % 2500;
    digitalWrite(BUZZER_WARN, (t < 400) ? HIGH : LOW);
}

// Goi khi phat hien 1 R-peak -> phat 1 tieng bip ngan.
// CHI bip nhip khi NORMAL (khong nguy hiem VA khong luu y). Luc vang/do, coi
// dac trung cua trang thai do (D19 beeep / D4 tit) lo, khong bip nhip chen vao.
void beepOnHeartbeat() {
    if (danger_active || warn_active) return;   // do hoac vang -> khong bip nhip
    tone(BUZZER_DANGER, BEEP_FREQ);
    _beep_off_ms = millis() + BEEP_DURATION_MS;
}

// Goi moi vong loop -> tat tieng bip sau BEEP_DURATION_MS (non-blocking)
void beepService() {
    if (danger_active) return;            // bao dong dang giu tone, khong dung
    if (warn_active) {                    // chuyen sang vang -> tat ngay tieng bip dang ngan
        if (_beep_off_ms) { noTone(BUZZER_DANGER); _beep_off_ms = 0; }
        return;
    }
    if (_beep_off_ms && millis() >= _beep_off_ms) {
        noTone(BUZZER_DANGER);
        _beep_off_ms = 0;
    }
}

// SpO2 + HR từ MAX30102 (cập nhật ~1 lần/giây qua max30102Poll)
static int  oled_spo2  = 0;
static int  oled_hr_ppg = 0;
static bool spo2_valid  = false;

// Refresh OLED mỗi ~100ms độc lập với sampling rate
static unsigned long last_oled_ms = 0;
const unsigned long OLED_REFRESH_MS = 25;   // 40 fps — sóng trôi mượt/nhanh hơn
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

// ── Timer sampling 360Hz: ISR doc ADC -> ring buffer ────
// Sampling chay bang hardware timer DOC LAP voi loop. Du loop nang (OLED/MAX/
// MQTT) sampling van dung 360Hz. ISR phai cuc nhe + IRAM_ATTR + portMUX.
#define RING_SIZE 1024
static volatile uint16_t adc_ring[RING_SIZE];
static volatile uint32_t ring_head = 0;   // ISR ghi
static volatile uint32_t ring_tail = 0;   // loop doc
static hw_timer_t* ecg_timer = NULL;
static portMUX_TYPE ring_mux = portMUX_INITIALIZER_UNLOCKED;

void IRAM_ATTR onEcgTimer() {
    uint16_t v = (uint16_t)analogRead(ECG_PIN);   // ADC read OK trong ISR tren ESP32
    portENTER_CRITICAL_ISR(&ring_mux);
    uint32_t next = (ring_head + 1) % RING_SIZE;
    if (next != ring_tail) {        // tranh tran (overwrite tail)
        adc_ring[ring_head] = v;
        ring_head = next;
    }
    portEXIT_CRITICAL_ISR(&ring_mux);
}

// Lay 1 mau tu ring buffer (loop goi). Tra ve false neu rong.
bool ringPop(uint16_t* out) {
    bool has = false;
    portENTER_CRITICAL(&ring_mux);
    if (ring_tail != ring_head) {
        *out = adc_ring[ring_tail];
        ring_tail = (ring_tail + 1) % RING_SIZE;
        has = true;
    }
    portEXIT_CRITICAL(&ring_mux);
    return has;
}

// ── Notch filter 50Hz (khu nhieu dien luoi) — biquad IIR ──
// Thiet ke cho fs=360Hz, f0=50Hz, Q=8. He so tinh san.
// Khu dinh 50Hz (nguon nhieu chinh khi cam tay / cam sac) ma giu hinh ECG.
static float _nx1 = 0, _nx2 = 0, _ny1 = 0, _ny2 = 0;
static inline float notch50(float x) {
    // b0,b1,b2,a1,a2 cho notch 50Hz @360Hz, Q=8
    const float b0 = 0.9565f, b1 = -1.5566f, b2 = 0.9565f;
    const float a1 = -1.5566f, a2 = 0.9131f;
    float y = b0*x + b1*_nx1 + b2*_nx2 - a1*_ny1 - a2*_ny2;
    _nx2 = _nx1; _nx1 = x;
    _ny2 = _ny1; _ny1 = y;
    return y;
}

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

    // Bo phieu cho lop nay (KHONG cap nhat oled_label ngay — cho chot sau 10s)
    if (r.label_index >= 0 && r.label_index < 5) {
        vote_count[r.label_index]++;
        vote_conf[r.label_index] += r.confidence;
    }

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

    // TAT COI NGAY DAU TIEN (truoc Serial/OLED/WiFi) — tranh coi keu luc boot
    // khi chan con noi/HIGH truoc khi code kip set. Lam som nhat co the.
    pinMode(BUZZER_WARN, OUTPUT);
    digitalWrite(BUZZER_WARN, LOW);
    noTone(BUZZER_DANGER);

    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== ECG ESP32 AAMI Edge Inference ===");

    pinMode(LO_PLUS,  INPUT);
    pinMode(LO_MINUS, INPUT);

    // 3 LED bao trang thai
    pinMode(LED_GREEN,  OUTPUT);
    pinMode(LED_YELLOW, OUTPUT);
    pinMode(LED_RED,    OUTPUT);
    digitalWrite(LED_GREEN, LOW); digitalWrite(LED_YELLOW, LOW); digitalWrite(LED_RED, LOW);

    // ADC: dai day du 0-3.3V cho tin hieu ECG (mac dinh chi ~0-1.1V -> doc sai)
    analogSetPinAttenuation(ECG_PIN, ADC_11db);

    // Timer sampling 360Hz (chu ky 2778us). ISR doc ADC -> ring buffer.
    // Chay doc lap voi loop nen OLED/MAX/MQTT khong pha sample rate.
    ecg_timer = timerBegin(1000000);                 // timer 1MHz (1us/tick)
    timerAttachInterrupt(ecg_timer, &onEcgTimer);
    timerAlarm(ecg_timer, SAMPLE_INTERVAL_US, true, 0);  // 2778us, auto-reload
    Serial.println(">> ECG timer 360Hz started");

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

    // ── BYPASS TEST: dua thang 4 beat that vao CNN (khong qua pipeline) ──
    // Muc dich: kiem chứng weights co dung khong.
    // - Neu ra dung 4 label N/S/V/F -> weights OK, bug o pipeline (z-score/R-peak)
    // - Neu sai label -> weights/inference loi, can train + export lai
    Serial.println("\n===== [BYPASS TEST] dua thang beat that vao CNN =====");
    const char* test_names[4] = {"N (Normal)", "S (SVE)", "V (VEB)", "F (Fusion)"};
    float test_beat[187];
    for (int cls = 0; cls < 4; cls++) {
        for (int i = 0; i < 187; i++) test_beat[i] = REAL_BEATS[cls][i];
        InferenceResult r = runInference(test_beat);
        Serial.printf("  Beat THAT [%s] -> CNN du doan: %s conf=%.2f %s\n",
            test_names[cls], r.label_name, r.confidence,
            (r.label_index == cls) ? "[DUNG]" : "[SAI]");
    }
    Serial.println("===== [BYPASS TEST] xong =====\n");

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
    oled_bpm = oled_hr_ppg;   // BPM hiển thị lấy từ MAX30102 (PPG), không từ ECG
#endif

    // ── Leads detection (debounce) ──────────────────────
    bool leadsOffReal = false;
#if DEMO_MODE
    leads_on = true;
    // Demo kiem thu: Normal 30s -> SVE 10s -> VEB 10s -> Fusion 10s -> lap (chu ky 60s).
    // Moi truong bat thuong 10s = khop 1 lan vote (10s) -> den/coi kip phan ung.
    static unsigned long _demoStart = 0;
    static int _demoType = -1;
    if (_demoStart == 0) _demoStart = millis();
    unsigned long elapsed = (millis() - _demoStart) % 60000;   // chu ky 60s
    int t;
    if      (elapsed < 30000) t = 0;   // Normal 0-30s
    else if (elapsed < 40000) t = 1;   // SVE    30-40s
    else if (elapsed < 50000) t = 2;   // VEB    40-50s
    else                      t = 3;   // Fusion 50-60s
    if (t != _demoType) {
        _demoType = t;
        setFakeBeatType(t);
        // DEMO: gan label + den/coi TRUC TIEP theo truong demo (bo qua model/vote).
        // demo type 0/1/2/3 = Normal/SVE/VEB/Fusion = label_index 0/1/2/3.
        oled_label = LABEL_NAMES[t];
        oled_conf  = 1.0f;
        updateLeds(t);
        Serial.printf("\n===== [DEMO] %s =====\n", LABEL_NAMES[t]);
    }
#else
    int lo_p = digitalRead(LO_PLUS);
    int lo_m = digitalRead(LO_MINUS);
    leads_on = !(lo_p || lo_m);

    static unsigned long _ld = 0;
    if (millis() - _ld >= 1000) {
        Serial.printf("[LEADS] LO+=%d LO-=%d leads_on=%d\n", lo_p, lo_m, leads_on);
        _ld = millis();
    }
    static unsigned long _leadsOffStart = 0;
    if (!leads_on) {
        if (_leadsOffStart == 0) _leadsOffStart = millis();
        if (millis() - _leadsOffStart > 800) leadsOffReal = true;
    } else {
        _leadsOffStart = 0;
    }
    if (leadsOffReal) {
        sample_index = 0;
        ring_tail = ring_head;   // bo het mau ton dong khi off that
        oled_label = "Leads Off";
        oled_conf  = 0.0f;
        unsigned long now = millis();
        if (now - last_leads_off_publish >= LEADS_OFF_INTERVAL_MS) {
            publishLeadsOff(); last_leads_off_publish = now;
        }
        if (now - last_oled_ms >= OLED_REFRESH_MS) {
            oledDrawWave(); last_oled_ms = now;
        }
        updateLeds(-1);   // leads off that -> tat het den
    }
#endif

    // ── Xu ly TAT CA mau co trong ring buffer (timer da lay san @360Hz) ──
    static unsigned long _dbg_t = 0; static int _amin = 4095, _amax = 0; static int _cnt = 0;
    if (!leadsOffReal) {
        uint16_t raw;
        while (ringPop(&raw)) {
#if DEMO_MODE
            float val = generate_fake_ecg();
#else
            float val = raw / 4095.0f;
            if ((int)raw < _amin) _amin = raw;
            if ((int)raw > _amax) _amax = raw;
            _cnt++;
#endif
            ecg_buffer[sample_index++] = val;     // RAW cho CNN (giu du 360Hz)

            // ── Bip nhip tim ──────────────────────────────────────
#if DEMO_MODE
            // DEMO: bip 1 lan moi khi beat fake lap lai (dang tin, khong loan)
            if (fakeNewBeat()) beepOnHeartbeat();
#else
            // THAT: phat hien R-peak realtime. Refractory dem bang SO MAU
            // (khong dung millis vi nhieu mau/vong loop). 90 mau @360Hz = 250ms.
            {
                static float bl = 0.5f, dev = 0.02f;
                static bool above = false;
                static unsigned long samp_n = 0, last_beat_n = 0;
                samp_n++;
                bl  += (val - bl) * 0.003f;
                dev += (fabsf(val - bl) - dev) * 0.01f;
                float thr = bl + 4.0f * dev;
                if (val > thr && !above && (samp_n - last_beat_n) >= 90) {
                    above = true; last_beat_n = samp_n;
                    beepOnHeartbeat();
                } else if (val < bl) {
                    above = false;
                }
            }
#endif

            // Hien thi: downsample 5:1 (push moi 5 mau) -> song cuon cham, de nhin.
            // Lay TRUNG BINH 5 mau (khong bo 4) de muot + dung notch 50Hz.
            static float _accSum = 0; static int _accN = 0;
            _accSum += notch50(val); _accN++;
            if (_accN >= 5) {
                oledPushSample(_accSum / _accN);
                _accSum = 0; _accN = 0;
            }

            if (sample_index >= BUFFER_SIZE) {
                process_beat();
                memmove(ecg_buffer, ecg_buffer + 270, 270 * sizeof(float));
                sample_index = 270;
            }
        }
    }
#if !DEMO_MODE
    if (millis() - _dbg_t >= 1000) {
        Serial.printf("[ADC] bien-do=%d  sample/s=%d  buf=%d/%d\n",
            _amax - _amin, _cnt, sample_index, BUFFER_SIZE);
        _amin = 4095; _amax = 0; _cnt = 0; _dbg_t = millis();
    }
#endif

    beepService();   // tat tieng bip nhip tim sau BEEP_DURATION_MS (non-blocking)
    warnService();   // coi luu y (1206) bip ngat quang khi den vang

    unsigned long now_ms = millis();

    // ── Chot ket qua vote sau moi 10s (majority + uu tien tong conf) ──
    if (vote_start_ms == 0) vote_start_ms = now_ms;
    int countdown = 10 - (int)((now_ms - vote_start_ms) / 1000);
    if (countdown < 0) countdown = 0;

    if (now_ms - vote_start_ms >= VOTE_WINDOW_MS) {
#if !DEMO_MODE
        // CHI chot vote khi do THAT. DEMO da gan label truc tiep theo truong demo.
        int total = 0, best = -1; float best_score = -1;
        for (int i = 0; i < 5; i++) {
            total += vote_count[i];
            float score = vote_count[i] + vote_conf[i] * 0.01f;
            if (vote_count[i] > 0 && score > best_score) { best_score = score; best = i; }
        }
        if (best >= 0) {
            float avg_conf = vote_conf[best] / vote_count[best];
            // POST-PROCESSING (do that): model edge co domain shift voi AD8232
            // -> hay nham Normal thanh SVE/V voi conf thap. Neu best != Normal
            // ma conf < 0.7 -> fallback ve Normal (an toan hon false alarm).
            if (best != 0 && avg_conf < 0.7f) {
                Serial.printf("[VOTE 10s] %s conf=%.2f thap -> fallback Normal (domain shift)\n",
                    LABEL_NAMES[best], avg_conf);
                best = 0;
                avg_conf = 1.0f - avg_conf;   // dao nguoc: cao = chac chan Normal
            }
            oled_label = LABEL_NAMES[best];
            oled_conf  = avg_conf;
            updateLeds(best);
            Serial.printf("[VOTE 10s] => %s (%d/%d phieu, conf tb=%.2f)\n",
                LABEL_NAMES[best], vote_count[best], total, oled_conf);
        } else {
            oled_label = "---";
            oled_conf  = 0.0f;
            updateLeds(-1);
            Serial.println("[VOTE 10s] => khong co nhip nao (--)");
        }
#endif
        // Reset cho chu ky moi (van reset de log/cloud dung)
        for (int i = 0; i < 5; i++) { vote_count[i] = 0; vote_conf[i] = 0; }
        vote_start_ms = now_ms;
    }

    // Refresh OLED:
    //  - Man SONG: ve nhanh moi OLED_REFRESH_MS (muot)
    //  - Man SO LIEU: ve cham moi 500ms (label da chot + dem nguoc)
    if (now_ms - last_oled_ms >= OLED_REFRESH_MS) {
        oledDrawWave();
        last_oled_ms = now_ms;
    }
    static unsigned long last_info_ms = 0;
    if (now_ms - last_info_ms >= 500) {
        oledDrawInfo(oled_label, oled_conf, leads_on, oled_bpm, oled_spo2, countdown);
        last_info_ms = now_ms;
    }

    // Sampling KHONG con o loop — timer 360Hz lo viec do. Loop chi xu ly mau
    // tu ring buffer + OLED/MAX/MQTT. delay(1) nha CPU cho RTOS (feed watchdog),
    // ring buffer 1024 mau du dem trong 1ms (timer chi them ~0.36 mau/ms).
    delay(1);
}
