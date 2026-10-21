#!/usr/bin/env bash
# common/sdl/dxx-sdl.h, the one header through which fork code includes
# SDL, compiled against the pinned SDL3 headers the way sdl3=1 builds
# (ADR 3): the SDL3 include directory searched, SDL_ENABLE_OLD_NAMES on the
# command line.
source "$(dirname "$0")/lib.sh"
WRAPPER="$REPO_ROOT/common/sdl/dxx-sdl.h"

# Compile a file that includes the wrapper and uses the SDL2 name SDL_RWops,
# with the extra arguments given, from scratch directory $1.
compile_with_wrapper()
{
	local d=$1
	shift
	printf '%s\n' '#include "common/sdl/dxx-sdl.h"' \
		'int f(SDL_RWops *ops) { return SDL_MAJOR_VERSION + (ops != nullptr); }' > "$d/use.cpp"
	c++ -std=c++23 -fsyntax-only -I"$REPO_ROOT" -I"$dev_deps_prefix/include" \
		-I"$dev_deps_prefix/include/SDL3" "$@" "$d/use.cpp" 2>&1
}

# Test that the wrapper compiles with SDL3 under its SDL2 names when
# SDL_ENABLE_OLD_NAMES is defined.
test_sdl3_with_old_names_compiles()
{
	local d out
	[[ -e $dev_deps_prefix/include/SDL3/SDL.h ]] || skip "no pinned SDL3 headers in $dev_deps_prefix; run deps.sh"
	[[ -e $WRAPPER ]] || die "$WRAPPER is missing"
	d=$(make_scratch) || die "make_scratch failed"
	out=$(compile_with_wrapper "$d" -DSDL_ENABLE_OLD_NAMES)
	assert_eq "$?" 0 "$out"
}

# Test that the wrapper fails an SDL3 compile without SDL_ENABLE_OLD_NAMES
# with an error naming the define and ADR 3.
test_sdl3_without_old_names_fails()
{
	local d out rc
	[[ -e $dev_deps_prefix/include/SDL3/SDL.h ]] || skip "no pinned SDL3 headers in $dev_deps_prefix; run deps.sh"
	d=$(make_scratch) || die "make_scratch failed"
	out=$(compile_with_wrapper "$d") && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "SDL3 builds need SDL_ENABLE_OLD_NAMES on the command line (ADR 3)"
}

run_tests
