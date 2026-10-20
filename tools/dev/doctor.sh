#!/usr/bin/env bash
# Check this machine's toolchain against tools/dev/toolchain.pins, so
# two runs on two days use the same tools (ADR 1, ticket #57).
#
#   doctor.sh [--manifest FILE] [--prefix DIR] [--venv DIR]
#
# Each `tool <name> <platform> <version>` line whose platform is `any` or
# this machine's (darwin, linux; DOCTOR_PLATFORM overrides uname) is probed
# with probe_<name> below and must report exactly <version>.  A missing
# tool is drift too.  The dependency prefix (default: env.sh's
# dev_deps_prefix) must have been built by deps.sh from the manifest's
# current dependency lines and build options; the venv (default: dev_venv)
# from the `venv python` pin and the requirements.lock next to the
# manifest.  Either not made
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

# clangd, clang-format and clang-tidy must come from one LLVM release, so
# they report one version or none.
probe_llvm()
{
	local t v seen=''
	for t in clangd clang-format clang-tidy; do
		v=$("$(dev_llvm_tool "$t")" --version 2>/dev/null | grep -i version | first_version)
		[[ -n $v ]] || { echo "($t missing)"; return 0; }
		[[ -z $seen || $seen == "$v" ]] || { echo "$seen ($t is $v)"; return 0; }
		seen=$v
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

rc=0
while read -r kind name where want _; do
	[[ $kind == tool ]] || continue
	[[ $where == any || $where == "$platform" ]] || continue
	if ! declare -F "probe_$name" >/dev/null; then
		echo "FAIL $name: doctor.sh has no probe for it"
		rc=1
		continue
	fi
	got=$("probe_$name")
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
	else
		echo "ok   venv python $got_python"
	fi
fi

[[ $rc -eq 0 ]] && echo "doctor.sh: toolchain matches $(basename "$manifest")"
exit "$rc"
