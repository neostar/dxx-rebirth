#!/usr/bin/env bash
# Watch an unattended run for what it must not leave behind (macOS): print
# one line per new crash report and per on-screen window of a dev binary
# or a Problem Reporter dialog.  Runs until killed; the orchestrator runs it
# beside the agents and reads its output.
#
#   watch.sh [--rounds N]
#
# Every WATCH_INTERVAL seconds (default 2):
# - each crash report (.ips) written since the start prints
#   "<UTC time> crash report: <process name> <file>", once.  The directory
#   is ~/Library/Logs/DiagnosticReports, or NO_WINDOW_REPORTS_DIR; it is
#   only read.  One that cannot be read ends the watch with status 1.
# - each on-screen window (CGWindowList) whose owner's name matches
#   WATCH_NAMES, an extended regex (default: the games, the unit tests,
#   probes, Problem Reporter), prints "<UTC time> window: <owner> (pid N)",
#   once per owner and pid.
# --rounds N stops after N rounds, with status 0 (the tests use it).
# WATCH_WINDOWS_CMD replaces the window lister; it prints "PID NAME" per
# window.  A lister that fails or prints nothing ends the watch with status
# 1: an on-screen list always holds some window, so an empty one means the
# watch cannot see.
set -u
usage()
{
	echo "usage: $0 [--rounds N]" >&2
	exit 2
}
rounds=0
case "${1-}" in
--rounds) [[ ${2-} =~ ^[1-9][0-9]*$ ]] || usage; rounds=$2 ;;
'') ;;
*) usage ;;
esac
interval=${WATCH_INTERVAL:-2}
names=${WATCH_NAMES:-'^(d[12]x-rebirth|test-.+|.*probe.*|Problem Reporter)$'}
reports=${NO_WINDOW_REPORTS_DIR:-$HOME/Library/Logs/DiagnosticReports}

# Print "PID NAME" for each on-screen window.
windows()
{
	if [[ -n ${WATCH_WINDOWS_CMD-} ]]; then
		"$WATCH_WINDOWS_CMD"
		return
	fi
	osascript -l JavaScript -e 'ObjC.import("CoreGraphics");
		ObjC.deepUnwrap(ObjC.castRefToObject($.CGWindowListCopyWindowInfo(
			$.kCGWindowListOptionOnScreenOnly, 0)))
			.map(w => w.kCGWindowOwnerPID + " " + w.kCGWindowOwnerName).join("\n")'
}

marker=$(mktemp "${TMPDIR:-/tmp}/watch.XXXXXX") || exit 1
found=$(mktemp "${TMPDIR:-/tmp}/watch.XXXXXX") || { rm -f "$marker"; exit 1; }
trap 'rm -f "$marker" "$found"' EXIT
seen=$'\n'
round=0
while :; do
	if ! find "$reports" -maxdepth 1 -name '*.ips' -newer "$marker" -print0 >"$found" 2>/dev/null; then
		echo "watch.sh: cannot read the crash report directory $reports" >&2
		exit 1
	fi
	while IFS= read -r -d '' f; do
		[[ $seen == *$'\n'"report $f"$'\n'* ]] && continue
		seen+="report $f"$'\n'
		name=$(grep -oE '"(procName|app_name)" *: *"[^"]*"' "$f" | head -n 1 |
			sed -E 's/^.*: *"(.*)"$/\1/')
		echo "$(date -u +%FT%TZ) crash report: ${name:-unknown} $f"
	done <"$found"
	if ! list=$(windows) || [[ -z $list ]]; then
		echo "watch.sh: cannot list the on-screen windows" >&2
		exit 1
	fi
	while read -r pid name; do
		[[ $name =~ $names ]] || continue
		[[ $seen == *$'\n'"window $pid $name"$'\n'* ]] && continue
		seen+="window $pid $name"$'\n'
		echo "$(date -u +%FT%TZ) window: $name (pid $pid)"
	done <<<"$list"
	round=$((round + 1))
	[[ $rounds -eq 0 || $round -lt $rounds ]] || exit 0
	sleep "$interval"
done
