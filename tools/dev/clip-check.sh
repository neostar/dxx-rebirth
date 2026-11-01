#!/usr/bin/env bash
# Check that the game's sound never clips, with margin (#104): capture the D2
# autodemo loop at full sliders, without and with music, plus the crafted
# stimulus of a sound above full scale, and check every capture's clipped
# samples and peak.
#
#   clip-check.sh [--seconds N] [--keep DIR] BINARY
#
# BINARY is a D2X built with the debug harness (build.sh dev dbg_harness=1;
# SDL2 or sdl3=1): the runs are hidden (DXX_DBG_HIDDEN) under run.sh, so
# silent, each with a fresh D2X_REBIRTH_HOME holding a descent.cfg at both
# sliders 8, the defaults and the loudest setting.  Three runs:
#   demo        -autodemo -nomusic, N seconds (default 120);
#   demo-music  -autodemo with the CD music of $DXX_D2_DATA/cdmusic (custom
#               music, title.flac and levels/), N seconds;
#   stimulus    -nomusic at the menu, DXX_DBG_SOUND_STIMULUS=1
#               (common/debug/sound_stimulus.h): one sound at F1_0, then the
#               same at F3_0, 20 seconds (CLIP_CHECK_STIMULUS_SECONDS).
# How the demo runs capture depends on the build (CLIP_CHECK_MODE overrides
# it: render or disk):
#   - SDL3 (a BINARY linked to libSDL3): rendered, deterministic.
#     DXX_DBG_AUDIO_RENDER (common/debug/audio_render.h) has each game frame
#     render its audio to the capture, with no device, at
#     DXX_DBG_FIXED_FRAMETIME=30; the run ends once N seconds of audio
#     (44100 Hz S16 stereo) are written, and the capture is cut to exactly
#     that, so two runs write the same bytes.  A run that has not written
#     them CLIP_CHECK_RENDER_LIMIT seconds (default 3 N + 60) after its
#     capture started fails.
#   - SDL2: SDL's disk audio driver, in real time, N seconds of wall clock
#     from the capture's start; its peaks vary from run to run with the
#     frame timing.
# The stimulus always runs on the disk driver.  A run with no capture
# CLIP_CHECK_START_LIMIT seconds (default 60) after its launch fails.
# Each run ends by TERM to run.sh, so its status must be 143 (128 + TERM,
# what run.sh returns for a run it ended on TERM); any other status, a new
# crash report among them (run.sh's 1), fails it, as do a run that ended
# before the TERM and one killed CLIP_CHECK_KILL_AFTER seconds (default 30)
# after it, still going.
#
# tools/dev/clip_check.py reads each capture and prints its numbers:
# samples, clipped samples, peak as a share of full scale, its margin in
# dB.  Exit 0 only when every run passes:
#   - no clipped sample, and every peak at most -1 dB (0.8913);
#   - the demo without music peaks at most 1 dB below what music at full
#     scale leaves the effects: (1 - 3/32) at -1 dB, 0.8077, 3/32 being the
#     music gain of common/main/digi_policy.h;
#   - the music run's log says "Now playing:", so music played;
#   - the F3_0 burst peaks no higher than the F1_0 one.
# Exit 1 on a failed check or run, 2 on a usage error or a missing input:
# DXX_D2_DATA unset, its cdmusic missing, BINARY not executable, a --keep
# DIR inside the repository, a CLIP_CHECK_MODE other than render or disk.
#
# The captures derive from the licensed game data (game-data.md): they go
# to a temporary directory removed at exit, or with --keep to DIR, which
# must lie outside the repository, kept for listening.  Report numbers
# only.  Waits for the file CLIP_CHECK_STATE (default
# ~/Developer/dxx-rebirth-wt/machine.state) to read "gate: idle" when it
# exists: a merge gate runs alone.
set -u
dev_dir=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$dev_dir/../.." && pwd -P)
source "$dev_dir/env.sh"

usage() { echo "usage: $0 [--seconds N] [--keep DIR] BINARY" >&2; exit 2; }
seconds=120
keep=
while (($#)); do
	case $1 in
	--seconds)
		[[ ${2-} =~ ^[1-9][0-9]*$ ]] || usage
		seconds=$2
		shift 2
		;;
	--keep)
		[[ -n ${2-} ]] || usage
		keep=$2
		shift 2
		;;
	--*) usage ;;
	*) break ;;
	esac
done
(($# == 1)) || usage
binary=$1
stimulus_seconds=${CLIP_CHECK_STIMULUS_SECONDS:-20}
kill_after=${CLIP_CHECK_KILL_AFTER:-30}
render_limit=${CLIP_CHECK_RENDER_LIMIT:-$((3 * seconds + 60))}
start_limit=${CLIP_CHECK_START_LIMIT:-60}
[[ $stimulus_seconds =~ ^[1-9][0-9]*$ && $kill_after =~ ^[1-9][0-9]*$ && $render_limit =~ ^[1-9][0-9]*$ &&
	$start_limit =~ ^[1-9][0-9]*$ ]] ||
	{ echo "clip-check.sh: CLIP_CHECK_STIMULUS_SECONDS, CLIP_CHECK_KILL_AFTER, CLIP_CHECK_RENDER_LIMIT and CLIP_CHECK_START_LIMIT take whole seconds" >&2; exit 2; }
[[ -x $binary && -f $binary ]] || { echo "clip-check.sh: not an executable file: $binary" >&2; exit 2; }
binary=$(cd "$(dirname "$binary")" && pwd)/$(basename "$binary")
mode=${CLIP_CHECK_MODE-}
if [[ -z $mode ]]; then
	if { otool -L "$binary" 2> /dev/null || ldd "$binary" 2> /dev/null; } | grep -q 'libSDL3[.-]'; then
		mode=render
	else
		mode=disk
	fi
fi
[[ $mode == render || $mode == disk ]] ||
	{ echo "clip-check.sh: CLIP_CHECK_MODE is render or disk, not '$mode'" >&2; exit 2; }
render_bytes=$((seconds * 44100 * 4))
[[ -n ${DXX_D2_DATA-} && -d ${DXX_D2_DATA-} ]] ||
	{ echo "clip-check.sh: DXX_D2_DATA does not name the D2 data directory" >&2; exit 2; }
music=$DXX_D2_DATA/cdmusic
[[ -f $music/title.flac && -d $music/levels ]] ||
	{ echo "clip-check.sh: no CD music in $music (title.flac, levels/)" >&2; exit 2; }

if [[ -n $keep ]]; then
	resolved=$(python3 -c 'import os, sys; print(os.path.realpath(sys.argv[1]))' "$keep") || exit 2
	if [[ $resolved == "$root" || $resolved == "$root"/* ]]; then
		echo "clip-check.sh: --keep $keep lies inside the repository; the captures derive from licensed data" >&2
		exit 2
	fi
	mkdir -p "$resolved" || exit 2
	out=$resolved
else
	out=$(mktemp -d "${TMPDIR:-/tmp}/clip-check.XXXXXX") || exit 2
	trap 'rm -rf "$out"' EXIT
fi

state=${CLIP_CHECK_STATE:-$HOME/Developer/dxx-rebirth-wt/machine.state}
if [[ -f $state ]]; then
	"$dev_dir/wait-for.sh" "$state" 3600 'gate: idle' > /dev/null ||
		{ echo "clip-check.sh: the merge gate still runs ($state)" >&2; exit 1; }
fi

# Write descent.cfg into home $1; $2 = music or none.
write_cfg()
{
	{
		echo "DigiVolume=8"
		echo "MusicVolume=8"
		if [[ $2 == music ]]; then
			echo "MusicType=3"
			echo "CMLevelMusicPath=$music/levels/"
			echo "CMLevelMusicPlayOrder=0"
			for i in 0 1 2 3 4; do
				echo "CMMiscMusic$i=$music/title.flac"
			done
		fi
	} > "$1/descent.cfg"
}

# The size of file $1 in bytes, 0 when it does not exist.
size_of() { [[ -f $1 ]] && wc -c < "$1" | tr -d ' ' || echo 0; }

# Send run.sh (pid $1) TERM and wait for it; KILL it CLIP_CHECK_KILL_AFTER
# seconds later if it still runs.  Returns run.sh's status.
stop_run()
{
	local pid=$1 rc watchdog
	kill -TERM "$pid" 2> /dev/null
	(sleep "$kill_after"; kill -KILL "$pid" 2> /dev/null) &
	watchdog=$!
	wait "$pid" && rc=0 || rc=$?
	kill "$watchdog" 2> /dev/null
	wait "$watchdog" 2> /dev/null
	return "$rc"
}

# Play run $1 for $2 seconds, on the disk driver or rendered ($3, disk or
# render), with extra arguments $4...; the capture is $out/$1.raw.  Fails on
# a run that did not end as TERM ends it.
play()
{
	local name=$1 secs=$2 how=$3 home=$out/$1.home pid rc start
	local capture=$out/$1.raw
	shift 3
	rm -rf "$home" "$capture"
	mkdir -p "$home" || return 1
	write_cfg "$home" "$([[ $name == demo-music ]] && echo music || echo none)" || return 1
	if [[ $how == render ]]; then
		SDL_AUDIO_DRIVER=dummy DXX_DBG_AUDIO_RENDER=$capture DXX_DBG_FIXED_FRAMETIME=30 DXX_DBG_HIDDEN=1 \
			D2X_REBIRTH_HOME=$home "$dev_dir/run.sh" "$binary" -hogdir "$DXX_D2_DATA" \
			-window -pilot harness -nomovies "$@" > "$out/$name.out" 2> "$out/$name.err" &
	else
		SDL_AUDIO_DRIVER=disk SDL_AUDIO_DISK_OUTPUT_FILE=$capture DXX_DBG_HIDDEN=1 \
			D2X_REBIRTH_HOME=$home "$dev_dir/run.sh" "$binary" -hogdir "$DXX_D2_DATA" \
			-window -pilot harness -nomovies "$@" > "$out/$name.out" 2> "$out/$name.err" &
	fi
	pid=$!
	# Every limit counts from the capture's start (the game opens audio, or
	# its render file, at startup), not from the launch, however slowly the
	# game starts.
	start=$SECONDS
	while [[ ! -e $capture ]] && kill -0 "$pid" 2> /dev/null &&
		((SECONDS - start < start_limit)); do
		sleep 1
	done
	if [[ ! -e $capture ]] && kill -0 "$pid" 2> /dev/null; then
		echo "clip-check.sh: $name: no capture after $start_limit s" >&2
		stop_run "$pid" && rc=0 || rc=$?
		((rc == 143)) || echo "clip-check.sh: $name: run.sh exited $rc after its TERM" >&2
		return 1
	fi
	if [[ $how == render ]]; then
		start=$SECONDS
		while (($(size_of "$capture") < render_bytes)) && kill -0 "$pid" 2> /dev/null &&
			((SECONDS - start < render_limit)); do
			sleep 1
		done
		if (($(size_of "$capture") < render_bytes)) && kill -0 "$pid" 2> /dev/null; then
			echo "clip-check.sh: $name: $secs s of audio not rendered after $render_limit s" >&2
			stop_run "$pid" && rc=0 || rc=$?
			((rc == 143)) || echo "clip-check.sh: $name: run.sh exited $rc after its TERM" >&2
			return 1
		fi
	else
		start=$SECONDS
		while ((SECONDS - start < secs)) && kill -0 "$pid" 2> /dev/null; do
			sleep 1
		done
	fi
	if ! kill -0 "$pid" 2> /dev/null; then
		wait "$pid" && rc=0 || rc=$?
		echo "clip-check.sh: $name: the run ended before its TERM, run.sh status $rc (see $out/$name.err)" >&2
		return 1
	fi
	stop_run "$pid" && rc=0 || rc=$?
	if ((rc != 143)); then
		echo "clip-check.sh: $name: run.sh exited $rc (see $out/$name.err)" >&2
		return 1
	fi
	if [[ $how == render ]]; then
		python3 -c 'import sys; p, n = sys.argv[1], int(sys.argv[2]); d = open(p, "rb").read(n); open(p, "wb").write(d)' \
			"$capture" "$render_bytes" || return 1
	fi
}

# Half the bytes the run's seconds hold at the lowest rate (11025 Hz S16
# stereo): a shorter capture means the audio stopped early.  A rendered
# capture holds them exactly.
min_bytes() { [[ $2 == render ]] && echo "$render_bytes" || echo $(($1 * 11025 * 4 / 2)); }

# Every peak at most -1 dB; the effects alone at most 1 dB below what the
# music gain (3/32, digi_policy.h) leaves them.
run_max=0.8913
effects_max=0.8077

rc=0
echo "clip-check.sh: demo runs: $mode, $seconds s of audio, KILL $kill_after s after TERM"
play demo "$seconds" "$mode" -autodemo -nomusic &&
	python3 "$dev_dir/clip_check.py" demo demo "$out/demo.raw" "$(min_bytes "$seconds" "$mode")" "$effects_max" || rc=1
if play demo-music "$seconds" "$mode" -autodemo; then
	if ! grep -q "Now playing:" "$out/demo-music.out" "$out/demo-music.err"; then
		echo "clip-check.sh: demo-music: no \"Now playing:\" line; the music did not play" >&2
		rc=1
	fi
	python3 "$dev_dir/clip_check.py" demo demo-music "$out/demo-music.raw" "$(min_bytes "$seconds" "$mode")" "$run_max" || rc=1
else
	rc=1
fi
DXX_DBG_SOUND_STIMULUS=1 play stimulus "$stimulus_seconds" disk -nomusic &&
	python3 "$dev_dir/clip_check.py" stimulus stimulus "$out/stimulus.raw" "$(min_bytes "$stimulus_seconds" disk)" "$run_max" || rc=1
if ((rc == 0)); then
	echo "clip-check.sh: passed"
else
	echo "clip-check.sh: failed"
fi
exit "$rc"
