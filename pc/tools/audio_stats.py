#!/usr/bin/env python3
"""Look at a sound dump from --audio-dump (32 kHz stereo 16-bit WAV).

    audio_stats.py DUMP.wav [--seg NAME:F1-F2 ...] [--gaps MS] [--every SEC]
    audio_stats.py DUMP.wav --compare OTHER.wav

Segments are given in game frames (60 per second, as in --input-script): with
--deterministic the game makes exactly 1/60 s of sound per frame, so frame F
starts at about sample F * 32000 / 60. Without segments the whole file is one.

For each segment it prints the length, loudness (RMS and peak in dBFS, per
channel), how much of it is silent (10 ms blocks under -60 dBFS), the longest
silence, clicks (sample-to-sample jumps over half the range), clipped samples,
the DC offset, how alike the two channels are, and the strongest frequency.
--gaps lists silences at least MS long that have sound on both sides
(dropouts); --every prints the loudness every SEC seconds.
--compare says whether two dumps are the same sample for sample and, if not,
where they first differ.
"""
import argparse
import sys
import wave

import numpy as np

FPS = 60
SILENT_DB = -60.0
BLOCK_MS = 10


def load(path):
    with wave.open(path, "rb") as w:
        if w.getsampwidth() != 2:
            sys.exit(f"{path}: expected 16-bit samples")
        ch = w.getnchannels()
        rate = w.getframerate()
        data = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2")
    data = data.reshape(-1, ch).astype(np.float64) / 32768.0
    return data, rate


def db(x):
    return 20.0 * np.log10(max(x, 1e-9))


def blocks_db(mono, rate):
    n = max(1, rate * BLOCK_MS // 1000)
    usable = len(mono) // n * n
    if usable == 0:
        return np.zeros(0)
    b = mono[:usable].reshape(-1, n)
    rms = np.sqrt(np.mean(b * b, axis=1))
    return 20.0 * np.log10(np.maximum(rms, 1e-9))


def runs(mask):
    """(start, length) of each run of True"""
    out = []
    start = None
    for i, m in enumerate(mask):
        if m and start is None:
            start = i
        elif not m and start is not None:
            out.append((start, i - start))
            start = None
    if start is not None:
        out.append((start, len(mask) - start))
    return out


def stats(name, x, rate, first_frame, gaps_ms):
    secs = len(x) / rate
    print(f"[{name}] {secs:.2f} s from frame {first_frame}")
    if len(x) == 0:
        return
    for c, label in enumerate("LR"[: x.shape[1]]):
        s = x[:, c]
        rms = np.sqrt(np.mean(s * s))
        print(f"  {label}: rms {db(rms):6.1f} dBFS  peak {db(np.max(np.abs(s))):6.1f} dBFS"
              f"  dc {np.mean(s) * 32768:+.1f}")
    mono = x.mean(axis=1)
    bdb = blocks_db(mono, rate)
    silent = bdb < SILENT_DB
    sr = runs(silent)
    longest = max((l for _, l in sr), default=0) * BLOCK_MS
    print(f"  silent {100.0 * silent.mean() if len(silent) else 0:5.1f}%  longest silence {longest} ms")
    jumps = np.abs(np.diff(x, axis=0)) > 0.5
    clipped = np.abs(x) >= 32767 / 32768.0
    print(f"  clicks {int(jumps.any(axis=1).sum())}  clipped {int(clipped.sum())}")
    if x.shape[1] == 2:
        l, r = x[:, 0], x[:, 1]
        denom = np.sqrt(np.sum(l * l) * np.sum(r * r))
        corr = float(np.sum(l * r) / denom) if denom > 0 else 0.0
        print(f"  L/R correlation {corr:+.3f}")
    if np.max(np.abs(mono)) > 0:
        n = min(len(mono), 1 << 14)
        win = np.hanning(n)
        power = np.zeros(n // 2 + 1)
        for s0 in range(0, len(mono) - n + 1, n):
            power += np.abs(np.fft.rfft(mono[s0:s0 + n] * win)) ** 2
        power[0] = 0
        freqs = np.fft.rfftfreq(n, 1.0 / rate)
        peak = freqs[int(np.argmax(power))]
        centroid = float(np.sum(freqs * power) / np.sum(power))
        print(f"  strongest {peak:.0f} Hz  centroid {centroid:.0f} Hz")
    if gaps_ms:
        need = max(1, gaps_ms // BLOCK_MS)
        for start, length in sr:
            if length >= need and start > 0 and start + length < len(silent):
                at = first_frame + start * BLOCK_MS * FPS / 1000.0
                print(f"  gap {length * BLOCK_MS} ms at frame {at:.0f}")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("dump")
    ap.add_argument("--seg", action="append", default=[], metavar="NAME:F1-F2")
    ap.add_argument("--gaps", type=int, default=0, metavar="MS")
    ap.add_argument("--every", type=float, default=0, metavar="SEC")
    ap.add_argument("--compare", metavar="OTHER")
    args = ap.parse_args()

    x, rate = load(args.dump)
    if args.compare:
        y, rate2 = load(args.compare)
        n = min(len(x), len(y))
        diff = np.nonzero(np.any(x[:n] != y[:n], axis=1))[0]
        if rate == rate2 and len(x) == len(y) and len(diff) == 0:
            print(f"same: {len(x)} samples")
            return 0
        print(f"lengths {len(x)} / {len(y)} samples")
        if len(diff):
            f = diff[0]
            print(f"first difference at sample {f} (frame {f * FPS / rate:.1f}), "
                  f"{len(diff)} samples differ")
        return 1

    segs = []
    for spec in args.seg:
        name, _, rng = spec.partition(":")
        a, _, b = rng.partition("-")
        segs.append((name, int(a), int(b) if b else None))
    if not segs:
        segs = [("all", 0, None)]
    for name, a, b in segs:
        s0 = a * rate // FPS
        s1 = len(x) if b is None else min(len(x), b * rate // FPS)
        stats(name, x[s0:s1], rate, a, args.gaps)
    if args.every:
        step = int(args.every * rate)
        print("time(s) frame  rms(dBFS)")
        for s0 in range(0, len(x), step):
            seg = x[s0:s0 + step]
            rms = np.sqrt(np.mean(seg * seg))
            print(f"{s0 / rate:7.1f} {s0 * FPS // rate:6d} {db(rms):7.1f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
