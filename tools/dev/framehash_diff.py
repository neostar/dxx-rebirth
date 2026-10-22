# Compare a run's frame hash log (GLOSSARY.md, debug harness) with its
# baseline frame by frame, and print one line for scene NAME:
#   NAME: N/N frames identical
# and exit 0, or, when a frame differs, is missing or is extra,
#   NAME: M/N frames identical; first difference: section '<header>', frame I
# and exit 1.  N counts the baseline's frames, M those the run matches at
# the same section and frame index.  Sections pair up in order and must
# have the same header.  A log that is not a frame hash log ("# " headers,
# each followed by "INDEX WxH HASH" frame lines), or that has nothing to
# compare (no section, or a compared section with no frames), exits 2.
#
# --section PREFIX compares only the first section whose header starts
# with PREFIX and that a later header closes: a demo run ended by TERM
# (docs/agents/game-data.md) compares its first whole demo, not the title
# screens around it, timed by the wall clock, nor what -autodemo played
# after it until the TERM.  A log with no such section exits 2.
#
#   python3 framehash_diff.py [--section PREFIX] NAME BASELINE RUN
import re
import sys

FRAME = re.compile(r'[0-9]+ [0-9]+x[0-9]+ [0-9a-f]{16}')


def parse(path):
	"""The log at path as a list of (header, [frame line, ...])."""
	sections = []
	with open(path, encoding='utf-8') as f:
		for n, line in enumerate(f, 1):
			line = line.rstrip('\n')
			if line.startswith('# '):
				sections.append((line, []))
			elif sections and FRAME.fullmatch(line):
				sections[-1][1].append(line)
			else:
				raise ValueError(f'{path}:{n}: not a frame hash log line')
	if not sections:
		raise ValueError(f'{path}: no frame hash log sections')
	return sections


def framed(sections, path):
	"""Sections, when each has a frame."""
	for header, frames in sections:
		if not frames:
			raise ValueError(f"{path}: section '{header}' has no frames")
	return sections


def closed(sections, prefix, path):
	"""The first section of a log whose header starts with prefix and
	that a later section closes, as a log."""
	for s in sections[:-1]:
		if s[0].startswith(prefix):
			return [s]
	raise ValueError(f"{path}: no closed '{prefix}' section")


def diff(base, run):
	"""(identical, total, first) for logs base and run: first is
	(header, frame index) of the first difference, or None."""
	identical = total = 0
	first = None
	for i in range(max(len(base), len(run))):
		bh, bf = base[i] if i < len(base) else (None, [])
		rh, rf = run[i] if i < len(run) else (None, [])
		total += len(bf)
		if bh != rh:
			if first is None:
				first = (bh or rh, 0)
			continue
		for j in range(max(len(bf), len(rf))):
			if j < len(bf) and j < len(rf) and bf[j] == rf[j]:
				identical += 1
			elif first is None:
				first = (bh, j)
	return identical, total, first


def main(argv):
	prefix = None
	if len(argv) == 6 and argv[1] == '--section':
		prefix, argv = argv[2], argv[:1] + argv[3:]
	if len(argv) != 4:
		print(f'usage: {argv[0]} [--section PREFIX] NAME BASELINE RUN', file=sys.stderr)
		return 2
	name = argv[1]
	try:
		base, run = parse(argv[2]), parse(argv[3])
		if prefix is not None:
			base, run = closed(base, prefix, argv[2]), closed(run, prefix, argv[3])
		base, run = framed(base, argv[2]), framed(run, argv[3])
	except (OSError, ValueError) as e:
		print(f'framehash_diff: {e}', file=sys.stderr)
		return 2
	identical, total, first = diff(base, run)
	line = f'{name}: {identical}/{total} frames identical'
	if first is None:
		print(line)
		return 0
	print(f"{line}; first difference: section '{first[0]}', frame {first[1]}")
	return 1


if __name__ == '__main__':
	sys.exit(main(sys.argv))
