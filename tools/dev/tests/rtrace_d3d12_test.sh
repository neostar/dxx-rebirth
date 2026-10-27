#!/usr/bin/env bash
# tools/dev/rtrace-d3d12.sh's verdicts on a fake rtrace-gpu.exe that prints
# what SDL's D3D12 backend prints and logs given frame hash
# lines; the manual GitHub workflow (d3d12-check.yml) and windows.sh wine
# run the real one.
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
# calls.log beside it, and writes the trace's
# pinned lines (or $FAKE_LINES, | between lines, when set; their hashes
# replaced by $FAKE_HASH, when set) to its log, CR LF
# too.
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
lines=${FAKE_LINES-$lines}
[[ -z ${FAKE_HASH-} ]] || lines=$(sed "s/[0-9a-f]\{16\}/$FAKE_HASH/g" <<< "$lines")
printf '%s\n' "$lines" | tr '|' '\n' | sed 's/$/\r/' > "$2"
exit "${FAKE_STATUS:-0}"
EOF
	chmod +x "$d/rtrace-gpu.exe"
	echo "$d"
}

# Run rtrace-d3d12.sh with the arguments, from a scratch directory (the
# default frame hashes file lands there); print its output, then its exit
# status.
check()
{
	local out rc IFS='|' cwd
	cwd=$(make_scratch) || die "make_scratch failed"
	out=$(cd "$cwd" && D3D12_LINES=$d3d12_lines PIN_SAMPLE="${expected[*]}" \
		PIN_OVERBRIGHT="${expected_overbright[*]}" PIN_FLASH="${expected_flash[*]}" \
		"$DEV_DIR/rtrace-d3d12.sh" "$@" 2>&1) && rc=0 || rc=$?
	printf '%s\nstatus %s\n' "$out" "$rc"
}

# Test that a build that renders every frame of every synthetic trace on
# D3D12, with the debug layer on, passes, naming the adapter, and that
# each trace ran with SDL_GPU_DRIVER asking for D3D12.
test_d3d12_run_passes()
{
	local d out
	d=$(fake_build)
	out=$(check --hashes "$d/hashes.txt" "$d")
	assert_contains "$out" "rtrace-d3d12: adapter: Microsoft Basic Render Driver"
	assert_contains "$out" "rtrace-d3d12: passed"
	assert_contains "$out" "status 0"
	assert_eq "$(cat "$d/calls.log")" "sample.rtr direct3d12
overbright.rtr direct3d12
flash.rtr direct3d12"
}

# Test that the frame hash lines are recorded, not compared: every trace's
# lines go to the hashes file, named by trace, and are printed, and lines
# other than GL's pass.  With no --hashes option the file is
# d3d12-frame-hashes.txt in the current directory.
test_frame_hashes_are_recorded_not_compared()
{
	local d out h
	d=$(fake_build)
	out=$(check --hashes "$d/hashes.txt" "$d")
	assert_eq "$(cat "$d/hashes.txt")" "sample 0 64x48 86388f1d5003af2a
sample 1 64x48 9f40fbb36ef64325
overbright 0 64x48 5d1b6b445fb7f05a
overbright 1 64x48 418a435cc9c67e56
flash 0 64x48 a088becd21e46f87
flash 1 64x48 4142e7869f743d59
flash 2 64x48 c33733e407fec502"
	assert_contains "$out" "sample 1 64x48 9f40fbb36ef64325"
	out=$(FAKE_HASH=0000000000000000 \
		check --hashes "$d/other.txt" "$d")
	assert_contains "$out" "status 0"
	assert_contains "$(cat "$d/other.txt")" "sample 0 64x48 0000000000000000"
	# The default path.
	h=$(make_scratch) || die "make_scratch failed"
	out=$(cd "$h" && D3D12_LINES=$d3d12_lines PIN_SAMPLE="${expected[0]}|${expected[1]}" \
		PIN_OVERBRIGHT="${expected_overbright[0]}|${expected_overbright[1]}" \
		PIN_FLASH="${expected_flash[0]}|${expected_flash[1]}|${expected_flash[2]}" \
		"$DEV_DIR/rtrace-d3d12.sh" "$d" 2>&1)
	assert_contains "$out" "rtrace-d3d12: passed"
	assert_contains "$(cat "$h/d3d12-frame-hashes.txt")" "flash 2 64x48 c33733e407fec502"
}

# Test that a trace that does not render all its frames fails, naming it:
# a missing line, an extra one, a malformed one, an index out of order, an
# empty log.
test_missing_or_short_frame_lines_fail()
{
	local d out lines
	d=$(fake_build)
	for lines in '0 64x48 86388f1d5003af2a' \
		"${expected[0]}|${expected[1]}|2 64x48 0123456789abcdef" \
		'0 64x48 86388f1d5003af2a|1 64x48 nothex' \
		'0 64x48 86388f1d5003af2a|garbage' \
		'1 64x48 9f40fbb36ef64325|0 64x48 86388f1d5003af2a' \
		''; do
		out=$(FAKE_LINES=$lines check --hashes "$d/h.txt" "$d")
		assert_contains "$out" "rtrace-d3d12: the sample trace did not render all its frames"
		assert_contains "$out" "status 1"
		assert_not_contains "$out" "rtrace-d3d12: passed"
	done
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
	d=$(make_scratch) || die "make_scratch failed"
	assert_contains "$(check)" "status 2"
	assert_contains "$(check '')" "status 2"
	assert_contains "$(check --bogus "$d")" "status 2"
	assert_contains "$(check --hashes)" "status 2"
	assert_contains "$(check "$d")" "rtrace-d3d12: no rtrace-gpu.exe in $d"
	assert_contains "$(check "$d")" "status 1"
}

run_tests
