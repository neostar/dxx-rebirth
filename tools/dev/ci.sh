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
# One runs at a time on the machine: a ci.sh in another worktree waits,
# naming the running one, up to DXX_CI_MAX_WAIT seconds (default 3600),
# then fails (lock: ~/Library/Caches/dxx-rebirth/ci.lock, on Linux under
# ~/.cache; DXX_CI_MACHINE_LOCK overrides it).
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
if [[ $dev_os == darwin ]]; then
	machine_lock=${DXX_CI_MACHINE_LOCK:-$HOME/Library/Caches/dxx-rebirth/ci.lock}
else
	machine_lock=${DXX_CI_MACHINE_LOCK:-${XDG_CACHE_HOME:-$HOME/.cache}/dxx-rebirth/ci.lock}
fi
dev_tm_exclude "$root/build" >/dev/null
if ! mkdir -p "${log%/*}" "${machine_lock%/*}"; then
	echo "ci.sh: cannot write $log" >&2
	exit 1
fi
# A lock is a directory holding its owner's pid and worktree (mkdir is
# atomic; macOS has no flock).  try_lock takes one, or takes it over once
# from a ci.sh that no longer runs: its pid no longer runs, or there is no
# pid file for 3 s (killed between mkdir and writing it).  A takeover holds
# a second lock, <lock>.takeover (mkdir again), checks again under it, and
# writes the new pid before letting it go.  Only a takeover or the owner's
# exit removes a lock, and only one takeover runs at a time, so of runs
# taking over at once one gets the lock, and none removes a lock taken
# since its check.  The exit removes a lock only while its pid file is ours.
owned=() takeover='' logs=
trap '[[ -z $takeover ]] || rmdir "$takeover"
for l in "${owned[@]+"${owned[@]}"}"; do
	[[ $(cat "$l/pid" 2>/dev/null) != "$$" ]] || rm -rf "$l"
done
rm -rf ${logs:+"$logs"}' EXIT
# Without an INT trap, bash ignores an INT that reaches it alone (kill, not
# Ctrl-C) while it waits for a command, and carries on once the command
# ends.  The trap exits then, through the EXIT trap, and dies of INT, so a
# calling shell stops too.  An INT ignored at entry (a background command)
# stays ignored.
trap 'trap - INT; kill -INT $$' INT
# Print the pid in lock $1's pid file, waiting up to 3 s for one to appear.
lock_owner()
{
	local owner
	for _ in $(seq 30); do
		owner=$(cat "$1/pid" 2>/dev/null)
		[[ -z $owner && -d $1 ]] || break
		sleep 0.1
	done
	printf '%s\n' "$owner"
}
# Succeed if lock $1 exists and its owner $2 (empty: no pid file) no longer
# runs.
lock_stale()
{
	[[ -d $1 ]] && ! { [[ -n $2 ]] && kill -0 "$2" 2>/dev/null; }
}
# Make lock $1, which mkdir just made, ours.
own_lock()
{
	owned+=("$1")
	printf '%s\n' "$root" > "$1/worktree"
	echo $$ > "$1/pid"
}
# Try once to take lock $1.  Status 0: taken; 1: a running ci.sh holds it,
# its pid in held_by; 2: another ci.sh is taking over a stale one.
try_lock()
{
	local l=$1 try got=
	held_by=
	for try in 1 2; do
		mkdir "$l" 2>/dev/null && { own_lock "$l"; return 0; }
		held_by=$(cat "$l/pid" 2>/dev/null)
		if [[ $try -eq 2 ]] || ! lock_stale "$l" "$held_by"; then
			[[ -n $held_by ]] || held_by=$(lock_owner "$l")
			return 1
		fi
		mkdir "$l.takeover" 2>/dev/null || return 2
		takeover=$l.takeover
		lock_stale "$l" "$(lock_owner "$l")" && rm -rf "$l"
		mkdir "$l" 2>/dev/null && { own_lock "$l"; got=1; }
		rmdir "$l.takeover"
		takeover=
		[[ -z $got ]] || return 0
	done
	return 1
}
try_lock "$lock"
case $? in
1)
	echo "ci.sh: another ci.sh (pid ${held_by:-unknown}) is running; its log: $log" >&2
	echo "ci.sh: remove $lock if it no longer runs" >&2
	exit 1 ;;
2)
	echo "ci.sh: another ci.sh is taking over the lock of one that no longer runs" >&2
	echo "ci.sh: remove $lock.takeover if none runs" >&2
	exit 1 ;;
esac
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

# One ci.sh at a time on this machine, whatever its worktree: parallel runs
# overload it (budget overruns, timing flakes).  Wait for the machine lock,
# polling every DXX_CI_POLL seconds (default 5), up to DXX_CI_MAX_WAIT
# seconds (default 3600), then fail.  It is taken after the worktree's own
# lock, always in that order.
max_wait=${DXX_CI_MAX_WAIT:-3600}
[[ $max_wait =~ ^[0-9]+$ ]] || { echo "ci.sh: DXX_CI_MAX_WAIT is '$max_wait', not seconds" >&2; exit 2; }
waited_for='' wait_start=$EPOCHSECONDS
while ! try_lock "$machine_lock"; do
	if [[ -n $held_by && $held_by != "$waited_for" ]]; then
		waited_for=$held_by
		say "ci.sh: waiting for ci.sh pid $held_by in $(cat "$machine_lock/worktree" 2>/dev/null ||
			echo 'an unknown worktree') (machine lock $machine_lock, up to $max_wait s)"
	fi
	if ((EPOCHSECONDS - wait_start >= max_wait)); then
		say "ci.sh: gave up after $max_wait s waiting for ci.sh pid ${held_by:-unknown};" 			"remove $machine_lock if it no longer runs" >&2
		exit 1
	fi
	sleep "${DXX_CI_POLL:-5}"
done

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
