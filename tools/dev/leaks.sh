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
# step does).  Exit status 2: usage, an unreadable or malformed exclusion
# list, or not macOS.
# Leaked blocks allocated under a symbol the exclusion list names for this
# program are excluded (leaks -exclude): system frameworks' leaks that no
# program can free.  The list is LEAKS_EXCLUDE (default
# tools/dev/leaks-exclude), one "<program> <symbol> -- <reason>" a line,
# <program> the basename of CMD; # comments and blank lines are ignored.
# The run passes when leaks excluded every leak it found.  A malformed
# entry, or one naming a symbol CMD itself defines (nm, raw or demangled),
# which would hide the program's own leaks, is a usage error, and so is nm
# failing on CMD.  A program with entries whose checked run excluded no
# leak fails with a STALE line: drop its entries.
set -u
[[ $# -gt 0 ]] || { echo "usage: $0 CMD [ARG...]" >&2; exit 2; }
command -v leaks >/dev/null || { echo "leaks.sh: leaks not found; it is macOS only" >&2; exit 2; }
list=${LEAKS_EXCLUDE:-$(cd "$(dirname "$0")" && pwd)/leaks-exclude}
[[ -r $list ]] || { echo "leaks.sh: cannot read the exclusion list $list" >&2; exit 2; }
prog=$1
[[ $prog == */* ]] || prog=$(command -v "$prog") || { echo "leaks.sh: $1: not found" >&2; exit 2; }
name=${prog##*/}
excludes=()
while IFS= read -r line || [[ -n $line ]]; do
	[[ -z $line || $line == \#* ]] && continue
	entry=${line%% -- *} reason=${line#* -- }
	program=${entry%% *} symbol=${entry#* }
	if [[ $line != *' -- '* || $entry != *' '* || -z $program || -z $symbol || -z $reason ]]; then
		echo "leaks.sh: $list: an entry without a program, a symbol or a reason: $line" >&2
		exit 2
	fi
	[[ $program == "$name" ]] || continue
	# The program's own symbols, raw and demangled: leaks matches frames
	# by their demangled names (ns::f()).
	if [[ -z ${syms+set} ]]; then
		syms=$(nm -jU "$prog" && nm -jUC "$prog") ||
			{ echo "leaks.sh: nm cannot list the symbols of $prog" >&2; exit 2; }
	fi
	if grep -qxF -e "$symbol" -e "_$symbol" <<<"$syms"; then
		echo "leaks.sh: $list: $name defines $symbol, so excluding it would hide its own leaks" >&2
		exit 2
	fi
	excludes+=(-exclude "$symbol")
done < "$list"
out=$(mktemp "${TMPDIR:-/tmp}/leaks.XXXXXX")
trap 'rm -f "$out"' EXIT
leaks --atExit ${excludes[@]+"${excludes[@]}"} -- "$@" > "$out" 2>&1
rc=$?
summary=$(grep -E '^Process [0-9]+: [0-9]+ leaks? for ' "$out")
count=$(sed -nE 's/^Process [0-9]+: ([0-9]+) leaks? for .*/\1/p' <<<"$summary")
excluded=$(sed -nE 's/^([0-9]+) leaks? excluded .*/\1/p' "$out")
if [[ ${#excludes[@]} -gt 0 && -n $summary && $rc -eq 0 && -z $excluded ]]; then
	echo "STALE leaks-exclude: $name: its run excluded no leak; drop its entries from ${list##*/}" >&2
	exit 1
fi
if [[ $rc -eq 0 && $summary == *": 0 leaks for "* ]]; then
	echo "leaks.sh: ${1##*/}: ${summary#*: }"
	exit 0
fi
if [[ $rc -eq 0 && -n $count && $count == "$excluded" ]]; then
	echo "leaks.sh: ${1##*/}: ${summary#*: } All excluded (${list##*/})."
	exit 0
fi
cat "$out"
verdict=${summary:+${summary#*: }}
echo "leaks.sh: ${1##*/}: ${verdict:-leaks could not check it (status $rc)}" >&2
exit 1
