#!/usr/bin/env bash
# R3 on D3D12 (#49): rtrace-gpu.exe on each committed synthetic trace,
# with SDL_GPU on D3D12 and its debug layer, must log GL's pinned frame
# hash lines (rtrace-pins.sh, as rtrace-tests.sh pins them) exactly.
#
#   rtrace-d3d12.sh DIR [LAUNCHER...]
#
# DIR holds rtrace-gpu.exe and its DLLs (windows.sh build).  Each run goes
# through gpu-validate.sh, which fails it on any SDL warning or error (the
# D3D12 debug layer's messages among them) and when the debug layer is
# not active; LAUNCHER, when given, runs the exe (windows.sh wine passes
# `wine`).  SDL_GPU_DRIVER=direct3d12 asks SDL for D3D12; a run on another
# driver fails, as does one that names no D3D12 adapter.  Prints the
# adapter SDL picked (on a GitHub runner, WARP: Microsoft Basic Render
# Driver).  Line ends are a platform's: the logs' CRs are dropped before
# the compare, nothing else.  Prints `rtrace-d3d12: passed` and exits 0
# when every trace passes; 1 otherwise; 2 on a usage error.  Runs in Git
# Bash on Windows (the workflow d3d12-check.yml), which ends a hung run by
# its job's timeout.
set -u
dev_dir=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$dev_dir/../.." && pwd)
[[ $# -ge 1 && -n $1 ]] || { echo "usage: $0 DIR [LAUNCHER...]" >&2; exit 2; }
dir=$1
shift
launch=("$@")
exe=$dir/rtrace-gpu.exe
[[ -f $exe ]] || { echo "rtrace-d3d12: no rtrace-gpu.exe in $dir" >&2; exit 1; }
source "$dev_dir/rtrace-pins.sh"
export SDL_GPU_DRIVER=direct3d12

tmp=$(mktemp -d "${TMPDIR:-/tmp}/rtrace-d3d12.XXXXXX") ||
	{ echo "rtrace-d3d12: mktemp failed" >&2; exit 1; }
trap 'rm -rf "$tmp"' EXIT

# Run on trace $1, named $2, which must log lines $3....
check()
{
	local trace=$1 name=$2 out=$tmp/$2 driver adapter lines i ok=1
	shift 2
	[[ $# -gt 0 ]] || { echo "rtrace-d3d12: no pinned lines for the $name trace" >&2; return 1; }
	"$dev_dir/gpu-validate.sh" ${launch[@]+"${launch[@]}"} "$exe" "$trace" "$out.log" \
		> "$out.out" 2>&1 || ok=0
	cat "$out.out"
	driver=$(sed -n 's/.*SDL_GPU Driver: //p' "$out.out" | head -n 1)
	adapter=$(sed -n 's/.*D3D12 Adapter: //p' "$out.out" | head -n 1)
	[[ -n $adapter ]] && echo "rtrace-d3d12: adapter: $adapter"
	if [[ $driver != D3D12 ]]; then
		echo "rtrace-d3d12: the $name trace ran on ${driver:-no driver}, not D3D12" >&2
		ok=0
	elif [[ -z $adapter ]]; then
		echo "rtrace-d3d12: the $name trace named no D3D12 adapter" >&2
		ok=0
	fi
	mapfile -t lines < <(tr -d '\r' < "$out.log" 2>/dev/null)
	[[ ${#lines[@]} -eq $# ]] || lines=(mismatch)
	for ((i = 0; i < $#; i++)); do
		[[ ${lines[i]-} == "${*:i+1:1}" ]] && continue
		echo "rtrace-d3d12: the $name trace's frame hashes are not GL's:" >&2
		cat "$out.log" >&2 2>/dev/null
		ok=0
		break
	done
	[[ $i -eq $# ]] && echo "rtrace-d3d12: the $name trace logged GL's lines"
	if [[ $ok -eq 0 ]]; then
		echo "rtrace-d3d12: rtrace-gpu failed on the $name trace" >&2
		return 1
	fi
}
rc=0
check "$sample" sample "${expected[@]}" || rc=1
check "$overbright" overbright "${expected_overbright[@]}" || rc=1
check "$flash" flash "${expected_flash[@]}" || rc=1
[[ $rc -eq 0 ]] && echo "rtrace-d3d12: passed"
exit "$rc"
