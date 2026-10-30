# Judge SDL_GPU's frames of one trace against GL's by rubric R4
# (docs/agents/rubric.md), printing a verdict per frame and one per trace.
#
#   python3 compare_frames.py BACKEND NAME GL_DIR GPU_DIR
#
# BACKEND is metal, vulkan or d3d12.  Each directory holds what rtrace and
# rtrace-gpu write: FRAME.rgb, the frame's RGB bytes top row first, and
# frames.log, the frame hash log (their LOG argument), whose lines
# "INDEX WxH HASH" give each frame's size.  R4:
#   metal:           every frame bit-exact;
#   vulkan, d3d12:  the 95th percentile (nearest rank) of the frames' FLIP
#                    means <= 0.01 and the worst frame's <= 0.03; the
#                    maximum error is not judged.
# A frame that is missing, empty, truncated, of another size than GL's, or
# an extra one, fails the trace, as do a missing or malformed log and a
# trace without frames.  Exit status 0 when the trace passes, 1 when it
# fails, 2 on a usage error.  FLIP is flip-evaluator (LDR, default
# settings) on the frames as 0..1 RGB.
import math
import re
import sys
from pathlib import Path

P95_BOUND = 0.01
WORST_BOUND = 0.03
BACKENDS = {'metal': 'Metal', 'vulkan': 'Vulkan', 'd3d12': 'D3D12'}
LINE = re.compile(r'([0-9]+) ([0-9]+)x([0-9]+) [0-9a-f]{16}')


def read_log(directory, side):
	"""The frame sizes [(w, h), ...] of directory's log, or an error string."""
	path = directory / 'frames.log'
	try:
		text = path.read_text(encoding='utf-8')
	except OSError as e:
		return f'no frames.log of {side} in {directory}: {e.strerror}'
	sizes = []
	for n, line in enumerate(text.split('\n')):
		if line == '' and n == text.count('\n'):
			break
		m = LINE.fullmatch(line)
		if not m or int(m.group(1)) != n or int(m.group(2)) == 0 or int(m.group(3)) == 0:
			return f'{path}: line {n + 1} is not frame hash line {n} ("INDEX WxH HASH")'
		sizes.append((int(m.group(2)), int(m.group(3))))
	if not sizes:
		return f'{path}: no frame hash lines'
	return sizes


def read_frame(directory, index, size):
	"""The bytes of frame index, or an error string saying what is wrong."""
	path = directory / f'{index}.rgb'
	try:
		data = path.read_bytes()
	except OSError as e:
		return f'no frame {index} ({e.strerror})'
	if not data:
		return f'frame {index} is empty'
	want = size[0] * size[1] * 3
	if len(data) != want:
		return f'frame {index} has {len(data)} bytes, not {want} ({size[0]}x{size[1]})'
	return data


def flip_mean(gl, gpu, size):
	"""FLIP's mean error of gpu against gl (equal-length bytes), or None."""
	try:
		import flip_evaluator as flip
		import numpy as np
	except ImportError:
		return None
	shape = (size[1], size[0], 3)
	a = np.frombuffer(gl, dtype=np.uint8).reshape(shape).astype(np.float32) / 255
	b = np.frombuffer(gpu, dtype=np.uint8).reshape(shape).astype(np.float32) / 255
	_, mean, _ = flip.evaluate(a, b, 'LDR', applyMagma=False, computeMeanError=True)
	return float(mean)


def main(argv):
	if len(argv) != 5 or argv[1] not in BACKENDS:
		print(__doc__.split('\n\n')[0].replace('#', '').strip(), file=sys.stderr)
		print('usage: compare_frames.py metal|vulkan|d3d12 NAME GL_DIR GPU_DIR', file=sys.stderr)
		return 2
	backend, name = argv[1], argv[2]
	gl_dir, gpu_dir = Path(argv[3]), Path(argv[4])
	exact = backend == 'metal'
	bar = 'bit-exact' if exact else f'p95 FLIP mean <= {P95_BOUND}, worst <= {WORST_BOUND}'
	print(f'compare-backends: {name}, {BACKENDS[backend]} against GL (R4: {bar})')
	gl_sizes = read_log(gl_dir, 'GL')
	gpu_sizes = read_log(gpu_dir, 'SDL_GPU')
	problems = [s for s in (gl_sizes, gpu_sizes) if isinstance(s, str)]
	if problems:
		for p in problems:
			print(f'  {p}')
		print(f'compare-backends: {name}: FAIL (no frame log to judge)')
		return 1
	failed = []
	means = []
	for i, size in enumerate(gl_sizes):
		label = f'  frame {i} ({size[0]}x{size[1]}): '
		if i >= len(gpu_sizes):
			print(label + 'missing from SDL_GPU  FAIL')
			failed.append(i)
			continue
		if gpu_sizes[i] != size:
			print(label + f'SDL_GPU drew {gpu_sizes[i][0]}x{gpu_sizes[i][1]}, size mismatch  FAIL')
			failed.append(i)
			continue
		gl = read_frame(gl_dir, i, size)
		gpu = read_frame(gpu_dir, i, size)
		if isinstance(gl, str) or isinstance(gpu, str):
			for side, f in (('GL', gl), ('SDL_GPU', gpu)):
				if isinstance(f, str):
					print(label + f'{side}: {f}  FAIL')
			failed.append(i)
			continue
		if gl == gpu:
			print(label + 'identical' if exact else label + 'FLIP mean 0.00000  ok (identical)')
			means.append(0.0)
			continue
		mean = flip_mean(gl, gpu, size)
		if exact:
			px = sum(1 for k in range(0, len(gl), 3) if gl[k:k + 3] != gpu[k:k + 3])
			levels = max(abs(x - y) for x, y in zip(gl, gpu))
			note = '' if mean is None else f', FLIP mean {mean:.5f}'
			print(label + f'differs in {px} px, up to {levels} levels{note}  FAIL')
			failed.append(i)
		elif mean is None:
			print(label + 'cannot run FLIP (flip_evaluator or numpy missing)  FAIL')
			failed.append(i)
		else:
			means.append(mean)
			if mean > WORST_BOUND:
				print(label + f'FLIP mean {mean:.5f} > {WORST_BOUND}  FAIL')
				failed.append(i)
			elif mean > P95_BOUND:
				print(label + f'FLIP mean {mean:.5f} > {P95_BOUND}  over the p95 bound')
			else:
				print(label + f'FLIP mean {mean:.5f}  ok')
	for i in range(len(gl_sizes), len(gpu_sizes)):
		print(f'  frame {i}: extra frame from SDL_GPU, GL drew {len(gl_sizes)}  FAIL')
		failed.append(i)
	total = len(gl_sizes)
	if failed:
		print(f'compare-backends: {name}: FAIL ({len(failed)} of {total} frames fail: '
		      + ', '.join(str(i) for i in failed) + ')')
		return 1
	if exact:
		print(f'compare-backends: {name}: PASS ({total} of {total} frames bit-exact)')
		return 0
	ranked = sorted(means)
	p95 = ranked[math.ceil(0.95 * len(ranked)) - 1]
	worst = ranked[-1]
	if p95 > P95_BOUND:
		print(f'compare-backends: {name}: FAIL (p95 frame FLIP mean {p95:.5f} > {P95_BOUND}, '
		      f'worst {worst:.5f}, {total} frames)')
		return 1
	print(f'compare-backends: {name}: PASS (p95 frame FLIP mean {p95:.5f} <= {P95_BOUND}, '
	      f'worst {worst:.5f} <= {WORST_BOUND}, {total} frames)')
	return 0


sys.exit(main(sys.argv))
