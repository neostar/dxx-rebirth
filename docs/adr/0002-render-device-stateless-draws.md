# 2. Render device: stateless draws with transform recipes

Date: 2026-10-20. Status: accepted.
Amended 2026-10-23 (owner): the `fan` helper is `render::ambient::fan`, on the front end's state.
Amended 2026-10-27 (owner, decided 2026-10-22): the renderer is chosen at startup, not switched at run time.
Amended 2026-10-27 (owner, #16): the device gains `select_eye` and `copy_region`, GL-only behind caps.
Amended 2026-10-27 (#28): the depth pairing rule.
Amended 2026-10-28 (#32): the pixel-centre rule for lines and points.
Amended 2026-10-28 (owner, #29): the depth band keeps `discard` on every depth-tested draw.
Amended 2026-10-29 (#33): the font formats are not quantized; the adapter uploads the texel GL samples.
Amended 2026-10-29 (#34): SDL_GPU builds GL's mipmaps; the FLIP bound of each texture filter.
Amended 2026-10-31 (#31): the vertex shader's position is `precise`.

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
  (#25) and D3D12 (WARP) are not measured. Decided (owner, #29): the model stays and every
  depth-tested draw keeps `discard`. T9's screen of 48 combinations found no other bit-exact
  one; in the demo only explosion sprites reach the band (41 draws in 17 of 5,396 frames), and
  keeping `discard` costs at most 1 % (1024x768 and 3072x2304).
- **Precise position** (#31): `_g3_draw_tmap_2` draws a wall twice, the base texture and then
  the overlay texture over the same corners, in one draw state (depth `test_and_write`, LEQUAL),
  so the second pass shows only where its depth is not above the first's. The vertex shader's
  position output is `precise`, so both passes get bit-identical depth whatever pipeline each
  runs on. SPIRV-Cross then writes both matrix products as the ordered fma chains of
  `spvFMulMatrixVector` (marked `[[clang::optnone]]`, its intent; Metal documents no guarantee),
  and DXC drops the `fast` flags and marks the chain `dx.precise` (pinned dxc: 14 of 14 float
  operations `fast` before, 27 `dx.precise` and 2 `fast` after). Measured on Metal (M1 Max,
  Darwin 27.2.0), the overlay trace has 0 depth ties and GL's hashes with `precise` and without
  it, so on Metal today it changes the shader and no pixel; it guards other compilers (Vulkan
  and D3D12 drivers are not measured). `shader_blobs`'s `vertex_position_is_precise` pins it in
  the committed blobs; test-sdlgpu's `overlay_passes_tie_nowhere` counts the ties (frames 1 and
  2 of `overlay.rtr`: an opaque base and a transparent overlay), and
  `depth_clear_keeps_the_colour_and_resets_the_depth` pins the pass split at a depth-only clear.
- **Pixel-centre rule** (#32): lines, line loops and points reach SDL_GPU as triangle lists
  (`common/render/line_quads.h`), because D3D12 has no wide lines or sized points. A pixel belongs
  to a quad when its centre lies inside, and on the quad's left or upper edge in window
  coordinates with y down (Metal's top-left rule; Vulkan and D3D12 are not measured). The expansion
  runs in clip space, on the triangles' transform model: the segment is clipped against GL's six
  planes before any divide, and each corner moves in the image plane by pixels * w / viewport
  (clip units), which the diagonal entries of P and MV map back to a model-space step for the
  unchanged vertex shader. Diagonal recipes only: there is no inverse, and `expansion_view_of`
  asserts that no x or y row of P or MV mixes x with y.
  Dividing first mirrors an endpoint behind the eye (the spike's `push_wide_line` computes
  `clip[0] / clip[3]` with no clip); test-sdlgpu's
  `dividing_before_the_clip_mirrors_an_endpoint_behind_the_eye` replays that expansion on the
  lines trace and finds 69 px off GL, against 1 for the adapter.
  - Width w >= 2: GL draws w pixels along the minor axis (a parallelogram with vertical edges for
    an x-major line, horizontal for a y-major one; ties at |dx| = |dy| take x-major), with
    Metal's ties as above and no slide: GL's wide ties do not follow the line's direction
    (probed at widths 2 and 3, up and down, left and right; `wide_line_ties_do_not_follow_the_direction_on_gl`).
    Axis-aligned lines match pixel for pixel. The lines trace's width-2 frame is bit-exact with GL.
  - **Major axis**: x-major when the endpoints, rounded to a 1/256 pixel grid in window space,
    have |dx| >= |dy| (`x_major`). GL's float endpoints put |dx| and |dy| of an exact diagonal
    about 1e-6 pixel apart either way, so a plain comparison picked y-major on all four 45-degree
    spokes of the lines trace and drew three of them against GL (colours along y where GL runs
    them along x). On the grid the 135, 225 and 315 degree spokes tie, which is x-major, and the
    45 degree one is a unit short of a tie and stays y-major, as GL has it
    (`diagonal_spokes_take_gls_major_axis`). The grid is inferred from these four spokes
    (several grids and roundings fit them): a hardware vertex grid of 1/256 pixel is the
    working model, not a measured GL fact.
  - Width 1: GL's diamond-exit rule includes a centre at the line's start and not at its end,
    whichever way it runs. The adapter slides the line 1/256 pixel along itself, away from the
    included end, which moves the ties to GL's. Only ties at pixel centres follow; an endpoint
    inside the last pixel's diamond (GL leaves that pixel out) and exact 45-degree ties do not.
  - Points: a square of round(size) pixels, at least 1, centred on the point (Apple's GL rounds
    1.5 to 2). The game's widths are whole (`linedotscale`).
  - Quads are drawn without culling, as GL culls triangles only.
  - **R3 tolerance** (Metal): on the lines trace (`lines.rtr`), loops, strips and points are
    bit-exact (frame 2 has GL's hash). The other frames cover GL's pixels but at most 14, 0, 2
    and 1 of 3072 px in frames 0, 1, 3 and 4, each
    next to a pixel both cover, plus colour differences (below). GL's golden coverage and colours are `rtrace/lines_coverage.h` and
    `rtrace/lines_colours.h`; `lines_trace_frames_match_gl_within_the_tolerance` holds the adapter
    to both. Each coverage difference is one pixel at one end of a line, at most one per end (14
    of 76 ends in frame 0); frame 1 is bit-exact with GL. Five of frame 0's 14 are ends of exact
    diagonals, where the end pixel is the diamond-exit rule's tie (moving the major axis shifted
    two of them from one spoke to another: 13 became 14); the other nine are at ends of ordinary
    lines, inside a pixel's diamond (unmodelled, as in the width-1 bullet). The limits stay the measured counts: a rule of one pixel per end
    would allow 76. Vulkan and D3D12 are judged by FLIP in #37 (R4's bounds).
  - **Colour along a line** is GL's to the byte on every pixel but 14 of frame 0, which are 1
    level off in one channel (frame 1, 3 and 4 are exact on the pixels both cover). GL
    interpolates along the major axis at the pixel's centre, not at the centre's projection onto
    the line (the earlier model of this paragraph, refuted: the 38-level differences it
    explained were the three diagonals above running along the wrong axis). The 14 pixels sit
    on 8 spokes, five of them on the vertical one; GL's value is within 0.45 level of the exact
    interpolation (round to nearest) and the quad's within 0.6, a half level off in either
    direction. Cause open: not the 1/256 slide (0.1 level at most); candidates are the
    rasteriser's attribute precision and the blend's rounding. Vertex colours are clamped to
    [0, 1] before they interpolate, as GL does
    (`colours_above_one_are_clamped_along_a_clipped_line`).
  - **Not measured**: the 1/256 pixel slide needs at least 8 subpixel bits. D3D12 has exactly 8
    and keeps the slide only by rounding to nearest; Vulkan guarantees 4 (#37 checks). Wide lines
    and points at a sub-viewport's edge: GL spills fragments past the viewport (only the scissor
    stops them), SDL_GPU clips to the viewport.
- The **front end** (`similar/arch/ogl`) keeps the game's current blend and alpha reference, as GL
  did, and builds each draw state from them. Game code calling `gr_settransblend` is unchanged.
  `render::ambient::fan(vertices, texture)` (`common/render/ambient.h`) builds the common case's
  self-contained draw call from that state.
- Textures live in the front end's existing pool; `has_transparency` lets SDL_GPU skip `discard`, except that a vertex alpha below the
  reference also needs it and every depth-tested draw keeps it (the depth band, above).
- **Texture formats** (#33): `texture_format` names the GL internal format, and an adapter uploads
  the texel GL samples from the image's RGBA, not the bit depth of the name. Measured on Apple's GL
  (M1 Max, macOS 27.2), 64 levels per channel through `fonts_trace`'s ramps
  (`gl_keeps_the_font_formats_at_eight_bits`): `GL_RGBA2`, `GL_LUMINANCE4_ALPHA4` and
  `GL_INTENSITY4` keep all eight bits, so the RGBA2 frames equal the RGBA8 ones and the 64 levels
  stay distinct. A control on the same draw-and-`glReadPixels` path shows quantization where there
  is some: `GL_RGBA4` samples 16 levels. One quirk: `glGetTexImage` on a `GL_RGBA2` texture
  returns 16 levels although sampling keeps all 64, so a capture that read textures back from GL
  would see 4-bit RGBA2; sampling is what draws the frame. SDL_GPU quantizes nothing. The formats
  differ from RGBA8 in which channels they keep: `rgb8` samples alpha 1; `luminance4_alpha4` a grey of the red with its own alpha;
  `intensity4` the red in all four channels. `sdlgpu::frame::texture` applies that, and the fonts
  trace hashes as GL's (R3). The game's own font texels are uniform (white or clear), so only a
  mixed-channel image shows the rule. Not measured: other GL drivers, which may store these
  formats at their named depth; the SDL_GPU frames then differ from theirs by that quantization.
  That caveat touches no game frame: SDL_GPU samples `R8G8B8A8_UNORM` on every backend, and the
  game's intensity4 and luminance4_alpha4 texels are 0 and 255, exact at any UNORM depth. Only
  `GL_RGBA2` could differ, with palette colours, used only under `-gl_intensity4_ok 0
  -gl_luminance4_alpha4_ok 0`.
- **Mipmaps and filters** (#34): GL builds a trilinear or anisotropic texture's mipmaps with
  `gluBuild2DMipmaps`, on the CPU. SDL_GPU builds the same chain (`common/render/mipmaps.h`:
  a 2x2 mean rounded half up, a mean of two truncated once a side is 1) and uploads every
  level. Measured on Apple's GL (M1 Max, macOS 26.7): `glGetTexImage` of every level equals
  the chain (test-rtrace's `gl_mipmaps_are_glus_box_filter`); SDL's
  `SDL_GenerateMipmapsForGPUTexture` is the backend's own filter (a blit), so it is not used.
  A sampler without linear mipmaps reads the base level alone, as `GL_NEAREST` does; D3D12's
  nearest sampler drops anisotropy (`device_caps::anisotropy_with_nearest`), so the classic
  filter with anisotropy looks there as classic without it.
  - **FLIP per filter** (LDR-FLIP mean of a frame against GL's, the filters trace,
    `filters.rtr`): Metal measures classic 0.0079 (5 of 3072 px, each taking another
    minified nearest texel, up to 225 levels off), classic with anisotropy 0.00008, upscale and trilinear 0.0005, each with
    anisotropy 0.0008 (at most one level off in a channel, on 2 to 38 px); without mipmaps
    the filtered frames measured 0.12 to 0.23. Metal's frames are pinned as its own
    (`expected_filters_metal`), as the lines trace's are, and `rtrace-tests.sh` holds them to
    GL's frames by those pixel counts and levels (`filters_gl_tolerance`). The bound #37 holds Vulkan and
    D3D12 to on this trace, per frame: classic 0.02, every other filter 0.01, both inside
    R4's worst-frame 0.03. D3D12's classic frame with anisotropy is held to the classic bound
    against GL's classic frame: GL's classic frame against GL's anisotropic one measures
    0.24, the look the decided D3D12 sampler gives up. The bounds are first values: #37
    calibrates them from the Vulkan and D3D12 runs.
  - **Minified nearest is not bit-exact on Metal.** Classic is the game's default filter,
    and in-game scenes are full of distant minified textures, so R4's "Metal bit-exact"
    will likely fail on scenes for this reason, as on #18's opaque texel ties. The cause is
    not measured: the classic frame's 3x magnified quad matches GL exactly, so the texel
    centre is right; sample-coordinate precision under strong minification is more likely
    than exact ties. R4's Metal rule for minified nearest is the owner's decision in #37
    ([comment](https://github.com/neostar/dxx-rebirth/issues/37#issuecomment-6484114572)).
- GL-only features (quad-buffer stereo, `copy_region`) stay behind `caps`.
- No command list in the interface; SDL_GPU collects a frame internally and submits at `present`.

## Consequences

- Recording and fake adapters store values; tests compare values instead of modelling GL state.
- Each draw re-sends about 100 bytes of state; adapters deduplicate (GL state shadow, SDL_GPU
  pipeline key).
- A new effect extends the closed `blend` or recipe lists: an interface change, accepted as the
  price of R1.
