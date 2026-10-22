#!/usr/bin/env python3
# Check that a process's memory stays flat: the endurance run of
# tools/dev/nightly.sh (ADR 5, nightly tier).
#
#   rss_trend.py [--warmup S] [--window S] [--max-slope MIB_PER_HOUR]
#                [--min-samples N] [--min-windows N] SAMPLES
#
# SAMPLES holds one "SECONDS KIB" line per sample (seconds since the start,
# the process's memory in KiB: its physical footprint on macOS, which
# counts compressed and swapped pages that RSS leaves out; RSS elsewhere).
# Samples in the warm-up (default 300 s: levels, sounds and textures
# loading over the first demos) are left out.  The rest are cut into
# windows (default 60 s) from the end of the warm-up, and the Theil-Sen
# slope (the median of the slopes between every two whole windows' lowest
# samples) gives the growth rate, which fails above the limit (default
# 8 MiB per hour).  Demo playback loops: memory rises through each demo and
# falls at the next by about 10 MiB, so the minima, not every sample, show
# a leak, and where a run ends in the loop does not decide.  The median,
# unlike a least-squares line, is not turned by one step: a 22 MiB drop a
# third of the way through an hour hides no 16 MiB/h leak.  A step near
# the middle of the run still can, since half the pairs then straddle it.
# Fewer than 10 samples (--min-samples) or 30 whole windows
# (--min-windows) after the warm-up fail as "too short to judge", with no
# slope: a 600 s run's 5 windows read the same flat game as -37 or +27
# MiB/h.  30 windows are half an hour, where one demo loop's 20 MiB swing
# moves the slope by a few MiB/h, not tens; Theil-Sen's 29 % breakdown
# point then lets 8 windows (a demo loading, a step) be wild without
# turning it; the default hour of nightly.sh gives 55, clearing it twice.
# Each verdict prints its numbers so the scheduled runs can calibrate the
# limit, which is provisional until they do: the windows, the slope with
# its 95 % confidence range (Sen's, from Kendall's tau), and the lowest
# and highest window minima, whose spread is the noise the slope fights.
# Exit status 0 flat, 1 growing or too short to judge, 2 unreadable input
# or a usage error.
import argparse
import math
import sys


def read_samples(path):
	samples = []
	with open(path) as f:
		for n, line in enumerate(f, 1):
			if not line.strip():
				continue
			parts = line.split()
			try:
				if len(parts) != 2:
					raise ValueError
				samples.append((float(parts[0]), float(parts[1])))
			except ValueError:
				print(f'rss_trend: {path}:{n}: not "SECONDS KIB": {line.rstrip()}',
					file=sys.stderr)
				sys.exit(2)
	return samples


def slope(samples):
	"""The Theil-Sen slope, the median of the slopes between every two
	samples, and its 95 % confidence range (Sen 1968): the slopes ranked
	1.96 standard deviations of Kendall's S either side of the median."""
	slopes = sorted((r2 - r1) / (t2 - t1) for i, (t1, r1) in enumerate(samples)
		for t2, r2 in samples[i + 1:])
	n, pairs = len(samples), len(slopes)
	mid = pairs // 2
	median = slopes[mid] if pairs % 2 else (slopes[mid - 1] + slopes[mid]) / 2
	c = 1.96 * math.sqrt(n * (n - 1) * (2 * n + 5) / 18)
	low = max(0, math.floor((pairs - c) / 2) - 1)
	high = min(pairs - 1, math.ceil((pairs + c) / 2))
	return median, slopes[low], slopes[high]


def main():
	p = argparse.ArgumentParser(description='Fail when memory grows over time.')
	p.add_argument('--warmup', type=float, default=300)
	p.add_argument('--window', type=float, default=60)
	p.add_argument('--max-slope', type=float, default=8, help='MiB per hour')
	p.add_argument('--min-samples', type=int, default=10)
	p.add_argument('--min-windows', type=int, default=30)
	p.add_argument('samples')
	args = p.parse_args()
	if args.min_windows < 3:
		p.error('--min-windows must be at least 3: two points always fit a line')
	if args.min_samples < 1:
		p.error('--min-samples must be at least 1')
	if args.window <= 0:
		p.error('--window must be above 0')
	try:
		samples = read_samples(args.samples)
	except OSError as e:
		print(f'rss_trend: {e}', file=sys.stderr)
		return 2
	kept = [s for s in samples if s[0] >= args.warmup]
	if len(kept) < args.min_samples:
		print(f'rss_trend: too short to judge: {len(kept)} samples after the '
			f'{args.warmup:g} s warm-up, need {args.min_samples}')
		return 1
	minima = {}
	full = int((kept[-1][0] - args.warmup) // args.window)
	for t, r in kept:
		w = int((t - args.warmup) // args.window)
		if w >= full and full > 0:
			continue  # the last window is cut short by the end of the run
		if w not in minima or r < minima[w][1]:
			minima[w] = (t, r)
	lows = [r / 1024 for _, r in minima.values()]
	noise = f'window minima {min(lows):.1f} to {max(lows):.1f} MiB (spread {max(lows) - min(lows):.1f} MiB)'
	span = f'{len(kept)} samples over {kept[-1][0] - kept[0][0]:g} s after the {args.warmup:g} s warm-up'
	if len(minima) < args.min_windows:
		print(f'rss_trend: too short to judge: {len(minima)} windows of {args.window:g} s after the '
			f'{args.warmup:g} s warm-up, need {args.min_windows}; {noise}; {span}')
		return 1
	mib_h, low, high = (v * 3600 / 1024 for v in slope(list(minima.values())))
	verdict = 'growing' if mib_h > args.max_slope else 'flat'
	print(f'rss_trend: {verdict}: slope {mib_h:.1f} MiB/h (95% range {low:.1f} to {high:.1f}, '
		f'Theil-Sen) over {len(minima)} windows of {args.window:g} s, limit {args.max_slope:g} MiB/h '
		f'(provisional); {noise}; {span}')
	return 1 if verdict == 'growing' else 0


if __name__ == '__main__':
	sys.exit(main())
