#!/usr/bin/env bash
# Behaviour of the shared test helpers in tools/dev/tests/lib.sh.
# Single quotes hold scripts expanded by the shells they are written for.
# _test_state is set per test, in run_tests' subshell, on purpose.
# shellcheck disable=SC2016,SC2031
source "$(dirname "$0")/lib.sh"

# Test that scratch directories and repositories made through command
# substitution, as every test does, are removed when the test file exits.
test_scratch_dirs_are_removed_on_exit()
{
	local paths p
	# unchecked: a status is what this test does not rely on
	paths=$(bash -c 'source "$1"; d=$(make_scratch); r=$(make_repo); echo "$d"; echo "$r"' \
		_ "$DEV_DIR/tests/lib.sh")
	[[ -n $paths ]] || fail 'no paths printed'
	for p in $paths; do
		[[ ! -e $p ]] || fail "$p left behind"
	done
}

# Test that tgit refuses to act on anything but a scratch repository: an
# empty -C, which git takes as the current directory, or none at all, a
# second -C, --git-dir or --work-tree before the subcommand, or GIT_DIR=.
test_tgit_refuses_non_scratch_repository()
{
	local out r a
	out=$(cd "$REPO_ROOT" && tgit -C "" status 2>&1) && fail "tgit -C '' ran: $out"
	assert_contains "$out" "tgit: refusing"
	out=$(tgit status 2>&1) && fail "tgit without -C ran: $out"
	r=$(make_repo) || die "make_repo failed"
	for a in "-C $REPO_ROOT" "--git-dir=$REPO_ROOT/.git" "--git-dir $REPO_ROOT/.git" \
		"--work-tree=$REPO_ROOT" "-c x.y=1 -C $REPO_ROOT"; do
		# shellcheck disable=SC2086  # split into options on purpose
		out=$(tgit -C "$r" $a status 2>&1) && fail "tgit -C $r $a ran: $out"
		assert_contains "$out" "tgit: refusing"
	done
	out=$(tgit -C "$r" commit -m "GIT_DIR=$REPO_ROOT/.git" 2>&1) && fail "GIT_DIR= ran: $out"
	assert_contains "$out" "tgit: refusing 'GIT_DIR="
	out=$(tgit -C "$r" rev-parse --git-dir 2>&1)
	assert_eq "$out" ".git" "a --git-dir after the subcommand is a query"
	out=$(real_git config user.name intruder 2>&1) && fail "real_git config ran: $out"
	assert_contains "$out" "real_git: refusing"
	# A refusal fails the running test; these were meant.
	[[ -e $_test_state/refused ]] || fail "a refusal did not mark the test failed"
	rm -f "$_test_state/refused"
	tgit -C "$(make_repo)" status -s || fail "tgit refused a scratch repository"
}

# Test that tgit refuses every argument reaching outside the scratch root:
# a push, fetch, pull or remote to a repository there (absolute, file://
# or climbing out with ..), and core.worktree or core.hooksPath set by
# config or -c; and that the repository outside is left as it was.  A
# clone from this clone stays allowed.
test_tgit_refuses_paths_outside_scratch()
{
	local r o outside before a
	r=$(make_repo) || die "make_repo failed"
	o=$(make_repo) || die "make_repo failed"
	outside=$(mktemp -d "${TMPDIR:-/tmp}/dxx-outside.XXXXXX") || die "mktemp failed"
	mv "$o" "$outside/o" || die "mv failed"
	o=$outside/o
	# raw git: the state of the repository outside, read only
	before=$(git -C "$o" for-each-ref; cat "$o/.git/config")
	for a in "push $o HEAD:refs/heads/x" "push file://$o HEAD:refs/heads/x" \
		"fetch $o" "pull $o main" "remote add o $o" "-c remote.o.url=$o push o HEAD" \
		"config core.worktree $outside" "-c core.worktree=$outside status" \
		"config core.hooksPath $outside" "-c core.hooksPath=$outside status" \
		"push ../../../../../../../../..$o HEAD:refs/heads/x" "push ssh://h/x HEAD" \
		"-c alias.x=!true x" "config core.hooksPath hooks" "push $REPO_ROOT HEAD:refs/heads/x"; do
		# shellcheck disable=SC2086  # split into arguments on purpose
		tgit -C "$r" $a >/dev/null 2>&1 && fail "tgit ran: $a"
	done
	# raw git: the state of the repository outside, read only
	assert_eq "$(git -C "$o" for-each-ref; cat "$o/.git/config")" "$before"
	rm -rf "$outside"
	[[ -e $_test_state/refused ]] || fail "no refusal marked the test failed"
	rm -f "$_test_state/refused"
	tgit -C "$r" clone -q --shared "$REPO_ROOT" "$r/clone" ||
		fail "a clone of this clone was refused"
	tgit -C "$r" remote add self "$r" || fail "a remote in the scratch root was refused"
	tgit -C "$r" config core.hooksPath >/dev/null; tgit -C "$r" config --unset core.hooksPath
	[[ ! -e $_test_state/refused ]] || fail "reading or unsetting core.hooksPath was refused"
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
	local real out rc before ignores
	real=$(make_repo) || die "make_repo failed"
	# unchecked: the planted test ignores the status on purpose
	ignores='test_ignores_status() { local r; r=$(make_repo)'
	mkdir -p "$real/tools/dev/tests"
	cp "$DEV_DIR"/*.sh "$DEV_DIR/toolchain.pins" "$real/tools/dev/"
	cp "$DEV_DIR/tests/lib.sh" "$real/tools/dev/tests/"
	printf '%s\n' 'source "$(dirname "$0")/lib.sh"' \
		'make_scratch() { :; }' \
		"$ignores" \
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
	d=$(make_scratch) || die "make_scratch failed"
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
	d=$(make_scratch) || die "make_scratch failed"
	printf '%s\n' "source '$DEV_DIR/tests/lib.sh'" \
		'test_env() { echo "token=${SECRET_TOKEN-unset} home=${HOME:+set} gh=${DXX_GH_TOKEN-unset}"' \
		'	echo "jobs=${DXX_JOBS-unset} sdl=${SDL_MAC_BACKGROUND_APP-unset}"; }' \
		'run_tests' > "$d/env_test.sh"
	out=$(SECRET_TOKEN=decoy DXX_GH_TOKEN=secret DXX_JOBS=3 SDL_MAC_BACKGROUND_APP=1 \
		bash "$d/env_test.sh" 2>&1)
	assert_contains "$out" "token=unset home=set gh=unset"$'\n'"jobs=3 sdl=1"
}

# Test that git finds no repository above the scratch root, even with the
# scratch root inside a clone: a script under test that runs git on its
# current directory, as rebase.sh does, acts on nothing.
test_git_finds_no_repository_above_scratch_root()
{
	local r out script
	r=$(make_repo) || die "make_repo failed"
	mkdir "$r/tmp"
	# raw git: what the ceiling must stop
	script='source "$1"; git rev-parse --git-dir; cd "$(make_scratch)" && git rev-parse --git-dir'
	out=$(TMPDIR="$r/tmp" bash -c "$script" _ "$DEV_DIR/tests/lib.sh" 2>&1)
	assert_not_contains "$out" "$r"
	assert_not_contains "$out" "$(cd "$r" && pwd -P)"
	# raw git: git's own message
	assert_contains "$out" "fatal: not a git repository"
}

# Print what sourcing env.sh with DXX_JOBS=$1 prints, and its status.
source_env_with_jobs()
{
	DXX_JOBS=$1 bash -c 'source "$1"; echo "jobs=$dev_jobs"' _ "$DEV_DIR/env.sh" 2>&1
	echo "rc=$?"
}

# Test that DXX_JOBS must be a positive integer: 0 or a word would make
# check.sh wait forever for a free job slot.
test_jobs_must_be_positive_integer()
{
	local out
	out=$(source_env_with_jobs 0)
	assert_contains "$out" "DXX_JOBS must be a positive integer, got '0'"
	assert_contains "$out" "rc=2"
	assert_not_contains "$out" "jobs=0"
	out=$(source_env_with_jobs many)
	assert_contains "$out" "DXX_JOBS must be a positive integer, got 'many'"
	assert_contains "$out" "rc=2"
	assert_contains "$(source_env_with_jobs 12)" "jobs=12"
}

run_tests
