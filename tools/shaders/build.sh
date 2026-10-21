#!/usr/bin/env bash
# Rebuild the committed shader headers, shaders/generated/*.h, from
# shaders/hlsl/ with generate.sh, and remove a header whose source is gone.
# Where the pinned DXC is installed (the Linux reference container) it runs
# generate.sh directly; elsewhere (the Mac) it runs it in that container
# (tools/dev/container.sh run, offline, on a copy of the working tree) and
# unpacks the headers it writes.  The committed headers change only once a
# complete set is built: one header per source, each holding its SPIR-V, MSL
# and DXIL arrays; a failed or partial build leaves them as they are.
# check.sh then passes, in both legs (R9).
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
dev_dir=$(cd "$here/../dev" && pwd)
root=$(cd "$dev_dir/../.." && pwd)
# shellcheck source=../dev/env.sh
source "$dev_dir/env.sh"
# shellcheck source=lib.sh
source "$here/lib.sh"
gen=$root/shaders/generated

new=$(mktemp -d "${TMPDIR:-/tmp}/shaders-build.XXXXXX")
trap 'rm -rf "$new"' EXIT
if [[ -x $dxc ]]; then
	"$here/generate.sh" --out "$new" "$root/shaders"
else
	echo "build.sh: no dxc here; building in the reference container" >&2
	"$dev_dir/container.sh" run tools/shaders/generate.sh --tar | tar -x -C "$new"
fi

shopt -s nullglob
headers=()
for src in "$root"/shaders/hlsl/*.hlsl; do
	h=$(shader_header "${src##*/}")
	for f in spirv msl dxil; do
		python3 -I "$here/embed.py" extract "$new/$h" "$f" /dev/null 2>/dev/null || {
			echo "build.sh: the build gave no complete $h ($f); shaders/generated is unchanged" >&2
			exit 1
		}
	done
	headers+=("$h")
done
mkdir -p "$gen"
for h in "${headers[@]}"; do
	cp "$new/$h" "$gen/$h"
done
for h in "$gen"/*.h; do
	[[ -f $root/shaders/hlsl/$(shader_source "${h##*/}") ]] || rm -f "$h"
done
ls "$gen"
