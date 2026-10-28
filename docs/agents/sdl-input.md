# SDL3 input: what SDL decides and what we decide

Research for #73, read before #42 (keyboard), #43 (mouse and wheel) and #44 (joystick and
gamepad). Pin: SDL3 3.4.18 (`tools/dev/toolchain.pins`). Claims cite `[Sn file:line]`; the
sources, their dates and pins, and the open questions are in `sdl-input-sources.md` beside this
file. Window, HiDPI and SDL version evidence live in `sdl-window.md`; this doc keeps only their
input consequences. Read the summary, then only the area of your ticket.

## Summary

| Area | Recommendation | Ticket |
|---|---|---|
| Version | Keep 3.4.18; the next 3.4.x patch carries the macOS 26 mouse-rate fix | #43 |
| Key mapping | Keycodes → game key table; `event.key.key` matches SDL2's `keysym.sym` | #42 |
| Text input | `SDL_StartTextInput(window)` only while a text field is open | #42 |
| Meta / Cmd | Map LCMD/RCMD to `SDLK_LGUI`/`SDLK_RGUI`; SDL3's `SDLK_LMETA` differs | #42 ADR |
| Mouse motion | Relative mode per window, SDL's unscaled default, one fraction accumulator | #43 |
| Cursor | Window coordinates to pixels before menu hit tests (`sdl-window.md`, #48) | #43 |
| Wheel | One step per unit of `integer_y`, un-flipped by `direction` | #43 |
| Gamepad/joystick | Mapped devices via the gamepad API, the rest (HOTAS) via joystick | #44 |
| Saved bindings | Pin the gamepad virtual layout to SDL2's 21 buttons (SDL3: 26) | #44 |
| Hot-plug | Open by instance ID from `SDL_EVENT_*_ADDED`; append new joysticks | #44 |
| Backends | SDL's defaults: HIDAPI on (its Xbox drivers off on Windows); GameInput, RawInput, WGI off | #44 |
| Pumping | One `SDL_PumpEvents` + `SDL_PeepEvents` batch per frame; measure first | #42-#44 |
| Tests (R12) | `SDL_PushEvent` for keys, mouse, wheel; virtual joysticks for pads | #42-#44 |
| Platform-only hints | Set `MAC_PRESS_AND_HOLD` and `JOYSTICK_ENHANCED_REPORTS`; test mapping fixtures need the platform field; leave the rest (table) | #42, #44 |

## Version and the SDL2 baselines

SDL3: 3.4.18 is the latest release; `sdl-window.md` ("SDL version") holds the evidence and the
pin recommendation. The input consequence: commit `6c2bedd5cb` on `release-3.4.x`, after
3.4.18, opts out of macOS 26's UpdateCycle [S3 SDL_cocoaevents.m:108-110]. Per its commit
message, for apps built with the macOS 26 SDK "This effectively restricts input polling rate to
VBlank cadence" [S3]. #43 measures the mouse event rate before and after (sources doc). Two more
3.4.x commits after 3.4.18 add or fix controllers: `e11a858318` (8BitDo Ultimate 2C Wireless via
HIDAPI) and `c26b9859f6` (a crash looking for "NES Controller") [S2].

SDL2 baselines (`toolchain.pins`):

- **macOS: the "SDL2" build runs on SDL3.** Homebrew's `sdl2` is an alias of `sdl2-compat`
  [S45 Aliases/sdl2], "SDL2 compatibility layer that uses SDL3 behind the scenes" [S45
  sdl2-compat.rb:2]; its README: "it uses SDL3 behind the scenes" [S46 README.md:10-11]. The
  Mac pin 2.32.74 is an sdl2-compat version; the formula `depends_on "sdl3"` [S45
  sdl2-compat.rb:18]. So on the Mac every "same result as SDL2" comparison (R12 below, the
  measurements in the sources doc) compares SDL3 with SDL3, and the UpdateCycle cap may already
  hit today's Mac build. Its gamepad header keeps SDL2's 21 buttons
  [S47 SDL_gamecontroller.h:761-782].
- **Linux: real SDL2 2.30.0**, the only true SDL2 baseline. The latest SDL2 release is 2.32.10
  (2025-09-01) [S44]; 2.32.10's `WhatsNew.txt` ends at 2.30.0 [S49], and the 2.32.0 release
  notes include "Improved event processing latency when gamepad/sensor is open" and "Fixed
  input for Thrustmaster PlayStation wheels when hid-tmff2 is installed" [S44]. The gamepad
  enum is unchanged, 21 buttons [S48 SDL_gamecontroller.h:747-768].
  Recommendation: leave the Linux pin, the container's snapshot; 2.32.0 calls itself "a stable
  bugfix release" and says "The SDL development team is focusing efforts on SDL3" [S44].

## How the game drives input today

Facts the areas below rely on (upstream code, as on `sdl-gpu-port` on 2026-10-22):

- `event_poll` pumps once, then drains up to 128 events per `SDL_PeepEvents` batch, per frame
  (`common/arch/sdl/event.cpp:118-158`). Input is read once per frame, before drawing
  (`event_process`, `event.cpp:297-342`).
- Keys: the SDL keycode (`keysym.sym`) is looked up in `key_properties`, whose index is the
  game key code (`common/arch/sdl/key.cpp:516-523`). Text for the `key_ascii` buffer is the
  keycode itself on SDL2, so it is unshifted (`key.cpp:496-513`). No `SDL_TEXTINPUT` handling.
- Mouse: `xrel`/`yrel` are summed into a virtual cursor `Mouse.x/y` (`mouse.cpp:158-170`); menus
  hit-test that cursor against canvases in pixels (`mouse.cpp:190-195`). The SDL2 build ignores
  the wheel: `SDL_MOUSEWHEEL` is unhandled (`event.cpp:160-246`). Grab sets window grab plus
  relative mode (`event.cpp:346-360`).
- Joysticks: devices are enumerated once at `joy_init` by device index; their buttons, hats
  (4 each) and axes (2 each as buttons) are flattened into one numbered list
  (`common/arch/sdl/joy.cpp:364-470`). No raw-joystick hot-plug.
- Gamepads (SDL2 only): a fixed virtual layout, buttons `0..SDL_CONTROLLER_BUTTON_MAX-1`, then 2
  axis-buttons per axis from `SDL_CONTROLLER_BUTTON_MAX`, positive first, negative second
  (`gamecontroller.cpp:99-102, 165-168, 320-329`, `similar/main/kconfig.cpp:124`). Hot-plug by
  `SDL_CONTROLLERDEVICEADDED`, whose `which` is used as a device index
  (`gamecontroller.cpp:224-253, 434-439`).
- Saved pilot bindings are one `uint8_t` per control: the game key code, mouse button or
  flattened joystick index, never a device identity (`common/main/playsave.h:173`).

## Keyboard (#42)

**What SDL3 does.** Key events carry `scancode` (physical), `key` (layout keycode), `mod`, `raw`,
`down`, `repeat`; `keysym` is gone [S4 SDL_events.h:360-373; S5 README-migration.md:387]. The
event keycode ignores the modifier state: SDL builds it with `modstate = SDL_KMOD_NONE`, adding
shift only for the French number row [S6 SDL_keyboard.c:491-510, 569].
`SDL_HINT_KEYCODE_OPTIONS` defaults to `"french_numbers,latin_letters"`: "pressing the key
associated with SDL_SCANCODE_A on a Russian keyboard would yield 'a' instead of a Cyrillic
letter" [S7 SDL_hints.h:2495-2527]. `SDL_GetKeyboardState` returns `bool *` [S5:1020].

**What it leaves to us.** Keycodes or scancodes for bindings; the meaning of non-Latin and
AZERTY layouts follows from that choice.

**Recommendation.** Keep the keycode lookup in an `#elif SDL_MAJOR_VERSION == 3` branch reading
`kevent->key` and `kevent->down`: saved bindings store game key codes found through keycodes, so
keycodes keep them meaning the same key as on SDL2, and `latin_letters` keeps Cyrillic and Thai
layouts on the letter keys. A scancode mapping would make WASD positional on AZERTY but change
saved bindings for non-US players: decide it in an ADR, not in #42.

In the SDL3 branch of `key_flush`, index the keyboard state by scancode and store by game key
code. The SDL2 branch passes each SDL scancode as the game key index [S8 SDL_scancode.h:160,
176, 190; `common/include/key.h:164, 201, 206`]:

| SDL scancode | Value | Game key at that index |
|---|---|---|
| `SDL_SCANCODE_CAPSLOCK` | 57 | `KEY_SPACEBAR` (0x39) |
| `SDL_SCANCODE_SCROLLLOCK` | 71 | `KEY_PAD7` (0x47) |
| `SDL_SCANCODE_NUMLOCKCLEAR` | 83 | `KEY_PADPERIOD` (0x53) |

A fix to offer upstream as its own commit.

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

**What SDL3 does.** Cmd is `SDL_SCANCODE_LGUI`/`RGUI` [S8 SDL_scancode.h:341], named "Left
Command" on macOS [S10b SDL_cocoakeyboard.m:409-411]. `SDLK_LGUI` is `0x400000e3u`;
`SDLK_LMETA` is a separate "Extended key Left Meta", `0x20000004u` [S11 SDL_keycode.h:275, 317].
SDL's Cocoa backend installs the app menu with key equivalents Cmd-H, Cmd-Q (terminate), Cmd-W,
Cmd-M and Ctrl-Cmd-F [S10 SDL_cocoaevents.m:418-505]. `SDL_HINT_MAC_OPTION_AS_ALT` only affects
IME text [S7:2690-2701].

**The trap.** `key.cpp:273-278` defines `SDLK_LMETA` as `SDLK_UNKNOWN` for SDL2 only. On SDL3
the table compiles against SDL3's own `SDLK_LMETA`, so LCMD/RCMD silently map to keys a Mac
keyboard never sends.

**Recommendation for the ADR.** Map LCMD/RCMD to `SDLK_LGUI`/`SDLK_RGUI` on SDL3, so Cmd binds
like Ctrl and Alt (`KEY_METAED` already exists, `key.cpp:404-405, 471-472`). Record that Cmd-Q,
Cmd-H, Cmd-M and Cmd-W reach the game as `KEY_COMMAND` plus the key and also run their menu
action (ADR 7), so a binding on them fires beside the action. Whether Cmd-Q in play should quit is the owner's call.

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

Per platform, relative mode with the default hints (S14 `SDL_windowsrawinput.c`, S16
`SDL_x11xinput2.c`, S17 `SDL_waylandevents.c`, S18 `SDL_cocoamouse.m`):

| Platform | Source of deltas | Cite |
|---|---|---|
| Windows | Raw Input on a thread at `THREAD_PRIORITY_TIME_CRITICAL` | S14:57-102 |
| X11 | XInput2 `XI_RawMotion`, `raw_values` with the system-scale hint unset | S16:101-117 |
| Wayland | `zwp_relative_pointer_v1` `dx_unaccel`, pointer locked | S17:1192-1198, 3724-3809 |
| macOS | `NSEvent deltaX/deltaY`, cursor disassociated | S18:361-389, 603-614 |

On Windows relative mode is raw input (`WIN_SetRelativeMouseMode` calls `WIN_SetRawMouseEnabled`
[S15 SDL_windowsmouse.c:693-696]), and the system curve comes only from `WIN_ApplySystemScale`,
which SDL registers as the platform's `ApplySystemScale` [S15:737, 840] and calls only under the
hint [S13:736-739]; unscaled deltas by default are our inference from that code path. X11 picks
`raw_values` unless the hint is set [S16:106-117]. The Wayland protocol warns that "the
non-accelerated delta does not represent 'raw' events as they were read from some device" [S19
relative-pointer-unstable-v1.xml:101-108]. Apple documents `deltaX` only as "The x-coordinate
change for scroll wheel, mouse-move, mouse-drag, and swipe events", not whether it is accelerated
[S20]. SDL's Cocoa backend uses `NSEvent`, not `GCMouse`: "We could use GCMouse and GCKeyboard
if we needed to, as is done in SDL_uikitevents.m" [S21 SDL_cocoavideo.m:213].

**What it leaves to us.** Whether to accelerate, float-to-int conversion, and when relative
mode is on.

**Recommendation.** Use `SDL_SetWindowRelativeMouseMode` plus `SDL_SetWindowMouseGrab` in the
SDL3 branch of `event_change_focus`. Leave the scale hints and the transform unset: Windows, X11
and Wayland then give unscaled deltas, the usual choice for mouse look; offer acceleration
later as a pilot option through the system-scale hint if players ask. Convert `xrel`/`yrel` in
ADR 3's fraction-accumulating helper, carrying the remainder per axis, so slow movement adds up
instead of truncating to zero. SDL's own `SDL_MOUSE_INTEGER_MODE` hint [S13:282] is undocumented
in `SDL_hints.h` and serves sdl2-compat; the helper is the supported path.

## Cursor coordinates and warp (#43)

The game's virtual cursor (summed `xrel`) and `mouse_flush`'s `SDL_GetMouseState` are window
coordinates, while menus hit-test canvases in pixels; on macOS the build sets
`SDL_WINDOW_ALLOW_HIGHDPI` (`similar/arch/ogl/gr.cpp:861-863`). The conversion (pixel density)
belongs to the window module, `sdl-window.md` §4 and #48; #43 calls it before `mouse_in_window`.
Warps in relative mode emit no motion by default (`SDL_HINT_MOUSE_RELATIVE_WARP_MOTION`
[S7:2940-2953]); the game never warps, and needs no warp.

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
generated for HIDAPI, RawInput, WGI or virtual devices [S25 SDL_gamepad.c:1450-1475, 3226-3245],
else from the device's driver: `SDL_PrivateGetGamepadMapping` asks
`SDL_PrivateJoystickGetAutoGamepadMapping` [S25:2378-2382], which calls that driver's
`GetGamepadMapping` [S27 SDL_joystick.c:1657-1669]. Whether a given HOTAS gets a mapping that
way depends on the driver per platform: a measurement (sources doc). SDL types flight sticks and
throttles (`SDL_JOYSTICK_TYPE_FLIGHT_STICK`, `THROTTLE`) [S26 SDL_joystick.h:130-135;
S27:3384-3427]. Face buttons are positional (SOUTH/EAST/WEST/NORTH); labels come from
`SDL_GetGamepadButtonLabel`, and "Applications should provide a way for users to swap between
South/East as their accept/cancel buttons" [S5:506]. Mapping DB lines with SDL2's
`platform:Mac OS X` still match on macOS [S25:2391-2400]; SDL_GameControllerDB is "compatible
with SDL v2.0.10 and newer" and documents `SDL_AddGamepadMappingsFromFile` for SDL3 [S28
README.md:15-18]. Driver priority: HIDAPI first, then GameInput, RawInput, DirectInput/XInput,
WGI on Windows; IOKit and GameController (`SDL_IOS_JoystickDriver`) on macOS [S27:51-111].
Defaults: HIDAPI on for desktop [S29 SDL_hidapijoystick_c.h:60-70], except its Xbox drivers, which
default off on Windows (XInput carries those pads there; "Platform-only hints" below); GameInput off outside GDK
[S30 SDL_gameinputjoystick.cpp:30-35]; RawInput, WGI off; XInput on [S31
SDL_rawinputjoystick.c:1025, SDL_windows_gaming_input.c:589, SDL_xinputjoystick.c:49].

**Superseded advice.** Microsoft: "Use the GameInput API for all new code" and calls it "a
functional superset of all legacy input APIs—XInput, DirectInput, Raw Input, Human Interface
Device (HID), and WinRT APIs" [S33]. SDL 3.4 leaves GameInput off on desktop Windows, for pads
and for keyboard/mouse ("SDL_HINT_WINDOWS_GAMEINPUT is disabled by default" [S32 WhatsNew.txt:78;
S7:4570-4580]). The advice is for code calling Windows directly; through SDL, GameInput stays
off until we set the hint, and a measurement should come first.

**Recommendation.** Keep the upstream split in SDL3 branches: gamepads through `SDL_Gamepad`
(fixed layout, menu keys), everything else, including HOTAS and throttles, through
`SDL_Joystick` with all axes, buttons and hats. Keep loading `gamecontrollerdb.txt`:
`SDL_GameControllerAddMappingsFromRW` and `...FromFile` map to `SDL_AddGamepadMappingsFromIO`
and `SDL_AddGamepadMappingsFromFile`, which exist [S37 SDL_oldnames.h:224-225]. SDL3's
`SDL_GetBasePath` returns `const char *`, owned by SDL [S34 SDL_filesystem.h:101]; the SDL3
branch keeps the pointer and drops `SDL_free(base)`. Select no backend (no driver hint on or off); the
one gamepad hint to set, `SDL_HINT_JOYSTICK_ENHANCED_REPORTS`, is in "Platform-only hints" below. Rumble
(`SDL_RumbleGamepad`, `SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN` [S35 SDL_gamepad.h:798-817, 1536])
stays out of #44: the game has none today.

## Saved bindings across SDL2 and SDL3 (#44)

**The break.** The gamepad virtual layout starts axis-buttons at `SDL_CONTROLLER_BUTTON_MAX`,
two per axis, positive then negative (`gamecontroller.cpp:320-329`). SDL2's enum runs
`SDL_CONTROLLER_BUTTON_A` to `TOUCHPAD`, 21 buttons, in 2.30.0, 2.32.10 and sdl2-compat 2.32.74
[S36 SDL_gamecontroller.h:733-754; S48:747-768; S47:761-782]. Under `SDL_ENABLE_OLD_NAMES` the
same name is `SDL_GAMEPAD_BUTTON_COUNT` [S37:199], 26 buttons (`SOUTH` to `MISC6`) [S35:156-182].
Every saved axis-button index moves. The D1 default "Fire primary" is
`GC_AXIS_BUTTON(SDL_CONTROLLER_AXIS_TRIGGERRIGHT)` (`kconfig.cpp:557`): 21 + 2×5 = 31 on SDL2.
On SDL3 index 31 is 26 + 2×2 + 1, −RightX (right stick left).

**Renames that change meaning.** Old names that compile but now take an instance ID where the
code passes a device index: `SDL_JoystickOpen` → `SDL_OpenJoystick`, `SDL_GameControllerOpen`
→ `SDL_OpenGamepad`, `SDL_IsGameController` → `SDL_IsGamepad` [S37:262, 273, 347; S35:556,
754; S26:395]. Two neighbours fail to compile instead, so they cannot ship silently:
`SDL_JoystickID` is `Uint32` with 0 invalid [S5:909], so `instance_id{-1}`
(`gamecontroller.cpp:69`) is a narrowing error and the SDL3 branch uses 0 as its sentinel; and
`SDL_free(base)` on SDL3's `const char *` does not convert. `SDL_NumJoysticks` and
`SDL_JoystickGetDeviceInstanceID` (`joy.cpp:378`, `gamecontroller.cpp:227, 331`) are absent
from the enabled old-names block [S37:38-681], which forces the enumeration rewrite.

**Recommendation.** In SDL3 branches: a constant 21 for the axis-button start (SDL2's layout,
with a `static_assert` against the SDL2 header value on SDL2 builds), SDL3's extra buttons
`MISC2..MISC6` placed after the axis-buttons; a unit test that pins indices 0-32 to SDL2's
meaning. Joystick flattening keeps SDL2's order; whether SDL3's `SDL_GetJoysticks` order equals
SDL2's device-index order per platform is a measurement. Storing a device GUID beside the
bindings would make multi-device HOTAS setups robust, but it is a save-format change: an ADR
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

**Recommendation.** Open gamepads from `SDL_EVENT_GAMEPAD_ADDED` by instance ID; the startup
scan's duplicate guard can go once the event path is tested. Raw joysticks: also open from
`SDL_EVENT_JOYSTICK_ADDED` (gamepads excluded) so a macOS HOTAS that appears after start still
works; append it to the flattened list rather than re-sorting, so live bindings stay put
mid-game. Under Steam, expect a virtual Xbox pad; nothing to configure.

## Event pumping, latency and frame timing (#42-#44)

**What SDL3 does.** `SDL_PumpEvents` runs the video backend's pump and then
`SDL_UpdateJoysticks` (when `SDL_HINT_AUTO_UPDATE_JOYSTICKS` is on, the default); it calls
`SDL_PumpEventsInternal(false)`, which pushes no poll sentinel [S40 SDL_events.c:1480-1546;
S7:595-604]. Event timestamps are nanoseconds from `SDL_GetTicksNS` [S5:369]. `SDL_DelayPrecise`
busy-waits to the requested time [S41 SDL_timer.h:284-299]. Joystick events stop when the app is
in the background unless `SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS` [S7:1280-1292].

**Recommendation.** Keep `event_poll`'s pump-then-peep loop with the SDL3 range
`SDL_EVENT_FIRST..SDL_EVENT_LAST`. The frame limiter (`timer_delay_bound`, `timer.cpp:65-85`)
sleeps after drawing, so input waits up to one frame plus the sleep; any change there (pumping
after the sleep, `SDL_DelayPrecise`) waits for the latency measurement.

## Injected events and virtual devices (R12)

**What SDL3 offers.** `SDL_PushEvent` queues an event; set `event.common.timestamp` or SDL fills
it [S5:373]. "Pushing device input events onto the queue doesn't modify the state of the device
within SDL" [S4:1375-1376], so code reading `SDL_GetKeyboardState` (`key_flush`) needs another
test. `SDL_AttachVirtualJoystick` returns an instance ID; initialise its descriptor with
`SDL_INIT_INTERFACE` [S5:937-939]. SDL's own test sets `SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS`
"1", attaches a virtual pad, sets a button and calls `SDL_UpdateJoysticks` [S42
testautomation_joystick.c:19, 44-52, 103-112]. Virtual joysticks take their gamepad mapping from
the virtual driver [S25:1470-1471].

**Recommendation.** Key, mouse and wheel tests push SDL3 events into `process_event_batch` and
compare the game events with the Linux SDL2 build's (on the Mac the SDL2 build is sdl2-compat on
SDL3, so it is no independent baseline). The debug harness's `harness_push_input`
(`event.cpp:88-113`) needs an SDL3 branch (`event.key.down`, `event.key.key`). Gamepad tests
attach a virtual gamepad and a virtual 3-axis, 32-button flight stick, with the background-events
hint, on the dummy video driver.

## Platform-only hints and their defaults (#42-#44)

**What SDL does.** A hint or behaviour scoped to one OS or one controller driver does nothing
on the others, so a wrong default stays invisible on the Mac and shows only where that backend
runs: Windows on GitHub, one bug per run, or a real controller by hand (#93). The scope is stated
in `R/sdl3/SDL_hints.h` ("This hint is only used on Windows", "The default is "0" on Windows",
"Linux"-named hints) and in the sources named in the table. Lines below are `SDL_hints.h` unless a
file is named; `R/` is `docs/reference/`. The set at 3.4.18, by the bar "a later ticket would set
it by accident or leave a default that is wrong for us":

| Hint or behaviour (line) | Platform or driver | Default | The game sets | Owner |
|---|---|---|---|---|
| `MAC_PRESS_AND_HOLD` (`:2725`; read at `src/video/cocoa/SDL_cocoaevents.m:108`) | macOS | 1: holding a letter opens the accents menu instead of repeating | `"0"` before `SDL_Init`, so a held key repeats in pilot-name and chat fields; manual check on the Mac (key events with text input off are unaffected) | #42 |
| `JOYSTICK_ENHANCED_REPORTS` (`:1395`; note `:1385-1389`) | HIDAPI: PlayStation (Bluetooth), Switch | 1: report mode changes on open; "can't be disabled on PlayStation controllers without power cycling the controller"; breaks Windows DirectInput for other applications | `"auto"` before `SDL_Init`: the game uses no rumble, LEDs or gyro, so SDL leaves the pad's mode alone | #44 |
| `R/sdl3/SDL_gamepad.h:361-363`; `src/joystick/SDL_gamepad.c:2434-2447` | `SDL_AddGamepadMappingsFromIO` and `...FromFile` | "Mappings not belonging to the current platform or with no platform field specified will be ignored": a line without a matching `platform:` is dropped, no error. `SDL_AddGamepadMapping` and `SDL_HINT_GAMECONTROLLERCONFIG` have no such filter (`SDL_gamepad.c:2563-2724`). macOS also accepts SDL2's `Mac OS X` (`:2394-2401`) | `gamecontrollerdb.txt` keeps loading through `FromFile`; a test fixture goes in through `SDL_AddGamepadMapping`, or carries the running platform's field (Linux container: `platform:Linux`) | #44 |
| `JOYSTICK_HIDAPI_XBOX` (`:2078`), `_XBOX_360`, `_XBOX_ONE` (`:2151`), `_GIP` (`:2187`) | HIDAPI, Windows | 0 on Windows, else the value of `JOYSTICK_HIDAPI`; each later one defaults to the one before | default: Xbox pads reach the game through XInput on Windows, through HIDAPI on macOS and Linux, with the same gamepad API | #44 |
| `XINPUT_ENABLED` (`:4775`), `JOYSTICK_DIRECTINPUT` (`:699`) | Windows | 1, 1 | default: XInput for Xbox pads, DirectInput for HOTAS; `xinput` is a reserved mapping GUID (`SDL_gamepad.h:313`) | #44 |
| `JOYSTICK_RAWINPUT` (`:2308`), `JOYSTICK_WGI` (`:2408`), `WINDOWS_GAMEINPUT` (`:4573`), `JOYSTICK_GAMEINPUT` (`:1453`) | Windows | 0, 0, 0, 0 (1 on GDK) | default; covered above | #44 |
| `WINDOWS_CLOSE_ON_ALT_F4` (`:4514`) | Windows | 1: Alt+F4 posts a window close | default; do not bind Alt+F4, SDL posts the close when it sees it | #42 |
| `WINDOWS_RAW_KEYBOARD` (`:4587`) | Windows | 0: keys through the message loop | default; low-latency raw keys are a candidate only if the latency measurement asks | #42 |
| `JOYSTICK_LINUX_CLASSIC` (`:2229`), `_LINUX_DEADZONES` (`:2244`), `_LINUX_DIGITAL_HATS` (`:2260`), `_LINUX_HAT_DEADZONES` (`:2278`) | Linux joystick driver | `/dev/input/event*`; raw unfiltered axes; hat axes digital only if they look digital; hat deadzones on | default; a HOTAS hat or throttle on Linux is checked by hand (#93), because virtual joysticks bypass this driver | #44 |
| `JOYSTICK_IOKIT` (`:2215`), `JOYSTICK_MFI` (`:2294`) | macOS | 1, 1: IOKit and GCController both on | default; no hint selects between them, so which one a given device uses is a hand check (#93) | #44 |
| `MAC_CTRL_CLICK_EMULATE_RIGHT_CLICK` (`:2645`) | macOS | 0: Ctrl+click stays a left click | default; Ctrl is a game key | #43 |
| `TRACKPAD_IS_TOUCH_ONLY` (`:3667`) | macOS | 0: trackpad sends mouse events | default | #43 |
| `HIDAPI_ENUMERATE_ONLY_CONTROLLERS` (`:1206`) | macOS (HIDAPI) | 1: avoids "macOS keyboard capture permission prompts" | default; the game never calls `SDL_hid_enumerate` | #44 |
| `ALLOW_ALT_TAB_WHILE_GRABBED` (`:60`; `src/events/SDL_keyboard.c:657-668`) | any OS with an Alt+Tab; the Mac has none | 1: with the keyboard grabbed and a fullscreen window, Alt+Tab minimizes the window | default: it is the player's way out of a keyboard grab | #43 |
| `SteamAPI_InitEx` before `SDL_Init` (`SDL_gamepad.h:51-52`); `ENABLE_STEAM_SCREEN_KEYBOARD` (`:856`, since 3.4.12) | Steam builds | not applicable until a Steam build exists; Steam sets the keyboard hint itself in Big Picture | none now; a Steam build initialises Steam first and starts text input only for a text field | none (future Steam build) |

**Set aside.**
- Per-driver HIDAPI hints for other pads (`_PS3`, `_PS5`, `_SWITCH`, `_SHIELD`, `_STADIA`, `_WII`, `_LUNA`, `_8BITDO` and the rest) and their LED hints: each defaults to `JOYSTICK_HIDAPI` (on), and none changes what a button or axis reads.
- `JOYSTICK_HIDAPI_COMBINE_JOY_CONS` (`:1545`, default 1, two Joy-Cons become one pad): device-specific, nothing to set.
- `JOYSTICK_HIDAPI_STEAM` (`:1801`, default 0, Bluetooth Steam Controllers; permission prompts on iOS and Android): mobile only.
- `HIDAPI_LIBUSB`, `_LIBUSB_WHITELIST`, `_UDEV` (`:1130`, `:1177`), `JOYSTICK_THREAD` (`:2356`, Windows RawInput only), `EVDEV_DEVICES` and `MUTE_CONSOLE_KEYBOARD` (KMSDRM and console): driver plumbing that needs a real device, none on the Mac or in the container; tested by hand with #93.
- `WINDOWS_ENABLE_MENU_MNEMONICS` (`:4543`, default 0): the default already keeps Alt free of the menu beep.
- `MAC_OPTION_AS_ALT`, `MAC_SCROLL_MOMENTUM`, `KEYCODE_OPTIONS`: correct in the sections above.
- `TIMER_RESOLUTION`: frame timing, in `sdl-window.md` section 5.
- `MOUSE_FOCUS_CLICKTHROUGH`, `MOUSE_EMULATE_WARP_WITH_RELATIVE`, `MOUSE_AUTO_CAPTURE`: not platform-scoped in the header, and the game neither warps nor depends on the focus click.

**Recommendation.** `init.cpp` sets two hints, `MAC_PRESS_AND_HOLD` (macOS only) and
`JOYSTICK_ENHANCED_REPORTS`, both before `SDL_Init`; every other row stays at SDL's default, and
a test pins that `init.cpp` sets nothing else. Test fixtures for mappings carry the platform
(table row 3). Rows with a "by hand" cell (Linux joystick driver, IOKit and GCController on real
HOTAS, Windows XInput) go to #93's rig rather than into a criterion #44 cannot meet on the Mac.
On an SDL pin bump, re-read the platform-scoped hint lists in `SDL_hints.h` and the platform
paragraphs of `SDL_gamepad.h` and add any new entry here.

## Design proposal for #42-#44

A proposal, not a decision; ADR 3 already fixes branches, `dcx::sdl` and headless-SDL tests.

- **Interface.** Unchanged: `d_event_keycommand` (game key code), `d_event_mouse_moved`
  (integer dx, dy, dz), joystick events with flattened indices. No SDL type above
  `common/arch/sdl`.
- **Deep helpers behind the seam**, each with a one-line interface and its own unit test:
  `motion_accumulator` (float deltas in, integer out, remainder kept); `wheel_steps(const
  SDL_MouseWheelEvent &)` (integer steps, un-flipped); `gamepad_virtual_index(button)` /
  `axis_button_index(axis, sign)` (SDL2's 21-button layout); `key_from_event(const
  SDL_KeyboardEvent &)` (keycode, Cmd as GUI). One place per rule gives the leverage; locality
  means a failing test names one handler and one helper.
- **Platform differences** only as hints and queries in `similar/arch/sdl/init.cpp`, where the
  `SDL_SetHint` calls live today; input code stays free of platform `#ifdef`s.
- **Test adapters.** `SDL_PushEvent` for keyboard and mouse, `SDL_AttachVirtualJoystick` for
  pads: two adapters at the SDL boundary that exercise the SDL3 branches and the helpers
  together.
