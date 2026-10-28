# Driving Metal, Vulkan and D3D12 through SDL_GPU

What SDL_GPU 3.4.18 does per backend, which decisions it leaves to the SDL_GPU adapter of the
render device (ADR 2), and what the adapter should do with each, for speed (R11) and a small
codebase. Read it before designing, implementing or reviewing the SDL_GPU adapter or a ticket
named under an area. Correctness (headless readback, shader formats, no Y flip on Metal, the
exact transform model) was settled by spike #55 and is not repeated. Window, display, frame
pacing and the clock belong to `docs/agents/sdl-window.md`; swapchain, present mode and
GPU-side HiDPI belong here.

Source IDs: `S0` is `docs/reference/sdl3/SDL_gpu.h`, `S1`-`S5` are SDL's GPU sources under
`docs/reference/sdl3/src/gpu/`, all at tag `release-3.4.18` and fetched by
`tools/dev/fetch-docs.sh`; a `file:line` stands for the passage at that line. `V*` are vendor,
Khronos and SDL-project sources. Passages, dates, the SDL version detail, the currency of what
SDL uses against current vendor guidance, and how to extend this research:
`docs/agents/sdl-gpu-backends-sources.md`. Researched 2026-10-22 (#71); the SDL pin stays at
3.4.18, moving to 3.4.20 when it is released.

## Summary

The spike's frames hold 247-507 draw calls, 4-5 pipelines, 2 samplers and 20-39 textures, about
95 % fans of about 4 vertices (spike FINDINGS, T3/T5): few draws for any of the three APIs.
What costs is how SDL turns each draw into binds. ADR 2 lets the adapter collect a frame and
submit at `present`, so every finding fits one frame shape:

```
draw/texture/clear ──► frame record (CPU): vertices and texture pixels into the mapped
                       transfer buffer, draws as small records
present ──► copy pass: vertex upload + texture uploads
        ──► render pass(es) into the offscreen colour + depth target
        ──► acquire swapchain, blit offscreen → swapchain, submit
```

| # | Area | Recommendation | Tickets |
|---|---|---|---|
| 1 | Draw volume | Bind cache for pipeline, viewport, texture and uniforms; vertex buffer bound once at offset 0, draws by `first_vertex`; draw in game order; merge equal-state neighbours only if #53 asks | #26-#28, #53 |
| 2 | Uploads | One transfer buffer per frame mapped with `cycle=true`; all uploads in one copy pass before the render pass; tight rows at 512-byte offsets; movie texture uploaded with `cycle=true` | #24, #27, #36 |
| 3 | Passes, pipelines | Offscreen target, one pass per frame, split at depth clears with depth carried by STORE/LOAD; small closed pipeline set built at device creation | #24, #26, #30, #31 |
| 4 | Shader binding | SDL's register layout as in `shaders/hlsl/`; two uniform slots pushed on change | #26-#28 |
| 5 | Capabilities | Probe once: D32_FLOAT preferred, sample counts, present modes, anisotropy clamped to 16 | #28, #34, #35, #48, #52 |
| 6 | Presentation | Frames in flight 2 set once; acquire at present; VSYNC by default, IMMEDIATE on macOS opt-in only (SDL #12715) | #48, #50 |
| 7 | Measurement | CPU time in-game everywhere; GPU time on Metal via Instruments; SDL call counts as the only D3D12 proxy | #53 |
| 8 | Device choice | SDL's order, driver by option or `SDL_GPU_DRIVER`; debug mode explicit; hardware Vulkan only in the game, GL fallback | #50, #52 |
| 9 | Backend-only properties | Each one's default checked; D3D12 targets created with the clear values their load ops use | #28, #35 |

## 1. Draw-call volume and batching

**What SDL does.** A draw checks bindings only in debug mode (S1 `SDL_gpu.c:2347-2372`). Each
backend rebinds only changed textures and samplers (S3 `:2583-2612`, S4 `:7683-7711`,
S5 `:4917-4945`). SDL passes every pipeline bind through,
`RENDERPASS_DEVICE->BindGraphicsPipeline(` (S1 `:1928`), and every backend then marks all
bindings dirty (S3 `:2405-2457`, S4 `:8103-8150`, S5 `:4753-4795`), so a repeated bind costs a
full rebind on the next draw. Cost of a changed binding per backend:

- **Metal**: one `set*` call per dirty slot, an `@autoreleasepool` per draw (S3 `:2666-2780`,
  `:2861-2880`).
- **Vulkan**: any dirty flag, a uniform push or a new vertex-buffer offset included, passes the
  early return `if ( !commandBuffer->needVertexBufferBind && ... )` (S4 `:5186-5196`) and rebinds
  all four descriptor sets; a new texture or pipeline also allocates and writes a descriptor set
  (S4 `:5167-5470`).
- **D3D12**: a new texture copies descriptors with `CopyDescriptorsSimple` into the
  shader-visible heap (S5 `:5045-5080`); uniforms are root CBVs (S5 `:5195`); heaps hold 65 536
  view descriptors (S5 `:122`), far above a frame, and a heap switch "can incur a pipeline flush
  on some hardware" (V-M3).
- Uniform pushes take 256 bytes each on Metal and D3D12, in 32 KiB pool buffers (S3 `:2968-3040`,
  S5 `:4671-4750`, S2 `:35`).

**Recommendation.**

1. A **bind cache**: remember the bound pipeline, viewport, texture + sampler and the bytes last
   pushed per uniform slot, and call SDL only on change.
2. Keep the frame's vertex buffer bound at offset 0 and start each draw at its `first_vertex`.
3. Draw in game order: many draws run with depth off, and all GL draws blend (spike T3), so
   sorting by texture or pipeline would change the image. Batching means **merging neighbours**
   whose pipeline, texture, viewport and uniforms are equal into one triangle list
   (`fan_to_list` already emits lists). Build it only if #53 measures SDL_GPU CPU time over GL
   (M1). Instancing and indirect draws do not fit fans with their own vertices.

Vendor guidance (Arm, Khronos, AMD, Apple) agrees: sources file, "Vendor guidance on draw volume".

## 2. Upload strategy and cycling

**What SDL does.** Buffers, transfer buffers and textures are containers; with `cycle=true` a
write to a resource still in flight goes to a free internal one, created if none is free
(S0 `:309-353`; S3 `:1547-1578`; S4 `:6065-6110`). Cycling a texture makes all of it undefined
(S0 `:345-347`). Vulkan brackets each texture copy with two pipeline barriers, since barriers
cannot sit inside a render pass (S4 `:2550-2575`, `:8869-8925`). D3D12 without options 13 needs
256-byte row pitch and 512-byte offsets and otherwise copies through a temporary buffer
(S5 `:6018-6060`). Metal reads rows as `sourceBytesPerRow:BytesPerRow(destination->w, ...)`
(S3 `:1814-1816`): it ignores `pixels_per_row` (SDL #16474, open, V-S5).

**Recommendation.**

1. A **transfer ring**: one upload transfer buffer mapped with `cycle=true` per frame; `draw`
   appends vertices and `texture` appends pixels as the calls arrive (ADR 2's views live only
   during the call, so the copy happens anyway). At `present`: unmap, one copy pass with the
   vertex upload and all texture uploads (`cycle=true`), then the render pass. SDL: "Perform your
   data uploads as early as possible in the frame" (S0 `:163`).
2. Grow by chunks: a frame that outgrows the buffer gets a second transfer and vertex buffer;
   keep the high-water mark.
3. Tight rows (`pixels_per_row = 0`) at 512-byte aligned offsets. Power-of-two widths from 64 up
   already have 256-byte rows; narrower ones cost D3D12 a realignment copy without options 13.
4. A slot re-uploaded after a draw in the same frame used it ends the render pass; the next pass
   loads colour and depth (area 3). Rare; M3 counts it.
5. **Movies (#36)**: one persistent texture, uploaded with `cycle=true` per movie frame.
6. `read_pixels` downloads from the offscreen target, since "The swapchain texture is write-only"
   (S0 `:4337`), and waits on a fence.

## 3. Render passes and pipeline switches

**What SDL does.** "Beginning a new render pass is relatively expensive. Use as few render
passes as you can." (S0 `:158-159`); all render state resets when a pass ends (S0 `:86-88`).
There is no clear inside a pass: a clear is a load op. `cycle` on a target applies only "if the
texture is bound and load_op is not LOAD" (S0 `:2036`; depth: "any load ops are not LOAD",
`:2097`). Pipelines are built synchronously with no cache: `(VkPipelineCache)VK_NULL_HANDLE`
(S4 `:6764`), empty `CachedPSO` (S5 `:2994-2995`), MSL compiled by `newLibraryWithSource`
(S3 `:864`).

**Recommendation.**

1. Render into an **offscreen target**: colour R8G8B8A8_UNORM (as the spike's headless device),
   D32_FLOAT depth, at the game's resolution. The first pass of a frame clears colour (the game
   clears to black after every swap, spike T5) and depth, with `cycle=true`. Then blit to the
   swapchain, whose format SDL picks ("B8G8R8A8 or R8G8B8A8", S0 `:1351`;
   `SDL_GetGPUSwapchainTextureFormat`). The same target serves `read_pixels`, HiDPI scaling and
   headless traces. M9 measures the blit against drawing straight into the swapchain.
2. The **pass planner** splits a frame at each mid-frame depth clear (3D view, rear-view, #31) and
   at mid-frame re-uploads. Store and load ops:
   - colour: STORE on every pass; LOAD on every pass after the first.
   - depth: STORE on a pass whose successor does not clear depth, and LOAD on that successor;
     DONT_CARE only on the frame's last pass or on a pass whose successor clears depth.
   - `cycle` only on the first pass: SDL ignores it under LOAD anyway.

   On Apple GPUs a load "is significantly slower than MTLLoadAction.dontCare or
   MTLLoadAction.clear" (V-A2). If #53 finds the splits costly, replace a depth-clear split with a
   **depth-reset draw** in the pass: a viewport quad, depth compare ALWAYS, colour writes off (M2).
3. The **pipeline cache** (#26, `pipeline_key` in `common/render/sdlgpu_frame.h`). Built so in
   #26; lines (#32) and points remain to confirm it: every topology reaches SDL as a
   triangle list (fans by `fan_to_list`, strips by `strip_to_list`, lines and points as clip-space quads by `line_quads.h`, #32, ADR 2's pixel-centre rule), so
   the key is (blend, depth, cull, alpha test) = 5 x 2 x 2 x 2 = 40 pipelines (#29) per (colour format, depth
   format, sample count), plus the depth-reset pipeline; SDL builds its own blit pipelines for
   `SDL_BlitGPUTexture`. The spike saw only 14 GL state combinations, about 6 pipelines (spike
   FINDINGS). `alpha_ref`, `wrap` and `line_width` are uniform, sampler and CPU state. Build the
   set at device creation (#24 asks this) and rebuild it when MSAA or the depth format changes;
   no disk pipeline cache. Topology is not in the key: points are quads too.
4. Keep `discard` out of opaque draws (#29, built: `tex_opaque.frag.hlsl`; a draw takes the discarding pipeline when its reference is above 0 and its texture has `has_transparency` or a vertex alpha is below the reference; every depth-tested draw also keeps the discarding pipeline until the depth model is decided, ADR 2): on Apple GPUs discard makes
   "feedback fragments", which with blending "reduce hidden surface removal efficiency" (V-A2).

## 4. Shader binding conventions per backend

**What SDL does.** SDL prescribes the layout per format (S0 `:2711-2752`): SPIR-V sets 0/1 for
vertex resources and uniforms, 2/3 for fragment; DXIL `space0`/`space1` and `space2`/`space3`;
MSL `[[texture]]`, `[[sampler]]`, `[[buffer]]` with vertex buffers from `[[buffer(14)]]`. Four
uniform slots per stage keep their value until pushed again (S0 `:294-307`). There are no push
constants: uniform slots become root CBVs, dynamic-offset uniform buffers and
`setVertexBuffer:offset:`. "Don't use uniform buffers for large amounts of data (more than a
matrix or so)" (S0 `:166-167`).

**Recommendation.** Keep the layout in `shaders/hlsl/`: vertex `b0, space1` (projection and
modelview, 128 bytes), fragment `t0/s0, space2` and `b0, space3` (`alpha_ref`, `use_texture`).
Push the vertex slot when the transform recipes change, the fragment slot when `alpha_ref` or
textured/untextured changes; the bind cache compares bytes. A 1x1 white texture for untextured
draws would make `use_texture` constant at the price of a texture fetch (M7, #27).

## 5. Format and feature support as capability queries

**What SDL does.** SDL aims at a feature set where "the app won't have to branch itself by
querying feature support" (S0 `:145-148`) and offers queries where hardware differs:

- **Depth**: "Either (but not necessarily both!) D24_UNORM or D32_FLOAT" (S0 `:748`), as in
  Vulkan (V-K8). Apple's feature set tables list Depth32Float and no Depth24Unorm_Stencil8 (V-A3).
  Query: `SDL_GPUTextureSupportsFormat`.
- **MSAA**: `SDL_GPUTextureSupportsSampleCount` (S3 `:1491-1501`). Apple's tables give 4 samples
  up to Apple9 and 8 only on Apple10 (M5), 2 on "Some macOS devices" (V-A3): #35's 8x is a probe
  result, not a given.
- **Anisotropy**: SDL passes `max_anisotropy` unclamped (S4 `:6822`, S5 `:3351`) and has no limit
  query; Vulkan guarantees 16 where anisotropy is supported (V-K9).
- **Anisotropy with nearest filtering**: false on D3D12, so a nearest sampler drops anisotropy
  there. SDL has no query, so the probe decides by driver name (`SDL_GetGPUDeviceDriver`: true
  for "metal" and "vulkan" only). Evidence: the WARP run's `CreateSampler2: Filter unrecognized`.
- **Mipmaps**: `SDL_GenerateMipmapsForGPUTexture` needs SAMPLER and COLOR_TARGET usage and runs
  outside a pass (S1 `:3097-3106`).
- **Present modes**: VSYNC always; Metal adds IMMEDIATE on macOS only, no MAILBOX
  (S3 `:3734-3748`); D3D12 all three (S5 `:6682-6705`); Vulkan what the surface reports.
- **GPU-side HiDPI**: the acquire call fills in the swapchain's width and height
  (`swapchain_texture_width`/`height`, S0 `:4344-4347`). The offscreen target keeps the game's
  resolution; the blit scales it to the swapchain's size, so a Retina or 5K window changes only
  the blit's destination. Window points vs pixels: `sdl-window.md` (#48).

**Recommendation.** A **capability probe** at device creation fills ADR 2's `caps`:

- Depth: D32_FLOAT preferred, since the game runs on 32-bit float depth and 16-bit breaks 23 of
  47 frames (spike, #28). On a D24-only device fall back to GL with the reason logged, until M5
  shows D24 meets R4's FLIP gate; this rule is an inference, not a measurement.
- Supported sample counts for #35; IMMEDIATE support; anisotropy `min(requested, 16)`.
- HDR compositions stay unused: the game's output is SDR.

## 6. Presentation

**What SDL does.** Frames in flight default to 2, range 1-3, and "calling this function will
stall and flush the command queue" (S0 `:4201-4213`). `SDL_WaitAndAcquireGPUSwapchainTexture`
blocks until an image is free and can return NULL, "for example if the window is minimized"
(S0 `:4328-4330`). Metal takes the drawable with `nextDrawable` in the acquire (S3 `:3936`),
which "waits up to one second" when none is free (V-A1). D3D12 presents FLIP_DISCARD with
sync interval 0 and `DXGI_PRESENT_ALLOW_TEARING` for IMMEDIATE (S5 `:7033-7046`,
`:8108-8125`). SDL has no latency control yet: opt-in low-latency presentation is planned for
3.6.0 and may slip to 3.8 (SDL #15656, V-S4). On macOS, IMMEDIATE with a high-pixel-density
window on a 120 Hz display in native fullscreen crashes and reboots macOS (SDL #12715, open,
V-S7); the owner's M1 Max display is ProMotion.

**Recommendation.** Set frames in flight once at device creation (keep 2). Acquire the
swapchain at `present`, after the frame is recorded, so the blocking wait and Metal's drawable
hold are short ("Always acquire a drawable as late as possible", archived 2017 guide, V-A1).
On a NULL swapchain texture skip the blit and still submit. Present with VSYNC; offer IMMEDIATE
where the probe allows, but on macOS only as an explicit opt-in until #48/#50 have tested it
on the ProMotion display (M8). Pacing and the frame limiter: `sdl-window.md`. Revisit latency
when SDL 3.6.0 ships #15656.

## 7. Per-backend performance measurement (R11)

**What SDL does.** No timestamp or occlusion queries in 3.4.18; a query API is in review for
3.6.0 (SDL #11696, PR #15651, V-S3). Debug groups and labels exist (`SDL_PushGPUDebugGroup`;
D3D12 needs `WinPixEventRuntime.dll`, S0 `:2985-3030`), which #53 wants in captures.

**Recommendation.**

- **CPU time, every backend**: `-dbg-perf` around the adapter's frame (record, encode, submit),
  with the acquire wait reported apart.
- **GPU time, Metal**: Instruments' Metal System Trace, recordable with `xctrace record`
  (V-A6; documented only in `man xctrace`), plus `MTL_HUD_ENABLED=1 MTL_HUD_LOG_ENABLED=1` (V-A6).
  Apple's GL runs on Metal (spike FINDINGS), so one trace may show both renderers (M4). A
  tool-free bound everywhere: time from `SDL_SubmitGPUCommandBufferAndAcquireFence` until
  `SDL_QueryGPUFence` turns true.
- **Vulkan in the container**: lavapipe is a software renderer (S0 `:2342-2343`); use it for
  counts and RenderDoc captures (V-N3; untested on lavapipe), not for GPU time.
- **D3D12**: no Windows machine and PIX ships only for Windows (V-M4). The counting fake (Design)
  counts SDL calls per frame (draws, pipeline binds, texture binds, uniform pushes, passes,
  upload bytes). The counts are identical on every backend, so for D3D12 they are a proxy only:
  its cost per call stays unmeasured.
- Revisit at SDL 3.6.0: its query API would time all three backends.

## 8. Device choice and driver variance

**What SDL does.** Backends are tried as Metal, D3D12, Vulkan (S1 `:314-327`). The
`SDL_GPU_DRIVER` hint is read before the name property (S1 `:610-613`), so a user's environment
beats the game's option. Debug mode defaults on:
`SDL_GetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN, true)` (S1 `:724`).
Vulkan ranks devices by type, then device-local memory, fails on a non-conformant best device
(S4 `:12100-12245`), and accepts software drivers unless
`SDL_PROP_GPU_DEVICE_CREATE_VULKAN_REQUIRE_HARDWARE_ACCELERATION_BOOLEAN` is set, which SDL
suggests "if you can provide your own fallback renderer (for example, an OpenGL renderer)"
(S0 `:2341-2346`). `preferLowPower` matters only with two GPUs (S3 `:4543-4560`,
S5 `:9122-9125`).

**Recommendation.** Create the device through properties with debug mode set explicitly: on in
tests and `gpu-validate.sh` (R6), off in the game. Pass the driver option as the name property
and log the chosen backend and `SDL_GetGPUDeviceProperties`' names (#50). The game requires
hardware Vulkan and falls back to GL (#52) when creation or the probe fails; container tests
allow lavapipe. Leave `preferLowPower` false.

## 9. Backend-only properties and their defaults

**What SDL does.** A property scoped to one backend is ignored by the other two, so a wrong
default stays invisible on Metal and lavapipe and shows only where that backend runs: for D3D12,
the WARP run on GitHub, one bug per run. S0 lists every such property in two places:
`SDL_CreateGPUDeviceWithProperties` ("With the D3D12 backend", "With the Vulkan backend", "With
the Metal backend", S0 `:2311-2362`) and `SDL_CreateGPUTexture` ("(Direct3D 12 only)",
S0 `:2801-2818`). The complete set at 3.4.18:

| Property (S0 line) | Default | The adapter sets |
|---|---|---|
| `TEXTURE_CREATE_D3D12_CLEAR_R/G/B/A_FLOAT` (`:2801-2812`) | 0 | the colour target's clear colour, from the constant its load op clears to |
| `TEXTURE_CREATE_D3D12_CLEAR_DEPTH_FLOAT` (`:2813`) | 0 | the depth clear value (1.0), from the constant its load op clears to (#28) |
| `TEXTURE_CREATE_D3D12_CLEAR_STENCIL_NUMBER` (`:2816`) | 0 | default: no stencil is cleared |
| `DEVICE_CREATE_D3D12_SEMANTIC_NAME_STRING` (`:2313`) | "TEXCOORD" | default: `shaders/hlsl/` uses `TEXCOORDn` |
| `DEVICE_CREATE_D3D12_ALLOW_FEWER_RESOURCE_SLOTS_BOOLEAN` (`:2315`) | false | default (Tier 2 hardware only) |
| `DEVICE_CREATE_D3D12_AGILITY_SDK_*` (`:2323-2338`) | unset | default |
| `DEVICE_CREATE_VULKAN_REQUIRE_HARDWARE_ACCELERATION_BOOLEAN` (`:2341`) | false | §8: true in the game, false in container tests |
| `DEVICE_CREATE_VULKAN_OPTIONS_POINTER` (`:2347`) | none | default |
| `DEVICE_CREATE_METAL_ALLOW_MACFAMILY1_BOOLEAN` (`:2355`) | false | default |

The clear-value rule: D3D12 fixes a target's optimized clear value at creation; a load-op
clear to any other value still clears correctly, slower, and the debug layer logs
`ClearDepthStencilView` (or `ClearRenderTargetView`): "The clear values do not match those
passed to resource creation" (EXECUTION WARNING #821 for depth). The WARP run fails on it
(`d3d12-run-40591444825/`, every frame, depth at 1.0 against SDL's 0).

**Recommendation.** Every texture created as a colour or depth target sets its D3D12 clear
properties from the same constant its load op clears to; a clear value that changes at run time
(the game's clear colour) re-creates nothing and costs the warning only when it differs. A test
pins "creation clear value equals load-op clear value" per target, so the mismatch fails locally
instead of on WARP. On an SDL pin bump, re-read both S0 lists and add any new property here.

## Design: the SDL_GPU adapter as a deep module

The adapter keeps ADR 2's interface: `draw`, `texture`, `clear`, `present`, `read_pixels`,
`caps`. Callers get every finding above as **leverage** without learning it, and backend
knowledge has **locality** in one module; deleting the adapter would scatter these rules into
the front end. Backend differences enter only as values from the capability probe: no part of
the adapter branches on a backend's name.

| Part | Owns | Interface | Tested by |
|---|---|---|---|
| Frame record | one frame's draws, clears and uploads, in order | append draw, upload, clear; take the frame | unit tests on values |
| Transfer ring | mapped transfer buffers, chunk growth, 512-byte packing | reserve bytes → span + offset; finish into a copy pass | unit tests |
| Pass planner | pass boundaries and load/store ops (area 3) | frame record → list of passes | pure function, unit tests |
| Pipeline cache | the closed pipeline set, built at creation | key → pipeline | resource counter (#24) |
| Bind cache | last pipeline, viewport, texture, uniform bytes | bind calls that report whether SDL was called | unit tests, call counts |
| Capability probe | depth format, sample counts, present modes, anisotropy | device → caps | lavapipe and Metal tests |
| **GPU command seam** | the SDL_GPU calls | the dozen SDL_GPU calls the adapter uses (begin/end pass, bind pipeline, set viewport, bind vertex buffer, bind sampler, push uniforms, draw, upload, blit, download), not a mirror of `SDL_gpu.h` | two adapters: SDL_GPU and a counting fake |

Only the GPU command seam has two adapters, so only it is a real seam; the other parts are
internal modules tested through their own small interfaces. The counting fake lets CI assert
SDL call counts per frame without a GPU. A batcher (area 1) would sit between frame record and
encoder and is built only if M1 asks for it.

## Measurement questions

These need a measurement, not a source.

| ID | Question | Ticket |
|---|---|---|
| M1 | Adapter CPU time vs GL at 250-500 draws on Metal: is neighbour merging needed for R11? How long are runs of equal state? | #53 (counts: #26-#28) |
| M2 | Pass split (colour and depth LOAD) vs depth-reset draw on Apple silicon at 1080p-5K | #31, #53 |
| M3 | How often a texture slot is re-uploaded after being drawn in the same frame | #27, #36 |
| M4 | Does Metal System Trace show Apple GL's GPU work, so GL and SDL_GPU compare in one tool? | #53 |
| M5 | Does D24_UNORM depth meet R4's FLIP gate? | #28, #52 |
| M6 | Startup time of building the pipeline set from MSL source on Metal; is an offline metallib worth it? | #24, #26 |
| M7 | 1x1 white texture vs `use_texture` uniform for untextured draws | #27 |
| M8 | Frames in flight 2 vs 3, VSYNC vs IMMEDIATE: latency and frame-time variance with the game's limiter. A D3D12 measurement in SDL #15656 found `allowedFramesInFlight=1` left median render-present latency at 9.60 ms vs 9.55 ms default (V-S4). Test IMMEDIATE on macOS on the 120 Hz ProMotion display in native fullscreen with HiDPI (SDL #12715) before offering it | #48, #50, #53 |
| M9 | Offscreen target + blit vs drawing into the swapchain: frame time at 5K | #48, #53 |
