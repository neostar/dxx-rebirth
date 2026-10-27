# 2. Render device: stateless draws with transform recipes

Date: 2026-10-20. Status: accepted.
Amended 2026-10-23 (owner): the `fan` helper is `render::ambient::fan`, on the front end's state.
Amended 2026-10-27 (owner, decided 2026-10-22): the renderer is chosen at startup, not switched at run time.
Amended 2026-10-27 (owner, #16): the device gains `select_eye` and `copy_region`, GL-only behind caps.

## Context

GL and SDL_GPU must sit behind one seam, the renderer chosen at startup, with GL output bit-identical to
today (R1). Three designs were compared (spike workspace `design/render-{1,2,3}-*.md`): a minimal
stateless device, a per-frame command list, and a GL-like device with sticky state. The spike showed
that a matrix built on the CPU can differ from GL's by one ULP and that this causes visible depth
ties, and that sticky state is what lets the alpha test leak into 2D.

## Decision

- The seam is `dcx::render::device`, an interface with `draw`, `texture` and three frame operations
  (`clear`, `present`, `read_pixels`), plus `select_eye` and `copy_region` for stereo, which only
  a device whose caps report them implements (the base throws `std::logic_error`). Adapters: GL,
  SDL_GPU, recording, fake.
- A **draw call** is self-contained: topology, vertices, texture slot, draw state, viewport and
  transforms. No state carries from one draw to the next.
- Transforms are **transform recipes** (`perspective`, `ortho`, `translate_scale`, `identity`),
  never matrices. The GL adapter replays the same GL calls as today, so R1 holds by construction;
  the SDL_GPU adapter builds matrices with the exact model from the spike (clip = P·(MV·v), viewport
  depth 0.5..1, depth clamp plus near/far clip distances).
- The **front end** (`similar/arch/ogl`) keeps the game's current blend and alpha reference, as GL
  did, and builds each draw state from them. Game code calling `gr_settransblend` is unchanged.
  `render::ambient::fan(vertices, texture)` (`common/render/ambient.h`) builds the common case's
  self-contained draw call from that state.
- Textures live in the front end's existing pool; `has_transparency` lets SDL_GPU skip `discard`.
- GL-only features (quad-buffer stereo, `copy_region`) stay behind `caps`.
- No command list in the interface; SDL_GPU collects a frame internally and submits at `present`.

## Consequences

- Recording and fake adapters store values; tests compare values instead of modelling GL state.
- Each draw re-sends about 100 bytes of state; adapters deduplicate (GL state shadow, SDL_GPU
  pipeline key).
- A new effect extends the closed `blend` or recipe lists: an interface change, accepted as the
  price of R1.
