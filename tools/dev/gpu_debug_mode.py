# Print every SDL_GPU device creation in the repository's C, C++ and
# Objective-C(++) sources (every file git tracks or would track, outside
# third_party/) that does not ask for SDL's debug mode, as "file:line: ...",
# and exit 1 if there is one (ADR 5: GPU validation is a gate, and SDL's
# debug mode is the program's own).  Game code creates the device too, so
# the scan is not limited to tools/dev/fork-dirs.
#
# Debug mode is asked for by `true` or by the constant `gpu_debug_mode`
# (namespace-qualified or not), which a release build may define false:
# - SDL_CreateGPUDevice(formats, DEBUG, name) with DEBUG one of those;
# - SDL_CreateGPUDeviceWithProperties(props) where the last
#   SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN, X)
#   before it in the same function, on that same props object, has X DEBUG;
# - any other SDL_CreateGPUDevice* is reported as unknown to this gate, and
#   so is one named without being called (its address taken, as in
#   &SDL_CreateGPUDevice or p = SDL_CreateGPUDevice), which could create a
#   device unseen.
# Comments are ignored.
#
#   python3 gpu_debug_mode.py ROOT
import os
import re
import subprocess
import sys

SOURCES = ('.c', '.cc', '.cpp', '.cxx', '.h', '.hh', '.hpp', '.m', '.mm')
DEBUG = re.compile(r'^(true|(\w+\s*::\s*)*gpu_debug_mode)$')
PROPERTY = 'SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN'


def strip_comments(text):
	"""Blank out comments, keeping newlines (line numbers) and literals."""
	out, i, n = [], 0, len(text)
	while i < n:
		ch = text[i]
		if text.startswith('//', i):
			j = text.find('\n', i)
			j = n if j < 0 else j
			out.append(' ' * (j - i))
			i = j
		elif text.startswith('/*', i):
			j = text.find('*/', i + 2)
			j = n if j < 0 else j + 2
			out.append(re.sub(r'[^\n]', ' ', text[i:j]))
			i = j
		elif ch in '"\'':
			j = i + 1
			while j < n and text[j] != ch and text[j] != '\n':
				j += 2 if text[j] == '\\' else 1
			out.append(text[i:j + 1])
			i = j + 1
		else:
			out.append(ch)
			i += 1
	return ''.join(out)


def arguments(text, start):
	"""Split the call's arguments from just after its '(' at top level."""
	args, depth, current = [], 0, ''
	for ch in text[start:]:
		if ch in '([{':
			depth += 1
		elif ch in ')]}':
			if depth == 0:
				return [a.strip() for a in args + [current]]
			depth -= 1
		elif ch == ',' and depth == 0:
			args.append(current)
			current = ''
			continue
		current += ch
	return None


def function_body(text, pos):
	"""Return (start, end) of the innermost brace block around pos that is
	not a namespace, extern "C", class, struct or union body."""
	stack, blocks = [], []
	for m in re.finditer(r'[{}]', text):
		if m.group() == '{':
			stack.append(m.start())
		elif stack:
			start = stack.pop()
			if start < pos < m.start():
				blocks.append((start, m.start()))
	# Outermost first: a function's own body, not a block inside it.
	for start, end in sorted(blocks):
		head = re.split(r'[;{}]', text[:start])[-1]
		if not re.search(r'\bnamespace\b|\bextern\s*"C"|\b(class|struct|union)\b[^()]*$', head):
			return start, end
	return 0, len(text)


def sets_debug_property(text, start, call, props):
	"""Is the last debug-mode property set on props before the call true?"""
	last = None
	for m in re.finditer(r'\bSDL_SetBooleanProperty\s*\(', text[start:call]):
		args = arguments(text, start + m.end())
		if args and len(args) == 3 and args[0] == props and args[1] == PROPERTY:
			last = args[2]
	return last is not None and bool(DEBUG.match(last))


def problem(text, call):
	name = call.group(1)
	if not call.group(2):
		return f'{name} named but not called (its address taken)'
	args = arguments(text, call.end())
	if name == 'SDL_CreateGPUDevice':
		if args is not None and len(args) == 3 and DEBUG.match(args[1]):
			return None
		return 'SDL_CreateGPUDevice without debug_mode true or gpu_debug_mode'
	if name == 'SDL_CreateGPUDeviceWithProperties':
		start, _ = function_body(text, call.start())
		if args and len(args) == 1 and sets_debug_property(text, start, call.start(), args[0]):
			return None
		return (f'{name} without {PROPERTY} last set to true or gpu_debug_mode '
			'on its props before it, in the same function')
	return f'{name}: a device creation gpu_debug_mode.py does not know'


def sources(root):
	files = subprocess.run(['git', '-C', root, 'ls-files', '-z', '-co', '--exclude-standard'],
		check=True, capture_output=True, text=True).stdout.split('\0')
	return sorted(f for f in files
		if f.endswith(SOURCES) and not f.startswith('third_party/'))


def main(root):
	bad = 0
	for rel in sources(root):
		try:
			with open(os.path.join(root, rel), errors='replace') as f:
				text = strip_comments(f.read())
		except FileNotFoundError:
			continue
		# Names inside string literals (a log message) are not uses.
		code = re.sub(r'"(\\.|[^"\\\n])*"', lambda m: '"' + ' ' * (len(m.group()) - 2) + '"', text)
		for call in re.finditer(r'\b(SDL_CreateGPUDevice\w*)\b(\s*\()?', code):
			message = problem(text, call)
			if message:
				line = text.count('\n', 0, call.start()) + 1
				print(f'{rel}:{line}: {message}; GPU validation needs it (ADR 5)')
				bad = 1
	return bad


if __name__ == '__main__':
	sys.exit(main(sys.argv[1]))
