# Domain Docs

How the engineering skills should consume this repo's domain documentation when exploring the codebase.

- Choosing or reviewing a test for the renderer or platform port (what to add, what to skip): `docs/agents/testing-games.md`.

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

## Before recommending a change to the plan

The approved plan of the port is `~/.claude/plans/ok-i-want-the-greedy-sutton.md`; the spike's
measured findings are `~/Developer/dxx-rebirth-spike/FINDINGS.md`. Read the findings before
recommending a change to the plan: our measurements win over general advice.

## Use the glossary's vocabulary

When your output names a domain concept (in an issue title, a refactor proposal, a hypothesis, a test name), use the term as defined in `GLOSSARY.md`. Don't drift to synonyms the glossary explicitly avoids.

If the concept you need isn't in the glossary yet, that's a signal: either you're inventing language the project doesn't use (reconsider) or there's a real gap (note it for `/domain-modeling`).

## Flag ADR conflicts

If your output contradicts an existing ADR, surface it explicitly rather than silently overriding:

> _Contradicts ADR-0007 (event-sourced orders), but worth reopening because…_
