#!/usr/bin/env bash
# Command-line behaviour of tools/dev/run.sh, which runs a binary with the
# dev environment, after checking its libraries resolve, under no-window.sh.
# Single quotes hold scripts expanded by the shells they are written for.
# shellcheck disable=SC2016
source "$(dirname "$0")/lib.sh"
RUN="$DEV_DIR/run.sh"

# Test that run.sh without a binary is a usage error with exit status 2.
test_no_binary_is_usage_error()
{
	local out rc
	out=$("$RUN" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 2
	assert_contains "$out" "usage"
}

# Test that a binary runs with its arguments and the dev environment
# (sanitizer settings from env.sh, SDL in the background).
test_runs_binary_with_dev_environment()
{
	local out
	out=$(env -u ASAN_OPTIONS "$RUN" /bin/sh -c 'echo "arg=$0 asan=$ASAN_OPTIONS sdl=$SDL_MAC_BACKGROUND_APP"' bar 2>&1)
	assert_eq "$?" 0 "$out"
	assert_contains "$out" "arg=bar"
	assert_contains "$out" "abort_on_error=0"
	assert_contains "$out" "sdl=1"
}

# Test that the binary taking focus fails the run.
test_focus_change_fails()
{
	local d out rc
	d=$(make_scratch) || die "make_scratch failed"; mkdir "$d/reports"
	out=$(NO_WINDOW_FRONTMOST_CMD=$(run_sampler "$d") NO_WINDOW_REPORTS_DIR="$d/reports" \
		"$RUN" /bin/sh -c "echo \$\$ > '$d/pid'; sleep 1" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "intruder"
}

# Test that a binary linking a library that does not resolve fails, naming
# the library, without being run (macOS: otool -L).
test_unresolved_library_fails_before_launch()
{
	[[ $(uname -s) == Darwin ]] || skip 'otool -L check is macOS only'
	command -v cc >/dev/null || skip 'no cc to build the test binary'
	local d out rc
	d=$(make_scratch) || die "make_scratch failed"
	printf 'int lib_answer(void) { return 42; }\n' > "$d/lib.c"
	printf 'int lib_answer(void);\nint main(void) { return lib_answer() == 42 ? 0 : 1; }\n' > "$d/main.c"
	if ! cc -dynamiclib "$d/lib.c" -install_name @rpath/libanswer.dylib -o "$d/libanswer.dylib" ||
		! cc "$d/main.c" -L"$d" -lanswer -Wl,-rpath,@executable_path -o "$d/prog"; then
		fail 'cannot build the test binary'; return
	fi
	out=$("$RUN" "$d/prog" 2>&1)
	assert_eq "$?" 0 "$out"
	rm "$d/libanswer.dylib"
	out=$("$RUN" "$d/prog" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "@rpath/libanswer.dylib"
}

# Write ips.sh into directory $1, for the stand-in binaries to source: its
# `ips NAME PID FILE` writes FILE as a crash report in the form macOS
# writes it, a header line, then a body whose procPath is redacted down to
# the program's name.
ips_lib()
{
	cat > "$1/ips.sh" <<'IPS'
ips()
{
	printf '{"app_name":"%s","bug_type":"309"}\n{\n  "pid" : %s,\n  "procName" : "%s",\n  "procPath" : "\\/Users\\/USER\\/*\\/%s",\n  "parentPid" : 1\n}\n' \
		"$1" "$2" "$1" "$1" > "$3"
}
IPS
}

# Test that a binary ended by a signal fails on its crash report, its pid
# and name, written late as ReportCrash writes it: here once no-window.sh
# (the stand-in's parent) has ended, so only run.sh can see it; its
# procPath is redacted as macOS writes it.  The report is named and kept.
# A report of another program, of another run of the same binary, or an
# older one, passes.  The stand-in ends by USR1, which leaves no real
# report.
test_crash_report_of_the_binary_fails()
{
	local d out rc
	d=$(make_scratch) || die "make_scratch failed"; mkdir "$d/reports"
	ips_lib "$d"
	printf '{"app_name":"other","pid":1}\n' > "$d/reports/old.ips"
	touch -t 200001010000 "$d/reports/old.ips"
	cat > "$d/crashy" <<EOF
#!/bin/sh
. "$d/ips.sh"
nw=\$PPID
(
	while kill -0 "\$nw" 2>/dev/null; do sleep 0.1; done
	ips other \$\$ "$d/reports/x.ips"
	ips crashy 1 "$d/reports/y.ips"
	ips crashy \$\$ "$d/reports/crashy-1.ips"
) > /dev/null 2>&1 &
kill -USR1 \$\$
EOF
	chmod +x "$d/crashy"
	out=$(NO_WINDOW_FRONTMOST_CMD=$(fake_sampler "$d" '1 Finder') NO_WINDOW_REPORTS_DIR="$d/reports" \
		"$RUN" "$d/crashy" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "run.sh: crashy left a crash report: $d/reports/crashy-1.ips"
	assert_not_contains "$out" "x.ips"
	assert_not_contains "$out" "y.ips"
	assert_not_contains "$out" "old.ips"
	[[ -e $d/reports/crashy-1.ips ]] || fail "the crash report was deleted"
}

# Test that the binary's name is compared as a fixed string: for crash.y, a
# late report with crash.y's pid from crashXy (a regex's dot would match
# it) passes, and the run keeps the binary's exit status.
test_crash_report_name_matches_exactly()
{
	local d out rc
	d=$(make_scratch) || die "make_scratch failed"; mkdir "$d/reports"
	ips_lib "$d"
	cat > "$d/crash.y" <<EOF
#!/bin/sh
. "$d/ips.sh"
nw=\$PPID
(while kill -0 "\$nw" 2>/dev/null; do sleep 0.1; done; ips crashXy \$\$ "$d/reports/a.ips") \
	> /dev/null 2>&1 &
kill -USR1 \$\$
EOF
	chmod +x "$d/crash.y"
	out=$(NO_WINDOW_FRONTMOST_CMD=$(fake_sampler "$d" '1 Finder') NO_WINDOW_REPORTS_DIR="$d/reports" \
		"$RUN" "$d/crash.y" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" $((128 + $(kill -l USR1))) "$out"
	assert_not_contains "$out" "a.ips"
}

# Test that a binary that exits normally is not held for a crash report:
# one written 2 s after no-window.sh ended is not waited for.
test_normal_exit_does_not_wait_for_a_report()
{
	local d out i
	d=$(make_scratch) || die "make_scratch failed"; mkdir "$d/reports"
	ips_lib "$d"
	cat > "$d/quick" <<EOF
#!/bin/sh
. "$d/ips.sh"
nw=\$PPID
(
	while kill -0 "\$nw" 2>/dev/null; do sleep 0.1; done
	sleep 2
	ips quick \$\$ "$d/reports/quick-1.ips"
	touch "$d/written"
) > /dev/null 2>&1 &
exit 0
EOF
	chmod +x "$d/quick"
	out=$(NO_WINDOW_FRONTMOST_CMD=$(fake_sampler "$d" '1 Finder') NO_WINDOW_REPORTS_DIR="$d/reports" \
		"$RUN" "$d/quick" 2>&1)
	assert_eq "$?" 0 "$out"
	assert_not_contains "$out" "quick-1.ips"
	for i in $(seq 100); do [[ -e $d/written ]] && break; sleep 0.1; done
}

# Start run.sh on a stand-in running shell code $3 in directory $2, with
# job control, so INT is not ignored as in a background command; send
# signal $1 to run.sh once the stand-in runs, and fail if run.sh or a
# process of the run (its process group) is still running after it.  Extra
# environment in $4...; run.sh's output goes to $2/out, its status to $2/rc.
signal_run()
{
	local sig=$1 d=$2 body=$3 pid child pgid; shift 3
	printf '#!/bin/sh\necho $$ > "%s/child"\n%s\n' "$d" "$body" > "$d/standin"
	chmod +x "$d/standin"
	set -m
	env "$@" NO_WINDOW_FRONTMOST_CMD="$(fake_sampler "$d" '1 Finder')" \
		NO_WINDOW_REPORTS_DIR="$d/reports" "$RUN" "$d/standin" > "$d/out" 2>&1 &
	pid=$!
	set +m
	# Bounds that only end a broken run; a loaded machine stays far inside.
	for _ in $(seq 1200); do [[ -s $d/child ]] && break; sleep 0.1; done
	child=$(cat "$d/child" 2>/dev/null)
	[[ -n $child ]] || { fail "the binary never started"; kill "$pid"; return; }
	pgid=$(ps -o pgid= -p "$child" | tr -d ' ')
	kill -"$sig" "$pid"
	for _ in $(seq 600); do kill -0 "$pid" 2>/dev/null || break; sleep 0.1; done
	kill -0 "$pid" 2>/dev/null && fail "run.sh still running after $sig"
	wait "$pid"
	echo "$?" > "$d/rc"
	if kill -0 -- -"$pgid" 2>/dev/null; then
		fail "a process of the run still runs after $sig to run.sh: $(cat "$d/out")"
		kill -KILL -- -"$pgid"
	fi
}

# Test that TERM to run.sh reaches the run: no-window.sh and the binary end,
# none left running, and run.sh exits.
test_term_leaves_no_child_running()
{
	local d
	d=$(make_scratch) || die "make_scratch failed"; mkdir "$d/reports"
	signal_run TERM "$d" 'exec sleep 60'
}

# Test that INT to run.sh (Ctrl-C) ends the binary too, which ignores INT
# as a background command of no-window.sh.  A test file run as a
# background command (check.sh) ignores INT, and so does the run.sh it
# starts: there INT cannot reach run.sh, and the test is skipped.
test_int_leaves_no_child_running()
{
	local d
	[[ $(trap -p INT) != "trap -- '' SIGINT" ]] || skip 'INT is ignored here (background command)'
	d=$(make_scratch) || die "make_scratch failed"; mkdir "$d/reports"
	signal_run INT "$d" 'exec sleep 60'
}

# Test that run.sh, on TERM, waits for a binary that quits gracefully on
# TERM, as SDL turns TERM into a quit event, rather than leaving it running:
# the stand-in quits 2 s after TERM, writing a marker first.
test_term_waits_for_a_slow_quit()
{
	local d
	d=$(make_scratch) || die "make_scratch failed"; mkdir "$d/reports"
	signal_run TERM "$d" "trap 'sleep 2; touch \"$d/quit\"; exit 0' TERM
while :; do sleep 0.1; done"
	[[ -e $d/quit ]] || fail "run.sh returned before the binary quit: $(cat "$d/out")"
}

# Test that run.sh kills the run once it outlives the grace period after
# TERM (RUN_KILL_AFTER seconds), naming it: the stand-in ignores TERM.
test_term_kills_a_run_that_does_not_quit()
{
	local d
	d=$(make_scratch) || die "make_scratch failed"; mkdir "$d/reports"
	signal_run TERM "$d" "trap '' TERM
while :; do sleep 0.1; done" RUN_KILL_AFTER=1
	assert_contains "$(cat "$d/out")" "run.sh: standin still runs 1 s after TERM; killing it"
	assert_eq "$(cat "$d/rc")" 143
}

run_tests
