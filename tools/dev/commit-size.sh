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
#
# git runs no pre-commit hook for the commits a rebase makes, so a squash
# or fixup in `rebase -i` that joins small commits into a large one passes
# it.  check.sh's commit_sizes step is the backstop: `commit-size.sh
# --history BASE` measures every commit in BASE..HEAD against its first
# parent and fails on one over the limit, unless its message has a
# `Large-commit: <reason>` trailer or tools/dev/commit-size-allow lists its
# subject with a reason.
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

# Sum git's --numstat lines on the standard input, leaving out the files
# commit-size-exclude lists.  Each line "@HASH [PARENTS]" (git log) starts
# a commit: print "HASH LINES" for each, a root commit counting 0, as
# against its missing parent; without them print the one sum.
count()
{
	awk -v ex="$dev_dir/commit-size-exclude" -v bin="$binary_lines" '
		BEGIN { while ((getline l < ex) > 0) if (l !~ /^#/ && l != "") skip[l] = 1 }
		/^@/ { if (c != "") print c, n + 0; c = substr($1, 2); n = 0; root = NF < 2; next }
		NF == 0 || root || $3 in skip { next }
		$1 == "-" { n += bin; next }
		{ n += $1 + $2 }
		END { print (c != "" ? c " " : "") n + 0 }'
}

if [[ ${1-} == --history ]]; then
	git rev-parse -q --verify "${2-}^{commit}" >/dev/null ||
		{ echo "commit-size: --history needs a base commit, got '${2-}'" >&2; exit 2; }
	rc=0
	allowed=$(sed -nE 's/^(.+) -- [^[:space:]].*$/\1/p' "$dev_dir/commit-size-allow" 2>/dev/null)
	while read -r c lines; do
		[[ -n $c && $lines -gt $max ]] || continue
		git log -1 --format=%B "$c" | grep -qE '^Large-commit: [^[:space:]]' && continue
		subject=$(git log -1 --format=%s "$c")
		grep -qxF -- "$subject" <<<"$allowed" && continue
		echo "commit-size: $lines changed lines, over $max: $subject; split it, or give" \
			"a Large-commit: <reason> trailer"
		rc=1
	done < <(git log --no-merges --no-renames --numstat --format='@%H %P' "$2..HEAD" | count)
	exit "$rc"
fi

base=HEAD
if amending; then
	base=$(git rev-parse -q --verify HEAD^ 2>/dev/null) ||
		base=$(git hash-object -t tree /dev/null)
fi
lines=$(git diff --numstat --no-renames --cached "$base" | count)
[[ $lines -le $max ]] && exit 0
if [[ -n ${DXX_ALLOW_LARGE_COMMIT-} ]]; then
	echo "commit-size: $lines changed lines allowed: $DXX_ALLOW_LARGE_COMMIT"
	exit 0
fi
echo "commit-size: $lines changed lines, over $max; split the change, or set" \
	"DXX_ALLOW_LARGE_COMMIT=<reason> and give the reason in the message" >&2
exit 1
