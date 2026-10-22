#!/usr/bin/env bash
# Command-line behaviour of tools/dev/framehash_diff.py: compare a run's
# frame hash log with its baseline, frame by frame.  The logs here are
# synthetic; real ones derive from the licensed data and stay out of git.
source "$(dirname "$0")/lib.sh"
DIFF="$DEV_DIR/framehash_diff.py"

# Write a frame hash log to file $1: header $2, then one frame per hash in
# the remaining arguments; a "#..." argument starts a new section.
write_log()
{
	local f=$1 i=0 a
	shift
	: > "$f"
	for a in "$@"; do
		if [[ $a == \#* ]]; then
			printf '%s\n' "$a" >> "$f"
			i=0
		else
			printf '%d 640x480 %016x\n' "$i" "$a" >> "$f"
			i=$((i + 1))
		fi
	done
}

# Test that identical logs report every frame identical, with status 0.
test_identical_logs_pass()
{
	local d out rc
	d=$(make_scratch) || die "make_scratch failed"
	write_log "$d/base" '# scene s1.txt' 1 2 3
	write_log "$d/run" '# scene s1.txt' 1 2 3
	out=$(python3 "$DIFF" S1 "$d/base" "$d/run" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 0 "$out"
	assert_eq "$out" "S1: 3/3 frames identical"
}

# Test that a changed frame fails, counted out of the baseline's frames and
# named by its section and its index in that section.
test_changed_frame_reported()
{
	local d out rc
	d=$(make_scratch) || die "make_scratch failed"
	write_log "$d/base" '# no demo' 1 2 '# scene s1.txt' 3 4 5 6
	write_log "$d/run" '# no demo' 1 2 '# scene s1.txt' 3 4 9 9
	out=$(python3 "$DIFF" S1 "$d/base" "$d/run" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_eq "$out" "S1: 4/6 frames identical; first difference: section '# scene s1.txt', frame 2"
}

# Test that frames the run lacks or adds fail at the first such frame.
test_missing_and_extra_frames_reported()
{
	local d out rc
	d=$(make_scratch) || die "make_scratch failed"
	write_log "$d/base" '# scene s3.txt' 1 2 3
	write_log "$d/run" '# scene s3.txt' 1 2
	out=$(python3 "$DIFF" S3 "$d/base" "$d/run" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_eq "$out" "S3: 2/3 frames identical; first difference: section '# scene s3.txt', frame 2"
	out=$(python3 "$DIFF" S3 "$d/run" "$d/base" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_eq "$out" "S3: 2/2 frames identical; first difference: section '# scene s3.txt', frame 2"
}

# Test that a section whose header differs fails at its first frame, and
# that its frames count as differing.
test_changed_section_reported()
{
	local d out rc
	d=$(make_scratch) || die "make_scratch failed"
	write_log "$d/base" '# scene s4.txt' 1 '# no demo' 2 3
	write_log "$d/run" '# scene s4.txt' 1 '# demo playback a.dem' 2 3
	out=$(python3 "$DIFF" S4 "$d/base" "$d/run" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_eq "$out" "S4: 1/3 frames identical; first difference: section '# no demo', frame 0"
}

# Test that a file that is not a frame hash log, a missing file and wrong
# arguments exit 2.
test_bad_input_exits_2()
{
	local d out rc
	d=$(make_scratch) || die "make_scratch failed"
	printf '0 640x480 0000000000000001\n' > "$d/headless"
	write_log "$d/ok" '# no demo' 1
	out=$(python3 "$DIFF" S1 "$d/headless" "$d/ok" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 2 "$out"
	assert_contains "$out" "headless:1: not a frame hash log line"
	out=$(python3 "$DIFF" S1 "$d/ok" "$d/absent" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 2 "$out"
	out=$(python3 "$DIFF" S1 "$d/ok" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 2 "$out"
	assert_contains "$out" "usage:"
}

# Test that --section compares only the first section whose header starts
# with its prefix and that a later header closes, so a demo run ended by
# TERM compares its first whole demo, not the wall-clock title screens
# around it, a later demo or a section the end cut.
test_section_selects_closed_sections()
{
	local d out rc
	d=$(make_scratch) || die "make_scratch failed"
	write_log "$d/base" '# no demo' 1 2 '# demo playback a.dem' 5 6 7 '# no demo' 8 \
		'# demo playback a.dem' 5
	write_log "$d/run" '# no demo' 1 '# demo playback a.dem' 5 6 7 '# no demo' \
		'# demo playback a.dem' 5 6 '# no demo'
	out=$(python3 "$DIFF" --section '# demo playback' S2 "$d/base" "$d/run" 2>&1) &&
		rc=0 || rc=$?
	assert_eq "$rc" 0 "$out"
	assert_eq "$out" "S2: 3/3 frames identical"
	write_log "$d/run" '# demo playback a.dem' 5 6 9 '# no demo'
	out=$(python3 "$DIFF" --section '# demo playback' S2 "$d/base" "$d/run" 2>&1) &&
		rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_eq "$out" "S2: 2/3 frames identical; first difference: section '# demo playback a.dem', frame 2"
}

# Test that a --section selecting no closed section in a log fails, so a
# run that never finished its demo does not pass on zero frames.
test_section_with_no_closed_section_fails()
{
	local d out rc
	d=$(make_scratch) || die "make_scratch failed"
	write_log "$d/base" '# demo playback a.dem' 5 6 '# no demo'
	write_log "$d/run" '# no demo' 1 '# demo playback a.dem' 5 6
	out=$(python3 "$DIFF" --section '# demo playback' S2 "$d/base" "$d/run" 2>&1) &&
		rc=0 || rc=$?
	assert_eq "$rc" 2 "$out"
	assert_contains "$out" "no closed '# demo playback' section"
}

# Test that a section the run lacks at the end, or adds there, fails at
# its first frame, so a run cut short or running on cannot pass.
test_missing_and_extra_sections_reported()
{
	local d out rc
	d=$(make_scratch) || die "make_scratch failed"
	write_log "$d/base" '# scene s1.txt' 1 2 '# scene s3.txt' 3
	write_log "$d/run" '# scene s1.txt' 1 2
	out=$(python3 "$DIFF" S1 "$d/base" "$d/run" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_eq "$out" "S1: 2/3 frames identical; first difference: section '# scene s3.txt', frame 0"
	out=$(python3 "$DIFF" S1 "$d/run" "$d/base" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_eq "$out" "S1: 2/2 frames identical; first difference: section '# scene s3.txt', frame 0"
}

# Test that a frame differing only in its index or picture size fails:
# the whole frame line is compared, not only its hash.
test_whole_frame_line_compared()
{
	local d out rc
	d=$(make_scratch) || die "make_scratch failed"
	write_log "$d/base" '# scene s1.txt' 1 2
	printf '# scene s1.txt\n0 640x480 %016x\n1 320x200 %016x\n' 1 2 > "$d/size"
	printf '# scene s1.txt\n0 640x480 %016x\n7 640x480 %016x\n' 1 2 > "$d/index"
	for f in size index; do
		out=$(python3 "$DIFF" S1 "$d/base" "$d/$f" 2>&1) && rc=0 || rc=$?
		assert_eq "$rc" 1 "$f: $out"
		assert_eq "$out" "S1: 1/2 frames identical; first difference: section '# scene s1.txt', frame 1"
	done
}

run_tests
