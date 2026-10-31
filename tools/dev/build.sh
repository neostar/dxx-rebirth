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
# Both build with the hardened standard library (hardening.py, ADR 5).
# build/ is kept out of Time Machine backups (env.sh's dev_tm_exclude).
# Both build D2X only, unless the arguments choose the games: SConstruct reads
# d1x and d2x from the command line alone, not from site files, so build.sh
# passes d1x=0 d2x=1.  Any d1x=, d2x= or dxx= argument replaces that pair (a
# repeated d1x= is a list of prefixes to SConstruct, so d1x=0 beside d1x=1
# fails with "Illegal Variables key '0_expect_sconf...'"): `d1x=1 d2x=1` builds
# both, `d1x=1` D1X alone, as SConstruct reads them.
# Pass `check` to build and run the unit tests; scons then runs under
# no-window.sh, so a test that takes focus or crashes fails the build.
# Pass `sdl3=1` (any SCons true spelling: yes, true, on, ...; the last
# sdl3= wins) to build against the pinned SDL3 ($dev_deps_prefix) in
# build/<profile>-sdl3.  Every build, sdl3 or not, links the pinned PhysicsFS
# of $dev_deps_prefix (patched against CVE-2024-58392, ticket #87) and fails
# without it: pkg-config reads the prefix's .pc files first (physfs.pc names
# the prefix where deps.sh built it) and site-deps.py puts its lib directory
# ahead of Homebrew's.  For sdl3=1, pkg-config also relocates the .pc files to
# where the prefix lies (--define-prefix), and site-sdl3.py, read last, links the pinned libraries ahead of Homebrew's
# and on macOS makes Homebrew's headers system ones (see its header).
# ccache shares its cache across worktrees: CCACHE_BASEDIR, the source
# tree, has it rewrite the tree's absolute paths (hardening-exit.h's
# -include) to relative ones, and site-dev.py maps the tree to . in the
# debug information, which ccache otherwise hashes the directory of.
# Each build directory keeps its own SCons signature database
# (<builddir>/.sconsign.dblite, through DXX_SCONSIGN and site-dev.py):
# with SConstruct's one build/.sconsign, builds running at once (the
# merge gate's R1 beside its unit tests) drop each other's signatures.
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
if [[ $profile == asan && $dev_os == darwin ]] && command -v brew >/dev/null; then
	sdl3=$(brew --prefix)/lib/libSDL3.dylib
	if [[ -e $sdl3 ]]; then
		for d in build/asan/common build/asan/d2x-rebirth; do
			mkdir -p "$d"
			ln -sf "$sdl3" "$d/libSDL3.dylib"
		done
	fi
fi
# Build trees are rebuilt, never restored from a backup.
dev_tm_exclude "$PWD/build"
# Tests that create an SDL window stay in the background on macOS.
export SDL_MAC_BACKGROUND_APP=1

jobs=$dev_jobs
sites=("site=$dev_dir/site-dev.py")
[[ $profile == asan ]] && sites+=("site=$dev_dir/site-asan.py")
sites+=("site=$dev_dir/site-deps.py")
wrap=() sdl3=0 games=(d1x=0 d2x=1)
for a in "$@"; do
	[[ $a == d1x=* || $a == d2x=* || $a == dxx=* ]] && games=()
	[[ $a == check ]] && wrap=("$dev_dir/no-window.sh")
	if [[ $a == sdl3=* ]]; then
		# SCons' BoolVariable spellings, in any case.
		case $(tr '[:upper:]' '[:lower:]' <<<"${a#sdl3=}") in
			y|yes|true|t|1|on|all) sdl3=1 ;;
			n|no|false|f|0|off|none) sdl3=0 ;;
			*)
				echo "build.sh: $a is not a SCons boolean (1/0, yes/no, true/false, on/off, ...)" >&2
				exit 2 ;;
		esac
	fi
done
# Every build links the pinned PhysicsFS of $dev_deps_prefix (patched, #87),
# never Homebrew's or the system's: no physfs.pc there is an error.  After
# the argument checks, so a usage error is reported first.  SCons splits
# flags at spaces (site-deps.py), so the prefix may not hold one.
[[ $dev_deps_prefix != *" "* ]] || {
	echo "build.sh: DXX_DEPS_PREFIX $dev_deps_prefix holds a space, which SCons splits its flags at" >&2
	exit 1
}
[[ -f $dev_deps_prefix/lib/pkgconfig/physfs.pc ]] || {
	echo "build.sh: no PhysicsFS in $dev_deps_prefix; run tools/dev/deps.sh" >&2
	exit 1
}
# pkg-config reads the prefix's .pc files first.
PKG_CONFIG_PATH=$dev_deps_prefix/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}
export PKG_CONFIG_PATH DXX_DEPS_PREFIX=$dev_deps_prefix
build=()
if [[ $sdl3 == 1 ]]; then
	sites+=("site=$dev_dir/site-sdl3.py")
	build=(builddir="build/$profile-sdl3"
		compilation_database="build/$profile-sdl3/compile_commands.json")
	# --define-prefix relocates the .pc files to where the prefix lies (it
	# would also move the system ones of libpng and the SDL1.2/2 libraries,
	# which a plain build reads too: Ubuntu's would name /usr/lib).
	# SConstruct finds a PKG_CONFIG with arguments only by its full path.
	PKG_CONFIG="$(command -v pkg-config || echo pkg-config) --define-prefix"
	DXX_SDL3_PREFIX=$dev_deps_prefix DXX_SDL3_SYSTEM_INCLUDE=
	if [[ $dev_os == darwin ]] && command -v brew >/dev/null; then
		DXX_SDL3_SYSTEM_INCLUDE=$(brew --prefix)/include
	fi
	export PKG_CONFIG DXX_SDL3_PREFIX DXX_SDL3_SYSTEM_INCLUDE
fi
# The build directory SCons uses: the last builddir= wins, as in SCons.
dir=build/$profile
[[ $sdl3 == 1 ]] && dir=build/$profile-sdl3
for a in "$@"; do
	[[ $a == builddir=* ]] && dir=${a#builddir=}
done
mkdir -p "$dir"
export CCACHE_BASEDIR=$PWD DXX_SCONSIGN=$dir/.sconsign
exec "${wrap[@]}" scons -j"$jobs" ${games[@]+"${games[@]}"} "${sites[@]}" "${build[@]}" "$@"
