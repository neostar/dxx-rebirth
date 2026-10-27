# List every call of an OpenGL function outside the GL adapter and the
# platform code, and exit 1 unless they are the ones the pending list names.
# ADR 2 keeps GL behind the render device: game code asks the device (draw,
# texture, clear, present, read_pixels, select_eye, copy_region) or its caps.
#
#   python3 check_gl_calls.py text ROOT [ALLOW [PENDING]]
#       The text pass (check.sh --fast): no dependency but Python.  It strips
#       comments, string literals and #define lines from every C and C++
#       source under ROOT (outside third_party, build*, traces and dot
#       directories) and matches `glFoo(`, `gluFoo(` and the front end's
#       OGL_ENABLE, OGL_DISABLE and OGL_BINDTEXTURE macros.  It sees every
#       preprocessor branch of every file, also those a build leaves out.
#
# The allow list (tools/dev/gl-calls-allow, one path regex per line) names the
# places GL may be called from: the adapter, the platform's context,
# extension and sync code, the rtrace GL host and the adapter's pixel tests.
# A call elsewhere is reported as "file:line:col" and fails.
#
# The pending list (tools/dev/gl-calls-pending), one entry per line:
#   <path> <count> -- <reason>
# lets a file keep that many calls until the ticket named in the reason moves
# them to the device; a file may have several entries with different reasons,
# which add up.  An entry without a reason, a second entry with the same
# reason, or entries allowing more calls than the file has fails too, so the
# list shrinks to empty as the tickets land.
#
# CHECK_GL_CALLS_LIST sets how many call sites a report names.
import os
import re
import sys

TEXT_CALL = re.compile(r'\bglu?[A-Z]\w*\s*\(|\bOGL_(?:ENABLE|DISABLE|BINDTEXTURE)\s*\(')
SOURCE = ('.c', '.cc', '.cpp', '.cxx', '.h', '.hh', '.hpp', '.hxx', '.m', '.mm')
SKIP_DIRS = ('third_party', 'traces')


def read_list(path):
	"""Return the non-comment lines of the file at path."""
	with open(path, encoding='utf-8') as f:
		return [line.rstrip('\n') for line in f if line.strip() and not line.lstrip().startswith('#')]


def parse_pending(path):
	"""Return ({file: count}, errors) of the pending list."""
	pending, errors, seen = {}, [], set()
	for line in read_list(path):
		m = re.fullmatch(r'(\S+) (\d+) -- (\S.*)', line)
		if not m:
			errors.append(f'{path}: not "<path> <count> -- <reason>": {line}')
		elif (m.group(1), m.group(3)) in seen:
			errors.append(f'{path}: second entry for {m.group(1)} with the reason "{m.group(3)}"')
		else:
			seen.add((m.group(1), m.group(3)))
			pending[m.group(1)] = pending.get(m.group(1), 0) + int(m.group(2))
	return pending, errors


def strip_text(text):
	"""Return text with comments, string and character literals and #define
	lines blanked, keeping its line breaks."""
	def blank(m):
		return '\n' * m.group(0).count('\n')
	text = re.sub(r'/\*.*?\*/', blank, text, flags=re.S)
	text = re.sub(r'//[^\n]*', '', text)
	text = re.sub(r'"(?:\\.|[^"\\\n])*"', '""', text)
	text = re.sub(r"'(?:\\.|[^'\\\n])*'", "''", text)
	return re.sub(r'(?m)^[ \t]*#[ \t]*define\b[^\n]*(?:\\\n[^\n]*)*', blank, text)


def text_hits(root):
	"""Return {(path, line, column)} of the GL calls the text pass sees."""
	hits = set()
	for directory, subdirs, names in os.walk(root):
		subdirs[:] = [d for d in subdirs
			if not d.startswith(('.', 'build')) and not (directory == root and d in SKIP_DIRS)]
		for name in names:
			if not name.endswith(SOURCE):
				continue
			path = os.path.join(directory, name)
			with open(path, encoding='utf-8', errors='replace') as f:
				text = strip_text(f.read())
			for number, line in enumerate(text.split('\n'), 1):
				for m in TEXT_CALL.finditer(line):
					hits.add((path, number, m.start() + 1))
	return hits


def compare(root, found, allow_path, pending_path, problems):
	"""Append to problems what the hits found break; return the count outside
	the allowed places and the count the pending list allows."""
	allow = [re.compile(r) for r in read_list(allow_path)]
	pending, errors = parse_pending(pending_path)
	problems += errors
	counts, where = {}, {}
	for path, line, column in sorted(found):
		rel = os.path.relpath(os.path.normpath(os.path.join(root, path)), root)
		if any(r.search(rel) for r in allow):
			continue
		counts[rel] = counts.get(rel, 0) + 1
		where.setdefault(rel, []).append(f'{rel}:{line}:{column}')
	listed = int(os.environ.get('CHECK_GL_CALLS_LIST') or 8)
	for rel, n in sorted(counts.items()):
		allowed = pending.get(rel, 0)
		if n > allowed:
			problems.append(f'{n - allowed} GL call(s) outside the GL adapter in {rel} '
				f'(pending {allowed}): ' + ', '.join(where[rel][:listed]))
	for rel, allowed in sorted(pending.items()):
		if counts.get(rel, 0) < allowed:
			problems.append(f'{pending_path}: {rel} allows {allowed} but has {counts.get(rel, 0)}: lower it')
	return sum(counts.values()), sum(pending.values())


def main(argv):
	usage = 'usage: check_gl_calls.py text ROOT [ALLOW [PENDING]]'
	if len(argv) < 3 or argv[1] != 'text':
		print(usage, file=sys.stderr)
		return 2
	mode, root = argv[1], os.path.abspath(argv[2])
	rest = argv[3:]
	here = os.path.dirname(os.path.abspath(__file__))
	if len(rest) > 2:
		print(usage, file=sys.stderr)
		return 2
	allow_path = rest[0] if rest else os.path.join(here, 'gl-calls-allow')
	pending_path = rest[1] if len(rest) > 1 else os.path.join(here, 'gl-calls-pending')
	for path in (allow_path, pending_path):
		if not os.path.isfile(path):
			print(f'gl_calls: {path} is missing', file=sys.stderr)
			return 1
	problems = []
	found = text_hits(root)
	outside, pending = compare(root, found, allow_path, pending_path, problems)
	for p in problems[:20]:
		print(f'gl_calls: {p}', file=sys.stderr)
	if len(problems) > 20:
		print(f'gl_calls: ... and {len(problems) - 20} more', file=sys.stderr)
	print(f'gl_calls ({mode}): {outside} GL call(s) outside the GL adapter and platform code, '
		f'{pending} pending')
	return 1 if problems else 0


if __name__ == '__main__':
	sys.exit(main(sys.argv))
