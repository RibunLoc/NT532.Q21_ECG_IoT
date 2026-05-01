#ifndef FAKE_ECG_H
#define FAKE_ECG_H

// ── Fake ECG generator cho DEMO_MODE ─────────────────────────
// Tao tin hieu gia mo phong nhip tim Normal (60-80 bpm)
//
// Pattern moi nhip ~ 360 samples (1 giay tai 360Hz, ~60bpm):
//   P-wave   : sample 50-80   (small bump, +0.1)
//   PQ       : 80-130          (baseline)
//   QRS      : 130-160         (R-peak +1.0 sample 145)
//   ST       : 160-200         (baseline)
//   T-wave   : 200-260         (medium bump, +0.3)
//   rest     : 260-360         (baseline)
//
// Output range: [0, 1] de khop voi analogRead() / 4095.0f

#include <math.h>

// Counter mo phong vi tri trong cycle (0..359)
static int fake_cycle_pos = 0;
static int fake_beat_type = 0;   // 0=Normal, 1=VEB (test alert)

// Generate 1 sample fake ECG
// Tra ve gia tri trong [0.0, 1.0] (giong analogRead/4095)
static float generate_fake_ecg() {
    int p = fake_cycle_pos;
    float val = 0.5f;   // baseline ~ giua range

    if (fake_beat_type == 0) {
        // ── Normal beat ──
        if (p >= 50 && p < 80) {
            // P-wave: hump nho
            float t = (p - 50) / 30.0f;   // 0..1
            val += 0.05f * sinf(t * 3.14159f);
        }
        else if (p >= 130 && p < 160) {
            // QRS complex: spike cao
            float t = (p - 130) / 30.0f;
            if (t < 0.3f) {
                val -= 0.1f * t / 0.3f;       // Q dip
            } else if (t < 0.6f) {
                val += 0.45f * sinf((t-0.3f) / 0.3f * 3.14159f);  // R-peak
            } else {
                val -= 0.05f * (1.0f - (t-0.6f)/0.4f);   // S
            }
        }
        else if (p >= 200 && p < 260) {
            // T-wave: hump trung binh
            float t = (p - 200) / 60.0f;
            val += 0.15f * sinf(t * 3.14159f);
        }
    } else {
        // ── VEB beat (rong, khong P-wave) ──
        if (p >= 110 && p < 180) {
            // QRS rong gap doi Normal
            float t = (p - 110) / 70.0f;
            val += 0.5f * sinf(t * 3.14159f);
        }
        else if (p >= 200 && p < 260) {
            // T-wave nguoc dau (negative)
            float t = (p - 200) / 60.0f;
            val -= 0.2f * sinf(t * 3.14159f);
        }
    }

    // Them noise nho cho realistic
    val += ((float)random(-50, 50)) / 10000.0f;

    // Clip ve [0, 1]
    if (val < 0.0f) val = 0.0f;
    if (val > 1.0f) val = 1.0f;

    // Tang counter, doi beat type sau moi 5 nhip
    fake_cycle_pos++;
    if (fake_cycle_pos >= 360) {
        fake_cycle_pos = 0;
        // Cu 5 Normal -> 1 VEB de test alert
        static int beat_count = 0;
        beat_count++;
        if (beat_count % 6 == 5) {
            fake_beat_type = 1;   // VEB
        } else {
            fake_beat_type = 0;   // Normal
        }
    }

    return val;
}

#endif // FAKE_ECG_H
