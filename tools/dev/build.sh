#!/usr/bin/env bash
# Build D2X-Rebirth with the fork's development settings.
#
#   build.sh [dev|asan] [scons arguments...]
#
# dev (default): build/dev, no LTO, ccache, constant version string,
#   compilation database in build/dev/compile_commands.json.
# asan: build/asan with AddressSanitizer and UndefinedBehaviorSanitizer;
#   site-dev.py, then site-asan.py's overrides (SCons reads site files in
#   order, later ones win).
# Both build D2X only: SConstruct reads d1x and d2x from the command line
# alone, not from site files, so they are passed here.
# Pass `check` to build and run the unit tests; scons then runs under
# no-window.sh, so a test that takes focus or crashes fails the build.
set -eu
dev_dir=$(cd "$(dirname "$0")" && pwd)
cd "$dev_dir/../.."

profile=dev
case "${1-}" in
	dev|asan) profile=$1; shift ;;
esac

# Sanitizer options (ASAN_OPTIONS, UBSAN_OPTIONS).
source "$dev_dir/env.sh"

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

jobs=$dev_jobs
sites=("site=$dev_dir/site-dev.py")
[[ $profile == asan ]] && sites+=("site=$dev_dir/site-asan.py")
wrap=()
for a in "$@"; do
	[[ $a == check ]] && wrap=("$dev_dir/no-window.sh")
done
exec "${wrap[@]}" scons -j"$jobs" d1x=0 d2x=1 "${sites[@]}" "$@"
