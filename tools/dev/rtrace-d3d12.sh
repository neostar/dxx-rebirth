#!/usr/bin/env bash
# R3 on D3D12 (#49): rtrace-gpu.exe on each committed synthetic trace,
# with SDL_GPU on D3D12.
#
#   rtrace-d3d12.sh [--hashes FILE] DIR [LAUNCHER...]
#   rtrace-d3d12.sh --smoke DIR [LAUNCHER...]
#
# DIR holds rtrace-gpu.exe and its DLLs (windows.sh build).  LAUNCHER, when
# given, runs the exe (windows.sh wine passes `wine`).  SDL_GPU_DRIVER=
# direct3d12 asks SDL for D3D12; a run on another driver fails.  Line ends
# are a platform's: the logs' CRs are dropped, nothing else.  Prints
# `rtrace-d3d12: passed` (`smoke test passed`) and exits 0 when every trace
# passes; 1 otherwise; 2 on a usage error.  Runs in Git Bash on Windows (the
# workflow d3d12-check.yml).  Each run has DXX_D3D12_TIMEOUT seconds
# (default 300) under timeout(1), then a KILL 10 s later; one that runs
# out fails as a timeout.
#
# Default (the GitHub run, WARP): each run goes through gpu-validate.sh,
# which fails it on any SDL warning or error (the D3D12 debug layer's
# messages among them) and when SDL's "Validation layers enabled" line is
# missing.  DXX_D3D12_GPU_VALIDATION=1 asks rtrace-gpu.exe for GPU-based
# validation (SDL 3.4.18 never enables it) and to confirm it from SDL's
# device; the run fails unless its log has rtrace-gpu's line `rtrace-gpu:
# D3D12 GPU-based validation confirmed on SDL's device` and the debug
# layer's startup options it read that from.  The adapter must be WARP (Microsoft
# Basic Render Driver).  Every trace must render all its frames: one frame
# hash line each, in order.  The lines are recorded in FILE (default
# d3d12-frame-hashes.txt), one `TRACE INDEX WxH HASH` per line, and printed,
# for the FLIP compare of #37; they are not compared with GL's, because WARP
# rounds UNORM ties differently from a GPU.
#
# --smoke (Wine): the exe runs, SDL picks D3D12 and every trace renders all
# its frames.  No debug layer or GPU-based validation is asked for or
# required (Wine has none), any adapter will do, and no hashes are recorded.
set -u
dev_dir=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$dev_dir/../.." && pwd)
usage() { echo "usage: $0 [--hashes FILE | --smoke] DIR [LAUNCHER...]" >&2; exit 2; }
smoke=0
hashes=d3d12-frame-hashes.txt
hashes_given=
limit=${DXX_D3D12_TIMEOUT:-300}
[[ $limit =~ ^[1-9][0-9]*$ ]] || usage
while [[ $# -gt 0 ]]; do
	case $1 in
		--smoke) [[ -z $hashes_given ]] || usage; smoke=1; shift ;;
		--hashes) [[ $# -ge 2 && -n $2 && $smoke -eq 0 ]] || usage; hashes=$2; hashes_given=1; shift 2 ;;
		-*) usage ;;
		*) break ;;
	esac
done
[[ $# -ge 1 && -n $1 ]] || usage
dir=$1
shift
launch=("$@")
exe=$dir/rtrace-gpu.exe
[[ -f $exe ]] || { echo "rtrace-d3d12: no rtrace-gpu.exe in $dir" >&2; exit 1; }
command -v timeout >/dev/null || { echo "rtrace-d3d12: no timeout(1) on PATH" >&2; exit 1; }
source "$dev_dir/rtrace-pins.sh"
export SDL_GPU_DRIVER=direct3d12
warp="Microsoft Basic Render Driver"

tmp=$(mktemp -d "${TMPDIR:-/tmp}/rtrace-d3d12.XXXXXX") ||
	{ echo "rtrace-d3d12: mktemp failed" >&2; exit 1; }
trap 'rm -rf "$tmp"' EXIT
: > "$tmp/hashes"

# Run on trace $1, named $2, which must render $3 frames.
check()
{
	local trace=$1 name=$2 frames=$3 out=$tmp/$2 driver adapter lines i ok=1 shape rc
	if [[ $smoke -eq 1 ]]; then
		SDL_LOGGING='gpu=info,*=warn' timeout -k 10 "$limit" \
			"${launch[@]+"${launch[@]}"}" "$exe" "$trace" "$out.log" > "$out.raw" 2>&1
		rc=$?
		tr -d '\r' < "$out.raw" > "$out.out"
	else
		DXX_D3D12_GPU_VALIDATION=1 timeout -k 10 "$limit" "$dev_dir/gpu-validate.sh" \
			${launch[@]+"${launch[@]}"} "$exe" "$trace" "$out.log" > "$out.out" 2>&1
		rc=$?
	fi
	[[ $rc -eq 0 ]] || ok=0
	cat "$out.out"
	if [[ $rc -eq 124 || $rc -eq 137 ]]; then
		echo "rtrace-d3d12: the $name trace timed out after $limit s (status $rc)" >&2
	fi
	driver=$(sed -n 's/.*SDL_GPU Driver: //p' "$out.out" | head -n 1)
	adapter=$(sed -n 's/.*D3D12 Adapter: //p' "$out.out" | head -n 1)
	[[ -n $adapter ]] && echo "rtrace-d3d12: adapter: $adapter"
	if [[ $driver != D3D12 ]]; then
		echo "rtrace-d3d12: the $name trace ran on ${driver:-no driver}, not D3D12" >&2
		ok=0
	elif [[ $smoke -eq 0 ]]; then
		if [[ -z $adapter ]]; then
			echo "rtrace-d3d12: the $name trace named no D3D12 adapter" >&2
			ok=0
		elif [[ $adapter != "$warp" ]]; then
			echo "rtrace-d3d12: the $name trace ran on $adapter, not on WARP ($warp)" >&2
			ok=0
		fi
		if ! grep -q '^rtrace-gpu: D3D12 debug layer startup options: ' "$out.out" ||
			! grep -qx "rtrace-gpu: D3D12 GPU-based validation confirmed on SDL's device" "$out.out"; then
			echo "rtrace-d3d12: the $name trace did not confirm GPU-based validation on SDL's device" >&2
			ok=0
		fi
	fi
	# One line per frame, in order: `INDEX WxH HASH`.
	mapfile -t lines < <(tr -d '\r' < "$out.log" 2>/dev/null)
	shape=1
	[[ ${#lines[@]} -eq $frames ]] || shape=0
	for ((i = 0; shape == 1 && i < frames; i++)); do
		[[ ${lines[i]} =~ ^$i\ [0-9]+x[0-9]+\ [0-9a-f]{16}$ ]] || shape=0
	done
	if [[ $shape -eq 1 ]]; then
		for ((i = 0; i < frames; i++)); do
			echo "$name ${lines[i]}" >> "$tmp/hashes"
		done
		echo "rtrace-d3d12: the $name trace rendered $frames frames"
	else
		echo "rtrace-d3d12: the $name trace did not render all its frames ($frames expected):" >&2
		cat "$out.log" >&2 2>/dev/null
		ok=0
	fi
	if [[ $ok -eq 0 ]]; then
		echo "rtrace-d3d12: rtrace-gpu failed on the $name trace" >&2
		return 1
	fi
}
rc=0
check "$sample" sample "${#expected[@]}" || rc=1
check "$overbright" overbright "${#expected_overbright[@]}" || rc=1
check "$flash" flash "${#expected_flash[@]}" || rc=1
check "$walls" walls "${#expected_walls[@]}" || rc=1
if [[ $smoke -eq 0 ]]; then
	if cp "$tmp/hashes" "$hashes"; then
		echo "rtrace-d3d12: frame hashes (recorded in $hashes, not compared):"
		cat "$tmp/hashes"
	else
		echo "rtrace-d3d12: cannot write $hashes" >&2
		rc=1
	fi
fi
if [[ $rc -eq 0 ]]; then
	if [[ $smoke -eq 1 ]]; then echo "rtrace-d3d12: smoke test passed"; else echo "rtrace-d3d12: passed"; fi
fi
exit "$rc"
