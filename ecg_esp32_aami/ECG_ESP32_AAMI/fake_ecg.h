#ifndef FAKE_ECG_H
#define FAKE_ECG_H

// ── Fake ECG generator cho DEMO_MODE ─────────────────────────
// Phat lai BEAT THAT tu MIT-BIH (file fake_real_beats.h, export tu notebook).
// Beat that = model nhan DUNG 100% khi demo (vi day la du lieu no train).
//
// Type: 0=Normal  1=SVE(S)  2=VEB(V)  3=Fusion(F)  (khop REAL_BEATS[])
//
// Beat that da z-score normalize (~ -3..+3). Pipeline ESP32 lay ADC roi z-score
// LAI — vi z-score bat bien voi scale/offset tuyen tinh, ta map beat ve [0,1]
// roi pipeline chuan hoa lai se ra dung dang -> model nhan dung.
//
// Beat dai 187 mau. Phat tuan tu, lap lai (mo phong nhip lien tiep cung loai).

#include "fake_real_beats.h"

int fake_beat_type = 0;          // set tu ngoai (setFakeBeatType) — extern de .ino bypass R-peak
static int fake_idx = 0;         // vi tri trong beat 187 mau

// Min/max cua tung beat (moi beat dai khac nhau) -> map ve [0,1] giu dung dang.
// Tinh 1 lan khi doi type.
static float _bmin = 0, _bmax = 1;
static void _computeRange() {
    const float* b = REAL_BEATS[fake_beat_type];
    _bmin = b[0]; _bmax = b[0];
    for (int i = 1; i < 187; i++) {
        if (b[i] < _bmin) _bmin = b[i];
        if (b[i] > _bmax) _bmax = b[i];
    }
    if (_bmax - _bmin < 1e-6f) _bmax = _bmin + 1e-6f;
}

// Chu ky 1 nhip = 187 mau beat + nghi baseline. Tong ~480 mau @360Hz = ~1.33s
// (~75 bpm) -> song giong tim that, khong don dap.
#define BEAT_LEN     187
#define REST_LEN     293            // nghi sau beat (baseline)
#define CYCLE_LEN    (BEAT_LEN + REST_LEN)   // 480

static bool _new_beat = false;      // co bao dau 1 nhip moi (de bip 1 lan)

void setFakeBeatType(int t) {
    if (t < 0) t = 0; if (t > 3) t = 3;
    fake_beat_type = t;
    fake_idx = 0;
    _computeRange();
}

// .ino doc co nay de bip 1 lan moi nhip
bool fakeNewBeat() { bool b = _new_beat; _new_beat = false; return b; }

static float generate_fake_ecg() {
    float z;
    if (fake_idx < BEAT_LEN) {
        z = REAL_BEATS[fake_beat_type][fake_idx];   // dang trong beat
    } else {
        z = REAL_BEATS[fake_beat_type][0];          // nghi: giu baseline (mau dau)
    }

    fake_idx++;
    if (fake_idx >= CYCLE_LEN) {
        fake_idx = 0;
        _new_beat = true;     // bat dau nhip moi -> .ino se bip
    }

    float val = 0.1f + 0.8f * (z - _bmin) / (_bmax - _bmin);
    val += ((float)random(-30, 30)) / 10000.0f;
    if (val < 0.0f) val = 0.0f;
    if (val > 1.0f) val = 1.0f;
    return val;
}

#endif // FAKE_ECG_H
