"""
Tao 1 beat synthetic mo phong VEB pattern de test Lambda.
Pattern: wide QRS (~80ms), khong co P-wave, T-wave nguoc chieu.
Khong dam bao 100% cloud predict V — chi de smoke test pipeline.
"""
import json
import numpy as np

WINDOW = 187
FS = 360
t = np.arange(WINDOW) / FS

# Baseline noise
signal = np.random.normal(0, 0.05, WINDOW)

# QRS rong (wide) tai trung tam — dac trung VEB
center = WINDOW // 2
qrs_width = 30   # ~83ms (Normal QRS chi ~30ms, VEB rong gap 2-3 lan)
for i in range(center - qrs_width, center + qrs_width):
    if 0 <= i < WINDOW:
        # R-peak amplitude lon, hinh tam giac
        dist = abs(i - center) / qrs_width
        signal[i] += 3.0 * (1 - dist) ** 2

# T-wave nguoc chieu sau QRS (dac trung VEB)
t_start = center + qrs_width + 5
t_end   = min(t_start + 40, WINDOW)
for i in range(t_start, t_end):
    progress = (i - t_start) / (t_end - t_start)
    signal[i] -= 1.5 * np.sin(np.pi * progress)

# Z-score normalize (giong notebook)
signal = (signal - signal.mean()) / (signal.std() + 1e-8)

payload = {
    'device_id': 'ecg-device-001',
    'timestamp': 1745923456,
    'edge_label': 'V',
    'edge_confidence': 0.85,
    'samples': signal.tolist(),
}

with open('test_event_V.json', 'w') as f:
    json.dump(payload, f)

print(f'Saved test_event_V.json ({len(signal)} samples)')
print(f'Signal stats: min={signal.min():.2f}, max={signal.max():.2f}, mean={signal.mean():.2e}')
