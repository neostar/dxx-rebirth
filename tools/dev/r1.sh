#!/usr/bin/env bash
# R1, the GL picture stays bit-identical: play scenes S1-S4 with no window
# and compare every frame's hash with a baseline (framehash_diff.py).
#
#   r1.sh capture [--base REV]   build REV (default sdl-gpu-port), play the
#                                scenes, keep their logs as the baseline
#   r1.sh check                  build the working tree, play the scenes,
#                                compare them with the baseline
#
# Prints one "NAME: N/N frames identical" line per scene and fps, or the
# first differing frame (section, frame index); exit 0 only when every
# frame of every scene matches.  DXX_D2_DATA names the game data
# (docs/agents/game-data.md); R1_FPS the -maxfps values (default "60 200");
# R1_DIR the directory of baseline, runs and builds (default
# traces/local/r1, kept out of git: the logs derive from licensed data);
# R1_TIMEOUT the seconds a run may take (default 900).
# baseline/commit records the commit a baseline was built from.
#
# capture builds a `git archive` copy of REV in R1_DIR/src; the scene
# scripts always come from the working tree, so both sides play the same
# input.  REV needs the harness's input scripts (DXX_DBG_INPUT_SCRIPT).
# The build is build.sh's dev profile, hardened standard library included
# (ADR 5), with the hidden-run harness (game-data.md).
#
# S1, S3 and S4 are scripted scenes (common/debug/scenes) on the harness
# clock, which advances per timer_update call, not per frame: their logs
# compare only at the same -maxfps, so each fps has its own baseline.  S2
# stands in for the coverage demo with the installed demo (-autodemo): its
# first whole demo is compared with the baseline and across the fps
# values, as a demo plays the same frames at any frame rate.  Every run is
# hidden (DXX_DBG_HIDDEN) under run.sh, with a fresh D2X_REBIRTH_HOME.
set -u
dev_dir=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$dev_dir/../.." && pwd)
source "$dev_dir/env.sh"
# env.sh's sanitizer settings, or the caller's, with abort_on_error=0 last
# (CLAUDE.md): an abort leaves a crash report.
export ASAN_OPTIONS=$ASAN_OPTIONS:abort_on_error=0 UBSAN_OPTIONS=$UBSAN_OPTIONS:abort_on_error=0

usage() { echo "usage: $0 capture [--base REV] | check" >&2; exit 2; }
mode=${1-}
shift || true
base=sdl-gpu-port
case $mode in
capture) [[ $# -eq 0 ]] || { [[ $# -eq 2 && $1 == --base ]] && base=$2; } || usage ;;
check) [[ $# -eq 0 ]] || usage ;;
*) usage ;;
esac
data=${DXX_D2_DATA-}
[[ -n $data && -f $data/DESCENT2.HOG ]] || { echo "r1.sh: DXX_D2_DATA must name the directory of DESCENT2.HOG" >&2; exit 2; }
r1=${R1_DIR:-$root/traces/local/r1}
mkdir -p "$r1" && r1=$(cd "$r1" && pwd)
read -r -a fps_list <<<"${R1_FPS:-60 200}"
scenes=(S1:s1-title-menu S2:demo S3:s3-briefing S4:s4-automap-cockpit)
demo_section='# demo playback'

# Build the harness build in source tree $1; its log goes to $r1/build.log.
build()
{
	echo "r1.sh: building $1"
	(cd "$1" && tools/dev/build.sh dev dbg_harness=1 builddir=build/r1 \
		compilation_database=build/r1/compile_commands.json) > "$r1/build.log" 2>&1 ||
		{ echo "r1.sh: build failed, see $r1/build.log" >&2; exit 1; }
}

# Succeed when log $1 holds a demo section that a later header closes.
demo_closed()
{
	awk -v p="$demo_section" 'index($0, p) == 1 { d = 1; next } d && /^# / { f = 1 } END { exit !f }' "$1" 2>/dev/null
}

# Play scene $2 (a script name, or demo) at -maxfps $3 with binary $1 into
# log $4.  A demo run ends by TERM once its first demo is whole.
play()
{
	local bin=$1 scene=$2 fps=$3 log=$4 home=${4%.log}.home pid rc
	local start=$SECONDS limit=${R1_TIMEOUT:-900}
	local env=(DXX_DBG_HIDDEN=1 DXX_DBG_FIXED_FRAMETIME=30 DXX_DBG_FRAMEHASH="$log"
		D2X_REBIRTH_HOME="$home")
	local args=(-hogdir "$data" -window -pilot harness -nomovies -maxfps "$fps")
	if [[ $scene == demo ]]; then
		# The demo goes on after TERM; its frames are written by then.
		args+=(-autodemo)
		env+=(RUN_KILL_AFTER=3)
	else
		env+=(DXX_DBG_INPUT_SCRIPT="$root/common/debug/scenes/$scene.txt")
	fi
	rm -rf "$home" "$log" && mkdir -p "$home" || return 1
	env "${env[@]}" "$dev_dir/run.sh" "$bin" "${args[@]}" > "${log%.log}.out" 2>&1 &
	pid=$!
	while kill -0 "$pid" 2>/dev/null; do
		if [[ $scene == demo ]] && demo_closed "$log"; then
			kill -TERM "$pid"
			break
		fi
		if ((SECONDS - start >= limit)); then
			echo "r1.sh: $scene at $fps fps still runs after $limit s; ending it" >&2
			kill -TERM "$pid"
			wait "$pid"
			return 1
		fi
		sleep 0.2
	done
	wait "$pid"
	rc=$?
	if [[ $scene == demo ]]; then
		demo_closed "$log" || { echo "r1.sh: demo at $fps fps: no whole demo, see ${log%.log}.out" >&2; return 1; }
	elif [[ $rc -ne 0 ]]; then
		echo "r1.sh: $scene at $fps fps exited $rc, see ${log%.log}.out" >&2
		return 1
	elif ! grep -q '^# scene ' "$log"; then
		echo "r1.sh: $scene at $fps fps: no scene section; does the build play input scripts?" >&2
		return 1
	fi
}

# Play every scene at every fps with binary $1 into directory $2.
play_all()
{
	local fps s
	for fps in "${fps_list[@]}"; do
		mkdir -p "$2/$fps"
		for s in "${scenes[@]}"; do
			echo "r1.sh: playing ${s%%:*} at $fps fps"
			play "$1" "${s#*:}" "$fps" "$2/$fps/${s#*:}.log" || exit 1
		done
	done
}

# Compare the logs in directory $2 with those in $1.  Fail when a frame
# differs.
compare()
{
	local fps s rc=0 sec
	for fps in "${fps_list[@]}"; do
		for s in "${scenes[@]}"; do
			sec=()
			[[ ${s#*:} == demo ]] && sec=(--section "$demo_section")
			python3 "$dev_dir/framehash_diff.py" "${sec[@]}" "${s%%:*} at $fps fps" \
				"$1/$fps/${s#*:}.log" "$2/$fps/${s#*:}.log" || rc=1
		done
	done
	return $rc
}

# Compare the demo in directory $1 across the fps values.
compare_fps()
{
	local fps rc=0
	for fps in "${fps_list[@]:1}"; do
		python3 "$dev_dir/framehash_diff.py" --section "$demo_section" \
			"S2 at $fps fps against ${fps_list[0]} fps" \
			"$1/${fps_list[0]}/demo.log" "$1/$fps/demo.log" || rc=1
	done
	return $rc
}

if [[ $mode == capture ]]; then
	rev=$(git -C "$root" rev-parse --verify "$base^{commit}") || exit 2
	rm -rf "$r1/src" "$r1/baseline.new" && mkdir -p "$r1/src" || exit 1
	git -C "$root" archive "$rev" | tar -x -C "$r1/src" || exit 1
	# The archive's SConstruct has its $Format$ (export-subst) filled in,
	# which puts the commit in the version string the main menu shows;
	# the plain one versions the build as a checkout does.
	git -C "$root" show "$rev:SConstruct" > "$r1/src/SConstruct" || exit 1
	build "$r1/src"
	play_all "$r1/src/build/r1/d2x-rebirth/d2x-rebirth" "$r1/baseline.new"
	compare_fps "$r1/baseline.new" || exit 1
	git -C "$root" log -1 --format="%H %s" "$rev" > "$r1/baseline.new/commit"
	rm -rf "$r1/baseline" && mv "$r1/baseline.new" "$r1/baseline" || exit 1
	echo "r1.sh: baseline of $(cut -c1-12 "$r1/baseline/commit") in $r1/baseline"
else
	[[ -f $r1/baseline/commit ]] || { echo "r1.sh: no baseline in $r1; run r1.sh capture" >&2; exit 2; }
	build "$root"
	play_all "$root/build/r1/d2x-rebirth/d2x-rebirth" "$r1/run"
	echo "r1.sh: baseline of $(cut -c1-12 "$r1/baseline/commit")"
	rc=0
	compare "$r1/baseline" "$r1/run" || rc=1
	compare_fps "$r1/run" || rc=1
	exit $rc
fi
