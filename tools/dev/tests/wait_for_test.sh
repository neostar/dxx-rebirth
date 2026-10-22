#!/usr/bin/env bash
# Command-line behaviour of tools/dev/wait-for.sh, which waits until a file
# holds a line containing a fixed string, up to a deadline.
source "$(dirname "$0")/lib.sh"
WAIT_FOR="$DEV_DIR/wait-for.sh"

# Test that a line written after the start ends the wait with status 0,
# well before the deadline.
test_match_ends_the_wait()
{
	local d out start
	d=$(make_scratch) || die "make_scratch failed"
	printf 'starting\n' > "$d/log"
	{ sleep 1; printf 'run: passed; log: x\n' >> "$d/log"; } &
	start=$SECONDS
	out=$("$WAIT_FOR" "$d/log" "passed; log:" 60 2>&1)
	assert_eq "$?" 0 "$out"
	wait "$!" || fail "the writer failed"
	[[ $((SECONDS - start)) -lt 30 ]] || fail "the match took $((SECONDS - start)) s"
	return 0
}

# Test that the pattern is a fixed string: regex characters match only
# themselves.
test_pattern_is_a_fixed_string()
{
	local d out rc
	d=$(make_scratch) || die "make_scratch failed"
	printf 'rc=1 a.b\n' > "$d/log"
	out=$("$WAIT_FOR" "$d/log" "a.b" 5 2>&1)
	assert_eq "$?" 0 "$out"
	out=$("$WAIT_FOR" "$d/log" "rc=." 1 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
}

# Test that no matching line by the deadline fails with status 1, naming
# the file and what it waited for, and that the deadline holds.
test_deadline_fails()
{
	local d out rc start
	d=$(make_scratch) || die "make_scratch failed"
	printf 'still running\n' > "$d/log"
	start=$SECONDS
	out=$("$WAIT_FOR" "$d/log" "passed" 2 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "wait-for.sh: no line containing 'passed' in $d/log after 2 s"
	[[ $((SECONDS - start)) -lt 30 ]] || fail "the deadline took $((SECONDS - start)) s"
	return 0
}

# Test that a missing file is waited for until the deadline: one created
# in time with the line passes, one never created fails, saying so.
test_missing_file_waits_until_the_deadline()
{
	local d out rc
	d=$(make_scratch) || die "make_scratch failed"
	{ sleep 1; printf 'done\n' > "$d/late"; } &
	out=$("$WAIT_FOR" "$d/late" "done" 60 2>&1)
	assert_eq "$?" 0 "$out"
	wait "$!" || fail "the writer failed"
	out=$("$WAIT_FOR" "$d/never" "done" 1 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "wait-for.sh: $d/never does not exist after 1 s"
}

# Test that missing or extra arguments, an empty pattern and a deadline
# that is not a positive whole number of seconds are usage errors.
test_usage_errors()
{
	local d args out rc
	d=$(make_scratch) || die "make_scratch failed"
	: > "$d/log"
	for args in "" "$d/log" "$d/log x" "$d/log x 1 extra" "$d/log x ten" "$d/log x 0" \
		"$d/log x -1" "$d/log x 1.5" "$d/log  1"; do
		# shellcheck disable=SC2086  # the arguments are words
		out=$("$WAIT_FOR" $args 2>&1) && rc=0 || rc=$?
		assert_eq "$rc" 2 "args '$args': $out"
		assert_contains "$out" "usage:"
	done
	out=$("$WAIT_FOR" "$d/log" "" 1 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 2 "empty pattern: $out"
}

run_tests --parallel
