#!/usr/bin/env bash
# Judge an SDL_GPU backend's frames of a trace against GL's by rubric R4
# (docs/agents/rubric.md), per frame and per trace (#101).
#
#   compare-backends.sh [OPTION...] TRACE BACKEND
#   compare-backends.sh --gl-frames DIR --gpu-frames DIR BACKEND
#
# TRACE is a .rtr trace (common/render/rtrace/synthetic/, or one you
# record: docs/contributing/shaders.md).  BACKEND is metal, vulkan or d3d12.
# Replays TRACE on GL (rtrace) and on BACKEND through SDL_GPU (rtrace-gpu),
# both through no-window.sh, then compares the frames (compare_frames.py):
#   metal:          every frame bit-exact;
#   vulkan, d3d12: per trace, the 95th percentile of the frames' FLIP means
#                   <= 0.01 and the worst frame's <= 0.03 (no max-error gate).
# Prints a verdict per frame and one per trace.  Exit status 0 when the
# trace passes, 1 when it fails (also a missing or failed program, a frame
# that is missing, empty or of another size, or SDL_GPU running another
# driver than BACKEND), 2 on a usage error.  A platform piece that does not
# exist yet is a SKIP line naming its ticket, no verdict, exit status 0 --
# which reads as a pass to a caller that checks only the status: a gate
# step or the scene parity check (#37) must give --require-verdict.
#
# R6 (0 validation messages in every R4 run, docs/agents/rubric.md) is not
# checked here; tools/dev/gpu-validate.sh does it, on Metal and Vulkan.
#
# Options:
#   --require-verdict a SKIP is a failure (exit status 1), not exit status 0
#   --gl-frames DIR   use frames recorded elsewhere for GL instead of
#   --gpu-frames DIR  (or for SDL_GPU) instead of replaying: DIR holds
#                     N.rgb and frames.log, which rtrace and rtrace-gpu
#                     write with RTRACE_FRAMES_DIR=DIR and LOG=DIR/frames.log
#                     (a WARP or CI run, judged against GL's frames from
#                     another machine).  With both, no TRACE is given.
#   --out DIR         keep the frames of the replays in DIR/gl and DIR/gpu
#                     (DIR must be new or empty; default: a temporary
#                     directory, removed at the end)
#   --rtrace PATH     the GL replay program (default
#                     build/dev/common/rtrace; build.sh dev
#                     build/dev/common/rtrace builds it)
#   --rtrace-gpu PATH the SDL_GPU replay program (default
#                     build/dev-sdl3/common/rtrace-gpu; build.sh dev sdl3=1
#                     build/dev-sdl3/common/rtrace-gpu builds it)
# Each replay has COMPARE_BACKENDS_TIMEOUT seconds (default 300); one that
# runs out is killed and fails.
#
# Platforms: macOS runs Metal on its own.  Missing defaults are SKIPs, each
# naming the ticket that adds the piece: the GL replay on Linux (#81), the
# Vulkan replay (Linux, lavapipe, #25; off Linux always), and on Windows
# (Git Bash) the GL replay (#49) and the D3D12 replay (#49; off Windows
# always).  A program named with --rtrace or --rtrace-gpu
# that is missing fails everywhere; Metal off macOS fails (give
# --gpu-frames).
set -u
dev_dir=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$dev_dir/../.." && pwd)
# shellcheck source=env.sh
source "$dev_dir/env.sh"

usage()
{
	echo "usage: $0 [--out DIR] [--rtrace PATH] [--rtrace-gpu PATH] [--gl-frames DIR] [--gpu-frames DIR] [--require-verdict] TRACE metal|vulkan|d3d12" >&2
	echo "       $0 [--require-verdict] --gl-frames DIR --gpu-frames DIR metal|vulkan|d3d12" >&2
	exit 2
}

out=""
rtrace=""
rtrace_gpu=""
gl_frames=""
gpu_frames=""
require_verdict=0
while [[ $# -gt 0 ]]; do
	case $1 in
		--out|--rtrace|--rtrace-gpu|--gl-frames|--gpu-frames)
			[[ $# -ge 2 && -n $2 ]] || usage
			case $1 in
				--out) out=$2 ;;
				--rtrace) rtrace=$2 ;;
				--rtrace-gpu) rtrace_gpu=$2 ;;
				--gl-frames) gl_frames=$2 ;;
				--gpu-frames) gpu_frames=$2 ;;
			esac
			shift 2 ;;
		--require-verdict) require_verdict=1; shift ;;
		-*) usage ;;
		*) break ;;
	esac
done
limit=${COMPARE_BACKENDS_TIMEOUT:-300}
[[ $limit =~ ^[1-9][0-9]*$ ]] ||
	{ echo "compare-backends: COMPARE_BACKENDS_TIMEOUT=$limit is not a positive number of seconds" >&2; exit 2; }
if [[ -n $gl_frames && -n $gpu_frames ]]; then
	[[ $# -eq 1 ]] || usage
	trace=""
	backend=$1
	name=frames
else
	[[ $# -eq 2 ]] || usage
	trace=$1
	backend=$2
	[[ -f $trace ]] ||
		{ echo "compare-backends: no trace $trace" >&2; exit 2; }
	name=$(basename "$trace" .rtr)
fi
case $backend in
	metal) sdl_driver=metal driver_name=Metal ;;
	vulkan) sdl_driver=vulkan driver_name=Vulkan ;;
	d3d12) sdl_driver=direct3d12 driver_name=D3D12 ;;
	*) usage ;;
esac
if [[ -n $out ]]; then
	if [[ -d $out ]] && [[ -n $(ls -A "$out") ]]; then
		echo "compare-backends: --out $out is not empty" >&2
		exit 2
	fi
fi

case $(uname -s) in
	Darwin) platform=macos exe="" ;;
	Linux) platform=linux exe="" ;;
	MINGW*|MSYS*|CYGWIN*) platform=windows exe=.exe ;;
	*) echo "compare-backends: unsupported platform $(uname -s)" >&2; exit 1 ;;
esac

# Print process $1 and all its descendants, one pid a line (ps, as
# no-window.sh does: Git Bash has no pgrep).
tree_pids()
{
	echo "$1"
	ps -A -o pid=,ppid= | awk -v root="$1" '
		{ parent[$1] = $2 }
		END { for (p in parent) for (q = p; q > 1; q = parent[q]) if (q == root) { if (p != root) print p; break } }'
}

# Kill the tree of process $1: TERM, then KILL a second later.
kill_tree()
{
	local pids
	mapfile -t pids < <(tree_pids "$1")
	kill -TERM "${pids[@]}" 2>/dev/null
	sleep 1
	kill -KILL "${pids[@]}" 2>/dev/null
}

# The replay running now, for the signal traps.
running=""
# shellcheck disable=SC2329 # invoked by the traps below
on_signal()
{
	trap '' INT TERM
	[[ -z $running ]] || kill_tree "$running"
	exit "$1"
}
trap 'on_signal 130' INT
trap 'on_signal 143' TERM

# Run "$@", killing it and what it started after $limit seconds: status
# 124 then.
bounded()
{
	local pid i
	"$@" &
	pid=$!
	running=$pid
	for ((i = 0; i < limit * 10; i++)); do
		if ! kill -0 "$pid" 2>/dev/null; then
			running=""
			wait "$pid"
			return
		fi
		sleep 0.1
	done
	kill_tree "$pid"
	running=""
	wait "$pid"
	echo "compare-backends: ${1##*/} ran past $limit s and was killed" >&2
	return 124
}

# Which replay programs are needed, and whether they can run: a failure
# (fail=1) or a SKIP, each told now, before anything runs.
fail=0
skips=0
skip()
{
	echo "SKIP $*"
	skips=1
}
if [[ -z $gl_frames ]]; then
	if [[ -n $rtrace ]]; then
		[[ -x $rtrace ]] ||
			{ echo "compare-backends: no rtrace at $rtrace" >&2; fail=1; }
	else
		rtrace=$root/build/dev/common/rtrace$exe
		if [[ ! -x $rtrace ]]; then
			case $platform in
				macos)
					echo "compare-backends: no rtrace at $rtrace; build.sh dev build/dev/common/rtrace builds it" >&2
					fail=1 ;;
				linux)
					skip "the GL replay: no rtrace at $rtrace; Linux needs #81's GL replay (--gl-frames DIR takes frames recorded elsewhere)" ;;
				windows)
					skip "the GL replay: no rtrace$exe at $rtrace; Windows needs #49's Windows rtrace (--gl-frames DIR takes frames recorded elsewhere)" ;;
			esac
		fi
	fi
fi
if [[ -z $gpu_frames ]]; then
	if [[ $backend == metal && $platform != macos ]]; then
		echo "compare-backends: Metal runs only on macOS; --gpu-frames DIR takes frames recorded there" >&2
		fail=1
	elif [[ -n $rtrace_gpu ]]; then
		[[ -x $rtrace_gpu ]] ||
			{ echo "compare-backends: no rtrace-gpu at $rtrace_gpu" >&2; fail=1; }
	elif [[ $backend == vulkan && $platform != linux ]]; then
		skip "the Vulkan replay: it runs on Linux, on lavapipe in the reference container, #25 (--gpu-frames DIR takes frames recorded there)"
	elif [[ $backend == d3d12 && $platform != windows ]]; then
		skip "the D3D12 replay: it runs on Windows, WARP or a GPU, #49 (--gpu-frames DIR takes frames recorded there)"
	else
		rtrace_gpu=$root/build/dev-sdl3/common/rtrace-gpu$exe
		if [[ ! -x $rtrace_gpu ]]; then
			case $backend in
				metal)
					echo "compare-backends: no rtrace-gpu at $rtrace_gpu; build.sh dev sdl3=1 build/dev-sdl3/common/rtrace-gpu builds it" >&2
					fail=1 ;;
				vulkan)
					skip "the Vulkan replay: no rtrace-gpu at $rtrace_gpu; Vulkan runs on lavapipe, #25 (--gpu-frames DIR takes frames recorded elsewhere)" ;;
				d3d12)
					skip "the D3D12 replay: no rtrace-gpu$exe at $rtrace_gpu; D3D12 on Windows needs #49 (--gpu-frames DIR takes frames recorded there)" ;;
			esac
		fi
	fi
fi
[[ $fail -eq 0 ]] || exit 1
if [[ $skips -eq 1 ]]; then
	echo "compare-backends: SKIP: no verdict for $name on $driver_name"
	if [[ $require_verdict -eq 1 ]]; then
		echo "compare-backends: --require-verdict: a SKIP is a failure" >&2
		exit 1
	fi
	exit 0
fi

tmp=""
replayed=0
if [[ -z $out ]]; then
	tmp=$(mktemp -d "${TMPDIR:-/tmp}/compare-backends.XXXXXX") ||
		{ echo "compare-backends: mktemp failed" >&2; exit 1; }
	trap 'rm -rf "${tmp:?}"' EXIT
	out=$tmp
fi
if [[ -z $gl_frames ]]; then
	replayed=1
	gl_frames=$out/gl
	mkdir -p "$gl_frames" || { echo "compare-backends: cannot make $gl_frames" >&2; exit 1; }
	if ! RTRACE_FRAMES_DIR=$gl_frames bounded "$dev_dir/no-window.sh" "$rtrace" "$trace" \
		"$gl_frames/frames.log" > "$out/gl.out" 2>&1; then
		cat "$out/gl.out" >&2
		echo "compare-backends: rtrace failed on $trace" >&2
		exit 1
	fi
fi
if [[ -z $gpu_frames ]]; then
	replayed=1
	gpu_frames=$out/gpu
	mkdir -p "$gpu_frames" || { echo "compare-backends: cannot make $gpu_frames" >&2; exit 1; }
	if ! SDL_GPU_DRIVER=$sdl_driver SDL_LOGGING='gpu=info,*=warn' RTRACE_FRAMES_DIR=$gpu_frames \
		bounded "$dev_dir/no-window.sh" "$rtrace_gpu" "$trace" "$gpu_frames/frames.log" \
		> "$out/gpu.out" 2>&1; then
		cat "$out/gpu.out" >&2
		echo "compare-backends: rtrace-gpu failed on $trace" >&2
		exit 1
	fi
	ran=$(tr -d '\r' < "$out/gpu.out" | sed -n 's/.*SDL_GPU Driver: //p' | head -n 1)
	if [[ -z $ran ]]; then
		cat "$out/gpu.out" >&2
		echo "compare-backends: rtrace-gpu named no SDL_GPU driver on $trace" >&2
		exit 1
	elif [[ $ran != "$driver_name" ]]; then
		echo "compare-backends: rtrace-gpu ran on $ran, not $driver_name, on $trace" >&2
		exit 1
	fi
fi

python3 "$dev_dir/compare_frames.py" "$backend" "$name" "$gl_frames" "$gpu_frames"
rc=$?
case $rc in
	0) ;;
	1)
		if [[ -n $tmp && $replayed -eq 1 ]]; then
			echo "compare-backends: --out DIR keeps the frames to look at" >&2
		fi ;;
	*) echo "compare-backends: compare_frames.py failed (status $rc)" >&2; rc=1 ;;
esac
case $name in
	lines|filters)
		if [[ $backend == metal && $rc -ne 0 ]]; then
			echo "compare-backends: note: R3 pins the $name trace to Metal's own frames within a tolerance (#32, #34); R4's bit-exact bar fails it by design" >&2
		fi ;;
esac
exit "$rc"
