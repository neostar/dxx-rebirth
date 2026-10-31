# Print every registration of an SDL or SDL3_mixer audio callback in the
# repository's fork code outside the tests (every C, C++ and Objective-C(++)
# file git tracks or would track, outside third_party/, docs/ and unittest/
# and tests/ directories) as "file:line: ...", and exit 1 if there is one.
#
# docs/agents/sdl-audio.md area 4, rule 3: the game polls MIX_TrackPlaying
# from its per-frame batch and runs no code of its own on the mixing
# thread.  ThreadSanitizer cannot hold that rule: SDL and SDL3_mixer are not
# instrumented, so a race entered from their callbacks goes unreported (a
# planted stopped callback writing the game's bits ran on the mixing thread
# and TSan reported nothing).  This lint holds it instead: the names it
# refuses are MIX_Set...Callback (stopped, post-mix, and any later one),
# SDL_SetAudioStream...Callback and SDL_SetAudioPostmixCallback.  Comments
# and string literals are ignored; a macro or a lookup by string can still
# hide a call from this text scan.  SDL_OpenAudioDeviceStream also registers
# one when its third argument is not nullptr or NULL.
#
# The allowlist (default tools/dev/audio-callbacks-allow) lets a file keep
# registrations, one entry per file, as in sdl-arms-allow:
#   <path> <count> -- <reason>
# A slice that removes a registration lowers its count or drops the entry.
#
#   python3 check_audio_callbacks.py ROOT [ALLOWLIST]
import os
import re
import sys

from check_sdl_arms import read_allowlist
from gpu_debug_mode import sources, strip_comments

CALLBACK = re.compile(r'\b(MIX_Set\w*Callback|SDL_SetAudioStream\w*Callback|SDL_SetAudioPostmixCallback)\b')
OPEN_STREAM = re.compile(r'\bSDL_OpenAudioDeviceStream\s*\(')
NO_CALLBACK = re.compile(r'(nullptr|NULL)')
SKIPPED = re.compile(r'(^|/)(unittest|tests)/|^docs/')


def third_argument(code, start):
	"""Return the text of the third argument of the call whose '(' ends before start."""
	depth, arg, args = 0, start, []
	for i in range(start, len(code)):
		ch = code[i]
		if ch in '([{':
			depth += 1
		elif ch in ')]}':
			if depth == 0:
				args.append(code[arg:i])
				break
			depth -= 1
		elif ch == ',' and depth == 0:
			args.append(code[arg:i])
			arg = i + 1
	return args[2].strip() if len(args) > 2 else ''


def registrations(text):
	"""Return [(line, what)] for each callback registration in the source text."""
	code = re.sub(r'"(\\.|[^"\\\n])*"', lambda m: '"' + ' ' * (len(m.group()) - 2) + '"',
		strip_comments(text))
	found = [(m.start(), m.group(1)) for m in CALLBACK.finditer(code)]
	for m in OPEN_STREAM.finditer(code):
		if not NO_CALLBACK.fullmatch(third_argument(code, m.end())):
			found.append((m.start(), 'SDL_OpenAudioDeviceStream with a callback'))
	return [(code.count('\n', 0, start) + 1, what) for start, what in sorted(found)]


def main(root, allowlist):
	allowed, problems = read_allowlist(allowlist)
	name = os.path.basename(allowlist)
	have = {}
	for rel in sources(root):
		if SKIPPED.search(rel):
			continue
		try:
			with open(os.path.join(root, rel), encoding='utf-8', errors='replace') as f:
				text = f.read()
		except OSError:
			continue
		found = registrations(text)
		have[rel] = len(found)
		if len(found) <= allowed.get(rel, (0, 0))[0]:
			continue
		for line, what in found:
			problems.append(f'{rel}:{line}: {what} registers an audio callback'
				' (sdl-audio.md area 4, rule 3: poll from the frame batch instead)')
	for rel, (count, number) in allowed.items():
		if have.get(rel, 0) < count:
			problems.append(f'{name}:{number}: {rel} has {have.get(rel, 0)} registrations, lower the count')
	for problem in problems:
		print(problem)
	return 1 if problems else 0


if __name__ == '__main__':
	root = sys.argv[1]
	allowlist = sys.argv[2] if len(sys.argv) > 2 else os.path.join(root, 'tools/dev/audio-callbacks-allow')
	sys.exit(main(root, allowlist))
