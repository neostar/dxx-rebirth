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
# (sanitizer settings from env.sh, SDL in the background, Retina off).
test_runs_binary_with_dev_environment()
{
	local out
	out=$(env -u ASAN_OPTIONS "$RUN" /bin/sh -c 'echo "arg=$0 asan=$ASAN_OPTIONS sdl=$SDL_MAC_BACKGROUND_APP hidpi=${SDL_VIDEO_HIGHDPI_DISABLED-}"' bar 2>&1)
	assert_eq "$?" 0 "$out"
	assert_contains "$out" "arg=bar"
	assert_contains "$out" "abort_on_error=0"
	assert_contains "$out" "sdl=1"
	assert_contains "$out" "hidpi=1"
}

# Test that a run gets SDL's real joystick backends off, whatever the
# caller's environment held: SDL reads hints from the environment, so the
# binary opens only virtual joysticks (game-data.md).
test_turns_off_real_joysticks()
{
	local out
	out=$(SDL_JOYSTICK_HIDAPI=1 SDL_JOYSTICK_IOKIT=1 SDL_JOYSTICK_MFI=1 \
		"$RUN" /bin/sh -c 'echo "hidapi=${SDL_JOYSTICK_HIDAPI-} iokit=${SDL_JOYSTICK_IOKIT-} mfi=${SDL_JOYSTICK_MFI-}"' 2>&1)
	assert_eq "$?" 0 "$out"
	assert_contains "$out" "hidapi=0 iokit=0 mfi=0"
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

# Run a stand-in game that writes line $2 to stderr and exits 0, through
# run.sh in directory $1, with environment $3... (DXX_DBG_HIDDEN=1 makes it
# a hidden run).  run.sh's output goes to $1/out, its status to $1/rc.
hidden_game_run()
{
	local d=$1 line=$2; shift 2
	mkdir -p "$d/reports"
	printf '%s\n' "$line" > "$d/line"
	cat > "$d/game" <<EOF
#!/bin/sh
echo before
cat "$d/line" >&2
echo after >&2
EOF
	chmod +x "$d/game"
	env -u DXX_DBG_HIDDEN -u NO_WINDOW_SOUND "$@" NO_WINDOW_FRONTMOST_CMD="$(fake_sampler "$d" '1 Finder')" \
		NO_WINDOW_REPORTS_DIR="$d/reports" "$RUN" "$d/game" > "$d/out" 2>&1
	echo "$?" > "$d/rc"
}

# Test that a hidden game run whose log names the dummy audio driver
# passes, with the game's output passed on.
test_hidden_run_on_the_dummy_driver_passes()
{
	local d
	d=$(make_scratch) || die "make_scratch failed"
	hidden_game_run "$d" 'DXX_DBG_HIDDEN: audio driver dummy' DXX_DBG_HIDDEN=1
	assert_eq "$(cat "$d/rc")" 0 "$(cat "$d/out")"
	assert_contains "$(cat "$d/out")" "before"
	assert_contains "$(cat "$d/out")" "DXX_DBG_HIDDEN: audio driver dummy"
	assert_contains "$(cat "$d/out")" "after"
}

# Test that a hidden game run on SDL's disk driver passes: it writes a
# file and plays nothing (R12 captures).
test_hidden_run_on_the_disk_driver_passes()
{
	local d
	d=$(make_scratch) || die "make_scratch failed"
	hidden_game_run "$d" 'DXX_DBG_HIDDEN: audio driver disk' DXX_DBG_HIDDEN=1 \
		SDL_AUDIO_DRIVER=disk SDL_AUDIO_DISK_OUTPUT_FILE="$d/a.raw"
	assert_eq "$(cat "$d/rc")" 0 "$(cat "$d/out")"
	hidden_game_run "$d" 'DXX_DBG_HIDDEN: audio driver diskx' DXX_DBG_HIDDEN=1
	assert_eq "$(cat "$d/rc")" 1 "$(cat "$d/out")"
	assert_contains "$(cat "$d/out")" "run.sh: game: hidden run opened audio driver 'diskx', not dummy or disk"
}

# Test that a hidden game run whose log names another audio driver fails,
# naming it: that run could play sound.
test_hidden_run_on_another_driver_fails()
{
	local d
	d=$(make_scratch) || die "make_scratch failed"
	hidden_game_run "$d" 'DXX_DBG_HIDDEN: audio driver coreaudio' DXX_DBG_HIDDEN=1
	assert_eq "$(cat "$d/rc")" 1 "$(cat "$d/out")"
	assert_contains "$(cat "$d/out")" "run.sh: game: hidden run opened audio driver 'coreaudio', not dummy or disk"
}

# Test that a hidden game run whose log names no driver once audio is
# initialised fails.
test_hidden_run_naming_no_driver_fails()
{
	local d
	d=$(make_scratch) || die "make_scratch failed"
	hidden_game_run "$d" 'DXX_DBG_HIDDEN: audio driver ' DXX_DBG_HIDDEN=1
	assert_eq "$(cat "$d/rc")" 1 "$(cat "$d/out")"
	assert_contains "$(cat "$d/out")" "run.sh: game: hidden run opened audio but names no driver"
}

# Test that a run with sound asked for (NO_WINDOW_SOUND=1), or one not
# hidden, is not checked: another driver passes.
test_unchecked_runs_pass_on_another_driver()
{
	local d
	d=$(make_scratch) || die "make_scratch failed"
	hidden_game_run "$d" 'DXX_DBG_HIDDEN: audio driver coreaudio' DXX_DBG_HIDDEN=1 NO_WINDOW_SOUND=1
	assert_eq "$(cat "$d/rc")" 0 "$(cat "$d/out")"
	d=$(make_scratch) || die "make_scratch failed"
	hidden_game_run "$d" 'DXX_DBG_HIDDEN: audio driver coreaudio'
	assert_eq "$(cat "$d/rc")" 0 "$(cat "$d/out")"
}

# Test that a hidden run which never initialised audio, so names no
# driver, passes: -nosound plays nothing.
test_hidden_run_without_audio_passes()
{
	local d
	d=$(make_scratch) || die "make_scratch failed"
	hidden_game_run "$d" 'no audio here' DXX_DBG_HIDDEN=1
	assert_eq "$(cat "$d/rc")" 0 "$(cat "$d/out")"
}

# Test that a line holding the report after other text, as after a partial
# stderr write the game never ended, fails: only the exact dummy line
# passes.
test_report_after_other_text_fails()
{
	local d
	d=$(make_scratch) || die "make_scratch failed"
	hidden_game_run "$d" 'partialDXX_DBG_HIDDEN: audio driver coreaudio' DXX_DBG_HIDDEN=1
	assert_eq "$(cat "$d/rc")" 1 "$(cat "$d/out")"
	assert_contains "$(cat "$d/out")" \
		"run.sh: game: hidden run's audio driver report is not alone on its line: 'partialDXX_DBG_HIDDEN: audio driver coreaudio'"
	d=$(make_scratch) || die "make_scratch failed"
	hidden_game_run "$d" 'partialDXX_DBG_HIDDEN: audio driver dummy' DXX_DBG_HIDDEN=1
	assert_eq "$(cat "$d/rc")" 1 "$(cat "$d/out")"
	d=$(make_scratch) || die "make_scratch failed"
	hidden_game_run "$d" 'DXX_DBG_HIDDEN: audio driver dummy ' DXX_DBG_HIDDEN=1
	assert_eq "$(cat "$d/rc")" 1 "$(cat "$d/out")"
}

# Test that a hidden run on another driver that then crashes still has its
# crash report, written late, awaited and named.
test_crash_report_of_a_hidden_run_on_another_driver_is_named()
{
	local d out rc
	d=$(make_scratch) || die "make_scratch failed"; mkdir "$d/reports"
	ips_lib "$d"
	cat > "$d/crashy" <<EOF
#!/bin/sh
. "$d/ips.sh"
echo 'DXX_DBG_HIDDEN: audio driver coreaudio' >&2
nw=\$PPID
(
	while kill -0 "\$nw" 2>/dev/null; do sleep 0.1; done
	sleep 1
	ips crashy \$\$ "$d/reports/crashy-1.ips"
) > /dev/null 2>&1 &
kill -USR1 \$\$
EOF
	chmod +x "$d/crashy"
	out=$(env -u NO_WINDOW_SOUND DXX_DBG_HIDDEN=1 NO_WINDOW_FRONTMOST_CMD="$(fake_sampler "$d" '1 Finder')" \
		NO_WINDOW_REPORTS_DIR="$d/reports" "$RUN" "$d/crashy" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "run.sh: crashy: hidden run opened audio driver 'coreaudio', not dummy or disk"
	assert_contains "$out" "run.sh: crashy left a crash report: $d/reports/crashy-1.ips"
}

# Test that a hidden run whose stderr a process still holds 5 s after the
# run ended fails: its copy would be incomplete.  The holder holds it until
# the test kills it, as run.sh's 5 s run longer on a loaded machine; run.sh
# must not wait for it.
test_hidden_run_with_stderr_held_open_fails()
{
	local d out rc
	d=$(make_scratch) || die "make_scratch failed"; mkdir "$d/reports"
	cat > "$d/game" <<EOF
#!/bin/sh
echo 'DXX_DBG_HIDDEN: audio driver dummy' >&2
sleep 120 > /dev/null &
echo \$! > "$d/holder"
EOF
	chmod +x "$d/game"
	out=$(env -u NO_WINDOW_SOUND DXX_DBG_HIDDEN=1 NO_WINDOW_FRONTMOST_CMD="$(fake_sampler "$d" '1 Finder')" \
		NO_WINDOW_REPORTS_DIR="$d/reports" "$RUN" "$d/game" 2>&1) && rc=0 || rc=$?
	kill "$(cat "$d/holder")" 2>/dev/null || fail "run.sh waited for the holder to end"
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "run.sh: game: stderr still open 5 s after the run; cannot check its audio driver"
}

# Run hidden_game_run's stand-in, with a dummy-driver line, in directory
# $1, with a tee on PATH that runs shell code $2 instead of copying.
hidden_run_with_tee()
{
	local d=$1
	mkdir -p "$d/bin"
	printf '#!/bin/sh\n%s\n' "$2" > "$d/bin/tee"
	chmod +x "$d/bin/tee"
	hidden_game_run "$d" 'DXX_DBG_HIDDEN: audio driver dummy' DXX_DBG_HIDDEN=1 PATH="$d/bin:$PATH"
}

# Test that a hidden run fails when copying its stderr fails.
test_hidden_run_whose_copy_fails_fails()
{
	local d
	d=$(make_scratch) || die "make_scratch failed"
	hidden_run_with_tee "$d" 'cat > "$1"; exit 1'
	assert_eq "$(cat "$d/rc")" 1 "$(cat "$d/out")"
	assert_contains "$(cat "$d/out")" "run.sh: game: copying stderr failed"
}

# Test that a hidden run fails when its stderr copy cannot be read: here
# the copy was never written.
test_hidden_run_whose_copy_is_unreadable_fails()
{
	local d
	d=$(make_scratch) || die "make_scratch failed"
	hidden_run_with_tee "$d" 'cat > /dev/null'
	assert_eq "$(cat "$d/rc")" 1 "$(cat "$d/out")"
	assert_contains "$(cat "$d/out")" "run.sh: game: cannot read its stderr copy"
}

run_tests
