#!/usr/bin/env bash
# Command-line behaviour of tools/dev/check.sh and the pre-commit hook,
# run against a throwaway repository holding a copy of tools/dev.
source "$(dirname "$0")/lib.sh"

# Make a throwaway repo with tools/dev copied in (no tests, so check.sh
# does not recurse into this file) and the fork's hooks installed.
make_dev_repo()
{
	local r
	r=$(make_repo)
	mkdir -p "$r/tools/dev/tests"
	cp "$DEV_DIR"/*.sh "$r/tools/dev/"
	cp "$DEV_DIR/fork-dirs" "$DEV_DIR/repo-config" "$r/tools/dev/"
	cp -R "$DEV_DIR/hooks" "$DEV_DIR/style" "$r/tools/dev/"
	cp "$DEV_DIR/tests/lib.sh" "$r/tools/dev/tests/"
	(cd "$r" && tools/dev/setup.sh >/dev/null)
	printf '%s\n' "$r"
}

# Test that the fast subset passes on a clean index and names its steps.
test_fast_passes_clean_index()
{
	local r out
	r=$(make_dev_repo)
	printf 'int x;\n' > "$r/a.cpp"
	git -C "$r" add a.cpp
	out=$("$r/tools/dev/check.sh" --fast 2>&1)
	assert_eq "$?" 0 "$out"
	assert_contains "$out" "guard"
}

# Test that the fast subset fails when licensed data is staged.
test_fast_fails_on_staged_game_data()
{
	local r out rc
	r=$(make_dev_repo)
	printf 'x' > "$r/descent2.hog"
	git -C "$r" add descent2.hog
	out=$(cd "$r" && tools/dev/check.sh --fast 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1
	assert_contains "$out" "descent2.hog"
}

# Test that the fast subset runs every *_test.sh and fails if one fails.
test_fast_runs_shell_tests()
{
	local r out rc
	r=$(make_dev_repo)
	printf '#!/usr/bin/env bash\necho ran-passing\n' > "$r/tools/dev/tests/pass_test.sh"
	printf '#!/usr/bin/env bash\necho ran-failing; exit 3\n' > "$r/tools/dev/tests/zfail_test.sh"
	out=$("$r/tools/dev/check.sh" --fast 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1
	assert_contains "$out" "ran-passing"
	assert_contains "$out" "ran-failing"
	assert_contains "$out" "zfail_test.sh"
}

# Test that each shell test runs under no-window.sh: a focus change while
# one runs fails the check.
test_fast_runs_shell_tests_windowless()
{
	local r d out rc
	r=$(make_dev_repo)
	d=$(make_scratch); mkdir "$d/reports"
	printf '#!/usr/bin/env bash
sleep 1
' > "$r/tools/dev/tests/focus_test.sh"
	out=$(NO_WINDOW_FRONTMOST_CMD=$(fake_sampler "$d" Terminal intruder) NO_WINDOW_REPORTS_DIR="$d/reports" \
		"$r/tools/dev/check.sh" --fast 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "intruder"
	assert_contains "$out" "FAILED: tests/focus_test.sh"
}

# Test that the fast subset runs the format check on the fork directories.
test_fast_fails_on_misformatted_fork_code()
{
	local r out rc
	r=$(make_dev_repo)
	printf 'static int twice(const int& x) {\n    return x*2;\n}\n' > "$r/tools/bad.cpp"
	out=$(cd "$r" && tools/dev/check.sh --fast 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "bad.cpp"
	assert_contains "$out" "failed: format"
}

# Test that an unknown option is a usage error with exit status 2.
test_unknown_option_is_usage_error()
{
	local r out rc
	r=$(make_dev_repo)
	out=$("$r/tools/dev/check.sh" --bogus 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 2
	assert_contains "$out" "usage"
}

# Test that the pre-commit hook refuses a commit with game data and
# allows one without it.
test_pre_commit_hook_blocks_game_data()
{
	local r out rc
	r=$(make_dev_repo)
	printf 'DHF\0\0\0\0' > "$r/renamed.bin"
	git -C "$r" add renamed.bin
	out=$(tgit -C "$r" commit -q -m data 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "renamed.bin"
	git -C "$r" rm -q --cached renamed.bin
	printf 'int y;\n' > "$r/b.cpp"
	git -C "$r" add b.cpp
	out=$(tgit -C "$r" commit -q -m code 2>&1)
	assert_eq "$?" 0 "$out"
}

# Test that setup.sh installs the hooks and enables rerere in a fresh clone.
test_setup_installs_hooks()
{
	local r
	r=$(make_dev_repo)
	git -C "$r" config --unset core.hooksPath
	(cd "$r" && tools/dev/setup.sh >/dev/null)
	assert_eq "$(git -C "$r" config core.hooksPath)" "tools/dev/hooks"
	assert_eq "$(git -C "$r" config rerere.enabled)" "true"
}

# Test that the fast subset fails, naming the key, when the repository's
# local config deviates from what setup.sh records.
test_fast_fails_on_repo_config_deviation()
{
	local r out rc
	r=$(make_dev_repo)
	git -C "$r" config user.name intruder
	git -C "$r" config commit.gpgsign false
	git -C "$r" config --unset rerere.enabled
	out=$("$r/tools/dev/check.sh" --fast 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "user.name"
	assert_contains "$out" "commit.gpgsign"
	assert_contains "$out" "rerere.enabled"
	assert_contains "$out" "failed: repo_config"
}

# Test that a worktree's check reads the shared config of its main clone.
test_fast_checks_repo_config_from_worktree()
{
	local r wt out rc
	r=$(make_dev_repo)
	tgit -C "$r" add -A && tgit -C "$r" commit -q -m tools >/dev/null 2>&1
	wt=$(make_scratch)/wt
	git -C "$r" worktree add -q -b side "$wt"
	out=$("$wt/tools/dev/check.sh" --fast 2>&1)
	assert_eq "$?" 0 "$out"
	git -C "$r" config user.email intruder@example.invalid
	out=$(cd "$wt" && tools/dev/check.sh --fast 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "user.email"
}

# Test that the shell tests and check.sh, run with GIT_DIR and
# GIT_INDEX_FILE exported to a decoy repository as inside a git hook, leave
# the decoy's config and HEAD unchanged.
test_hook_environment_does_not_leak_into_decoy_repo()
{
	local decoy r config_before head_before
	decoy=$(make_repo)
	r=$(make_dev_repo)
	config_before=$(cat "$decoy/.git/config")
	head_before=$(git -C "$decoy" rev-parse HEAD)
	GIT_DIR="$decoy/.git" GIT_INDEX_FILE="$decoy/.git/index" \
		bash "$DEV_DIR/tests/guard_test.sh" >/dev/null 2>&1
	GIT_DIR="$decoy/.git" GIT_INDEX_FILE="$decoy/.git/index" \
		bash -c 'source "$1"; make_repo >/dev/null' _ "$DEV_DIR/tests/lib.sh"
	(cd "$r" && GIT_DIR="$decoy/.git" GIT_INDEX_FILE="$decoy/.git/index" \
		tools/dev/check.sh --fast >/dev/null 2>&1)
	assert_eq "$(cat "$decoy/.git/config")" "$config_before"
	assert_eq "$(git -C "$decoy" rev-parse HEAD)" "$head_before"
}

# Test that the shell tests run without any of git's repository variables,
# whether or not the test sources lib.sh.
test_shell_tests_see_no_git_repository_variables()
{
	local r out
	r=$(make_dev_repo)
	printf '#!/usr/bin/env bash\necho "ns=${GIT_NAMESPACE-unset} alt=${GIT_ALTERNATE_OBJECT_DIRECTORIES-unset}"\n' \
		> "$r/tools/dev/tests/env_test.sh"
	out=$(cd "$r" && GIT_NAMESPACE=decoy GIT_ALTERNATE_OBJECT_DIRECTORIES=/nonexistent \
		tools/dev/check.sh --fast 2>&1)
	assert_contains "$out" "ns=unset alt=unset"
}

# Print the ASAN_OPTIONS a shell test sees under check.sh when uname -s
# reports $1.
asan_options_on()
{
	local r bin
	r=$(make_dev_repo)
	bin=$(make_scratch)
	printf '#!/bin/sh\necho %s\n' "$1" > "$bin/uname"; chmod +x "$bin/uname"
	printf '#!/usr/bin/env bash\necho "asan=$ASAN_OPTIONS"\n' > "$r/tools/dev/tests/env_test.sh"
	(cd "$r" && env -u ASAN_OPTIONS PATH="$bin:$PATH" tools/dev/check.sh --fast 2>&1) | grep '^asan='
}

# Test that LeakSanitizer stays on where it works and is off on macOS.
test_leak_detection_on_except_macos()
{
	assert_contains "$(asan_options_on Linux)" "detect_leaks=1"
	assert_contains "$(asan_options_on Darwin)" "detect_leaks=0"
	assert_contains "$(asan_options_on Linux)" "abort_on_error=0"
}

run_tests
