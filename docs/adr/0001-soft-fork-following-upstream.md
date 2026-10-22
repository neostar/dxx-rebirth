# 1. Soft fork that follows upstream

Date: 2026-10-20. Status: accepted.

## Context

The SDL3 + SDL_GPU port lives on the fork `neostar/dxx-rebirth`. Upstream is active (857 commits by
the maintainer since 2025). The port will have more than one maintainer, and the owner wants the
option to offer it upstream once it is stable and playable.

## Decision

The fork follows upstream: `sdl-gpu-port` is rebased on `upstream/master` regularly, and every
change is written so upstream could accept it.

- Upstream code changes only where a slice needs it; SDL1.2, SDL2, GLES and `opengl=0` builds keep
  building and behaving as today (rubric R8).
- Fork code follows `docs/agents/conventions.md`, which matches upstream's own style.
- Commits are small, signed and self-contained, so a series can be offered upstream as is.

## Consequences

- Upstream fixes arrive through rebases; each rebase is checked with `git range-diff` and a
  per-commit build (`git rebase --exec`). `git rerere` is off: it replayed a bad resolution.
- Designs that move or delete large amounts of upstream code are rejected in favour of designs that
  add beside it (ADR 3).
- Deleting SDL1.2/SDL2 support stays out of scope; it would turn every rebase into conflicts.
