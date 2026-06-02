# Mô hình AI phân loại nhịp tim — Tóm tắt

Bản tóm tắt dùng để trả lời câu hỏi giảng viên: "Train model gì? Dataset nào?"

## 1. Bài toán

Phân loại **5 lớp nhịp tim theo chuẩn AAMI EC57** từ tín hiệu ECG 1 chuyển đạo:

| AAMI | Tên đầy đủ | Ý nghĩa lâm sàng |
|------|------------|------------------|
| **N** | Normal | Nhịp bình thường |
| **S** | Supraventricular (SVE) | Trên thất — tâm nhĩ |
| **V** | Ventricular (VEB) | Thất — rung thất (nguy hiểm) |
| **F** | Fusion | Nhịp hỗn hợp N + V |
| **Q** | Unknown / Paced | Không xác định / có máy tạo nhịp |

## 2. Dataset: MIT-BIH Arrhythmia Database

- **Nguồn**: PhysioNet MIT-BIH (chuẩn vàng quốc tế cho nghiên cứu ECG)
- **48 bản ghi**, mỗi bản 30 phút, sampling 360 Hz, chuyển đạo MLII
- **Khoảng 110,000 nhịp tim** đã được bác sĩ chú thích
- **Mapping MIT-BIH → AAMI**: 15 loại annotation gốc được gộp về 5 lớp N/S/V/F/Q theo khuyến cáo AAMI EC57

## 3. Chia tập theo **Inter-patient** (DS1 / DS2)

Đây là điểm **quan trọng** trong báo cáo:

- **DS1** (22 bệnh nhân) → train
- **DS2** (22 bệnh nhân khác) → test
- 4 bản có máy tạo nhịp (paced) bị loại

**Tại sao inter-patient?**
- Cách *intra-patient* (chia ngẫu nhiên) cho accuracy ảo > 99% nhưng **không phản ánh thực tế** — model thấy nhịp cùng người trong cả train và test
- *Inter-patient* mô phỏng đúng thực tế: model phải tổng quát hoá sang **bệnh nhân chưa từng thấy**
- Đây là chuẩn đánh giá nghiêm túc của AAMI

## 4. Tiền xử lý

1. Phát hiện đỉnh R (R-peak) từ annotation của MIT-BIH
2. Cắt cửa sổ **187 mẫu** quanh mỗi R-peak (~520ms @ 360Hz)
3. Z-score normalization mỗi nhịp (mean=0, std=1)
4. Class weights để cân bằng (vì N chiếm > 80%)

## 5. Kiến trúc model EDGE (chạy trên ESP32)

CNN 1D **siêu nhẹ**, tối ưu cho vi điều khiển:

```
Input  (187, 1)
  → Conv1D(16 filters, kernel=7, stride=2, ReLU)  → (94, 16)
  → Conv1D(32 filters, kernel=5, stride=2, ReLU)  → (47, 32)
  → Conv1D(64 filters, kernel=3, stride=2, ReLU)  → (24, 64)
  → GlobalAveragePooling
  → Dense(32, ReLU)
  → Dropout(0.3)
  → Dense(5, Softmax)
```

- **Tổng tham số**: ~11,000 (rất nhỏ, chạy được trên ESP32)
- **Loss**: Categorical Cross-Entropy + class weights
- **Optimizer**: Adam
- **Batch size**: 256, **Epochs**: ~20 (early stopping)
- **Inference**: ~28ms / nhịp trên ESP32 @ 240MHz

## 6. Kiến trúc model CLOUD (chạy trên AWS Lambda)

Model lớn hơn, ONNX format, dùng để **xác minh chéo** edge:
- CNN sâu hơn, nhiều tham số hơn (~100K+)
- Chạy CPU AWS Lambda, latency ~200ms / nhịp
- Threshold riêng cho VEB để tăng độ nhạy

## 7. Cascade Edge-Cloud (điểm bán hàng kiến trúc)

Mỗi nhịp:
1. **ESP32 (Edge)** chạy CNN nhỏ → có kết quả ngay
2. Nếu **bất thường** (S/V/F) → gửi sóng RAW lên AWS qua MQTT
3. **AWS Lambda (Cloud)** chạy ONNX model lớn → xác minh
4. Chỉ khi **cả 2 đồng ý bất thường** → gửi email cảnh báo qua SNS

**Lợi ích**:
- Edge nhanh, không phụ thuộc internet
- Cloud lọc false alarm của edge → email không spam
- Bệnh nhân vẫn được theo dõi liên tục offline (chỉ mất cảnh báo)

## 8. Kết quả

(Bạn điền vào theo notebook thực tế khi đã train xong)

| Metric | Edge | Cloud |
|--------|------|-------|
| Accuracy | ~XX% | ~XX% |
| VEB Sensitivity | XX% | XX% |
| VEB PPV | XX% | XX% |
| S Sensitivity | XX% | XX% |
| F Sensitivity | XX% | XX% |

**Lưu ý**: vì inter-patient nên accuracy chỉ ~85-90% là **bình thường** trong nghiên cứu — không phải > 99% như chia ngẫu nhiên.

## 9. Cách AI chạy được trên ESP32 (chi tiết kỹ thuật)

ESP32 chỉ có **520KB RAM + 240MHz CPU** — KHÔNG có Python, KHÔNG có TensorFlow Lite, KHÔNG có GPU. Để CNN chạy được, nhóm em làm thủ công 3 bước:

### Bước 1: Train model bằng Keras/Python (trên Kaggle/PC)
- Notebook `ecg_aami_inter_patient.ipynb` train model trong Python/TensorFlow
- Sau khi train xong, model lưu dạng `.h5` (Keras format) — file ~50KB

### Bước 2: Export weights ra C array
- Viết script Python đọc file `.h5` → trích từng tensor weights ra
- Convert thành mảng C `static const float[]` lưu trong file `ecg_weights_aami.h`
- **File này được biên dịch vào firmware**, nằm trong **flash memory** (4MB của ESP32), không tốn RAM
- Tổng weights ~11,000 số float = **~44KB flash** (chấp nhận được)

```c
// ecg_weights_aami.h (auto-generated)
static const float conv1_w[112] = { 0.252f, -0.153f, ... };
static const float conv1_b[16]  = { ... };
static const float conv2_w[2560] = { ... };
// ... cứ thế cho 5 layers
```

### Bước 3: Tự viết Conv1D + Dense + Softmax bằng C thuần

Vì ESP32 không có TFLite, nhóm em **viết lại từng phép tính** trong file `inference.h`:

**Conv1D với padding=same, stride=2, ReLU**:
```c
for (int o = 0; o < out_len; o++) {        // duyệt output position
    for (int f = 0; f < out_ch; f++) {     // duyệt output filter
        float sum = bias[f];
        for (int k = 0; k < kernel; k++) { // duyệt kernel
            for (int c = 0; c < in_ch; c++) {
                sum += input[...] * weights[...];
            }
        }
        output[...] = (sum > 0) ? sum : 0;  // ReLU
    }
}
```

**Pipeline đầy đủ** (`runInference()`):
1. Conv1D(16, k=7, s=2, ReLU) → output (94, 16)
2. Conv1D(32, k=5, s=2, ReLU) → output (47, 32)
3. Conv1D(64, k=3, s=2, ReLU) → output (24, 64)
4. GlobalAveragePooling → (64,)
5. Dense(32) + ReLU
6. Dense(5) + Softmax → 5 xác suất

**Argmax** lấy class có xác suất cao nhất → trả về label.

### Tối ưu để chạy trên ESP32

| Khía cạnh | Giải pháp |
|-----------|-----------|
| **RAM nhỏ (520KB)** | Buffers Conv1/2/3 dùng `static` → không tạo trên stack (~20KB tổng) |
| **CPU yếu** | Model nhỏ chỉ 11K params, ~28ms/nhịp @ 240MHz |
| **Không có FPU mạnh** | Dùng `float32` (ESP32 có FPU), không cần quantization INT8 |
| **Flash limited (4MB)** | Weights ~44KB nhỏ gọn, dư chỗ cho firmware + WiFi stack |
| **Không có batch** | Inference 1 nhịp 1 lần — match real-time requirement |

### Verify tính đúng đắn

Trong `setup()` có **BYPASS TEST**: feed 4 beat MIT-BIH chuẩn (file `fake_real_beats.h`) qua CNN C → in label dự đoán. So với label gốc → confirm code C tương đương Python.

```
===== [BYPASS TEST] dua thang beat that vao CNN =====
  Beat THAT [N (Normal)]  -> CNN du doan: Normal       conf=0.92 [DUNG]
  Beat THAT [V (VEB)]     -> CNN du doan: Ventricular  conf=0.97 [DUNG]
```

### Tóm tắt 1 câu

> Em train CNN bằng Keras → export weights ra mảng C → viết lại Conv1D/Dense/Softmax bằng C thuần → biên dịch trực tiếp vào firmware Arduino. Toàn bộ inference 28ms/nhịp, không cần thư viện ngoài, không cần internet.

## 10. Trả lời câu hỏi giảng viên

**Q: Train model gì?**
A: CNN 1D 3 lớp Conv + Dense + Softmax cho edge, ONNX CNN lớn hơn cho cloud. Bài toán 5-class classification theo AAMI EC57.

**Q: Dataset nào?**
A: MIT-BIH Arrhythmia Database từ PhysioNet. Đây là dataset chuẩn vàng cho nghiên cứu ECG, 48 bản ghi 30 phút, 110K nhịp đã được bác sĩ chú thích.

**Q: Chia train/test thế nào?**
A: Theo chuẩn inter-patient (DS1/DS2 của Chazal et al.) — train trên 22 bệnh nhân, test trên 22 bệnh nhân khác. Không chia ngẫu nhiên để model phải tổng quát hoá.

**Q: Tại sao không dùng intra-patient cho accuracy cao?**
A: Intra-patient cho ra accuracy ảo > 99% vì model thấy nhịp cùng người trong cả train và test. Trong thực tế, hệ thống gặp bệnh nhân **mới hoàn toàn** — inter-patient phản ánh đúng tình huống đó. Đây là khuyến cáo của AAMI EC57.

**Q: Model có gì đặc biệt?**
A: Kiến trúc **cascade edge-cloud**:
- Model nhỏ chạy real-time trên ESP32 (~28ms/nhịp)
- Model lớn chạy trên cloud xác minh
- Chỉ alert khi 2 model đồng ý → giảm false alarm
