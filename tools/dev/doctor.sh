#!/usr/bin/env bash
# Check this machine's toolchain against tools/dev/toolchain.pins, so
# two runs on two days use the same tools (ADR 1, ticket #57).
#
#   doctor.sh [--manifest FILE] [--prefix DIR] [--venv DIR]
#
# Each `tool <name> <platform> <version>` line whose platform is `any` or
# this machine's (darwin, linux; DOCTOR_PLATFORM overrides uname) is probed
# with probe_<name> below and must report exactly <version>; each `pkg
# <module> <platform> <version>` line with pkg-config.  A missing tool or
# library is drift too.  The dependency prefix (default: env.sh's
# dev_deps_prefix) must have been built by deps.sh from the manifest's
# current dependency lines and build options; the venv (default: dev_venv)
# from the `venv python` pin and the requirements.lock next to the
# manifest, with exactly the locked packages installed.  Either not made
# yet is a SKIP.  Exit status 1 means drift, 2 a usage error.
# The probes are called by name, as probe_<name>.
# shellcheck disable=SC2329
set -u
dev_dir=$(cd "$(dirname "$0")" && pwd)
source "$dev_dir/env.sh"

usage()
{
	echo "usage: $0 [--manifest FILE] [--prefix DIR] [--venv DIR]" >&2
	exit 2
}

manifest=$dev_dir/toolchain.pins
prefix=$dev_deps_prefix
venv=$dev_venv
while [[ $# -gt 0 ]]; do
	case $1 in
	--manifest) [[ $# -ge 2 ]] || usage; manifest=$2; shift 2 ;;
	--prefix) [[ $# -ge 2 ]] || usage; prefix=$2; shift 2 ;;
	--venv) [[ $# -ge 2 ]] || usage; venv=$2; shift 2 ;;
	*) usage ;;
	esac
done
[[ -r $manifest ]] || { echo "doctor.sh: cannot read $manifest" >&2; exit 2; }

platform=${DOCTOR_PLATFORM:-$dev_os}

# Print the first X.Y[.Z] version number in the standard input.
first_version()
{
	grep -oE '[0-9]+\.[0-9]+(\.[0-9]+)?' | head -n 1
}

# Each probe prints the version of its tool, or nothing when it is missing.
probe_apple-clang()
{
	command -v clang >/dev/null || return 0
	clang --version 2>/dev/null | sed -n 's/^Apple clang version \([0-9.]*\).*/\1/p' | head -n 1
}

# clangd, clang-format and clang-tidy from the pinned LLVM release in the
# prefix: each still the program deps.sh unpacked (bin.sha256), all of one
# version, and clangd and clang-format built from one source commit
# (clang-tidy does not report its commit).
probe_llvm()
{
	local dir=$prefix/opt/llvm t v line src seen='' seen_src=''
	for t in clangd clang-format clang-tidy; do
		v=$("$dir/bin/$t" --version 2>/dev/null | grep -i version | first_version)
		[[ -n $v ]] || { echo "($t missing)"; return 0; }
		[[ -z $seen || $seen == "$v" ]] || { echo "$seen ($t is $v)"; return 0; }
		seen=$v
		line=$(grep -F "  bin/$t" "$dir/bin.sha256" 2>/dev/null)
		[[ -n $line && $(dev_sha256 "$dir/bin/$t") == "${line%% *}" ]] ||
			{ echo "$v (bin/$t is not the pinned release's)"; return 0; }
		[[ $t == clang-tidy ]] && continue
		src=$("$dir/bin/$t" --version 2>/dev/null | grep -oE '\(https?://[^)]*\)' | head -n 1)
		[[ -z $seen_src || $seen_src == "$src" ]] ||
			{ echo "$v ($t is from another source)"; return 0; }
		seen_src=$src
	done
	echo "$seen"
}

# Clang beside GCC in the container (Apple clang on the Mac is apple-clang).
probe_clang()
{
	command -v clang >/dev/null || return 0
	clang --version 2>/dev/null | sed -n 's/.*clang version \([0-9.]*\).*/\1/p' | head -n 1
}

probe_gcc()
{
	command -v gcc >/dev/null || return 0
	gcc -dumpfullversion 2>/dev/null
}

probe_uv()
{
	command -v uv >/dev/null || return 0
	uv --version 2>/dev/null | first_version
}

probe_cmake()
{
	command -v cmake >/dev/null || return 0
	cmake --version 2>/dev/null | first_version
}

probe_shellcheck()
{
	command -v shellcheck >/dev/null || return 0
	shellcheck --version 2>/dev/null | sed -n 's/^version: //p'
}

probe_ccache()
{
	command -v ccache >/dev/null || return 0
	ccache --version 2>/dev/null | head -n 1 | first_version
}

probe_scons()
{
	command -v scons >/dev/null || return 0
	scons --version 2>/dev/null | grep -i 'SCons:' | first_version
}

# The version of pkg-config module $1, or nothing when it is missing.
probe_pkg()
{
	pkg-config --modversion "$1" 2>/dev/null
}

rc=0
while read -r kind name where want _; do
	[[ $kind == tool || $kind == pkg ]] || continue
	[[ $where == any || $where == "$platform" ]] || continue
	if [[ $kind == pkg ]]; then
		got=$(probe_pkg "$name")
	elif ! declare -F "probe_$name" >/dev/null; then
		echo "FAIL $name: doctor.sh has no probe for it"
		rc=1
		continue
	else
		got=$("probe_$name")
	fi
	if [[ -z $got ]]; then
		echo "FAIL $name: not found, manifest pins $want"
		rc=1
	elif [[ $got != "$want" ]]; then
		echo "FAIL $name: found $got, manifest pins $want"
		rc=1
	else
		echo "ok   $name $got"
	fi
done < <(grep -v '^[[:space:]]*#' "$manifest")

# The dependency prefix records the dependency lines and build options it
# was built from.
if [[ ! -d $prefix ]]; then
	echo "SKIP deps: $prefix not built; run tools/dev/deps.sh"
elif [[ $("$dev_dir/deps.sh" --manifest "$manifest" --prefix "$prefix" stamp) != \
	"$(cat "$prefix/deps.stamp" 2>/dev/null)" ]]; then
	echo "FAIL deps: $prefix was built from other pins; run tools/dev/deps.sh"
	rc=1
else
	echo "ok   deps $prefix"
fi

# Print the packages locked but not installed in the venv (-name==version)
# and installed but not locked (+name==version) on one line; nothing when
# they agree.
venv_drift()
{
	local lock
	lock=$(dirname "$manifest")/requirements.lock
	comm -3 \
		<(sed -n 's/^\([A-Za-z0-9._-]*==[^ ]*\) .*/\1/p' "$lock" | tr '[:upper:]_' '[:lower:]-' | sort) \
		<(uv pip freeze --python "$venv/bin/python3" 2>/dev/null | tr '[:upper:]_' '[:lower:]-' | sort) |
		awk '/^\t/ {sub(/^\t/, "+"); print; next} {print "-" $0}' | paste -sd ' ' -
}

# The venv records the Python and lockfile it was made from, and its
# Python must still be that version.
want_python=$(dev_venv_python "$manifest")
if [[ -n $want_python ]]; then
	got_python=$("$venv/bin/python3" -c 'import platform; print(platform.python_version())' 2>/dev/null)
	if [[ ! -d $venv ]]; then
		echo "SKIP venv: $venv not made; run tools/dev/deps.sh venv"
	elif [[ $got_python != "$want_python" ]]; then
		echo "FAIL venv python: found ${got_python:-none}, manifest pins $want_python"
		rc=1
	elif [[ $(dev_venv_pins "$manifest") != "$(cat "$venv/venv.stamp" 2>/dev/null)" ]]; then
		echo "FAIL venv: $venv was made from another lockfile or Python; run tools/dev/deps.sh venv"
		rc=1
	elif extra=$(venv_drift) && [[ -n $extra ]]; then
		echo "FAIL venv: installed packages differ from requirements.lock: $extra; run tools/dev/deps.sh venv"
		rc=1
	else
		echo "ok   venv python $got_python"
	fi
fi

# The machine: jobs, RAM and free disk on the volumes holding the repository
# and Docker's data (DXX_DOCKER_DATA; OrbStack's or Docker Desktop's on the
# Mac, /var/lib/docker on Linux), the lower of the two.
ram=$(sysctl -n hw.memsize 2>/dev/null ||
	awk '/^MemTotal:/ {print $2 * 1024}' /proc/meminfo 2>/dev/null)
echo "info machine: $dev_jobs jobs (DXX_JOBS), $((${ram:-0} / 1073741824)) GiB RAM"
disk_paths=("$dev_dir")
for d in "${DXX_DOCKER_DATA-}" "$HOME/.orbstack" "$HOME/Library/Containers/com.docker.docker" \
	/var/lib/docker; do
	[[ -n $d && -d $d ]] && { disk_paths+=("$d"); break; }
done
free_gib=$(df -Pk "${disk_paths[@]}" 2>/dev/null | awk 'NR > 1 && (min == "" || $4 < min) {min = $4}
	END {print int(min / 1048576)}')
if [[ $free_gib -lt 15 ]]; then
	echo "FAIL disk: $free_gib GiB free, below 15 GiB; run tools/dev/container.sh prune"
	rc=1
elif [[ $free_gib -lt 40 ]]; then
	echo "WARN disk: $free_gib GiB free, below 40 GiB; run tools/dev/container.sh prune"
else
	echo "ok   disk $free_gib GiB free"
fi

# Reference images: container.sh build keeps one.
if command -v docker >/dev/null && docker info >/dev/null 2>&1; then
	images=$(docker image ls --format '{{.Repository}}:{{.Tag}}' dxx-rebirth-ref | grep -c .)
	if [[ $images -gt 1 ]]; then
		echo "WARN images: $images dxx-rebirth-ref images; remove the old ones (docker rmi)"
	else
		echo "ok   images: $images dxx-rebirth-ref image"
	fi
fi

[[ $rc -eq 0 ]] && echo "doctor.sh: toolchain matches $(basename "$manifest")"
exit "$rc"
