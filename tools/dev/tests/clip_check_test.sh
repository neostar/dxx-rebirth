#!/usr/bin/env bash
# Command-line behaviour of tools/dev/clip_check.py, which counts the
# clipped samples of a capture for clip-check.sh, and clip-check.sh itself
# with a stand-in game.  The captures here are synthetic; real ones derive from the
# licensed data and stay out of git.
source "$(dirname "$0")/lib.sh"
CHECK="$DEV_DIR/clip_check.py"

# Write S16 little-endian stereo frames to file $1: for each remaining
# argument VALUE:COUNT, COUNT frames with VALUE on both sides.
write_capture()
{
	python3 - "$@" <<'EOF'
import struct, sys
with open(sys.argv[1], "wb") as f:
    for part in sys.argv[2:]:
        value, count = (int(x) for x in part.split(":"))
        f.write(struct.pack("<hh", value, value) * count)
EOF
}

# Test that a capture below full scale passes with its peak and margin.
test_clean_capture_passes()
{
	local d out rc
	d=$(make_scratch) || die "make_scratch failed"
	write_capture "$d/c.raw" 0:10 16384:5 -8192:5
	out=$(python3 "$CHECK" demo demo "$d/c.raw" 16 1 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 0 "$out"
	assert_eq "$out" "demo: samples=40 clipped=0 peak=0.5000 margin=6.02 dB"
}

# Test that samples at either limit of the format count as clipped and fail.
test_clipped_samples_fail()
{
	local d out rc
	d=$(make_scratch) || die "make_scratch failed"
	write_capture "$d/c.raw" 100:10 32767:2 -32768:1
	out=$(python3 "$CHECK" demo demo "$d/c.raw" 16 1 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "clipped=6"
}

# Test that a missing, short, odd-length or silent capture is an error (2).
test_bad_captures_are_errors()
{
	local d out rc
	d=$(make_scratch) || die "make_scratch failed"
	out=$(python3 "$CHECK" demo demo "$d/none.raw" 16 1 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 2 "$out"
	write_capture "$d/short.raw" 100:2
	out=$(python3 "$CHECK" demo demo "$d/short.raw" 16 1 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 2 "$out"
	printf 'abc' > "$d/odd.raw"
	out=$(python3 "$CHECK" demo demo "$d/odd.raw" 1 1 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 2 "$out"
	write_capture "$d/silent.raw" 0:100
	out=$(python3 "$CHECK" demo demo "$d/silent.raw" 16 1 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 2 "$out"
	out=$(python3 "$CHECK" demo demo "$d/silent.raw" many 1 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 2 "$out"
	out=$(python3 "$CHECK" loud demo "$d/silent.raw" 16 1 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 2 "$out"
}

# Test that a peak above MAX_PEAK fails, though nothing clipped, and that a
# MAX_PEAK outside 0..1 is an error.
test_peak_above_the_margin_fails()
{
	local d out rc
	d=$(make_scratch) || die "make_scratch failed"
	write_capture "$d/c.raw" 0:10 26000:5
	out=$(python3 "$CHECK" demo demo "$d/c.raw" 16 0.8077 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 0 "$out"
	write_capture "$d/c.raw" 0:10 27000:5
	out=$(python3 "$CHECK" demo demo "$d/c.raw" 16 0.8077 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "demo: peak 0.8240 above 0.8077"
	out=$(python3 "$CHECK" demo demo "$d/c.raw" 16 0 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 2 "$out"
}

# Test that the stimulus passes with two bursts, the second no louder.
test_stimulus_with_equal_bursts_passes()
{
	local d out rc
	d=$(make_scratch) || die "make_scratch failed"
	write_capture "$d/s.raw" 0:100 4000:50 0:20000 4000:50 0:100
	out=$(python3 "$CHECK" stimulus stimulus "$d/s.raw" 16 1 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 0 "$out"
	assert_contains "$out" "stimulus: burst peaks 0.1221 / 0.1221"
}

# Test that a louder second burst (a volume above F1_0 louder than full
# scale), or a burst count other than two, fails.
test_stimulus_louder_or_missing_burst_fails()
{
	local d out rc
	d=$(make_scratch) || die "make_scratch failed"
	write_capture "$d/s.raw" 0:100 4000:50 0:20000 12000:50 0:100
	out=$(python3 "$CHECK" stimulus stimulus "$d/s.raw" 16 1 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "the F3_0 burst peaks above the F1_0 one"
	write_capture "$d/one.raw" 0:100 4000:50 0:100
	out=$(python3 "$CHECK" stimulus stimulus "$d/one.raw" 16 1 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "expected 2 bursts, found 1"
}

run_tests
