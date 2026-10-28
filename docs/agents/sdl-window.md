# SDL3 window, display and frame timing

What SDL3 does for us on macOS, Windows and Linux (X11 and Wayland) for windows, displays and
timing, what it leaves to us, and what the game should do. Researched 2026-10-22 against SDL
`release-3.4.18`, the latest SDL3 release that day [S1]. Every claim cites an S id; the quotes,
dates and re-fetch steps are in `docs/agents/sdl-window-sources.md`, and `file:line` means the
file at that tag. Frame pacing and the clock are this doc's; the swapchain, present modes and
GPU-side HiDPI are `docs/agents/sdl-gpu-backends.md` (#71); input grabs and focus for input are
the #73 input research. Read the area you are working on, then the
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

**Recommendation.** Classic main. The game nests modal loops (every menu and newmenu runs its
own event loop), so callbacks would mean inverting the whole program for no desktop gain. In the
SDL3 arm: include `<SDL3/SDL_main.h>` once, in `similar/main/inferno.cpp` (the file already
`#undef`s SDL2's `argv`/`argc` macros). Order in `arch_init`: `SDL_SetAppMetadata` (name,
version, identifier; it also feeds the Wayland app id [S9:804]), then every pre-init hint
(below), then `SDL_Init(SDL_INIT_VIDEO)`. Hidden runs get `SDL_MAC_BACKGROUND_APP` from the
environment (section 7), so `arch_init` stays the same in tests. Teardown stays `arch_close`'s
reverse order, ending in `SDL_Quit`. Keep the three SDL2 hints' intent, renamed:
`SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS` `"0"` (SDL3 default is `"auto"`, which already spares
borderless windows [S6:3900]); `SDL_HINT_WINDOWS_CLOSE_ON_ALT_F4` `"0"` (the old `NO_CLOSE` hint
was inverted [S9:851]); the keyboard grab hint is gone, use `SDL_SetWindowKeyboardGrab` [S9:828]
(#73 owns it).

**Informs** #40. **Sources** S3, S4, S5, S6, S9, S27.

## 2. Window creation and the renderer's claim

**SDL3 does.** A window carries at most one of `OPENGL`, `METAL`, `VULKAN`; with none, macOS
gets a default (`OPENGL` when GL is compiled in, else `METAL`) [S7:284-295, S7:2492-2501].
`SDL_Metal_CreateView` converts a window in place, dropping `OPENGL` and unloading GL ("No
problem, we can convert to Metal") [S7:6407]. The Metal GPU backend claims a window through
`SDL_Metal_CreateView` [S46:3668]; the Vulkan backend calls the video driver's
`Vulkan_CreateSurface` directly [S47:9974-9985], not the converting public call [S7:6339].
SDL's own GPU test creates its window with flags 0 and then claims it [S48:247]; that a
flag-less window is enough on every backend is inferred from these three. The swapchain the
claim creates is #71's (`docs/agents/sdl-gpu-backends.md`). `SDL_GL_SetSwapInterval` fails
unless a context is current [S7:5593-5594]. The spike found `SDL_CreateGPUDevice` needs only
`SDL_INIT_VIDEO`, no window (`~/Developer/dxx-rebirth-spike/notes/T1.md:66-68`).

**Left to us.** When the renderer is chosen, and in which order device, window and context are
made.

**Recommendation.** Choose before the window exists: create the SDL_GPU device first (driver
from option or `SDL_GPU_DRIVER`); if that fails, log why and take GL (#52) before any window is
on screen. Then create the window with `SDL_WINDOW_OPENGL` for GL, no graphics flag for SDL_GPU,
and claim it. For GL: create the context, then set the swap interval. Today
`gr_set_attributes` calls `SDL_GL_SetSwapInterval` before `SDL_GL_CreateContext` (#41 calls
it an existing bug; SDL3 rejects the call [S7:5594]); the graphics menu calls it again with a
context (`similar/main/menu.cpp`). Window flags: `HIGH_PIXEL_DENSITY` off until #48, then on
(section 4), `HIDDEN` for hidden runs (section 7), fullscreen from the config. The size passed
to `SDL_CreateWindow` is in window coordinates (section 4).

**Informs** #41, #50, #52. **Sources** S7, S46, S47, S48, spike T1.

## 3. Fullscreen and display modes

**SDL3 does.** Fullscreen is borderless desktop by default; exclusive needs a mode from
`SDL_GetFullscreenDisplayModes` passed to `SDL_SetWindowFullscreenMode` [S8:2333-2335,
S8:1019-1021]. `SDL_WINDOW_FULLSCREEN_DESKTOP` is gone [S9:2183]. Size, position and fullscreen
changes are asynchronous requests confirmed by events; `SDL_SyncWindow` blocks until applied,
possibly for "somewhat lengthy animations" [S9:2271-2285, S8:2365-2370]. Exclusive means SDL
changes the display mode itself: `CGDisplaySetDisplayMode` on macOS [S15m:665],
`ChangeDisplaySettingsExW(..., CDS_FULLSCREEN, ...)` on Windows [S16:877], XRandR on X11
[S17:1076]; on Wayland modes are emulated and scaled by the compositor
(`SDL_HINT_VIDEO_WAYLAND_MODE_SCALING`, default `stretch`) [S6:4003]. Per platform:

- **macOS.** Desktop fullscreen uses a Space by default (`SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES`)
  [S6:3824] through `toggleFullScreen:` [S15:940-944]; the menu bar hides when fullscreen was
  set by `SDL_SetWindowFullscreen` [S6:3844]. On notched Macs SDL reports the screen's safe
  area for fullscreen windows [S15:3103-3112]; Apple positions content in the safe area when an
  app uses the system full-screen (`toggleFullScreen`) [S33], and an Info.plist key decides
  whether the system shifts the display's active area for the app [S32].
- **Windows.** Microsoft advises borderless flip-model windows over exclusive fullscreen ("you
  may want to reconsider whether your application actually needs a fullscreen exclusive mode")
  [S30:20] and offering a render resolution independent of output resolution [S30:78]. The
  D3D12 swapchain is #71's; GL on Windows goes through WGL and the driver (gap below).
- **X11.** SDL sets `_NET_WM_BYPASS_COMPOSITOR` by default [S6:4142].
- **Wayland.** Only popups position themselves [S23]; exclusive modes are a scaled emulation.

**Left to us.** Exclusive or borderless; how a resolution choice maps to the window; waiting
for async results.

**Recommendation.** Borderless desktop fullscreen only, on every platform, as the SDL2 build
already does (`SDL_WINDOW_FULLSCREEN_DESKTOP`); the game's resolution then becomes a render
resolution scaled to the window's pixel size, which Microsoft's advice and Wayland's emulation
both favour. Offer exclusive modes once someone needs them. Toggle with
`SDL_SetWindowFullscreen(bool)` and act on the `ENTER/LEAVE_FULLSCREEN` and
`PIXEL_SIZE_CHANGED` events that confirm it; `SDL_SyncWindow` belongs at startup and in tests.
Keep Spaces on: SDL then enters fullscreen through `toggleFullScreen:` [S15:942-944], the path
for which Apple keeps content in the notch's safe area [S33]. Menu visibility stays `auto`. The
game sets no `SDL_WINDOW_RESIZABLE`, so its title bar has no fullscreen button; adding one is a
decision for #41 and #48. Notch: add
`NSPrefersDisplaySafeAreaCompatibilityMode` to `Info.plist` only after the manual check in #48
shows a problem.

**Informs** #41, #48. **Sources** S6, S8, S9, S15, S15m, S16, S17, S23, S30, S32, S33.

## 4. HiDPI and multiple monitors

**SDL3 does.** Window size and pixel size are distinct; their ratio is the pixel density, and
`SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED` is guaranteed at creation and on every resize [S10:8].
Display scale is pixel density times content scale and changes with the display setting or a
move to another display [S10:10]. macOS and Wayland work in window coordinates and need
`SDL_WINDOW_HIGH_PIXEL_DENSITY` for full detail; Windows and X11 work in pixels and report a
content scale [S10:24-27]. On Windows SDL declares Per-Monitor V2 by default [S14:582-583,
S14:541]; Microsoft: PMv2 apps are notified of DPI changes and never bitmap-scaled [S29:51-57].
On Wayland, `HIGH_PIXEL_DENSITY` makes SDL use `wp_fractional_scale_v1` plus a viewport
[S19:2785-2796], so a 125% desktop gets an exact buffer (scale is a fraction of 120 [S38:92-97]);
without it the compositor scales a blurry buffer [S23]. `SDL_GL_GetDrawableSize` is replaced by
`SDL_GetWindowSizeInPixels` [S9:2197]; `ALLOW_HIGHDPI` became `HIGH_PIXEL_DENSITY` [S9:2264].

**Left to us.** Whether to opt in; which size the config means; mouse coordinates; what to
redo when the pixel size or display changes.

**Recommendation.** `SDL_WINDOW_HIGH_PIXEL_DENSITY` off until #48, then on every platform (the
SDL2 build sets `ALLOW_HIGHDPI` only under `__APPLE__`). Off first, because with density 2
`SDL_SetWindowSize` takes window coordinates while the `PIXEL_SIZE_CHANGED` handler feeds pixel
sizes into `gr_set_mode`, so each event would double the window (#41's review); #48 turns it on
once sizes are converted (pixel size ÷ `SDL_GetWindowPixelDensity`) and #43 converts mouse
positions. The canvas and viewport are always
`SDL_GetWindowSizeInPixels`; one handler for `PIXEL_SIZE_CHANGED` calls
`gr_set_mode_from_window_size` (it replaces `SIZE_CHANGED`, which also covers a move to a
display with another density). Mouse positions arrive in window coordinates; multiply by
`SDL_GetWindowPixelDensity` [S10] (one tested conversion, #48, mouse side in #43). The
configured windowed size is in window coordinates; to keep a 640x480 window from being tiny at
200% on Windows, multiply the initial size by `SDL_GetDisplayContentScale` of the target display
(1.0 on macOS [S10]) — a design choice, made through a query rather than a platform `#if`. The
game draws no OS-scaled UI, so `DISPLAY_SCALE_CHANGED` needs no handler. Remove the
`NSHighResolutionCapable=false` workaround in SDL3 builds (#48); Apple: that key says whether
"the Cocoa app supports high-resolution displays" [S31].

**Informs** #48, #43, #41. **Sources** S8, S9, S10, S14, S19, S23, S29, S31, S38.

## 5. Frame timing

**SDL3 does.** `SDL_GetTicks` is 64-bit milliseconds; `SDL_GetTicksNS` gives nanoseconds [S11,
S9:2051]. `SDL_DelayPrecise` sleeps in 1 ms steps, then once more short of the target, "busy
waiting if necessary" [S11:286-288, S12:686-701]. On Windows SDL raises the timer resolution to
1 ms by default [S12:581, S6:3630] and sleeps on a high-resolution waitable timer (Windows 10
1803 and later) [S13:63, S13:113]. Microsoft: since Windows 10 2004 `timeBeginPeriod` is per
process, and since Windows 11 a fully occluded, minimised or inaudible window-owning process is
not guaranteed the higher resolution [S28:74-76]. macOS: App Nap throttles timers of a
background app that is not visible, not audible and holds no activity assertion [S34]; SDL takes
no such assertion [S49]. Vsync on macOS is unreliable under the compositor: SDL #12088 is open
with milestone 3.6.0, and a maintainer says the 60 Hz drops look like "the system compositor
taking control" [S41]; a maintainer advises against busy loops and for robustness to timing
fluctuations [S42].

On Linux, SDL 3.4 picks Wayland only when the compositor offers `wp_fifo_manager_v1` and
otherwise falls back to XWayland "for GPU performance reasons" [S18:543-546]; fifo-v1 makes
a display refresh a readiness constraint for a commit [S37]. So on GNOME or KDE without
fifo-v1 the game runs on X11 unless `SDL_VIDEO_DRIVER=wayland` is set.

**Left to us.** All frame pacing in classic main: the cap, sleeping, and what vsync means.

**The game today.** `timer_update` converts `SDL_GetTicks` milliseconds to `fix` (1 ms is
65.536 units). Three limiters share one rule: `calc_frame_time` (`similar/main/game.cpp:684-704`,
gameplay), `timer_delay_bound` (`common/arch/sdl/timer.cpp:64-87`, menus) and the automap loop
(`similar/main/automap.cpp:1058-1066`). Each waits until `1/SysMaxFPS` has passed, sleeping
about 1 ms per iteration unless vsync or `-nonicefps`. With vsync on, the bound becomes
`1/MAXIMUM_FPS` (200 in release) and the loop sleeps only on the swap. At 200 fps the 1 ms
granularity is 20% of a frame.

**Recommendation.** One pacing kernel (see [design](#design)) in the SDL3 arm, called by all
three limiters:

1. Clock from `SDL_GetTicksNS`, converted to `fix` once per frame; the debug harness's fixed
   step stays as is (R1 determinism does not depend on the wall clock).
2. Sleep with `SDL_DelayNS` slices while multiplayer needs `multi_do_frame`, and
   `SDL_DelayPrecise` for the final slice. Always sleep, also with vsync: the swap need not block
   (Wayland skips swaps of hidden windows [S20:117-128] and caps its wait at 20 Hz [S20:147];
   macOS and Windows are a measurement below; SDL_GPU's NULL swapchain texture is #71's).
   While `SDL_WINDOW_OCCLUDED` or `MINIMIZED` is set, cap at `DESIGNATED_GAME_FPS` (30), a
   design choice.
3. Vsync stays a tearing control, not the clock: the game's cap still bounds the frame rate,
   and game time comes from the clock alone, since ProMotion and VRR vary the refresh rate.
   Present modes, including the macOS `IMMEDIATE` crash, are #71's.
4. Leave `SDL_HINT_TIMER_RESOLUTION` at its default. App Nap: measure first (below); if a
   background multiplayer game is throttled, hold a `ProcessInfo` activity while a game runs
   [S35], in the platform module's macOS adapter.

**Informs** #40 (clock, sleep), #41 (GL swap), #50 (GPU present), #53 (pacing numbers).
**Sources** S6, S9, S11, S12, S13, S18, S20, S28, S34, S35, S37, S41, S42, S49.

## 6. Focus, minimise, occlusion, background and sleep

**SDL3 does.** Window events are top-level (`SDL_EVENT_WINDOW_*`; `SDL_WINDOWEVENT` is gone)
[S9:381]. Occlusion: macOS sends `OCCLUDED`/`EXPOSED` from `NSWindowOcclusionState`
[S15:1132-1139]; Wayland maps xdg-shell's `suspended` state ("not ordinarily being repainted",
e.g. occluded or screen locked [S39:911-913]) to `OCCLUDED` [S19:461-466]; X11 sends it on unmap
[S21:653-656], and 3.4.18 misses minimise and occlusion on ICCCM window managers such as i3
that only change `WM_STATE`; the next 3.4.x adds that [S2]. The Windows backend sends no
`OCCLUDED` [S49]. Wayland does not report user-initiated minimise [S23:56]. The
background/foreground events are iOS and Android only [S24:100-103]; desktop sleep and wake
send no event. SDL disables the screensaver by default [S6:3696] (on Wayland through
`idle-inhibit` [S40]). On macOS `mach_absolute_time` does not advance during sleep [S36].

**Left to us.** Pause policy, CPU use while hidden, behaviour after wake.

**Recommendation.** Keep the game's policy: play continues without focus (multiplayer), the
window stays up (`MINIMIZE_ON_FOCUS_LOSS` `"0"`). Track a visibility state from
`OCCLUDED`/`EXPOSED`/`MINIMIZED`/`RESTORED`/`HIDDEN`/`SHOWN` for the pacer (section 5) and
nothing else, and treat missing events (Windows occlusion, Wayland minimise, X11 under i3 on
3.4.18) as "visible". After sleep, game time should not jump on macOS if SDL's clock behaves
like `mach_absolute_time` (gap below); Windows is a gap. Re-enable the screensaver
(`SDL_EnableScreenSaver`) only if the owner wants it in menus; default stays off.

**Informs** #41, #40. **Sources** S2, S6, S9, S15, S19, S21, S23, S24, S36, S39, S40, S49.

## 7. Hidden windows for headless tests

**SDL3 does.** `SDL_WINDOW_HIDDEN` windows are not mapped and not in the dock or taskbar
[S8:200]. Wayland only maps a window after its first buffer [S23:27] and SDL skips swaps of
hidden windows because compositors would stall them forever [S20:117-128]. The `offscreen` video
driver exists "for automated testing" on headless boxes [S22:25-30] and offers GL through EGL
and Vulkan surfaces [S22:87-105], but not Metal (spike
`~/Developer/dxx-rebirth-spike/notes/T1.md:69-71`: `SDL_VIDEO_DRIVER=offscreen` with Metal
fails). `SDL_HINT_MAC_BACKGROUND_APP` keeps the process from becoming frontmost [S6:2631]
(spike: verified).

**Left to us.** Which mechanism per platform; where it is chosen.

**Recommendation.** One request flag (`hidden`) into the platform module, adding only
`SDL_WINDOW_HIDDEN` as `DXX_DBG_HIDDEN` does in the SDL2 build (`gr_init`, `similar/arch/ogl/gr.cpp`). The rest
comes from the environment: macOS uses cocoa with `SDL_MAC_BACKGROUND_APP=1` from
`tools/dev/no-window.sh:22` (proven for GL hashes); Linux uses `SDL_VIDEO_DRIVER=offscreen` in
the container for GL via EGL, the one Linux path that needs no compositor; SDL_GPU on
`offscreen` (lavapipe) is a measurement below; Windows under Wine is unmeasured. The game's code
path is the same in tests and in play.

**Informs** #41 (GL smoke and hashes), #50 and #24 (SDL_GPU), #65 (R1 on Linux), #66.
**Sources** S6, S8, S20, S22, S23, spike T1 and T3.

## 8. Platform-only properties and their defaults

**SDL3 does.** A property or hint scoped to one platform or video driver is ignored elsewhere, so
a default that is wrong for the game stays invisible on macOS, our development and test platform,
and surfaces only on the GitHub Windows run or in the Linux container, one bug per run. This
section cites the pinned files directly: `R/` is `docs/reference/`, and lines count the pinned
file. Sources: the platform-scoped lists of `SDL_CreateWindowWithProperties` (`R/sdl3/SDL_video.h:1327-1370`),
the hints marked for one platform in `R/sdl3/SDL_hints.h`, and the video drivers under
`R/sdl3/src/video/`. The create properties are all of the "wrap an existing window" kind
(`COCOA_WINDOW`, `COCOA_VIEW`, `WAYLAND_WL_SURFACE`, `WIN32_HWND`, `X11_WINDOW`,
`WAYLAND_SURFACE_ROLE_CUSTOM`, and the two below); the game wraps no window, so it sets none of
them. The full set at 3.4.18 that could matter:

| Property or hint (R/sdl3 file:line) | Scope | Default | The game sets | Owner |
|---|---|---|---|---|
| `SDL_PROP_WINDOW_CREATE_WAYLAND_CREATE_EGL_WINDOW_BOOLEAN` (`SDL_video.h:1348`), `..._WIN32_PIXEL_FORMAT_HWND_POINTER` (`:1360`) | Wayland, Windows | unset | default: both exist for windows without `SDL_WINDOW_OPENGL` or for sharing a GL pixel format between windows; neither applies | #41 |
| `SDL_WINDOW_HIGH_PIXEL_DENSITY` / `SDL_PROP_WINDOW_CREATE_HIGH_PIXEL_DENSITY_BOOLEAN` (`SDL_video.h:1420`; `README-highdpi.md:7,16-21`; cocoa `SDL_cocoawindow.m:1247,2534`; wayland `SDL_waylandwindow.c:58,2989`) | macOS, Wayland only; no code path in the windows or x11 drivers | off: macOS renders at window size, Wayland at scale 1.0 [`SDL_waylandwindow.c:58`] | off until #48 converts window sizes and #43 mouse positions (section 4), then on every platform; on Windows and X11 it changes nothing, so the Windows and Linux runs cannot show a missing flag | #48 |
| `SDL_HINT_VIDEO_X11_SCALING_FACTOR` (`SDL_hints.h:4192`; chain in `src/video/x11/SDL_x11modes.c:51-139`) | X11 | unset; content scale comes from `Xft.dpi` in the X resource database (`:64-101`), then XSETTINGS `Gdk/WindowScalingFactor` and `Xft/DPI` (`:113-121`), then `GDK_SCALE` (`:126-130`), else 1.0 (`:135`) | default; the initial window size multiplies by `SDL_GetDisplayContentScale` (section 4), so a headless or bare X server (scale 1.0) yields an unscaled window. Tests that need a scale set the hint | #48 |
| `SDL_WINDOWS_DPI_AWARENESS` (no `SDL_HINT_` macro; `src/video/windows/SDL_windowsvideo.c:580-592`) | Windows | unset means Per-Monitor V2 (`:582`) | default; the hint is undocumented in `SDL_hints.h`. `unaware`, `system` or `permonitor` would make Windows bitmap-scale or skip `WM_DPICHANGED`, which section 4's conversions assume | #48 |
| `SDL_HINT_MOUSE_DPI_SCALE_CURSORS` (`SDL_hints.h:2828`) | Windows ("currently implemented on Windows", `:2822`) | `"0"`, cursors keep their size at 200% | `"1"` only if the game shows an SDL system cursor rather than its own; set before cursors are created | #43 |
| `SDL_HINT_VIDEO_X11_NET_WM_PING` (`SDL_hints.h:4167`), `SDL_HINT_WINDOWS_ENABLE_MESSAGELOOP` (`:4565`) | X11; Windows | `"1"`; `"1"` (the message loop runs only inside `SDL_PumpEvents`) | default. The X11 hint documents the failure: an app that cannot answer pings in time is shown as hung. Windows has no hint text on it; that a window whose loop stops pumping shows as not responding is inferred. Fix by pumping events in long loads (level load, movies), not by `"0"` | #41 |
| `SDL_HINT_VIDEO_FORCE_EGL` (`SDL_hints.h:3814`; `SDL_windowsvideo.c:393`, `SDL_cocoavideo.m:144`) | Windows, macOS (WGL and CGL by default); X11 uses GLX; Wayland and `offscreen` use EGL regardless | `"0"` | default; the GL fallback (#52) therefore runs a different context path in each place: WGL on the Windows run, GLX on X11, EGL in the container. A GL bug that shows in one cannot be dismissed from another | #52 |
| `SDL_HINT_VIDEO_WAYLAND_SCALE_TO_DISPLAY` (`SDL_hints.h:4070`; `SDL_waylandvideo.c:604`) | Wayland | `"0"` | default: it forces 1:1 pixels for windows without `HIGH_PIXEL_DENSITY`, with blur and rounding the header lists; the flag in section 4 is the correct route | #48 |

The Windows-only and X11-only rows above are the ones no local run exercises. Covered elsewhere
in this doc, each with the right default: `SDL_HINT_TIMER_RESOLUTION` (`SDL_hints.h:3638`,
section 5), `SDL_HINT_WINDOWS_CLOSE_ON_ALT_F4` (`:4521`) and `MINIMIZE_ON_FOCUS_LOSS` (`:3908`,
section 1), `SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES` / `_MENU_VISIBILITY` (`:3832`, `:3853`) and
`SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR` (`:4148`, section 3), `SDL_HINT_VIDEO_WAYLAND_MODE_SCALING`
(`:4011`, section 3), `SDL_HINT_VIDEO_ALLOW_SCREENSAVER` (`:3703`, section 6),
`SDL_HINT_MAC_BACKGROUND_APP` (`:2637`, section 7). The `offscreen` driver is never auto-selected:
`OFFSCREEN_Available` is true only when `SDL_VIDEO_DRIVER` names it (`src/video/offscreen/SDL_offscreenvideo.c:54-60`),
which section 7 already implies by setting it in the environment.

**Set aside.** `SDL_HINT_VIDEO_METAL_AUTO_RESIZE_DRAWABLE` (`:3870`, default `"1"`), `MAC_OPENGL_ASYNC_DISPATCH`
(`:2676`): macOS-only, so local runs show them. `MAC_OPTION_AS_ALT`, `MAC_CTRL_CLICK_EMULATE_RIGHT_CLICK`,
`WINDOWS_RAW_KEYBOARD*`, `WINDOWS_GAMEINPUT`, `MAC_PRESS_AND_HOLD`, `ALLOW_ALT_TAB_WHILE_GRABBED`: input, in `sdl-input.md` "Platform-only hints". `WINDOWS_ERASE_BACKGROUND_MODE`
(`:4717`, `"initial"`), `FORCE_RAISEWINDOW` (`:917`), `WINDOW_ALLOW_TOPMOST` (`:4489`), `WINDOWS_ENABLE_MENU_MNEMONICS`
(`:4550`), `INTRESOURCE_ICON*`, `USE_D3D9EX`: cosmetic or unused paths, nothing the game's flow
reaches. `VIDEO_WAYLAND_MODE_EMULATION`, `PREFER_LIBDECOR`, `ALLOW_LIBDECOR`, `MATCH_EXCLUSIVE_MODE_ON_MOVE`:
exclusive modes and decorations, outside the borderless-only decision. `VIDEO_DISPLAY_PRIORITY`
(`:3730`) and `VIDEO_DOUBLE_BUFFER` (`:3753`, Wayland, default off): a user or measurement
choice. X11 visual, override-redirect and XSync hints, KMSDRM, Vivante, OpenVR, Emscripten
properties: other targets. `SDL_GL_SetSwapInterval(-1)` (adaptive, glX and WGL drivers only,
`SDL_video.h:3402-3414`): the game passes only 0 or 1 (`similar/arch/ogl/gr.cpp:774`).
`SDL_PROP_WINDOW_SDR_WHITE_LEVEL_FLOAT` (`:1512`, differs on Windows and Apple): HDR is unused.

**Recommendation.** A later ticket that touches window creation, fullscreen, DPI or the event
loop opens this table first and sets or leaves each row as written. The two rows that already
behave differently from macOS are tested rather than trusted: a unit test pins "initial window
size equals configured size times `SDL_GetDisplayContentScale`" with a fake scale (#48), and the
Windows run logs `SDL_GetWindowPixelDensity` and `SDL_GetDisplayContentScale` once so a
density of 1.0 against a scale of 2.0 reads as expected rather than as a bug. Never read the
absence of an effect on macOS as proof for Windows or X11. On an SDL pin bump, re-read the
platform-scoped lists in `SDL_video.h` (the property list and "On Windows/Wayland/X11" blocks)
and the hints marked for one platform, and add any new entry here.

**Informs** #41, #43, #48, #52. **Sources** `R/sdl3/` files named per row; S6, S10, S14.

## SDL version

3.4.18 (2026-10-02) is the latest SDL3 release; no 3.5 or 3.6 release exists [S1]. Its release
notes list no window or timing change that touches us [S44]. The `release-3.4.x` branch carries
41 commits after it [S2], five of which matter here:

| Commit (3.4.x) | Change | Affects us |
|---|---|---|
| 700368ecf2 | "x11: Don't report hidden windows as minimized"; the diff reads `WM_STATE` so ICCCM window managers such as i3 report minimise and occlusion | the pacer's visibility state on X11 |
| 2074f3f839 | cocoa: stop macOS resizing windows moved between displays | multi-monitor Macs |
| 6c2bedd5cb | cocoa: opt out of UpdateCycle; with the macOS 26 SDK input polling is held to "VBlank cadence" | mouse rate: local builds use the macOS 27 SDK (`xcrun --show-sdk-version` gives 27.0); that SDK 27 keeps SDK 26's behaviour is inferred |
| 3b20901057 | win32: `WM_DPICHANGED` resize fix for moves between displays of different scale | Windows multi-monitor |
| be9f6fb995 | win: focus flicker fix for DPI-unaware processes | none (SDL is PMv2) |

`main` adds `SDL_AppQuit` on `SDL_EVENT_TERMINATING` (callbacks only), HDR, clipboard and
drag-and-drop changes; none touches window or timer behaviour we use [S45].

**Recommendation.** Keep the 3.4.18 pin for #40 and #41. Move to the next 3.4.x release that
contains 6c2bedd5cb before #43 measures mouse rate. Re-run this table against that release's
notes.

## Design

The **platform module** is fork code in `common/sdl/` (ADR 3's "window/surface module"): window
creation, renderer claim, fullscreen state, visibility and pacing sit behind a small interface;
upstream files call it from their `#elif SDL_MAJOR_VERSION == 3` arms (ADR 3), so SDL3 knowledge
has locality in one directory.

Interface (what a caller must know):

- `open_display(display_request, ops) -> display` (`common/sdl/display.h`) — the request holds
  renderer preference (GL, or SDL_GPU with a driver name), fullscreen, borderless, windowed size
  in window coordinates, `hidden` and vsync. It creates the GPU device first, falls back to GL
  with a logged reason (#52), creates the window with the matching flag, claims it or makes the
  GL context current, then sets the swap interval and syncs the window. The result owns device,
  window, claim and context (RAII) and the window's `window_state`. `ops` is SDL
  (`sdl_display_ops`) or a fake for unit tests.
- `window_state` (`common/sdl/window_state.h`) — a pure kernel of the window's visibility and
  fullscreen, fed translated events (`window_event_of`). Fullscreen is a request: it keeps the
  last request, which toggles flip and SDL's ENTER/LEAVE_FULLSCREEN move, and starts from
  `SDL_GetWindowFlags` after the sync, pending flags included.
- `gr_toggle_fullscreen` and `gr_handle_window_event` (`similar/arch/ogl/gr.cpp`) — the game's
  side. The toggle asks SDL for the opposite of the last request. `event_poll` hands every window
  event to the handler, the one place they go: for the game's own window it updates
  `window_state`, follows a confirmed fullscreen change into `CGameCfg.WindowMode`, and, when the
  window's pixel size now (`SDL_GetWindowSizeInPixels`) differs from the canvas, sets the mode
  and sends the game's existing `d_window_size_event` (`react_to_window_event` is its testable
  part). `gr_window_visibility` gives the pacer the visibility.
- `frame_pacer` (`common/sdl/frame_pacer.h`) — a pure kernel: given now, the target period,
  visibility and whether multiplayer needs polling, it returns how long to sleep. Clock and sleep
  are injected; adapters are SDL (`SDL_GetTicksNS`, `SDL_DelayNS`/`SDL_DelayPrecise`) and a fake
  for unit tests, the conventions' "pure kernel in a small header" pattern.

Seams:

- **Renderer** — the render device seam already exists with two real adapters (GL, SDL_GPU), so
  the claim belongs inside `open_display`, not in the game. Only the platform module knows that SDL_GPU
  converts the window in place and GL needs a context.
- **Platform** — SDL itself is the adapter: differences arrive as hints set once before
  `SDL_Init` and as queries (pixel density, content scale, window flags). No `__APPLE__` or
  `_WIN32` in game code; the one exception the research may justify (an App Nap activity) is a
  macOS-only source file selected by the build, inside the module.
- **Headless** — not a third adapter: tests use the real module with SDL's `offscreen` driver
  or a hidden cocoa window (section 7), chosen by the environment. The pacer and the fullscreen
  and visibility bookkeeping are tested through their interfaces with fakes.

Deletion test: without the module, the claim order, swap-interval timing, async fullscreen
handling and pacing rules reappear in `gr.cpp`, `event.cpp`, `game.cpp`, `automap.cpp` and
`timer.cpp`, each
with its own `#if` chain. It earns its keep.

_ADR 3 now follows (amended 2026-10-23):_ the module chooses the renderer at startup instead of
"hand[ing] the window to SDL_GPU for the runtime renderer switch". The owner dropped the runtime
switch (#51 closed, #50 updated 2026-10-22), and SDL's in-place conversion makes hand-over
unnecessary. #41's acceptance criterion "can hand the window to SDL_GPU later (ADR 3)" is still to
follow.

## Gaps and measurements

To re-verify or extend, or to read a quote behind an S id: `docs/agents/sdl-window-sources.md`.

**Gaps (a source would answer).**

- Windows: whether `QueryPerformanceCounter` advances during sleep (game time after wake).
- Windows GL: whether WGL windows get flip-model presentation (fullscreen optimisations) from
  the driver; Microsoft's flip-model page covers DXGI swapchains only.
- macOS: which clock our SDL build compiles in (`clock_gettime` or `mach_absolute_time`,
  `src/timer/unix/SDL_systimer.c:95-106`); Apple documents only `mach_absolute_time` here.
- Apple's App Nap guide is archived and last updated 2016-09-13 [S34]; no current page found
  that restates the heuristics. Apple pages state no dates.
- X11 content scale: the sources are now read (section 8); what GNOME and KDE on X11 actually
  report is the measurement below.

**Questions that need a measurement, not a source.**

| Question | Ticket |
|---|---|
| With vsync on, do GL swap and SDL_GPU present block while occluded or minimised (macOS, Windows, X11, Wayland)? CPU use while hidden | #41 (GL), #50 (SDL_GPU) |
| Frame-interval p50/p99 with vsync on and off at 60 Hz and on a ProMotion 120 Hz panel; does vsync stay engaged (SDL #12088)? | #53 |
| Does App Nap throttle a background SDL_GPU Metal game that plays no audio (e.g. multiplayer with music off)? GL is likely exempt ("It isn’t using OpenGL" [S34]) | #50 |
| Does Windows 11 drop the 1 ms resolution for a minimised game, and does the high-resolution waitable timer make that moot? Needs real Windows, not Wine | #41 (no Windows ticket yet) |
| `SDL_GetDisplayContentScale` on GNOME and KDE Wayland at 125% and 200%, and on X11 | #48 |
| Spaces toggle time and the time `SDL_SyncWindow` blocks; menu bar and notch behaviour in fullscreen on a notched Mac | #41, #48 |
| GL hashes from a hidden window on Linux `offscreen`; SDL_GPU (lavapipe) on `offscreen`; a hidden cocoa window on SDL_GPU Metal | #41, #24, #65 |
| Visibility events on X11 under i3 or another ICCCM-only window manager, before and after the next 3.4.x | #41 |
| Mouse event rate with the macOS 27 SDK on 3.4.18 vs a release with 6c2bedd5cb | #43 |
