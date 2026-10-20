#!/usr/bin/env bash
# Run a command and fail if it took focus or crashed into a dialog (macOS).
#
#   no-window.sh CMD [ARG...]
#
# The frontmost app is sampled before, every 0.5 s during, and after CMD.
# Exit status: CMD's own status, or 1 if the frontmost app changed or a new
# crash report (.ips, shown with a CrashReporter dialog) appeared.  On other
# systems CMD just runs.  SDL_MAC_BACKGROUND_APP=1 is exported for CMD.
#
# NO_WINDOW_FRONTMOST_CMD and NO_WINDOW_REPORTS_DIR replace the sampler and
# the crash report directory (used by the tests).
set -u
[[ $# -gt 0 ]] || { echo "usage: $0 CMD [ARG...]" >&2; exit 2; }
export SDL_MAC_BACKGROUND_APP=1
if [[ $(uname -s) != Darwin && -z ${NO_WINDOW_FRONTMOST_CMD-} ]]; then
	exec "$@"
fi

reports=${NO_WINDOW_REPORTS_DIR:-$HOME/Library/Logs/DiagnosticReports}
frontmost()
{
	if [[ -n ${NO_WINDOW_FRONTMOST_CMD-} ]]; then
		"$NO_WINDOW_FRONTMOST_CMD"
	else
		osascript -e 'tell application "System Events" to get name of first process whose frontmost is true'
	fi
}

marker=$(mktemp "${TMPDIR:-/tmp}/no-window.XXXXXX")
trap 'rm -f "$marker"' EXIT
before=$(frontmost)
seen=()

"$@" &
pid=$!
while kill -0 "$pid" 2>/dev/null; do
	app=$(frontmost)
	[[ $app != "$before" ]] && seen+=("$app")
	sleep 0.5
done
wait "$pid"
rc=$?
app=$(frontmost)
[[ $app != "$before" ]] && seen+=("$app")

new_reports=$(find "$reports" -maxdepth 1 -name '*.ips' -newer "$marker" 2>/dev/null)
if [[ ${#seen[@]} -ne 0 ]]; then
	echo "no-window.sh: frontmost app changed from $before to: ${seen[*]}" >&2
	rc=1
fi
if [[ -n $new_reports ]]; then
	echo "no-window.sh: new crash report: $new_reports" >&2
	rc=1
fi
exit "$rc"
