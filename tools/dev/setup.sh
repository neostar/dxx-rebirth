#!/usr/bin/env bash
# One-time setup of a clone or worktree for fork development.
set -eu
cd "$(git rev-parse --show-toplevel)"
# Pre-commit hook: runs tools/dev/check.sh --fast.
git config core.hooksPath tools/dev/hooks
echo "core.hooksPath = tools/dev/hooks"
