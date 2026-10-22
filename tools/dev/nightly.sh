#!/usr/bin/env bash
# The nightly tier (ADR 5): slow, thorough checks, one command, local only.
#
#   nightly.sh [--steps S,...] [--endurance SECONDS]
#   nightly.sh --leg native|container [--steps S,...] [--endurance SECONDS]
#
# Without --leg, both legs run at once, as in ci.sh: the native leg here
# (the Mac) and the container leg in the Linux reference container
# (container.sh run --clean: the commit under test, offline).  Each leg's
# output is printed whole when both are done, native first, and goes to
# build/nightly/nightly.log, replaced by each run.  --leg runs one leg's
# steps on this machine (the container leg inside the container).
#
# Steps, each printing what it runs, a SKIP naming what is missing, or
# its failure:
#   native leg:
#   analyzer        the Clang Static Analyzer (clang-tidy clang-analyzer-*,
#                   pinned LLVM, deeper limits than the check tier's) on
#                   every fork C++ file in the debug-harness build's
#                   compilation database
#   thread_safety   clang's -Wthread-safety on the same files
#   container leg:
#   glibcxx_debug   the unit tests built with libstdc++'s debug mode
#                   (_GLIBCXX_DEBUG: checked iterators) and run
# --steps runs only the steps named (of the leg's); --endurance sets the
# endurance run's length.  DXX_NIGHTLY_DATABASE names a compilation
# database to analyse instead of building the harness (the tests).
# Exit status 1 names the failed steps, 2 is a usage error.
set -u
dev_dir=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$dev_dir/../.." && pwd)
source "$dev_dir/env.sh"

usage()
{
	echo "usage: $0 [--leg native|container] [--steps S,...] [--endurance SECONDS]" >&2
	exit 2
}

native_steps=(analyzer thread_safety)
container_steps=(glibcxx_debug)

harness_dir=build/nightly/harness
clang_tidy=$(dev_llvm_tool clang-tidy)
# The LLVM release finds the macOS SDK through SDKROOT (as in format.sh).
if [[ $dev_os == darwin && -z ${SDKROOT-} ]]; then
	SDKROOT=$(xcrun --show-sdk-path 2>/dev/null) && export SDKROOT
fi

# Build the game with the dev profile (hardened) and the debug harness
# into $harness_dir, with its compilation database.
build_harness()
{
	"$dev_dir/build.sh" dev dbg_harness=1 "builddir=$harness_dir" \
		"compilation_database=$harness_dir/compile_commands.json"
}

# Print the compilation database to analyse: DXX_NIGHTLY_DATABASE, else
# the harness build's, built first.
database()
{
	if [[ -n ${DXX_NIGHTLY_DATABASE-} ]]; then
		printf '%s\n' "$DXX_NIGHTLY_DATABASE"
		return
	fi
	build_harness >&2 || return
	printf '%s\n' "$root/$harness_dir/compile_commands.json"
}

# Print the fork directories (tools/dev/fork-dirs), one per line.
fork_dirs()
{
	grep -v -e '^#' -e '^$' "$dev_dir/fork-dirs"
}

# Print, NUL-separated, every C++ source file of database $1 that lies in
# a fork directory, as an absolute path, each once.
fork_sources()
{
	python3 - "$1" "$root" <<'PY'
import json, os, sys
db, root = sys.argv[1], sys.argv[2]
dirs = [d.strip() for d in open(os.path.join(root, 'tools/dev/fork-dirs'))
	if d.strip() and not d.startswith('#')]
seen = set()
for e in json.load(open(db)):
	f = os.path.normpath(os.path.join(e['directory'], e['file']))
	rel = os.path.relpath(f, root)
	if not f.endswith(('.cpp', '.cc', '.cxx')) or f in seen:
		continue
	if any(rel == d or rel.startswith(d + '/') for d in dirs):
		seen.add(f)
		sys.stdout.write(f + '\0')
PY
}

# Run the pinned clang-tidy with config $1 on every fork C++ file of the
# database, reporting findings in fork files only; print how many files
# step $2 checked.  The remaining arguments go to clang-tidy.
tidy_fork()
{
	local config=$1 step=$2 db files=() filter
	shift 2
	if [[ ! -x $clang_tidy ]]; then
		echo "SKIP $step: $clang_tidy not found; run tools/dev/deps.sh build"
		return 0
	fi
	db=$(database) || { echo "$step: building the harness failed"; return 1; }
	mapfile -d '' -t files < <(fork_sources "$db")
	echo "$step: ${#files[@]} fork files in $db"
	[[ ${#files[@]} -gt 0 ]] || { echo "$step: no fork file to check"; return 1; }
	filter="^$root/($(fork_dirs | paste -sd '|' -))/"
	"$clang_tidy" -p "$(dirname "$db")" --quiet --config="$config" \
		--header-filter="$filter" "$@" "${files[@]}" 2>&1 |
		grep -v -e '^[0-9]* warnings\? generated\.$' -e '^Suppressed [0-9]* warnings'
	return "${PIPESTATUS[0]}"
}

# The Clang Static Analyzer on every fork file, with deeper limits than
# the check tier's clang-tidy run: four times the exploded-graph nodes and
# twice the loop unrolling.
step_analyzer()
{
	tidy_fork "{Checks: '-*,clang-analyzer-*', WarningsAsErrors: '*'}" analyzer \
		--extra-arg=-Xclang --extra-arg=-analyzer-config \
		--extra-arg=-Xclang --extra-arg=max-nodes=900000 \
		--extra-arg=-Xclang --extra-arg=-analyzer-max-loop --extra-arg=-Xclang --extra-arg=8
}

# clang's thread-safety analysis on every fork file (libc++'s std::mutex
# carries the annotations); says so when no fork file takes a lock.
step_thread_safety()
{
	local d n=0
	tidy_fork "{Checks: '-*,clang-diagnostic-thread-safety*', WarningsAsErrors: '*'}" \
		thread_safety --allow-no-checks --extra-arg=-Wthread-safety || return
	while IFS= read -r d; do
		n=$((n + $(grep -rlE --include='*.[ch]' --include='*.[ch]pp' --include='*.mm' \
			'std::(mutex|shared_mutex|lock_guard|unique_lock|scoped_lock)|SDL_LockMutex' \
			"$root/$d" 2>/dev/null | wc -l)))
	done < <(fork_dirs)
	[[ $n -gt 0 ]] || echo "thread_safety: no fork file takes a lock yet; all compile cleanly with -Wthread-safety"
}

# libstdc++'s debug mode: build the unit tests with _GLIBCXX_DEBUG, beside
# the hardened flags, into build/nightly/glibcxx-debug and run them all
# (scons -k).  Debug mode changes the layout of the standard containers,
# so a test that reads a class of the prebuilt Boost.Test library holding
# one gets the library's layout wrong and crashes: a test failing for that
# is listed in tools/dev/glibcxx-debug-known with the reason, and printed
# rather than failing the step.  Any other failure fails it.
step_glibcxx_debug()
{
	local flags out rc n name reason unexpected=()
	flags="$(python3 "$dev_dir/hardening.py") -D_GLIBCXX_DEBUG"
	out=$("$dev_dir/build.sh" dev builddir=build/nightly/glibcxx-debug \
		compilation_database=build/nightly/glibcxx-debug/compile_commands.json \
		"CPPFLAGS=$flags" -k check 2>&1) && rc=0 || rc=$?
	printf '%s\n' "$out"
	[[ $rc -eq 0 ]] && return 0
	while IFS= read -r n; do
		name=${n##*/}
		name=${name%.check}
		reason=$(sed -n "s/^$name -- //p" "$dev_dir/glibcxx-debug-known")
		if [[ -n $reason ]]; then
			echo "glibcxx_debug: $name fails for a known reason: $reason"
		else
			unexpected+=("$n")
		fi
	done < <(awk '/^Failed node list:/ { f = 1; next } f && /^\t/ { print $1; next } { f = 0 }' \
		<<<"$out")
	[[ $(grep -c '^Failed node list:' <<<"$out") -gt 0 ]] || return 1
	[[ ${#unexpected[@]} -eq 0 ]] || { echo "glibcxx_debug: failed: ${unexpected[*]}"; return 1; }
}

leg='' only='' endurance=''
while [[ $# -gt 0 ]]; do
	case $1 in
	--leg) [[ $# -ge 2 && $2 =~ ^(native|container)$ ]] || usage; leg=$2; shift ;;
	--steps) [[ $# -ge 2 ]] || usage; only=$2; shift ;;
	--endurance) [[ $# -ge 2 && $2 =~ ^[1-9][0-9]*$ ]] || usage; endurance=$2; shift ;;
	*) usage ;;
	esac
	shift
done
export DXX_NIGHTLY_ENDURANCE=${endurance:-${DXX_NIGHTLY_ENDURANCE:-3600}}

# Run leg $1's steps (those named by --steps, if given); fail if any did.
run_leg()
{
	local -n all=${1}_steps
	local s steps=() failed=() start
	if [[ -n $only ]]; then
		for s in ${only//,/ }; do
			[[ " ${native_steps[*]} ${container_steps[*]} " == *" $s "* ]] || usage
			[[ " ${all[*]} " == *" $s "* ]] && steps+=("$s")
		done
	else
		steps=("${all[@]}")
	fi
	for s in "${steps[@]+"${steps[@]}"}"; do
		echo "== $s"
		start=$EPOCHSECONDS
		"step_$s" || failed+=("$s")
		echo "-- $s: $((EPOCHSECONDS - start)) s"
	done
	if [[ ${#failed[@]} -ne 0 ]]; then
		echo "nightly.sh: failed: ${failed[*]}"
		return 1
	fi
	echo "nightly.sh: $1 leg passed"
}

if [[ -n $leg ]]; then
	run_leg "$leg"
	exit
fi
