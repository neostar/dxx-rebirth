# Strict warnings for fork code (ADR 5): -Wconversion -Wsign-conversion
# -Wshadow -Werror on every compile whose source lies in a directory listed
# in tools/dev/fork-dirs; upstream files compile as before.  site-dev.py
# calls install(), so the build.sh profiles get it and SConstruct stays
# unchanged.  `python3 fork_warnings.py` prints the flags.  A profile adds
# its own fork-only flags to FORK_ONLY_FLAGS (site-asan.py: UBSan's
# implicit-conversion, which upstream code trips).
import os
import subprocess

STRICT_FLAGS = ('-Wconversion', '-Wsign-conversion', '-Wshadow', '-Werror')
FORK_ONLY_FLAGS = []
_dev_dir = os.path.dirname(os.path.realpath(__file__))
_root = os.path.dirname(os.path.dirname(_dev_dir))


_gxx_accepts = {}


def gxx_accepts(flags):
	"""Whether g++, SCons' default CXX, compiles with flags; False without
	g++.  Probed once per process: SCons may read a site file twice."""
	key = tuple(flags)
	if key not in _gxx_accepts:
		try:
			probe = subprocess.run(['g++', *flags, '-x', 'c++', '-fsyntax-only', '-'],
				input=b'', capture_output=True)
			_gxx_accepts[key] = probe.returncode == 0
		except OSError:
			_gxx_accepts[key] = False
	return _gxx_accepts[key]


def fork_dirs():
	with open(os.path.join(_dev_dir, 'fork-dirs')) as f:
		lines = (line.strip() for line in f)
		return tuple(os.path.join(_root, d) + os.sep for d in lines if d and not d.startswith('#'))


def _path(node):
	# A SCons node, or a path relative to the top directory (build.sh runs
	# SCons from the repository root); resolved, since SCons sees the
	# physical working directory and this file may lie under a symlink.
	srcnode = getattr(node, 'srcnode', None)
	return os.path.realpath(srcnode().get_abspath() if srcnode else str(node))


def flags_for(target, source, env, for_signature):
	"""$_DXX_FORK_WARNINGS: STRICT_FLAGS and FORK_ONLY_FLAGS when a source
	is fork code."""
	dirs = fork_dirs()
	for s in source or ():
		if _path(s).startswith(dirs):
			return list(STRICT_FLAGS) + FORK_ONLY_FLAGS
	return []


def install():
	"""Make every SCons environment expand $_DXX_FORK_WARNINGS after the
	include flags, after all of SConstruct's own warning options."""
	import SCons.Defaults
	env = SCons.Defaults.ConstructionEnvironment
	env['_DXX_FORK_WARNINGS'] = flags_for
	if '$_DXX_FORK_WARNINGS' not in env['_CPPINCFLAGS']:
		env['_CPPINCFLAGS'] += ' $_DXX_FORK_WARNINGS'


if __name__ == '__main__':
	print(' '.join(STRICT_FLAGS))
