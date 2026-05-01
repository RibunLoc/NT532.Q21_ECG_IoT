#ifndef NORMALIZE_H
#define NORMALIZE_H

#include <math.h>

// ── Z-score normalization (in-place, 1-loop variance) ─────────
// Khop voi pipeline training:
//   signal = (signal - signal.mean()) / (signal.std() + 1e-8)
//
// Toan hoc: Var(X) = E[X^2] - (E[X])^2
//   → tinh sum + sum_sq trong 1 loop, sau do compute mean/std 1 lan
//
// Tai sao quan trong: model train tren signal mean=0, std=1.
// Neu khong normalize, ESP32 gui input range [0,1] → distribution
// shift → CNN predict sai (~20% acc thay vi 87%).
static void zscore_normalize(float* buffer, int length) {
    // ── Loop 1: tinh sum va sum_sq cung luc ──
    float sum    = 0.0f;
    float sum_sq = 0.0f;
    for (int i = 0; i < length; i++) {
        float x = buffer[i];
        sum    += x;
        sum_sq += x * x;
    }

    float mean     = sum / length;
    float variance = (sum_sq / length) - (mean * mean);

    // Edge case: variance < 0 do floating-point error khi tat ca samples bang nhau
    if (variance < 0.0f) variance = 0.0f;
    float stddev = sqrtf(variance);

    // ── Loop 2: apply normalize in-place ──
    // 1e-8f de tranh chia cho 0 khi tin hieu phang (vd ADC stuck)
    float inv_std = 1.0f / (stddev + 1e-8f);
    for (int i = 0; i < length; i++) {
        buffer[i] = (buffer[i] - mean) * inv_std;
    }
}

#endif // NORMALIZE_H
