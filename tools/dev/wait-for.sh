#!/usr/bin/env bash
# Wait for a background run's file (its log, its output) to show a line.
#
#   wait-for.sh FILE DEADLINE STRING [STRING...]
#
# Polls FILE every 0.5 s until a line of it contains any STRING, a fixed
# string (grep -F: no regex), prints the first such line and exits 0.
# Give every ending a run can have, so a failed run ends the wait too:
# for ci.sh `ci.sh: passed` and `ci.sh: failed`, for nightly.sh
# `nightly.sh: passed` and `nightly.sh: failed`.  After about DEADLINE
# seconds (a positive whole number; checked once a second or so) it exits
# 1, naming the STRINGs and FILE: a FILE that does not exist yet is waited
# for until then.  A FILE that exists but cannot be read fails at once
# with 1.  Missing arguments, an empty STRING, one holding a newline, or
# another DEADLINE are usage errors (2).  It reads only FILE, so its own
# command line, which holds the STRINGs, never matches.
set -u
usage()
{
	echo "usage: $0 FILE DEADLINE STRING [STRING...]" >&2
	exit 2
}
[[ $# -ge 3 && -n $1 && $2 =~ ^[1-9][0-9]*$ ]] || usage
file=$1 seconds=$2
shift 2
patterns=() names=''
for s in "$@"; do
	[[ -n $s && $s != *$'\n'* ]] || usage
	patterns+=(-e "$s")
	names+=${names:+ or }"'$s'"
done
deadline=$((SECONDS + seconds))
while :; do
	if [[ -e $file ]]; then
		line=$(grep -F -m 1 "${patterns[@]}" -- "$file") && rc=0 || rc=$?
		[[ $rc -eq 0 ]] && { printf '%s\n' "$line"; exit 0; }
		[[ $rc -eq 1 ]] || { echo "wait-for.sh: cannot read $file" >&2; exit 1; }
	fi
	[[ $SECONDS -lt $deadline ]] || break
	sleep 0.5
done
if [[ -e $file ]]; then
	echo "wait-for.sh: no line containing $names in $file after $seconds s" >&2
else
	echo "wait-for.sh: $file does not exist after $seconds s" >&2
fi
exit 1
