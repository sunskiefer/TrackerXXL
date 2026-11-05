#!/usr/bin/env python3
"""Writes a few small synthesized demo samples (mono, 16-bit, 44.1 kHz) so the sample screens have something to load.
They are generated here from plain maths: no third-party audio, no licence strings."""
import sys, os, wave, numpy as np
out = sys.argv[1]
os.makedirs(out, exist_ok=True)
sr = 44100
def save(name, x):
    x = np.clip(x, -1, 1)
    w = wave.open(os.path.join(out, name), 'wb'); w.setnchannels(1); w.setsampwidth(2); w.setframerate(sr)
    w.writeframes((x * 32000).astype('<i2').tobytes()); w.close()
rng = np.random.default_rng(1)
t = np.arange(int(sr * 0.45)) / sr
f = 45 + 110 * np.exp(-t * 28)
kick = np.sin(2 * np.pi * np.cumsum(f) / sr) * np.exp(-t * 7)
kick[:60] *= np.linspace(0, 1, 60)
save('demo_kick.wav', kick * 0.95)
t = np.arange(int(sr * 0.28)) / sr
sn = (rng.standard_normal(len(t)) * np.exp(-t * 18) * 0.7 + np.sin(2 * np.pi * 190 * t) * np.exp(-t * 26) * 0.6)
save('demo_snare.wav', sn * 0.9)
t = np.arange(int(sr * 0.09)) / sr
n = rng.standard_normal(len(t)); hat = np.diff(n, prepend=0) * np.exp(-t * 55)
save('demo_hat.wav', hat / (np.abs(hat).max() + 1e-9) * 0.8)
t = np.arange(int(sr * 0.7)) / sr
saw = 2 * ((55 * t) % 1.0) - 1
k = np.exp(-t * 4.5)
b = np.zeros_like(saw); a = 0.08
for i in range(1, len(saw)): b[i] = b[i - 1] + a * (saw[i] - b[i - 1])
save('demo_bass_A1.wav', b * 2.2 * k)
print('demo samples written to', out)
