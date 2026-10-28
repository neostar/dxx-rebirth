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

# Test that --tidy fails on a fork C++ file the compilation database has no
# entry for, naming it and the build that writes one, and tidies it once the
# entry is there: clang-tidy would borrow a neighbour's command and pass the
# config's ExtraArgs to the driver as file names, dropping the findings
# they turn on (-Wunsafe-buffer-usage here).
test_tidy_needs_a_database_entry()
{
	local r out rc
	r=$(fork_repo) || die "fork_repo failed"
	printf '%s
' 'int third(const int *p);' 'int third(const int *p)' '{' '	return p[2];' '}' \
		> "$r/src/plant.cpp"
	out=$("$r/tools/dev/format.sh" --tidy 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "src/plant.cpp: no entry in build/dev/compile_commands.json"
	assert_contains "$out" "run tools/dev/build.sh dev"
	assert_contains "$out" "belongs in tools/dev/sdl3-only"
	assert_not_contains "$out" "no such file or directory"
	add_database_entry "$r" src/plant.cpp
	out=$("$r/tools/dev/format.sh" --tidy 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "src/plant.cpp:4:9: error: unsafe buffer access"
	assert_not_contains "$out" "no entry in"
}

# Test that --tidy tidies an SDL3-only source (tools/dev/sdl3-only), which
# build/dev's database lacks, with build/dev-sdl3's, and SKIPs it, naming
# it and the build that writes the database, when there is none.
test_tidy_takes_sdl3_only_sources_from_the_sdl3_database()
{
	local r out rc
	r=$(fork_repo) || die "fork_repo failed"
	printf '%s\n' 'int third(const int *p);' 'int third(const int *p)' '{' '	return p[2];' '}' \
		> "$r/src/gpu.cpp"
	echo src/gpu.cpp > "$r/tools/dev/sdl3-only"
	out=$("$r/tools/dev/format.sh" --tidy 2>&1)
	assert_eq "$?" 0 "$out"
	assert_contains "$out" "SKIP tidy src/gpu.cpp: SDL3-only, and no build/dev-sdl3/compile_commands.json"
	assert_not_contains "$out" "no entry in"
	mkdir -p "$r/build/dev-sdl3"
	cp "$r/build/dev/compile_commands.json" "$r/build/dev-sdl3/compile_commands.json"
	out=$("$r/tools/dev/format.sh" --tidy 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "src/gpu.cpp: no entry in build/dev-sdl3/compile_commands.json"
	python3 - "$r/build/dev-sdl3/compile_commands.json" <<'PY'
import json, sys
db = json.load(open(sys.argv[1]))
db.append(dict(db[0], file="src/gpu.cpp", arguments=db[0]["arguments"][:-1] + ["src/gpu.cpp"]))
json.dump(db, open(sys.argv[1], "w"))
PY
	out=$("$r/tools/dev/format.sh" --tidy 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "src/gpu.cpp:4:9: error: unsafe buffer access"
}

# Test that --tidy runs the files in parallel without losing a failure or
# the order: with one job and with four, the bad file is the middle or the
# last of the files, the largest file (started first) is a good one, and the
# findings print in the order the files were given.
test_tidy_runs_files_in_parallel_in_a_stable_order()
{
	local r out rc jobs f
	r=$(fork_repo) || die "fork_repo failed"
	for f in a b c; do
		printf '%s\n' "int f_$f(const int *p);" "int f_$f(const int *p)" '{' '	return p[2];' '}' \
			> "$r/src/$f.cpp"
		add_database_entry "$r" "src/$f.cpp"
	done
	printf '%s\n' "int big;" '// padding padding padding padding padding padding padding' \
		'// padding padding padding padding padding padding padding' > "$r/src/d.cpp"
	add_database_entry "$r" src/d.cpp
	for jobs in 1 4; do
		out=$(DXX_JOBS=$jobs "$r/tools/dev/format.sh" --tidy "$r/src/a.cpp" "$r/src/d.cpp" \
			"$r/src/c.cpp" 2>&1) && rc=0 || rc=$?
		assert_eq "$rc" 1 "$out (jobs $jobs)"
		assert_contains "$out" "src/a.cpp:4:9: error: unsafe buffer access"
		assert_contains "$out" "src/c.cpp:4:9: error: unsafe buffer access"
		assert_eq "$(grep -n 'error: unsafe buffer' <<<"$out" | head -1 | grep -c 'src/a.cpp')" 1 "a prints first (jobs $jobs)"
		out=$(DXX_JOBS=$jobs "$r/tools/dev/format.sh" --tidy "$r/src/d.cpp" "$r/src/d.cpp" \
			"$r/src/b.cpp" 2>&1) && rc=0 || rc=$?
		assert_eq "$rc" 1 "$out (last file bad, jobs $jobs)"
		assert_contains "$out" "src/b.cpp:4:9: error: unsafe buffer access"
	done
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

# Test that a staged fork header that includes another fork header by its
# path from the top, as conventions.md requires, finds it.
test_staged_header_includes_by_path_from_top()
{
	local r out
	r=$(fork_repo) || die "fork_repo failed"
	mkdir -p "$r/src/sub"
	printf '%s\n' '#pragma once' 'inline int one() { return 1; }' > "$r/src/sub/one.h"
	printf '%s\n' '#pragma once' '#include "src/sub/one.h"' 'inline int two() { return one() + 1; }' \
		> "$r/src/sub/two.h"
	"$r/tools/dev/format.sh" "$r/src" || die "format failed"
	tgit -C "$r" add src/sub/one.h src/sub/two.h
	out=$("$r/tools/dev/format.sh" --staged 2>&1)
	assert_eq "$?" 0 "$out"
}

# Test that a staged fork header is tidied against the pinned SDL3 the way
# sdl3=1 builds it (ADR 3): <SDL.h> found in the SDL3 include directory and
# the SDL2 names compiled through SDL_ENABLE_OLD_NAMES.
test_staged_header_sees_sdl3_as_sdl3_builds()
{
	local r out
	[[ -e $dev_deps_prefix/include/SDL3/SDL.h ]] || skip "no pinned SDL3 headers in $dev_deps_prefix; run deps.sh"
	r=$(fork_repo) || die "fork_repo failed"
	printf '%s\n' '#pragma once' '#include <SDL.h>' \
		'inline bool is_stream(const SDL_RWops *ops) { return ops != nullptr; }' > "$r/src/stream.h"
	"$r/tools/dev/format.sh" "$r/src" || die "format failed"
	tgit -C "$r" add src/stream.h
	out=$("$r/tools/dev/format.sh" --staged 2>&1)
	assert_eq "$?" 0 "$out"
}

# Test that a staged fork header finds <SDL_image.h> and <SDL_mixer.h> in
# the SDL3_image and SDL3_mixer include directories, as sdl3=1 builds do.
test_staged_header_sees_sdl3_image_and_mixer()
{
	local r out
	[[ -e $dev_deps_prefix/include/SDL3_mixer/SDL_mixer.h ]] ||
		skip "no pinned SDL3_image and SDL3_mixer headers in $dev_deps_prefix; run deps.sh"
	r=$(fork_repo) || die "fork_repo failed"
	printf '%s\n' '#pragma once' '#include <SDL_image.h>' '#include <SDL_mixer.h>' \
		'inline int versions() { return IMG_Version() + MIX_Version(); }' > "$r/src/versions.h"
	"$r/tools/dev/format.sh" "$r/src" || die "format failed"
	tgit -C "$r" add src/versions.h
	out=$("$r/tools/dev/format.sh" --staged 2>&1)
	assert_eq "$?" 0 "$out"
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
