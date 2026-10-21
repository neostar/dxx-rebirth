#!/usr/bin/env bash
# R9: the committed shader headers are exactly what the pinned toolchain
# builds from their HLSL sources.  Rebuilds every header of SHADER_DIR
# (default: the repository's shaders/) with generate.sh into a scratch
# directory and compares it byte for byte with SHADER_DIR/generated/; a
# source without its header, or a header without its source, fails.
#
#   check.sh [SHADER_DIR]
#
# DXC (DXIL) runs on Linux x86-64 only: without it the DXIL blobs are kept
# from the committed headers, with a SKIP naming dxc, and the reference
# container's leg checks them.  On macOS each MSL blob is also compiled
# with Xcode's Metal compiler (xcrun metal), a SKIP without it.  A missing
# tool is a SKIP, except in the reference image (DXX_REFERENCE_IMAGE=1),
# which has every pinned tool, where it fails.  DXX_SHADERCROSS and DXX_DXC
# override the tools, as for generate.sh.
set -u
here=$(cd "$(dirname "$0")" && pwd)
dev_dir=$(cd "$here/../dev" && pwd)
root=$(cd "$dev_dir/../.." && pwd)
# shellcheck source=../dev/env.sh
source "$dev_dir/env.sh"
# shellcheck source=lib.sh
source "$here/lib.sh"
dir=$(cd "${1:-$root/shaders}" && pwd) || exit 2

rc=0
# Print a missing tool's SKIP for step $1, or its FAIL in the reference image.
missing()
{
	if [[ ${DXX_REFERENCE_IMAGE-} == 1 ]]; then
		echo "FAIL $1: $2"
		rc=1
	else
		echo "SKIP $1: $2"
	fi
}

if [[ ! -x $shadercross ]]; then
	missing shaders "shadercross not found at $shadercross; run tools/dev/deps.sh build"
	exit "$rc"
fi
keep=()
if [[ ! -x $dxc ]]; then
	missing dxil "dxc not found at $dxc (Linux x86-64 only); DXIL kept from the headers, \
the container leg rebuilds it"
	keep=(--keep-dxil)
fi

work=$(mktemp -d "${TMPDIR:-/tmp}/shaders-check.XXXXXX")
trap 'rm -rf "$work"' EXIT
if ! "$here/generate.sh" --out "$work/new" "${keep[@]+"${keep[@]}"}" "$dir"; then
	echo "FAIL shaders: generate.sh failed"
	exit 1
fi

shopt -s nullglob
for src in "$dir"/hlsl/*.hlsl; do
	f=${src##*/}
	h=$(shader_header "$f")
	if [[ ! -f $dir/generated/$h ]]; then
		echo "FAIL $h: missing; run tools/shaders/build.sh"
		rc=1
	elif cmp -s "$work/new/$h" "$dir/generated/$h"; then
		echo "ok   $h"
	else
		echo "FAIL $h: differs from the rebuild of hlsl/$f; run tools/shaders/build.sh"
		rc=1
	fi
done
for h in "$dir"/generated/*.h; do
	src=$(shader_source "${h##*/}")
	[[ -f $dir/hlsl/$src ]] && continue
	echo "FAIL ${h##*/}: no source hlsl/$src"
	rc=1
done

# Apple's Metal compiler accepts every MSL blob (macOS only).  Probed by
# running it: Xcode 27's default toolchain holds a metal shim, which
# `xcrun -f metal` finds also without the Metal toolchain and which then
# fails with "missing Metal Toolchain".
if [[ $dev_os == darwin ]]; then
	if ! xcrun metal --version >/dev/null 2>&1; then
		missing metal "xcrun metal cannot run; run xcodebuild -downloadComponent MetalToolchain"
	else
		for h in "$dir"/generated/*.h; do
			n=$(basename "$h" .h)
			python3 -I "$here/embed.py" extract "$h" msl "$work/$n.metal" || { rc=1; continue; }
			if xcrun metal -c "$work/$n.metal" -o "$work/$n.air" 2> "$work/metal.log"; then
				echo "ok   $n.h: MSL compiles (xcrun metal)"
			else
				echo "FAIL $n.h: xcrun metal: $(cat "$work/metal.log")"
				rc=1
			fi
		done
	fi
fi
exit "$rc"
