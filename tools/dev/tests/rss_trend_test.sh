#!/usr/bin/env bash
# Command-line behaviour of tools/dev/rss_trend.py, the endurance run's
# check that a process's memory stays flat, and of footprint.py, its
# sampler (nightly.sh, ADR 5).
source "$(dirname "$0")/lib.sh"
TREND=("$(command -v python3)" "$DEV_DIR/rss_trend.py")

# Write samples "SECONDS RSS_KIB" to file $1, one every 10 s for $2
# seconds: RSS starts at 200000 KiB and grows by $3 KiB per hour, plus
# $4 KiB of alternating noise.
samples()
{
	awk -v n="$2" -v g="$3" -v z="$4" 'BEGIN {
		for (t = 0; t <= n; t += 10)
			printf "%d %d\n", t, 200000 + g * t / 3600 + ((t / 10) % 2 ? z : -z)
	}' > "$1"
}

# Test that a flat run with noise passes and says so.
test_flat_samples_pass()
{
	local d out
	d=$(make_scratch) || die "make_scratch failed"
	samples "$d/rss" 3600 0 3000
	out=$("${TREND[@]}" "$d/rss" 2>&1)
	assert_eq "$?" 0 "$out"
	assert_contains "$out" "flat"
}

# Test that RSS growing by 64 MiB an hour fails against the default limit,
# naming the slope.
test_growing_samples_fail()
{
	local d out rc
	d=$(make_scratch) || die "make_scratch failed"
	samples "$d/rss" 3600 65536 0
	out=$("${TREND[@]}" "$d/rss" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "growing"
	assert_contains "$out" "64.0 MiB/h"
}

# Test that the warm-up is left out: a jump in the first minutes (levels
# and sounds loading) does not count as growth.
test_warmup_is_left_out()
{
	local d out
	d=$(make_scratch) || die "make_scratch failed"
	samples "$d/rss" 600 0 0
	awk '$1 < 60 { $2 = 50000 } { print }' "$d/rss" > "$d/rss2"
	out=$("${TREND[@]}" --warmup 60 "$d/rss2" 2>&1)
	assert_eq "$?" 0 "$out"
}

# Test that --max-slope sets the limit in MiB per hour.
test_max_slope_option()
{
	local d out rc
	d=$(make_scratch) || die "make_scratch failed"
	samples "$d/rss" 3600 4096 0
	"${TREND[@]}" "$d/rss" >/dev/null 2>&1 || fail "4 MiB/h failed the default limit"
	out=$("${TREND[@]}" --max-slope 2 "$d/rss" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
}

# Test that too few samples after the warm-up fail rather than pass.
test_too_few_samples_fail()
{
	local d out rc
	d=$(make_scratch) || die "make_scratch failed"
	samples "$d/rss" 200 0 0
	out=$("${TREND[@]}" "$d/rss" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "too few samples"
}

# Test that --min-samples sets how many samples are enough.
test_min_samples_option()
{
	local d out
	d=$(make_scratch) || die "make_scratch failed"
	samples "$d/rss" 200 0 0
	out=$("${TREND[@]}" --warmup 120 --window 20 --min-samples 5 "$d/rss" 2>&1)
	assert_eq "$?" 0 "$out"
}

# Test that a malformed line is an error naming it.
test_malformed_line_fails()
{
	local d out rc
	d=$(make_scratch) || die "make_scratch failed"
	samples "$d/rss" 600 0 0
	echo "oops" >> "$d/rss"
	out=$("${TREND[@]}" "$d/rss" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 2 "$out"
	assert_contains "$out" "oops"
}

# Test that a sawtooth (RSS rising through each demo loop and falling at
# the next) is flat: the trend is fitted to each window's minimum, so
# where a short run ends in the loop does not decide.
test_sawtooth_is_flat()
{
	local d out
	d=$(make_scratch) || die "make_scratch failed"
	awk 'BEGIN { for (t = 0; t <= 290; t += 5) printf "%d %d\n", t, 163840 + 10240 * ((t % 50) / 45) }' \
		> "$d/rss"
	out=$("${TREND[@]}" --warmup 0 --window 50 "$d/rss" 2>&1)
	assert_eq "$?" 0 "$out"
	out=$("${TREND[@]}" --warmup 0 --window 50 --max-slope 1 "$d/rss" 2>&1)
	assert_eq "$?" 0 "$out"
}

# Test that a single drop in memory does not hide a leak: 16 MiB/h with a
# 22 MiB drop a third of the way in, which a least-squares line through
# the window minima reads as falling.
test_step_down_does_not_hide_leak()
{
	local d out rc
	d=$(make_scratch) || die "make_scratch failed"
	awk 'BEGIN { for (t = 0; t <= 3900; t += 10)
		printf "%d %d\n", t, 200000 + 16384 * t / 3600 - (t >= 1500 ? 22528 : 0) }' > "$d/rss"
	out=$("${TREND[@]}" "$d/rss" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "growing"
}

# Test that a single drop with no leak is flat.
test_step_down_alone_is_flat()
{
	local d out
	d=$(make_scratch) || die "make_scratch failed"
	awk 'BEGIN { for (t = 0; t <= 3900; t += 10)
		printf "%d %d\n", t, 200000 - (t >= 1500 ? 22528 : 0) }' > "$d/rss"
	out=$("${TREND[@]}" "$d/rss" 2>&1)
	assert_eq "$?" 0 "$out"
}

# Test that a sawtooth on top of a leak still fails: the leak shows in the
# minima.
test_sawtooth_with_leak_fails()
{
	local d out rc
	d=$(make_scratch) || die "make_scratch failed"
	awk 'BEGIN { for (t = 0; t <= 3900; t += 5)
		printf "%d %d\n", t, 163840 + 16384 * t / 3600 + 10240 * ((t % 50) / 45) }' > "$d/rss"
	out=$("${TREND[@]}" "$d/rss" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "growing"
}

# Test that the default warm-up is 300 s: memory still rising through the
# first five minutes (the first demos loading) does not count.
test_default_warmup()
{
	local d out rc
	d=$(make_scratch) || die "make_scratch failed"
	awk 'BEGIN { for (t = 0; t <= 600; t += 10)
		printf "%d %d\n", t, 200000 + (t < 300 ? 100 * t : 30000) }' > "$d/rss"
	out=$("${TREND[@]}" "$d/rss" 2>&1)
	assert_eq "$?" 0 "$out"
	out=$("${TREND[@]}" --warmup 0 "$d/rss" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
}

# Test that footprint.py, the endurance run's sampler, reports a process's
# memory in KiB, 64 MiB written counting, and fails for a process gone.
test_footprint_reports_memory()
{
	local d pid kib out rc
	d=$(make_scratch) || die "make_scratch failed"
	python3 -c 'import sys, time
b = bytearray(64 << 20)
for i in range(0, len(b), 4096): b[i] = 1
open(sys.argv[1], "w").close()
time.sleep(30)' "$d/ready" &
	pid=$!
	for _ in $(seq 100); do [[ -e $d/ready ]] && break; sleep 0.1; done
	kib=$(python3 "$DEV_DIR/footprint.py" "$pid")
	kill "$pid"; wait "$pid" 2>/dev/null
	[[ $kib =~ ^[0-9]+$ && $kib -ge 65536 ]] || fail "footprint of 64 MiB written: '$kib' KiB"
	out=$(python3 "$DEV_DIR/footprint.py" "$pid" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "cannot read process $pid"
}

run_tests
