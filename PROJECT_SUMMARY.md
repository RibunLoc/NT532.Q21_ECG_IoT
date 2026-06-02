# Tổng hợp Đồ án IoT ECG — Hệ thống theo dõi & cảnh báo nhịp tim

> **Bản tổng kết toàn bộ công việc nhóm em đã thực hiện**.
> Dùng để chuẩn bị thuyết trình + trả lời câu hỏi giảng viên.

---

## 1. Tổng quan đồ án

**Mục tiêu**: Xây dựng hệ thống IoT hoàn chỉnh đo và phân loại nhịp tim **realtime**, cảnh báo bất thường qua email.

**Đặc điểm**:
- AI chạy trực tiếp trên vi điều khiển ESP32 (Edge AI)
- Cascade verify với model lớn trên AWS Cloud
- Dashboard web hiển thị realtime
- Cảnh báo đa kênh: đèn LED + buzzer cục bộ + email qua SNS

---

## 2. Kiến trúc 3 tầng

```
┌─────────────────────────────────────────────────────────────┐
│  Tầng 1: EDGE — ESP32 + AD8232 + MAX30102 + OLED + LED      │
│  - Đo ECG 360Hz, AI inference 28ms                          │
│  - Cảnh báo cục bộ qua LED/buzzer                           │
└──────────────────────────┬──────────────────────────────────┘
                           │ MQTT local (TCP 1883)
                           ▼
┌─────────────────────────────────────────────────────────────┐
│  Tầng 2: GATEWAY — Python bridge trên laptop                │
│  - Cầu nối MQTT local ↔ MQTT TLS AWS                        │
│  - Lưu certs, ký SigV4                                      │
└──────────────────────────┬──────────────────────────────────┘
                           │ MQTT TLS (port 8883)
                           ▼
┌─────────────────────────────────────────────────────────────┐
│  Tầng 3: CLOUD — AWS IoT Core + Lambda + DynamoDB + SNS     │
│  - 3 IoT Rules định tuyến message                           │
│  - Lambda ONNX inference (model lớn hơn ESP32)              │
│  - DynamoDB audit log + SNS email alert                     │
└──────────────────────────┬──────────────────────────────────┘
                           │ AWS SigV4 WebSocket MQTT
                           ▼
┌─────────────────────────────────────────────────────────────┐
│  CLIENT: Dashboard Next.js (React + Recharts)               │
│  - Subscribe MQTT realtime trực tiếp                        │
│  - Cognito auth, hiển thị sóng + label + cloud verify       │
└─────────────────────────────────────────────────────────────┘
```

---

## 3. Phần cứng đã làm

### Vi điều khiển: ESP32 DevKit
- Dual-core 240MHz, 520KB RAM, 4MB flash, WiFi/Bluetooth tích hợp
- FPU (xử lý float32) — đủ chạy CNN
- ADC 12-bit, hardware timer chính xác

### Cảm biến

| Linh kiện | Chức năng | Kết nối |
|-----------|-----------|---------|
| **AD8232** | Đo ECG analog (3 điện cực) | GPIO 35 (ADC) + 32/33 (Leads-off detect) |
| **MAX30102** | Đo SpO2 + nhịp tim PPG | I2C bus 2 (Wire1, D25/D26) |

### Hiển thị
- **2 màn OLED 0.96" SSD1306 I2C** (cùng địa chỉ 0x3C → dùng 2 bus riêng):
  - OLED 1 (Wire, D21/D22): vẽ sóng ECG cuộn ngang
  - OLED 2 (Wire1, D25/D26): hiển thị label + BPM + SpO2 + countdown

### Cảnh báo

| Linh kiện | Chân | Trạng thái |
|-----------|------|------------|
| **LED xanh** | GPIO 27 | Normal (bình thường) |
| **LED vàng** | GPIO 13 | SVE/Fusion (cảnh báo) |
| **LED đỏ** | GPIO 14 | VEB/Unknown (nguy hiểm) |
| **Buzzer 9056 passive** | GPIO 4 | Bíp nhịp tim (Normal) + còi nguy hiểm (VEB) |
| **Buzzer 1206 active** | GPIO 19 | Beep cảnh báo SVE/Fusion |

### Nguồn
- Pin LiPo / USB
- **Lưu ý quan trọng**: KHÔNG cấp nguồn vào chân 3V3 (làm hỏng board) — phải dùng VIN hoặc USB

---

## 4. Phần mềm Edge — ESP32 firmware (10 file)

| File | Vai trò |
|------|---------|
| `ECG_ESP32_AAMI.ino` | Entry point, setup() + loop(), điều phối tất cả module |
| `config.h` | Tập trung cấu hình: WiFi, MQTT, pin, threshold, DEMO_MODE |
| `ecg_weights_aami.h` | 11K float weights CNN — auto-generated từ Keras |
| `inference.h` | CNN engine viết tay: Conv1D, GAP, Dense, Softmax |
| `normalize.h` | Z-score normalization (mean=0, std=1) |
| `r_peak_detector.h` | Tìm đỉnh R bằng Pan-Tompkins simplified |
| `oled_display.h` | Driver 2 OLED SSD1306 trên 2 bus I2C |
| `max30102_sensor.h` | Driver MAX30102 đo SpO2/HR |
| `fake_ecg.h` | Generator phát beat MIT-BIH cho DEMO_MODE |
| `fake_real_beats.h` | 4 beat MIT-BIH thật cho BYPASS TEST + DEMO |

### Pipeline xử lý

```
ADC 360Hz (ISR timer) → ring buffer
        ↓
loop() pop → /4095 → ecg_buffer (540 mẫu)
        ↓ khi buffer đầy
process_beat():
  1. z-score normalize
  2. R-peak detect (adaptive threshold mean + 3*std)
  3. Cắt window 187 mẫu quanh peak
  4. CNN inference (~28ms)
        ↓
Vote 10s: gom 7-10 nhịp → chốt label ổn định
        ↓
publishResult() + publishRaw() qua MQTT
        ↓
Đồng thời: update OLED + LED + buzzer
```

### Tính năng đặc biệt

- **BYPASS TEST lúc boot**: feed 4 beat MIT-BIH thật vào CNN để verify code C tương đương Python
- **DEMO_MODE**: phát beat MIT-BIH giả theo chu kỳ 54s (Normal 30s → SVE 8s → VEB 8s → Fusion 8s) khi không có AD8232
- **Fallback Normal**: vote 10s nếu best != Normal và conf < 0.7 → fallback Normal (tránh false alarm)
- **Cascade decision**: chỉ gửi sóng raw lên cloud khi edge phát hiện bất thường

---

## 5. Phần mềm Gateway

`ecg_gateway/gateway.py` — Python bridge ~150 dòng

**Vai trò**:
- Subscribe MQTT local broker (Mosquitto trên laptop, port 1883)
- Forward message lên AWS IoT Core qua MQTT TLS (port 8883)
- Quản lý certs/key TLS authentication
- Thêm `server_time` (timestamp Unix) vào payload trước khi forward (vì ESP32 chỉ có millis từ boot)

**Tại sao cần gateway?**
- ESP32 không đủ RAM để chạy MQTT TLS handshake với AWS (X.509 cert + SigV4)
- Gateway làm "proxy" — ESP32 chỉ cần MQTT TCP đơn giản
- Trong production có thể thay bằng Raspberry Pi nhỏ gọn

---

## 6. AI — Model Edge & Cloud

### Dataset: MIT-BIH Arrhythmia Database
- 48 bản ghi, 30 phút/bản, 360Hz, ~110K nhịp đã chú thích
- Chuẩn vàng FDA cho thiết bị y tế đo ECG
- Bệnh viện Beth Israel + MIT Lab for Computational Physiology

### Chia inter-patient theo Chazal et al.
- **DS1** (22 bệnh nhân) → train
- **DS2** (22 bệnh nhân khác) → test
- Loại 4 bệnh nhân paced (102, 104, 107, 217)
- → Model phải tổng quát hoá sang bệnh nhân mới, không phải nhớ thuộc

### Phân loại 5 lớp AAMI EC57

| AAMI | Tên | Ý nghĩa lâm sàng |
|------|-----|-------------------|
| **N** | Normal | Nhịp khoẻ, phát từ nút SA, P + QRS hẹp |
| **S** | Supraventricular | Phát từ nhĩ, đến sớm, P bất thường, QRS hẹp |
| **V** | Ventricular | **NGUY HIỂM** — phát từ thất, không P, QRS rộng + biến dạng |
| **F** | Fusion | Lai N + V |
| **Q** | Unknown / Paced | Không xác định / máy tạo nhịp |

### Model Edge (chạy ESP32)

CNN 1D 3 lớp, ~11K params, ~28ms/inference:
```
Input (187, 1)
  → Conv1D(16 filter, kernel=7, stride=2, ReLU) → (94, 16)
  → Conv1D(32 filter, kernel=5, stride=2, ReLU) → (47, 32)
  → Conv1D(64 filter, kernel=3, stride=2, ReLU) → (24, 64)
  → GlobalAveragePooling
  → Dense(32, ReLU) + Dropout(0.3)
  → Dense(5, Softmax)
```

### Model Cloud (chạy AWS Lambda)

ONNX format, ~100K+ params, ~200ms/inference. Sâu hơn Edge → chính xác hơn để xác minh chéo.

### Cascade Edge-Cloud

```
ESP32 inference → Normal conf >= 0.8?
    YES → Publish nhẹ, không gửi cloud (tiết kiệm 90% cost)
    NO  → Publish + gửi 187 mẫu raw qua MQTT 'ecg/raw'
              ↓
        AWS Lambda chạy ONNX → cloud_label
              ↓
        agrees = (edge_abnormal AND cloud_abnormal)
              ↓
        agrees == TRUE → Publish 'ecg/alert' → IoT Rule → SNS Email
        agrees == FALSE → Bỏ qua, chỉ log DynamoDB
```

### Tại sao không chạy AI ở gateway?
- **SPOF**: gateway chết → mất AI cho mọi thiết bị
- **Scale**: 1000 thiết bị cần 1000 gateway (không khả thi)
- **Bảo trì**: update model phải đến từng nhà
- **Compliance y tế**: AWS có HIPAA, máy local không có
- **Triết lý IoT chuẩn**: Edge nhanh + Cloud chính xác + Gateway chỉ bridge

---

## 7. AWS Cloud

### Dịch vụ sử dụng

| Service | Vai trò |
|---------|---------|
| **IoT Core** | MQTT broker, 3 IoT Rules định tuyến |
| **Lambda** | Chạy ONNX inference cho cascade verify |
| **DynamoDB** | 2 bảng audit log |
| **SNS** | Gửi email cảnh báo |
| **Cognito** | Auth User Pool + Identity Pool cho dashboard |
| **IAM** | Roles cho Lambda, Identity Pool, IoT Rule |

### 3 IoT Rules

| Rule | Topic subscribe | Action |
|------|-----------------|--------|
| `ECG_Result_to_DDB` | `ecg/result` | Insert DynamoDB `ecg-events` |
| `ECG_Raw_to_Lambda` | `ecg/raw` | Invoke Lambda inference |
| `ECG_Alert_SNS` | `ecg/alert` | Publish SNS topic → email |

### 2 bảng DynamoDB

| Bảng | Partition Key | Sort Key | Nội dung |
|------|---------------|----------|----------|
| `ecg-events` | device_id | timestamp | Mọi kết quả edge (audit) |
| `ecg-alerts` (errorAction) | device_id | timestamp | Lỗi để debug |

### Lambda handler

`lambda/handler.py`:
1. Nhận event từ IoT Rule `ecg/raw`
2. Run ONNX inference trên 187 mẫu
3. So sánh với edge label
4. Publish `ecg/verified` lên dashboard (mỗi inference)
5. Nếu `agrees AND abnormal` → publish `ecg/alert` → email SNS
6. Log vào DynamoDB `ecg-events`

---

## 8. Dashboard Web

Next.js 16 + React + Recharts + TailwindCSS, deploy local (npm run dev)

### Trang chính (`/`)

| Component | Hiển thị |
|-----------|----------|
| **Header** | Status AWS + Device online (chấm xanh/đỏ) |
| **Alert banner** | Cảnh báo xác nhận kép (đỏ) khi nhận `ecg/alert` |
| **Card Nhịp tim** | BPM realtime |
| **Card Phân loại** | Edge label + confidence (badge màu) |
| **Card Cloud verify** | Label cloud + agrees ✓/✗ |
| **Biểu đồ sóng ECG** | Realtime stream từ ESP32 |
| **Biểu đồ BPM** | Sparkline 60 điểm gần nhất |
| **Biểu đồ phân bố lớp** | Bar chart đếm nhịp trong session |
| **Probability bars** | 5 thanh xác suất edge |

### Trang lịch sử cảnh báo (`/alerts`)

- Query DynamoDB qua API route `/api/events`
- Filter `alertOnly=true` để chỉ hiển thị nhịp đáng ngờ

### Auth

- **Cognito User Pool**: đăng ký/đăng nhập bằng email
- **Cognito Identity Pool**: cấp temporary AWS credentials
- **API `/api/iot-attach`**: tự động attach IoT Policy `ECG-Dashboard-Cognito-Policy` cho identity ID
- **AWS SigV4 WebSocket MQTT**: browser kết nối trực tiếp IoT Core (không qua gateway)

### MQTT topics subscribe

```typescript
client.subscribe(['ecg/result', 'ecg/raw', 'ecg/alert', 'ecg/verified'])
```

---

## 9. Quá trình phát triển

### Notebook training: `ecg_aami_inter_patient.ipynb`

Train trên Kaggle (GPU T4 free):

1. Load MIT-BIH bằng `wfdb`
2. Chia DS1/DS2 inter-patient
3. Cắt nhịp 187 mẫu quanh R-peak từ annotation
4. Z-score normalize từng nhịp
5. Map 15 loại MIT-BIH → 5 lớp AAMI
6. Augmentation cho lớp thiểu số
7. Train 2 model: Edge (nhẹ) + Cloud (nặng) với class_weights
8. Callback: EarlyStopping patience=8, ReduceLROnPlateau patience=6
9. Save `best_aami.h5` + `best_cloud.onnx`
10. Script export Edge weights ra C array `ecg_weights_aami.h`

### Vấn đề đã giải quyết trong quá trình làm

| Vấn đề | Cách xử lý |
|--------|------------|
| 2 OLED cùng địa chỉ I2C | Dùng 2 bus Wire + Wire1 |
| Crash LoadProhibited PubSubClient | Check `WiFi.status()` TRƯỚC khi gọi `mqtt.connected()` |
| Boot loop ESP32 | Tắt brownout detector, dùng `WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0)` |
| Timer 360Hz không chính xác | Calib `timerAlarm(3306)` thay vì 2778 |
| ADC dính rail 4095 | Detect saturation, defensive skip inference |
| Domain shift AD8232 vs MIT-BIH | Vote 10s + fallback Normal khi conf thấp |
| Cascade verify cloud không khớp UI | Lambda so sánh display label (sau fallback) thay vì raw_label |
| Dashboard mất MQTT connection | Bổ sung topic `ecg/verified` vào IoT Policy |

---

## 10. Demo & Trải nghiệm người dùng

### Chế độ DEMO_MODE (chu kỳ 54s)

| Thời gian | Loại | Đèn | Buzzer | Cảnh báo |
|-----------|------|-----|--------|-----------|
| 0-30s | Normal | Xanh | Beep nhịp | Không |
| 30-38s | SVE | Vàng | Beep cảnh báo | Cloud verify |
| 38-46s | VEB | Đỏ | Còi liên tục | Email SNS |
| 46-54s | Fusion | Vàng | Beep cảnh báo | Cloud verify |
| Lặp lại | | | | |

### Chế độ thật (AD8232)

1. Dán 3 điện cực: RA dưới đòn phải, LA dưới đòn trái, RL sườn trái dưới
2. Cấp nguồn pin → tránh nhiễu 50Hz từ adapter
3. Ngồi yên hoàn toàn 30s đầu để baseline ổn định
4. Quan sát đèn + dashboard

---

## 11. Tài liệu trong project

| File | Mô tả |
|------|-------|
| `PROJECT_SUMMARY.md` | Bản tổng hợp này |
| `MODEL_README.md` | Chi tiết train model |
| `ARCHITECTURE.md` | Sơ đồ kiến trúc cũ |
| `note_thuyet_trinh.txt` | Ghi chú thuyết trình |
| `Thuyết trình IOT.pptx` | Slide |
| `data-flow.excalidraw` | Sơ đồ data flow |
| `roadmap_IOT.excalidraw` | Roadmap dự án |
| `VideoDemo/` | Video quay demo |

---

## 12. Trả lời câu hỏi giảng viên — Cheatsheet

### Q: Em làm gì trong đồ án?
> Em xây hệ thống IoT theo dõi nhịp tim realtime — ESP32 đo ECG bằng AD8232, chạy AI phân loại ngay trên thiết bị, gửi kết quả qua MQTT lên AWS Cloud, dashboard web hiển thị realtime. Khi phát hiện bất thường, hệ thống cảnh báo cục bộ bằng đèn + còi và gửi email người thân.

### Q: Điểm khác biệt so với máy ECG thông thường?
> Hệ thống của em có **AI phân loại 5 lớp** ngay trên thiết bị, cảnh báo tự động không cần bác sĩ đọc. Kiến trúc cascade Edge-Cloud giúp giảm 90% false alarm so với chỉ dùng 1 model.

### Q: Train model gì, dataset nào?
> CNN 1D nhỏ cho edge và CNN lớn cho cloud, train trên MIT-BIH Arrhythmia Database — chuẩn vàng FDA Mỹ. Chia inter-patient DS1/DS2 theo Chazal et al. để model phải tổng quát hoá sang bệnh nhân mới.

### Q: Sao chạy được AI trên ESP32?
> Em train Keras trên Kaggle, export weights ra mảng C `static const float[]`, viết tay Conv1D + Dense + Softmax bằng C thuần. Tổng weights 44KB nhúng vào firmware. ESP32 có FPU nên float32 chạy được, inference 28ms/nhịp.

### Q: Tại sao có cả Edge và Cloud?
> Edge nhanh nhưng model nhỏ → có thể nhầm. Cloud chậm hơn nhưng model lớn chính xác hơn. Cascade AND: chỉ alert khi cả 2 đồng ý → giảm false alarm. Edge vẫn cảnh báo cục bộ khi mất mạng → không phụ thuộc internet.

### Q: Tại sao không chạy AI ở gateway?
> Gateway là máy local → single point of failure. Cloud có SLA cao, scale tự động, có HIPAA compliance cho y tế. Edge AI thì offline được. Gateway chỉ làm protocol bridge — đơn giản → ít bug.

### Q: Phần khó nhất khi làm?
> Train model nhỏ vừa chạy được trên ESP32 vừa giữ accuracy chấp nhận được. Em phải cân bằng class_weights vì N chiếm 80%, dùng GlobalAveragePooling thay vì Flatten để giảm tham số, và inter-patient split để tránh accuracy ảo.
