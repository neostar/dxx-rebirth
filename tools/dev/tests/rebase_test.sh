#!/usr/bin/env bash
# Command-line behaviour of tools/dev/rebase.sh against a throwaway repo
# with an "upstream" branch and a fork branch on top of an older upstream.
source "$(dirname "$0")/lib.sh"
REBASE="$DEV_DIR/rebase.sh"

# Make a repo where fork has two commits on base and up has one new commit.
make_fork_repo()
{
	local r
	r=$(make_repo)
	git -C "$r" branch up
	git -C "$r" checkout -q -b fork
	printf 'a\n' > "$r/a.txt"; git -C "$r" add a.txt; tgit -C "$r" commit -q -m 'fork: add a'
	printf 'b\n' > "$r/b.txt"; git -C "$r" add b.txt; tgit -C "$r" commit -q -m 'fork: add b'
	git -C "$r" checkout -q up
	printf 'u\n' > "$r/u.txt"; git -C "$r" add u.txt; tgit -C "$r" commit -q -m 'upstream: add u'
	git -C "$r" checkout -q fork
	printf '%s\n' "$r"
}

# Test that the branch is rebased, the command runs once per commit and the
# range-diff shows both fork commits as unchanged.
test_rebases_runs_exec_per_commit_and_prints_range_diff()
{
	local r out log
	r=$(make_fork_repo)
	log=$(make_scratch)/exec.log
	out=$(cd "$r" && "$REBASE" --upstream up --exec "echo run >> $log" 2>&1)
	assert_eq "$?" 0 "$out"
	assert_eq "$(wc -l < "$log" | tr -d ' ')" 2
	assert_eq "$(git -C "$r" log --format=%s up..fork | tr '\n' '|')" 'fork: add b|fork: add a|'
	git -C "$r" merge-base --is-ancestor up fork || fail 'fork is not on top of up'
	assert_contains "$out" "fork: add a"
	assert_contains "$out" " = "
}

# Test that a failing per-commit command stops the rebase with an error.
test_failing_exec_stops_with_error()
{
	local r out rc
	r=$(make_fork_repo)
	out=$(cd "$r" && "$REBASE" --upstream up --exec 'false' 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1
	assert_contains "$out" "rebase stopped"
	git -C "$r" rebase --abort
}

# Test that rerere is enabled for the rebase so resolutions are recorded.
test_enables_rerere()
{
	local r
	r=$(make_fork_repo)
	(cd "$r" && "$REBASE" --upstream up --exec true >/dev/null 2>&1)
	assert_eq "$(git -C "$r" config rerere.enabled)" "true"
}

# Test that the default per-commit command builds and runs the fast checks
# where the commit has tools/dev, and skips visibly where it predates them.
test_default_exec_skips_commits_without_dev_tools()
{
	local r out log
	r=$(make_fork_repo)
	log=$(make_scratch)/exec.log
	git -C "$r" checkout -q fork
	mkdir -p "$r/tools/dev"
	printf '#!/bin/sh\necho build >> %s\n' "$log" > "$r/tools/dev/build.sh"
	printf '#!/bin/sh\necho "check $*" >> %s\n' "$log" > "$r/tools/dev/check.sh"
	chmod +x "$r/tools/dev/build.sh" "$r/tools/dev/check.sh"
	git -C "$r" add tools; tgit -C "$r" commit -q -m 'fork: add tools'
	out=$(cd "$r" && "$REBASE" --upstream up 2>&1)
	assert_eq "$?" 0 "$out"
	assert_eq "$(tr '\n' '|' < "$log")" 'build|check --fast|'
	assert_eq "$(grep -c '^rebase.sh: skip checks at .* fork: add [ab]: no tools' <<<"$out")" 2 "$out"
}

# Test that an option without its value is a usage error.
test_option_without_value_is_usage_error()
{
	local r out rc
	r=$(make_fork_repo)
	out=$(cd "$r" && "$REBASE" --exec 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 2
	assert_contains "$out" "usage"
}

run_tests
