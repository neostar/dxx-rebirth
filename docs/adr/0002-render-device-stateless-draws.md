# 2. Render device: stateless draws with transform recipes

Date: 2026-10-20. Status: accepted.
Amended 2026-10-23 (owner): the `fan` helper is `render::ambient::fan`, on the front end's state.
Amended 2026-10-27 (owner, decided 2026-10-22): the renderer is chosen at startup, not switched at run time.
Amended 2026-10-27 (owner, #16): the device gains `select_eye` and `copy_region`, GL-only behind caps.
Amended 2026-10-27 (#28): the depth pairing rule.

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
- **Depth pairing rule** (#28): no depth remap with depth clamp plus the near and far clip
  distances (the exact model above, `enable_depth_clip = false`), or a remap of [-1, 1] to [0, 1]
  with depth clip; never one half of each. SDL documents the flag as
  "true to enable depth clip, false to enable depth clamp" (`SDL_GPURasterizerState`,
  `docs/reference/sdl3/SDL_gpu.h`, 3.4.18), and adds: "the D3D12 driver will enable depth clamping
  even if enable_depth_clip is true". Depth clip keeps 0 <= z <= w in clip space; without a remap
  the clip depth of `gluPerspective` is 0 at 2·near·far/(far + near), about twice the near plane,
  so the exact model with depth clip drops everything nearer. `test-sdlgpu`'s
  `depth_clip_drops_what_is_nearer_than_twice_the_near_plane` renders the walls trace's near-plane
  frame both ways: a quad at 1.5 near planes vanishes with clip and shows with clamp.
  Depth clamp without the clip distances draws what lies before the near plane (spike #55: 8134
  px off GL). The rule holds while no shader writes its own depth (SDL#12740).
- **The depth band** (#28), from the near plane to 2·near·far/(far + near), where z_ndc < 0:
  the exact model is not known to be exact there. GL writes window depth 0.5 + 0.5·z_ndc
  (0.25 to 0.5). A bare Metal probe with depth clamp and viewport depth 0.5..1 writes 0.5 for
  z_ndc -0.5 and -0.25 (the #28 review), and Vulkan and D3D12 clamp to the viewport's minimum
  by spec; a clamp there ties every surface in the band, and LESS_OR_EQUAL then keeps the one
  drawn last where GL keeps the nearer. Through the device on Metal (M1 Max, macOS 26.7) the
  band does not tie: test-sdlgpu's `depth_band_keeps_the_nearer` draws a quad at 0.12 deep,
  then one at 0.18 behind it, and the frame has GL's hash (test-rtrace's
  `depth_band_keeps_the_nearer_on_gl`), 0 px off; at equal depth the later quad covers
  432 px, so the test sees a tie. The device's order in the band depends on the discarding
  shader (measured, #29): without the depth-tested exemption in `needs_alpha_test`,
  `depth_band_keeps_the_nearer` (192 blue px, wrong hash) and `depth_test_keeps_the_nearer`
  (3072 px) go red. The mechanism is inferred, not documented: a shader with `discard` takes
  Apple's late depth-test path, which evidently skips the viewport clamp (SDL sets
  `MTLDepthClipModeClamp` per pipeline). This rests on undocumented Apple-GPU behaviour an OS
  update could change; the exemption is that dependency. Vulkan
  (#25) and D3D12 (WARP) are not measured. Open for the owner: whether to change the model
  for the band; T9's screen of 48 combinations found no other bit-exact one.
- The **front end** (`similar/arch/ogl`) keeps the game's current blend and alpha reference, as GL
  did, and builds each draw state from them. Game code calling `gr_settransblend` is unchanged.
  `render::ambient::fan(vertices, texture)` (`common/render/ambient.h`) builds the common case's
  self-contained draw call from that state.
- Textures live in the front end's existing pool; `has_transparency` lets SDL_GPU skip `discard`, except that a vertex alpha below the
  reference also needs it and every depth-tested draw keeps it (the depth band, above).
- GL-only features (quad-buffer stereo, `copy_region`) stay behind `caps`.
- No command list in the interface; SDL_GPU collects a frame internally and submits at `present`.

## Consequences

- Recording and fake adapters store values; tests compare values instead of modelling GL state.
- Each draw re-sends about 100 bytes of state; adapters deduplicate (GL state shadow, SDL_GPU
  pipeline key).
- A new effect extends the closed `blend` or recipe lists: an interface change, accepted as the
  price of R1.
