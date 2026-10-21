#!/usr/bin/env bash
# Compile every shaders/hlsl/<name>.<stage>.hlsl (stage vert or frag) into
# the generated header shaders/generated/<name>_<stage>.h (embed.py): SPIR-V,
# MSL and the reflection's resource counts from the pinned SDL_shadercross,
# DXIL from the pinned DXC release (tools/dev/toolchain.pins), which runs on
# Linux x86-64 only, so the reference container is where all of it builds.
#
#   generate.sh [--out DIR] [--keep-dxil] [--tar] [SHADER_DIR]
#
# SHADER_DIR defaults to the repository's shaders/; headers go to its
# generated/ unless --out names another directory.  --keep-dxil takes each
# DXIL blob from SHADER_DIR's committed header instead of DXC (check.sh, on
# a machine without DXC), and skips a source without one.  --tar writes the headers as a tar archive to the
# standard output instead (build.sh, from the container).  The tools are
# DXX_SHADERCROSS and DXX_DXC, by default the dependency prefix's (lib.sh).
# A source named otherwise than <name>.<vert|frag>.hlsl, <name> in lower
# snake_case, fails before anything is built.  A new
# header goes into tools/dev/commit-size-exclude: generated, its size says
# nothing about the change.
set -eu
dev_dir=$(cd "$(dirname "$0")/../dev" && pwd)
root=$(cd "$dev_dir/../.." && pwd)
# shellcheck source=../dev/env.sh
source "$dev_dir/env.sh"
here=$(cd "$(dirname "$0")" && pwd)
# shellcheck source=lib.sh
source "$here/lib.sh"
embed=$here/embed.py

out='' keep_dxil=0 tar=0
while [[ $# -gt 0 ]]; do
	case $1 in
	--out) out=$2; shift ;;
	--keep-dxil) keep_dxil=1 ;;
	--tar) tar=1 ;;
	-*) echo "usage: $0 [--out DIR] [--keep-dxil] [--tar] [SHADER_DIR]" >&2; exit 2 ;;
	*) break ;;
	esac
	shift
done
dir=$(cd "${1:-$root/shaders}" && pwd)
[[ -n $out ]] || out=$dir/generated
shopt -s nullglob
sources=("$dir"/hlsl/*.hlsl)
[[ ${#sources[@]} -gt 0 ]] || { echo "generate.sh: no HLSL source in $dir/hlsl" >&2; exit 1; }
for src in "${sources[@]}"; do
	shader_header "${src##*/}" >/dev/null || exit 1
done
[[ -x $shadercross ]] || { echo "generate.sh: shadercross not found at $shadercross" >&2; exit 1; }
[[ $keep_dxil -eq 1 || -x $dxc ]] || { echo "generate.sh: dxc not found at $dxc" >&2; exit 1; }

work=$(mktemp -d "${TMPDIR:-/tmp}/shaders.XXXXXX")
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/out"
for src in "${sources[@]}"; do
	file=${src##*/}
	name=$(shader_header "$file")
	name=${name%.h}
	case $name in
	*_vert) stage=vertex profile=vs_6_0 ;;
	*) stage=fragment profile=ps_6_0 ;;
	esac
	b=$work/$name
	# Nothing to keep the DXIL of: check.sh reports the header missing.
	[[ $keep_dxil -eq 0 || -f $dir/generated/$name.h ]] || continue
	# From the source's directory, by its bare name: no path reaches a blob.
	(cd "$dir/hlsl" &&
		for f in spv msl json; do
			"$shadercross" "$file" -o "$b.$f" >&2
		done
		if [[ $keep_dxil -eq 1 ]]; then
			python3 -I "$embed" extract "$dir/generated/$name.h" dxil "$b.dxil"
		else
			"$dxc" -T "$profile" -E main "$file" -Fo "$b.dxil" >&2
		fi)
	python3 -I "$embed" render "$name" "shaders/hlsl/$file" "$stage" \
		"$b.spv" "$b.msl" "$b.dxil" "$b.json" "$work/out/$name.h"
done

if [[ $tar -eq 1 ]]; then
	(cd "$work/out" && tar -cf - ./*.h)
else
	mkdir -p "$out"
	cp "$work"/out/*.h "$out/"
fi
