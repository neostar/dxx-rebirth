## Conventions

Read `docs/agents/conventions.md` before writing or reviewing C++, SConstruct, tests or commit messages.
Fork C++ follows the C++ safety rules of ADR 5 (`docs/adr/0005-cpp-safety-rules-for-new-code.md`).

## Dev workflow

Scripts in `tools/dev/`, each documented in its header: `setup.sh` once per clone or worktree
(pre-commit hook, rerere); `check.sh` (all checks; `--pre-commit` is the hook, `--fast` a
quick subset); `build.sh [dev|asan]`
(D2X profiles, `check` runs the unit tests); `rebase.sh` (rebase on upstream with per-commit
checks); `fetch-docs.sh` (SDL3/SDL_GPU reference docs into `docs/reference/`). Launch binaries
with `run.sh`; `leaks.sh` runs one under macOS's `leaks --atExit`,
`gpu-validate.sh` an SDL_GPU one with GPU validation as a gate. Toolchain (ADR 1, ADR 4): `toolchain.pins` pins every tool and dependency;
`doctor.sh` fails on drift (a `check.sh --fast` step); `deps.sh` builds the pinned dependencies
from a sha256-verified cache into `third_party/<os>-<arch>/`, `deps.sh venv` makes the hashed
Python venv; `container.sh build|check|prune|save|load` is the Linux reference container.
`ci.sh` is the CI, local only: `check.sh` natively and in the container offline, at once
(`--clean`: a fresh clone of HEAD).

## Agent skills

### Issue tracker

Issues live in GitHub Issues on the fork neostar/dxx-rebirth, never upstream. See `docs/agents/issue-tracker.md`.

### Triage labels

The five default labels: needs-triage, needs-info, ready-for-agent, ready-for-human, wontfix. See `docs/agents/triage-labels.md`.

### Domain docs

Single-context: `GLOSSARY.md` and `docs/adr/` at the repo root. See `docs/agents/domain.md`.
