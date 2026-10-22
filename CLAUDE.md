## Conventions

Read `docs/agents/conventions.md` before writing or reviewing C++, SConstruct, tests or commit messages.
Fork C++ follows the C++ safety rules of ADR 5 (`docs/adr/0005-cpp-safety-rules-for-new-code.md`).

## Hard rules

Every agent follows the hard rules in `docs/agents/issue-tracker.md` (windowless runs, crash
reports, licensed data, git). Running the game and where the licensed data is: `docs/agents/game-data.md`.

## Dev workflow

Scripts in `tools/dev/`, each documented in its header comment (read it before running one):
`setup.sh` once per clone or worktree; `check.sh` all checks (`--fast` a quick subset, the hooks
run its other modes); `build.sh` the D2X profiles; `run.sh` launches binaries; `ci.sh` the CI,
local only (ADR 4). Toolchain (ADR 1, ADR 4): `toolchain.pins` pins every tool and dependency,
`doctor.sh` checks it, `deps.sh` builds it, `container.sh` is the Linux reference container.
Also: `rebase.sh`, `fetch-docs.sh`, `leaks.sh`, `gpu-validate.sh`, `no-window.sh`; `watch.sh`
reports crash reports and dev windows during unattended runs; `r1.sh` the R1 frame-hash check
(`docs/agents/game-data.md`).
Shaders: HLSL in `shaders/hlsl/`, generated headers committed in `shaders/generated/`;
`tools/shaders/build.sh` rebuilds them in the reference container (DXC is Linux-only),
`tools/shaders/check.sh` checks the rebuild is byte-identical (R9, `check.sh`'s `shaders` step).

## Agent skills

### Issue tracker

Issues live in GitHub Issues on the fork neostar/dxx-rebirth, never upstream. See `docs/agents/issue-tracker.md`.

### Triage labels

See `docs/agents/triage-labels.md`.

### Domain docs

Single-context: `GLOSSARY.md` and `docs/adr/` at the repo root. See `docs/agents/domain.md`.
