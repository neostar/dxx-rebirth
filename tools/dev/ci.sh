#!/usr/bin/env bash
# The fork's CI (ADR 4): local only, one command.
#
#   ci.sh [--fast] [--clean] [--container]
#
# Two legs, run at once:
# 1. native: tools/dev/check.sh on this machine (skipped with --container);
# 2. container: tools/dev/check.sh in the Linux reference container,
#    offline (container.sh check): by default on a copy of the working
#    tree; with --clean on a fresh clone of the commit under test, with no
#    build caches.
# Each leg's output is printed whole when both are done, native first.
# --fast passes --fast to check.sh in both legs.  Both legs always run;
# exit status 1 names the failed ones, 2 is a usage error.  Build the image
# first with `container.sh build`.
set -u
dev_dir=$(cd "$(dirname "$0")" && pwd)

usage()
{
	echo "usage: $0 [--fast] [--clean] [--container]" >&2
	exit 2
}

fast=() clean=() native=1
while [[ $# -gt 0 ]]; do
	case $1 in
	--fast) fast=(--fast) ;;
	--clean) clean=(--clean) ;;
	--container) native=0 ;;
	*) usage ;;
	esac
	shift
done

logs=$(mktemp -d "${TMPDIR:-/tmp}/ci.XXXXXX")
trap 'rm -rf "$logs"' EXIT
legs=()
declare -A pid
if [[ $native -eq 1 ]]; then
	legs+=(native)
	{
		echo "== ci: native check.sh ${fast[*]-}"
		"$dev_dir/check.sh" "${fast[@]+"${fast[@]}"}"
	} > "$logs/native" 2>&1 &
	pid[native]=$!
fi
legs+=(container)
{
	echo "== ci: container check.sh ${clean[*]-} ${fast[*]-}"
	"$dev_dir/container.sh" check "${clean[@]+"${clean[@]}"}" "${fast[@]+"${fast[@]}"}"
} > "$logs/container" 2>&1 &
pid[container]=$!

failed=()
for leg in "${legs[@]}"; do
	wait "${pid[$leg]}" || failed+=("$leg")
	cat "$logs/$leg"
done

if [[ ${#failed[@]} -ne 0 ]]; then
	echo "ci.sh: failed: ${failed[*]}" >&2
	exit 1
fi
echo "ci.sh: passed"
