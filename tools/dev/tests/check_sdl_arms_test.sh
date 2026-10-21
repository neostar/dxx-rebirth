#!/usr/bin/env bash
# Command-line behaviour of tools/dev/check_sdl_arms.py: every
# SDL_MAJOR_VERSION chain with an SDL2 branch has an SDL3 or >= 2 branch
# (ADR 3), or an allowlist entry with a reason.
source "$(dirname "$0")/lib.sh"
LINT="$DEV_DIR/check_sdl_arms.py"

# Make a throwaway repository with an empty allowlist and print its path.
make_lint_repo()
{
	local r
	r=$(make_repo) || die "make_repo failed"
	mkdir -p "$r/tools/dev"
	: > "$r/tools/dev/sdl-arms-allow"
	printf '%s\n' "$r"
}

# Test that a chain with an SDL2 branch and no SDL3 branch is reported at
# its SDL2 branch, and that chains with an SDL3 or >= 2 branch pass.
test_sdl2_chain_without_sdl3_reported()
{
	local r out rc
	r=$(make_lint_repo) || die "make_lint_repo failed"
	mkdir -p "$r/common/arch"
	printf '%s\n' '#if SDL_MAJOR_VERSION == 1' 'a' '#elif SDL_MAJOR_VERSION == 2' 'b' '#endif' \
		'#if SDL_MAJOR_VERSION == 2' 'c' '#elif SDL_MAJOR_VERSION == 3' 'd' '#endif' \
		'#if SDL_MAJOR_VERSION == 1' 'e' '#elif SDL_MAJOR_VERSION >= 2' 'f' '#endif' \
		'#  if DXX_USE_SDLMIXER && SDL_MAJOR_VERSION == 2' 'g' '#  endif' > "$r/common/arch/a.cpp"
	out=$(python3 "$LINT" "$r" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "common/arch/a.cpp:3: SDL2 branch without an SDL3 or >= 2 branch"
	assert_contains "$out" "common/arch/a.cpp:16: SDL2 branch without an SDL3 or >= 2 branch"
	assert_eq "$(grep -c 'SDL2 branch without' <<<"$out")" 2 "$out"
}

# Test that every branch an SDL2 build takes counts as an SDL2 one: an
# #else after SDL1-only branches, != 1, < 2 and reversed operands, also
# beside other conditions; a chain no SDL2 build enters is not reported.
test_every_form_of_sdl2_branch_reported()
{
	local r out rc
	r=$(make_lint_repo) || die "make_lint_repo failed"
	printf '%s\n' '#if SDL_MAJOR_VERSION == 1' 'a' '#else' 'b' '#endif' \
		'#if DXX_USE_OGLES && SDL_MAJOR_VERSION == 1' '#else' '#endif' \
		'#if defined(__linux__) || SDL_MAJOR_VERSION != 1' '#endif' \
		'#if SDL_MAJOR_VERSION < 2' '#elif X' '#endif' \
		'#if 2 == SDL_MAJOR_VERSION' '#endif' \
		'#if SDL_MAJOR_VERSION <= 2 && (X || !Y)' '#endif' \
		'#if SDL_MAJOR_VERSION == 1' '#endif' \
		'#ifdef X' '#elif !(SDL_MAJOR_VERSION == 1)' '#endif' > "$r/g.cpp"
	out=$(python3 "$LINT" "$r" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	local line
	for line in 3 7 9 12 14 16 21; do
		assert_contains "$out" "g.cpp:$line: SDL2 branch without an SDL3 or >= 2 branch"
	done
	assert_eq "$(grep -c 'SDL2 branch without' <<<"$out")" 7 "$out"
}

# Test that only an explicit SDL3 or >= 2-style branch that an SDL3 build
# takes covers a chain: == 3, >= 2, > 1 and their reversed forms do; an
# #else, != 1, and an SDL3 branch after one an SDL3 build already took do
# not.
test_only_an_sdl3_branch_sdl3_takes_covers()
{
	local r out rc
	r=$(make_lint_repo) || die "make_lint_repo failed"
	printf '%s\n' '#if SDL_MAJOR_VERSION == 3' '#else' '#endif' \
		'#if SDL_MAJOR_VERSION == 1' '#elif SDL_MAJOR_VERSION == 3' '#else' '#endif' \
		'#if SDL_MAJOR_VERSION > 1' '#endif' \
		'#if 1 < SDL_MAJOR_VERSION' '#endif' \
		'#if 3 == SDL_MAJOR_VERSION' '#else' '#endif' \
		'#if X && SDL_MAJOR_VERSION >= 2' '#endif' \
		'#if SDL_MAJOR_VERSION != 1' '#elif SDL_MAJOR_VERSION == 3' '#endif' \
		'#if SDL_MAJOR_VERSION == 2' '#elif SDL_MAJOR_VERSION != 1' '#endif' > "$r/h.cpp"
	out=$(python3 "$LINT" "$r" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "h.cpp:17: SDL2 branch without an SDL3 or >= 2 branch"
	assert_contains "$out" "h.cpp:20: SDL2 branch without an SDL3 or >= 2 branch"
	assert_eq "$(grep -c 'SDL2 branch without' <<<"$out")" 2 "$out"
}

# Test that a chain is the run of #if, #elif, #else and #endif at one
# nesting level: a nested chain's SDL3 branch does not cover the outer
# chain's SDL2 branch, and the outer chain's SDL3 branch, after the nested
# chain, covers it.
test_nested_chains_kept_apart()
{
	local r out rc
	r=$(make_lint_repo) || die "make_lint_repo failed"
	printf '%s\n' '#if SDL_MAJOR_VERSION == 2' '#ifdef X' '#if SDL_MAJOR_VERSION == 3' '#endif' '#endif' \
		'#endif' \
		'#if SDL_MAJOR_VERSION == 2' '#ifndef Y' '#endif' '#elif SDL_MAJOR_VERSION == 3' '#endif' > "$r/b.h"
	out=$(python3 "$LINT" "$r" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "b.h:1: SDL2 branch"
	assert_eq "$(grep -c 'SDL2 branch without' <<<"$out")" 1 "$out"
}

# Test that directives in comments are ignored, a continued condition is
# read whole, and third_party/ is not scanned.
test_comments_continuations_third_party()
{
	local r out rc
	r=$(make_lint_repo) || die "make_lint_repo failed"
	mkdir -p "$r/third_party/x"
	printf '%s\n' '/*' '#if SDL_MAJOR_VERSION == 2' '#endif' '*/' \
		"#if X && \\" '	SDL_MAJOR_VERSION == 2 // #elif SDL_MAJOR_VERSION == 3' '#endif' \
		'#if SDL_MAJOR_VERSION == 1' "#elif \\" '	SDL_MAJOR_VERSION == 3' '#endif' \
		'#if SDL_MAJOR_VERSION == 2' '#endif' > "$r/c.mm"
	printf '%s\n' '#if SDL_MAJOR_VERSION == 2' '#endif' > "$r/third_party/x/d.c"
	out=$(python3 "$LINT" "$r" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "c.mm:5: SDL2 branch"
	assert_contains "$out" "c.mm:12: SDL2 branch"
	assert_eq "$(grep -c 'SDL2 branch without' <<<"$out")" 2 "$out"
}

# Test that an allowlist entry lets a file keep that many uncovered chains,
# and that more chains than allowed, an entry without a reason and an entry
# allowing more chains than the file has and a second entry for a file
# each fail, naming the file.
test_allowlist_counts_and_reasons()
{
	local r out rc allow
	r=$(make_lint_repo) || die "make_lint_repo failed"
	allow=$r/tools/dev/sdl-arms-allow
	printf '%s\n' '#if SDL_MAJOR_VERSION == 2' '#endif' '#if SDL_MAJOR_VERSION == 2' '#endif' > "$r/e.cpp"
	printf '%s\n' '# comment' '' 'e.cpp 2 -- ported in #42' > "$allow"
	out=$(python3 "$LINT" "$r" 2>&1)
	assert_eq "$?" 0 "$out"
	assert_eq "$out" ""
	printf '%s\n' 'e.cpp 1 -- ported in #42' > "$allow"
	out=$(python3 "$LINT" "$r" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "e.cpp:1: SDL2 branch"
	assert_contains "$out" "e.cpp:3: SDL2 branch"
	assert_contains "$out" "e.cpp: 2 uncovered chains, the allowlist allows 1"
	printf '%s\n' 'e.cpp 2' > "$allow"
	out=$(python3 "$LINT" "$r" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "sdl-arms-allow:1: entry needs <path> <count> -- <reason>"
	printf '%s\n' 'e.cpp 3 -- ported in #42' 'gone.cpp 1 -- ported in #43' > "$allow"
	out=$(python3 "$LINT" "$r" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "sdl-arms-allow:1: e.cpp has 2 uncovered chains, lower the count"
	assert_contains "$out" "sdl-arms-allow:2: gone.cpp has 0 uncovered chains, lower the count"
	printf '%s\n' 'e.cpp 2 -- ported in #42' 'e.cpp 2 -- ported in #43' > "$allow"
	out=$(python3 "$LINT" "$r" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "sdl-arms-allow:2: second entry for e.cpp, first at line 1"
}

# Test that the allowlist can be named on the command line.
test_allowlist_argument()
{
	local r out d
	r=$(make_lint_repo) || die "make_lint_repo failed"
	d=$(make_scratch) || die "make_scratch failed"
	printf '%s\n' '#if SDL_MAJOR_VERSION == 2' '#endif' > "$r/f.cpp"
	printf '%s\n' 'f.cpp 1 -- ported in #42' > "$d/allow"
	out=$(python3 "$LINT" "$r" "$d/allow" 2>&1)
	assert_eq "$?" 0 "$out"
}

run_tests
