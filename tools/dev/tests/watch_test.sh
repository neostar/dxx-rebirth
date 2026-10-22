#!/usr/bin/env bash
# Command-line behaviour of tools/dev/watch.sh, which reports new crash
# reports and on-screen windows of dev binaries, with a fake window lister
# and a scratch report directory.
source "$(dirname "$0")/lib.sh"
WATCH="$DEV_DIR/watch.sh"

# Write into directory $1 a fake window lister that prints $1/windows and,
# on its first call, writes a crash report of d2x-rebirth into $1/reports;
# print its path.
fake_lister()
{
	cat > "$1/lister" <<EOF
#!/bin/sh
if [ ! -e "$1/called" ]; then
	: > "$1/called"
	printf '{"app_name":"d2x-rebirth","bug_type":"309"}\n{"procName" : "d2x-rebirth"}\n' \
		> "$1/reports/new.ips"
fi
cat "$1/windows"
EOF
	chmod +x "$1/lister"
	printf '%s\n' "$1/lister"
}

# Test that each crash report written since the start prints one line with
# its process name, an older one none, and that each on-screen window of a
# dev binary or Problem Reporter prints one line per owner and pid, other
# apps' none.
test_reports_new_crash_reports_and_dev_windows_once()
{
	local d out
	d=$(make_scratch) || die "make_scratch failed"; mkdir "$d/reports"
	printf '{"app_name":"old"}\n' > "$d/reports/old.ips"
	touch -t 200001010000 "$d/reports/old.ips"
	printf '%s\n' '1 Finder' '42 d2x-rebirth' '42 d2x-rebirth' '43 Problem Reporter' \
		'44 test-render' > "$d/windows"
	out=$(WATCH_WINDOWS_CMD=$(fake_lister "$d") NO_WINDOW_REPORTS_DIR="$d/reports" \
		WATCH_INTERVAL=0.1 "$WATCH" --rounds 3 2>&1)
	assert_eq "$?" 0 "$out"
	assert_eq "$(sed -E 's/^[0-9TZ:-]+ //' <<<"$out")" "window: d2x-rebirth (pid 42)
window: Problem Reporter (pid 43)
window: test-render (pid 44)
crash report: d2x-rebirth $d/reports/new.ips"
}

# Test that a window lister that fails or lists nothing ends the watch
# with status 1, naming why: a watch that cannot see is no watch.
test_blind_window_lister_fails()
{
	local d out rc
	d=$(make_scratch) || die "make_scratch failed"; mkdir "$d/reports"
	out=$(WATCH_WINDOWS_CMD=false NO_WINDOW_REPORTS_DIR="$d/reports" "$WATCH" --rounds 1 2>&1) &&
		rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "cannot list the on-screen windows"
	out=$(WATCH_WINDOWS_CMD=true NO_WINDOW_REPORTS_DIR="$d/reports" "$WATCH" --rounds 1 2>&1) &&
		rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
}

# Test that a crash report directory that is missing (a typo in
# NO_WINDOW_REPORTS_DIR) or unreadable ends the watch with status 1, naming
# it: a watch that cannot read it would report no crash ever.
test_unreadable_reports_dir_fails()
{
	local d out rc
	d=$(make_scratch) || die "make_scratch failed"; mkdir "$d/reports"
	printf '%s\n' '1 Finder' > "$d/windows"
	out=$(WATCH_WINDOWS_CMD=$(fake_lister "$d") NO_WINDOW_REPORTS_DIR="$d/typo" \
		"$WATCH" --rounds 1 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "watch.sh: cannot read the crash report directory $d/typo"
	chmod 000 "$d/reports"
	out=$(WATCH_WINDOWS_CMD=$(fake_lister "$d") NO_WINDOW_REPORTS_DIR="$d/reports" \
		"$WATCH" --rounds 1 2>&1) && rc=0 || rc=$?
	chmod 755 "$d/reports"
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "watch.sh: cannot read the crash report directory $d/reports"
}

# Test that a missing or non-numeric round count is a usage error.
test_bad_rounds_is_usage_error()
{
	local rc
	"$WATCH" --rounds 2>/dev/null && rc=0 || rc=$?
	assert_eq "$rc" 2
	"$WATCH" --rounds x 2>/dev/null && rc=0 || rc=$?
	assert_eq "$rc" 2
}

run_tests
