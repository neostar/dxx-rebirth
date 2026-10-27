#!/usr/bin/env bash
# tools/dev/rtrace-d3d12.sh's verdicts on a fake rtrace-gpu.exe that prints
# what SDL's D3D12 backend prints and logs given frame hash lines; the
# manual GitHub workflow (d3d12-check.yml) and windows.sh wine run the real
# one.
# Single quotes hold scripts expanded by the shells they are written for.
# shellcheck disable=SC2016
source "$(dirname "$0")/lib.sh"
# shellcheck disable=SC2034  # rtrace-pins.sh reads it
root=$REPO_ROOT
# shellcheck source=../rtrace-pins.sh
source "$DEV_DIR/rtrace-pins.sh"

d3d12_lines='INFO: Validation layers enabled, expect debug level performance!
INFO: SDL_GPU Driver: D3D12
INFO: D3D12 Adapter: Microsoft Basic Render Driver
INFO: D3D12 Driver: 10.0.26100.1
INFO: sdlgpu: debug mode on'

# Make a directory holding a fake rtrace-gpu.exe and print its path.  The
# fake prints $FAKE_OUT (default: the D3D12 lines above) with CR LF line
# ends, as SDL's log on Windows, records its trace and SDL_GPU_DRIVER in
# calls.log beside it, and writes the trace's pinned lines (or
# $FAKE_LINES, | between lines, when set) to its log, CR LF too.
fake_build()
{
	local d
	d=$(make_scratch) || die "make_scratch failed"
	cat > "$d/rtrace-gpu.exe" <<'EOF'
#!/usr/bin/env bash
here=$(cd "$(dirname "$0")" && pwd)
echo "${1##*/} ${SDL_GPU_DRIVER-unset}" >> "$here/calls.log"
printf '%s\n' "${FAKE_OUT-$D3D12_LINES}" | sed 's/$/\r/'
case ${1##*/} in
	sample.rtr) lines=$PIN_SAMPLE ;;
	overbright.rtr) lines=$PIN_OVERBRIGHT ;;
	flash.rtr) lines=$PIN_FLASH ;;
	*) exit 1 ;;
esac
printf '%s\n' "${FAKE_LINES-$lines}" | tr '|' '\n' | sed 's/$/\r/' > "$2"
exit "${FAKE_STATUS:-0}"
EOF
	chmod +x "$d/rtrace-gpu.exe"
	echo "$d"
}

# Run rtrace-d3d12.sh on build $1 with the remaining arguments; print its
# output, then its exit status.
check()
{
	local out rc IFS='|'
	out=$(D3D12_LINES=$d3d12_lines PIN_SAMPLE="${expected[*]}" \
		PIN_OVERBRIGHT="${expected_overbright[*]}" PIN_FLASH="${expected_flash[*]}" \
		"$DEV_DIR/rtrace-d3d12.sh" "$@" 2>&1) && rc=0 || rc=$?
	printf '%s\nstatus %s\n' "$out" "$rc"
}

# Test that a build that logs GL's pinned lines for every synthetic trace
# on D3D12, with the debug layer on, passes, naming the adapter, and that
# each trace ran with SDL_GPU_DRIVER asking for D3D12.
test_pinned_lines_on_d3d12_pass()
{
	local d out
	d=$(fake_build)
	out=$(check "$d")
	assert_contains "$out" "rtrace-d3d12: adapter: Microsoft Basic Render Driver"
	assert_contains "$out" "rtrace-d3d12: passed"
	assert_contains "$out" "status 0"
	assert_eq "$(cat "$d/calls.log")" "sample.rtr direct3d12
overbright.rtr direct3d12
flash.rtr direct3d12"
}

# Test that a frame hash line other than GL's fails, naming the trace, and
# so do a missing line and an extra one.
test_other_frame_lines_fail()
{
	local d out
	d=$(fake_build)
	out=$(FAKE_LINES='0 64x48 0000000000000000|1 64x48 9f40fbb36ef64325' check "$d")
	assert_contains "$out" "rtrace-d3d12: the sample trace's frame hashes are not GL's"
	assert_contains "$out" "status 1"
	assert_contains "$(FAKE_LINES='0 64x48 86388f1d5003af2a' check "$d")" \
		"rtrace-d3d12: the sample trace's frame hashes are not GL's"
	assert_contains "$(FAKE_LINES="${expected[0]}|${expected[1]}|2 64x48 0" check "$d")" \
		"rtrace-d3d12: the sample trace's frame hashes are not GL's"
}

# Test that a run on another driver, or one naming no driver or no
# adapter, fails.
test_other_driver_or_no_adapter_fails()
{
	local d out
	d=$(fake_build)
	out=$(FAKE_OUT="${d3d12_lines/Driver: D3D12/Driver: Vulkan}" check "$d")
	assert_contains "$out" "rtrace-d3d12: the sample trace ran on Vulkan, not D3D12"
	assert_contains "$out" "status 1"
	out=$(FAKE_OUT="${d3d12_lines/INFO: D3D12 Adapter: Microsoft Basic Render Driver/}" check "$d")
	assert_contains "$out" "rtrace-d3d12: the sample trace named no D3D12 adapter"
	assert_contains "$out" "status 1"
}

# Test that a validation failure fails the run (gpu-validate.sh decides):
# no debug layer, a debug layer message, a failing program.
test_validation_failure_fails()
{
	local d out
	d=$(fake_build)
	out=$(FAKE_OUT="${d3d12_lines/INFO: Validation layers enabled*!/WARNING: Validation layers not found, continuing without validation}" check "$d")
	assert_contains "$out" "the D3D12 debug layer was not active"
	assert_contains "$out" "rtrace-d3d12: rtrace-gpu failed on the sample trace"
	assert_contains "$out" "status 1"
	out=$(FAKE_OUT="$d3d12_lines
ERROR: D3D12 ERROR: bad [ EXECUTION ERROR #1]" check "$d")
	assert_contains "$out" "status 1"
	assert_contains "$(FAKE_STATUS=3 check "$d")" "status 1"
}

# Test that a launcher runs the program (windows.sh runs it through wine).
test_launcher_runs_the_program()
{
	local d l out
	d=$(fake_build)
	l=$(make_scratch) || die "make_scratch failed"
	printf '#!/bin/sh\necho launched >> "%s/launches"\nexec "$@"\n' "$l" > "$l/launch"
	chmod +x "$l/launch"
	out=$(check "$d" "$l/launch")
	assert_contains "$out" "status 0"
	assert_eq "$(wc -l < "$l/launches" | tr -d ' ')" 3
}

# Test the usage errors and a directory without the build.
test_usage_and_missing_build()
{
	local d
	assert_contains "$(check)" "status 2"
	assert_contains "$(check '')" "status 2"
	d=$(make_scratch) || die "make_scratch failed"
	assert_contains "$(check "$d")" "rtrace-d3d12: no rtrace-gpu.exe in $d"
	assert_contains "$(check "$d")" "status 1"
}

run_tests
