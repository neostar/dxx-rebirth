#!/usr/bin/env bash
# Wait for a background run's file (its log, its output) to show a line.
#
#   wait-for.sh FILE STRING DEADLINE
#
# Polls FILE every 0.5 s until a line of it contains STRING, a fixed string
# (grep -F: no regex), and exits 0.  At DEADLINE seconds (a positive whole
# number) it exits 1, naming what it waited for: a FILE that does not
# exist yet is waited for until then.  A FILE that exists but cannot be
# read fails at once with 1.  Missing or extra arguments, an empty STRING
# or another DEADLINE are usage errors (2).  It reads only FILE, so its own
# command line, which holds STRING, never matches.
set -u
usage()
{
	echo "usage: $0 FILE STRING DEADLINE_SECONDS" >&2
	exit 2
}
[[ $# -eq 3 && -n $1 && -n $2 && $3 =~ ^[1-9][0-9]*$ ]] || usage
file=$1 string=$2 deadline=$((SECONDS + $3))
while :; do
	if [[ -e $file ]]; then
		grep -qF -- "$string" "$file" && rc=0 || rc=$?
		[[ $rc -eq 0 ]] && exit 0
		[[ $rc -eq 1 ]] || { echo "wait-for.sh: cannot read $file" >&2; exit 1; }
	fi
	[[ $SECONDS -lt $deadline ]] || break
	sleep 0.5
done
if [[ -e $file ]]; then
	echo "wait-for.sh: no line containing '$string' in $file after $3 s" >&2
else
	echo "wait-for.sh: $file does not exist after $3 s" >&2
fi
exit 1
