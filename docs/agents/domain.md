# Domain Docs

How the engineering skills should consume this repo's domain documentation when exploring the codebase.


## Before exploring, read these

This repo is single-context: read **`GLOSSARY.md`** at the repo root, and the ADRs in
**`docs/adr/`** that touch the area you're about to work in. The `/domain-modeling` skill extends
both when a term or decision gets resolved.

Platform research, read before working in its area:

- SDL_GPU adapter, its passes, uploads, pipelines, swapchain and GPU measurement:
  `docs/agents/sdl-gpu-backends.md`.
- SDL3 windows, displays, fullscreen, HiDPI, frame pacing, hidden test windows:
  `docs/agents/sdl-window.md`.
- SDL3 audio: sound effects, music, movie audio, silent runs, per-platform backends:
  `docs/agents/sdl-audio.md`.
- SDL3 input: keyboard, text input, Cmd, mouse and wheel, gamepads, joysticks, saved bindings,
  injected-event tests: `docs/agents/sdl-input.md`.
- Testing input devices: injection, replay, virtual and real rigs, hot-plug: `docs/agents/input-testing.md`.
- Choosing or reviewing a test for the renderer or platform port (what to add, what to skip):
  `docs/agents/testing-games.md`.

## Grounding a brief

Every implementer and reviewer brief names its lane's docs below; the ticket names its own extras.
`R/` is the pinned mirror, `/Users/troyer/Developer/dxx-rebirth/docs/reference/`: name it by that
absolute path, since worktrees lack it. Its index is `tools/dev/reference-docs.txt`.

**Both lanes**: `GLOSSARY.md`; ADR 1 (each upstream line changed costs a rebase); `rubric.md`
(the R items the ticket cites); `testing-games.md`; `~/Developer/dxx-rebirth-spike/FINDINGS.md`.

**Platform-only defaults**: code that creates an SDL object (device, texture, window, audio
stream, gamepad) or sets a hint checks the area doc's table of properties and hints that only one
platform or backend reads. Those defaults go wrong silently on macOS and surface only on that
platform: `sdl-gpu-backends.md` §9, and the "Platform-only" section of `sdl-window.md`,
`sdl-input.md` and `sdl-audio.md`. The reviewer holds the code to the table.

**SDL_GPU lane** (the render device, its SDL_GPU adapter, shaders, backend parity):

- ADR 2: the render device's contract and transform recipes.
- `sdl-gpu-backends.md`, read whole; §5 is the capability probe, §9 the backend-only properties.
- `R/sdl3/SDL_gpu.h` (the API); `R/sdl3/src/gpu/` (what Metal, Vulkan and D3D12 really do, in
  `metal/`, `vulkan/` and `d3d12/`); `R/sdl3/test/testgpu_*.c` (SDL's own usage).
- Shaders: HLSL in `shaders/hlsl/`, generated headers rebuilt by `tools/shaders/build.sh` (R9);
  `R/shadercross/README.txt`.
- `R/vulkan-validation/` for R6; `R/flip/README.md` for FLIP thresholds.
- D3D12 and Windows evidence: `~/Developer/dxx-rebirth-spike/windows/FINDINGS.md` (Q3: which
  backend SDL picks on Windows and what the GPU must support; what needs WARP), and the WARP run
  logs (GitHub Actions, debug layer on) in `~/Developer/dxx-rebirth-spike/notes/d3d12-run-*/`.

**SDL3 platform lane** (init, windows, input, audio):

- ADR 3: SDL3 divergences are new `#elif SDL_MAJOR_VERSION == 3` branches or `dcx::sdl` helpers.
- The area's research doc above.
- For every area: `R/sdl3/README-migration.md`, `R/sdl3/SDL_oldnames.h` (the rename map ADR 3's
  old names rest on) and `R/sdl3/SDL_hints.h`.
- The area's pinned SDL3 files, all under `R/sdl3/`:
  - Init, timer, main: `SDL_init.h`, `SDL_main.h`, `SDL_timer.h`, `README-main-functions.md`.
  - Windows: `SDL_video.h`, `SDL_events.h`, `README-highdpi.md`, `src/video/<os>/`.
  - Input: `SDL_keyboard.h`, `SDL_mouse.h`, `SDL_gamepad.h`, `SDL_joystick.h`, `src/events/`,
    `src/joystick/`.
  - Audio: `SDL_audio.h`, `src/audio/`, and `R/sdl3_mixer/`.
- Windows window, DPI and timer behaviour: `R/microsoft/` (three docs, none on input or audio).

**Not in the mirror**, so the brief names what stands in:

- D3D12's filter, anisotropy and sample-count rules: SDL's mapping in
  `R/sdl3/src/gpu/d3d12/SDL_gpu_d3d12.c` and the debug-layer log of a WARP run.
- Vulkan's sampler chapter: only `R/vulkan-docs/chapters/limits.adoc`.
- SDL2's mouse and event headers.

A web source used instead of a pinned one: the report names it as unpinned.

## Before recommending a change to the plan

The plan is the open tickets, their native dependencies and the ADRs; the rubric that tickets
cite (R1-R16) is `docs/agents/rubric.md`. The spike's measured findings are
`~/Developer/dxx-rebirth-spike/FINDINGS.md`: read them before recommending a change to the
plan, as our measurements win over general advice.

## Use the glossary's vocabulary

When your output names a domain concept (in an issue title, a refactor proposal, a hypothesis, a test name), use the term as defined in `GLOSSARY.md`. Don't drift to synonyms the glossary explicitly avoids.

If the concept you need isn't in the glossary yet, that's a signal: either you're inventing language the project doesn't use (reconsider) or there's a real gap (note it for `/domain-modeling`).

## Flag ADR conflicts

If your output contradicts an existing ADR, surface it explicitly rather than silently overriding:

> _Contradicts ADR-0007 (event-sourced orders), but worth reopening because…_
