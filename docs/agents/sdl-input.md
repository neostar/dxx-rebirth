# SDL3 input: what SDL decides and what we decide

Research for #73, read before #42 (keyboard), #43 (mouse and wheel) and #44 (joystick and
gamepad). Pin: SDL3 3.4.18 (`tools/dev/toolchain.pins`). Every claim cites a source in
[Sources](#sources) as `[Sn file:line]`; line numbers are at the pinned tag, and the raw files are
mirrored by `tools/dev/fetch-docs.sh` into `docs/reference/` (block `# Input research (#73)` in
`tools/dev/reference-docs.txt`). Read the summary, then only the area of your ticket.

## Summary

| Area | Recommendation | Ticket |
|---|---|---|
| Version | Keep 3.4.18 (the latest release); move to the next 3.4.x patch for the macOS 26 mouse-rate fix | #43 |
| Key mapping | Keep mapping SDL keycodes to the game's key table; `event.key.key` matches SDL2's `keysym.sym` | #42 |
| Text input | Call `SDL_StartTextInput(window)` only while a text field is open; read `SDL_EVENT_TEXT_INPUT` | #42 |
| Meta / Cmd | Map LCMD/RCMD to `SDLK_LGUI`/`SDLK_RGUI`: SDL3's `SDLK_LMETA` is a different, extended key | #42 ADR |
| Mouse motion | Relative mode per window; keep SDL's unaccelerated default; one fraction accumulator | #43 |
| HiDPI | Convert window coordinates to pixels with the pixel density before menu hit tests | #43 |
| Wheel | Handle `SDL_EVENT_MOUSE_WHEEL` through `integer_y`, un-flipped by `direction` | #43 |
| Gamepad/joystick | Keep the split: mapped devices via the gamepad API, the rest (HOTAS) via the joystick API | #44 |
| Saved bindings | Pin the gamepad virtual layout to SDL2's 21 buttons; today's code shifts it to 26 on SDL3 | #44 |
| Hot-plug | Open by instance ID from `SDL_EVENT_*_ADDED`; keep SDL2's flattening order for joysticks | #44 |
| Backends | Keep SDL's backend defaults (HIDAPI on; GameInput, RawInput, WGI off on Windows) | #44 |
| Pumping | Keep one `SDL_PumpEvents` + `SDL_PeepEvents` batch per frame; measure before changing | #42-#44 |
| Tests (R12) | `SDL_PushEvent` for keys, mouse and wheel; `SDL_AttachVirtualJoystick` for pads | #42-#44 |

## Version: 3.4.18 against the latest

3.4.18 (2026-10-02) is the latest SDL3 release; the releases page marks it "Latest" [S1]. The
`release-3.4.x` branch carries 41 commits after it as of 2026-10-22 [S2]. Those that touch input:

- **macOS 26 mouse rate** (`6c2bedd5cb`, 2026-10-17): "Tahoe changed the default event processing
  loop for apps built with the macOS 26 SDK to synchronize with CADisplayLink. This effectively
  restricts input polling rate to VBlank cadence, which is not desirable for games." SDL now
  registers `NSApplicationUpdateCycleEnabled` NO before NSApp starts [S3 SDL_cocoaevents.m:108-110].
  3.4.18 lacks it: a game built with the macOS 26 SDK gets mouse motion at display rate.
- `ea764208b4` raw keyboard key-down detection (Windows, Hankaku/Zenkaku stuck key);
  `f699e5916c` Xbox One S buttons on old firmware; `e411c2b722` Windows joystick init no longer
  fails when DirectInput fails; `0d58c93498` Steam virtual gamepad info after a subsystem restart
  [S2].

Recommendation: keep the pin at 3.4.18 now. Move it to the next 3.4.x patch release once it
exists, if the macOS build uses the macOS 26 SDK (a measurement, below). No 3.5/3.6 tag exists
[S1, S2].

## How the game drives input today

Facts the areas below rely on (upstream code, as on `sdl-gpu-port` on 2026-10-22):

- `event_poll` pumps once, then drains up to 128 events per `SDL_PeepEvents` batch, per frame
  (`common/arch/sdl/event.cpp:118-158`). Input is read once per frame, before drawing
  (`event_process`, `event.cpp:297-342`).
- Keys: the SDL keycode (`keysym.sym`) is looked up in `key_properties`, whose index is the
  game key code (`common/arch/sdl/key.cpp:516-523`). Text for the `key_ascii` buffer is the
  keycode itself on SDL2, so it is unshifted (`key.cpp:496-513`). No `SDL_TEXTINPUT` handling.
- Mouse: `xrel`/`yrel` are summed into a virtual cursor `Mouse.x/y` (`mouse.cpp:158-170`); menus
  hit-test that cursor against canvases in pixels (`mouse.cpp:190-195`). The SDL2 build ignores the wheel:
  `SDL_MOUSEWHEEL` is unhandled (`event.cpp:160-246`). Grab sets window grab plus relative mode
  (`event.cpp:346-360`).
- Joysticks: devices are enumerated once at `joy_init` by device index; their buttons, hats
  (4 each) and axes (2 each as buttons) are flattened into one numbered list
  (`common/arch/sdl/joy.cpp:364-470`). No raw-joystick hot-plug.
- Gamepads (SDL2 only): a fixed virtual layout, buttons `0..SDL_CONTROLLER_BUTTON_MAX-1`, then 2
  axis-buttons per axis from `SDL_CONTROLLER_BUTTON_MAX` (`gamecontroller.cpp:99-102`,
  `similar/main/kconfig.cpp:124`). Hot-plug by `SDL_CONTROLLERDEVICEADDED`, whose `which` is used
  as a device index (`gamecontroller.cpp:224-253, 434-439`).
- Saved pilot bindings are one `uint8_t` per control: the game key code, mouse button or
  flattened joystick index, never a device identity (`common/main/playsave.h:173`).

## Keyboard (#42)

**What SDL3 does.** Key events carry `scancode` (physical), `key` (layout keycode), `mod`, `raw`,
`down`, `repeat`; `keysym` is gone [S4 SDL_events.h:360-373; S5 README-migration.md:387]. The
event keycode ignores the modifier state: SDL builds it with `modstate = SDL_KMOD_NONE`, adding
shift only for the French number row [S6 SDL_keyboard.c:491-510, 569]. `SDL_HINT_KEYCODE_OPTIONS`
defaults to `"french_numbers,latin_letters"`: "pressing the key associated with
SDL_SCANCODE_A on a Russian keyboard would yield 'a' instead of a Cyrillic letter" [S7
SDL_hints.h:2495-2527]. `SDL_GetKeyboardState` returns `bool *` [S5:1020].

**What it leaves to us.** Keycodes or scancodes for bindings; the meaning of non-Latin and
AZERTY layouts follows from that choice.

**Recommendation.** Keep the keycode lookup in an `#elif SDL_MAJOR_VERSION == 3` branch reading
`kevent->key` and `kevent->down`: saved bindings store game key codes found through keycodes, so
keycodes keep them meaning the same key as on SDL2, and `latin_letters` keeps Cyrillic and Thai
layouts on the letter keys. A scancode mapping would make WASD positional on AZERTY but change
saved bindings for non-US players: decide it in an ADR, not in #42. In the SDL3 branch of
`key_flush`, index the keyboard state by scancode and store by game key code: the SDL2 branch
passes `SDL_SCANCODE_CAPSLOCK` (57 [S8 SDL_scancode.h:160]) as the game key index, which is
`KEY_SPACEBAR` (0x39, `common/include/key.h:164`); an upstream-candidate fix.

## Text input (#42)

**What SDL3 does.** "Text input is no longer automatically enabled when initializing video";
starting it "may shown an input method editor (IME) and cause key up/down events to be skipped,
so should only be enabled when the application wants text input" [S5:1014]. It is per window:
`SDL_StartTextInput(SDL_Window *)` [S5:1016; S9 SDL_keyboard.h:354-378].

**Recommendation.** Keep key events as the source of game commands. For pilot names, chat and
savegame names, start text input on the main window when the field opens, stop it when it
closes, and fill `key_ascii` from `SDL_EVENT_TEXT_INPUT` (UTF-8; copy it, SDL owns the memory
[S5:375]). That also gives shifted characters, which the SDL2 path's unshifted keycode text
does not.

## Modifiers and the macOS Cmd key (#42, meta-key ADR)

**What SDL3 does.** Cmd is `SDL_SCANCODE_LGUI`/`RGUI`, named "Left Command" on macOS
[S8 SDL_scancode.h:341; `src/video/cocoa/SDL_cocoakeyboard.m:409` in the tarball]. `SDLK_LGUI` is `0x400000e3u`; `SDLK_LMETA` is a
separate "Extended key Left Meta", `0x20000004u` [S11 SDL_keycode.h:275, 317]. SDL's Cocoa
backend installs the app menu with key equivalents Cmd-H, Cmd-Q (terminate), Cmd-W, Cmd-M and
Ctrl-Cmd-F [S10 SDL_cocoaevents.m:418-505]. `SDL_HINT_MAC_OPTION_AS_ALT` only affects IME text
[S7:2690-2701].

**The trap.** `key.cpp:273-278` defines `SDLK_LMETA` as `SDLK_UNKNOWN` for SDL2 only. On SDL3
the table compiles against SDL3's own `SDLK_LMETA`, so LCMD/RCMD silently map to keys a Mac
keyboard never sends.

**Recommendation for the ADR.** Map LCMD/RCMD to `SDLK_LGUI`/`SDLK_RGUI` on SDL3, so Cmd binds
like Ctrl and Alt (`KEY_METAED` already exists, `key.cpp:404-405, 471-472`). Record that Cmd-Q,
Cmd-H, Cmd-M and Cmd-W still go to the menu first, so Cmd is a modifier for bindings, not a fire
button. Whether Cmd-Q in play should quit is the owner's call.

## Mouse motion: relative mode and acceleration (#43)

**What SDL3 does.** Motion is float, "sub-pixel motion depending on the platform and display
scaling" [S5:377; S4:453-465]. `SDL_SetRelativeMouseMode` is replaced by
`SDL_SetWindowRelativeMouseMode(window, enabled)` [S5:1180; S12 SDL_mouse.h:450-476], and
`SDL_SetWindowGrab` by `SDL_SetWindowMouseGrab` (+ `SDL_SetWindowKeyboardGrab`, which replaces
`SDL_HINT_GRAB_KEYBOARD`) [S5:828, 2222]. In relative mode SDL applies the system
acceleration curve only if `SDL_HINT_MOUSE_RELATIVE_SYSTEM_SCALE` is set ("0": "Relative mouse
motion will be unscaled. (default)") [S7:2920-2934; S13 SDL_mouse.c:731-746].
`SDL_SetRelativeMouseTransform` (3.4.0) replaces both scale hints with a callback that "may be a
thread separate from the main event loop that is run at realtime priority" [S12:190-211, 448].

Per platform, relative mode with the default hints:

| Platform | Source of deltas | Citation |
|---|---|---|
| Windows | Raw Input on a dedicated thread at `THREAD_PRIORITY_TIME_CRITICAL` | [S14 SDL_windowsrawinput.c:57-102; S15 SDL_windowsmouse.c:693-696] |
| X11 | XInput2 `XI_RawMotion`, `raw_values` when the system-scale hint is unset | [S16 SDL_x11xinput2.c:101-117; SDL_x11mouse.c:450-456] |
| Wayland | `zwp_relative_pointer_v1` `dx_unaccel`, pointer locked with `zwp_pointer_constraints_v1` | [S17 SDL_waylandevents.c:1192-1198, 3724-3809] |
| macOS | `NSEvent deltaX/deltaY` with the cursor disassociated (`CGAssociateMouseAndMouseCursorPosition(NO)`) | [S18 SDL_cocoamouse.m:361-389, 603-614] |

The Wayland protocol warns that "the non-accelerated delta does not represent 'raw' events as
they were read from some device" [S19 relative-pointer-unstable-v1.xml:101-108]. Apple
documents `deltaX` only as "The x-coordinate change for scroll wheel, mouse-move, mouse-drag,
and swipe events", not whether it is accelerated [S20]. SDL's Cocoa backend does not use
`GCMouse`: "We could use GCMouse and GCKeyboard if we needed to, as is done in
SDL_uikitevents.m" [S21 SDL_cocoavideo.m:213].

**What it leaves to us.** Whether to accelerate, float-to-int conversion, and when relative
mode is on.

**Recommendation.** Use `SDL_SetWindowRelativeMouseMode` plus `SDL_SetWindowMouseGrab` in the
SDL3 branch of `event_change_focus`. Leave the scale hints and the transform unset: Windows, X11
and Wayland then give unaccelerated deltas, the usual choice for mouse look; offer acceleration
later as a pilot option through the system-scale hint if players ask. Convert `xrel`/`yrel` in
the one fraction-accumulating helper ADR 3 names, carrying the remainder per axis, so slow
movement is not truncated to zero. Ignore SDL's internal `SDL_MOUSE_INTEGER_MODE` hint
[S13:282]: it is undocumented in `SDL_hints.h` and exists for sdl2-compat.

## Cursor coordinates, HiDPI and warp (#43)

**What SDL3 does.** Mouse positions are window coordinates; "The macOS and iOS coordinate system
always deals in window coordinates", Windows in physical pixels; without the 2D renderer,
convert "using `SDL_GetWindowPixelDensity()` as scale factor" [S22 README-highdpi.md:25-37].
Warps in relative mode emit no motion by default (`SDL_HINT_MOUSE_RELATIVE_WARP_MOTION`), and SDL
may emulate warps with relative mode (`SDL_HINT_MOUSE_EMULATE_WARP_WITH_RELATIVE`)
[S7:2840-2860, 2940-2953].

**Recommendation.** The game's virtual cursor (summed `xrel`) and `mouse_flush`'s
`SDL_GetMouseState` are window coordinates; multiply by the pixel density before comparing with
canvases in pixels (the macOS build sets `SDL_WINDOW_ALLOW_HIGHDPI`,
`similar/arch/ogl/gr.cpp:861-863`). Do this in the same `dcx::sdl` helper. The game never warps;
keep it that way.

## Mouse wheel (#43)

**What SDL3 does.** `SDL_MouseWheelEvent` has float `x`/`y` and, since 3.2.12, `integer_x`/
`integer_y` "accumulated to whole scroll 'ticks'"; `direction` FLIPPED means "the values in X and
Y will be opposite. Multiply by -1 to change them back" [S4:492-506]. SDL accumulates the
residual itself [S13:1048-1075]. macOS: precise (trackpad) deltas are scaled by 0.1, notched
deltas rounded away from zero, and FLIPPED set from `isDirectionInvertedFromDevice` [S18:617-648];
Apple: deltas "are automatically inverted ... according to the user's preferences" [S23].
Momentum is off by default (`SDL_HINT_MAC_SCROLL_MOMENTUM`) [S7:2705-2716]. Wayland feeds
`axis_value120`, where "each multiple of 120 represent[s] one logical scroll step" [S17:1093-1130;
S24 wayland.xml:2466-2476].

**Recommendation.** Handle `SDL_EVENT_MOUSE_WHEEL` on SDL3 (and `SDL_MOUSEWHEEL` on SDL2, the R8
exception ADR 3 records): send one `z_up`/`z_down` per unit of `integer_y`, multiplied by -1 when
`direction` is FLIPPED, so a weapon-cycle binding follows the physical wheel, not the user's
natural-scrolling setting. Keep momentum off.

## Gamepad and joystick API, mapping database, backends (#44)

**What SDL3 does.** A joystick is a gamepad when a mapping exists: from the database, else
generated for HIDAPI, RawInput, WGI or virtual devices [S25 SDL_gamepad.c:1450-1475, 2366-2389,
3226-3245]. Flight sticks and throttles are typed (`SDL_JOYSTICK_TYPE_FLIGHT_STICK`, `THROTTLE`)
and get no automatic mapping [S26 SDL_joystick.h:130-135; S27 SDL_joystick.c:3384-3427]. Face
buttons are positional (SOUTH/EAST/WEST/NORTH); labels come from `SDL_GetGamepadButtonLabel`, and
"Applications should provide a way for users to swap between South/East as their accept/cancel
buttons" [S5:506]. Mapping DB lines with SDL2's `platform:Mac OS X` still match on macOS
[S25:2391-2400]; SDL_GameControllerDB is "compatible with SDL v2.0.10 and newer" and documents
`SDL_AddGamepadMappingsFromFile` for SDL3 [S28 README.md:15-18]. Driver priority: HIDAPI first,
then GameInput, RawInput, DirectInput/XInput, WGI on Windows; IOKit and GameController
(`SDL_IOS_JoystickDriver`) on macOS [S27:51-111]. Defaults: HIDAPI on for desktop [S29
SDL_hidapijoystick_c.h:60-70]; GameInput off outside GDK [S30 SDL_gameinputjoystick.cpp:30-35];
RawInput, WGI off; XInput on [S31 SDL_rawinputjoystick.c:1025, SDL_windows_gaming_input.c:589,
SDL_xinputjoystick.c:49; S32 WhatsNew.txt:111, 117].

**Superseded advice.** Microsoft: "Use the GameInput API for all new code" and calls it "a
functional superset of all legacy input APIs—XInput, DirectInput, Raw Input, Human Interface
Device (HID), and WinRT APIs" [S33]. SDL 3.4 leaves GameInput off for desktop Windows, for pads
and for keyboard/mouse ("SDL_HINT_WINDOWS_GAMEINPUT is disabled by default" [S32:78; S7:4570-4580]).
The advice applies to code calling Windows directly; through SDL it changes nothing unless we
set the hint, which we should not without a measurement.

**Recommendation.** Keep the upstream split in SDL3 branches: gamepads through `SDL_Gamepad`
(fixed layout, menu keys), everything else, including HOTAS and throttles, through
`SDL_Joystick` with all axes, buttons and hats. Keep loading `gamecontrollerdb.txt` (now
`SDL_AddGamepadMappingsFromIO`); SDL3's `SDL_GetBasePath` returns `const char *` that must not
be freed [S34 SDL_filesystem.h:101], so the SDL3 branch drops `SDL_free(base)`. Set no backend
hints. Rumble (`SDL_RumbleGamepad`, `SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN` [S35 SDL_gamepad.h:798-817,
1536]) is out of scope for #44: the game has none today.

## Saved bindings across SDL2 and SDL3 (#44)

**The break.** The gamepad virtual layout starts axis-buttons at `SDL_CONTROLLER_BUTTON_MAX`.
SDL2 2.30's enum runs `SDL_CONTROLLER_BUTTON_A` to `TOUCHPAD`, 21 buttons [S36
SDL_gamecontroller.h:733-754]; under `SDL_ENABLE_OLD_NAMES` the same name is
`SDL_GAMEPAD_BUTTON_COUNT` [S37 SDL_oldnames.h:199], 26 buttons (`SOUTH` to `MISC6`) [S35:156-182].
Every saved axis-button index moves: the D1 default "Fire primary" is
`GC_AXIS_BUTTON(SDL_CONTROLLER_AXIS_TRIGGERRIGHT)` (`kconfig.cpp:557`), 21 + 2×5 = 31 on SDL2; on
SDL3 index 31 is axis 2 positive (right stick X).

**Silent renames.** Old names that compile but change argument meaning: `SDL_JoystickOpen` →
`SDL_OpenJoystick(SDL_JoystickID)`, `SDL_GameControllerOpen` → `SDL_OpenGamepad(SDL_JoystickID)`,
`SDL_IsGameController` → `SDL_IsGamepad(SDL_JoystickID)` [S37:262, 273, 347; S35:556, 754;
S26:395]: a device index passed there is now an instance ID. `SDL_JoystickID` is `Uint32` with 0
invalid [S5:909], so `instance_id{-1}` (`gamecontroller.cpp:69`) is a valid-looking ID.
`SDL_NumJoysticks` fails to compile [S37:1007], which forces the enumeration rewrite.

**Recommendation.** In SDL3 branches: a constant 21 for the axis-button start (SDL2's layout,
with a `static_assert` against the SDL2 header value on SDL2 builds), SDL3's extra buttons
`MISC2..MISC6` placed after the axis-buttons; a unit test that pins indices 0-32 to SDL2's
meaning. Joystick flattening keeps SDL2's order; whether SDL3's `SDL_GetJoysticks` order equals
SDL2's device-index order per platform is a measurement (below). Storing a device GUID beside
the bindings would make multi-device HOTAS setups robust, but it is a save-format change: an ADR
after #44, not part of it.

## Hot-plug, device IDs and Steam Input (#44)

**What SDL3 does.** `SDL_EVENT_JOYSTICK_ADDED` and `GAMEPAD_ADDED` carry the instance ID
[S5:502, 933] and are sent "for devices that are already plugged in during SDL_Init" [S4:589,
663]. "Your application should always support gamepad hotplugging ... like macOS and Windows when
using Windows.Gaming.Input, controllers may not be available at startup" [S35:68-75]. Steam:
when Steam sends gamepad input "it will appear in your game as regular Xbox controller input",
and Valve asks for "SDL version 2.0.8 or above" so Steam can make SDL ignore controllers Steam
Input handles [S38]; SDL reads Steam's `SteamVirtualGamepadInfo` file to report the real device
[S39 SDL_steam_virtual_gamepad.c:125-149; S27:3272-3279].

**Recommendation.** Open gamepads from `SDL_EVENT_GAMEPAD_ADDED` by instance ID and drop the
startup scan's duplicate guard only once the event path is tested. Raw joysticks: also open from
`SDL_EVENT_JOYSTICK_ADDED` (gamepads excluded) so a macOS HOTAS that appears after start still
works; append it to the flattened list rather than re-sorting, so live bindings do not move
mid-game. Under Steam, expect a virtual Xbox pad; nothing to configure.

## Event pumping, latency and frame timing (#42-#44)

**What SDL3 does.** `SDL_PumpEvents` runs the video backend's pump and then
`SDL_UpdateJoysticks` (when `SDL_HINT_AUTO_UPDATE_JOYSTICKS` is on, the default); it calls
`SDL_PumpEventsInternal(false)`, which pushes no poll sentinel [S40 SDL_events.c:1480-1546; S7:595-604]. Event
timestamps are nanoseconds from `SDL_GetTicksNS` [S5:369]. `SDL_DelayPrecise` busy-waits to the
requested time [S41 SDL_timer.h:284-299]. Joystick events stop when the app is in the
background unless `SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS` [S7:1280-1292].

**Recommendation.** Keep `event_poll`'s pump-then-peep loop with the SDL3 range
`SDL_EVENT_FIRST..SDL_EVENT_LAST`. The frame limiter (`timer_delay_bound`, `timer.cpp:65-85`)
sleeps after drawing, so input waits up to one frame plus the sleep; any change there (pumping
after the sleep, `SDL_DelayPrecise`) needs the latency measurement below first.

## Injected events and virtual devices (R12)

**What SDL3 offers.** `SDL_PushEvent` queues an event; set `event.common.timestamp` or SDL fills
it [S5:373]. Pushed events do not update SDL's keyboard or mouse state, so code reading
`SDL_GetKeyboardState` (`key_flush`) is not exercised that way. `SDL_AttachVirtualJoystick`
returns an instance ID; initialise its descriptor with `SDL_INIT_INTERFACE` [S5:937-939]. SDL's own
test sets `SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS` "1", attaches a virtual pad, sets a button
and calls `SDL_UpdateJoysticks` [S42 testautomation_joystick.c:19, 44-52, 103-112]. Virtual
joysticks take their gamepad mapping from the virtual driver [S25:1470-1471].

**Recommendation.** Key, mouse and wheel tests push SDL3 events into `process_event_batch` and
compare the game events with the SDL2 build's (R12); the debug harness's
`harness_push_input` (`event.cpp:88-113`) needs an SDL3 branch (`event.key.down`,
`event.key.key`). Gamepad tests attach a virtual gamepad and a virtual 3-axis, 32-button
flight stick, with the background-events hint, on the dummy video driver.

## Design: the input module

Today input is shallow: each handler reads SDL structs and calls `event_send` directly, so every
SDL3 divergence is a branch in four upstream files. ADR 3 keeps those branches; the depth goes
into a few `dcx::sdl` helpers in fork code, behind `common/sdl/dxx-sdl.h`.

- **Interface.** The game keeps its own vocabulary: `d_event_keycommand` (game key code),
  `d_event_mouse_moved` (integer dx, dy, dz), joystick button and axis events with flattened
  indices. Nothing above `common/arch/sdl` sees an SDL type; that is today's interface and stays.
- **Seam.** The SDL3 branches in `key.cpp`, `mouse.cpp`, `joy.cpp`, `gamecontroller.cpp` and
  `event.cpp` call `dcx::sdl` helpers; each helper is a deep module with a one-line interface:
  `motion_accumulator` (float deltas in, integer deltas out, remainder kept);
  `window_to_pixels` (pixel density); `wheel_steps(const SDL_MouseWheelEvent &)` (integer steps,
  un-flipped); `gamepad_virtual_index(button)` / `axis_button_index(axis, sign)` (SDL2's 21-button
  layout); `key_from_event(const SDL_KeyboardEvent &)` (keycode, Cmd as GUI). The helpers hold
  the leverage: one place per rule, unit-testable without a window.
- **Platform differences.** Only through SDL hints and queries set in one place
  (`similar/arch/sdl/init.cpp`, where `SDL_SetHint` calls live today), never `#ifdef __APPLE__`
  in input code. The macOS UpdateCycle fix comes from SDL, not from game code.
- **Test seam.** Two adapters at the SDL boundary: `SDL_PushEvent` into the real queue for
  keyboard and mouse, `SDL_AttachVirtualJoystick` for pads. Both run real SDL on headless
  drivers (ADR 3), so the tests cover the SDL3 branches and the helpers together (locality: a
  failing test names one handler and one helper).

## Sources

Accessed 2026-10-22. Raw files are pinned with SHA-256 in `tools/dev/reference-docs.txt` (block
`# Input research (#73)`; S5, S7, S22 in the earlier SDL block) and mirrored under
`docs/reference/` by `tools/dev/fetch-docs.sh`. SDL files are at tag `release-3.4.18` (commit
829a65d769d935c4852f8159e964312c0957260a, released 2026-10-02); headers and sources state no
date of their own. Line numbers were checked against the release tarball pinned in
`toolchain.pins`, whose files are byte-identical to the tag's raw files.

| Id | Source | Version | Stated date |
|---|---|---|---|
| S1 | https://github.com/libsdl-org/SDL/releases (page and `gh api repos/libsdl-org/SDL/releases`) | — | 3.4.18 published 2026-10-02T22:43:21Z |
| S2 | `gh api repos/libsdl-org/SDL/compare/release-3.4.18...release-3.4.x` | branch head 2026-10-22 | commit dates 2026-10-02..10-18 |
| S3 | `sdl3/input/3.4.x-6c2bedd5/SDL_cocoaevents.m`, commit https://github.com/libsdl-org/SDL/commit/6c2bedd5cb0194b0258d2ec13466fbc5f631ad8c | 6c2bedd5cb | committed 2026-10-17 |
| S4 | `sdl3/input/SDL_events.h` | 3.4.18 | none |
| S5 | `sdl3/README-migration.md` | 3.4.18 | none |
| S6 | `sdl3/input/src/events/SDL_keyboard.c` | 3.4.18 | none |
| S7 | `sdl3/SDL_hints.h` | 3.4.18 | none |
| S8 | `sdl3/input/SDL_scancode.h` | 3.4.18 | none |
| S9 | `sdl3/input/SDL_keyboard.h` | 3.4.18 | none |
| S10 | `sdl3/input/src/video/cocoa/SDL_cocoaevents.m` (and `SDL_cocoakeyboard.m:409-411` in the tarball) | 3.4.18 | none |
| S11 | `sdl3/input/SDL_keycode.h` | 3.4.18 | none |
| S12 | `sdl3/input/SDL_mouse.h` | 3.4.18 | none |
| S13 | `sdl3/input/src/events/SDL_mouse.c` | 3.4.18 | none |
| S14 | `sdl3/input/src/video/windows/SDL_windowsrawinput.c` | 3.4.18 | none |
| S15 | `sdl3/input/src/video/windows/SDL_windowsmouse.c` | 3.4.18 | none |
| S16 | `sdl3/input/src/video/x11/SDL_x11xinput2.c`, `SDL_x11mouse.c` | 3.4.18 | none |
| S17 | `sdl3/input/src/video/wayland/SDL_waylandevents.c` | 3.4.18 | none |
| S18 | `sdl3/input/src/video/cocoa/SDL_cocoamouse.m` | 3.4.18 | none |
| S19 | `sdl3/input/wayland-protocols/relative-pointer-unstable-v1.xml` (SDL's bundled copy) | 3.4.18 | none |
| S20 | https://developer.apple.com/documentation/appkit/nsevent/deltax (JSON: `/tutorials/data/documentation/appkit/nsevent/deltax.json`) | — | none ("Copyright © 2026") |
| S21 | `sdl3/input/src/video/cocoa/SDL_cocoavideo.m` | 3.4.18 | none |
| S22 | `sdl3/README-highdpi.md` | 3.4.18 | none |
| S23 | https://developer.apple.com/documentation/appkit/nsevent/isdirectioninvertedfromdevice | macOS 10.7+ | none |
| S24 | `sdl3/input/wayland-protocols/wayland.xml` (SDL's bundled copy) | 3.4.18 | none |
| S25 | `sdl3/input/src/joystick/SDL_gamepad.c` | 3.4.18 | none |
| S26 | `sdl3/input/SDL_joystick.h` | 3.4.18 | none |
| S27 | `sdl3/input/src/joystick/SDL_joystick.c` | 3.4.18 | none |
| S28 | `gamecontrollerdb/README.md` | ff333823a85a | head commit 2026-10-19 |
| S29 | `sdl3/input/src/joystick/hidapi/SDL_hidapijoystick_c.h` | 3.4.18 | none |
| S30 | `sdl3/input/src/joystick/gdk/SDL_gameinputjoystick.cpp` | 3.4.18 | none |
| S31 | `sdl3/input/src/joystick/windows/{SDL_rawinputjoystick.c,SDL_windows_gaming_input.c,SDL_xinputjoystick.c}` | 3.4.18 | none |
| S32 | `sdl3/input/WhatsNew.txt` (3.4.0 section) | 3.4.18 | none |
| S33 | https://learn.microsoft.com/en-us/gaming/gdk/docs/features/common/input/overviews/input-overview | gdk-2604 | ms.date 2022-08-01, edited 2026-02-19, updated_at 2026-05-12 |
| S34 | `sdl3/input/SDL_filesystem.h` | 3.4.18 | none |
| S35 | `sdl3/input/SDL_gamepad.h` | 3.4.18 | none |
| S36 | `sdl2/input/SDL_gamecontroller.h` | SDL2 release-2.30.0 | none |
| S37 | `sdl3/input/SDL_oldnames.h` | 3.4.18 | none |
| S38 | https://partner.steamgames.com/doc/features/steam_controller/getting_started_for_devs | — | none stated |
| S39 | `src/joystick/SDL_steam_virtual_gamepad.c` (tarball only, not pinned) | 3.4.18 | none |
| S40 | `sdl3/input/src/events/SDL_events.c` | 3.4.18 | none |
| S41 | `sdl3/input/SDL_timer.h` | 3.4.18 | none |
| S42 | `sdl3/input/test/testautomation_joystick.c` | 3.4.18 | none |
| S43 | https://learn.microsoft.com/en-us/windows/win32/inputdev/about-raw-input | git 376575d2 | ms.date 2025-07-14, updated_at 2026-03-18 |

Also pinned for the areas above: `SDL_video.h` (`SDL_SetWindowKeyboardGrab` 2580,
`SDL_SetWindowMouseGrab` 2600), `src/video/wayland/SDL_waylandmouse.c` (warp needs
`wp_pointer_warp_v1` or pointer confinement, 1327-1334). S43 backs the Raw Input model: "The raw
input API provides a stable and robust way for applications to accept raw input from any HID,
including the keyboard and mouse"; it says nothing on acceleration. S20, S23, S33, S38 and S43
are web pages: their quotes above came from WebFetch extractions on 2026-10-22; re-fetch to
verify.

## How to extend this research

**Gaps and open questions (sources).**

- XInput2 protocol spec (`XI2proto.txt`): gitlab.freedesktop.org refused the fetch (Anubis) and
  no GitHub mirror resolved; the X11 claims rest on SDL's source only.
- Apple does not document whether `NSEvent deltaX` is accelerated; no Apple page on
  `NSApplicationUpdateCycleEnabled` was found; the claim rests on the SDL commit.
- SDL wiki pages were not read: they are generated from the same headers (S4-S37).
- Valve's Steam Input page states no date; check it for an SDL3-specific section.
- SDL2's `SDL_MOUSEWHEEL` fields (`preciseY`, 2.0.18+) were not pinned; #43's SDL2 wheel needs
  the SDL2 header at the Linux pin (2.30.0) and the macOS Homebrew version.

**Questions that need a measurement, not a source.**

| Question | Ticket |
|---|---|
| Is the macOS build linked against the macOS 26 SDK, and does 3.4.18 cap mouse motion at the display rate there? Event count per second at 1 kHz polling, 3.4.18 vs a 3.4.x build with `6c2bedd5cb` | #43 |
| Are macOS relative deltas (`NSEvent deltaX`) accelerated? Same physical movement at two speeds, ratio of summed deltas | #43 |
| Does `SDL_GetJoysticks` list devices in the order SDL2's device indices did, per platform, with two joysticks and a gamepad? | #44 |
| Does a HOTAS (stick + throttle) stay a joystick, not a gamepad, on each platform with HIDAPI on? | #44 |
| Input-to-photon latency of the current pump-then-draw-then-sleep loop, SDL2 vs SDL3, vsync on and off | #43 (or a frame-timing ticket) |
| Does `SDL_HINT_MAC_PRESS_AND_HOLD`'s default ("1") show the accents popup while a key is held in play, with text input stopped? | #42 |
| Does SDL3's `latin_letters` give the same key-table hits as SDL2 on Russian, Thai, AZERTY and QWERTZ layouts? | #42 |
| Raw Input vs GameInput (`SDL_HINT_WINDOWS_GAMEINPUT`) event rate and jitter on Windows | #43 |
