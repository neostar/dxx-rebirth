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

# Test that clip-check.sh rejects unknown options, a bad --seconds, a
# missing binary and missing game data, before it runs anything.
test_clip_check_usage_errors()
{
	local d out rc
	d=$(make_scratch) || die "make_scratch failed"
	out=$("$DEV_DIR/clip-check.sh" --loud x 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 2 "$out"
	out=$("$DEV_DIR/clip-check.sh" --seconds 0 x 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 2 "$out"
	out=$("$DEV_DIR/clip-check.sh" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 2 "$out"
	out=$("$DEV_DIR/clip-check.sh" "$d/missing" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 2 "$out"
	printf '#!/bin/sh\nexit 0\n' > "$d/game"
	chmod +x "$d/game"
	out=$(DXX_D2_DATA="$d/nodata" "$DEV_DIR/clip-check.sh" "$d/game" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 2 "$out"
	assert_contains "$out" "DXX_D2_DATA"
	mkdir "$d/data"
	out=$(DXX_D2_DATA="$d/data" "$DEV_DIR/clip-check.sh" "$d/game" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 2 "$out"
	assert_contains "$out" "no CD music"
}

# Run clip-check.sh on a stand-in game in directory $1:
# the stand-in writes capture $1/<run>.raw (the stimulus run's when
# DXX_DBG_SOUND_STIMULUS is set) to the disk driver's file, or to
# DXX_DBG_AUDIO_RENDER's when that is set, prints line $2, and then waits
# for TERM, or with $3 = exit, exits 0 at once (and the runs last 30 s,
# which clip-check.sh cuts short when run.sh ends, so no TERM comes).  Fake game data and music; output
# in $1/out, status in $1/rc.  clip-check.sh's arguments before the game
# are those of the array standin_args when set, else --seconds and the
# runs' length.
standin_check()
{
	local d=$1 line=$2 ending=${3-wait} secs=2 args=()
	mkdir -p "$d/data/cdmusic/levels" "$d/reports"
	: > "$d/data/cdmusic/title.flac"
	{
		printf '#!/bin/sh\n'
		# A stubborn stand-in is deaf to TERM before its capture exists,
		# so clip-check.sh, which waits for the capture, cannot TERM it
		# early.
		if [[ $ending == stubborn ]]; then
			printf 'trap "" TERM\n'
		fi
		# The whole capture appears at once (written beside it, then
		# moved), before the stand-in does anything else.
		# shellcheck disable=SC2016  # expanded by the stand-in
		printf 'if [ -n "$DXX_DBG_SOUND_STIMULUS" ]; then src="%s/stimulus.raw" dst=$SDL_AUDIO_DISK_OUTPUT_FILE; elif [ -n "$DXX_DBG_AUDIO_RENDER" ]; then src="%s/demo.raw" dst=$DXX_DBG_AUDIO_RENDER; else src="%s/demo.raw" dst=$SDL_AUDIO_DISK_OUTPUT_FILE; fi\ncp "$src" "$dst.part" && mv "$dst.part" "$dst"\n' "$d" "$d" "$d"
		printf 'echo "%s"\n' "$line"
		if [[ $ending == exit ]]; then
			printf 'exit 0\n'
			secs=30
		elif [[ $ending == stubborn ]]; then
			# shellcheck disable=SC2016  # expanded by the stand-in
			printf 'i=0\nwhile [ $i -lt 100 ]; do sleep 0.1; i=$((i + 1)); done\n'
		else
			printf 'while :; do sleep 0.1; done\n'
		fi
	} > "$d/game"
	chmod +x "$d/game"
	if declare -p standin_args > /dev/null 2>&1; then
		args=("${standin_args[@]+"${standin_args[@]}"}")
	else
		args=(--seconds "$secs")
	fi
	DXX_D2_DATA="$d/data" CLIP_CHECK_STATE="$d/no-state" CLIP_CHECK_STIMULUS_SECONDS=1 \
		NO_WINDOW_FRONTMOST_CMD="$(fake_sampler "$d" '1 Finder')" NO_WINDOW_REPORTS_DIR="$d/reports" \
		"$DEV_DIR/clip-check.sh" "${args[@]+"${args[@]}"}" "$d/game" > "$d/out" 2>&1 && echo 0 > "$d/rc" || echo $? > "$d/rc"
}

# Write the stand-in's captures into $1: a demo peaking at $2 (a 16-bit
# value), $3 frames long (default 12100), and a stimulus of two equal bursts.
standin_captures()
{
	write_capture "$1/demo.raw" 0:100 "$2":$((${3-12100} - 100))
	write_capture "$1/stimulus.raw" 0:100 4000:50 0:20000 4000:50 0:100
}

# Test that a run passes on clean captures with margin, music playing, and
# the run ended by TERM (status 143).
test_clip_check_passes_a_clean_run()
{
	local d
	d=$(make_scratch) || die "make_scratch failed"
	standin_captures "$d" 4000
	standin_check "$d" "Now playing: title.flac"
	assert_eq "$(cat "$d/rc")" 0 "$(cat "$d/out")"
	assert_contains "$(cat "$d/out")" "clip-check.sh: passed"
}

# Test that a run that ends by itself, before its TERM, fails.
test_clip_check_fails_a_run_not_ended_by_term()
{
	local d
	d=$(make_scratch) || die "make_scratch failed"
	standin_captures "$d" 4000
	standin_check "$d" "Now playing: title.flac" exit
	assert_eq "$(cat "$d/rc")" 1 "$(cat "$d/out")"
	assert_contains "$(cat "$d/out")" "demo: the run ended before its TERM"
}

# Test that a run still going CLIP_CHECK_KILL_AFTER seconds after its TERM
# is killed and fails: run.sh, given 60 s of grace, is killed after 1, and
# its status (137) is not TERM's 143.  The stand-in, deaf to TERM, ends by
# itself after 10 s.
test_clip_check_kills_and_fails_a_run_that_outlives_its_term()
{
	local d
	d=$(make_scratch) || die "make_scratch failed"
	standin_captures "$d" 4000
	RUN_KILL_AFTER=60 CLIP_CHECK_KILL_AFTER=1 standin_check "$d" "Now playing: title.flac" stubborn
	assert_eq "$(cat "$d/rc")" 1 "$(cat "$d/out")"
	assert_contains "$(cat "$d/out")" "demo: run.sh exited 137"
}

# Test that a music run whose log never says "Now playing:" fails.
test_clip_check_fails_when_music_did_not_play()
{
	local d
	d=$(make_scratch) || die "make_scratch failed"
	standin_captures "$d" 4000
	standin_check "$d" "no music here"
	assert_eq "$(cat "$d/rc")" 1 "$(cat "$d/out")"
	assert_contains "$(cat "$d/out")" "the music did not play"
}

# Test that a demo peaking above the effects margin (0.8077) fails,
# though below -1 dB.
test_clip_check_fails_a_demo_without_margin()
{
	local d
	d=$(make_scratch) || die "make_scratch failed"
	standin_captures "$d" 28000
	standin_check "$d" "Now playing: title.flac"
	assert_eq "$(cat "$d/rc")" 1 "$(cat "$d/out")"
	assert_contains "$(cat "$d/out")" "demo: peak 0.8545 above 0.8077"
}

# Test that clip-check.sh refuses a --keep directory inside the repository.
test_clip_check_refuses_keep_inside_the_repository()
{
	local d out rc
	d=$(make_scratch) || die "make_scratch failed"
	printf '#!/bin/sh\nexit 0\n' > "$d/game"
	chmod +x "$d/game"
	mkdir -p "$d/data/cdmusic/levels"
	: > "$d/data/cdmusic/title.flac"
	out=$(DXX_D2_DATA="$d/data" "$DEV_DIR/clip-check.sh" --keep "$DEV_DIR/../../build/clip" "$d/game" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 2 "$out"
	assert_contains "$out" "inside the repository"
	[[ -e $DEV_DIR/../../build/clip ]] && fail "--keep created a directory inside the repository"
}

# Test that a rendered run (an SDL3 build) ends once a second of audio is
# written and passes, its capture cut to exactly that second, and that
# --keep keeps the captures of a passing run.
test_clip_check_renders_and_keeps_a_clean_run()
{
	local d
	d=$(make_scratch) || die "make_scratch failed"
	standin_captures "$d" 4000 50000
	standin_args=(--seconds 1 --keep "$d/keep")
	CLIP_CHECK_MODE=render standin_check "$d" "Now playing: title.flac"
	unset standin_args
	assert_eq "$(cat "$d/rc")" 0 "$(cat "$d/out")"
	assert_contains "$(cat "$d/out")" "clip-check.sh: demo runs: render, 1 s of audio"
	assert_contains "$(cat "$d/out")" "demo: samples=88200 clipped=0"
	assert_eq "$(wc -c < "$d/keep/demo.raw" | tr -d ' ')" 176400
	[[ -f $d/keep/demo-music.raw && -f $d/keep/stimulus.raw ]] || fail "--keep kept no captures"
}

# Test that a rendered run that stops writing short of its audio fails.
test_clip_check_fails_a_render_cut_short()
{
	local d
	d=$(make_scratch) || die "make_scratch failed"
	standin_captures "$d" 4000
	CLIP_CHECK_MODE=render CLIP_CHECK_RENDER_LIMIT=2 standin_check "$d" "Now playing: title.flac"
	assert_eq "$(cat "$d/rc")" 1 "$(cat "$d/out")"
	assert_contains "$(cat "$d/out")" "demo: 2 s of audio not rendered after 2 s"
}

# Test that a render cut short whose run ignores TERM is killed
# CLIP_CHECK_KILL_AFTER seconds later and fails, not left running.
test_clip_check_kills_a_short_render_that_outlives_its_term()
{
	local d
	d=$(make_scratch) || die "make_scratch failed"
	standin_captures "$d" 4000
	RUN_KILL_AFTER=60 CLIP_CHECK_KILL_AFTER=1 CLIP_CHECK_MODE=render CLIP_CHECK_RENDER_LIMIT=1 \
		standin_check "$d" "Now playing: title.flac" stubborn
	assert_eq "$(cat "$d/rc")" 1 "$(cat "$d/out")"
	assert_contains "$(cat "$d/out")" "demo: 2 s of audio not rendered after 1 s"
	assert_contains "$(cat "$d/out")" "demo: run.sh exited 137 after its TERM"
}

# Test the defaults: 120 s of audio and 30 s from TERM to KILL.  The
# stand-in ends at once, so the rendered runs fail fast.
test_clip_check_defaults()
{
	local d
	d=$(make_scratch) || die "make_scratch failed"
	standin_captures "$d" 4000
	standin_args=()
	CLIP_CHECK_MODE=render standin_check "$d" "Now playing: title.flac" exit
	unset standin_args
	assert_contains "$(cat "$d/out")" "clip-check.sh: demo runs: render, 120 s of audio, KILL 30 s after TERM"
}

# Test that a CLIP_CHECK_MODE other than render or disk is a usage error.
test_clip_check_rejects_an_unknown_mode()
{
	local d out rc
	d=$(make_scratch) || die "make_scratch failed"
	printf '#!/bin/sh\nexit 0\n' > "$d/game"
	chmod +x "$d/game"
	mkdir -p "$d/data/cdmusic/levels"
	: > "$d/data/cdmusic/title.flac"
	out=$(CLIP_CHECK_MODE=loud DXX_D2_DATA="$d/data" "$DEV_DIR/clip-check.sh" "$d/game" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 2 "$out"
	assert_contains "$out" "CLIP_CHECK_MODE"
}

run_tests
