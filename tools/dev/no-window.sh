#!/usr/bin/env bash
# Run a command and fail if it took focus or crashed into a dialog (macOS).
#
#   no-window.sh CMD [ARG...]
#
# Every 0.5 s the run's process tree (CMD and its descendants) and the
# frontmost app's pid are sampled.  Exit status: CMD's own status, or 1 if
# a process of the run became the frontmost app or a new crash report (.ips,
# shown with a CrashReporter dialog) names one of them as its pid or
# parentPid.  Other apps taking focus, such as the owner switching apps,
# and other crash reports pass.  If the frontmost app cannot be read
# (osascript needs the Automation permission for System Events), it exits 1
# without running CMD.  On other systems CMD just runs.
# SDL_MAC_BACKGROUND_APP=1 is exported for CMD, and the run is silent: SDL's
# audio driver is dummy (SDL_AUDIODRIVER for SDL2, SDL_AUDIO_DRIVER for
# SDL3), also over one the caller set, unless NO_WINDOW_SOUND=1.  The one
# other driver kept is disk, which writes the mix to a file and plays
# nothing (R12 captures): asked for in either variable, both name it, and
# SDL_AUDIO_DISK_OUTPUT_FILE (SDL_DISKAUDIOFILE for SDL2 is set to it) must
# be absolute, else the run fails before CMD starts: SDL's default is
# relative and the game changes its working directory.
#
# NO_WINDOW_FRONTMOST_CMD replaces the sampler (it prints "PID NAME") and
# NO_WINDOW_REPORTS_DIR the crash report directory (used by the tests).
set -u
[[ $# -gt 0 ]] || { echo "usage: $0 CMD [ARG...]" >&2; exit 2; }
export SDL_MAC_BACKGROUND_APP=1
if [[ ${NO_WINDOW_SOUND-} != 1 ]]; then
	if [[ ${SDL_AUDIO_DRIVER-} == disk || ${SDL_AUDIODRIVER-} == disk ]]; then
		if [[ ${SDL_AUDIO_DISK_OUTPUT_FILE-} != /* ]]; then
			echo "no-window.sh: the disk audio driver needs an absolute SDL_AUDIO_DISK_OUTPUT_FILE, not '${SDL_AUDIO_DISK_OUTPUT_FILE-}'" >&2
			exit 1
		fi
		export SDL_AUDIODRIVER=disk SDL_AUDIO_DRIVER=disk SDL_DISKAUDIOFILE=$SDL_AUDIO_DISK_OUTPUT_FILE
	else
		export SDL_AUDIODRIVER=dummy SDL_AUDIO_DRIVER=dummy
	fi
fi
if [[ $(uname -s) != Darwin && -z ${NO_WINDOW_FRONTMOST_CMD-} ]]; then
	exec "$@"
fi

reports=${NO_WINDOW_REPORTS_DIR:-$HOME/Library/Logs/DiagnosticReports}
# Print the frontmost app as "PID NAME".
frontmost()
{
	if [[ -n ${NO_WINDOW_FRONTMOST_CMD-} ]]; then
		"$NO_WINDOW_FRONTMOST_CMD"
	else
		osascript -e 'tell application "System Events"' \
			-e 'set p to first process whose frontmost is true' \
			-e 'return ((unix id of p) as text) & " " & (name of p)' -e 'end tell'
	fi
}

# Print the pids of process $1 and its descendants.
tree_pids()
{
	ps -A -o pid=,ppid= | awk -v root="$1" '
		{ parent[$1] = $2 }
		END { for (p in parent) for (q = p; q > 1; q = parent[q]) if (q == root) { print p; break } }'
}

marker=$(mktemp "${TMPDIR:-/tmp}/no-window.XXXXXX")
trap 'rm -f "$marker"' EXIT
before=$(frontmost)
if [[ -z $before ]]; then
	echo "no-window.sh: cannot read the frontmost app; allow this terminal to control System Events (Automation)" >&2
	exit 1
fi
seen=()

"$@" &
pid=$!
run_pids=" $pid "
# Add the run's current processes to run_pids, then record the frontmost
# app if it is one of them.
sample()
{
	local p app_pid app_name
	for p in $(tree_pids "$pid"); do
		[[ $run_pids == *" $p "* ]] || run_pids+="$p "
	done
	read -r app_pid app_name <<<"$(frontmost)"
	[[ $run_pids == *" $app_pid "* ]] && seen+=("$app_name (pid $app_pid)")
	return 0
}
while kill -0 "$pid" 2>/dev/null; do
	sample
	sleep 0.5
done
wait "$pid"
rc=$?
sample

run_reports=()
while IFS= read -r f; do
	ips_pids=$(grep -oE '"(pid|parentPid)" *: *[0-9]+' "$f" | grep -oE '[0-9]+$')
	for p in $ips_pids; do
		if [[ $run_pids == *" $p "* ]]; then
			run_reports+=("$f")
			break
		fi
	done
done < <(find "$reports" -maxdepth 1 -name '*.ips' -newer "$marker" 2>/dev/null)
if [[ ${#seen[@]} -ne 0 ]]; then
	echo "no-window.sh: the run became the frontmost app: ${seen[*]}" >&2
	rc=1
fi
if [[ ${#run_reports[@]} -ne 0 ]]; then
	echo "no-window.sh: new crash report of the run: ${run_reports[*]}" >&2
	rc=1
fi
exit "$rc"
