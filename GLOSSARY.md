# Glossary

Terms used in code, tickets, ADRs and agent prompts for the SDL3 + SDL_GPU port.

- **Upstream code**: any file that exists in `upstream/master`. Changed only where a slice needs it.
- **Fork code**: a file this fork adds. Follows every rule in `docs/agents/conventions.md`.
- **Render device**: `dcx::render::device`, the seam between the front end and a renderer (ADR 2).
- **Adapter**: one implementation of a seam: GL, SDL_GPU, recording or fake for the render device.
- **Front end**: the game-facing drawing functions in `similar/arch/ogl` (`g3_draw_*`, `ogl_u*`);
  they keep the current blend and alpha reference and turn calls into draw calls.
- **Draw call**: one self-contained `draw` request: topology, vertices, texture slot, draw state,
  viewport and transforms.
- **Draw state**: blend, depth, cull, alpha reference, wrap and line width of one draw call.
- **Vertex**: position, unclamped colour and texture coordinates of one point of a draw call.
- **Topology**: the primitive a draw call's vertices form (fan, strip, triangles, lines, line loop,
  points), as GL's mode of the same name.
- **Viewport**: the rectangle of the target a draw call covers, in pixels, origin bottom left.
- **Texture slot**: a texture's index in the front end's texture pool; slot 0 draws untextured.
- **Texture image**: the pixels uploaded to a texture slot, RGBA and padded to a power of two,
  with its size, internal format, filter and `has_transparency`.
- **Transform recipe**: a projection or modelview described by the GL calls that build it
  (`perspective`, `ortho`, `translate_scale`, `identity`), never as a matrix.
- **Caps**: what an adapter supports beyond the common set (quad-buffer stereo, `copy_region`,
  anisotropy).
- **Trace**: a recorded sequence of render-device calls (`.rtr`), replayed by `rtrace`.
- **Capture**: frames recorded from the real game through the GL interposer; derived from licensed
  data, so never committed.
- **Synthetic trace**: a trace generated from procedural data; may be committed.
- **Scene**: a scripted, repeatable game run used as a test (S1-S5).
- **Debug harness**: developer aids built only with the `dbg_harness=1` build option (default 0).
  `DXX_DBG_FIXED_FRAMETIME=fps` advances game time by 1/fps per frame, so a demo plays back the
  same frames at any frame rate. `DXX_DBG_FRAMEHASH=path` writes the **frame hash log**: one
  line per presented frame, `<frame> <w>x<h> <FNV-1a 64 of its RGB pixels>`, in sections headed
  `# demo playback <demo file>` or `# no demo` that each count frames from 0, so a demo's frames
  line up across runs. `DXX_DBG_HIDDEN=1` creates the window hidden, so nothing shows on screen;
  on macOS its frames hash the same as a shown window's. `-pilot name` creates a missing pilot with the
  defaults, so a fresh `D2X_REBIRTH_HOME` skips the pilot prompt.
- **Golden**: the reference image or hash a test compares against; always produced by GL replay.
- **R1-R16**: the rubric criteria in the approved plan.
