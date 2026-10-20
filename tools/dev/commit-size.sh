#!/usr/bin/env bash
# Reject a staged change over 300 changed lines (conventions.md, Commits),
# not counting the files tools/dev/commit-size-exclude lists.  Run by the
# pre-commit hook.  DXX_ALLOW_LARGE_COMMIT=<reason> lets a larger change
# through and prints the reason, which belongs in the commit message too.
set -u
dev_dir=$(cd "$(dirname "$0")" && pwd)
max=300

lines=$(git diff --cached --numstat --no-renames |
	awk -v ex="$dev_dir/commit-size-exclude" '
	BEGIN { while ((getline l < ex) > 0) if (l !~ /^#/ && l != "") skip[l] = 1 }
	!($3 in skip) && $1 != "-" { n += $1 + $2 }
	END { print n + 0 }')
[[ $lines -le $max ]] && exit 0
if [[ -n ${DXX_ALLOW_LARGE_COMMIT-} ]]; then
	echo "commit-size: $lines changed lines allowed: $DXX_ALLOW_LARGE_COMMIT"
	exit 0
fi
echo "commit-size: $lines changed lines, over $max; split the change, or set" \
	"DXX_ALLOW_LARGE_COMMIT=<reason> and give the reason in the message" >&2
exit 1
