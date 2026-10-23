#!/usr/bin/env bash
# The exclusion list of tools/dev/leaks.sh: a leak allocated under a symbol
# listed for the program passes, any other leak fails, a symbol the
# program defines or a malformed entry is a usage error, and an entry that
# excludes nothing is STALE.  macOS only, as leaks is.
source "$(dirname "$0")/lib.sh"
LEAKS="$DEV_DIR/leaks.sh"

# Build, in a new scratch directory, the program `leak`, which leaks one
# block through strdup (a system library's frame) when its first argument
# holds `s`, one from its own plant_leak when it holds `p` and one from its
# own C++ plant::cxx_leak() when it holds `c`; print its path.
build_leaker()
{
	local d
	d=$(make_scratch) || die "make_scratch failed"
	cat > "$d/leak.cpp" <<'EOF'
#include <cstdlib>
#include <cstring>
extern "C" [[gnu::noinline]] void *plant_leak()
{
	return std::malloc(64);
}
namespace plant {
[[gnu::noinline]] void *cxx_leak()
{
	return std::malloc(32);
}
}
[[gnu::noinline]] static void leak_cxx()
{
	void *volatile p{plant::cxx_leak()};
	p = nullptr;
}
[[gnu::noinline]] static void leak_strdup()
{
	void *volatile p{strdup("leaked")};
	p = nullptr;
}
[[gnu::noinline]] static void leak_plant()
{
	void *volatile p{plant_leak()};
	p = nullptr;
}
/* Overwrites the stack the leaks left their pointers on. */
[[gnu::noinline]] static void scrub()
{
	volatile char stack[8192];
	for (auto &c : stack)
		c = 0;
}
int main(int argc, char **argv)
{
	const char *what{argc > 1 ? argv[1] : ""};
	if (std::strchr(what, 's'))
		leak_strdup();
	if (std::strchr(what, 'p'))
		leak_plant();
	if (std::strchr(what, 'c'))
		leak_cxx();
	scrub();
	return 0;
}
EOF
	g++ -std=c++23 -g -O0 "$d/leak.cpp" -o "$d/leak" >&2 || return
	printf '%s\n' "$d/leak"
}

# Run leaks.sh with exclusion list $1 on program $2 and its arguments, its
# output and status passed on.
leaks_with()
{
	LEAKS_EXCLUDE=$1 "$DEV_DIR/no-window.sh" "$LEAKS" "${@:2}" 2>&1
}

need_leaks() { [[ $(uname -s) == Darwin ]] || skip "leaks is macOS only"; }

# Test that a leak allocated under a symbol listed for the program passes,
# saying so, and that the same leak fails with the entry scoped to another
# program.
test_listed_symbol_excludes_its_leak()
{
	need_leaks
	local p d out rc
	p=$(build_leaker) || die "build failed"
	d=$(make_scratch) || die "make_scratch failed"
	printf '# a comment\n\nleak strdup -- the test leak\n' > "$d/listed"
	out=$(leaks_with "$d/listed" "$p" s) && rc=0 || rc=$?
	assert_eq "$rc" 0 "$out"
	assert_contains "$out" "All excluded (listed)."
	printf 'other strdup -- another program\n' > "$d/other"
	out=$(leaks_with "$d/other" "$p" s) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "leak: 1 leak for"
}

# Test that a run with one excluded leak and one other leak fails.
test_mixed_leaks_fail()
{
	need_leaks
	local p d out rc
	p=$(build_leaker) || die "build failed"
	d=$(make_scratch) || die "make_scratch failed"
	printf 'leak strdup -- the test leak\n' > "$d/listed"
	out=$(leaks_with "$d/listed" "$p" sp) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "leak: 2 leaks for"
}

# Canary: the real tools/dev/leaks-exclude excuses no leak of a program it
# does not name.
test_default_list_excuses_no_other_program()
{
	need_leaks
	local p out rc
	p=$(build_leaker) || die "build failed"
	out=$("$DEV_DIR/no-window.sh" "$LEAKS" "$p" sp 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "leak: 2 leaks for"
}

# Test that an entry naming a symbol the program defines is a usage error:
# it would hide the program's own leaks.
test_own_symbol_is_a_usage_error()
{
	need_leaks
	local p d out rc
	p=$(build_leaker) || die "build failed"
	d=$(make_scratch) || die "make_scratch failed"
	printf 'leak plant_leak -- would hide our own leak\n' > "$d/own"
	out=$(leaks_with "$d/own" "$p" p) && rc=0 || rc=$?
	assert_eq "$rc" 2 "$out"
	assert_contains "$out" "leak defines plant_leak"
}

# Test that an entry naming a C++ symbol the program defines, as leaks
# prints its frames (demangled), is a usage error too.
test_own_cxx_symbol_is_a_usage_error()
{
	need_leaks
	local p d out rc
	p=$(build_leaker) || die "build failed"
	d=$(make_scratch) || die "make_scratch failed"
	printf 'leak plant::cxx_leak() -- would hide our own leak\n' > "$d/own"
	out=$(leaks_with "$d/own" "$p" c) && rc=0 || rc=$?
	assert_eq "$rc" 2 "$out"
	assert_contains "$out" "leak defines plant::cxx_leak()"
}

# Test that nm failing on the program, here a shell script, is a usage
# error rather than a check passed.
test_unreadable_symbols_are_a_usage_error()
{
	need_leaks
	local d out rc
	d=$(make_scratch) || die "make_scratch failed"
	printf '#!/bin/sh\nexit 0\n' > "$d/leak"
	chmod +x "$d/leak"
	printf 'leak strdup -- the test leak\n' > "$d/listed"
	out=$(leaks_with "$d/listed" "$d/leak") && rc=0 || rc=$?
	assert_eq "$rc" 2 "$out"
	assert_contains "$out" "nm cannot list the symbols of"
}

# Test that an entry for a program whose run excluded no leak is STALE.
test_entry_excluding_nothing_is_stale()
{
	need_leaks
	local p d out rc
	p=$(build_leaker) || die "build failed"
	d=$(make_scratch) || die "make_scratch failed"
	printf 'leak strdup -- the test leak\n' > "$d/listed"
	out=$(leaks_with "$d/listed" "$p") && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "STALE leaks-exclude: leak: its run excluded no leak"
}

# Test that an entry without a program, a symbol or a reason, and a
# missing list, are usage errors, before the program runs.
test_bad_list_is_a_usage_error()
{
	need_leaks
	local d out rc
	d=$(make_scratch) || die "make_scratch failed"
	printf 'true strdup\n' > "$d/bare"
	out=$(leaks_with "$d/bare" /usr/bin/true) && rc=0 || rc=$?
	assert_eq "$rc" 2 "$out"
	assert_contains "$out" "an entry without a program, a symbol or a reason: true strdup"
	printf 'strdup -- no symbol\n' > "$d/nosymbol"
	out=$(leaks_with "$d/nosymbol" /usr/bin/true) && rc=0 || rc=$?
	assert_eq "$rc" 2 "$out"
	out=$(leaks_with "$d/missing" /usr/bin/true) && rc=0 || rc=$?
	assert_eq "$rc" 2 "$out"
	assert_contains "$out" "cannot read the exclusion list"
}

run_tests --parallel
