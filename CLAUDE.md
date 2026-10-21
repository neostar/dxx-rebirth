## Conventions

Read `docs/agents/conventions.md` before writing or reviewing C++, SConstruct, tests or commit messages.

## Dev workflow

Scripts in `tools/dev/`, each documented in its header comment (read it before running one):
`setup.sh` once per clone or worktree; `check.sh` all checks (`--fast` a quick subset, the hooks
run its other modes); `build.sh` the D2X profiles; `run.sh` launches binaries; `ci.sh` the CI,
local only (ADR 4). Toolchain (ADR 1, ADR 4): `toolchain.pins` pins every tool and dependency,
`doctor.sh` checks it, `deps.sh` builds it, `container.sh` is the Linux reference container.
Also: `rebase.sh`, `fetch-docs.sh`, `leaks.sh`, `gpu-validate.sh`, `no-window.sh`.

## Agent skills

### Issue tracker

Issues live in GitHub Issues on the fork neostar/dxx-rebirth, never upstream. See `docs/agents/issue-tracker.md`.

### Triage labels

See `docs/agents/triage-labels.md`.

### Domain docs

Single-context: `GLOSSARY.md` and `docs/adr/` at the repo root. See `docs/agents/domain.md`.
