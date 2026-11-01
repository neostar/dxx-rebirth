#!/usr/bin/env python3
"""Measure clipping in a raw audio capture of the game (clip-check.sh, #104).

    clip_check.py demo NAME CAPTURE MIN_BYTES MAX_PEAK
    clip_check.py stimulus NAME CAPTURE MIN_BYTES MAX_PEAK

CAPTURE is signed 16-bit little-endian stereo: the disk audio driver's
output (the SDL2 build's runs and the stimulus) or an SDL3 build's
rendered mix (DXX_DBG_AUDIO_RENDER, the demo runs).  A clipped sample is one at
the format's limit, -32768 or 32767: the mix reached full scale there.

demo prints one line, "NAME: samples=N clipped=C peak=P margin=M dB", P
the largest magnitude as a share of full scale and M its distance below
full scale, and exits 0 only when no sample clipped and P is at most
MAX_PEAK (a share of full scale, the margin the caller asks for).

stimulus reads the capture of the crafted stimulus (sound_stimulus.h): two
bursts of the same sound separated by silence, the first at F1_0 and the
second at F3_0.  It prints the demo line and "NAME: burst peaks a / b",
and exits 0 only when nothing clipped, exactly two bursts were found and
the second (above full scale) peaks no higher than the first: a volume
above F1_0 plays at full scale.

Exit 1 on a failed check, 2 on a usage error or a capture that is missing,
shorter than MIN_BYTES, of an odd length or silent.  Numbers only: the
captures derive from the licensed game data and stay out of git.
"""

import array
import math
import sys

FULL_SCALE = 32768
# A gap between bursts: this many stereo frames at most this level.
SILENCE_LEVEL = 2
SILENCE_FRAMES = 10000


def fail_usage(message):
    print(f"clip_check.py: {message}", file=sys.stderr)
    sys.exit(2)


def read_capture(path, min_bytes):
    try:
        with open(path, "rb") as f:
            data = f.read()
    except OSError as e:
        fail_usage(f"cannot read {path}: {e.strerror}")
    if len(data) < min_bytes:
        fail_usage(f"{path}: {len(data)} bytes, fewer than {min_bytes}")
    if len(data) % 4:
        fail_usage(f"{path}: {len(data)} bytes, not whole 16-bit stereo frames")
    samples = array.array("h")
    samples.frombytes(data)
    if sys.byteorder != "little":
        samples.byteswap()
    return samples


def summary(name, samples):
    clipped = sum(1 for s in samples if s in (-32768, 32767))
    peak = max(abs(s) for s in samples) / FULL_SCALE
    if peak == 0:
        fail_usage(f"{name}: the capture is silent")
    margin = -20 * math.log10(peak)
    print(f"{name}: samples={len(samples)} clipped={clipped} peak={peak:.4f} margin={margin:.2f} dB")
    return clipped, peak


def burst_peaks(samples):
    """The peak magnitude of each run of sound between silent gaps."""
    peaks = []
    quiet = SILENCE_FRAMES
    for i in range(0, len(samples) - 1, 2):
        level = max(abs(samples[i]), abs(samples[i + 1]))
        if level > SILENCE_LEVEL:
            if quiet >= SILENCE_FRAMES:
                peaks.append(0)
            peaks[-1] = max(peaks[-1], level)
            quiet = 0
        else:
            quiet += 1
    return peaks


def main(argv):
    if len(argv) != 6 or argv[1] not in ("demo", "stimulus"):
        fail_usage("usage: clip_check.py demo|stimulus NAME CAPTURE MIN_BYTES MAX_PEAK")
    mode, name, path, min_bytes, max_peak = argv[1:6]
    try:
        min_bytes = int(min_bytes)
    except ValueError:
        fail_usage(f"MIN_BYTES is not a whole number: {min_bytes!r}")
    try:
        max_peak = float(max_peak)
    except ValueError:
        fail_usage(f"MAX_PEAK is not a number: {max_peak!r}")
    if not 0 < max_peak <= 1:
        fail_usage(f"MAX_PEAK is not a share of full scale: {max_peak}")
    samples = read_capture(path, min_bytes)
    clipped, peak = summary(name, samples)
    ok = clipped == 0
    if peak > max_peak:
        print(f"{name}: peak {peak:.4f} above {max_peak:.4f}")
        ok = False
    if mode == "stimulus":
        peaks = burst_peaks(samples)
        print(f"{name}: burst peaks {' / '.join(f'{p / FULL_SCALE:.4f}' for p in peaks)}")
        if len(peaks) != 2:
            print(f"{name}: expected 2 bursts, found {len(peaks)}")
            ok = False
        elif peaks[1] > peaks[0]:
            print(f"{name}: the F3_0 burst peaks above the F1_0 one")
            ok = False
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
