#!/usr/bin/env bash
# One-time setup of a clone or worktree for fork development.
set -eu
cd "$(git rev-parse --show-toplevel)"
# Pre-commit hook: runs tools/dev/check.sh --fast.
git config core.hooksPath tools/dev/hooks
echo "core.hooksPath = tools/dev/hooks"
# Replay recorded conflict resolutions on every rebase on upstream (ADR 1).
git config rerere.enabled true
git config rerere.autoupdate true
echo "rerere.enabled = true"
