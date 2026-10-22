#!/usr/bin/env bash
# Command-line behaviour of tools/dev/no-window.sh, which runs a command and
# fails if it changed the frontmost app or left a macOS crash report, and
# the real check: a headless SDL3 GPU tool stays in the background.
source "$(dirname "$0")/lib.sh"
NO_WINDOW="$DEV_DIR/no-window.sh"

# Test that a command that leaves the frontmost app alone passes and that
# the command's exit status is passed through.
test_unchanged_frontmost_passes()
{
	local d out rc
	d=$(make_scratch) || die "make_scratch failed"; mkdir "$d/reports"
	out=$(NO_WINDOW_FRONTMOST_CMD=$(fake_sampler "$d" "1 Terminal") NO_WINDOW_REPORTS_DIR="$d/reports" \
		"$NO_WINDOW" sh -c 'exit 0' 2>&1)
	assert_eq "$?" 0 "$out"
	out=$(NO_WINDOW_FRONTMOST_CMD=$(fake_sampler "$d" "1 Terminal") NO_WINDOW_REPORTS_DIR="$d/reports" \
		"$NO_WINDOW" sh -c 'exit 7' 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 7
}

# Test that the run is silent: SDL2's and SDL3's audio driver is dummy for
# the command, also over a driver the caller chose, and only
# NO_WINDOW_SOUND=1 keeps the caller's (a game run played sound through
# the speakers during a meeting).
test_run_is_silent()
{
	local d out
	d=$(make_scratch) || die "make_scratch failed"; mkdir "$d/reports"
	out=$(SDL_AUDIODRIVER=coreaudio SDL_AUDIO_DRIVER=coreaudio \
		NO_WINDOW_FRONTMOST_CMD=$(fake_sampler "$d" "1 Terminal") NO_WINDOW_REPORTS_DIR="$d/reports" \
		"$NO_WINDOW" sh -c 'echo "$SDL_AUDIODRIVER $SDL_AUDIO_DRIVER"' 2>&1)
	assert_eq "$out" "dummy dummy"
	out=$(SDL_AUDIODRIVER=coreaudio SDL_AUDIO_DRIVER=coreaudio NO_WINDOW_SOUND=1 \
		NO_WINDOW_FRONTMOST_CMD=$(fake_sampler "$d" "1 Terminal") NO_WINDOW_REPORTS_DIR="$d/reports" \
		"$NO_WINDOW" sh -c 'echo "$SDL_AUDIODRIVER $SDL_AUDIO_DRIVER"' 2>&1)
	assert_eq "$out" "coreaudio coreaudio"
}

# Test that a child of the run taking focus fails and names it.  The child
# keeps focus until the sampler has reported it twice: the second sample
# listed the run's processes while the child was alive, so the result does
# not depend on how fast samples come under load (about 75 s at most:
# 600 polls of 0.1 s plus their cost).
test_child_of_run_taking_focus_fails()
{
	local d out rc
	d=$(make_scratch) || die "make_scratch failed"; mkdir "$d/reports"
	cat > "$d/child" <<'CHILD'
i=0
until [ "$(cat "$1/seen" 2>/dev/null | wc -l)" -ge 2 ] || [ "$i" -ge 600 ]; do
	sleep 0.1; i=$((i + 1))
done
CHILD
	out=$(NO_WINDOW_FRONTMOST_CMD=$(run_sampler "$d") NO_WINDOW_REPORTS_DIR="$d/reports" \
		"$NO_WINDOW" sh -c "sh '$d/child' '$d' & echo \$! > '$d/pid'; wait" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "intruder (pid $(cat "$d/pid"))"
}

# Test that an app outside the run taking focus (the owner switching apps)
# passes.
test_unrelated_app_taking_focus_passes()
{
	local d out
	d=$(make_scratch) || die "make_scratch failed"; mkdir "$d/reports"
	out=$(NO_WINDOW_FRONTMOST_CMD=$(fake_sampler "$d" "$$ Terminal" "1 Google Chrome") \
		NO_WINDOW_REPORTS_DIR="$d/reports" "$NO_WINDOW" sh -c 'sleep 1' 2>&1)
	assert_eq "$?" 0 "$out"
}

# Test that a new crash report (which comes with a CrashReporter dialog)
# of a process of the run, or of its child, fails the run and is named.
test_new_crash_report_of_run_fails()
{
	local d out rc
	d=$(make_scratch) || die "make_scratch failed"; mkdir "$d/reports"
	out=$(NO_WINDOW_FRONTMOST_CMD=$(fake_sampler "$d" "1 Terminal") NO_WINDOW_REPORTS_DIR="$d/reports" \
		"$NO_WINDOW" sh -c "sleep 1; printf '{\"pid\" : 99999, \"parentPid\" : %s}' \$\$ > '$d/reports/probe-1.ips'" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "probe-1.ips"
}

# Test that a new crash report of a process outside the run passes.
test_unrelated_crash_report_passes()
{
	local d out
	d=$(make_scratch) || die "make_scratch failed"; mkdir "$d/reports"
	out=$(NO_WINDOW_FRONTMOST_CMD=$(fake_sampler "$d" "1 Terminal") NO_WINDOW_REPORTS_DIR="$d/reports" \
		"$NO_WINDOW" sh -c "printf '{\"pid\" : 1, \"parentPid\" : 1}' > '$d/reports/other-1.ips'" 2>&1)
	assert_eq "$?" 0 "$out"
}

# Test that a run fails, without running CMD, when the frontmost app cannot
# be read (osascript without the Automation permission prints nothing).
test_empty_frontmost_sample_fails()
{
	local d out rc
	d=$(make_scratch) || die "make_scratch failed"; mkdir "$d/reports"
	out=$(NO_WINDOW_FRONTMOST_CMD=$(fake_sampler "$d" '') NO_WINDOW_REPORTS_DIR="$d/reports" \
		"$NO_WINDOW" sh -c "touch '$d/ran'" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "cannot read the frontmost app"
	[[ ! -e $d/ran ]] || fail 'CMD ran without a working sampler'
}

# Test that a real headless SDL3 GPU tool, which sets
# SDL_HINT_MAC_BACKGROUND_APP, never becomes the frontmost app (macOS only).
test_headless_sdl_gpu_tool_stays_in_background()
{
	[[ $(uname -s) == Darwin ]] || return 0
	local d out sdl3_flags
	d=$(make_scratch) || die "make_scratch failed"
	pkg-config --exists sdl3 2>/dev/null || skip 'pkg-config finds no sdl3; install SDL3 to run this check'
	read -ra sdl3_flags < <(pkg-config --cflags --libs sdl3)
	cc -std=c11 "$DEV_DIR/tests/headless_probe.c" -o "$d/headless_probe" "${sdl3_flags[@]}" ||
		{ fail 'cannot build headless_probe'; return; }
	out=$("$DEV_DIR/run.sh" "$d/headless_probe" 2>&1)
	assert_eq "$?" 0 "$out"
	assert_contains "$out" "headless_probe: gpu device"
}

run_tests
