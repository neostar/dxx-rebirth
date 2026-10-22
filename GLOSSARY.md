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
- **Readback**: `gr_read_pixels`, reading a rectangle of the presented or the in-progress frame
  back from the renderer, named from the top left and returned top row first
  (`common/render/readback.h`); the savegame thumbnail and screenshots use it.
- **Texture slot**: where a texture lives for the render device: its index in the front end's
  texture pool plus one, or for the movie frame, kept outside the pool, the slot after the pool's;
  slot 0 draws untextured.
- **Texture image**: the pixels uploaded to a texture slot, RGBA and padded to a power of two,
  with its size, internal format, filter and `has_transparency`.
- **Transform recipe**: a projection or modelview described by the GL calls that build it
  (`perspective`, `ortho`, `translate_scale`, `identity`), never as a matrix.
- **Caps**: what an adapter supports beyond the common set (quad-buffer stereo, `copy_region`,
  anisotropy).
- **Trace**: a recorded sequence of render-device calls (`.rtr`), replayed by `rtrace`: the render
  target's size, then one record per draw, texture (its pixels included), clear and present, so it
  replays without the game; a present marks each frame's end and gives the game's frame index.
  Format, writer and reader: `common/render/trace.h`; replay: `common/render/replay.h`; the
  `rtrace` tool and its hidden GL window: `common/render/rtrace/`.
- **Capture**: frames recorded from the real game through the GL interposer; derived from licensed
  data, so never committed.
- **Synthetic trace**: a trace generated from procedural data; may be committed.
- **Scene**: a scripted, repeatable game run used as a test (S1-S5).
- **Debug harness**: developer aids built only with the `dbg_harness=1` build option (default 0).
  `DXX_DBG_FIXED_FRAMETIME=fps` advances game time by 1/fps per frame, so a demo plays back the
  same frames at any frame rate. `DXX_DBG_FRAMEHASH=path` writes the **frame hash log**: one
  line per presented frame, `<frame> <w>x<h> <FNV-1a 64 of its RGB pixels>`, in sections headed
  `# demo playback <demo file>`, `# scene <name>` or `# no demo` that each count frames from 0, so
  a demo's or a scene's frames line up across runs. `DXX_DBG_INPUT_SCRIPT=path` plays the
  **input script** of a scene: timed key presses, one per line (`common/debug/input_script.h`),
  timed in **harness ticks** (calls of `event_process`) and pushed as SDL events; while it plays,
  the harness clock replaces the wall clock (each `timer_update` advances it 1/fps of
  `DXX_DBG_FIXED_FRAMETIME`, which it needs), so timed screens repeat too, and its `end` line
  ends the run. `DXX_DBG_HIDDEN=1` creates the window hidden, so nothing shows on screen;
  on macOS its frames hash the same as a shown window's. `-pilot name` creates a missing pilot with the
  defaults, so a fresh `D2X_REBIRTH_HOME` skips the pilot prompt.
- **Golden**: the reference image or hash a test compares against; always produced by GL replay.
- **R1-R16**: the rubric criteria in the approved plan.
- **R1 baseline**: the frame hash logs of S1-S4 played by a build of the branch point, one set
  per `-maxfps` value, kept in `traces/local/r1/baseline/` with the commit they came from;
  `tools/dev/r1.sh check` compares a change's frames with it.
