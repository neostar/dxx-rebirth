# 3. SDL3 beside SDL1.2 and SDL2: old names plus SDL3 branches

Date: 2026-10-20. Status: accepted.
Amended 2026-10-23 (owner, decided 2026-10-22): the renderer is chosen at startup, not switched at run time.

## Context

SDL1.2, SDL2 and SDL3 can't share a translation unit. Upstream separates SDL1.2 and SDL2 with
`#if SDL_MAJOR_VERSION` branches (129 chains). Three designs were compared (spike workspace
`design/platform-{1,2,3}-*.md`): one SDL-free platform header, upstream-style branches with helpers,
and full ports and adapters. The two module designs move 1400-1500 upstream lines and cost several
hours of rebase work a month; under ADR 1 that cost recurs indefinitely.

## Decision

- `sdl3=1` builds with SDL3's `SDL_ENABLE_OLD_NAMES` (from `SDL_oldnames.h`) and the SDL3 include
  directory, so renamed symbols compile unchanged and removed ones fail at compile time with
  self-describing names.
- Divergences get a new `#elif SDL_MAJOR_VERSION == 3` branch; SDL1.2 and SDL2 branches stay
  untouched, and identical SDL2/SDL3 code uses `>= 2`.
- Repeated or long divergences call helpers in `dcx::sdl` (`common/sdl/dxx-sdl.h`, the header
  through which fork code includes SDL; upstream code keeps its own `<SDL*.h>` includes).
- Depth is added only where it pays off, in fork code: mouse motion with a fraction accumulator,
  fullscreen and window handling, and a window/surface module that chooses the renderer at
  startup: it creates the SDL_GPU device first and falls back to GL with a logged reason, then
  creates the window (`SDL_WINDOW_OPENGL` for GL, no graphics flag for SDL_GPU, which claims it);
  `docs/agents/sdl-window.md` section 2.
- A lint checks that every chain with an SDL2 branch has an SDL3 or `>= 2` branch, instead of an
  `#else #error` in all 129 chains: `tools/dev/check_sdl_arms.py`, `check.sh`'s `sdl_arms` step,
  with the chains not yet ported counted per file, each with a reason, in
  `tools/dev/sdl-arms-allow`.
- Tests drive real SDL on headless drivers: `SDL_PushEvent`, virtual gamepads, the disk audio driver.

## Consequences

- SDL1.2/SDL2 preprocessed output stays identical, so R8 is checkable by diff.
- SDL knowledge stays spread over about 35 files.
- When upstream changes an SDL2 branch, its SDL3 twin can go stale without a conflict; the lint
  and the R12 tests catch it.
- SDL2 has no mouse-wheel handling today; adding it is a deliberate R8 exception.
