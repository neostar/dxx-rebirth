## Conventions

Read `docs/agents/conventions.md` before writing or reviewing C++, SConstruct, tests or commit messages.
Fork C++ follows the C++ safety rules of ADR 5 (`docs/adr/0005-cpp-safety-rules-for-new-code.md`).

## Windowless

Run every built binary, probe or test program through `tools/dev/run.sh` or
`tools/dev/no-window.sh`, with `ASAN_OPTIONS=abort_on_error=0`: nothing may open a window, take
focus or leave a crash report. Never delete files in `~/Library/Logs/DiagnosticReports`.
Running the game or the D2 demo, and where the licensed data is: `docs/agents/game-data.md`.

## Dev workflow

Scripts in `tools/dev/`, each documented in its header comment (read it before running one):
`setup.sh` once per clone or worktree; `check.sh` all checks (`--fast` a quick subset, the hooks
run its other modes); `build.sh` the D2X profiles; `run.sh` launches binaries; `ci.sh` the CI,
local only (ADR 4). Toolchain (ADR 1, ADR 4): `toolchain.pins` pins every tool and dependency,
`doctor.sh` checks it, `deps.sh` builds it, `container.sh` is the Linux reference container.
Also: `rebase.sh`, `fetch-docs.sh`, `leaks.sh`, `gpu-validate.sh`, `no-window.sh`; `r1.sh` the
R1 frame-hash check (`docs/agents/game-data.md`).
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
