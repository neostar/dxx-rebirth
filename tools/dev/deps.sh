#!/usr/bin/env bash
# Fetch the fork's pinned dependencies (SDL3, FluidSynth, SDL3_mixer,
# SDL3_image, SDL_shadercross, DXC) from tools/dev/toolchain.pins into a
# verified source cache, the same way on the Mac and for the Linux
# reference container (ticket #57).
#
#   deps.sh [--manifest FILE] [--cache DIR] [--platform OS-ARCH] [fetch]
#
# fetch: download each source once into the cache and verify it: a
#   `source` or `prebuilt` file by its pinned sha256, a `git` checkout by its
#   pinned commit (its submodules by the commits that commit records).  A
#   download that does not match is deleted, never cached.  `prebuilt` files
#   are fetched for --platform only (default: this machine's), so the Mac
#   can fill the cache for the Linux container.
# The cache defaults to env.sh's dev_deps_cache.  Exit status 1 means a
# source was refused, 2 a usage error.
set -eu
dev_dir=$(cd "$(dirname "$0")" && pwd)
source "$dev_dir/env.sh"

usage()
{
	echo "usage: $0 [--manifest FILE] [--cache DIR] [--platform OS-ARCH] [fetch]" >&2
	exit 2
}

manifest=$dev_dir/toolchain.pins
cache=$dev_deps_cache
platform=$dev_platform
while [[ $# -gt 0 ]]; do
	case $1 in
	--manifest) [[ $# -ge 2 ]] || usage; manifest=$2; shift 2 ;;
	--cache) [[ $# -ge 2 ]] || usage; cache=$2; shift 2 ;;
	--platform) [[ $# -ge 2 ]] || usage; platform=$2; shift 2 ;;
	fetch) shift ;;
	*) usage ;;
	esac
done
[[ -r $manifest ]] || { echo "deps.sh: cannot read $manifest" >&2; exit 2; }

refuse()
{
	echo "deps.sh: $*" >&2
	exit 1
}

# Fail unless file $2 (dependency $1) has sha256 $3.
verify_file()
{
	local name=$1 file=$2 want=$3 got
	[[ -f $file ]] || refuse "$name: not in the cache; run deps.sh fetch"
	got=$(dev_sha256 "$file")
	[[ $got == "$want" ]] ||
		refuse "$name: sha256 $got does not match the pinned $want ($file)"
}

# Fail unless checkout $2 (dependency $1) is clean at commit $3 with every
# submodule at the commit the superproject records.
verify_checkout()
{
	local name=$1 dir=$2 want=$3
	[[ -d $dir/.git ]] || refuse "$name: not in the cache; run deps.sh fetch"
	[[ $(git -C "$dir" rev-parse HEAD) == "$want" ]] ||
		refuse "$name: checkout $dir is not at the pinned commit $want"
	[[ -z $(git -C "$dir" status --porcelain --ignore-submodules=none) ]] ||
		refuse "$name: checkout $dir has local changes"
	! git -C "$dir" submodule status --recursive | grep -q '^[-+U]' ||
		refuse "$name: a submodule of $dir is missing or not at its recorded commit"
}

# Download $2 into cache file $3 for dependency $1 and keep it only if its
# sha256 is $4.
fetch_file()
{
	local name=$1 url=$2 file=$3 want=$4
	if [[ ! -f $file ]]; then
		echo "fetch $name: $url"
		mkdir -p "$cache"
		curl -fsSL -o "$file.part" "$url" || { rm -f "$file.part"; refuse "$name: download failed"; }
		if [[ $(dev_sha256 "$file.part") != "$want" ]]; then
			echo "deps.sh: $name: sha256 $(dev_sha256 "$file.part") does not match the pinned $want ($url)" >&2
			rm -f "$file.part"
			exit 1
		fi
		mv "$file.part" "$file"
	fi
	verify_file "$name" "$file" "$want"
}

# Clone $2 into cache checkout $3 for dependency $1 at commit $4, with its
# submodules.
fetch_checkout()
{
	local name=$1 url=$2 dir=$3 commit=$4
	if [[ ! -d $dir/.git ]]; then
		echo "fetch $name: $url at $commit"
		rm -rf "$dir.part"
		git clone -q --no-checkout "$url" "$dir.part" || refuse "$name: clone of $url failed"
		if ! git -C "$dir.part" -c advice.detachedHead=false checkout -q --detach "$commit" 2>/dev/null; then
			rm -rf "$dir.part"
			refuse "$name: commit $commit is not in $url"
		fi
		git -C "$dir.part" submodule update -q --init --recursive --depth 1 ||
			{ rm -rf "$dir.part"; refuse "$name: submodules of $url failed"; }
		mv "$dir.part" "$dir"
	fi
	verify_checkout "$name" "$dir" "$commit"
}

# Print the dependency lines (source, git, fetchcontent, prebuilt) of the
# manifest.
dep_lines()
{
	grep -E '^(source|git|fetchcontent|prebuilt)[[:space:]]' "$manifest"
}

# Visit every dependency line that applies here: call $1 with
#   source NAME VERSION URL SHA256 CACHED-FILE
#   git NAME COMMIT URL CACHED-DIR
#   fetchcontent NAME COMMIT URL CACHED-DIR
#   prebuilt NAME PLATFORM URL SHA256 CACHED-FILE
each_dep()
{
	local fn=$1 kind name a url sha
	while read -r kind name a url sha _; do
		case $kind in
		source) "$fn" source "$name" "$a" "$url" "$sha" "$cache/$name-$a.tar.gz" ;;
		git|fetchcontent) "$fn" "$kind" "$name" "$a" "$url" "$cache/$name-$a" ;;
		prebuilt) [[ $a == "$platform" ]] || continue
			"$fn" prebuilt "$name" "$a" "$url" "$sha" "$cache/$name-$(basename "$url")" ;;
		esac
	done < <(dep_lines)
}

fetch_one()
{
	case $1 in
	source|prebuilt) fetch_file "$2" "$4" "$6" "$5" ;;
	git|fetchcontent) fetch_checkout "$2" "$4" "$5" "$3" ;;
	esac
}

each_dep fetch_one
