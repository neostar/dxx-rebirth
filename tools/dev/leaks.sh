#!/usr/bin/env bash
# Run a program under macOS's leaks tool and fail if it leaked (ADR 5).
#
#   leaks.sh CMD [ARG...]
#
# LeakSanitizer is unsupported on Apple Silicon macOS, so on the Mac
# `leaks --atExit` checks the program's heap when it exits.  Prints leaks'
# summary line; on a leak, or when leaks cannot inspect CMD, prints its
# whole report and exits 1.  leaks drops CMD's own exit status, so this
# checks leaks only: run CMD plainly for its status (check.sh's unit_tests
# step does).  Exit status 2: usage, or not macOS.
set -u
[[ $# -gt 0 ]] || { echo "usage: $0 CMD [ARG...]" >&2; exit 2; }
command -v leaks >/dev/null || { echo "leaks.sh: leaks not found; it is macOS only" >&2; exit 2; }
out=$(mktemp "${TMPDIR:-/tmp}/leaks.XXXXXX")
trap 'rm -f "$out"' EXIT
leaks --atExit -- "$@" > "$out" 2>&1
rc=$?
summary=$(grep -E '^Process [0-9]+: [0-9]+ leaks? for ' "$out")
if [[ $rc -eq 0 && $summary == *": 0 leaks for "* ]]; then
	echo "leaks.sh: ${1##*/}: ${summary#*: }"
	exit 0
fi
cat "$out"
echo "leaks.sh: ${1##*/}: ${summary:+${summary#*: }}${summary:-leaks could not check it (status $rc)}" >&2
exit 1
