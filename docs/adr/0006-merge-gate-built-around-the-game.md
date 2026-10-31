# 6. The merge gate is built around the game

Date: 2026-10-22. Status: accepted (owner). Amended 2026-10-23 (owner):
the `gpu` path class; report-only nightly steps. Amends ADR 4 (which run counts for a merge) and
ADR 5 (the check tier splits into the merge gate and the full run).

## Context

The port changes game code from #6 on, where the costly failure is a wrong frame. Until now
every merge ran the full run (`ci.sh --clean`, native and container legs, every step): 12-14
minutes, two or three times per ticket, one at a time on the machine. More than half of it
tested `tools/dev` (278 shell tests); R1, the check that every frame stays bit-identical, ran
only when an agent ran `r1.sh` by hand.

The licensed data may be used locally in any way; it and everything derived from it stay out
of git (`docs/agents/game-data.md`). So R1 can run in CI on any leg that has the data.

## Decision

**The merge gate** decides a merge. It runs on the native leg, within 5 minutes, and a test
checks the budget (as ADR 5's tiers are checked):

- dev, asan and harness builds of the changed tree
- unit tests, sanitizer tests, leaks
- clang-tidy and clang-format on the changed fork files
- both games built with `sdl3=1` (compile and link), and clang-tidy on every SDL3-only source (`tools/dev/sdl3-only`), changed or not; a skipped tidy fails the step (#95)
- doctor once; gitleaks on the new commits
- **R1** `check` against the base commit's baseline, captured once per base and R1 inputs and reused

Budget basis, measured 2026-10-22 on the M1 Max: R1 `check` 123 s warm, 161 s cold; the other
steps about 3 minutes; R1 runs beside the unit and sanitizer tests.

Remeasured 2026-10-28 (#95, idle M1 Max), budget kept at 300 s: the SDL3 games build (`sdl3_games`,
64-78 s) runs beside the other steps like R1, and the clang-tidy of the changed files runs one
file per core. No fork C++ changed: 175 s (leaks 116 s, `sdl3_games` 70 s, R1 153 s). #28's
seven commits (render device, `changed_format` 54 s, was 136-146 s serial): 235 s, a 65 s margin.
Before the two changes the same chain took 332 s.

**Steps follow the changed paths.** The gate adds steps for what a change touches:

| Changed paths | Adds |
|---|---|
| game code, fork C++ | (the gate above) |
| `tools/dev/**`, `docs/agents/**`, `docs/adr/**`, `docs/contributing/**`, `GLOSSARY.md` | the shell tests of the changed scripts, shellcheck (the doc paths have no shell test: shellcheck alone) |
| image inputs (`deps.sh`, `env.sh`, `toolchain.pins`, `requirements.lock`, the Dockerfile), `SConstruct`, `site_scons/**`, fork build flags | the container leg |
| `shaders/**` | the shader rebuild check (R9) on the container leg |
| the inputs of the registered GPU programs (`gpu` class in `path_classes`, `tools/dev/ci.sh`): the SDL_GPU device's sources and tests (`common/render/sdlgpu*`, `common/render/unittest/sdlgpu*`), `common/sdl/sdl_wrap_gpu.h`, `tools/dev/gpu-programs`, `tools/dev/gpu-validate.sh` | `gpu_tests`: the programs `tools/dev/gpu-programs` lists, under `gpu-validate.sh` on Metal (`check.sh --gpu`) |
| any other path | the full run |

`SConstruct`, `shaders/**`, `deps.sh` and `toolchain.pins` also run the `gpu` leg; `path_classes`
in `tools/dev/ci.sh` holds the current list.

**The full run** runs nightly, before ADR 5's nightly tier, and is the gate before an upstream
offer or a release tag. A red nightly (a report-only step aside, below) holds further merges until it is fixed or a follow-up
issue names it.

**A step added after 2026-10-23 runs report-only for its first seven nightly runs** (owner,
2026-10-23, from research #78): its failure is logged as such and the nightly still passes, so a
flaky step does not hold merges at once. After the week the step holds merges like any other.

**R1 on the container leg** comes next (#65): the data mounted read-only, an offscreen GL
context, and a baseline per platform, since llvmpipe draws other pixels than the Mac.

## Consequences

- A merge waits about 5 minutes, and a wrong frame fails it.
- GCC-only findings (as `-Wmissing-braces` in #17) and container-only behaviour surface in the
  next nightly for changes outside the container leg's paths, as a follow-up fix.
- The shell tests keep their coverage: on tooling changes and nightly.
