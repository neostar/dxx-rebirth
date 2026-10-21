# SCons overrides for the AddressSanitizer + UndefinedBehaviorSanitizer
# build (tools/dev/build.sh asan), read after site-dev.py: everything not
# set here comes from there.  Separate build directory; debug keeps asserts.
#
# UBSan's Clang-only checks (ADR 5, Level 2), which `undefined` leaves out,
# are added where g++ (SCons' default CXX) has them: Apple clang on the Mac;
# GCC 14 in the container has none, and a note says so (as it does when g++
# is missing).  fork_warnings.gxx_accepts probes g++ once per process.  local-bounds
# reports through the UBSan runtime instead of trapping, since a trap leaves
# a crash report on macOS.  `-fno-sanitize-recover` makes each one fatal.
import sys

# SCons puts this file's directory on sys.path while it reads it.
import fork_warnings

CLANG_UBSAN = ('implicit-conversion', 'local-bounds', 'nullability')
# Upstream's unit tests convert -1 to unsigned on purpose
# (common/unittest/valptridx-range.cpp), so these are for fork code only
# (fork_warnings.py); the others are on for every file.
FORK_ONLY_UBSAN = ('implicit-conversion',)
# SCons executes this file with separate globals and locals, so no function
# here may refer to a name defined here: plain loops instead.
CLANG_UBSAN_MISSING = []
_extra = []
FORK_ONLY_CXXFLAGS = []
for _check in CLANG_UBSAN:
	_flags = ['-fsanitize=' + _check, '-fno-sanitize-recover=' + _check]
	if _check == 'local-bounds':
		_flags.append('-fno-sanitize-trap=' + _check)
	if not fork_warnings.gxx_accepts(_flags):
		CLANG_UBSAN_MISSING.append(_check)
	elif _check in FORK_ONLY_UBSAN:
		FORK_ONLY_CXXFLAGS += _flags
	else:
		_extra += _flags
if CLANG_UBSAN_MISSING:
	print('site-asan.py: g++ lacks -fsanitize=' + ','.join(CLANG_UBSAN_MISSING)
		+ '; skipped', file=sys.stderr)

builddir = 'build/asan'
compilation_database = 'build/asan/compile_commands.json'
debug = True
CXXFLAGS = ('-O1 -fsanitize=address,undefined -fno-omit-frame-pointer '
	'-fno-sanitize-recover=undefined ' + ' '.join(_extra)).rstrip()
LINKFLAGS = '-fsanitize=address,undefined'
fork_warnings.FORK_ONLY_FLAGS[:] = FORK_ONLY_CXXFLAGS
