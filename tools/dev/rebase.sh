#!/usr/bin/env bash
# Rebase the current fork branch on upstream (ADR 1).
#
#   rebase.sh [--upstream REF] [--exec CMD]
#
# REF defaults to upstream/master (fetch it first).  CMD runs after every
# rebased commit (git rebase --exec) and defaults to a dev build plus the
# fast checks; commits that predate tools/dev skip them with a message.
# rerere stays as setup.sh sets it: off.
# On success the range-diff between the old and new series is printed;
# on a conflict or a failing CMD the rebase stops for you to fix it.
set -u
upstream=upstream/master
# One line: git rebase --exec rejects newlines.  Expanded by git's shell.
# shellcheck disable=SC2016
exec_cmd='if [ -x tools/dev/build.sh ] && [ -x tools/dev/check.sh ]; then tools/dev/build.sh && tools/dev/check.sh --fast; else echo "rebase.sh: skip checks at $(git log -1 --format="%h %s"): no tools/dev/build.sh or check.sh"; fi'

usage()
{
	echo "usage: $0 [--upstream REF] [--exec CMD]" >&2
	exit 2
}

while [[ $# -gt 0 ]]; do
	case $1 in
		--upstream) [[ $# -ge 2 ]] || usage; upstream=$2; shift 2 ;;
		--exec) [[ $# -ge 2 ]] || usage; exec_cmd=$2; shift 2 ;;
		*) usage ;;
	esac
done

old=$(git rev-parse HEAD)
if ! git rebase --exec "$exec_cmd" "$upstream"; then
	echo "rebase.sh: rebase stopped; fix, then 'git rebase --continue' and run" >&2
	echo "  git range-diff $old...HEAD" >&2
	exit 1
fi
git range-diff "$old...HEAD"
