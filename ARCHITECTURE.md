# Kiến trúc dự án ECG IoT — Real-time Arrhythmia Detection

## Mô tả tổng quan

Hệ thống phát hiện loạn nhịp tim (cardiac arrhythmia) thời gian thực dựa trên kiến trúc **Edge-Cloud Cascade**, kết hợp một thiết bị IoT đeo (ESP32 + cảm biến AD8232) với hạ tầng AWS để vừa đảm bảo độ trễ thấp, vừa đạt độ chính xác y tế chuẩn AAMI EC57. Tín hiệu ECG được lấy mẫu liên tục tại 360 Hz, phân đoạn thành các nhịp tim (beats) quanh đỉnh R-peak, sau đó phân loại thành 5 nhóm theo chuẩn AAMI: N (Normal), S (Supraventricular), V (Ventricular ectopic — VEB), F (Fusion), Q (Unknown). Mục tiêu tối ưu là **VEB Sensitivity ≥ 90%** — chỉ số quan trọng nhất trong y tế vì VEB là tiền đề của các rối loạn nhịp nguy hiểm như nhịp nhanh thất (V-tach) và rung thất (V-fib).

## Kiến trúc 2 tầng (Cascade Inference)

**Tầng 1 — Edge AI trên ESP32:**
Một mạng CNN nhỏ được cài đặt **thuần C** trực tiếp trên vi điều khiển, không cần TFLite. Model chạy inference cục bộ với độ trễ ~50ms, đạt VEB Sensitivity 91.77%. MỌI nhịp tim đều được gửi lên topic MQTT `ecg/result` (kèm label, confidence, probs, và SpO₂/nhịp tim từ MAX30102). Khi phát hiện bất thường hoặc confidence thấp, thiết bị gửi THÊM nguyên 187 mẫu sóng thô lên topic `ecg/raw` để cloud xác nhận lại. Ngoài ra ESP32 còn có màn OLED hiển thị sóng ECG realtime + SpO₂ tại chỗ.

**Tầng 2 — Cloud Verification trên AWS Lambda:**
Một mạng CNN sâu hơn với BatchNorm + Residual connections (~925 KB ONNX) được export sang ONNX và deploy lên AWS Lambda thông qua ONNX Runtime. Model này đạt Accuracy 90.90% và VEB PPV 84.21% trên test set inter-patient — PPV cao hơn hẳn edge (lọc báo động giả tốt hơn), đúng vai trò verify trong cascade. Lambda được trigger tự động qua AWS IoT Rule khi có MQTT message tới topic `ecg/raw`. Nhờ cơ chế cascade — chỉ gửi alert khi CẢ edge VÀ cloud cùng phát hiện bất thường — hệ thống giảm đáng kể false alarm so với chỉ dùng một model.

## Hạ tầng AWS

- **AWS IoT Core** — broker MQTT, xác thực thiết bị qua X.509 certificate, định tuyến message bằng 3 IoT Rule:
  - `ECG_to_DynamoDB`: `SELECT * FROM 'ecg/result'` → ghi mọi nhịp vào bảng `ecg_results`
  - `ECG_to_CloudInference`: `SELECT * FROM 'ecg/raw'` → trigger Lambda verify
  - `ECG_Alert_SNS`: `SELECT * FROM 'ecg/alert'` → gửi SNS email
- **AWS Lambda (Python 3.11)** — chạy ONNX inference, xử lý logic cascade, ghi DynamoDB `ecg-events`, publish `ecg/alert`
- **DynamoDB** — 2 bảng: `ecg_results` (raw log mọi nhịp, ghi trực tiếp từ IoT Rule) và `ecg-events` (kết quả cloud-verified có cấu trúc: cloud_label, spo2, hr_ppg, is_alert...), composite key `(device_id, timestamp)`, billing PAY_PER_REQUEST
- **SNS (`ecg-alerts`)** — gửi cảnh báo email khi cloud model xác nhận bất thường
- **Cảm biến SpO₂ (MAX30102)** — đo nồng độ oxy máu + nhịp tim PPG (I2C), nhúng trong payload `ecg/result` & `ecg/raw`, hiển thị xu hướng trên dashboard

## Pipeline Training & MLOps

- **Dataset:** MIT-BIH Arrhythmia Database (48 records, 110K beats)
- **Split:** AAMI EC57 inter-patient (de Chazal 2004) — DS1 (22 records) train, DS2 (22 records) test, **không trùng bệnh nhân** giữa hai tập, đảm bảo generalization sang bệnh nhân mới
- **Augmentation:** time shift ±5 samples, amplitude scale 0.9–1.1, Gaussian noise σ=0.01, oversample minority classes
- **Training tracking:** MLflow self-hosted tại `mlflow.holoc.id.vn`, log metrics/artifacts/model versioning
- **Model export:** Edge model → C header file (`ecg_weights_aami.h`); Cloud model → ONNX format (`ecg_cloud_model.onnx`)

## Luồng end-to-end

```
[Bệnh nhân đeo AD8232 + MAX30102]
        │ (analog ECG signal + PPG SpO₂)
        ▼
[ESP32 + Edge CNN]  ── OLED hiển thị tại chỗ
        │ R-peak detect → 187-sample window → CNN inference (~50ms)
        │
        ├── MỌI nhịp ─────────> MQTT 'ecg/result' ─┐
        │                                          │
        └── bất thường/conf thấp ─> MQTT 'ecg/raw' │
                                          │        │
                              (gateway laptop forward TLS)
                                          │        │
                                          ▼        ▼
                                      AWS IoT Core
                                          │        │
                         IoT Rule 'ecg/raw'│        │IoT Rule 'ecg/result'
                                          ▼        ▼
                          [Lambda + Cloud CNN]   DynamoDB 'ecg_results'
                            ONNX verify (~50ms)    (raw log mọi nhịp)
                                  │
                    ┌─────────────┼──────────────┐
                    ▼             ▼              ▼
            DynamoDB        edge VÀ cloud    publish 'ecg/alert'
            'ecg-events'    cùng abnormal? ──Yes──> IoT Rule → SNS Email
            (verified)          │No                        │
                                ▼                          ▼
                            (no alert)            [Bác sĩ/người thân]
```

## Điểm đặc trưng kỹ thuật

1. **Per-patient z-score normalization** — chuẩn hoá tín hiệu theo từng bệnh nhân, giúp model generalize tốt sang inter-patient setting
2. **Threshold tuning per model** — Edge ưu tiên Sensitivity (bắt nhiều VEB), Cloud dùng `VEB_THRESHOLD=0.3` (cân bằng Sens/PPV, đạt PPV>80%)
3. **Label smoothing 0.05** trong cloud model — chống overconfident, giảm variance giữa các lần train
4. **Cold-start optimization** — load ONNX session trong global scope của Lambda, warm invocation chỉ ~50ms
5. **Bandwidth optimization** — cascade logic: nhịp Normal chỉ gửi `ecg/result` (payload nhẹ ~600B, có label + probs + SpO₂ + waveform rút gọn 47 mẫu); chỉ nhịp bất thường mới gửi THÊM `ecg/raw` (187 mẫu thô) lên cloud verify — giảm tải cloud inference cho phần lớn nhịp Normal

## Kết quả benchmark (test set DS2, 49,698 beats)

| Model | Size | Acc | VEB Sens | VEB PPV | Latency |
|-------|------|-----|----------|---------|---------|
| Edge CNN (ESP32, C) | thuần C, nhúng | 85.96% | **91.77%** | 71.24% | ~50ms |
| Cloud CNN (Lambda, ONNX) | ~925 KB | **90.90%** | 76.68% | **84.21%** | ~50ms |

**Phân vai cascade:** Edge ưu tiên **Sensitivity** (VEB Sens 91.77% — bắt nhiều, ít bỏ sót); Cloud ưu tiên **Precision/PPV** (84.21% — xác nhận chính xác, lọc báo động giả) và Accuracy tổng (90.90% > edge). Hai tầng bổ trợ nhau: nhạy ở edge, đặc hiệu ở cloud.

So với baseline học thuật **de Chazal 2004** (VEB Sens 77.7%, PPV 81.9%), edge model vượt rõ về Sensitivity (91.77%) — chỉ số quan trọng nhất cho phát hiện sớm; cloud model đạt PPV 84.21% tương đương baseline.

> Ghi chú: lớp S (Supraventricular) và F (Fusion) có sensitivity thấp ở cả 2 model — hạn chế cố hữu của inter-patient split (S/F ít mẫu, đặc trưng dễ lẫn N), cũng là thách thức chung trong các nghiên cứu AAMI.
