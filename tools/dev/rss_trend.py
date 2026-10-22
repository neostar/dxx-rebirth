#!/usr/bin/env python3
# Check that a process's memory stays flat: the endurance run of
# tools/dev/nightly.sh (ADR 5, nightly tier).
#
#   rss_trend.py [--warmup S] [--window S] [--max-slope MIB_PER_HOUR] [--min-samples N] SAMPLES
#
# SAMPLES holds one "SECONDS RSS_KIB" line per sample (seconds since the
# start, resident set size in KiB, as `ps -o rss=` prints it).  Samples in
# the warm-up (default 120 s: levels, sounds and textures loading) are left
# out.  The rest are cut into windows (default 60 s) from the end of the
# warm-up, and a least-squares line through each whole window's lowest
# sample gives the growth rate, which fails above the limit (default 8 MiB
# per hour).  Demo playback loops: RSS rises through each demo and falls at the
# next by about 10 MiB, so the minima, not every sample, show a leak, and
# where a run ends in the loop does not decide.  Fewer than 10 samples
# (--min-samples) or 3 windows after the warm-up fail: too few to call it
# flat.
# Exit status 0 flat, 1 growing or too few samples, 2 unreadable input.
import argparse
import sys

MIN_WINDOWS = 3


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
				print(f'rss_trend: {path}:{n}: not "SECONDS RSS_KIB": {line.rstrip()}',
					file=sys.stderr)
				sys.exit(2)
	return samples


def slope(samples):
	n = len(samples)
	mt = sum(t for t, _ in samples) / n
	mr = sum(r for _, r in samples) / n
	var = sum((t - mt) ** 2 for t, _ in samples)
	return sum((t - mt) * (r - mr) for t, r in samples) / var


def main():
	p = argparse.ArgumentParser(description='Fail when RSS grows over time.')
	p.add_argument('--warmup', type=float, default=120)
	p.add_argument('--window', type=float, default=60)
	p.add_argument('--max-slope', type=float, default=8, help='MiB per hour')
	p.add_argument('--min-samples', type=int, default=10)
	p.add_argument('samples')
	args = p.parse_args()
	try:
		samples = read_samples(args.samples)
	except OSError as e:
		print(f'rss_trend: {e}', file=sys.stderr)
		return 2
	kept = [s for s in samples if s[0] >= args.warmup]
	if len(kept) < args.min_samples:
		print(f'rss_trend: too few samples after the {args.warmup:g} s warm-up: '
			f'{len(kept)}, need {args.min_samples}')
		return 1
	minima = {}
	full = int((kept[-1][0] - args.warmup) // args.window)
	for t, r in kept:
		w = int((t - args.warmup) // args.window)
		if w >= full and full > 0:
			continue  # the last window is cut short by the end of the run
		if w not in minima or r < minima[w][1]:
			minima[w] = (t, r)
	if len(minima) < MIN_WINDOWS:
		print(f'rss_trend: too few {args.window:g} s windows after the warm-up: '
			f'{len(minima)}, need {MIN_WINDOWS}')
		return 1
	mib_h = slope(list(minima.values())) * 3600 / 1024
	first, last = kept[0][1] / 1024, kept[-1][1] / 1024
	verdict = 'growing' if mib_h > args.max_slope else 'flat'
	print(f'rss_trend: {len(kept)} samples over {kept[-1][0] - kept[0][0]:g} s after the '
		f'{args.warmup:g} s warm-up, {first:.1f} MiB to {last:.1f} MiB, slope '
		f'{mib_h:.1f} MiB/h over {len(minima)} window minima, limit {args.max_slope:g} MiB/h: {verdict}')
	return 1 if verdict == 'growing' else 0


if __name__ == '__main__':
	sys.exit(main())
