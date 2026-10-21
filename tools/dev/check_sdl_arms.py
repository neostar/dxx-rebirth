# Print every SDL_MAJOR_VERSION chain in the repository's C, C++ and
# Objective-C(++) sources (every file git tracks or would track, outside
# third_party/) that has an SDL2 branch but no SDL3 or >= 2 branch, as
# "file:line: ..." at its SDL2 branch, and exit 1 if there is one.  ADR 3
# builds SDL3 beside SDL1.2 and SDL2 with `#elif SDL_MAJOR_VERSION == 3`
# branches instead of an `#else #error` in every chain; this lint is what
# finds a chain an SDL3 build would silently take the wrong branch of.
#
# A chain is one #if/#ifdef/#ifndef with its #elif, #else and #endif at
# one nesting level.  Each condition is evaluated for SDL major versions
# 1, 2 and 3, its comparisons of SDL_MAJOR_VERSION with a number (either
# operand first) true or false and every other term unknown.  A branch is
# an SDL2 one when the chain compares SDL_MAJOR_VERSION up to it and an
# SDL2 build may take it: an #else after SDL1-only branches, != 1 and
# < 3 are SDL2 branches.  The chain is covered by a branch an SDL3 build
# may take whose condition compares explicitly for SDL3: == 3, or >= or >
# with a bound SDL3 meets (>= 2, > 1); an #else or != 1 does not cover.
# Comments are ignored and continued lines joined.
#
# The allowlist (default tools/dev/sdl-arms-allow) lets a file keep a
# number of such chains, one entry per line:
#   <path> <count> -- <reason>
# An entry without a reason, a second entry for a file, or one allowing
# more chains than the file has fails too, so the list shrinks as the SDL3
# slices land.
#
#   python3 check_sdl_arms.py ROOT [ALLOWLIST]
import itertools
import os
import re
import sys

from gpu_debug_mode import sources, strip_comments

DIRECTIVE = re.compile(r'^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)$')
COMPARISON = re.compile(r'\bSDL_MAJOR_VERSION\s*([=!<>]=|[<>])\s*(\d+)\b'
	r'|\b(\d+)\s*([=!<>]=|[<>])\s*SDL_MAJOR_VERSION\b')
REVERSED = {'<': '>', '<=': '>=', '>': '<', '>=': '<='}
DEFINED = re.compile(r'\bdefined\s*(\(\s*\w+\s*\)|\w+)')
TOKEN = re.compile(r'&&|\|\||!(?!=)|[()]|(?:[^&|!()]|!=)+')
OPERATOR = {'&&': ' and ', '||': ' or ', '!': ' not ', '(': '(', ')': ')'}
VERSIONS = (1, 2, 3)


def evaluate(condition):
	"""Return ({version: True, False or None}, compares, covers): the
	condition's value for each SDL major version (None when it depends on
	other terms), whether it compares SDL_MAJOR_VERSION, and whether it
	compares explicitly for SDL3."""
	comparisons = []

	def compare(m):
		op, bound = (m.group(1), m.group(2)) if m.group(1) else (REVERSED.get(m.group(4), m.group(4)), m.group(3))
		comparisons.append((op, int(bound)))
		return f' SDL_COMPARISON_{len(comparisons) - 1} '

	text = COMPARISON.sub(compare, DEFINED.sub(' defined_term ', condition))
	expr, terms = [], 0
	for token in TOKEN.findall(text):
		if token in OPERATOR:
			expr.append(OPERATOR[token])
		elif m := re.fullmatch(r'\s*SDL_COMPARISON_(\d+)\s*', token):
			op, bound = comparisons[int(m.group(1))]
			expr.append(f'(v {op} {bound})')
		elif token.strip():
			expr.append(f'u[{terms}]')
			terms += 1
	covers = any((op, bound) == ('==', 3) or (op, bound) in (('>=', 2), ('>=', 3), ('>', 1), ('>', 2))
		for op, bound in comparisons)
	values = {}
	for v in VERSIONS:
		try:
			seen = {bool(eval(''.join(expr), {'__builtins__': {}}, {'v': v, 'u': u}))
				for u in itertools.product((False, True), repeat=terms)}
		except SyntaxError:
			seen = {False, True}
		values[v] = seen.pop() if len(seen) == 1 else None
	return values, bool(comparisons), covers


def directives(text):
	"""Yield (line, keyword, condition) for each directive, comments
	blanked and continued lines joined, at the line it starts on."""
	lines = strip_comments(text).split('\n')
	i = 0
	while i < len(lines):
		start, line = i + 1, lines[i]
		while line.endswith('\\') and i + 1 < len(lines):
			i += 1
			line = line[:-1] + ' ' + lines[i]
		i += 1
		m = DIRECTIVE.match(line)
		if m:
			yield start, m.group(1), m.group(2)


def uncovered(text):
	"""Return the line of the first SDL2 branch of each chain with no SDL3
	branch."""
	stack, found = [], []
	for line, keyword, condition in directives(text):
		if keyword in ('if', 'ifdef', 'ifndef'):
			stack.append({'sdl2': None, 'sdl3': False, 'compares': False,
				'taken': {v: False for v in VERSIONS}})
		elif not stack:
			continue
		if keyword == 'endif':
			chain = stack.pop()
			if chain['sdl2'] is not None and not chain['sdl3']:
				found.append(chain['sdl2'])
			continue
		chain = stack[-1]
		if keyword == 'else':
			values, compares, covers = {v: True for v in VERSIONS}, False, False
		elif keyword in ('ifdef', 'ifndef'):
			values, compares, covers = {v: None for v in VERSIONS}, False, False
		else:
			values, compares, covers = evaluate(condition)
		chain['compares'] = chain['compares'] or compares
		taken = chain['taken']
		may_take = {v: not taken[v] and values[v] is not False for v in VERSIONS}
		if chain['compares'] and may_take[2] and chain['sdl2'] is None:
			chain['sdl2'] = line
		if covers and may_take[3]:
			chain['sdl3'] = True
		for v in VERSIONS:
			taken[v] = taken[v] or values[v] is True
	return sorted(found)


ENTRY = re.compile(r'^(\S+)\s+(\d+)\s+--\s+\S')


def read_allowlist(path):
	"""Return ({path: (count, entry line)}, [problems]) from the allowlist."""
	allowed, problems = {}, []
	name = os.path.basename(path)
	try:
		with open(path, encoding='utf-8') as f:
			lines = f.read().split('\n')
	except FileNotFoundError:
		return allowed, problems
	for number, line in enumerate(lines, 1):
		if not line.strip() or line.lstrip().startswith('#'):
			continue
		m = ENTRY.match(line)
		if not m:
			problems.append(f'{name}:{number}: entry needs <path> <count> -- <reason>')
			continue
		if m.group(1) in allowed:
			problems.append(f'{name}:{number}: second entry for {m.group(1)}, first at line {allowed[m.group(1)][1]}')
			continue
		allowed[m.group(1)] = (int(m.group(2)), number)
	return allowed, problems


def main(root, allowlist):
	allowed, problems = read_allowlist(allowlist)
	name = os.path.basename(allowlist)
	found = {}
	for rel in sources(root):
		try:
			with open(os.path.join(root, rel), encoding='utf-8', errors='replace') as f:
				text = f.read()
		except OSError:
			continue
		lines = uncovered(text)
		if lines:
			found[rel] = lines
	for rel, lines in found.items():
		count = allowed.get(rel, (0, 0))[0]
		if len(lines) <= count:
			continue
		for line in lines:
			problems.append(f'{rel}:{line}: SDL2 branch without an SDL3 or >= 2 branch (ADR 3)')
		if count:
			problems.append(f'{rel}: {len(lines)} uncovered chains, the allowlist allows {count}')
	for rel, (count, number) in allowed.items():
		have = len(found.get(rel, ()))
		if have < count:
			problems.append(f'{name}:{number}: {rel} has {have} uncovered chains, lower the count')
	for problem in problems:
		print(problem)
	return 1 if problems else 0


if __name__ == '__main__':
	root = sys.argv[1]
	allowlist = sys.argv[2] if len(sys.argv) > 2 else os.path.join(root, 'tools/dev/sdl-arms-allow')
	sys.exit(main(root, allowlist))
