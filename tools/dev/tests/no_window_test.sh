#!/usr/bin/env bash
# Command-line behaviour of tools/dev/no-window.sh, which runs a command and
# fails if it changed the frontmost app or left a macOS crash report, and
# the real check: a headless SDL3 GPU tool stays in the background.
source "$(dirname "$0")/lib.sh"
NO_WINDOW="$DEV_DIR/no-window.sh"

# Write a fake frontmost-app sampler that reports the app listed on each
# line of FILE in turn (the last line repeats), and print its path.
fake_sampler()
{
	local d=$1; shift
	printf '%s\n' "$@" > "$d/apps"
	printf '0\n' > "$d/n"
	cat > "$d/sampler" <<SAMPLER
#!/usr/bin/env bash
n=\$(cat "$d/n"); echo \$((n + 1)) > "$d/n"
total=\$(wc -l < "$d/apps")
[[ \$n -ge \$total ]] && n=\$((total - 1))
sed -n "\$((n + 1))p" "$d/apps"
SAMPLER
	chmod +x "$d/sampler"
	printf '%s\n' "$d/sampler"
}

# Test that a command that leaves the frontmost app alone passes and that
# the command's exit status is passed through.
test_unchanged_frontmost_passes()
{
	local d out rc
	d=$(make_scratch); mkdir "$d/reports"
	out=$(NO_WINDOW_FRONTMOST_CMD=$(fake_sampler "$d" Terminal) NO_WINDOW_REPORTS_DIR="$d/reports" \
		"$NO_WINDOW" sh -c 'exit 0' 2>&1)
	assert_eq "$?" 0 "$out"
	out=$(NO_WINDOW_FRONTMOST_CMD=$(fake_sampler "$d" Terminal) NO_WINDOW_REPORTS_DIR="$d/reports" \
		"$NO_WINDOW" sh -c 'exit 7' 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 7
}

# Test that a command that takes focus fails and names the new app.
test_focus_change_fails()
{
	local d out rc
	d=$(make_scratch); mkdir "$d/reports"
	out=$(NO_WINDOW_FRONTMOST_CMD=$(fake_sampler "$d" Terminal probe-tool) NO_WINDOW_REPORTS_DIR="$d/reports" \
		"$NO_WINDOW" sh -c 'sleep 1' 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1
	assert_contains "$out" "probe-tool"
}

# Test that a new crash report (which comes with a CrashReporter dialog)
# fails the run and is named.
test_new_crash_report_fails()
{
	local d out rc
	d=$(make_scratch); mkdir "$d/reports"
	out=$(NO_WINDOW_FRONTMOST_CMD=$(fake_sampler "$d" Terminal) NO_WINDOW_REPORTS_DIR="$d/reports" \
		"$NO_WINDOW" sh -c "touch '$d/reports/probe-2026-10-20-120000.ips'" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1
	assert_contains "$out" "probe-2026-10-20-120000.ips"
}

# Test that a real headless SDL3 GPU tool, which sets
# SDL_HINT_MAC_BACKGROUND_APP, never becomes the frontmost app (macOS only).
test_headless_sdl_gpu_tool_stays_in_background()
{
	[[ $(uname -s) == Darwin ]] || return 0
	local d out
	d=$(make_scratch)
	cc -std=c11 "$DEV_DIR/tests/headless_probe.c" -o "$d/headless_probe" \
		$(pkg-config --cflags --libs sdl3) || { fail 'cannot build headless_probe'; return; }
	out=$("$NO_WINDOW" "$d/headless_probe" 2>&1)
	assert_eq "$?" 0 "$out"
	assert_contains "$out" "headless_probe: gpu device"
}

run_tests
