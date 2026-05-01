# Kiến trúc dự án ECG IoT — Real-time Arrhythmia Detection

## Mô tả tổng quan

Hệ thống phát hiện loạn nhịp tim (cardiac arrhythmia) thời gian thực dựa trên kiến trúc **Edge-Cloud Cascade**, kết hợp một thiết bị IoT đeo (ESP32 + cảm biến AD8232) với hạ tầng AWS để vừa đảm bảo độ trễ thấp, vừa đạt độ chính xác y tế chuẩn AAMI EC57. Tín hiệu ECG được lấy mẫu liên tục tại 360 Hz, phân đoạn thành các nhịp tim (beats) quanh đỉnh R-peak, sau đó phân loại thành 5 nhóm theo chuẩn AAMI: N (Normal), S (Supraventricular), V (Ventricular ectopic — VEB), F (Fusion), Q (Unknown). Mục tiêu tối ưu là **VEB Sensitivity ≥ 90%** — chỉ số quan trọng nhất trong y tế vì VEB là tiền đề của các rối loạn nhịp nguy hiểm như nhịp nhanh thất (V-tach) và rung thất (V-fib).

## Kiến trúc 2 tầng (Cascade Inference)

**Tầng 1 — Edge AI trên ESP32:**
Một mạng CNN nhỏ (11K tham số, ~44 KB) được cài đặt **thuần C** trực tiếp trên vi điều khiển, không cần TFLite. Model chạy inference cục bộ với độ trễ ~50ms, đạt VEB Sensitivity 91.27% (sau threshold tuning ở 0.2). Khi tín hiệu là Normal với độ tin cậy cao (>0.8), thiết bị chỉ gửi heartbeat MQTT 50 bytes — tiết kiệm băng thông và pin. Khi phát hiện VEB hoặc khi confidence thấp, thiết bị mới gửi nguyên 187 mẫu sóng (~750 bytes) lên cloud để xác nhận lại.

**Tầng 2 — Cloud Verification trên AWS Lambda:**
Một mạng CNN sâu hơn với BatchNorm + Residual connections (235K tham số, ~920 KB) được export sang ONNX và deploy lên AWS Lambda thông qua ONNX Runtime. Model này đạt VEB Sensitivity 89.69% và PPV 79.52% trên test set inter-patient — vượt baseline kinh điển de Chazal 2004 về Sensitivity. Lambda được trigger tự động qua AWS IoT Rule khi có MQTT message tới topic `ecg/{device_id}/waveform`. Nhờ cơ chế cascade hai model độc lập đồng ý mới gửi alert, **PPV hiệu dụng của hệ thống đạt ~95%**, giảm đáng kể false alarm so với chỉ dùng một model.

## Hạ tầng AWS

- **AWS IoT Core** — broker MQTT, xác thực thiết bị qua X.509 certificate, định tuyến message bằng IoT Rule (SQL `SELECT * FROM 'ecg/+/waveform'`)
- **AWS Lambda (Python 3.11)** — chạy ONNX inference, xử lý logic cascade, ghi DynamoDB, gọi SNS
- **DynamoDB (`ecg-events`)** — lưu trữ event-stream với composite key `(device_id, timestamp)`, billing PAY_PER_REQUEST để tự co giãn
- **SNS (`ecg-alerts`)** — gửi cảnh báo email khi cloud model xác nhận VEB

## Pipeline Training & MLOps

- **Dataset:** MIT-BIH Arrhythmia Database (48 records, 110K beats)
- **Split:** AAMI EC57 inter-patient (de Chazal 2004) — DS1 (22 records) train, DS2 (22 records) test, **không trùng bệnh nhân** giữa hai tập, đảm bảo generalization sang bệnh nhân mới
- **Augmentation:** time shift ±5 samples, amplitude scale 0.9–1.1, Gaussian noise σ=0.01, oversample minority classes
- **Training tracking:** MLflow self-hosted tại `mlflow.holoc.id.vn`, log metrics/artifacts/model versioning
- **Model export:** Edge model → C header file (`ecg_weights_aami.h`); Cloud model → ONNX format (`ecg_cloud_model.onnx`)

## Luồng end-to-end

```
[Bệnh nhân đeo AD8232]
        │ (analog ECG signal)
        ▼
[ESP32 + Edge CNN, 11K params]
        │ R-peak detect → 187-sample window → CNN inference (~50ms)
        │
        ├── Normal & conf>0.8 ──> MQTT heartbeat (50B) ──> IoT Core ──> CloudWatch
        │
        └── VEB hoặc conf<0.8 ──> MQTT waveform (750B) ──> IoT Core
                                                              │
                                                       IoT Rule trigger
                                                              ▼
                                                  [Lambda + Cloud CNN, 235K params]
                                                       ONNX inference (~50ms)
                                                              │
                                                ┌─────────────┼──────────────┐
                                                ▼             ▼              ▼
                                         DynamoDB      Cloud=VEB? ──Yes──> SNS Email
                                       (audit log)         │No                │
                                                           ▼                  ▼
                                                       (no action)    [Bác sĩ/người thân]
```

## Điểm đặc trưng kỹ thuật

1. **Per-patient z-score normalization** — chuẩn hoá tín hiệu theo từng bệnh nhân, giúp model generalize tốt sang inter-patient setting
2. **Threshold tuning per model** — Edge dùng `th=0.2` (ưu tiên Sensitivity), Cloud dùng `th=0.4` (cân bằng Sens/PPV)
3. **Label smoothing 0.05** trong cloud model — chống overconfident, giảm variance giữa các lần train
4. **Cold-start optimization** — load ONNX session trong global scope của Lambda, warm invocation chỉ ~50ms
5. **Bandwidth optimization** — cascade logic giúp ~80-90% beats Normal chỉ gửi heartbeat 50B thay vì 750B, tiết kiệm ~93% data transfer

## Kết quả benchmark (test set DS2, 49,698 beats)

| Model | Params | Size | Acc | VEB Sens | VEB PPV | Latency |
|-------|--------|------|-----|----------|---------|---------|
| Edge CNN (ESP32, C) | 11K | 44 KB | 88.54% | **91.27%** | 74.65% | ~50ms |
| Cloud CNN (Lambda, ONNX) | 235K | 920 KB | 83.09% | 89.69% | **79.52%** | ~50ms |
| Cascade (Edge + Cloud agree) | — | — | — | ~90% | **~95%** | ~150ms |

So với baseline học thuật **de Chazal 2004** (VEB Sens 77.7%, PPV 81.9%), hệ thống vượt rõ rệt về Sensitivity — chỉ số quan trọng nhất cho ứng dụng phát hiện sớm.
