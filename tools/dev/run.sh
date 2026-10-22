#!/usr/bin/env bash
# Run a binary the way every dev and probe run should: windowless and silent.
#
#   run.sh BINARY [ARG...]
#
# Sources env.sh (sanitizer settings without abort()), checks that every
# library the binary links resolves (macOS: otool -L; Linux: ldd; else the
# check is skipped with a note), then runs it under no-window.sh.  An
# unresolved library fails with exit status 1 before launch, instead of a
# dyld crash and its dialog.  A new crash report of the binary (its pid and
# name) fails the run with status 1 and is named (macOS).  TERM, INT or HUP
# to run.sh ends the run: run.sh waits for all of it to quit, and kills what
# still runs RUN_KILL_AFTER seconds (default 10) later.
#
# SDL_VIDEO_HIGHDPI_DISABLED=1 renders at 1x on Retina: a bare dev binary
# has no Info.plist with NSHighResolutionCapable=false, so the game's
# SDL_WINDOW_ALLOW_HIGHDPI would otherwise draw into a quarter of the window.
set -u
dev_dir=$(cd "$(dirname "$0")" && pwd)
source "$dev_dir/env.sh"
[[ $# -gt 0 ]] || { echo "usage: $0 BINARY [ARG...]" >&2; exit 2; }
export SDL_VIDEO_HIGHDPI_DISABLED=1
bin=$1
[[ $bin == */* ]] || bin=$(command -v "$bin") || { echo "run.sh: $1: not found" >&2; exit 1; }

# Print each library of Mach-O binary $1 that does not resolve.  Libraries
# under /usr/lib and /System live in the dyld shared cache, not on disk.
unresolved_macho()
{
	local exe_dir lib lib2 rp found rpaths=()
	exe_dir=$(cd "$(dirname "$1")" && pwd)
	while read -r rp; do
		rpaths+=("${rp/@executable_path/$exe_dir}")
	done < <(otool -l "$1" | awk '$1 == "cmd" && $2 == "LC_RPATH" { getline; getline; print $2 }')
	while read -r lib; do
		case $lib in
		/usr/lib/*|/System/*) continue ;;
		@rpath/*)
			found=
			for rp in "${rpaths[@]+"${rpaths[@]}"}"; do
				rp=${rp/@loader_path/$exe_dir}
				[[ -e $rp/${lib#@rpath/} ]] && found=1
			done
			[[ -n $found ]] || echo "$lib" ;;
		@executable_path/*|@loader_path/*)
			lib2=${lib/@executable_path/$exe_dir}
			[[ -e ${lib2/@loader_path/$exe_dir} ]] || echo "$lib" ;;
		*) [[ -e $lib ]] || echo "$lib" ;;
		esac
	done < <(otool -L "$1" | awk 'NR > 1 { print $1 }')
}

if [[ $dev_os == darwin ]] && command -v otool >/dev/null; then
	missing=$(unresolved_macho "$bin")
elif command -v ldd >/dev/null; then
	missing=$(ldd "$bin" 2>/dev/null | awk '/not found/ { print $1 }')
else
	echo "run.sh: SKIP library check: neither otool nor ldd found" >&2
	missing=
fi
if [[ -n $missing ]]; then
	echo "run.sh: $bin: unresolved libraries:" >&2
	while IFS= read -r lib; do echo "  $lib"; done <<<"$missing" >&2
	exit 1
fi
# A crash report of the binary, written after the start, fails the run
# (macOS; NO_WINDOW_REPORTS_DIR overrides the directory, as for
# no-window.sh), also one no-window.sh misses because ReportCrash wrote it
# after no-window.sh looked.  A report is the binary's when its pid is the
# binary's and its procName, app_name or the last part of its procPath
# (macOS redacts the rest) is the binary's name, compared as a fixed
# string.  ReportCrash writes a report after the process died, so when the
# run ended by a signal run.sh looks for up to 5 s; after a normal exit it
# does not wait.  An exit status over 128 counts as a signal, so a normal
# `exit 200` waits too (accepted).  The report is named, never deleted.
reports=${NO_WINDOW_REPORTS_DIR:-}
[[ -z $reports && $dev_os == darwin ]] && reports=$HOME/Library/Logs/DiagnosticReports
[[ -n $reports ]] || exec "$dev_dir/no-window.sh" "$bin" "${@:2}"
tmp=$(mktemp -d "${TMPDIR:-/tmp}/run.XXXXXX") || exit 1
trap 'rm -rf "$tmp"' EXIT
touch "$tmp/start" || exit 1
name=$(basename "$bin")
# no-window.sh and the binary run in a process group of their own (job
# control).  TERM and HUP to run.sh are passed to that group, INT as TERM:
# the binary, a background command of no-window.sh, ignores INT.  The traps
# are set before the start, so a signal in between is passed on too, once:
# by the trap once the group exists, else right after the start.  The
# report check runs after them as well.
child='' pending='' sent=''
for sig in TERM INT HUP; do
	to=$sig
	[[ $sig == INT ]] && to=TERM
	# shellcheck disable=SC2064  # the signals are fixed now
	trap "pending=$to; [[ -z \$child ]] || { sent=1; kill -$to -- -\$child 2>/dev/null; }" "$sig"
done
# A shell that records its pid, then execs the binary under that pid.
set -m
# shellcheck disable=SC2016  # expanded by that shell
"$dev_dir/no-window.sh" /bin/sh -c 'echo "$$" > "$0" && exec "$@"' "$tmp/pid" "$bin" "${@:2}" &
child=$!
set +m
[[ -z $pending || -n $sent ]] || { sent=1; kill -"$pending" -- -"$child" 2>/dev/null; }
rc=
while [[ -z $rc ]]; do
	wait "$child"
	rc=$?
	# A trapped signal ends wait early with a status over 128; the
	# group is still ending.
	kill -0 "$child" 2>/dev/null && rc=
done
# no-window.sh ends at once on TERM, the binary maybe later: SDL turns TERM
# into a quit event.  After a signal, wait for the whole group, so the
# binary is neither left running nor its shutdown left unchecked; kill
# what still runs after the grace period.
if [[ -n $pending ]]; then
	kill_after=${RUN_KILL_AFTER:-10}
	for _ in $(seq $((kill_after * 10))); do
		kill -0 -- -"$child" 2>/dev/null || break
		sleep 0.1
	done
	if kill -0 -- -"$child" 2>/dev/null; then
		echo "run.sh: $name still runs $kill_after s after $pending; killing it" >&2
		kill -KILL -- -"$child" 2>/dev/null
		for _ in $(seq 50); do kill -0 -- -"$child" 2>/dev/null || break; sleep 0.1; done
	fi
fi
binary_pid=$(cat "$tmp/pid" 2>/dev/null)

# Succeed if crash report $1 is the binary's.
binary_report()
{
	local names
	grep -oE '"pid" *: *[0-9]+' "$1" | grep -oE '[0-9]+$' | grep -qxF "$binary_pid" || return 1
	names=$(grep -oE '"(procName|app_name|procPath)" *: *"[^"]*"' "$1" |
		sed -E 's/^.*: *"(.*)"$/\1/; s/^.*\///')
	grep -qxF -- "$name" <<<"$names"
}
# Print the binary's crash reports written since the start.
binary_reports()
{
	local f
	[[ -n $binary_pid ]] || return 0
	while IFS= read -r -d '' f; do
		binary_report "$f" && printf '%s\n' "$f"
	done < <(find "$reports" -maxdepth 1 -name '*.ips' -newer "$tmp/start" -print0 2>/dev/null)
}
found=$(binary_reports)
if [[ -z $found && $rc -gt 128 ]]; then
	for _ in $(seq 25); do
		[[ -z $pending ]] || break
		sleep 0.2
		found=$(binary_reports)
		[[ -z $found ]] || break
	done
fi
while IFS= read -r f; do
	[[ -n $f ]] || continue
	echo "run.sh: $name left a crash report: $f" >&2
	rc=1
done <<<"$found"
exit "$rc"
