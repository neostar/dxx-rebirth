# SDL3 input research: sources and open questions

Sources for `sdl-input.md` (#73), and how to extend that research. Accessed 2026-10-22.

## Sources

Raw files are pinned with SHA-256 in `tools/dev/reference-docs.txt` and mirrored under
`docs/reference/<path>` by `tools/dev/fetch-docs.sh`; the Path column gives that path. Files under
`sdl3/` are at tag `release-3.4.18` (commit 829a65d769d935c4852f8159e964312c0957260a, released
2026-10-02) unless the path says otherwise; headers and sources state no date of their own. Line
numbers were checked against the release tarball pinned in `toolchain.pins`, whose files are
byte-identical to the tag's raw files. `sdl3/SDL_events.h`, `sdl3/SDL_timer.h` and
`sdl3/SDL_video.h` are pinned by the window research (#74); S5, S7 and S22 by the earlier SDL
block.

| Id | Path or URL | Version | Stated date |
|---|---|---|---|
| S1 | https://github.com/libsdl-org/SDL/releases (page and `gh api repos/libsdl-org/SDL/releases`) | — | 3.4.18 published 2026-10-02T22:43:21Z, marked "Latest" |
| S2 | `gh api repos/libsdl-org/SDL/compare/release-3.4.18...release-3.4.x`; `gh api repos/libsdl-org/SDL/commits/<sha>` | branch head 2026-10-22 | commits 2026-10-02..10-18 |
| S3 | `sdl3/3.4.x-6c2bedd5/src/video/cocoa/SDL_cocoaevents.m`; https://github.com/libsdl-org/SDL/commit/6c2bedd5cb0194b0258d2ec13466fbc5f631ad8c | 6c2bedd5cb | committed 2026-10-17 |
| S4 | `sdl3/SDL_events.h` | 3.4.18 | none |
| S5 | `sdl3/README-migration.md` | 3.4.18 | none |
| S6 | `sdl3/src/events/SDL_keyboard.c` | 3.4.18 | none |
| S7 | `sdl3/SDL_hints.h` | 3.4.18 | none |
| S8 | `sdl3/SDL_scancode.h` | 3.4.18 | none |
| S9 | `sdl3/SDL_keyboard.h` | 3.4.18 | none |
| S10 | `sdl3/src/video/cocoa/SDL_cocoaevents.m` | 3.4.18 | none |
| S10b | `sdl3/src/video/cocoa/SDL_cocoakeyboard.m` | 3.4.18 | none |
| S11 | `sdl3/SDL_keycode.h` | 3.4.18 | none |
| S12 | `sdl3/SDL_mouse.h` | 3.4.18 | none |
| S13 | `sdl3/src/events/SDL_mouse.c` | 3.4.18 | none |
| S14 | `sdl3/src/video/windows/SDL_windowsrawinput.c` | 3.4.18 | none |
| S15 | `sdl3/src/video/windows/SDL_windowsmouse.c` | 3.4.18 | none |
| S16 | `sdl3/src/video/x11/SDL_x11xinput2.c`, `sdl3/src/video/x11/SDL_x11mouse.c` | 3.4.18 | none |
| S17 | `sdl3/src/video/wayland/SDL_waylandevents.c`, `.../SDL_waylandmouse.c` | 3.4.18 | none |
| S18 | `sdl3/src/video/cocoa/SDL_cocoamouse.m` | 3.4.18 | none |
| S19 | `sdl3/wayland-protocols/relative-pointer-unstable-v1.xml` (SDL's bundled copy) | 3.4.18 | none |
| S20 | https://developer.apple.com/documentation/appkit/nsevent/deltax (JSON: `/tutorials/data/documentation/appkit/nsevent/deltax.json`) | — | none ("Copyright © 2026") |
| S21 | `sdl3/src/video/cocoa/SDL_cocoavideo.m` | 3.4.18 | none |
| S22 | `sdl3/README-highdpi.md` | 3.4.18 | none |
| S23 | https://developer.apple.com/documentation/appkit/nsevent/isdirectioninvertedfromdevice | macOS 10.7+ | none |
| S24 | `sdl3/wayland-protocols/wayland.xml` (SDL's bundled copy) | 3.4.18 | none |
| S25 | `sdl3/src/joystick/SDL_gamepad.c` | 3.4.18 | none |
| S26 | `sdl3/SDL_joystick.h` | 3.4.18 | none |
| S27 | `sdl3/src/joystick/SDL_joystick.c` | 3.4.18 | none |
| S28 | `gamecontrollerdb/README.md` | ff333823a85a | head commit 2026-10-19 |
| S29 | `sdl3/src/joystick/hidapi/SDL_hidapijoystick_c.h` | 3.4.18 | none |
| S30 | `sdl3/src/joystick/gdk/SDL_gameinputjoystick.cpp` | 3.4.18 | none |
| S31 | `sdl3/src/joystick/windows/{SDL_rawinputjoystick.c,SDL_windows_gaming_input.c,SDL_xinputjoystick.c}` | 3.4.18 | none |
| S32 | `sdl3/WhatsNew.txt`: GameInput off in 3.4.0 (:78), WGI off in 3.2.22 (:111), RawInput off in 3.2.16 (:117) | 3.4.18 | none |
| S33 | https://learn.microsoft.com/en-us/gaming/gdk/docs/features/common/input/overviews/input-overview | gdk-2604 (default view); gdk-2610, the newest moniker, has the same text | ms.date 2022-08-01, edited 2026-02-19, updated_at 2026-05-12 |
| S34 | `sdl3/SDL_filesystem.h` | 3.4.18 | none |
| S35 | `sdl3/SDL_gamepad.h` | 3.4.18 | none |
| S36 | `sdl2/SDL_gamecontroller-2.30.0.h` | SDL2 release-2.30.0 (859844eae358) | none |
| S37 | `sdl3/SDL_oldnames.h` (enabled block: lines 38-681) | 3.4.18 | none |
| S38 | https://partner.steamgames.com/doc/features/steam_controller/getting_started_for_devs | — | none stated |
| S39 | `sdl3/src/joystick/SDL_steam_virtual_gamepad.c` | 3.4.18 | none |
| S40 | `sdl3/src/events/SDL_events.c` | 3.4.18 | none |
| S41 | `sdl3/SDL_timer.h` | 3.4.18 | none |
| S42 | `sdl3/test/testautomation_joystick.c` | 3.4.18 | none |
| S43 | https://learn.microsoft.com/en-us/windows/win32/inputdev/about-raw-input | git 376575d2 | ms.date 2025-07-14, updated_at 2026-03-18 |
| S44 | `gh api repos/libsdl-org/SDL/releases` (SDL2 tags); https://github.com/libsdl-org/SDL/releases/tag/release-2.32.0 | 2.32.10 latest SDL2 | 2.32.10 published 2025-09-01; 2.32.0 published 2025-02-08 |
| S45 | `homebrew/Aliases-sdl2` (`../Formula/s/sdl2-compat.rb`), `homebrew/sdl2-compat.rb` | homebrew-core 3a29b9d7df51 | commit 2026-10-03 |
| S46 | `sdl2-compat/README.md` | sdl2-compat release-2.32.74 (e4df8a55f20d) | release 2026-10-02 |
| S47 | `sdl2-compat/SDL_gamecontroller.h` | sdl2-compat release-2.32.74 | release 2026-10-02 |
| S48 | `sdl2/SDL_gamecontroller-2.32.10.h` | SDL2 release-2.32.10 | release 2025-09-01 |
| S49 | `sdl2/WhatsNew.txt` (newest section: 2.30.0, line 5) | SDL2 release-2.32.10 | none |

Also cited: `sdl3/SDL_video.h` (`SDL_SetWindowKeyboardGrab` 2580, `SDL_SetWindowMouseGrab` 2600).
S43 backs the Raw Input model: "The raw input API provides a stable and robust way for
applications to accept raw input from any HID, including the keyboard and mouse"; it says
nothing on acceleration. S20, S23, S33, S38 and S43 are web pages whose quotes came from WebFetch
extractions on 2026-10-22; re-fetch to verify them verbatim.

## How to extend this research

**Gaps and open questions (sources).**

- XInput2 protocol spec (`XI2proto.txt`): gitlab.freedesktop.org refused the fetch (Anubis) and
  no GitHub mirror resolved; the X11 claims rest on SDL's source only.
- Apple does not document whether `NSEvent deltaX` is accelerated; no Apple page on
  `NSApplicationUpdateCycleEnabled` was found; that claim rests on the SDL commit message.
- SDL wiki pages were not read: they are generated from the same headers.
- Valve's Steam Input page states no date; check it for an SDL3-specific section.
- SDL2's `SDL_MOUSEWHEEL` fields (`preciseY`, 2.0.18+) were not pinned; #43's SDL2 wheel needs
  them from `sdl2/` and `sdl2-compat/` headers.
- To refresh the version finding, rerun S1 and S2 and filter the commits on `src/joystick/`,
  `src/events/` and the `src/video/*` input files.

**Questions that need a measurement, not a source.** On the Mac the SDL2 build is sdl2-compat on
SDL3, so "SDL2 vs SDL3" rows compare against the Linux SDL2 2.30.0 build there.

| Question | Ticket |
|---|---|
| Mouse event rate with the macOS 26 SDK: today's Mac build (sdl2-compat on Homebrew SDL3), 3.4.18, and a release with `6c2bedd5cb`, at 1 kHz polling | #43 |
| Are macOS relative deltas (`NSEvent deltaX`) accelerated? Same movement at two speeds, ratio of summed deltas | #43 |
| Does `SDL_GetJoysticks` list devices in the order SDL2's device indices did, per platform, with two joysticks and a gamepad (Linux SDL2 as the baseline)? | #44 |
| Does a HOTAS (stick + throttle) stay a joystick, not a gamepad, on each platform with HIDAPI on (driver `GetGamepadMapping`)? | #44 |
| Input-to-photon latency of the pump-then-draw-then-sleep loop, Linux SDL2 vs SDL3, vsync on and off | #43 (or a frame-timing ticket) |
| Does `SDL_HINT_MAC_PRESS_AND_HOLD`'s default ("1") show the accents popup while a key is held in play, with text input stopped? | #42 |
| Does SDL3's `latin_letters` give the same key-table hits as Linux SDL2 on Russian, Thai, AZERTY and QWERTZ layouts? | #42 |
| Raw Input vs GameInput (`SDL_HINT_WINDOWS_GAMEINPUT`) event rate and jitter on Windows | #43 |
