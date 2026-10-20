#!/usr/bin/env bash
# Rebase the current fork branch on upstream (ADR 1).
#
#   rebase.sh [--upstream REF] [--exec CMD]
#
# REF defaults to upstream/master (fetch it first).  CMD runs after every
# rebased commit (git rebase --exec) and defaults to a dev build plus the
# fast checks.  rerere is enabled so recorded conflict resolutions replay.
# On success the range-diff between the old and new series is printed;
# on a conflict or a failing CMD the rebase stops for you to fix it.
set -u
upstream=upstream/master
exec_cmd='tools/dev/build.sh && tools/dev/check.sh --fast'

while [[ $# -gt 0 ]]; do
	case $1 in
		--upstream) upstream=$2; shift 2 ;;
		--exec) exec_cmd=$2; shift 2 ;;
		*) echo "usage: $0 [--upstream REF] [--exec CMD]" >&2; exit 2 ;;
	esac
done

git config rerere.enabled true
git config rerere.autoupdate true

old=$(git rev-parse HEAD)
if ! git rebase --exec "$exec_cmd" "$upstream"; then
	echo "rebase.sh: rebase stopped; fix, then 'git rebase --continue' and run" >&2
	echo "  git range-diff $old...HEAD" >&2
	exit 1
fi
git range-diff "$old...HEAD"
