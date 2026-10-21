# Hardened standard library for every build.sh profile (ADR 5, ticket
# #58): container bounds, iterator and optional misuse traps at run time.
# Each macro is read only by its own library, so both are always defined:
# libc++ (the Mac) and libstdc++ (GCC and Clang in the container).
# hardening-exit.h, forced into every compile, makes a libc++ trap exit 1
# instead of abort(), which on macOS leaves a crash report.
# site-dev.py sets them as CPPFLAGS, so the dev and asan profiles get them;
# `python3 hardening.py` prints the flags for the gate self-test.  SCons
# splits CPPFLAGS at spaces, so the clone's path must have none.
import os

HARDENING_FLAGS = (
	'-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_DEBUG',
	'-D_GLIBCXX_ASSERTIONS',
	'-include',
	os.path.join(os.path.dirname(os.path.abspath(__file__)), 'hardening-exit.h'),
)

if __name__ == '__main__':
	print(' '.join(HARDENING_FLAGS))
