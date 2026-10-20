#!/usr/bin/env bash
# Build D2X-Rebirth with the fork's development settings.
#
#   build.sh [dev|asan] [scons arguments...]
#
# dev (default): build/dev, no LTO, ccache, constant version string,
#   compilation database in build/dev/compile_commands.json.
# asan: build/asan with AddressSanitizer and UndefinedBehaviorSanitizer.
# Pass `check` to build and run the unit tests.
set -eu
dev_dir=$(cd "$(dirname "$0")" && pwd)
cd "$dev_dir/../.."

profile=dev
case "${1-}" in
	dev|asan) profile=$1; shift ;;
esac

# Sanitizer failures exit non-zero instead of calling abort(): on macOS an
# abort brings up a CrashReporter dialog.  LeakSanitizer is unsupported on
# Apple Silicon macOS (use `leaks --atExit` there); elsewhere it stays on.
detect_leaks=1
[[ $(uname -s) == Darwin ]] && detect_leaks=0
export ASAN_OPTIONS=${ASAN_OPTIONS:-abort_on_error=0:halt_on_error=1:detect_leaks=$detect_leaks}
export UBSAN_OPTIONS=${UBSAN_OPTIONS:-print_stacktrace=1:halt_on_error=1:abort_on_error=0}

# Homebrew's sdl2-compat dlopens SDL3 and shows a modal "Failed loading SDL3
# library" alert if it cannot.  Under ASan its first candidate,
# @loader_path/libSDL3.dylib, resolves against the ASan runtime's directory,
# and DYLD_* variables are stripped by /bin/sh (SIP), through which SCons runs
# the tests.  Its next candidate, @executable_path/libSDL3.dylib, is put next
# to the test programs.
if [[ $profile == asan && $(uname -s) == Darwin ]] && command -v brew >/dev/null; then
	sdl3=$(brew --prefix)/lib/libSDL3.dylib
	if [[ -e $sdl3 ]]; then
		for d in build/asan/common build/asan/d2x-rebirth; do
			mkdir -p "$d"
			ln -sf "$sdl3" "$d/libSDL3.dylib"
		done
	fi
fi
# Tests that create an SDL window stay in the background on macOS.
export SDL_MAC_BACKGROUND_APP=1

jobs=$(sysctl -n hw.ncpu 2>/dev/null || nproc)
exec scons -j"$jobs" d1x=0 d2x=1 "site=$dev_dir/site-$profile.py" "$@"
