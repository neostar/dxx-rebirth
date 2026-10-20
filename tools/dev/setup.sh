#!/usr/bin/env bash
# One-time setup of a clone or worktree for fork development: writes the
# key=value rules of tools/dev/repo-config (pre-commit hook, rerere) into the
# clone's local config, which its worktrees share.  check.sh --fast checks
# them, and that the !section rules hold.
set -eu
dev_dir=$(cd "$(dirname "$0")" && pwd)
cd "$(git rev-parse --show-toplevel)"
while IFS= read -r rule; do
	[[ $rule == *=* && $rule != \#* ]] || continue
	git config "${rule%%=*}" "${rule#*=}"
	echo "${rule%%=*} = ${rule#*=}"
done < "$dev_dir/repo-config"
