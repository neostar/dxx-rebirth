#!/usr/bin/env bash
# Build the fork's pinned dependencies (SDL3, FluidSynth, SDL3_mixer,
# SDL3_image, SDL_shadercross, DXC) from tools/dev/toolchain.pins into
# the project-local prefix, the same way on the Mac and in the Linux
# reference container (ticket #57).
#
#   deps.sh [--manifest FILE] [--cache DIR] [--prefix DIR] [--platform OS-ARCH]
#           [fetch|build|all|stamp]
#
# fetch: download each source once into the cache and verify it: a
#   `source` or `prebuilt` file by its pinned sha256, a `git` checkout by its
#   pinned commit (its submodules by the commits that commit records).  A
#   download that does not match is deleted, never cached.  `prebuilt` files
#   are fetched for --platform only (default: this machine's), so the Mac
#   can fill the cache for the Linux container.
#   A `fetchcontent` checkout is not built itself: it stands in for the
#   CMake FetchContent download of that name in every build.
# build: verify the cache again, then build every source offline into a
#   fresh prefix, which records the pins and a hash of the build options in
#   deps.stamp, and remove the build tree.  Never downloads.
# stamp: print what build records in deps.stamp (doctor.sh compares the
#   two).
# all (default): fetch, then build.
# Cache and prefix default to env.sh's dev_deps_cache and dev_deps_prefix.
# Exit status 1 means a source was refused or a build failed, 2 a usage
# error.
set -eu
dev_dir=$(cd "$(dirname "$0")" && pwd)
source "$dev_dir/env.sh"

usage()
{
	echo "usage: $0 [--manifest FILE] [--cache DIR] [--prefix DIR]" \
		"[--platform OS-ARCH] [fetch|build|all|stamp]" >&2
	exit 2
}

manifest=$dev_dir/toolchain.pins
cache=$dev_deps_cache
prefix=$dev_deps_prefix
mode=all
platform=$dev_platform
while [[ $# -gt 0 ]]; do
	case $1 in
	--manifest) [[ $# -ge 2 ]] || usage; manifest=$2; shift 2 ;;
	--cache) [[ $# -ge 2 ]] || usage; cache=$2; shift 2 ;;
	--prefix) [[ $# -ge 2 ]] || usage; prefix=$2; shift 2 ;;
	--platform) [[ $# -ge 2 ]] || usage; platform=$2; shift 2 ;;
	fetch|build|all|stamp) mode=$1; shift ;;
	*) usage ;;
	esac
done
[[ -r $manifest ]] || { echo "deps.sh: cannot read $manifest" >&2; exit 2; }
jobs=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)

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

verify_one()
{
	case $1 in
	source|prebuilt) verify_file "$2" "$6" "$5" ;;
	git|fetchcontent) verify_checkout "$2" "$5" "$3" ;;
	esac
}

# Unpack prebuilt archive $2 (.tar.gz, .tar.xz or .tar.zst) of dependency
# $1 into $prefix/opt/$1: only the members prebuilt_members_$1 lists,
# without the archive's top directory, if that is defined, else all.
extract_prebuilt()
{
	local name=$1 file=$2 top members=()
	top=$(basename "$file")
	top=${top#"$name-"} top=${top%.tar.*}
	if declare -F "prebuilt_members_$name" >/dev/null; then
		read -ra members <<< "$("prebuilt_members_$name")"
		members=("${members[@]/#/$top/}")
	fi
	mkdir -p "$prefix/opt/$name"
	# Decompressed here: GNU tar does not detect compression on a pipe.
	case $file in
	*.zst) zstd -dcq --long=31 "$file" ;;
	*.xz) xz -dc "$file" ;;
	*.gz|*.tgz) gzip -dc "$file" ;;
	*) cat "$file" ;;
	esac | tar -xf - -C "$prefix/opt/$name" \
		${members[0]+--strip-components=1} "${members[@]+"${members[@]}"}" ||
		refuse "$name: cannot unpack $file"
}

# Print a hash of everything that shapes a build besides the pins: the
# generic CMake invocation (build_one) and each dependency's options on
# this platform.
build_options_hash()
{
	local f
	{
		declare -f build_one extract_prebuilt
		for f in $(declare -F | awk '$3 ~ /^(cmake_options|prebuilt_members)_/ {print $3}'); do
			echo "$f: $("$f")"
		done
	} | dev_sha256
}

# Print what a build records in deps.stamp.
stamp()
{
	dep_lines
	echo "options $(build_options_hash)"
}

build_one()
{
	local kind=$1 name=$2 src=$work/src/$2 opts=()
	case $kind in
	source) mkdir -p "$src"; tar -xf "$6" -C "$src" --strip-components=1 ;;
	# Out of source, straight from the verified checkout: a clone would
	# fetch the submodules from their remotes.
	git) src=$5 ;;
	fetchcontent) return 0 ;;
	prebuilt) extract_prebuilt "$name" "$6"; return 0 ;;
	esac
	declare -F "cmake_options_$name" >/dev/null &&
		read -ra opts <<< "$("cmake_options_$name" | tr '\n' ' ')"
	echo "build $name"
	if ! { cmake -S "$src" -B "$work/build/$name" -DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_INSTALL_PREFIX="$prefix" -DCMAKE_PREFIX_PATH="$prefix" \
		-DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_INSTALL_RPATH="$prefix/lib" \
		-DCMAKE_IGNORE_PREFIX_PATH='/opt/homebrew;/usr/local' \
		-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF \
		-DFETCHCONTENT_FULLY_DISCONNECTED=ON "${fetchcontent[@]+"${fetchcontent[@]}"}" \
		"${opts[@]+"${opts[@]}"}" > "$work/$name.log" 2>&1 &&
		cmake --build "$work/build/$name" --parallel "$jobs" >> "$work/$name.log" 2>&1 &&
		cmake --install "$work/build/$name" >> "$work/$name.log" 2>&1; }; then
		tail -n 30 "$work/$name.log" >&2
		refuse "$name: build failed; log in $work/$name.log"
	fi
}

if [[ $mode == stamp ]]; then
	stamp
	exit 0
fi
if [[ $mode != build ]]; then
	each_dep fetch_one
fi
if [[ $mode != fetch ]]; then
	each_dep verify_one
	# pkg-config sees the prefix only, never Homebrew's or the system's
	# libraries.
	export PKG_CONFIG_LIBDIR=$prefix/lib/pkgconfig PKG_CONFIG_PATH=
	work=$prefix-build
	rm -rf "$prefix" "$work"
	mkdir -p "$prefix" "$work"
	# Every build sees every `fetchcontent` checkout in place of its
	# download, and may download nothing else.
	fetchcontent=()
	add_fetchcontent()
	{
		[[ $1 == fetchcontent ]] || return 0
		fetchcontent+=("-DFETCHCONTENT_SOURCE_DIR_$(echo "$2" | tr '[:lower:]' '[:upper:]')=$5")
	}
	each_dep add_fetchcontent
	each_dep build_one
	stamp > "$prefix/deps.stamp"
	rm -rf "$work"
	echo "deps.sh: built $prefix"
fi
