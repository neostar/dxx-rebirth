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
	cp -R "$DEV_DIR/hooks" "$r/tools/dev/"
	cp "$DEV_DIR/tests/lib.sh" "$r/tools/dev/tests/"
	git -C "$r" config core.hooksPath tools/dev/hooks
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

# Test that setup.sh installs the hooks in a fresh clone.
test_setup_installs_hooks()
{
	local r
	r=$(make_dev_repo)
	git -C "$r" config --unset core.hooksPath
	(cd "$r" && tools/dev/setup.sh >/dev/null)
	assert_eq "$(git -C "$r" config core.hooksPath)" "tools/dev/hooks"
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

run_tests
