# Print every SDL_MAJOR_VERSION chain in the repository's C, C++ and
# Objective-C(++) sources (every file git tracks or would track, outside
# third_party/) that has an SDL2 branch but no SDL3 or >= 2 branch, as
# "file:line: ..." at its SDL2 branch, and exit 1 if there is one.  ADR 3
# builds SDL3 beside SDL1.2 and SDL2 with `#elif SDL_MAJOR_VERSION == 3`
# branches instead of an `#else #error` in every chain; this lint is what
# finds a chain an SDL3 build would silently take the wrong branch of.
#
# A chain is one #if/#ifdef/#ifndef with its #elif, #else and #endif at
# one nesting level.  A branch is an SDL2 one when its condition says
# SDL_MAJOR_VERSION == 2, an SDL3 one when it says SDL_MAJOR_VERSION == 3
# or SDL_MAJOR_VERSION >= 2.  Comments are ignored and continued lines
# joined.
#
# The allowlist (default tools/dev/sdl-arms-allow) lets a file keep a
# number of such chains, one entry per line:
#   <path> <count> -- <reason>
# An entry without a reason, a second entry for a file, or one allowing
# more chains than the file has fails too, so the list shrinks as the SDL3
# slices land.
#
#   python3 check_sdl_arms.py ROOT [ALLOWLIST]
import os
import re
import sys

from gpu_debug_mode import sources, strip_comments

DIRECTIVE = re.compile(r'^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)$')
SDL2 = re.compile(r'\bSDL_MAJOR_VERSION\s*==\s*2\b')
SDL3 = re.compile(r'\bSDL_MAJOR_VERSION\s*(==\s*3|>=\s*2)\b')


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
	"""Return the line of the SDL2 branch of each chain with no SDL3 branch."""
	stack, found = [], []
	for line, keyword, condition in directives(text):
		if keyword in ('if', 'ifdef', 'ifndef'):
			stack.append({'sdl2': None, 'sdl3': False})
		elif not stack:
			continue
		if keyword == 'endif':
			chain = stack.pop()
			if chain['sdl2'] is not None and not chain['sdl3']:
				found.append(chain['sdl2'])
			continue
		chain = stack[-1]
		if SDL2.search(condition) and chain['sdl2'] is None:
			chain['sdl2'] = line
		if SDL3.search(condition):
			chain['sdl3'] = True
	return found


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
