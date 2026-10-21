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

# Test that tgit refuses to act on anything but a scratch repository: an
# empty -C, which git takes as the current directory, or none at all.
test_tgit_refuses_non_scratch_repository()
{
	local out
	out=$(cd "$REPO_ROOT" && tgit -C "" status 2>&1) && fail "tgit -C '' ran: $out"
	assert_contains "$out" "tgit: refusing"
	out=$(tgit status 2>&1) && fail "tgit without -C ran: $out"
	out=$(real_git config user.name intruder 2>&1) && fail "real_git config ran: $out"
	assert_contains "$out" "real_git: refusing"
	# A refusal fails the running test; these three were meant.
	[[ -e $_scratch_root/refused ]] || fail "a refusal did not mark the test failed"
	rm -f "$_scratch_root/refused"
	tgit -C "$(make_repo)" status -s || fail "tgit refused a scratch repository"
}

# Print the state of repository $1 that a stray git could change: every
# ref, HEAD and the sha256 of its config.
repo_state()
{
	tgit -C "$1" for-each-ref
	tgit -C "$1" rev-parse HEAD
	dev_sha256 < "$1/.git/config"
}

# Test that a test whose repository path is empty fails and leaves the
# repository it runs in unchanged.  A throwaway repository holding tools/dev
# stands in for this clone: the test file runs from it, and its lib.sh takes
# it as REPO_ROOT.  make_scratch printing nothing is the empty path; one
# test ignores make_repo's status, one stops on it.
test_empty_repo_path_fails_without_touching_repository()
{
	local real out rc before
	real=$(make_repo) || die "make_repo failed"
	mkdir -p "$real/tools/dev/tests"
	cp "$DEV_DIR"/*.sh "$DEV_DIR/toolchain.pins" "$real/tools/dev/"
	cp "$DEV_DIR/tests/lib.sh" "$real/tools/dev/tests/"
	printf '%s\n' 'source "$(dirname "$0")/lib.sh"' \
		'make_scratch() { :; }' \
		'test_ignores_status() { local r; r=$(make_repo)' \
		'	tgit -C "$r" config user.name intruder' \
		'	tgit -C "$r" update-ref -d HEAD' \
		'	tgit -C "$r" commit -q --allow-empty -m intruder' \
		'	real_git update-ref -d HEAD; return 0; }' \
		'test_stops_on_status() { local r; r=$(make_repo) || die "make_repo failed"' \
		'	tgit -C "$r" config user.name intruder; }' \
		'run_tests' > "$real/tools/dev/tests/empty_test.sh"
	before=$(repo_state "$real")
	out=$(cd "$real" && bash tools/dev/tests/empty_test.sh 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "FAIL test_ignores_status"
	assert_contains "$out" "FAIL test_stops_on_status"
	assert_eq "$(repo_state "$real")" "$before"
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
		'test_env() { echo "token=${SECRET_TOKEN-unset} home=${HOME:+set}" \' \
		'	"gh=${DXX_GH_TOKEN-unset} jobs=${DXX_JOBS-unset} sdl=${SDL_MAC_BACKGROUND_APP-unset}"; }' \
		'run_tests' > "$d/env_test.sh"
	out=$(SECRET_TOKEN=decoy DXX_GH_TOKEN=secret DXX_JOBS=3 SDL_MAC_BACKGROUND_APP=1 \
		bash "$d/env_test.sh" 2>&1)
	assert_contains "$out" "token=unset home=set gh=unset jobs=3 sdl=1"
}

run_tests
