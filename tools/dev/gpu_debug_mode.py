# Print every SDL_CreateGPUDevice call in the fork's C and C++ files
# (tools/dev/fork-dirs) whose debug_mode argument is not literally true, as
# "file:line: ...", and exit 1 if there is one (ADR 5: GPU validation is a
# gate, and SDL's debug mode is the program's own).
#
#   python3 gpu_debug_mode.py ROOT
import os
import re
import sys

SOURCES = ('.c', '.cc', '.cpp', '.cxx', '.h', '.hh', '.hpp')


def arguments(text, start):
	"""Split the call's arguments from just after its '(' at top level."""
	args, depth, current = [], 0, ''
	for ch in text[start:]:
		if ch in '([{':
			depth += 1
		elif ch in ')]}':
			if depth == 0:
				return args + [current]
			depth -= 1
		elif ch == ',' and depth == 0:
			args.append(current)
			current = ''
			continue
		current += ch
	return None


def main(root):
	with open(os.path.join(root, 'tools/dev/fork-dirs')) as f:
		dirs = [d.strip() for d in f if d.strip() and not d.startswith('#')]
	bad = 0
	for d in dirs:
		for path, _, files in os.walk(os.path.join(root, d)):
			for name in sorted(files):
				if not name.endswith(SOURCES):
					continue
				file = os.path.join(path, name)
				with open(file, errors='replace') as f:
					text = f.read()
				for call in re.finditer(r'\bSDL_CreateGPUDevice\s*\(', text):
					args = arguments(text, call.end())
					if args is not None and len(args) == 3 and args[1].strip() == 'true':
						continue
					line = text.count('\n', 0, call.start()) + 1
					print(f'{os.path.relpath(file, root)}:{line}: SDL_CreateGPUDevice without '
						'debug_mode true; GPU validation needs it (ADR 5)')
					bad = 1
	return bad


if __name__ == '__main__':
	sys.exit(main(sys.argv[1]))
