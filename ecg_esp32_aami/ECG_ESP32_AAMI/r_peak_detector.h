#ifndef R_PEAK_DETECTOR_H
#define R_PEAK_DETECTOR_H

#include <math.h>
#include "config.h"

// ── R-peak Detector (Pan-Tompkins simplified) ──────────────────
// Workflow:
//   raw signal → derivative → square → find peak gan center
//
// Khong dung bandpass filter de don gian (FIR filter ton CPU + RAM)
// Voi tin hieu da z-score normalize, derivative + square du chinh xac
// cho 90% truong hop. Neu noise nhieu, them moving window integration.

// Buffer phu cho derivative + squared signal
// Dung static de tranh tran stack
static float deriv_buf[BUFFER_SIZE];
static float square_buf[BUFFER_SIZE];

// ── Tinh dao ham bac 1 (5-point Pan-Tompkins) ─────────────────
// y[n] = (1/8) * (-x[n-2] - 2*x[n-1] + 2*x[n+1] + x[n+2])
// Phuong phap nay vua tinh slope vua filter noise nhe
static void compute_derivative(const float* signal, int length, float* output) {
    // 2 sample dau va 2 sample cuoi: gan = 0 (khong du context)
    output[0] = 0;
    output[1] = 0;
    output[length-2] = 0;
    output[length-1] = 0;

    for (int i = 2; i < length - 2; i++) {
        output[i] = (-signal[i-2] - 2*signal[i-1] + 2*signal[i+1] + signal[i+2]) * 0.125f;
        // 0.125f = 1/8, nhan nhanh hon chia
    }
}

// ── Binh phuong de phong to peak + bo dau ─────────────────────
static void compute_square(const float* signal, int length, float* output) {
    for (int i = 0; i < length; i++) {
        output[i] = signal[i] * signal[i];
    }
}

// ── Tim R-peak gan giua buffer ────────────────────────────────
// Tra ve index cua R-peak hop le, hoac -1 neu khong tim duoc.
//
// Strategy:
//   1. Tinh threshold = 0.5 * max(squared_signal)
//   2. Trong vung center [center - search_range, center + search_range],
//      tim diem co squared > threshold va la local max
//   3. Diem nay tuong ung R-peak vi binh phuong dao ham max nhat
static int find_r_peak(const float* squared, int length) {
    int center = length / 2;     // 540/2 = 270
    int search_range = 100;       // ±100 samples ~ ±278ms (du 1 nhip o 60bpm)

    // Adaptive threshold robust voi outlier: dung mean + k*std thay vi 0.5*max.
    // Khi co spike (motion artifact), max bi keo len cao -> bo qua peak that.
    // Mean+std on dinh hon vi spike chiem ti le nho trong 540 mau.
    float sum = 0;
    for (int i = 0; i < length; i++) sum += squared[i];
    float mean = sum / length;
    float var = 0;
    for (int i = 0; i < length; i++) { float d = squared[i] - mean; var += d*d; }
    float stddev = sqrtf(var / length);
    // Threshold = mean + 3*std (R-peak thuong cao gap nhieu lan noise level)
    float threshold = mean + 3.0f * stddev;
    // Floor: neu noise rat thap, threshold tuyet doi 0.01 de loai duong bang
    if (threshold < 0.01f) threshold = 0.01f;

    // Tim local max trong vung center
    int   best_peak = -1;
    float best_val  = threshold;     // chi nhan peak vuot threshold

    int start = center - search_range;
    int end   = center + search_range;
    if (start < 1) start = 1;
    if (end > length - 1) end = length - 1;

    for (int i = start; i < end; i++) {
        // Local max: cao hon ca 2 hang xom truc tiep
        if (squared[i] > best_val &&
            squared[i] > squared[i-1] &&
            squared[i] > squared[i+1]) {
            best_val  = squared[i];
            best_peak = i;
        }
    }

    return best_peak;   // -1 neu khong tim duoc
}

// ── Public API: detect R-peak tu raw buffer ────────────────────
// Input : buffer da z-score normalize, do dai = BUFFER_SIZE (540)
// Output: index cua R-peak, hoac -1 neu khong tim duoc
//
// Caller nen kiem tra:
//   if peak_idx >= HALF_WINDOW && peak_idx <= BUFFER_SIZE - HALF_WINDOW - 1:
//       cat window 187 samples quanh peak
static int detect_r_peak(const float* signal, int length) {
    compute_derivative(signal, length, deriv_buf);
    compute_square(deriv_buf, length, square_buf);
    return find_r_peak(square_buf, length);
}

// ── Helper: cat window 187 samples quanh R-peak ────────────────
// Input : signal (540 samples), peak_idx (vi tri R-peak trong signal)
// Output: window (187 samples), R-peak o vi tri 93 (giua window)
// Return: true neu cat thanh cong, false neu peak qua gan bien
static bool extract_window(const float* signal, int peak_idx, float* window) {
    int start = peak_idx - HALF_WINDOW;       // 93 samples truoc peak
    int end   = peak_idx + HALF_WINDOW + 1;   // 93 samples sau peak (+1 cho center)

    if (start < 0 || end > BUFFER_SIZE) {
        return false;   // peak qua gan bien
    }

    for (int i = 0; i < SAMPLE_COUNT; i++) {
        window[i] = signal[start + i];
    }
    return true;
}

#endif // R_PEAK_DETECTOR_H
