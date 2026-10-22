# SDL3 window, display and frame timing

What SDL3 does for us on macOS, Windows and Linux (X11 and Wayland) for windows, displays and
timing, what it leaves to us, and what the game should do. Researched 2026-10-22 against SDL
`release-3.4.18`, the latest SDL3 release that day [S1]. Every claim cites a source in
[Sources](#sources); `file:line` means the file at that tag. The GPU side (swapchain, present modes,
HiDPI from the device) is `docs/agents/sdl-gpu-backends.md` (#71); input grabs and focus for
input are the #73 input research. Read the area you are working on, then the
[design](#design).

The game today (SDL2 build): `similar/arch/sdl/init.cpp` (`arch_init`: `SDL_Init`, three hints),
`similar/arch/ogl/gr.cpp` (`gr_init`: window plus GL context, `gr_toggle_fullscreen`,
`gr_set_attributes`), `common/arch/sdl/event.cpp` (only `SDL_WINDOWEVENT_SIZE_CHANGED`),
`common/arch/sdl/timer.cpp` and `calc_frame_time` in `similar/main/game.cpp` (the frame limiter),
`main` in `similar/main/inferno.cpp`.

## 1. Entry point, init and shutdown

**SDL3 does.** `SDL_main.h` is a header-only entry point; it is no longer included by `SDL.h`
[S9:7]. On Windows it provides `WinMain` and calls our `main` [S4:153]; macOS, X11 and Wayland
need nothing. App callbacks (`SDL_MAIN_USE_CALLBACKS`) are optional, and on desktop SDL fakes
them "with a simple loop" [S3:112-114], pacing `SDL_AppIterate` only through
`SDL_HINT_MAIN_CALLBACK_RATE` [S27:70-81]. SDL says classic main stays the primary model
[S3:116]. `SDL_INIT_VIDEO` implies events and belongs on the main thread [S5:81]; app metadata
goes before `SDL_Init` [S5:219-221].

**Left to us.** Which model; the order of hints, init and teardown.

**Recommendation.** Classic main. The game nests modal loops (every menu and newmenu runs its own
event loop), so callbacks would mean inverting the whole program for no desktop gain. In the
SDL3 arm: include `<SDL3/SDL_main.h>` once, in `similar/main/inferno.cpp` (the file already
`#undef`s SDL2's `argv`/`argc` macros). Order in `arch_init`: `SDL_SetAppMetadata` (name,
version, identifier; it also feeds the Wayland app id [S9:804]), then every pre-init hint (below,
and `SDL_HINT_MAC_BACKGROUND_APP` for hidden runs [S6:2637]), then `SDL_Init(SDL_INIT_VIDEO)`.
Teardown stays `arch_close`'s reverse order, ending in `SDL_Quit`. Keep the three SDL2 hints'
intent, renamed: `SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS` `"0"` (SDL3 default is `"auto"`, which
already spares borderless windows [S6:3908]); `SDL_HINT_WINDOWS_CLOSE_ON_ALT_F4` `"0"` (the old
`NO_CLOSE` hint was inverted [S9:851]); the keyboard grab hint is gone, use
`SDL_SetWindowKeyboardGrab` [S9:828] (#73 owns it).

**Informs** #40. **Sources** S3, S4, S5, S6, S9, S27.

## 2. Window creation and the renderer's claim

**SDL3 does.** A window carries at most one of `OPENGL`, `METAL`, `VULKAN`; with none, macOS
gets a default (`OPENGL` when GL is compiled in, else `METAL`) [S7:284-295, S7:2492-2501].
`SDL_Metal_CreateView` and `SDL_Vulkan_CreateSurface` convert a window in place, dropping
`OPENGL` and unloading GL ("No problem, we can convert to Metal") [S7:6339, S7:6407], so
`SDL_ClaimWindowForGPUDevice` works on any non-transparent window; it creates a VSYNC, SDR
swapchain [S25:4125-4134]. `SDL_GL_SetSwapInterval` fails unless a context is current
[S7:5593-5594]. The spike found `SDL_CreateGPUDevice` needs only `SDL_INIT_VIDEO`, no window
(spike `notes/T1.md`).

**Left to us.** When the renderer is chosen, and in which order device, window and context are
made.

**Recommendation.** Choose before the window exists: create the SDL_GPU device first (driver
from option or `SDL_GPU_DRIVER`); if that fails, log why and take GL (#52) before any window is
on screen. Then create the window with `SDL_WINDOW_OPENGL` for GL, no graphics flag for SDL_GPU,
and claim it. For GL: create the context, then set the swap interval. Today
`gr_set_attributes` calls `SDL_GL_SetSwapInterval` before `SDL_GL_CreateContext` (#41 calls
it an existing bug; SDL3 rejects the call [S7:5594]); the graphics menu calls it again with a
context (`similar/main/menu.cpp`). Window flags: `HIGH_PIXEL_DENSITY` on every platform
(section 4), `HIDDEN` for hidden runs (section 7), fullscreen from the config. The size passed
to `SDL_CreateWindow` is in window coordinates (section 4).

**Informs** #41, #50, #52. **Sources** S7, S25, spike T1.

## 3. Fullscreen and display modes

**SDL3 does.** Fullscreen is borderless desktop by default; exclusive needs a mode from
`SDL_GetFullscreenDisplayModes` passed to `SDL_SetWindowFullscreenMode` [S8:2333-2335,
S8:1019-1021]. `SDL_WINDOW_FULLSCREEN_DESKTOP` is gone [S9:2183]. Size, position and fullscreen
changes are asynchronous requests confirmed by events; `SDL_SyncWindow` blocks until applied,
possibly for "somewhat lengthy animations" [S9:2271-2285, S8:2365-2370]. Exclusive means SDL
changes the display mode itself: `CGDisplaySetDisplayMode` on macOS [S15m:665],
`ChangeDisplaySettingsExW(..., CDS_FULLSCREEN, ...)` on Windows [S16:877], XRandR on X11
[S17:1076]; on Wayland modes are emulated and scaled by the compositor
(`SDL_HINT_VIDEO_WAYLAND_MODE_SCALING`, default `stretch`) [S6:4011]. Per platform:

- **macOS.** Desktop fullscreen uses a Space by default (`SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES`)
  [S6:3832] through `toggleFullScreen:` [S15:940-944]; the menu bar hides when fullscreen was
  set by `SDL_SetWindowFullscreen` [S6:3853]. On notched Macs SDL reports the screen's safe
  area for fullscreen windows [S15:3103-3112]; Apple positions content in the safe area when an
  app uses the system full-screen (`toggleFullScreen`) [S33], and an Info.plist key decides
  whether the system shifts the display's active area for the app [S32].
- **Windows.** Microsoft advises borderless flip-model windows over exclusive fullscreen ("you
  may want to reconsider whether your application actually needs a fullscreen exclusive mode")
  [S30:20] and offering a render resolution independent of output resolution [S30:78]. SDL_GPU's
  D3D12 swapchain is `DXGI_SWAP_EFFECT_FLIP_DISCARD` [S26:7033]; GL on Windows goes through WGL
  and the driver (gap below).
- **X11.** SDL sets `_NET_WM_BYPASS_COMPOSITOR` by default [S6:4148].
- **Wayland.** A toplevel cannot position itself [S23]; exclusive modes are a scaled emulation.

**Left to us.** Exclusive or borderless; how a resolution choice maps to the window; waiting
for async results.

**Recommendation.** Borderless desktop fullscreen only, on every platform, as the SDL2 build
already does (`SDL_WINDOW_FULLSCREEN_DESKTOP`); the game's resolution then becomes a render
resolution scaled to the window's pixel size, which Microsoft's advice and Wayland's emulation
both favour. Do not offer exclusive modes until someone needs them. Toggle with
`SDL_SetWindowFullscreen(bool)` and react to `ENTER/LEAVE_FULLSCREEN` and `PIXEL_SIZE_CHANGED`
instead of reading flags right after the call; call `SDL_SyncWindow` only at startup and in tests,
never per frame. Keep Spaces on (users expect the green button), menu visibility `auto`. Notch:
with Spaces the system keeps content in the safe area [S33]; add
`NSPrefersDisplaySafeAreaCompatibilityMode` to `Info.plist` only after the manual check in #48
shows a problem.

**Informs** #41, #48. **Sources** S6, S8, S9, S15, S15m, S16, S17, S23, S26, S30, S32, S33.

## 4. HiDPI and multiple monitors

**SDL3 does.** Window size and pixel size are distinct; their ratio is the pixel density, and
`SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED` is guaranteed at creation and on every resize [S10:8].
Display scale is pixel density times content scale and changes with the display setting or a
move to another display [S8:987]. macOS and Wayland work in window coordinates and need
`SDL_WINDOW_HIGH_PIXEL_DENSITY` for full detail; Windows and X11 work in pixels and report a
content scale [S10:24-27]. On Windows SDL declares Per-Monitor V2 by default [S14:582-583,
S14:541]; Microsoft: PMv2 apps are notified of DPI changes and never bitmap-scaled [S29:51-57].
On Wayland, `HIGH_PIXEL_DENSITY` makes SDL use `wp_fractional_scale_v1` plus a viewport
[S19:2785-2796], so a 125% desktop gets an exact buffer (scale is a fraction of 120 [S38:92-97]);
without it the compositor scales a blurry buffer [S23]. `SDL_GL_GetDrawableSize` is replaced by
`SDL_GetWindowSizeInPixels` [S9:2197]; `ALLOW_HIGHDPI` became `HIGH_PIXEL_DENSITY` [S9:2264].

**Left to us.** Whether to opt in; which size the config means; mouse coordinates; what to
redo when the pixel size or display changes.

**Recommendation.** `SDL_WINDOW_HIGH_PIXEL_DENSITY` on every platform (the SDL2 build sets
`ALLOW_HIGHDPI` only under `__APPLE__`). The canvas and viewport are always
`SDL_GetWindowSizeInPixels`; one handler for `PIXEL_SIZE_CHANGED` calls
`gr_set_mode_from_window_size` (it replaces `SIZE_CHANGED`, which also covers a move to a
display with another density). Mouse positions arrive in window coordinates; multiply by
`SDL_GetWindowPixelDensity` [S10] (one tested conversion, #48, mouse side in #43). The configured
windowed size is in window coordinates; to keep a 640x480 window from being tiny at 200% on
Windows, multiply the initial size by `SDL_GetDisplayContentScale` of the target display (1.0
on macOS [S10]) — a query, no platform `#if`. The game draws no OS-scaled UI, so it can ignore
`DISPLAY_SCALE_CHANGED`. Remove the `NSHighResolutionCapable=false` workaround in SDL3 builds
(#48); Apple: that key says whether "the Cocoa app supports high-resolution displays" [S31].

**Informs** #48, #43, #41. **Sources** S8, S9, S10, S14, S19, S23, S29, S31, S38.

## 5. Frame timing

**SDL3 does.** `SDL_GetTicks` is 64-bit milliseconds; `SDL_GetTicksNS` gives nanoseconds
[S11, S9:2051]. `SDL_DelayPrecise` sleeps in 1 ms steps, then once more short of the target,
"busy waiting if necessary" [S11:286-288, S12:686-701]. On Windows SDL raises the timer
resolution to 1 ms by default [S12:581, S6:3638] and sleeps on a high-resolution waitable timer
(Windows 10 1803 and later) [S13:63, S13:113]. Microsoft: since Windows 10 2004
`timeBeginPeriod` is per process, and since Windows 11 a fully occluded, minimised or inaudible
window-owning process is not guaranteed the higher resolution [S28:74-76]. macOS: App Nap
throttles timers of a background app that is not visible, not audible and holds no activity
assertion [S34]; SDL takes no such assertion (no `beginActivity` in `src/` at the tag). Vsync on
macOS is unreliable under the compositor: SDL #12088 is open with milestone 3.6.0, and a
maintainer says the 60 Hz drops look like "the system compositor taking control" [S41]; a
maintainer advises against busy loops and for robustness to timing fluctuations [S42].

On Linux, SDL 3.4 picks Wayland only when the compositor offers `wp_fifo_manager_v1` and
otherwise falls back to XWayland "for GPU performance reasons" [S18:543-546]; fifo-v1 makes
a display refresh a readiness constraint for a commit [S37]. So on GNOME or KDE without
fifo-v1 the game runs on X11 unless `SDL_VIDEO_DRIVER=wayland` is set.

**Left to us.** All frame pacing in classic main: the cap, sleeping, and what vsync means.

**The game today.** `timer_update` converts `SDL_GetTicks` milliseconds to `fix` (1 ms is 65
units); `calc_frame_time` loops until `1/SysMaxFPS` has passed, sleeping `SDL_Delay(1)` per
iteration unless vsync or `-nonicefps`. With vsync on, the bound becomes `1/MAXIMUM_FPS`
(200 in release) and the loop never sleeps, trusting the swap to block. At 200 fps the 1 ms
granularity is 20% of a frame.

**Recommendation.** One pacing kernel (see [design](#design)) in the SDL3 arm:

1. Clock from `SDL_GetTicksNS`, converted to `fix` once per frame; the debug harness's fixed
   step stays as is (R1 determinism does not depend on the wall clock).
2. Sleep with `SDL_DelayNS` slices while multiplayer needs `multi_do_frame`, and
   `SDL_DelayPrecise` for the final slice. Always sleep, also with vsync: the swap need not block
   (Wayland skips swaps of hidden windows [S20:117-128] and caps its wait at 20 Hz [S20:147];
   macOS and Windows are a measurement below) or when SDL_GPU returns a NULL swapchain texture
   [S25:4328-4331]. While `SDL_WINDOW_OCCLUDED` or `MINIMIZED` is set, cap at
   `DESIGNATED_GAME_FPS` (30).
3. Vsync stays a tearing control, not the clock: the game's cap still bounds the frame rate.
   Do not derive game time from the display's refresh rate (ProMotion and VRR vary it).
   On macOS keep SDL_GPU off `IMMEDIATE` (SDL #12715, a reboot-level crash on 120 Hz [S43]).
4. Leave `SDL_HINT_TIMER_RESOLUTION` at its default. App Nap: measure first (below); if a
   background multiplayer game is throttled, hold a `ProcessInfo` activity while a game runs
   [S35], in the platform module's macOS adapter.

**Informs** #40 (clock, sleep), #41 (GL swap), #50 (GPU present), #53 (pacing numbers).
**Sources** S6, S9, S11, S12, S13, S18, S20, S25, S28, S34, S35, S37, S41, S42, S43.

## 6. Focus, minimise, occlusion, background and sleep

**SDL3 does.** Window events are top-level (`SDL_EVENT_WINDOW_*`; `SDL_WINDOWEVENT` is gone)
[S9:2268]. Occlusion: macOS sends `OCCLUDED`/`EXPOSED` from `NSWindowOcclusionState`
[S15:1132-1139]; Wayland maps xdg-shell's `suspended` state ("not ordinarily being repainted",
e.g. occluded or screen locked [S39:909-913]) to `OCCLUDED` [S19:461-466]; X11 sends it on unmap
[S21:653-656]; the Windows backend does not send `OCCLUDED` at all (no use in
`src/video/windows/`). Wayland does not report user-initiated minimise [S23:54-57]. The
background/foreground events are iOS and Android only [S24:100-103]; desktop sleep and wake
send no event. SDL disables the screensaver by default [S6:3703] (on Wayland through
`idle-inhibit` [S40]). On macOS `mach_absolute_time` does not advance during sleep [S36].

**Left to us.** Pause policy, CPU use while hidden, behaviour after wake.

**Recommendation.** Keep the game's policy: play continues without focus (multiplayer), the
window stays up (`MINIMIZE_ON_FOCUS_LOSS` `"0"`). Track a visibility state from
`OCCLUDED`/`EXPOSED`/`MINIMIZED`/`RESTORED`/`HIDDEN`/`SHOWN` for the pacer (section 5), never
for game logic, and treat missing events (Windows occlusion, Wayland minimise) as "visible".
After sleep, game time should not jump on macOS if SDL's clock behaves like
`mach_absolute_time` (gap below); Windows is a gap. Re-enable the
screensaver (`SDL_EnableScreenSaver`) only if the owner wants it in menus; default stays off.

**Informs** #41, #40. **Sources** S6, S9, S15, S19, S21, S23, S24, S36, S39, S40.

## 7. Hidden windows for headless tests

**SDL3 does.** `SDL_WINDOW_HIDDEN` windows are not mapped and not in the dock or taskbar
[S8:200]. Wayland only maps a window after its first buffer [S23:25-28] and SDL skips swaps of
hidden windows because compositors would stall them forever [S20:117-128]. The `offscreen` video
driver exists "for automated testing" on headless boxes [S22:25-30] and offers GL through EGL
and Vulkan, but not Metal (spike `notes/T1.md`: `SDL_VIDEO_DRIVER=offscreen` with Metal fails).
X11 reported hidden windows as minimised until a fix after 3.4.18 [S2: 700368ecf2].
`SDL_HINT_MAC_BACKGROUND_APP` keeps the process from becoming frontmost [S6:2637] (spike:
verified).

**Left to us.** Which mechanism per platform; where it is chosen.

**Recommendation.** One request flag (`hidden`) into the platform module; the platform comes
from SDL drivers chosen by the environment, not from code: macOS uses cocoa,
`SDL_WINDOW_HIDDEN` and `SDL_HINT_MAC_BACKGROUND_APP=1` (what `DXX_DBG_HIDDEN` does today, proven
for GL hashes); Linux uses `SDL_VIDEO_DRIVER=offscreen` in the container (GL via EGL,
SDL_GPU via lavapipe), never a hidden Wayland or X11 window; Windows under Wine is unmeasured.
`SDL_VIDEO_DRIVER` stays an environment setting so the game has no test-only code path.

**Informs** #41 (GL smoke and hashes), #50 and #24 (SDL_GPU), #65 (R1 on Linux), #66.
**Sources** S2, S6, S8, S20, S22, S23, spike T1 and T3.

## SDL version

3.4.18 (2026-10-02) is the latest SDL3 release; no 3.5 or 3.6 release exists [S1]. Its release
notes list no window or timing change that touches us [S44]. The `release-3.4.x` branch carries
41 commits after it [S2], five of which matter here:

| Commit (3.4.x) | Change | Affects us |
|---|---|---|
| 700368ecf2 | "x11: Don't report hidden windows as minimized" | hidden runs on X11 |
| 2074f3f839 | cocoa: stop macOS resizing windows moved between displays | multi-monitor Macs |
| 6c2bedd5cb | cocoa: opt out of UpdateCycle; with the macOS 26 SDK input polling is held to "VBlank cadence" | mouse rate, if we build with the macOS 26 SDK |
| 3b20901057 | win32: `WM_DPICHANGED` resize fix for moves between displays of different scale | Windows multi-monitor |
| be9f6fb995 | win: focus flicker fix for DPI-unaware processes | none (SDL is PMv2) |

`main` adds `SDL_AppQuit` on `SDL_EVENT_TERMINATING` (callbacks only) and HDR changes; nothing
else in window or timer code.

**Recommendation.** Keep the 3.4.18 pin for #40 and #41. Move to the next 3.4.x release that
contains 6c2bedd5cb before #43 measures mouse input, and before #48 closes, if the build uses
the macOS 26 SDK. Re-run this table against that release's notes.

## Design

The **platform module** is fork code (proposed home `common/platform/`, ADR 3's "window/surface
module"). It is a deep module: window creation, renderer claim, fullscreen state, pixel size,
visibility and pacing sit behind a small interface; upstream files call it from their
`#elif SDL_MAJOR_VERSION == 3` arms (ADR 3), so SDL3 knowledge has locality in one directory.

Interface (what a caller must know):

- `open(display_request) -> display` — the request holds renderer preference (GL, or SDL_GPU
  with a driver name), fullscreen flag, windowed size in window coordinates and `hidden`. It
  creates the GPU device first, falls back to GL with a logged reason (#52), creates the window
  with the matching flag, claims it or makes the GL context current, then sets the swap
  interval. The result owns all three (RAII) and exposes the **render device** (ADR 2) the front
  end already uses. The game never learns which renderer won, except through `caps`.
- `display.pixel_size()`, `display.set_fullscreen(bool)` — fullscreen is a request; the new size
  arrives as the game's existing `d_window_size_event`, translated from `PIXEL_SIZE_CHANGED`.
- `display.handle(const SDL_Event &)` — the one place window events go; it updates the visibility
  state and forwards size events.
- `frame_pacer` — a pure kernel: given now, the target period, visibility and whether multiplayer
  needs polling, it returns how long to sleep. Clock and sleep are injected; adapters are SDL
  (`SDL_GetTicksNS`, `SDL_DelayNS`/`SDL_DelayPrecise`) and a fake for unit tests, the
  conventions' "pure kernel in a small header" pattern.

Seams:

- **Renderer** — the render device seam already exists with two real adapters (GL, SDL_GPU), so
  the claim belongs inside `open`, not in the game. Only the platform module knows that SDL_GPU
  converts the window in place and GL needs a context.
- **Platform** — SDL itself is the adapter: differences arrive as hints set once before
  `SDL_Init` and as queries (pixel density, content scale, window flags). No `__APPLE__` or
  `_WIN32` in game code; the one exception the research may justify (an App Nap activity) is a
  macOS-only source file selected by the build, inside the module.
- **Headless** — not a third adapter: tests use the real module with SDL's `offscreen` driver
  or a hidden cocoa window (section 7), chosen by the environment. The pacer and the fullscreen
  and visibility bookkeeping are tested through their interfaces with fakes.

Deletion test: without the module, the claim order, swap-interval timing, async fullscreen
handling and pacing rules reappear in `gr.cpp`, `event.cpp`, `game.cpp` and `timer.cpp`, each
with its own `#if` chain. It earns its keep.

_Contradicts ADR-0003 (SDL3 beside SDL1.2 and SDL2), but worth rewording:_ it says the module
"hands the window to SDL_GPU for the runtime renderer switch". The owner dropped the runtime
switch (#51 closed, #50 updated 2026-10-22): the choice is at startup, and SDL's in-place
conversion makes hand-over unnecessary. ADR 3's wording should follow.

## How to extend this research

Re-fetch with `tools/dev/fetch-docs.sh` (pins at the end of `tools/dev/reference-docs.txt`);
check a new SDL release with `gh api repos/libsdl-org/SDL/releases` and
`gh api repos/libsdl-org/SDL/compare/release-3.4.18...<tag>`, filtering `src/video/` and
`src/timer/`.

**Gaps (a source would answer).**

- Windows: whether `QueryPerformanceCounter` advances during sleep (game time after wake).
- Windows GL: whether WGL windows get flip-model presentation (fullscreen optimisations) from
  the driver; Microsoft's flip-model page covers DXGI swapchains only.
- macOS: which clock our SDL build compiles in (`clock_gettime` or `mach_absolute_time`,
  `src/timer/unix/SDL_systimer.c:95-106`); Apple documents only `mach_absolute_time` here.
- Apple's App Nap guide is archived and last updated 2016-09-13 [S34]; no current page found
  that restates the heuristics. Apple pages state no dates.
- X11 content scale sources (Xft.dpi, `SDL_HINT_VIDEO_X11_SCALING_FACTOR`) were not read.

**Questions that need a measurement, not a source.**

| Question | Ticket |
|---|---|
| With vsync on, do GL swap and SDL_GPU present block while occluded or minimised (macOS, Windows, X11, Wayland)? CPU use while hidden | #41 (GL), #50 (SDL_GPU) |
| Frame-interval p50/p99 with vsync on and off at 60 Hz and on a ProMotion 120 Hz panel; does vsync stay engaged (SDL #12088)? | #53 |
| Does App Nap throttle a background windowed game that plays no audio (e.g. multiplayer with music off)? | #41 |
| Does Windows 11 drop the 1 ms resolution for a minimised game, and does the high-resolution waitable timer make that moot? Needs real Windows, not Wine | #41 (no Windows ticket yet) |
| `SDL_GetDisplayContentScale` on GNOME and KDE Wayland at 125% and 200%, and on X11 | #48 |
| Spaces toggle time and the time `SDL_SyncWindow` blocks; menu bar and notch behaviour in fullscreen on a notched Mac | #41, #48 |
| GL hashes from a hidden window on Linux `offscreen` and from a hidden cocoa window on SDL_GPU Metal | #41, #24, #65 |
| Mouse event rate with the macOS 26 SDK on 3.4.18 vs a release with 6c2bedd5cb | #43 |

## Sources

Accessed 2026-10-22 unless noted. Quotes join wrapped lines with one space and drop comment
markers (`*`, `//`) and list bullets. SDL files are at tag
`release-3.4.18` (commit 829a65d769d935c4852f8159e964312c0957260a, dated 2026-10-02), URL
`https://github.com/libsdl-org/SDL/blob/release-3.4.18/<path>`; files marked *pinned* are in
`tools/dev/reference-docs.txt`.

| Id | Source | Stated date | Passage |
|---|---|---|---|
| S1 | GitHub releases, libsdl-org/SDL (`gh api repos/libsdl-org/SDL/releases`) | release-3.4.18 published 2026-10-02T22:43:21Z | newest tag in the list; tags list has no 3.5/3.6 |
| S2 | `gh api repos/libsdl-org/SDL/compare/release-3.4.18...release-3.4.x` and per-commit `gh api repos/libsdl-org/SDL/commits/<sha>` | commits 2026-10-05..2026-10-18 | 700368ecf2 "x11: Don't report hidden windows as minimized"; 6c2bedd5cb "Tahoe changed the default event processing loop for apps built with the macOS 26 SDK to synchronize with CADisplayLink. This effectively restricts input polling rate to VBlank cadence, which is not desirable for games."; 2074f3f839 "Set the undocumented app setting NSScreenLayoutAdjustOnWindowMove to NO to prevent windows from being spuriously resized when being moved between displays."; 3b20901057 "can early-out of the WM_DPICHANGED handler, and prevent the window size from being properly adjusted when moved between displays with a different scale factor." |
| S3 | `docs/README-main-functions.md` (pinned) | none stated | :112-114 "Using the callback entry points works on every platform, because on platforms that don't require them, we can fake them with a simple loop in an internal implementation of the usual SDL_main."; :116 "The primary way we expect people to write SDL apps is still with SDL_main" |
| S4 | `include/SDL3/SDL_main.h` (pinned) | none | :153-154 "On Windows SDL provides WinMain(), which parses the command line and passes the arguments to your main function." |
| S5 | `include/SDL3/SDL_init.h` (pinned) | none | :81 "`SDL_INIT_VIDEO` implies `SDL_INIT_EVENTS`, should be initialized on the main thread"; :219-221 "Consider reporting some basic metadata about your application before calling SDL_Init, using either SDL_SetAppMetadata() or SDL_SetAppMetadataProperty()." |
| S6 | `include/SDL3/SDL_hints.h` (already pinned) | none | :2631 MAC_BACKGROUND_APP "\"1\": The application may remain in the background when launched."; :3638 TIMER_RESOLUTION "The default value is \"1\"." and "This hint is only used on Windows."; :3703 ALLOW_SCREENSAVER "\"0\": Disable screensaver. (default)"; :3832 MAC_FULLSCREEN_SPACES "\"1\": Enable Spaces support (FULLSCREEN_DESKTOP will use them and SDL_WINDOW_RESIZABLE windows will offer the \"fullscreen\" button on their titlebars). (default)"; :3853 menu visibility "\"auto\": The menu will be hidden if fullscreen mode was toggled on programmatically via `SDL_SetWindowFullscreen()`"; :3908 MINIMIZE_ON_FOCUS_LOSS "\"auto\": Fullscreen windows are minimized when they lose focus if they use exclusive fullscreen modes, so the desktop video mode is restored. (default)"; :4011 WAYLAND_MODE_SCALING "\"stretch\" - Video modes will be scaled to fill the entire display. (default)"; :4148 "\"1\": Enable _NET_WM_BYPASS_COMPOSITOR. (default)"; :4521 CLOSE_ON_ALT_F4 "\"1\": SDL will generate a window-close event when it sees Alt+F4. (default)" |
| S7 | `src/video/SDL_video.c` (pinned) | none | :284-295 `SDL_DefaultGraphicsBackends` returns `SDL_WINDOW_OPENGL` on macOS when `GL_CreateContext` exists, else `SDL_WINDOW_METAL`; :2492 "ensure no more than one of these flags is set"; :5594 "No OpenGL context has been made current"; :6339 "No problem, we can convert to Vulkan"; :6407 "No problem, we can convert to Metal" |
| S8 | `include/SDL3/SDL_video.h` (pinned) | none | :200 HIDDEN "window is neither mapped onto the desktop nor shown in the taskbar/dock/window list"; :987 "Conceptually this value corresponds to the scale display setting, and is updated when that setting is changed, or the window moves to a display with a different scale setting."; :1019-1021 "which can be NULL for borderless fullscreen desktop mode, or one of the fullscreen modes returned by SDL_GetFullscreenDisplayModes() to set an exclusive fullscreen mode."; :2333-2335 "By default a window in fullscreen state uses borderless fullscreen desktop mode"; :2365-2370 "On asynchronous windowing systems, this acts as a synchronization barrier ... somewhat lengthy animations that must complete before the window is in its final requested state." |
| S9 | `docs/README-migration.md` (already pinned) | none | :7 "The file with your main() function should include <SDL3/SDL_main.h>, as that is no longer included in SDL.h."; :804 SDL_SetAppMetadata replaces the WMCLASS variables; :828 "SDL_HINT_GRAB_KEYBOARD - use SDL_SetWindowKeyboardGrab() instead"; :851 "replaced with SDL_HINT_WINDOWS_CLOSE_ON_ALT_F4, defaulting to true"; :2051 "SDL_GetTicks() now returns a 64-bit value."; :2183 "SDL_WINDOW_FULLSCREEN_DESKTOP has been removed"; :2197 "SDL_GL_GetDrawableSize() has been removed. SDL_GetWindowSizeInPixels() can be used in its place."; :2264 "SDL_WINDOW_ALLOW_HIGHDPI => SDL_WINDOW_HIGH_PIXEL_DENSITY"; :2268 "SDL_WINDOWEVENT_SIZE_CHANGED - handle the SDL_EVENT_WINDOW_RESIZED and SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED events instead"; :2271-2272 "The following window operations are now considered to be asynchronous requests and should not be assumed to succeed unless a corresponding event has been received" |
| S10 | `docs/README-highdpi.md` (already pinned) | none | :8 "You are guaranteed to get a `SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED` event when a window is created and resized"; :27 "On Linux, X11 uses a similar approach to Windows and Wayland uses a similar approach to macOS."; table :16-21 (row :20 `SDL_GetDisplayContentScale()` 1.0 on macOS, 2.0 on Windows at 200%) |
| S11 | `include/SDL3/SDL_timer.h` (pinned) | none | :286-288 "It will attempt to wait as close to the requested time as possible, busy waiting if necessary, but could return later due to OS scheduling." |
| S12 | `src/timer/SDL_timer.c` (pinned) | none | :581 "Unless the hint says otherwise, let's have good sleep precision"; :691-694 "We'll use 1 ms, it's the minimum guaranteed to produce real sleeps across all platforms." |
| S13 | `src/timer/windows/SDL_systimer.c` (pinned) | none | :63 "CREATE_WAITABLE_TIMER_HIGH_RESOLUTION flag was added in Windows 10 version 1803."; :113 `pCreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS)` |
| S14 | `src/video/windows/SDL_windowsvideo.c` (pinned) | none | :582 `if (!hint \|\| SDL_strcmp(hint, "permonitorv2") == 0) {`; :541 `SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)` |
| S15 | `src/video/cocoa/SDL_cocoawindow.m` (pinned) | none | :942-944 "you need to be FullScreenPrimary, or toggleFullScreen doesn't work."; :1132-1139 `windowDidChangeOcclusionState` sends `SDL_EVENT_WINDOW_EXPOSED` or `SDL_EVENT_WINDOW_OCCLUDED`; :3103 "The view never seems to reflect the safe area, so we'll use the screen instead" |
| S15m | `src/video/cocoa/SDL_cocoamodes.m` (pinned) | none | :665 `result = CGDisplaySetDisplayMode(display, moderef, NULL);` |
| S16 | `src/video/windows/SDL_windowsmodes.c` (pinned) | none | :877 `ChangeDisplaySettingsExW(displaydata->DeviceName, &data->DeviceMode, NULL, CDS_FULLSCREEN, NULL)` |
| S17 | `src/video/x11/SDL_x11modes.c` (pinned) | none | :1076 `X11_XRRSetCrtcConfig(display, res, output_info->crtc, CurrentTime,` |
| S18 | `src/video/wayland/SDL_waylandvideo.c` (pinned) | none | :543-546 (:544) "This compositor lacks support for the fifo-v1 protocol; falling back to XWayland for GPU performance reasons (set SDL_VIDEO_DRIVER=wayland to override)"; :580-582 "so we don't regress GPU-bound performance and frame-pacing by default due to swapchain starvation." |
| S19 | `src/video/wayland/SDL_waylandwindow.c` (pinned) | none | :2785-2796 viewport and `wp_fractional_scale_manager_v1_get_fractional_scale` when `SDL_WINDOW_HIGH_PIXEL_DENSITY`; :465-466 `if (data->suspended) { SDL_SendWindowEvent(window, SDL_EVENT_WINDOW_OCCLUDED, 0, 0);` |
| S20 | `src/video/wayland/SDL_waylandopengles.c` (pinned) | none | :117-119 "For windows that we know are hidden, skip swaps entirely, if we don't do this compositors will intentionally stall us indefinitely"; :147 "20hz, so we'll progress even if throttled to zero." |
| S21 | `src/video/x11/SDL_x11events.c` (pinned) | none | :653-656 "This may be sent when minimizing a window." then `SDL_EVENT_WINDOW_MINIMIZED` and `SDL_EVENT_WINDOW_OCCLUDED` |
| S22 | `src/video/offscreen/SDL_offscreenvideo.c` (pinned) | none | :25-30 "Offscreen video driver is similar to dummy driver, however its purpose is enabling applications to use some of the SDL video functionality (notably context creation) while not requiring a display output. An example would be running a graphical program on a headless box for automated testing." |
| S23 | `docs/README-wayland.md` (pinned) | none | :27 "Wayland requires that the application initially present a buffer before the window becomes visible."; :54-57 "Wayland windows do not currently report the minimized state, aside from when it is activated programmatically via ```SDL_MinimizeWindow()```."; "Wayland does not allow toplevel windows to position themselves programmatically."; "Wayland handles high-DPI displays by scaling the desktop, which causes applications that are not designed to be DPI-aware to be automatically scaled by the window manager, which results in them being blurry." |
| S24 | `include/SDL3/SDL_events.h` (pinned) | none | :100-102 "The application is about to enter the background. ... Called on iOS in applicationWillResignActive() Called on Android in onPause()"; :139-140 "data1 is 1 for live-resize expose events, 0 otherwise." |
| S25 | `include/SDL3/SDL_gpu.h` (already pinned) | none | :4131-4132 "The swapchain will be created with SDL_GPU_SWAPCHAINCOMPOSITION_SDR and SDL_GPU_PRESENTMODE_VSYNC."; :4328-4330 "This function can fill the swapchain texture handle with NULL in certain cases, for example if the window is minimized. This is not an error." |
| S26 | `src/gpu/d3d12/SDL_gpu_d3d12.c` (pinned) | none | :7033 `swapchainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;` |
| S27 | `src/main/generic/SDL_sysmain_callbacks.c` (pinned) | none | :70 "Try to run at whatever rate the hint requested."; :81 `SDL_DelayPrecise(next_iteration - now);` |
| S28 | Microsoft, timeBeginPeriod, https://learn.microsoft.com/en-us/windows/win32/api/timeapi/nf-timeapi-timebeginperiod ; source MicrosoftDocs/sdk-api@1a7909a936dbc06faa7572ed9492968bed817649 (pinned) | ms.date 12/05/2018; last commit 2021-07-09 | :74 "Starting with Windows 10, version 2004, this function no longer affects global timer resolution."; :76 "Starting with Windows 11, if a window-owning process becomes fully occluded, minimized, or otherwise invisible or inaudible to the end user, Windows does not guarantee a higher resolution than the default system resolution." |
| S29 | Microsoft, High DPI desktop application development on Windows, https://learn.microsoft.com/en-us/windows/win32/hidpi/high-dpi-desktop-application-development-on-windows ; source MicrosoftDocs/win32@79eaaa46b30bd0efef0d0f5a65fd7d11fdd8e2de (pinned) | ms.date 07/14/2025; last commit 2026-09-01 | :51-57 "The application being notified when the DPI changes (both the top-level and child HWNDs)" ... "The application never being bitmap scaled by Windows" |
| S30 | Microsoft, For best performance, use DXGI flip model, https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/for-best-performance--use-dxgi-flip-model ; source MicrosoftDocs/win32@3fcc17ad9e68fca2c1f53cdb2b8cd3b274bbd406 (pinned) | ms.date 05/31/2018; last commit 2026-04-14 | :20 "you may want to reconsider whether your application actually needs a fullscreen exclusive mode, since the benefits of a flip model borderless window include faster Alt-Tab switching and better integration with modern display features."; :78 "consider providing users with the ability to choose a rendering resolution for performance scaling, independent from an output resolution" |
| S31 | Apple, NSHighResolutionCapable, https://developer.apple.com/documentation/bundleresources/information-property-list/nshighresolutioncapable (fetched as `https://developer.apple.com/tutorials/data/documentation/<path>.json`) | none stated | "A Boolean value indicating whether the Cocoa app supports high-resolution displays." |
| S32 | Apple, NSPrefersDisplaySafeAreaCompatibilityMode, https://developer.apple.com/documentation/bundleresources/information-property-list/nsprefersdisplaysafeareacompatibilitymode | none stated (macOS 12.0+) | "When this mode is active, the system changes the active area of the display to avoid the camera housing." |
| S33 | Apple, NSScreen.safeAreaInsets, https://developer.apple.com/documentation/appkit/nsscreen/safeareainsets | none stated (macOS 12.0+) | "When you call your window’s toggleFullScreen(_:) method to enter full-screen mode, the system automatically positions the window’s contents within the safe area." |
| S34 | Apple, Energy Efficiency Guide for Mac Apps: App Nap, https://developer.apple.com/library/archive/documentation/Performance/Conceptual/power_efficiency_guidelines_osx/AppNap.html | "Updated: 2016-09-13" (archive) | "Generally, an app is a candidate for App Nap if: It isn’t the foreground app It hasn’t recently updated content in the visible portion of a window It isn’t audible It hasn’t taken any IOKit power management or NSProcessInfo assertions It isn’t using OpenGL"; "Timer throttling, which reduces the frequency with which the app’s timers are fired" |
| S35 | Apple, ProcessInfo.beginActivity(options:reason:) and ActivityOptions.latencyCritical, https://developer.apple.com/documentation/foundation/processinfo/beginactivity(options:reason:) | none stated | "Begin an activity using the given options and reason."; latencyCritical: "A flag to indicate the activity requires the highest amount of timer and I/O precision available." |
| S36 | Apple, mach_absolute_time, https://developer.apple.com/documentation/kernel/1462446-mach_absolute_time | none stated | "this clock does not increment while the system is asleep." |
| S37 | wayland-protocols 1.49 (tag 2026-06-07), `staging/fifo/fifo-v1.xml` (pinned) | 2026-06-07 | "This protocol provides a way to use the completion of a display refresh cycle as an additional readiness constraint." |
| S38 | wayland-protocols 1.49, `staging/fractional-scale/fractional-scale-v1.xml` (pinned) | 2026-06-07 | :97 "The sent scale is the numerator of a fraction with a denominator of 120." |
| S39 | wayland-protocols 1.49, `stable/xdg-shell/xdg-shell.xml` (pinned) | 2026-06-07 | :911-913 "The surface is currently not ordinarily being repainted; for example because its content is occluded by another window, or its outputs are switched off due to screen locking." |
| S40 | wayland-protocols 1.49, `unstable/idle-inhibit/idle-inhibit-unstable-v1.xml` (pinned) | 2026-06-07 | "control behavior when display idles" |
| S41 | SDL issue #12088 "[MacOS] Vsync has issues on MacOS", https://github.com/libsdl-org/SDL/issues/12088 | opened 2025-01-25, updated 2025-11-14, open, milestone 3.6.0 | icculus 2025-11-10: "this specific thing sounds like the system compositor taking control, and we're unlikely to be able to avoid that." |
| S42 | SDL issue #12302 "SDL_AppIterate behaviour on macOS", https://github.com/libsdl-org/SDL/issues/12302 | icculus comment 2025-02-17; open | "Generally I would recommend against having a busy-loop, like wait_to_update uses. This cooks the CPU and wastes battery life" |
| S43 | SDL issue #12715, https://github.com/libsdl-org/SDL/issues/12715 | opened 2025-04-03, open | "The window's present mode is set to `SDL_GPU_PRESENTMODE_IMMEDIATE`" (macOS crash; already on #48) |
| S44 | SDL 3.4.18 release notes, https://github.com/libsdl-org/SDL/releases/tag/release-3.4.18 | 2026-10-02 | "This is a stable bugfix release, with the following changes:" (no window or timer item) |

**Superseded advice.** `SDL_HINT_TIMER_RESOLUTION`'s header cites a 2013 blog post on global
timer resolution cost [S6:3621-3628]; Microsoft changed the behaviour to per process in Windows 10
2004 and limited it for hidden processes in Windows 11 [S28], so the cost argument no longer
applies to us and the Windows 11 limit does. Apple's App Nap guide is archived (2016) [S34]. The
SDL2 names and defaults the game uses (`FULLSCREEN_DESKTOP`, `GL_GetDrawableSize`,
`ALLOW_HIGHDPI`, `NO_CLOSE_ON_ALT_F4`, `GRAB_KEYBOARD`, the minimise hint default) are all
superseded [S9, S6]. Microsoft's flip-model page (2018) applies to SDL_GPU's D3D12 swapchain,
not to GL.
