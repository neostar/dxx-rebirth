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

# Test that a focus change while the binary runs fails the run.
test_focus_change_fails()
{
	local d out rc
	d=$(make_scratch); mkdir "$d/reports"
	out=$(NO_WINDOW_FRONTMOST_CMD=$(fake_sampler "$d" Terminal intruder) NO_WINDOW_REPORTS_DIR="$d/reports" \
		"$RUN" /bin/sleep 1 2>&1) && rc=0 || rc=$?
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
	d=$(make_scratch)
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

run_tests
