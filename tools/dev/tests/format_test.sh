#!/usr/bin/env bash
# Command-line behaviour of tools/dev/format.sh and the scope of the
# clang-format configuration: fork directories only, never upstream files.
source "$(dirname "$0")/lib.sh"
FORMAT="$DEV_DIR/format.sh"

# Test that a file in the fork style passes --check.
test_fork_style_file_passes()
{
	local d out
	d=$(make_scratch) || die "make_scratch failed"
	printf '%s\n' \
		'namespace dcx {' \
		'' \
		'[[nodiscard]]' \
		'static int twice(const int &x)' \
		'{' \
		'	if (x > 0)' \
		'		return x * 2;' \
		'	return 0;' \
		'}' \
		'' \
		'}' > "$d/good.cpp"
	out=$("$FORMAT" --check "$d" 2>&1)
	assert_eq "$?" 0 "$out"
}

# Test that a misformatted file fails --check and is named.
test_misformatted_file_fails()
{
	local d out rc
	d=$(make_scratch) || die "make_scratch failed"
	printf 'static int twice(const int& x) {\n    return x*2;\n}\n' > "$d/bad.cpp"
	out=$("$FORMAT" --check "$d" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1
	assert_contains "$out" "bad.cpp"
}

# Test that format.sh without DIRs fails when tools/dev/fork-dirs is
# missing or lists no directory, instead of checking nothing.
test_missing_or_empty_fork_dirs_fails()
{
	local d out rc
	d=$(make_scratch) || die "make_scratch failed"
	cp "$DEV_DIR/format.sh" "$DEV_DIR/env.sh" "$d/"
	out=$("$d/format.sh" --check 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 2 "$out"
	assert_contains "$out" "fork-dirs"
	printf '# only a comment\n\n' > "$d/fork-dirs"
	out=$("$d/format.sh" --check 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 2 "$out"
	assert_contains "$out" "fork-dirs"
}

# Test that clang-format leaves upstream files alone under the root config.
test_root_config_leaves_upstream_unchanged()
{
	local f clang_format
	clang_format=$(dev_llvm_tool clang-format)
	for f in common/unittest/enumerate.cpp similar/main/ai.cpp common/include/3d.h; do
		(cd "$REPO_ROOT" && "$clang_format" "$f" | cmp -s - "$f") || fail "$f would be reformatted"
	done
}

# Test that clang-tidy runs no checks on upstream files under the root config.
test_root_tidy_config_enables_no_checks()
{
	local out tidy
	tidy=$(dev_llvm_tool clang-tidy)
	out=$(cd "$REPO_ROOT" && "$tidy" --list-checks common/unittest/enumerate.cpp -- 2>&1)
	assert_contains "$out" "No checks enabled"
	assert_not_contains "$out" "bugprone-"
	assert_not_contains "$out" "modernize-"
	assert_not_contains "$out" "clang-analyzer-"
}

# Test that --staged checks the staged C++ files in fork directories only:
# a misformatted staged fork file fails and is named; the same file staged
# in an upstream directory, or unstaged in a fork directory, is ignored.
test_staged_checks_staged_fork_files_only()
{
	local r out rc
	r=$(fork_repo) || die "fork_repo failed"
	printf 'static int twice(const int& x) {\n    return x*2;\n}\n' > "$r/common/upstream.cpp"
	cp "$r/common/upstream.cpp" "$r/src/unstaged.cpp"
	tgit -C "$r" add common/upstream.cpp
	out=$("$r/tools/dev/format.sh" --staged 2>&1)
	assert_eq "$?" 0 "$out"
	cp "$r/common/upstream.cpp" "$r/src/bad.cpp"
	tgit -C "$r" add src/bad.cpp
	out=$("$r/tools/dev/format.sh" --staged 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "src/bad.cpp"
	assert_not_contains "$out" "unstaged.cpp"
	assert_not_contains "$out" "upstream.cpp"
}

# Test that a NOLINT needs a check list and a reason: a bare NOLINT and one
# without a reason fail and are named by line; NOLINT(check): reason passes.
test_nolint_needs_check_and_reason()
{
	local r out rc
	r=$(fork_repo) || die "fork_repo failed"
	printf '%s\n' \
		'int one(); // NOLINT' \
		'// NOLINTNEXTLINE(bugprone-branch-clone)' \
		'int two();' > "$r/src/bare.cpp"
	tgit -C "$r" add src/bare.cpp
	out=$("$r/tools/dev/format.sh" --staged 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "src/bare.cpp:1: NOLINT without a check and a reason"
	assert_contains "$out" "src/bare.cpp:2: NOLINT without a check and a reason"
	r=$(fork_repo) || die "fork_repo failed"
	printf '%s\n' \
		'// NOLINTNEXTLINE(bugprone-branch-clone): both branches mirror the SDL2 path' \
		'int two();' > "$r/src/justified.cpp"
	tgit -C "$r" add src/justified.cpp
	out=$("$r/tools/dev/format.sh" --staged 2>&1)
	assert_eq "$?" 0 "$out"
}

# Test that a fork directory without the fork clang-tidy config, or with a
# stale copy, fails: clangd and clang-tidy read the config from there.
# --staged reads the configs from the index: an unstaged edit changes
# nothing, a staged one fails.
test_fork_dir_needs_current_tidy_config()
{
	local r out rc
	r=$(fork_repo) || die "fork_repo failed"
	printf 'Checks: -*\n' > "$r/src/.clang-tidy"
	out=$("$r/tools/dev/format.sh" --staged 2>&1)
	assert_eq "$?" 0 "unstaged edit: $out"
	tgit -C "$r" add src/.clang-tidy
	out=$("$r/tools/dev/format.sh" --staged 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "src/.clang-tidy"
	tgit -C "$r" rm -q --cached src/.clang-tidy
	out=$("$r/tools/dev/format.sh" --staged 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "src/.clang-tidy"
}

# Test that --tidy on every fork file also checks the fork directories'
# clang-tidy config and NOLINT reasons, staged or not.
test_tidy_checks_config_and_nolint()
{
	local r out rc
	r=$(fork_repo) || die "fork_repo failed"
	printf 'int probe(void); // NOLINT\n' > "$r/src/probe.c"
	out=$("$r/tools/dev/format.sh" --tidy 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "src/probe.c:1: NOLINT without a check and a reason"
	assert_not_contains "$out" ".clang-tidy"
	rm "$r/src/probe.c" "$r/src/.clang-tidy"
	out=$("$r/tools/dev/format.sh" --tidy 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "src/.clang-tidy: not a copy"
	cp "$r/tools/dev/style/clang-tidy" "$r/src/.clang-tidy"
	out=$("$r/tools/dev/format.sh" --tidy 2>&1)
	assert_eq "$?" 0 "$out"
}

# Test that C files get clang-format but not the C++ clang-tidy rules, so a
# C file needs no compilation database; a C++ file without one fails.
test_staged_c_file_skips_tidy()
{
	local r out rc
	r=$(fork_repo) || die "fork_repo failed"
	rm "$r/build/dev/compile_commands.json"
	printf 'int probe(void)\n{\n\treturn 0;\n}\n' > "$r/src/probe.c"
	tgit -C "$r" add src/probe.c
	out=$("$r/tools/dev/format.sh" --staged 2>&1)
	assert_eq "$?" 0 "$out"
	printf 'int probe()\n{\n\treturn 0;\n}\n' > "$r/src/probe.cpp"
	tgit -C "$r" add src/probe.cpp
	out=$("$r/tools/dev/format.sh" --staged 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "compile_commands.json"
}

# Test that a staged fork header is tidied with the fork's own flags, not
# a compilation database entry it does not have: a finding fails it.
test_staged_header_is_tidied_without_database_entry()
{
	local r out rc
	r=$(fork_repo) || die "fork_repo failed"
	printf '%s\n' '#pragma once' 'inline int *null_pointer() { return 0; }' > "$r/src/plant.h"
	tgit -C "$r" add src/plant.h
	rm "$r/build/dev/compile_commands.json"
	out=$("$r/tools/dev/format.sh" --staged 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "modernize-use-nullptr"
	assert_not_contains "$out" "no $r/build/dev/compile_commands.json"
	assert_not_contains "$out" "pragma-once"
}

# Test that --staged checks what is staged, not the working tree: a
# NOLINT without a reason and a clang-tidy finding, staged and then fixed
# in the working copy only, still fail, named by their repository paths;
# and a staged fix of a broken working copy passes.
test_staged_reads_the_index_not_the_working_tree()
{
	local r out rc
	r=$(fork_repo) || die "fork_repo failed"
	printf '%s\n' 'int *null_pointer();' 'int *null_pointer() { return 0; } // NOLINT' \
		> "$r/src/anchor.cpp"
	"$r/tools/dev/format.sh" "$r/src" || die "format failed"
	tgit -C "$r" add src/anchor.cpp
	printf '%s\n' 'int *null_pointer();' 'int *null_pointer()' '{' $'\treturn nullptr;' '}' \
		> "$r/src/anchor.cpp"
	out=$("$r/tools/dev/format.sh" --staged 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "src/anchor.cpp:5: NOLINT without a check and a reason"
	assert_contains "$out" "src/anchor.cpp:4:9: error: use nullptr"
	assert_contains "$out" "modernize-use-nullptr"
	assert_not_contains "$out" "format-staged."
	tgit -C "$r" add src/anchor.cpp
	printf 'int *broken() { return 0; } // NOLINT\n' > "$r/src/anchor.cpp"
	out=$("$r/tools/dev/format.sh" --staged 2>&1)
	assert_eq "$?" 0 "$out"
}

# Test that every C, C++ and Objective-C(++) extension is a fork file:
# misformatted .cc .cxx .hh .hxx .m and .mm files fail --staged and
# --check, a bare NOLINT in an .mm file fails, and an .mm file the
# compilation database lacks is not tidied.
test_every_source_extension_is_checked()
{
	local r out rc e
	r=$(fork_repo) || die "fork_repo failed"
	for e in cc cxx hh hxx m; do
		printf 'int   x_%s;\n' "$e" > "$r/src/f.$e"
	done
	printf '%s\n' 'int *g() { return 0; } // NOLINT' > "$r/src/g.mm"
	tgit -C "$r" add src
	out=$("$r/tools/dev/format.sh" --staged 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	for e in cc cxx hh hxx m; do
		assert_contains "$out" "src/f.$e: not formatted"
	done
	assert_contains "$out" "src/g.mm:1: NOLINT without a check and a reason"
	assert_not_contains "$out" "modernize-use-nullptr"
	out=$("$r/tools/dev/format.sh" --check 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "src/f.m:"
	assert_contains "$out" "src/f.hxx:"
}

run_tests
