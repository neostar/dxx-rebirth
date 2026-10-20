## Conventions

Read `docs/agents/conventions.md` before writing or reviewing C++, SConstruct, tests or commit messages.

## Dev workflow

Scripts in `tools/dev/`, each documented in its header: `setup.sh` once per clone or worktree
(pre-commit hook, rerere); `check.sh` (all checks; `--fast` is the hook); `build.sh [dev|asan]`
(D2X profiles, `check` runs the unit tests); `rebase.sh` (rebase on upstream with per-commit
checks); `fetch-docs.sh` (SDL3/SDL_GPU reference docs into `docs/reference/`). Run anything that
may open a window under `tools/dev/no-window.sh`.

## Agent skills

### Issue tracker

Issues live in GitHub Issues on the fork neostar/dxx-rebirth, never upstream. See `docs/agents/issue-tracker.md`.

### Triage labels

The five default labels: needs-triage, needs-info, ready-for-agent, ready-for-human, wontfix. See `docs/agents/triage-labels.md`.

### Domain docs

Single-context: `GLOSSARY.md` and `docs/adr/` at the repo root. See `docs/agents/domain.md`.
