# shellcheck shell=bash
# Environment shared by the tools in tools/dev and their tests; sourced,
# not run.

# git's repository variables.  Inside a git hook git exports some of them
# for the repository being committed to; the shell tests must never see
# any of them, or their throwaway repositories become that repository.
# shellcheck disable=SC2034  # used by the scripts sourcing this file
dev_git_repo_vars=(GIT_DIR GIT_INDEX_FILE GIT_WORK_TREE GIT_COMMON_DIR
	GIT_OBJECT_DIRECTORY GIT_ALTERNATE_OBJECT_DIRECTORIES GIT_PREFIX
	GIT_NAMESPACE)

# Sanitizer failures exit non-zero instead of calling abort(): on macOS an
# abort brings up a CrashReporter dialog.  LeakSanitizer is unsupported on
# Apple Silicon macOS (use `leaks --atExit` there); elsewhere it stays on.
# Settings already in the environment win.
if [[ $(uname -s) == Darwin ]]; then
	_dev_detect_leaks=0
else
	_dev_detect_leaks=1
fi
export ASAN_OPTIONS=${ASAN_OPTIONS:-abort_on_error=0:halt_on_error=1:detect_leaks=$_dev_detect_leaks}
export UBSAN_OPTIONS=${UBSAN_OPTIONS:-print_stacktrace=1:halt_on_error=1:abort_on_error=0}
unset _dev_detect_leaks

# This machine's OS (darwin, linux) and platform (OS-ARCH, as in the
# manifest's `prebuilt` lines).
# shellcheck disable=SC2034  # used by the scripts sourcing this file
dev_os=$(uname -s | tr '[:upper:]' '[:lower:]')
# shellcheck disable=SC2034
dev_platform=$dev_os-$(uname -m)

# How many jobs the tools run at once: DXX_JOBS, else the performance-core
# count (Apple Silicon), else every CPU.
# shellcheck disable=SC2034  # used by the scripts sourcing this file
dev_jobs=${DXX_JOBS:-$(sysctl -n hw.perflevel0.physicalcpu 2>/dev/null ||
	sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)}

# The pinned dependencies (tools/dev/toolchain.pins): deps.sh fetches
# their sources once into the sha256-verified cache and builds them into the
# prefix, one per OS and architecture; both are gitignored under
# third_party/.  DXX_DEPS_CACHE and DXX_DEPS_PREFIX override them, so
# worktrees can share one cache.
_dev_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
# shellcheck disable=SC2034  # used by the scripts sourcing this file
dev_deps_cache=${DXX_DEPS_CACHE:-$_dev_root/third_party/cache}
# shellcheck disable=SC2034
dev_deps_prefix=${DXX_DEPS_PREFIX:-$_dev_root/third_party/$dev_platform}
# The Python venv from tools/dev/requirements.lock (deps.sh venv): Python,
# FLIP and numpy, and the pinned shellcheck and cmake.  When it exists its
# bin comes first on PATH, so the fork's tools run the pinned versions.
# DXX_VENV overrides it.
# shellcheck disable=SC2034
dev_venv=${DXX_VENV:-$_dev_root/third_party/venv-$dev_platform}
if [[ -x $dev_venv/bin/python3 && :$PATH: != *":$dev_venv/bin:"* ]]; then
	export PATH=$dev_venv/bin:$PATH
fi
unset _dev_root

# Fill array $1 with NAME=VALUE for each variable the shell tests may see
# (ADR 4): the basics, every DXX_* and the sanitizer options set above;
# everything else in the caller's environment stays out.
dev_test_env()
{
	local -n _env=$1
	local v
	_env=()
	for v in PATH HOME TMPDIR LANG LC_ALL TZ USER ASAN_OPTIONS UBSAN_OPTIONS \
		$(compgen -e | grep '^DXX_'); do
		[[ -v $v ]] && _env+=("$v=${!v}")
	done
}

# Print the path of LLVM tool $1 (clangd, clang-format, clang-tidy): the
# pinned LLVM release that deps.sh installs into the prefix, never
# Homebrew's or Xcode's.
dev_llvm_tool()
{
	echo "$dev_deps_prefix/opt/llvm/bin/$1"
}

# Print the sha256 of file $1, or of the standard input without one:
# sha256sum on Linux, shasum on the Mac.
dev_sha256()
{
	if command -v sha256sum >/dev/null; then
		sha256sum "$@"
	else
		shasum -a 256 "$@"
	fi | awk '{print $1}'
}

# Print the Python version that manifest $1 pins for the venv.
dev_venv_python()
{
	awk '$1 == "venv" && $2 == "python" {print $3}' "$1"
}

# Print what the venv of manifest $1 records in venv.stamp: its Python
# version and the sha256 of the lockfile next to the manifest.
dev_venv_pins()
{
	printf 'python %s\nlock %s\n' "$(dev_venv_python "$1")" \
		"$(dev_sha256 "$(dirname "$1")/requirements.lock" 2>/dev/null)"
}
