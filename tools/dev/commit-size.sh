#!/usr/bin/env bash
# Reject a staged change over 300 changed lines (conventions.md, Commits),
# not counting the files tools/dev/commit-size-exclude lists.  Run by the
# pre-commit hook.  DXX_ALLOW_LARGE_COMMIT=<reason> lets a larger change
# through and prints the reason, which belongs in the commit message too.
#
# A binary file counts as $binary_lines lines: numstat gives it no count.
# For `git commit --amend` the commit being made is measured against
# HEAD's parent, not HEAD, or amending in steps would pass any size; git
# tells a hook nothing about an amend, so the hook's ancestor processes
# are read for the git command line (ps; an alias that hides --amend is
# not seen).  The hook cannot tell DXX_ALLOW_LARGE_COMMIT given on the
# command line from one left exported in the shell, as both arrive in
# its environment: the printed reason is the reminder.
set -u
dev_dir=$(cd "$(dirname "$0")" && pwd)
max=300
binary_lines=100

# Succeed if an ancestor of this process is `git commit ... --amend`.
amending()
{
	local pid=$$ ppid args
	for _ in 1 2 3 4 5 6; do
		read -r ppid args < <(ps -o ppid= -o args= -p "$pid" 2>/dev/null) || return 1
		if [[ " $args " =~ (^|[[:space:]/])git[[:space:]].*[[:space:]]commit[[:space:]] &&
			" $args " == *" --amend "* ]]; then
			return 0
		fi
		[[ $ppid -gt 1 ]] || return 1
		pid=$ppid
	done
	return 1
}

base=HEAD
if amending; then
	base=$(git rev-parse -q --verify HEAD^ 2>/dev/null) ||
		base=$(git hash-object -t tree /dev/null)
fi
lines=$(git diff --cached --numstat --no-renames "$base" |
	awk -v ex="$dev_dir/commit-size-exclude" -v bin="$binary_lines" '
	BEGIN { while ((getline l < ex) > 0) if (l !~ /^#/ && l != "") skip[l] = 1 }
	$3 in skip { next }
	$1 == "-" { n += bin; next }
	{ n += $1 + $2 }
	END { print n + 0 }')
[[ $lines -le $max ]] && exit 0
if [[ -n ${DXX_ALLOW_LARGE_COMMIT-} ]]; then
	echo "commit-size: $lines changed lines allowed: $DXX_ALLOW_LARGE_COMMIT"
	exit 0
fi
echo "commit-size: $lines changed lines, over $max; split the change, or set" \
	"DXX_ALLOW_LARGE_COMMIT=<reason> and give the reason in the message" >&2
exit 1
