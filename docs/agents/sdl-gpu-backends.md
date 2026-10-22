# Driving Metal, Vulkan and D3D12 through SDL_GPU

What SDL_GPU 3.4.18 does per backend, which decisions it leaves to the SDL_GPU adapter of the
render device (ADR 2), and what the adapter should do with each, for speed (R11) and for a
codebase that stays small. Read it before designing, implementing or reviewing the SDL_GPU
adapter or any ticket listed under an area. Correctness (headless readback, shader formats, no Y
flip on Metal, the exact transform model) was settled by spike #55 and is not repeated here.

Every claim carries a source ID: `S*` is SDL source at tag `release-3.4.18`, cited as
`file:line` and pinned in `tools/dev/reference-docs.txt` (run `tools/dev/fetch-docs.sh`, then
read under `docs/reference/sdl3/`); `V*` is a vendor or Khronos source. Both are listed under
[Sources](#sources). Researched 2026-10-22 (#71).

## The frame in one picture

The game's numbers set the scale: spike frames hold 247-507 draw calls, 4-5 pipelines, 2
samplers and 20-39 textures each, about 95 % fans of about 4 vertices (spike FINDINGS, T3/T5).
That is few draws for any of the three APIs; what costs is how SDL turns each draw into binds.
ADR 2 already lets the adapter collect a frame and submit at `present`. The findings below all
fit one frame shape:

```
draw/texture/clear calls ──► frame record (CPU): vertices into the mapped transfer buffer,
                             texture pixels into the same buffer, draws as small records
present ──► copy pass: vertex upload + texture uploads (one pass, all cycle=true)
        ──► render pass into the offscreen colour+depth target (one, split only where a
            clear or a re-upload forces it)
        ──► acquire swapchain late, blit offscreen → swapchain, submit
```

| # | Area | Recommendation in one line | Tickets |
|---|---|---|---|
| 1 | Draw volume | Deduplicate every bind SDL does not (pipeline, viewport, texture, uniforms); bind the frame's vertex buffer once and draw with `first_vertex`; merge runs of identical state only if #53 shows CPU time over GL | #26, #27, #28, #53 |
| 2 | Uploads | One upload transfer buffer and one vertex buffer per frame, both written with `cycle=true`; all uploads in one copy pass before the render pass; movie texture uploaded with `cycle=true` into one texture | #24, #27, #36 |
| 3 | Passes, pipelines | One render pass per frame into an offscreen target; mid-frame depth clears by a new pass first, a depth-reset draw only if measured slower; create the closed set of pipelines at device creation | #24, #26, #30, #31 |
| 4 | Shader binding | Keep the SDL register layout already in `shaders/hlsl/`; two uniform slots (vertex transforms, fragment alpha state), pushed only on change | #26, #27, #28 |
| 5 | Capabilities | Probe at device creation: D32_FLOAT depth, sample counts, present modes, anisotropy clamped to 16; no branch on the backend's name | #28, #34, #35, #48, #52 |
| 6 | Pacing | Frames in flight 2 (set once), `SDL_WaitAndAcquireGPUSwapchainTexture` at present, VSYNC or IMMEDIATE from the game's vsync option, the game's limiter stays the pacer | #50 |
| 7 | Measurement | CPU time in-game on every backend; GPU time on Metal from Instruments; the adapter's own bind counters for D3D12, which nobody here can time | #53 |
| 8 | Device choice | SDL's order (Metal, D3D12, Vulkan), overridable by option or `SDL_GPU_DRIVER`; debug mode off outside tests; refuse software Vulkan in the game and fall back to GL | #50, #52 |

## 1. Draw-call volume and batching

**What SDL does.** Each `SDL_DrawGPUPrimitives` checks bindings only in debug mode, then calls
the backend (S1 `SDL_gpu.c:2347-2372`). Every backend tracks dirty bindings and rebinds only what
changed since the last draw: a texture or sampler handle equal to the bound one is skipped
(S3 `SDL_gpu_metal.m:2583-2612`, S4 `SDL_gpu_vulkan.c:7683-7711`, S5 `SDL_gpu_d3d12.c:4917-4945`).

**What it leaves to us.** SDL deduplicates nothing above the backend:

- `SDL_BindGPUGraphicsPipeline` always reaches the backend (S1 `SDL_gpu.c:1915-1935`), and every
  backend then marks all bindings dirty: Metal re-sets the pipeline, raster and depth state
  (S3 `:2405-2457`); Vulkan flags new resource and uniform descriptor sets for both stages
  (S4 `:8103-8150`); D3D12 re-sets pipeline state and root signature (S5 `:4753-4795`). Binding
  the same pipeline twice costs a full rebind on the next draw.
- Per-draw cost of a changed binding differs by backend:
  - **Metal**: a `set*` call per dirty slot and an `@autoreleasepool` per draw
    (S3 `:2666-2780`, `:2861-2880`). Cheapest of the three.
  - **Vulkan**: any dirty binding, a uniform push included, rebinds all four descriptor sets with
    `vkCmdBindDescriptorSets`; a changed texture or pipeline first allocates a fresh descriptor
    set from a pool of 128 and writes it with `vkUpdateDescriptorSets` (S4 `:5167-5470`, pool
    size `:72`). A changed vertex-buffer offset also takes this path, since the early return
    checks `needVertexBufferBind` (S4 `:5186-5196`).
  - **D3D12**: a changed texture or sampler copies descriptors into the shader-visible heap with
    `CopyDescriptorsSimple` and sets a root descriptor table (S5 `:5045-5080`, `:5083-5130`);
    uniforms are root CBVs (S5 `:5195`). Heaps hold 65 536 view and 2048 sampler descriptors
    before a heap switch (S5 `:122-123`), far above a Descent frame; a heap switch "can incur a
    pipeline flush on some hardware" (V-M3), so staying inside one heap matters.
- Uniform pushes are 256-byte aligned on Metal and D3D12 and `minUniformBufferOffsetAlignment`
  on Vulkan, in 32 KiB pool buffers (S3 `:2968-3040`, S5 `:4671-4750`, S4 `:7803-7885`,
  S2 `SDL_sysgpu.h:35`). A push is cheap, but on Vulkan it costs a descriptor rebind (above).
- SDL's own advice: binding a pipeline "hundreds of times when you don't need to will slow the
  performance significantly" (S0 `SDL_gpu.h:160-162`).

**Recommendation.**

1. A **bind cache** in the adapter: remember the bound pipeline, viewport, texture+sampler and
   the bytes last pushed per uniform slot; call SDL only on change. This removes most of the
   per-draw work on Vulkan and D3D12 without reordering anything.
2. Write the frame's vertices into one vertex buffer, bind it once at offset 0 and pass each
   draw's start as `first_vertex`; never rebind with a new offset per draw (Vulkan path above).
3. Do not reorder draws: the game draws back to front with blending on every draw (spike T3), so
   sorting by texture or pipeline changes the image. Batching is limited to **merging adjacent
   draws** whose pipeline, texture, viewport and uniforms are all equal, fans turned into one
   triangle list (`fan_to_list` already emits lists). Build it only if #53 measures SDL_GPU CPU
   time above GL; 250-500 draws per frame may not need it (measurement question M1).
4. Instancing and indirect draws do not fit: every fan has its own vertices and the game has no
   repeated meshes.

Vendor guidance points the same way. Arm: "For Vulkan, aim for fewer than 1000 draw calls per
frame" and "The ﬁrst draw call, after a descriptor changes, has a higher CPU overhead" (V-K7).
Khronos: calling `vkUpdateDescriptorSets` for each draw call can take "longer than the time of the
draws themselves" (V-K2). AMD: "Sort draw calls by pipeline" and "Avoid copying descriptors"
(V-N2); the second is what SDL's D3D12 backend does on every texture change, and sorting is ruled
out for us, so deduplication is the lever left. Apple's only current page on per-draw cost says
binding resources one by one makes the driver "capture state and track residency" (V-A7); its
general batching advice survives only in the archived 2017 guide.

## 2. Upload strategy and cycling

**What SDL does.** Buffers, transfer buffers and textures are containers of internal resources.
With `cycle=true`, a write to a resource still referenced by an unfinished command buffer moves
to a free internal resource, creating one if none is free; with `cycle=false` the write goes to
the bound one (S0 `SDL_gpu.h:309-353`; Metal S3 `:1547-1578`, `:1710-1757`; Vulkan
S4 `:6065-6110`). In steady state a container written once per frame holds about one resource
per frame in flight plus one (inferred from the cycling code, not measured). Cycling
a texture makes the whole texture undefined (S0 `:345-347`). Transfer buffers on Metal are
write-combined, CPU-visible buffers (S3 `:1611-1650`, `:1671-1687`); GPU buffers and textures are
private.

Per upload: Metal encodes one blit copy (S3 `:1799-1830`). Vulkan brackets each copy with two
pipeline barriers, into transfer-destination layout and back to the default graphics-read layout,
because barriers cannot be issued inside a render pass (S4 `:2550-2575`, `:8869-8925`). D3D12
needs 256-byte row pitch and 512-byte offsets unless `UnrestrictedBufferTextureCopyPitchSupported`
(D3D12 options 13) is present; otherwise SDL copies through a temporary buffer
(S5 `:6018-6060`, `:9387-9396`).

**What it leaves to us.** When to upload, how to pack the transfer buffer, and whether to cycle.

**Recommendation.**

1. A **transfer ring**: one upload transfer buffer mapped with `cycle=true` once per frame;
   `draw` appends vertices and `texture` appends pixels into it as the calls arrive (ADR 2's
   views are valid only during the call, so this copy happens anyway). At `present`, unmap and
   record one copy pass: the vertex upload (`cycle=true`) and every texture upload, then the
   render pass. SDL's advice: "Perform your data uploads as early as possible in the frame"
   (S0 `:163`).
2. Grow by chunks: when a frame outgrows the buffer, add a second transfer buffer and vertex
   buffer rather than reallocating mid-frame; keep the high-water mark for the next frame.
3. Pack each texture's pixels tightly (`pixels_per_row = 0`) at a 512-byte aligned offset.
   Metal ignores `pixels_per_row` in 3.4.18 (open SDL issue #16474, V-S5; S3 `:1814-1816` passes
   `BytesPerRow(destination->w)`), so padded rows would break there. Power-of-two textures of
   width 64 and up already have 256-byte rows; narrower ones cost D3D12 a realignment copy
   unless options 13 is present.
4. Texture slots re-uploaded with new pixels: upload with `cycle=true`, so a draw already
   recorded keeps the old contents. If a slot is re-uploaded after a draw in the same frame used
   it, end the render pass, upload, and start a new one with `LOAD` (rare; measurement M3).
5. **Movies (#36)**: one persistent texture of the movie size, uploaded with `cycle=true` every
   new frame from the ring. No texture creation per frame; SDL keeps 2-3 internal textures.
6. Readback (`read_pixels`) downloads from the offscreen target (the swapchain texture is
   write-only, S0 `:4337-4338`) and waits on a fence; a stall is acceptable for screenshots and
   thumbnails.

## 3. Render passes and pipeline switches

**What SDL does.** A render pass is begun with its targets and load/store ops; "Beginning a new
render pass is relatively expensive. Use as few render passes as you can." (S0 `:158-159`). All
render state resets at the end of a pass (S0 `SDL_gpu.h:86-88`). Vulkan looks up a cached
`VkRenderPass` and framebuffer per begin (S4 `:7998-8060`) and uses render pass objects, not
dynamic rendering (S4 `:6239`). Pipelines are created synchronously with no pipeline cache:
`VK_NULL_HANDLE` cache on Vulkan (S4 `:6764`), empty `CachedPSO` on D3D12 (S5 `:2994-2995`);
Metal compiles MSL source with `newLibraryWithSource` at shader creation (S3 `:864`). There is
no clear inside a pass: a clear is a load op.

**What it leaves to us.** Pass count, load/store ops, when pipelines are built.

**Recommendation.**

1. Render the frame into an **offscreen colour + depth target** at the game's resolution, begun
   once with colour `CLEAR` (the game clears to black after every swap, spike T5) and depth
   `CLEAR`, colour `STORE`, depth `DONT_CARE` at the end of the frame. Then blit to the
   swapchain (`SDL_BlitGPUTexture`). The offscreen target also serves `read_pixels`, HiDPI
   scaling (#48) and headless traces with one code path. Begin it with `cycle=true` to break the
   frame-to-frame dependency, as SDL's own example says (S0 `:337-339`).
2. Mid-frame depth clears (3D view, rear-view, #31): start a new pass with colour `LOAD` and
   depth `CLEAR`. On a tile-based GPU (Apple silicon) a `LOAD` reads the colour back into tile
   memory: Apple says a load "is significantly slower than MTLLoadAction.dontCare or
   MTLLoadAction.clear" (V-A2). If #53 shows the extra passes costing, replace them with a
   **depth-reset draw** inside the pass: a quad over the viewport, depth compare `ALWAYS`,
   colour writes off.
   It needs one internal pipeline outside the `draw_state` set. Measurement M2 decides.
3. **Pipeline cache** (#26): the key is (blend, depth, cull) from `draw_state` (5 x 2 x 2 = 20)
   plus the target's colour format, depth format and sample count. `alpha_ref`, `wrap` and
   `line_width` are not pipeline state (uniform, sampler, CPU line expansion). Create all 20 at
   device creation (#24 asks for this already): no first-use stall, so no disk pipeline cache is
   needed on Vulkan or D3D12. Rebuild the set when MSAA or the depth format changes.
4. Pipeline switches happen in game order; the bind cache (area 1) drops the redundant ones.
5. Keep `discard` out of opaque draws (#29's `has_transparency`): on Apple GPUs, fragments from a
   shader with a discard statement are "feedback fragments", and these and blending "reduce
   hidden surface removal efficiency" (V-A2).

## 4. Shader binding conventions per backend

**What SDL does.** SDL prescribes the layout per format (S0 `SDL_gpu.h:2711-2752`): SPIR-V sets
0/1 (vertex resources, vertex uniforms) and 2/3 (fragment); DXIL `space0`/`space1` for vertex and
`space2`/`space3` for pixel shaders; MSL `[[texture]]`, `[[sampler]]`, `[[buffer]]` with vertex
buffers from `[[buffer(14)]]`. Shadercross produces all three from one HLSL source following the
DXIL layout. Up to 4 uniform slots per stage, each keeping its value for the rest of the command
buffer once pushed (S0 `:294-307`). SDL_GPU has no push constants: uniforms always go through
these slots, which become root CBVs on D3D12, dynamic-offset uniform buffers on Vulkan and
`setVertexBuffer:offset:` on Metal (area 1). "Don't use uniform buffers for large amounts of data
(more than a matrix or so)" (S0 `:166-167`).

**What it leaves to us.** How to split uniforms and how often to push them.

**Recommendation.** Keep the layout already in `shaders/hlsl/` (vertex `b0, space1`: projection
and modelview, 128 bytes; fragment `t0/s0, space2` and `b0, space3`: `alpha_ref`,
`use_texture`). Push the vertex slot only when the transform recipes change (once per object or
view, not per polygon) and the fragment slot only when `alpha_ref` or textured/untextured
changes; the bind cache compares bytes. Binding a 1x1 white texture for untextured draws (as the
spike did) would make `use_texture` constant and drop one class of fragment pushes; decide in
#27 by measurement, since it adds a texture fetch to every untextured fragment.

## 5. Format and feature support as capability queries

**What SDL does.** "The GPU API targets a feature set with a wide range of hardware support ...
It is designed so that the app won't have to branch itself by querying feature support"
(S0 `:145-148`), with a few queries where hardware differs:

- **Depth**: only D16_UNORM and *either* D24_UNORM *or* D32_FLOAT are guaranteed
  (S0 `:744-754`), matching Vulkan's rule (V-K8 `formats.adoc:3789-3792`). Apple silicon has no
  guaranteed Depth24Unorm_Stencil8: Apple's feature set tables list only Depth16Unorm,
  Depth32Float and the stencil formats (V-A3), and SDL asks `isDepth24Stencil8PixelFormatSupported`
  (S3 `:4270-4273`). Query with `SDL_GPUTextureSupportsFormat`.
- **MSAA**: `SDL_GPUTextureSupportsSampleCount` per format (Metal: `supportsTextureSampleCount`,
  S3 `:1491-1501`). Apple's tables give a render-pass maximum of 4 samples up to Apple9 and 8
  only on Apple10, and list 2x for "Some macOS devices" (V-A3), so the M1 Max offers 4x and maybe
  2x: #35's 8x is a probe result, not a given.
- **Anisotropy**: `enable_anisotropy` and `max_anisotropy` per sampler; SDL does not clamp it to
  the device limit (S4 `:6822`, S5 `:3351`) and exposes no limit query. Vulkan guarantees 16
  where anisotropy is supported (V-K9 `limits.adoc:6979`). Upper limits on D3D12 and Metal were
  not sourced here; clamping to 16 stays within the Vulkan guarantee.
- **Mipmaps**: `SDL_GenerateMipmapsForGPUTexture` needs SAMPLER and COLOR_TARGET usage and
  runs outside a pass (S1 `SDL_gpu.c:3097-3106`).
- **Present modes, HDR**: `SDL_WindowSupportsGPUPresentMode` and
  `SDL_WindowSupportsGPUSwapchainComposition`; VSYNC + SDR always work (S0 `:1313-1330`,
  `:4177-4178`). Metal offers IMMEDIATE (macOS only) but no MAILBOX (S3 `:3734-3748`); D3D12
  offers all three (S5 `:6682-6705`); Vulkan whatever the surface reports (S4 `:86-88`).
- **HiDPI**: the swapchain size comes back from the acquire call in pixels; `README-highdpi.md`
  (pinned) covers window points vs pixels.

**Recommendation.** A **capability probe** run once at device creation fills ADR 2's `caps`:
depth format (D32_FLOAT required; #28 found 16-bit breaks 23/47 frames and 24-bit is untested,
measurement M5), supported sample counts for #35, IMMEDIATE support, anisotropy fixed at
`min(requested, 16)`. Without D32_FLOAT the device is unusable and #52 falls back to GL with the
reason logged. No code path names a backend; backend differences enter only through the probe.
HDR compositions stay unused: the game's output is SDR.

## 6. Frame pacing and presentation

**What SDL does.** Frames in flight default to 2, range 1-3; changing it stalls and flushes the
queue (S0 `:4201-4213`, S2 `SDL_sysgpu.h:40`). `SDL_WaitAndAcquireGPUSwapchainTexture` blocks
until a swapchain image is free and can return NULL (minimised window), which is not an error
(S0 `SDL_gpu.h:4328-4331`). Present mode maps to `displaySyncEnabled` on Metal
(S3 `:3681`), to sync interval 1 or 0 and `DXGI_PRESENT_ALLOW_TEARING` for IMMEDIATE on a
FLIP_DISCARD swapchain on D3D12 (S5 `:7033-7046`, `:8108-8125`; V-M2), to the Vulkan present
mode on Vulkan. Metal takes the drawable with `nextDrawable` inside the acquire (S3 `:3936`),
which blocks while none is free: "If all drawables are in use, the layer waits up to one second
for one to become available, after which it returns nil" (V-A1). SDL's maintainers declined
deeper latency control for now (SDL issue #15656, V-S4).

**What it leaves to us.** When to acquire, which present mode, who paces.

**Recommendation.** Set frames in flight once at device creation (keep 2). Acquire the swapchain
texture at `present`, after the frame is recorded, so the blocking wait (and Metal's drawable
hold) is as short as possible. Apple's only source for this is the archived 2017 guide: "Always
acquire a drawable as late as possible" (V-A1). Skip the blit on a NULL swapchain texture but
still submit.
Map the game's vsync option to VSYNC or, where the probe allows, IMMEDIATE; the game's own frame
limiter stays the pacer, as with GL. Count time spent inside the acquire separately in `-dbg-perf`:
it is GPU or display wait, not adapter CPU time.

## 7. Per-backend performance measurement (R11)

**What SDL does.** No timestamp or occlusion queries in 3.4.18. A query API is in review (PR
#15651, SDL issue #11696, milestone 3.6.0, V-S3). SDL offers debug groups and labels
(`SDL_PushGPUDebugGroup`, `SDL_InsertGPUDebugLabel`; D3D12 needs `WinPixEventRuntime.dll`,
S0 `:2985-3030`), which #53 asks to see in captures.

**What it leaves to us.** Everything.

**Recommendation.**

- **CPU time, every backend**: the in-game `-dbg-perf` timer around the adapter's frame
  (recording, encoding, submit) with the acquire wait reported apart (area 6).
- **GPU time, Metal on the M1 Max**: Instruments' Metal System Trace, recordable from the command
  line with `xctrace record` (V-A6; documented only in the local `man xctrace`, the template name
  unverified), plus `MTL_HUD_ENABLED=1 MTL_HUD_LOG_ENABLED=1` for per-frame statistics and
  `gpuStartTime`/`gpuEndTime` of a command buffer (V-A6), which SDL does not expose. Apple's GL
  runs on Metal (spike FINDINGS insight 1), so the same trace should show GL's GPU work for the
  comparison R11 needs (measurement M4). A coarse,
  tool-free bound on every backend: submit with `SDL_SubmitGPUCommandBufferAndAcquireFence` and
  time until `SDL_QueryGPUFence` turns true.
- **Vulkan in the Linux container**: lavapipe is a software renderer (S0 `SDL_gpu.h:2342-2343`),
  so its times say nothing about GPUs. Use it for structural counts (RenderDoc captures Vulkan
  1.4 on Linux, V-N3; no source says it works on lavapipe, so try it first) and for SDL's CPU
  overhead only.
- **D3D12**: no Windows machine. PIX needs Windows (V-M4); CrossOver timings measure the
  translation layer. Compare D3D12 only through the adapter's **frame counters** (draws, SDL draw
  calls, pipeline binds, texture binds, uniform pushes, passes, upload bytes), which are the same
  on every backend and testable in CI.
- Revisit when SDL 3.6.0 ships the query API: GPU timestamps through SDL would cover all three
  (on Vulkan they are ticks scaled by `timestampPeriod`, V-K10).

## 8. Device choice and driver variance

**What SDL does.** Backends are tried in the order Metal, D3D12, Vulkan (S1 `SDL_gpu.c:314-327`);
`SDL_HINT_GPU_DRIVER` (`SDL_GPU_DRIVER`) wins over the `SDL_PROP_GPU_DEVICE_CREATE_NAME_STRING`
property (S1 `:610-613`), so a user's environment overrides the game's option. Debug mode
defaults to **true** when created through properties (S1 `:724`). Vulkan ranks physical devices
by type (discrete first, or integrated first with `preferLowPower`), then by device-local memory,
and fails if the best device is non-conformant (S4 `:12100-12245`); it accepts software drivers
such as lavapipe unless `SDL_PROP_GPU_DEVICE_CREATE_VULKAN_REQUIRE_HARDWARE_ACCELERATION_BOOLEAN`
is set, which SDL suggests when the app has its own fallback such as OpenGL (S0
`SDL_gpu.h:2341-2346`). D3D12 uses `EnumAdapterByGpuPreference` (S5 `:9122-9125`); Metal picks a
low-power device only on macOS with `preferLowPower`, irrelevant on Apple silicon's single GPU
(S3 `:4543-4560`). `SDL_GPUSupportsProperties` tests a configuration without creating a device.

**Recommendation.** Create the device through properties, always setting debug mode explicitly:
on in tests and `gpu-validate.sh` runs (R6), off in the game. Pass the game's driver option as
the name property and log the backend chosen and `SDL_GetGPUDeviceProperties`' device and driver
names (#50). In the game, require hardware Vulkan and fall back to GL (#52) when creation or the
capability probe fails; tests in the container set the property false to reach lavapipe. Leave
`preferLowPower` false.

## Design: the SDL_GPU adapter as a deep module

The adapter keeps ADR 2's interface unchanged: `draw`, `texture`, `clear`, `present`,
`read_pixels`, `caps`. Everything above sits behind it, so callers get every finding (bind
deduplication, transfer packing, pass planning) as **leverage** without learning any of it, and
backend knowledge has **locality** in one module. The deletion test passes: without the adapter
each of these rules would reappear in the front end.

Inside, the findings call for these parts. Only one of them is a **seam** with two adapters; the
rest are internal modules tested through the adapter's interface or their own small one:

| Part | Owns | Interface (small) | Tested by |
|---|---|---|---|
| Frame record | the draws, clears and uploads of one frame, in order | `append(draw)`, `append(upload)`, `clear`, `take()` | unit tests on values |
| Transfer ring | mapped transfer buffers, chunk growth, 512-byte packing | `reserve(bytes) → span + offset`, `finish(copy pass)` | unit tests with a fake allocator |
| Pass planner | where passes start and end (frame start, depth clear, mid-frame re-upload), load/store ops | `plan(frame record) → passes` | pure function, unit tests |
| Pipeline cache | the closed set of pipelines, built at creation | `get(blend, depth, cull) → pipeline` | resource counter (#24) |
| Bind cache | last pipeline, viewport, texture, uniform bytes | `bind_*` that returns whether SDL was called | unit tests, frame counters |
| Capability probe | depth format, sample counts, present modes, anisotropy limit | `probe(device) → caps` | tests on lavapipe and Metal |
| **GPU command seam** | the SDL_GPU calls themselves | the encoder the planner drives | two adapters: SDL_GPU, and a **counting fake** that records calls |

The **GPU command seam** is real because two adapters sit behind it: the SDL_GPU encoder and a
counting fake. The fake lets CI assert the frame counters (pipeline binds per frame, passes per
frame, uploads per frame) on every backend without a GPU, which is how D3D12 gets any performance
coverage (area 7). The batcher of area 1 is not a part yet: it becomes a function between frame
record and encoder only if M1 asks for it, and the frame record already gives it the input it
needs.

Backend differences enter only as values from the capability probe; no part branches on the
backend's name.

## SDL version

3.4.18 (2026-10-02) is the latest SDL release on 2026-10-22 (V-S1). After it:

- **3.4.20** (milestone due 2026-10-31, V-S2): Vulkan fixes for a never-created `defragLock`
  (upload vs memory defragmentation across threads), a double `vkDestroyImage` on a failed
  memory bind, an acquire that blocks while the display is off; a `first_binding` range check in
  `SDL_BindGPUVertexBuffers`. Open in that milestone: Metal ignoring `pixels_per_row` (#16474).
  No API change.
- **3.6.0** (no date): GPU query API with timestamps (#11696/#15651), `SDL_BeginGPURenderPass2`
  (#15994).
- Nothing in SDL's GPU commits since 3.4.18 changes a recommendation above
  (`release-3.4.x` and `main`, `src/gpu`, through 2026-10-18; V-S6).

**Recommendation**: keep the pin at 3.4.18 now; move to 3.4.20 when it is released (a patch
release; the Vulkan defrag race matters only with uploads from several threads, which we do not
do). Plan R11's GPU timing around 3.6.0's query API once it is released.

## Currency: what SDL 3.4.18 uses against current vendor guidance

SDL's backends use the older form of several APIs. Each is still valid; none changes a
recommendation above, because the adapter cannot reach below SDL.

| SDL uses | Current generation | Still applies? |
|---|---|---|
| Vulkan `VkRenderPass` objects (S4 `:6239`) | Dynamic rendering; the spec calls render pass objects legacy from 1.4 (V-K1) | Yes: "Legacy functionality remains available for use in the specification" (V-K1) |
| `vkCmdPipelineBarrier` (S4 `:2550-2575`) | Synchronization2, core in 1.3 (V-K4) | Yes: legacy, not removed (V-K4) |
| Descriptor sets with dynamic uniform buffers (S4 `:5167-5470`) | Push descriptors (core 1.4), `VK_EXT_descriptor_heap` (V-K2, V-K6) | Yes; per-draw descriptor updates remain the anti-pattern the bind cache avoids (V-K2) |
| No `VkPipelineCache` (S4 `:6764`), no D3D12 PSO cache (S5 `:2994-2995`) | `VK_KHR_pipeline_binary` (Roadmap 2026), `ID3D12PipelineLibrary` (V-K3, V-M5) | Yes: we build 20 pipelines at start, so caching across runs saves little (M6) |
| D3D12 legacy `ResourceBarrier` (S5 `:1814-1850`) | Enhanced Barriers, retail since Agility SDK 1.608.0, optional per driver (V-M1) | Yes: "The D3D12 runtime internally translates all ResourceBarrier calls" (V-M1) |
| Classic Metal (`MTLCommandQueue`, render encoders) | Metal 4 (`MTL4*`), opt-in on M1 and later (V-A4) | Yes: classic and Metal 4 queues interoperate; nothing found deprecates the classic API (V-A4) |

Older advice still cited: Apple's archived Metal Best Practices Guide (2017, before Apple
silicon) is the only Apple source for acquire-late and draw batching (V-A1, V-A7); Microsoft's
flip-model and latency pages date from 2017-2018 (V-M2); NVIDIA's posts from 2021-2023 (V-N1);
AMD's RDNA guide from 2023 (V-N2). Each concerns an API SDL still uses as described.

## Measurement questions

These need a measurement, not a source. Each names the ticket that should measure it.

| ID | Question | Ticket |
|---|---|---|
| M1 | Per-frame CPU time of the adapter vs GL at 250-500 draws on Metal: is merging adjacent equal-state draws needed for R11? How long are runs of equal state in real frames? | #53 (counts: #26-#28) |
| M2 | Cost of a mid-frame pass split (colour `LOAD`) vs a depth-reset draw on Apple silicon at 1080p-5K | #31, #53 |
| M3 | How often a texture slot is re-uploaded after being drawn in the same frame (trace texture records between draws) | #27, #36 |
| M4 | Does Instruments' Metal System Trace show Apple GL's GPU work, so GL and SDL_GPU compare in one tool? | #53 |
| M5 | Does D24_UNORM keep the R4 frames, for a device without D32_FLOAT? | #28, #52 |
| M6 | Startup time of creating 20 pipelines from MSL source on Metal; is an offline metallib worth it? | #24, #26 |
| M7 | 1x1 white texture vs `use_texture` uniform for untextured draws: fragment pushes saved vs texture fetches added | #27 |
| M8 | Frames in flight 2 vs 3 and VSYNC vs IMMEDIATE: frame-time variance with the game's limiter | #50, #53 |

## Sources

Every source below was fetched in this research run on 2026-10-22; dates are the ones the source
states. SDL files are pinned in `tools/dev/reference-docs.txt`
(SHA-256 there) at tag `release-3.4.18`, released 2026-10-02.

### SDL (primary, pinned)

| ID | File | Passages relied on |
|---|---|---|
| S0 | `include/SDL3/SDL_gpu.h` | performance list `:154-170`; uniforms `:294-307`; pass state reset `:86-88`; cycling `:309-353`; formats `:675-754`; present modes `:1313-1336`; binding layout `:2706-2752`; debug groups `:2985-3030`; device properties `:2266-2346`; frames in flight `:4201-4213`; VSYNC+SDR always `:4177-4178`; NULL swapchain `:4328-4331`; swapchain write-only `:4337-4338` |
| S1 | `src/gpu/SDL_gpu.c` | backend order `:314-327`; hint before property `:610-613`; debug mode default true `:724`; pipeline bind pass-through `:1915-1935`; draw `:2347-2372`; mipmap checks `:3097-3106` |
| S2 | `src/gpu/SDL_sysgpu.h` | `UNIFORM_BUFFER_SIZE 32768` `:35`; `MAX_FRAMES_IN_FLIGHT 3` `:40` |
| S3 | `src/gpu/metal/SDL_gpu_metal.m` | MSL compile `:864`; sample counts `:1491-1501`; texture cycling `:1547-1578`; buffers `:1611-1757`; upload `:1799-1830`; pipeline bind `:2405-2457`; sampler bind `:2583-2612`; resource bind `:2666-2780`; draw `:2861-2880`; uniforms `:2968-3040`; display sync `:3681`; present modes `:3734-3748`; `nextDrawable` `:3936`; D24 query `:4270-4273`; low power `:4543-4560` |
| S4 | `src/gpu/vulkan/SDL_gpu_vulkan.c` | present modes `:86-88`; descriptor pool `:72`; barrier strategy `:2550-2575`; descriptor bind `:5167-5470`; draw `:5498-5515`; buffer cycling `:6065-6110`; render pass objects `:6239`; dynamic states `:6376-6379`; no pipeline cache `:6764`; anisotropy `:6822`; sampler bind `:7683-7711`; uniforms `:7803-7885`; pass begin `:7998-8060`; pipeline bind `:8103-8150`; upload `:8869-8925`; device rank `:12100-12245` |
| S5 | `src/gpu/d3d12/SDL_gpu_d3d12.c` | feature level `:116`; heap sizes `:122-123`; barriers `:1814-1850`; no PSO cache `:2994-2995`; anisotropy `:3351`; uniforms `:4671-4750`; pipeline bind `:4753-4795`; sampler bind `:4917-4945`; descriptor copies `:5045-5130`; upload alignment `:6018-6060`; present modes `:6682-6705`; swapchain `:7033-7046`; present `:8108-8125`; adapter preference `:9122-9125`; options 13 `:9387-9396` |

### SDL project (web, not pinnable)

| ID | Source | Date | Passage |
|---|---|---|---|
| V-S1 | https://github.com/libsdl-org/SDL/releases/latest (redirects to `/releases/tag/release-3.4.18`); https://api.github.com/repos/libsdl-org/SDL/releases/latest | published 2026-10-02 (page `datetime="2026-10-02T22:43:21Z"`); fetched 2026-10-22 | `"tag_name": "release-3.4.18"`, `"published_at": "2026-10-02T22:43:21Z"` |
| V-S2 | https://github.com/libsdl-org/SDL/milestone (3.4.20; `gh api repos/libsdl-org/SDL/milestones`) | due 2026-10-31; fetched 2026-10-22 | issues #16462, #16544, #16548, #16493 closed; #16474 open |
| V-S3 | https://github.com/libsdl-org/SDL/issues/11696, https://github.com/libsdl-org/SDL/pull/15651 | issue opened 2024-12-24, milestone 3.6.0; comment by SDL_GPU maintainer thatcosmonaut 2026-08-18; fetched 2026-10-22 | "Yeah I decided that the best way forward with this is to add a BeginRenderPass2 call" and "Sorry this is moving slowly but it will absolutely be in by 3.6.0 at the latest." |
| V-S4 | https://github.com/libsdl-org/SDL/issues/15656 | comment by slouken 2026-10-03; fetched 2026-10-22 | "High latency presentation isn’t something we’re planning on adding. I’m pretty sure 3 frames of latency is plenty of time" |
| V-S5 | https://github.com/libsdl-org/SDL/issues/16474 | opened 2026-10-09, open; fetched 2026-10-22 | "`pixels_per_row` and `rows_per_layer` aren't being taken into account in the metal backend and tightly packed images are being assumed" |
| V-S6 | Commit lists: `gh api "repos/libsdl-org/SDL/commits?sha=release-3.4.x&path=src/gpu&since=2026-10-02T00:00:00Z"`, same for `main` | through 2026-10-18 | three commits on `release-3.4.x` after the tag; on `main` only an Xbox frame-interval change in addition |

### Khronos (primary, pinned)

| ID | File | Passage |
|---|---|---|
| V-K8 | Vulkan-Docs `v1.4.365` (2026-10-02) `chapters/formats.adoc:3789-3792` | "supported for at least one of ename:VK_FORMAT_X8_D24_UNORM_PACK32 and ename:VK_FORMAT_D32_SFLOAT" (the sentence opens on the line before: "ename:VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT feature must: be") |
| V-K9 | Vulkan-Docs `v1.4.365` `chapters/limits.adoc:6979` | "\| pname:maxSamplerAnisotropy \| 1 \| 16 \| min" (required-limits table, whitespace collapsed: 1 without and at least 16 with the `samplerAnisotropy` feature) |

### Khronos and DirectX-Specs (primary, pinned)

Pinned in `tools/dev/reference-docs.txt`: Vulkan-Docs `v1.4.365` (tagged 2026-10-02),
Vulkan-Guide `f71c5969` (2026-09-27), Vulkan-Samples `69622af0` (2026-10-22), DirectX-Specs
`f625c91c` (2026-04-16). The files state no date of their own; the dates are the commits'.

| ID | File | Passage |
|---|---|---|
| V-K1 | Vulkan-Docs `appendices/legacy.adoc`; Vulkan-Guide `chapters/deprecated.adoc`; Vulkan-Samples `extensions/dynamic_rendering_local_read/README.adoc` | "applications are advised to make use of flink:vkCmdBeginRendering and flink:vkCmdEndRendering to manage render passes from this API version onward."; "Legacy functionality remains available for use in the specification, but it may: be less capable than newer functionality."; "Deprecated items will still work in current Vulkan implementations." |
| V-K2 | Vulkan-Samples `performance/descriptor_management/README.adoc`, `extensions/descriptor_heap/README.adoc`; Vulkan-Docs `appendices/versions.adoc` | "for example calling https://www.khronos.org/registry/vulkan/specs/latest/man/html/vkUpdateDescriptorSets.html[vkUpdateDescriptorSets()] for each draw call, the time it takes to update descriptors can be longer than the time of the draws themselves."; "This extension is intended to completely replace Vulkan's existing descriptor set mechanism and, if available, supersedes descriptor buffers." |
| V-K3 | Vulkan-Samples `performance/pipeline_cache/README.adoc`, `extensions/pipeline_binary/README.adoc` | "In order to benefit from using a `VkPipelineCache` object, the data recorded during pipeline creation needs to be saved to disk and reused between application runs." |
| V-K4 | Vulkan-Docs `appendices/legacy.adoc`; Vulkan-Guide `chapters/extensions/VK_KHR_synchronization2.adoc` | "Synchronization 2 commands should: be used instead of synchronization 1 commands."; "Promoted to core in Vulkan 1.3" |
| V-K6 | Vulkan-Docs `appendices/roadmap/Roadmap-2024.adoc`, `Roadmap-2026.adoc` | "Vulkan 1.4 is required by this milestone." (2026); required lists name `VK_KHR_dynamic_rendering_local_read` (2024) and `VK_KHR_pipeline_binary` (2026) |
| V-K10 | Vulkan-Docs `chapters/queries.adoc`; Vulkan-Samples `api/timestamp_queries/README.adoc` | "The number of nanoseconds it takes for a timestamp value to be incremented by 1 can: be obtained from sname:VkPhysicalDeviceLimits::pname:timestampPeriod" |
| V-M1 | DirectX-Specs `d3d/D3D12EnhancedBarriers.md` (https://microsoft.github.io/DirectX-Specs/d3d/D3D12EnhancedBarriers.html); Agility SDK downloads https://devblogs.microsoft.com/directx/directx12agility/ (published 2021-04-20, modified 2026-09-23) | "Enhanced Barriers is not currently a hardware or driver requirement."; "The D3D12 runtime internally translates all ResourceBarrier calls to equivalent Enhanced Barriers at the driver interface."; table row "SDK 1.608.0 608 11/18/2022 Enhanced Barriers"; latest retail row "1.619.6 619 9/14/2026" |

### Arm (primary, pinned PDF)

| ID | Source | Date stated | Passage |
|---|---|---|---|
| V-K7 | Arm GPU Best Practices Developer Guide 101897, v3.4 issue 10, https://documentation-service.arm.com/static/67a62b17091bfc3e0a947695 (pinned) | "issued on 2025-01-31"; still the latest per Arm's documentation service | "For Vulkan, aim for fewer than 1000 draw calls per frame."; "The ﬁrst draw call, after a descriptor changes, has a higher CPU overhead than following draw calls that reuse the same descriptor set."; "Do not call vkAllocateDescriptorSets() on a performance critical code path." |

### Web pages (not pinnable; URL, stated date, quote)

All fetched 2026-10-22. Apple documentation pages state no date; read through
`developer.apple.com/tutorials/data/documentation/<path>.json`.

| ID | Source | Date stated | Passage |
|---|---|---|---|
| V-A1 | https://developer.apple.com/documentation/quartzcore/cametallayer/nextdrawable(); archived Metal Best Practices Guide, https://developer.apple.com/library/archive/documentation/3DDrawing/Conceptual/MTLBestPracticesGuide/Drawables.html | none; guide "Updated: 2017-03-27" | "If all drawables are in use, the layer waits up to one second for one to become available, after which it returns nil."; "Always acquire a drawable as late as possible; preferably, immediately before encoding an on-screen render pass." |
| V-A2 | https://developer.apple.com/documentation/metal/setting-load-and-store-actions; WWDC20 10632 https://developer.apple.com/videos/play/wwdc2020/10632/ | none; WWDC 2020-06-24 | "This action is significantly slower than MTLLoadAction.dontCare or MTLLoadAction.clear."; "This is typically the correct action for depth and stencil render targets." (store dontCare); "But some Metal features will also reduce hidden surface removal efficiency."; "Then we have feedback fragments, which are generated by fragment functions containing a discard statement or that return of depth value." |
| V-A3 | Metal Feature Set Tables, https://developer.apple.com/metal/Metal-Feature-Set-Tables.pdf (unversioned URL; sha256 `9f31df15…` on 2026-10-22, will drift); https://developer.apple.com/documentation/metal/mtldevice/supportstexturesamplecount(_:) | footer "May 21, 2026"; none | depth formats table lists Depth16Unorm, Depth32Float, Stencil8, Depth32Float_Stencil8, X32_Stencil8 and no Depth24Unorm_Stencil8; row "Maximum sample count in render passes with MSAA": 4 through Apple9, 8 for Apple10; sample count 2: "All iOS devices All tvOS devices Some macOS devices" |
| V-A4 | WWDC25 205 https://developer.apple.com/videos/play/wwdc2025/205/; https://developer.apple.com/documentation/metal/understanding-the-metal-4-core-api | 2025-06-09; none | "You can incrementally adopt Metal 4 over time, which is convenient for larger projects."; "Events work with any combination of MTLCommandQueue and MTL4CommandQueue instances." |
| V-A6 | https://developer.apple.com/documentation/xcode/monitoring-your-metal-apps-graphics-performance; https://developer.apple.com/documentation/metal/mtlcommandbuffer/gpustarttime; https://developer.apple.com/documentation/xcode/analyzing-the-performance-of-your-metal-app; local `man xctrace` (Xcode 27.1) | none | "Enables logging of per-frame statistics. Requires MTL_HUD_ENABLED=1."; "You can calculate how much time the GPU spends running a command buffer by subtracting this value from gpuEndTime."; "use the Game Performance template in Instruments, which combines threading and system call information with the Metal system trace instrument." |
| V-A7 | https://developer.apple.com/documentation/metal/improving-cpu-performance-by-using-argument-buffers | none | "Encoding resources into argument buffers eliminates the need for the Metal driver to capture state and track residency when it assigns individual resources to the indices of a function’s argument table." |
| V-M2 | https://devblogs.microsoft.com/directx/dxgi-flip-model/; https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/variable-refresh-rate-displays | 2018-04-09; ms.date 2018-05-31 | "Switching to DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL or DXGI_SWAP_EFFECT_FLIP_DISCARD (aka flip model) will give better performance, lower power usage, and provide a richer set of features."; "It is recommended to always pass this tearing flag when using sync interval 0 if CheckFeatureSupport reports that tearing is supported" |
| V-M3 | https://learn.microsoft.com/en-us/windows/win32/direct3d12/root-signature-limits; https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-setdescriptorheaps | ms.date 2018-05-31; 2018-12-05 | "The performance costs (in terms of levels of indirection) are zero for a root constant, 1 for a root descriptor, and 2 for a descriptor table."; "Changing descriptor heaps can incur a pipeline flush on some hardware." |
| V-M4 | https://devblogs.microsoft.com/pix/download/; https://learn.microsoft.com/en-us/windows/win32/direct3d12/timing | modified 2026-06-18; ms.date 2018-05-31 | "Latest main version, 2603.25: Download: ( x64 ) ( Arm64 )" (Windows builds only) |
| V-M5 | https://developer.nvidia.com/blog/advanced-api-performance-pipeline-state-objects/; https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nn-d3d12-id3d12pipelinelibrary | 2023-07-18; ms.date 2018-12-05 | "Avoid runtime PSO compilations as they most likely will lead to stalls. The driver-managed shader disk cache may come to the rescue. Use PSO libraries." |
| V-N1 | https://developer.nvidia.com/blog/advanced-api-performance-descriptors/; https://developer.nvidia.com/blog/advanced-api-performance-barriers/; https://developer.nvidia.com/blog/advanced-api-performance-swap-chains/ | 2023-10-27; 2021-10-22 (modified 2023-10-02); 2023-12-15 | "Use root (DirectX 12) or push (Vulkan) constants. They are the fastest way to transfer per-draw varying constants."; "Don’t insert redundant barriers"; "Use IDXGISwapChain2::SetMaximumFrameLatency(MaxLatency) to set the desired latency, where MaxLatency is some number of frames (counted by queued Present calls)." |
| V-N2 | AMD RDNA Performance Guide, https://gpuopen.com/learn/rdna-performance-guide/ | 2023-03-22 | "Changing the bound PSO may roll the hardware context. Sort draw calls by pipeline."; "Only constants changing every draw should be in the root signature or push constants. Avoid copying descriptors." |
| V-N3 | https://renderdoc.org/; https://renderdoc.org/docs/getting_started/faq.html | "Latest Release: v1.46 - 31 Aug, 2026"; none | "RenderDoc supports Windows, Linux, and Android for capture and replay out of the box."; "Future API support is at this point not clear; Metal, WebGL, and perhaps D3D9/D3D10 all being possible." |

Unreachable or degraded: `developer.arm.com/documentation/101897` renders only with JavaScript (the
PDF was used); `devblogs.microsoft.com/directx/shader-cache/` returns 404; no Apple web page
documents recording Metal System Trace from the command line (only `man xctrace`); Microsoft
documents `UnrestrictedBufferTextureCopyPitchSupported` only as a struct field, its meaning comes
from SDL's source (S5 `:6018-6060`).

## How to extend this research

Not covered: compute passes (the game needs none), multi-threaded command recording (the game is
single-threaded), Android and consoles, HDR output, the SDL_Render GPU renderer (a different
API), and Metal 4 or D3D12 features SDL does not use.

Open questions a source could answer later: whether SDL 3.6.0 moves Vulkan to dynamic rendering
or D3D12 to Enhanced Barriers (check `src/gpu` on `main` against the currency notes above);
the final shape of the query API and `SDL_BeginGPURenderPass2`; whether #16474 is fixed in
3.4.20 (then padded rows become possible on Metal). Checked and absent in 3.4.18: a waitable D3D12 swap chain
(`FRAME_LATENCY_WAITABLE_OBJECT` and `SetMaximumFrameLatency` do not occur in S5), so the
latency control Microsoft and NVIDIA describe (V-M2, V-N1) is out of reach through SDL. Unverified: the exact `xctrace`
template name for Metal System Trace (`xctrace list templates`), and RenderDoc on lavapipe.

To verify a claim: run `tools/dev/fetch-docs.sh` and open the cited `file:line` under
`docs/reference/`; the SHA-256 in the manifest guarantees the same text. To extend to a newer
SDL, add the new tag's files to the manifest next to the old ones and diff `src/gpu`.
Measurement questions are in [Measurement questions](#measurement-questions); answer each in its
ticket and replace the question here with the result.
