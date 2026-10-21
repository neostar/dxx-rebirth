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
have() { command -v "$1" >/dev/null; }
probe_apple-clang()
{
	have clang && clang --version 2>/dev/null | sed -n 's/^Apple clang version \([0-9.]*\).*/\1/p' | head -n 1
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
	have clang && clang --version 2>/dev/null | sed -n 's/.*clang version \([0-9.]*\).*/\1/p' | head -n 1
}

probe_gcc() { have gcc && gcc -dumpfullversion 2>/dev/null; }
probe_uv() { have uv && uv --version 2>/dev/null | first_version; }
probe_cmake() { have cmake && cmake --version 2>/dev/null | first_version; }
probe_shellcheck() { have shellcheck && shellcheck --version 2>/dev/null | sed -n 's/^version: //p'; }
probe_ccache() { have ccache && ccache --version 2>/dev/null | head -n 1 | first_version; }
probe_scons() { have scons && scons --version 2>/dev/null | grep -i 'SCons:' | first_version; }

# The version of pkg-config module $1, or nothing when it is missing.
probe_pkg() { pkg-config --modversion "$1" 2>/dev/null; }

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

# The machine: jobs, RAM, and free disk on the host volume holding the
# repository and, if the Docker daemon is already running, inside its VM
# (where images and build caches live; on Linux the same host disk).
ram=$(sysctl -n hw.memsize 2>/dev/null ||
	awk '/^MemTotal:/ {print $2 * 1024}' /proc/meminfo 2>/dev/null)
echo "info machine: $dev_jobs jobs (DXX_JOBS), $((${ram:-0} / 1073741824)) GiB RAM"

# Report free space $2 (KiB) of place $1 against the limits.
check_disk()
{
	local free_gib=$(($2 / 1048576))
	if [[ $free_gib -lt 15 ]]; then
		echo "FAIL disk $1: $free_gib GiB free, below 15 GiB; container.sh prune drops old images"
		rc=1
	elif [[ $free_gib -lt 40 ]]; then
		echo "WARN disk $1: $free_gib GiB free, below 40 GiB; container.sh prune drops old images"
	else
		echo "ok   disk $1 $free_gib GiB free"
	fi
}

# Succeed if the Docker daemon is up, without starting it: its unix socket
# (DOCKER_HOST, else the current context's) exists, and OrbStack, which
# starts its VM on a connection, reports itself running.
docker_running()
{
	local host=${DOCKER_HOST-}
	command -v docker >/dev/null || return 1
	[[ -n $host ]] || host=$(docker context inspect --format '{{.Endpoints.docker.Host}}' 2>/dev/null)
	if [[ $host == *.orbstack* ]] && command -v orbctl >/dev/null; then
		[[ $(orbctl status 2>/dev/null) == Running ]] || return 1
	fi
	[[ $host == unix://* && -S ${host#unix://} ]]
}

check_disk host "$(df -Pk "$dev_dir" 2>/dev/null | awk 'NR == 2 {print $4}')"
# Docker's disk, without starting a container: its data root when that is
# on this machine (Linux), else (a VM: OrbStack, whose disk grows on the
# host volume, or Docker Desktop) what images and build cache take.
if ! docker_running; then
	echo "SKIP disk docker: daemon not running"
elif data=$(docker info --format '{{.DockerRootDir}}' 2>/dev/null) && [[ -n $data && -d $data ]]; then
	check_disk docker "$(df -Pk "$data" 2>/dev/null | awk 'NR == 2 {print $4}')"
elif used=$(docker system df --format '{{.Type}} {{.Size}}' 2>/dev/null) && [[ -n $used ]]; then
	echo "info disk docker (VM): $(awk '/^Images / {i = $2} /^Build Cache / {c = $3}
		END {print "images " i ", build cache " c}' <<<"$used")"
else
	echo "SKIP disk docker: docker info and docker system df failed"
fi

# Reference images: container.sh build keeps one.
if docker_running; then
	images=$(docker image ls --format '{{.Repository}}:{{.Tag}}' dxx-rebirth-ref | grep -c .)
	if [[ $images -gt 1 ]]; then
		echo "WARN images: $images dxx-rebirth-ref images; remove the old ones (docker rmi)"
	else
		echo "ok   images: $images dxx-rebirth-ref image"
	fi
fi

[[ $rc -eq 0 ]] && echo "doctor.sh: toolchain matches $(basename "$manifest")"
exit "$rc"
