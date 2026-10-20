#!/usr/bin/env bash
# Behaviour of the shared test helpers in tools/dev/tests/lib.sh.
# Single quotes hold scripts expanded by the shells they are written for.
# shellcheck disable=SC2016
source "$(dirname "$0")/lib.sh"

# Test that scratch directories and repositories made through command
# substitution, as every test does, are removed when the test file exits.
test_scratch_dirs_are_removed_on_exit()
{
	local paths p
	paths=$(bash -c 'source "$1"; d=$(make_scratch); r=$(make_repo); echo "$d"; echo "$r"' \
		_ "$DEV_DIR/tests/lib.sh")
	[[ -n $paths ]] || fail 'no paths printed'
	for p in $paths; do
		[[ ! -e $p ]] || fail "$p left behind"
	done
}

# Test that a test calling skip is reported as skipped with its reason and
# does not fail the file.
test_skip_is_reported_and_passes()
{
	local d out
	d=$(make_scratch)
	printf '%s\n' "source '$DEV_DIR/tests/lib.sh'" \
		'test_needs_tool() { skip "tool missing"; fail "ran past skip"; }' \
		'run_tests' > "$d/skip_test.sh"
	out=$(bash "$d/skip_test.sh" 2>&1)
	assert_eq "$?" 0 "$out"
	assert_contains "$out" "SKIP test_needs_tool: tool missing"
	assert_not_contains "$out" "ran past skip"
}

# Test that a test file run directly sees only the allowlisted environment.
test_test_file_sees_only_allowlisted_environment()
{
	local d out
	d=$(make_scratch)
	printf '%s\n' "source '$DEV_DIR/tests/lib.sh'" \
		'test_env() { echo "token=${SECRET_TOKEN-unset} home=${HOME:+set}"; }' \
		'run_tests' > "$d/env_test.sh"
	out=$(SECRET_TOKEN=decoy bash "$d/env_test.sh" 2>&1)
	assert_contains "$out" "token=unset home=set"
}

run_tests
