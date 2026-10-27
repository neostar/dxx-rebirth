# Print every command in the given shell scripts that runs a registered GPU
# program other than as an argument of gpu-validate.sh, as "file:line:text",
# and exit 1 if there is one (ADR 5: GPU validation is a gate).
#
# A GPU program is named by $gpu_program (${gpu_program}, also inside a
# word such as "$root/$gpu_program") or by a path tools/dev/gpu-programs
# lists.  The scripts are split into commands, not lines: comments are
# stripped (a # starting a word, outside quotes), quotes and backslashes
# are honoured, line continuations joined, and ; & | && || ( ) and
# newlines end a command.  A command naming a GPU program passes only if a
# word ending in gpu-validate.sh comes before it and is the command's
# program: every word before that one is an assignment (X=y), a word
# ending in no-window.sh, or one of env, exec, command, time, !, {, if,
# then, do, else, elif, while, until, or bounded (rtrace-tests.sh's time
# bound, which runs its arguments as given).
#
#   python3 gpu_wrapper.py PROGRAMS_FILE SCRIPT...
import re
import sys

VAR = re.compile(r'\$(gpu_program\b|\{gpu_program\})')
LEADERS = {'env', 'exec', 'command', 'time', '!', '{', 'if', 'then', 'do', 'else', 'elif',
	'while', 'until', 'bounded'}


def commands(text):
	"""Yield (line, words) for each command; words keep their raw text."""
	words, word, line, start = [], '', 1, 1
	i, n, quote = 0, len(text), None

	def flush():
		nonlocal word
		if word:
			words.append(word)
			word = ''

	while i < n:
		ch = text[i]
		if quote:
			word += ch
			if ch == '\\' and quote == '"' and i + 1 < n:
				word += text[i + 1]
				line += text[i + 1] == '\n'
				i += 1
			elif ch == quote:
				quote = None
			line += ch == '\n'
		elif ch == '\\' and i + 1 < n:
			if text[i + 1] == '\n':
				line += 1
			else:
				word += text[i:i + 2]
			i += 1
		elif ch in '"\'':
			quote = ch
			word += ch
		elif ch == '#' and not word:
			j = text.find('\n', i)
			i = (n if j < 0 else j) - 1
		elif ch == '$' and text.startswith('$(', i):
			flush()
			if words:
				yield start, words
			words, start = [], line
			i += 1
		elif ch in ';&|()\n':
			flush()
			if words:
				yield start, words
			words = []
			line += ch == '\n'
			start = line
		elif ch in ' \t':
			flush()
		else:
			word += ch
		i += 1
	flush()
	if words:
		yield start, words


def wrapped(words, index):
	"""Is words[index] an argument of gpu-validate.sh run as the program?"""
	for j, w in enumerate(words[:index]):
		bare = w.strip('"\'')
		if bare.endswith('gpu-validate.sh'):
			return all(re.match(r'^\w+=', v) or v.strip('"\'').endswith('no-window.sh')
				or v in LEADERS for v in words[:j])
	return False


def main(programs_file, scripts):
	with open(programs_file) as f:
		programs = [p.strip() for p in f if p.strip() and not p.startswith('#')]
	bad = 0
	for script in scripts:
		try:
			with open(script) as f:
				text = f.read()
		except FileNotFoundError:
			continue
		lines = text.split('\n')
		for line, words in commands(text):
			for k, w in enumerate(words):
				if VAR.search(w) or any(p in w for p in programs):
					if not wrapped(words, k):
						print(f'{script}:{line}:{lines[line - 1]}')
						bad = 1
					break
	return bad


if __name__ == '__main__':
	sys.exit(main(sys.argv[1], sys.argv[2:]))
