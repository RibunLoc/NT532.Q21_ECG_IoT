#ifndef INFERENCE_H
#define INFERENCE_H

#include "ecg_weights.h"
#include <math.h>

// ── Labels ───────────────────────────────────────────────
const char* LABELS[] = {
    "Normal",
    "Supraventricular",
    "Ventricular",
    "Fusion",
    "Unknown"
};
const int NUM_CLASSES = 5;

// ── Buffers (global để tiết kiệm stack) ─────────────────
static float conv1_out[CONV1_OUT_LEN][CONV1_FILTERS];   // 94 x 8 = 752 floats = 3KB
static float conv2_out[CONV2_OUT_LEN][CONV2_FILTERS];   // 47 x 16 = 752 floats = 3KB
static float conv3_out[CONV3_OUT_LEN][CONV3_FILTERS];   // 24 x 32 = 768 floats = 3KB
static float gap_out[CONV3_FILTERS];                     // 32 floats
static float dense1_out[DENSE1_UNITS];                   // 16 floats
static float dense2_out[DENSE2_UNITS];                   // 5 floats

// ── Conv1D với padding 'same', stride=2, ReLU ───────────
// Input : float input[in_len][in_ch]
// Weight: float w[kernel][in_ch][out_ch]
// Bias  : float b[out_ch]
// Output: float output[out_len][out_ch]
static void conv1d_relu(
    const float* input, int in_len, int in_ch,
    const float* weights, const float* bias,
    int kernel, int out_ch, int stride,
    float* output, int out_len
) {
    // 'same' padding
    int pad_total = (out_len - 1) * stride + kernel - in_len;
    if (pad_total < 0) pad_total = 0;
    int pad_left = pad_total / 2;

    for (int o = 0; o < out_len; o++) {
        for (int f = 0; f < out_ch; f++) {
            float sum = bias[f];
            int start = o * stride - pad_left;

            for (int k = 0; k < kernel; k++) {
                int in_pos = start + k;
                if (in_pos < 0 || in_pos >= in_len) continue;

                for (int c = 0; c < in_ch; c++) {
                    // weights[k][c][f] flatten = weights[k*in_ch*out_ch + c*out_ch + f]
                    float w_val = weights[k * in_ch * out_ch + c * out_ch + f];
                    float x_val = input[in_pos * in_ch + c];
                    sum += x_val * w_val;
                }
            }

            // ReLU
            output[o * out_ch + f] = (sum > 0) ? sum : 0;
        }
    }
}

// ── Global Average Pooling 1D ───────────────────────────
static void global_avg_pool(
    const float* input, int in_len, int channels,
    float* output
) {
    for (int c = 0; c < channels; c++) {
        float sum = 0;
        for (int i = 0; i < in_len; i++) {
            sum += input[i * channels + c];
        }
        output[c] = sum / in_len;
    }
}

// ── Dense layer + ReLU ──────────────────────────────────
static void dense_relu(
    const float* input, int in_size,
    const float* weights, const float* bias,
    int out_size, float* output
) {
    for (int o = 0; o < out_size; o++) {
        float sum = bias[o];
        for (int i = 0; i < in_size; i++) {
            sum += input[i] * weights[i * out_size + o];
        }
        output[o] = (sum > 0) ? sum : 0;
    }
}

// ── Dense layer + Softmax ───────────────────────────────
static void dense_softmax(
    const float* input, int in_size,
    const float* weights, const float* bias,
    int out_size, float* output
) {
    // Linear
    for (int o = 0; o < out_size; o++) {
        float sum = bias[o];
        for (int i = 0; i < in_size; i++) {
            sum += input[i] * weights[i * out_size + o];
        }
        output[o] = sum;
    }

    // Softmax với numerical stability
    float max_val = output[0];
    for (int i = 1; i < out_size; i++)
        if (output[i] > max_val) max_val = output[i];

    float exp_sum = 0;
    for (int i = 0; i < out_size; i++) {
        output[i] = expf(output[i] - max_val);
        exp_sum += output[i];
    }

    for (int i = 0; i < out_size; i++)
        output[i] /= exp_sum;
}

// ── Inference Result ─────────────────────────────────────
struct InferenceResult {
    int         label_index;
    float       confidence;
    const char* label_name;
    float       all_probs[5];
};

// ── Setup (chỉ in thông tin) ─────────────────────────────
void setupInference() {
    Serial.println("✅ Pure C ECG Inference Engine");
    Serial.printf("   Input  : %d samples\n", INPUT_LEN);
    Serial.printf("   Conv1  : %d filters, kernel=%d\n", CONV1_FILTERS, CONV1_KERNEL);
    Serial.printf("   Conv2  : %d filters, kernel=%d\n", CONV2_FILTERS, CONV2_KERNEL);
    Serial.printf("   Conv3  : %d filters, kernel=%d\n", CONV3_FILTERS, CONV3_KERNEL);
    Serial.printf("   Dense1 : %d units\n", DENSE1_UNITS);
    Serial.printf("   Dense2 : %d units (classes)\n", DENSE2_UNITS);
}

// ── Inference ────────────────────────────────────────────
InferenceResult runInference(float* ecg_input) {
    InferenceResult result = {0, 0.0f, "Error", {0}};

    // Conv1: (187, 1) → (94, 8), stride=2
    conv1d_relu(
        ecg_input, INPUT_LEN, 1,
        conv1_w, conv1_b,
        CONV1_KERNEL, CONV1_FILTERS, 2,
        (float*)conv1_out, CONV1_OUT_LEN
    );

    // Conv2: (94, 8) → (47, 16), stride=2
    conv1d_relu(
        (float*)conv1_out, CONV1_OUT_LEN, CONV1_FILTERS,
        conv2_w, conv2_b,
        CONV2_KERNEL, CONV2_FILTERS, 2,
        (float*)conv2_out, CONV2_OUT_LEN
    );

    // Conv3: (47, 16) → (24, 32), stride=2
    conv1d_relu(
        (float*)conv2_out, CONV2_OUT_LEN, CONV2_FILTERS,
        conv3_w, conv3_b,
        CONV3_KERNEL, CONV3_FILTERS, 2,
        (float*)conv3_out, CONV3_OUT_LEN
    );

    // GlobalAveragePooling: (24, 32) → (32,)
    global_avg_pool(
        (float*)conv3_out, CONV3_OUT_LEN, CONV3_FILTERS,
        gap_out
    );

    // Dense1: (32,) → (16,)
    dense_relu(
        gap_out, CONV3_FILTERS,
        dense1_w, dense1_b,
        DENSE1_UNITS, dense1_out
    );

    // Dense2: (16,) → (5,) + Softmax
    dense_softmax(
        dense1_out, DENSE1_UNITS,
        dense2_w, dense2_b,
        DENSE2_UNITS, dense2_out
    );

    // Find best class
    int best_idx = 0;
    float best_conf = dense2_out[0];
    for (int i = 0; i < NUM_CLASSES; i++) {
        result.all_probs[i] = dense2_out[i];
        if (dense2_out[i] > best_conf) {
            best_conf = dense2_out[i];
            best_idx = i;
        }
    }

    result.label_index = best_idx;
    result.confidence  = best_conf;
    result.label_name  = LABELS[best_idx];
    return result;
}

// ── Debug ─────────────────────────────────────────────────
void printAllProbs(InferenceResult& r) {
    for (int i = 0; i < NUM_CLASSES; i++) {
        Serial.printf("  [%d] %-20s: %.3f %s\n",
            i, LABELS[i], r.all_probs[i],
            i == r.label_index ? "<--" : "");
    }
}

#endif // INFERENCE_H
