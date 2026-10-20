#!/usr/bin/env bash
# Mirror the pinned SDL3 / SDL_GPU / shadercross / FLIP / Vulkan reference
# docs into docs/reference/ (ignored by git) for offline lookup.
#
#   fetch-docs.sh [--manifest FILE] [--dest DIR]
#
# Each manifest line is "PATH URL SHA256"; # starts a comment.  A file whose
# checksum differs is reported and not kept; the exit status is then 1.
# Files already present with the right checksum are not downloaded again.
set -u
dev_dir=$(cd "$(dirname "$0")" && pwd)
manifest=$dev_dir/reference-docs.txt
dest=$dev_dir/../../docs/reference

usage()
{
	echo "usage: $0 [--manifest FILE] [--dest DIR]" >&2
	exit 2
}

while [[ $# -gt 0 ]]; do
	case $1 in
		--manifest) [[ $# -ge 2 ]] || usage; manifest=$2; shift 2 ;;
		--dest) [[ $# -ge 2 ]] || usage; dest=$2; shift 2 ;;
		*) usage ;;
	esac
done

sha256()
{
	shasum -a 256 "$1" | cut -d' ' -f1
}

rc=0
while read -r path url sum; do
	[[ -z ${path-} || $path == \#* ]] && continue
	out=$dest/$path
	if [[ -f $out && $(sha256 "$out") == "$sum" ]]; then
		continue
	fi
	mkdir -p "$(dirname "$out")"
	if ! curl -fsSL --retry 2 -o "$out.part" "$url"; then
		echo "fetch-docs.sh: $path: download failed: $url" >&2
		rm -f "$out.part"; rc=1; continue
	fi
	got=$(sha256 "$out.part")
	if [[ $got != "$sum" ]]; then
		echo "fetch-docs.sh: $path: sha256 $got, expected $sum" >&2
		rm -f "$out.part"; rc=1; continue
	fi
	mv "$out.part" "$out"
	echo "$path"
done < "$manifest"
exit "$rc"
