# SDL_GPU backends: sources and detail

Companion of `docs/agents/sdl-gpu-backends.md`: the vendor guidance behind its recommendations,
the SDL version detail, how what SDL 3.4.18 uses compares with current vendor guidance, every
source with its passage, and how to extend the research. `S*` and `V*` IDs are the ones the main
doc cites. A pinned `file:line` stands for the passage at that line of the pinned file; the
SHA-256 in `tools/dev/reference-docs.txt` fixes its text.

## Vendor guidance on draw volume

Vendor guidance points the same way. Arm: "For Vulkan, aim for fewer than 1000 draw calls per
frame" and "The ﬁrst draw call, after a descriptor changes, has a higher CPU overhead" (V-K7).
Khronos: calling `vkUpdateDescriptorSets` for each draw call can take "longer than the time of the
draws themselves" (V-K2). AMD: "Sort draw calls by pipeline" and "Avoid copying descriptors"
(V-N2); the second is what SDL's D3D12 backend does on every texture change, and sorting is ruled
out for us, so deduplication is the lever left. Apple's only current page on per-draw cost says
binding resources one by one makes the driver "capture state and track residency" (V-A7); its
general batching advice survives only in the archived 2017 guide.

## SDL version in detail

3.4.18 (2026-10-02) is the latest SDL release on 2026-10-22 (V-S1). After it:

- **3.4.20** (milestone due 2026-10-31, V-S2): Vulkan fixes for a never-created `defragLock`
  (upload vs memory defragmentation across threads) and a double `vkDestroyImage` on a failed
  memory bind; a `first_binding` range check in `SDL_BindGPUVertexBuffers`. Open in that
  milestone: Metal ignoring `pixels_per_row` (#16474). No API change. SDL #16548 (Vulkan acquire
  blocks while the display is off, seen on Hyprland) sits in the milestone but was closed
  not_planned (V-S8): known Vulkan behaviour for #50, not a fix.
- **3.6.0** (no date): GPU query API with timestamps (#11696/#15651), `SDL_BeginGPURenderPass2`
  (#15994), and opt-in low-latency presentation (#15656, welcomed by the maintainers; may slip to
  3.8, V-S4).
- Nothing in SDL's GPU commits since 3.4.18 changes a recommendation of the main doc
  (`release-3.4.x` and `main`, `src/gpu`, through 2026-10-18; V-S6).

**Recommendation**: keep the pin at 3.4.18 now; move to 3.4.20 when it is released (a patch
release; the Vulkan defrag race matters only with uploads from several threads, which we do not
do). Plan R11's GPU timing around 3.6.0's query API once it is released.

## Currency: what SDL 3.4.18 uses against current vendor guidance

SDL's backends use the older form of several APIs. Each is still valid; none changes a
recommendation of the main doc, because the adapter cannot reach below SDL.

| SDL uses | Current generation | Still applies? |
|---|---|---|
| Vulkan `VkRenderPass` objects (S4 `:6239`) | Dynamic rendering; the spec calls render pass objects legacy from 1.4 (V-K1) | Yes: "Legacy functionality remains available for use in the specification" (V-K1) |
| `vkCmdPipelineBarrier` (S4 `:2550-2575`) | Synchronization2, core in 1.3 (V-K4) | Yes: legacy, not removed (V-K4) |
| Descriptor sets with dynamic uniform buffers (S4 `:5167-5470`) | Push descriptors (core 1.4), `VK_EXT_descriptor_heap` (V-K2, V-K6) | Yes; per-draw descriptor updates remain the anti-pattern the bind cache avoids (V-K2) |
| No `VkPipelineCache` (S4 `:6764`), no D3D12 PSO cache (S5 `:2994-2995`) | `VK_KHR_pipeline_binary` (Roadmap 2026), `ID3D12PipelineLibrary` (V-K3, V-M5) | Yes: the adapter builds its small closed set of pipelines at start (main doc, area 3), so caching across runs saves little (M6) |
| D3D12 legacy `ResourceBarrier` (S5 `:1814-1850`) | Enhanced Barriers, retail since Agility SDK 1.608.0, optional per driver (V-M1) | Yes: "The D3D12 runtime internally translates all ResourceBarrier calls" (V-M1) |
| Classic Metal (`MTLCommandQueue`, render encoders) | Metal 4 (`MTL4*`), opt-in on M1 and later (V-A4) | Yes: classic and Metal 4 queues interoperate; nothing found deprecates the classic API (V-A4) |

Older advice still cited: Apple's archived Metal Best Practices Guide (2017, before Apple
silicon) is the only Apple source for acquire-late and draw batching (V-A1, V-A7); Microsoft's
flip-model and latency pages date from 2017-2018 (V-M2); NVIDIA's posts from 2021-2023 (V-N1);
AMD's RDNA guide from 2023 (V-N2). Each concerns an API SDL still uses as described.

## Sources

Every source below was fetched in this research run on 2026-10-22; dates are the ones the source
states. SDL files are pinned in `tools/dev/reference-docs.txt`
(SHA-256 there) at tag `release-3.4.18`, released 2026-10-02.

### SDL (primary, pinned)

| ID | File | Passages relied on |
|---|---|---|
| S0 | `include/SDL3/SDL_gpu.h` (local `docs/reference/sdl3/SDL_gpu.h`) | performance list `:154-170`; uniforms `:294-307`; pass state reset `:86-88`; cycling `:309-353`; formats `:675-754`; present modes `:1313-1336`; swapchain compositions `:1351`; colour target `cycle` `:2036`; depth target `cycle` `:2097`; binding layout `:2706-2752`; debug groups `:2985-3030`; device properties `:2266-2346`; frames in flight `:4201-4213`; VSYNC+SDR always `:4177-4178`; NULL swapchain `:4328-4331`; swapchain write-only `:4337-4338` |
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
| V-S4 | https://github.com/libsdl-org/SDL/issues/15656 "SDL_GPU: Opt-in low-latency presentation" | opened 2026-05-19, open, milestone 3.6.0; fetched 2026-10-22 | slouken 2026-05-20: "It seems like this is a case where we could improve behavior when the app requests it and if it's not available then it falls back to the current behavior."; thatcosmonaut 2026-06-16: "I will try to find some time but if not we can just push it to 3.8."; DomGries 2026-05-20 measured D3D12 median MsRenderPresentLatency "9.55 ms" default, "9.60 ms" with allowedFramesInFlight=1, "6.72 ms" with a maxLatency=1 patch; slouken 2026-10-03 declines only the opposite, more than 3 frames: "High latency presentation isn’t something we’re planning on adding." |
| V-S7 | https://github.com/libsdl-org/SDL/issues/12715 "[SDL GPU] SDL_GPU_PRESENTMODE_IMMEDIATE with SDL_WINDOW_HIGH_PIXEL_DENSITY causes macOS crash on 120hz monitor" | opened 2025-04-03, open; fetched 2026-10-22 | "A minimal SDL_gpu app with one renderpass built on macOS against SDL3-3.2.10 can consistently cause macOS to crash and reboot."; conditions: "The window's present mode is set to `SDL_GPU_PRESENTMODE_IMMEDIATE`", "The window is on a 120Hz monitor", native fullscreen via the green button |
| V-S8 | https://github.com/libsdl-org/SDL/issues/16548 | opened 2026-10-18, closed 2026-10-18 as not_planned; fetched 2026-10-22 | "an SDL GPU app that's presenting stops inside `vkAcquireNextImageKHR` and stays there until the display comes back on" |
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
or D3D12 to Enhanced Barriers (check `src/gpu` on `main` against the currency table); the final
shape of the query API, `SDL_BeginGPURenderPass2` and opt-in low-latency presentation (#15656);
whether #16474 is fixed in 3.4.20 (then padded rows work on Metal); whether #12715 is fixed.
Checked and absent in 3.4.18: a waitable D3D12 swap chain (`FRAME_LATENCY_WAITABLE_OBJECT` and
`SetMaximumFrameLatency` do not occur in S5), so the latency control Microsoft and NVIDIA
describe (V-M2, V-N1) is out of reach through SDL until #15656 lands. Unverified: the exact
`xctrace` template name for Metal System Trace (`xctrace list templates`), and RenderDoc on
lavapipe.

To verify a claim: run `tools/dev/fetch-docs.sh` and open the cited `file:line` under
`docs/reference/` (S0 is `docs/reference/sdl3/SDL_gpu.h`, S1-S5 are under
`docs/reference/sdl3/src/gpu/`); the SHA-256 in the manifest guarantees the same text. To extend
to a newer SDL, add the new tag's files to the manifest next to the old ones and diff `src/gpu`.
Measurement questions are in `sdl-gpu-backends.md`; answer each in its ticket and replace the
question there with the result.
