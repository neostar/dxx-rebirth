#!/usr/bin/env bash
# Command-line behaviour of tools/dev/wait-for.sh, which waits until a file
# holds a line containing one of several fixed strings, up to a deadline.
source "$(dirname "$0")/lib.sh"
WAIT_FOR="$DEV_DIR/wait-for.sh"

# Run wait-for.sh with the arguments, its output and status passed on; if
# it still runs 30 s after its own deadline would have ended it, kill it
# and fail the test: a broken deadline fails, it does not hang the tests.
waited()
{
	local pid i rc captured=$_scratch_root/waited.$BASHPID
	"$WAIT_FOR" "$@" > "$captured" 2>&1 &
	pid=$!
	for ((i = 0; i < 300; i++)); do
		kill -0 "$pid" 2>/dev/null || break
		sleep 0.1
	done
	if kill -0 "$pid" 2>/dev/null; then
		kill "$pid"
		wait "$pid"
		cat "$captured"
		fail "wait-for.sh ignored its deadline: $*"
		return 124
	fi
	wait "$pid" && rc=0 || rc=$?
	cat "$captured"
	return "$rc"
}

# Test that a line written after the start ends the wait with status 0,
# well before the deadline, printing the matching line.
test_match_ends_the_wait()
{
	local d out
	d=$(make_scratch) || die "make_scratch failed"
	printf 'starting\n' > "$d/log"
	{ sleep 1; printf 'run: passed; log: x\n' >> "$d/log"; } &
	out=$(waited "$d/log" 20 "passed; log:")
	assert_eq "$?" 0 "$out"
	assert_eq "$out" "run: passed; log: x"
	wait "$!" || fail "the writer failed"
}

# Test that any of several strings ends the wait, the first matching line
# printed: a failed run ends it as soon as a passed one would.
test_any_string_ends_the_wait()
{
	local d out
	d=$(make_scratch) || die "make_scratch failed"
	printf 'ci.sh: log: x\nci.sh: failed: gate\nci.sh: log: x\n' > "$d/log"
	out=$(waited "$d/log" 5 "ci.sh: passed" "ci.sh: failed")
	assert_eq "$?" 0 "$out"
	assert_eq "$out" "ci.sh: failed: gate"
}

# Test that the strings are fixed strings: regex characters match only
# themselves.
test_strings_are_fixed()
{
	local d out rc
	d=$(make_scratch) || die "make_scratch failed"
	printf 'rc=1 a.b\n' > "$d/log"
	out=$(waited "$d/log" 5 "a.b")
	assert_eq "$?" 0 "$out"
	out=$(waited "$d/log" 1 "rc=.") && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
}

# Test that no matching line by the deadline fails with status 1, naming
# the file and the strings it waited for.
test_deadline_fails()
{
	local d out rc
	d=$(make_scratch) || die "make_scratch failed"
	printf 'still running\n' > "$d/log"
	out=$(waited "$d/log" 2 "passed" "failed") && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "wait-for.sh: no line containing 'passed' or 'failed' in $d/log after 2 s"
}

# Test that a missing file is waited for until the deadline: one created
# in time with the line passes, one never created fails, saying so.
test_missing_file_waits_until_the_deadline()
{
	local d out rc
	d=$(make_scratch) || die "make_scratch failed"
	{ sleep 1; printf 'done\n' > "$d/late"; } &
	out=$(waited "$d/late" 20 "done")
	assert_eq "$?" 0 "$out"
	wait "$!" || fail "the writer failed"
	out=$(waited "$d/never" 1 "done") && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "wait-for.sh: $d/never does not exist after 1 s"
}

# Test that missing arguments, an empty string or one holding a newline,
# and a deadline that is not a positive whole number of seconds are usage
# errors.
test_usage_errors()
{
	local d args out rc
	d=$(make_scratch) || die "make_scratch failed"
	: > "$d/log"
	for args in "" "$d/log" "$d/log 1" "$d/log ten x" "$d/log 0 x" "$d/log -1 x" "$d/log 1.5 x"; do
		# shellcheck disable=SC2086  # the arguments are words
		out=$(waited $args) && rc=0 || rc=$?
		assert_eq "$rc" 2 "args '$args': $out"
		assert_contains "$out" "usage:"
	done
	out=$(waited "$d/log" 1 x "") && rc=0 || rc=$?
	assert_eq "$rc" 2 "empty string: $out"
	out=$(waited "$d/log" 1 $'a\nb') && rc=0 || rc=$?
	assert_eq "$rc" 2 "string with a newline: $out"
}

run_tests --parallel
