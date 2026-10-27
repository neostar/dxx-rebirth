# List every call of an OpenGL function outside the GL adapter and the
# platform code, and exit 1 unless they are the ones the pending list names.
# ADR 2 keeps GL behind the render device: game code asks the device (draw,
# texture, clear, present, read_pixels, select_eye, copy_region) or its caps.
#
# Two passes count the same calls against the same lists:
#
#   python3 check_gl_calls.py text ROOT [ALLOW [PENDING]]
#       The text pass (check.sh --fast): no dependency but Python.  It strips
#       comments, string literals and #define lines from every C and C++
#       source under ROOT (outside third_party, build*, traces and dot
#       directories) and matches `glFoo(`, `gluFoo(` and the front end's
#       OGL_ENABLE, OGL_DISABLE and OGL_BINDTEXTURE macros.  It sees every
#       preprocessor branch of every file, also those a build leaves out.
#   python3 check_gl_calls.py clang ROOT BUILD_DIR CLANG_QUERY [ALLOW [PENDING]]
#       The precise pass (check.sh): clang-query on every translation unit of
#       BUILD_DIR/compile_commands.json (build/dev) lists the calls of a
#       function that gl.h, glu.h or glext.h declares, at the line of the
#       macro expansion.  A GL function reached through a pointer (loadgl.h's
#       dglFoo on Windows) is a variable, not a declared function, so only the
#       text pass names it.  A missing clang-query is a visible SKIP.  A
#       translation unit fails the check when clang-query reports any error
#       (it exits 0 on non-fatal ones and drops the calls of bodies it could
#       not parse), exits non-zero or times out, or when the matches it
#       prints are not the N its "N matches." line counts.
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
# CHECK_GL_CALLS_JOBS sets how many translation units are queried at once,
# CHECK_GL_CALLS_TIMEOUT (seconds, default 600) how long each may take,
# CHECK_GL_CALLS_SYSROOT and CHECK_GL_CALLS_RESOURCE_DIR the SDK and the clang
# resource directory clang-query uses, CHECK_GL_CALLS_LIST how many call sites
# a report names.
import concurrent.futures
import json
import os
import re
import subprocess
import sys

MATCHER = (r'match callExpr(callee(functionDecl(isExpansionInFileMatching('
	r'"(/|^)(gl|glu|glext)[.]h$"))))')
HIT = re.compile(r'^(.+?):(\d+):(\d+): note: "root" binds here$')
SUMMARY = re.compile(r'^(\d+) match(?:es)?\.$')
ERROR = re.compile(r': (?:fatal )?error: ')
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


def tool_arguments(clang_query):
	"""Return clang-query's extra arguments: the compilation database names
	the platform's compiler, which finds its headers itself, so clang-query
	needs the SDK (macOS) and its own resource directory, the lib/clang/N
	beside its bin/."""
	extra = []
	sysroot = os.environ.get('CHECK_GL_CALLS_SYSROOT')
	if not sysroot and sys.platform == 'darwin':
		sysroot = subprocess.run(['xcrun', '--show-sdk-path'], capture_output=True, text=True,
			check=True).stdout.strip()
	if sysroot:
		extra += ['--extra-arg=-isysroot', f'--extra-arg={sysroot}', '--extra-arg=-isystem',
			f'--extra-arg={sysroot}/usr/include/c++/v1']
	resource_dir = os.environ.get('CHECK_GL_CALLS_RESOURCE_DIR')
	clang_dir = os.path.join(os.path.dirname(os.path.dirname(clang_query)), 'lib', 'clang')
	if not resource_dir and os.path.isdir(clang_dir) and (versions := sorted(os.listdir(clang_dir))):
		resource_dir = os.path.join(clang_dir, versions[-1])
	if resource_dir:
		extra.append(f'--extra-arg=-resource-dir={resource_dir}')
	return extra


def query(clang_query, build_dir, extra, source):
	"""Return (hits, errors) of the matcher on source."""
	timeout = int(os.environ.get('CHECK_GL_CALLS_TIMEOUT') or 600)
	try:
		p = subprocess.run([clang_query, '-p', build_dir, '--extra-arg=-w', *extra, '-c',
			'set output diag', '-c', MATCHER, source],
			capture_output=True, text=True, check=False, timeout=timeout)
	except subprocess.TimeoutExpired:
		return [], [f'{source}: clang-query took more than {timeout} s']
	hits, errors, counted = [], [], None
	for line in (p.stdout + p.stderr).splitlines():
		if m := HIT.match(line):
			hits.append((m.group(1), int(m.group(2)), int(m.group(3))))
		elif m := SUMMARY.match(line):
			counted = int(m.group(1))
		elif ERROR.search(line):
			errors.append(f'{source}: {line}')
	if p.returncode != 0 and not errors:
		errors.append(f'{source}: clang-query exited {p.returncode}')
	if not errors:
		if counted is None:
			errors.append(f'{source}: clang-query printed no match count')
		elif counted != len(hits):
			errors.append(f'{source}: clang-query counted {counted} matches, {len(hits)} parsed')
	return hits, errors


def clang_hits(root, build_dir, clang_query):
	"""Return ({(path, line, column)}, errors) of the GL calls clang-query finds
	in every translation unit of the compilation database."""
	database_path = os.path.join(build_dir, 'compile_commands.json')
	if not os.path.isfile(database_path):
		return set(), [f'{database_path} is missing; run tools/dev/build.sh']
	with open(database_path, encoding='utf-8') as f:
		database = json.load(f)
	sources = []
	for entry in database:
		if not isinstance(entry['file'], str) or not entry['file'].endswith(SOURCE):
			continue
		path = os.path.normpath(os.path.join(entry['directory'], entry['file']))
		rel = os.path.relpath(path, root)
		if not rel.startswith(('third_party/', '..')) and path not in sources:
			sources.append(path)
	if not sources:
		return set(), [f'no translation unit in {database_path}: wrong build directory?']
	extra = tool_arguments(clang_query)
	jobs = int(os.environ.get('CHECK_GL_CALLS_JOBS') or os.cpu_count() or 1)
	found, errors = set(), []
	with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
		for hits, problems in pool.map(lambda s: query(clang_query, build_dir, extra, s), sources):
			errors += problems
			found.update(hits)
	print(f'gl_calls: clang-query on {len(sources)} translation units')
	return found, errors


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
	usage = ('usage: check_gl_calls.py text ROOT [ALLOW [PENDING]]\n'
		'       check_gl_calls.py clang ROOT BUILD_DIR CLANG_QUERY [ALLOW [PENDING]]')
	if len(argv) < 3 or argv[1] not in ('text', 'clang'):
		print(usage, file=sys.stderr)
		return 2
	mode, root = argv[1], os.path.abspath(argv[2])
	rest = argv[3:]
	here = os.path.dirname(os.path.abspath(__file__))
	if mode == 'clang':
		if not 2 <= len(rest) <= 4:
			print(usage, file=sys.stderr)
			return 2
		build_dir, clang_query = os.path.abspath(rest[0]), os.path.abspath(rest[1])
		rest = rest[2:]
	elif len(rest) > 2:
		print(usage, file=sys.stderr)
		return 2
	allow_path = rest[0] if rest else os.path.join(here, 'gl-calls-allow')
	pending_path = rest[1] if len(rest) > 1 else os.path.join(here, 'gl-calls-pending')
	for path in (allow_path, pending_path):
		if not os.path.isfile(path):
			print(f'gl_calls: {path} is missing', file=sys.stderr)
			return 1
	problems = []
	if mode == 'clang':
		if not os.access(clang_query, os.X_OK):
			print(f'SKIP gl_calls: {clang_query} not found; the pinned LLVM archive holds '
				'bin/clang-query, deps.sh does not install it yet')
			return 0
		found, problems = clang_hits(root, build_dir, clang_query)
	else:
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
