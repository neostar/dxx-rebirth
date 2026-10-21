#!/usr/bin/env bash
# The fork's CI (ADR 4): local only, one command.
#
#   ci.sh [--fast] [--clean] [--container]
#
# Two legs, run at once:
# 1. native: tools/dev/check.sh on this machine (skipped with --container);
# 2. container: tools/dev/check.sh in the Linux reference container,
#    offline (container.sh check): by default on a copy of the working
#    tree; with --clean on a fresh clone of the commit under test, with no
#    build caches.
# Each leg's output is printed whole when both are done, native first.
# The whole output also goes to build/ci/ci.log (build/ is kept out of
# Time Machine), replaced by each run; its path is printed at the start and
# the end.  Read that file rather than redirecting ci.sh's output.  One
# ci.sh runs at a time per clone: a second one fails at once, naming the
# first (lock: build/ci/lock, taken over when its ci.sh no longer runs).
# --fast passes --fast to check.sh in both legs.  Both legs always run;
# exit status 1 names the failed ones, 2 is a usage error.  Build the image
# first with `container.sh build`.
set -u
dev_dir=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$dev_dir/../.." && pwd)
source "$dev_dir/env.sh"

usage()
{
	echo "usage: $0 [--fast] [--clean] [--container]" >&2
	exit 2
}

fast=() clean=() native=1
while [[ $# -gt 0 ]]; do
	case $1 in
	--fast) fast=(--fast) ;;
	--clean) clean=(--clean) ;;
	--container) native=0 ;;
	*) usage ;;
	esac
	shift
done

log=$root/build/ci/ci.log
lock=$root/build/ci/lock
dev_tm_exclude "$root/build" >/dev/null
if ! mkdir -p "${log%/*}"; then
	echo "ci.sh: cannot write $log" >&2
	exit 1
fi
# Take the lock, a directory holding the owner's pid (mkdir is atomic; macOS
# has no flock), or take it over once from a ci.sh that no longer runs: its
# pid no longer runs, or there is no pid file for 3 s (killed between mkdir
# and writing it).  A takeover holds a second lock, lock.takeover (mkdir
# again), checks again under it, and writes the new pid before letting it
# go.  Only a takeover or the owner's exit removes the lock, and only one
# takeover runs at a time, so of runs taking over at once one gets the
# lock, and none removes a lock taken since its check; the others fail at
# once.  The exit removes the lock only while its pid file is ours.
owned='' takeover='' logs=
trap '[[ -z $takeover ]] || rmdir "$lock.takeover"
[[ -z $owned || $(cat "$lock/pid" 2>/dev/null) != "$$" ]] || rm -rf "$lock"
rm -rf ${logs:+"$logs"}' EXIT
# Without an INT trap, bash ignores an INT that reaches it alone (kill, not
# Ctrl-C) while it waits for a command, and carries on once the command
# ends.  The trap exits then, through the EXIT trap, and dies of INT, so a
# calling shell stops too.  An INT ignored at entry (a background command)
# stays ignored.
trap 'trap - INT; kill -INT $$' INT
# Print the pid in the lock's pid file, waiting up to 3 s for one to appear.
lock_owner()
{
	local owner
	for _ in $(seq 30); do
		owner=$(cat "$lock/pid" 2>/dev/null)
		[[ -z $owner && -d $lock ]] || break
		sleep 0.1
	done
	printf '%s\n' "$owner"
}
# Succeed if the lock exists and its owner $1 (empty: no pid file) no
# longer runs.
lock_stale()
{
	[[ -d $lock ]] && ! { [[ -n $1 ]] && kill -0 "$1" 2>/dev/null; }
}
for try in 1 2; do
	mkdir "$lock" 2>/dev/null && { owned=1; echo $$ > "$lock/pid"; break; }
	owner=$(cat "$lock/pid" 2>/dev/null)
	if [[ $try -eq 2 ]] || ! lock_stale "$owner"; then
		[[ -n $owner ]] || owner=$(lock_owner)
		echo "ci.sh: another ci.sh (pid ${owner:-unknown}) is running; its log: $log" >&2
		echo "ci.sh: remove $lock if it no longer runs" >&2
		exit 1
	fi
	if ! mkdir "$lock.takeover" 2>/dev/null; then
		echo "ci.sh: another ci.sh is taking over the lock of one that no longer runs" >&2
		echo "ci.sh: remove $lock.takeover if none runs" >&2
		exit 1
	fi
	takeover=1
	lock_stale "$(lock_owner)" && rm -rf "$lock"
	mkdir "$lock" 2>/dev/null && { owned=1; echo $$ > "$lock/pid"; }
	rmdir "$lock.takeover"
	takeover=
	[[ -z $owned ]] || break
done
if ! : > "$log"; then
	echo "ci.sh: cannot write $log" >&2
	exit 1
fi
# Print the arguments as a line, also into the log.
say()
{
	printf '%s\n' "$*" | tee -a "$log"
}
say "ci.sh: log: $log"

logs=$(mktemp -d "${TMPDIR:-/tmp}/ci.XXXXXX")
legs=()
declare -A pid
if [[ $native -eq 1 ]]; then
	legs+=(native)
	{
		echo "== ci: native check.sh ${fast[*]-}"
		"$dev_dir/check.sh" "${fast[@]+"${fast[@]}"}"
	} > "$logs/native" 2>&1 &
	pid[native]=$!
fi
legs+=(container)
{
	echo "== ci: container check.sh ${clean[*]-} ${fast[*]-}"
	"$dev_dir/container.sh" check "${clean[@]+"${clean[@]}"}" "${fast[@]+"${fast[@]}"}"
} > "$logs/container" 2>&1 &
pid[container]=$!

failed=()
for leg in "${legs[@]}"; do
	wait "${pid[$leg]}" || failed+=("$leg")
	tee -a "$log" < "$logs/$leg"
done

if [[ ${#failed[@]} -ne 0 ]]; then
	say "ci.sh: failed: ${failed[*]}" >&2
	say "ci.sh: log: $log" >&2
	exit 1
fi
say "ci.sh: passed"
say "ci.sh: log: $log"
