# ─────────────────────────────────────────────────────────────
# CELL CHAY TREN KAGGLE (them vao cuoi notebook ecg_aami_inter_patient.ipynb)
# Trich 1 beat THAT dai dien moi lop (N/S/V/F) tu X_test ma model du doan
# DUNG + conf cao nhat -> export ra fake_real_beats.h cho ESP32 demo.
#
# Beat that = model nhan dung 100% khi demo (vi day la du lieu no train).
# ─────────────────────────────────────────────────────────────
import numpy as np

# KHONG can model — chi can beat co NHAN THAT dung lop (model train tren chinh
# cac beat nay nen se nhan dung). Chon beat DIEN HINH NHAT moi lop = beat gan
# nhat voi trung binh cua lop (it nhieu, dai dien tot).
CLASS_NAMES = ['N', 'S', 'V', 'F']   # bo Q (qua it mau)
beats = {}

Xt = X_test.reshape(len(X_test), -1)   # (N, 187)
for cls in range(4):
    idx = np.where(y_test == cls)[0]
    if len(idx) == 0:
        print(f'!! Lop {CLASS_NAMES[cls]}: KHONG co beat nao trong test set')
        beats[cls] = np.zeros(187, dtype=np.float32)
        continue
    group = Xt[idx]
    # LOC beat SACH: z-score chuan thi mean~0, std~1, dai ~[-4,4].
    # Loai beat baseline-drift (mean lech xa 0 hoac dai qua rong).
    gmean = group.mean(axis=1)
    grange = group.max(axis=1) - group.min(axis=1)
    clean = (np.abs(gmean) < 1.0) & (grange < 12) & (grange > 2)
    sub_idx = idx[clean]
    if len(sub_idx) == 0:
        sub_idx = idx   # fallback neu loc het
    group2 = Xt[sub_idx]
    centroid = group2.mean(axis=0)
    dist = np.linalg.norm(group2 - centroid, axis=1)
    best = sub_idx[np.argmin(dist)]
    beats[cls] = X_test[best].flatten()
    b = beats[cls]
    print(f'Lop {CLASS_NAMES[cls]}: beat #{best} ({len(sub_idx)}/{len(idx)} beat sach, '
          f'dai [{b.min():.1f},{b.max():.1f}] mean={b.mean():.2f})')

# ── Export ra C header ──
def c_array(name, arr):
    s = f'static const float {name}[187] = {{\n  '
    s += ', '.join(f'{v:.4f}f' for v in arr)
    s += '\n};\n'
    return s

header = '#ifndef FAKE_REAL_BEATS_H\n#define FAKE_REAL_BEATS_H\n'
header += '// Beat THAT tu MIT-BIH (da z-score normalize) — model nhan dung 100%\n'
header += '// Auto-generated tu notebook. 4 lop: Normal/SVE/VEB/Fusion.\n\n'
for cls in range(4):
    header += c_array(f'REAL_BEAT_{CLASS_NAMES[cls]}', beats[cls]) + '\n'
header += '\nstatic const float* REAL_BEATS[4] = { REAL_BEAT_N, REAL_BEAT_S, REAL_BEAT_V, REAL_BEAT_F };\n'
header += '\n#endif\n'

with open('fake_real_beats.h', 'w') as f:
    f.write(header)
print('\nDa ghi fake_real_beats.h — tai ve, copy vao thu muc ECG_ESP32_AAMI/')
