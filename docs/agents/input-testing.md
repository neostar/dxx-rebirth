# Input testing: what other engines and vendors do, and what this port adopts

Research for #92 (SDL3 game links), #42 (keyboard), #43 (mouse, wheel), #44 (joystick, gamepad,
saved bindings), #18 (trace recording) and #93 (measurements on real hardware). Sources, with quotes, dates and fetch
dates, are in `input-testing-sources.md`; `[S12]` means its row S12. Read `sdl-input.md`
("Injected events and virtual devices (R12)", "Event pumping, latency and frame timing") and
`testing-games.md` (§7, §11, §13) first: this doc extends them and does not restate SDL's own
tools (`SDL_PushEvent`, `SDL_AttachVirtualJoystick`).

Words used: a **seam** is the place where input enters the program under test; a **rig** is
hardware that produces real input; **replay** feeds a recorded input stream back at a seam.
Evidence labels as in `testing-games.md`: a quote is a source's own words, **our inference** is
ours, **measure** is an open question only a measurement settles.

## Summary

Every layer below tests a different seam. The lower the seam, the more of the real path it
covers and the less an agent can run windowless and silent on this machine.

| Layer | Seam | Who does it | Catches | Cost for us | Verdict |
|---|---|---|---|---|---|
| 1. In-engine injection, replay | engine event queue | Unity, Unreal, Godot, id Software | game logic fed by wrong events; whole-game regressions on a recorded stream | low: we have SDL events and a harness | **Adopt**, extend the harness past keys |
| 2. OS virtual devices | kernel or OS HID stack | Linux `uinput`/`uhid`, ViGEm, VIIPER, Karabiner | OS driver path, VID/PID to mapping, report descriptors, OS hot-plug | Linux: unknown in OrbStack; Windows, macOS: high | **Probe** Linux once; skip the rest |
| 3. Mapping and binding regression | mapping database, saved bindings | SDL GameControllerDB, Valve | wrong layout, double enumeration, bindings that moved | low: a corpus of pilot files | **Adopt** as a corpus, no vendor tool exists |
| 4. Hardware in the loop | real USB port, real screen | NVIDIA LDAT, PJRC Teensy, Linux gadget | the full path incl. hot-plug and latency | a board to buy (owner: no hardware purchase); LDAT is gated | **Not adopted**; the Linux `uhid` fixture of layer 2 stands in |
| 5. Platform certification | shipped-title behaviour | Microsoft (Xbox); Valve reviews controller support only | the failure class Xbox's XR-115 tests: disconnect and reconnect | low as an automated virtual-device test | **Adopt** the XR-115 cases, automated |
| 6. Input fuzzing | event stream, device shape | Android Monkey, syzkaller | range and shape assumptions | low, seeded | **Later**, bounded |

## 1. In-engine injection and input record/replay

**Unity Input System.** Test code feeds input through the same path as the platform backends:
"Generated input in tests is treated the same as input from platform backends at runtime, so you
can verify gameplay and UI logic without depending on connected hardware" [S1]. `InputTestFixture`
gives each test a fresh Input System and "restores the Input System to its original state after
the test completes" [S2]; tests add devices (`InputSystem.AddDevice<Gamepad>()`) and call
`Press`, `Release` and `Set` [S2]. Record and replay is a second feature: "InputEventTrace lets
you record input events for later processing. It also has features for writing traces to disk,
for loading them from disk, and for playing back previously recorded traces" [S3].

**Unreal.** Three tools, none a full answer. `InjectInputForAction` "Runs modifiers and triggers
delegates as if the input had come through the underlying input system as FKeys" [S4], so it
skips the key-to-action mapping that a binding test needs. The Automation Driver "simulates
input at the platform layer" and, in its first revision, supports "basically all keyboard and
mouse-related inputs"; controller input appears only among possible additions, and Epic is "not actively developing this feature" [S5].
Gauntlet launches and judges sessions, and "does not require any specific game-side automation
code or test framework" [S6]: input is the game's own business.

**Godot.** `Input.parse_input_event` "Feeds an [InputEvent] to the game. Can be used to
artificially trigger input events from code" and "has no influence on the operating system" [S7].
Injected events meet the engine's accumulation: with `use_accumulated_input` "all input events
generated during a frame will be merged and emitted when the frame is done rendering", and
`flush_buffered_events` sends them early [S7]. A test that injects motion must flush or step a
frame before it asserts.

**Deterministic replay in shipped games.** Doom records the per-tic command, not raw keys:
`G_WriteDemoTiccmd` stores forward move, side move, turn and buttons, and playback calls
`G_ReadDemoTiccmd` in the same place in the tic loop [S8]. After writing, the recorder reads the
command back ("make SURE it is exactly the same"), so the live game runs on the same quantised
command the demo will replay [S8]. Our inference: any recorded stream we replay needs the same
rule, record what the game consumes after quantisation, or live play and replay diverge by
rounding.

**What the web platform learned.** Browsers have mock gamepads "typically tied to
engine-specific test APIs – limiting them primarily to unit testing", and the Gamepad
interop proposal lists the bugs that slipped through: a controller "without a Home button"
exposed the wrong layout, a DualSense labelled non-standard, "Windows could enumerate one
controller twice" [S28]. Layout and enumeration bugs are what a mock above the OS misses.

**What it catches / costs / fits us.**

- Catches: game logic fed by wrong events, whole-game regressions on a recorded stream.
  Cannot catch: anything below the SDL seam (driver, GUID, mapping database).
- Cost: low. SDL3 gives two seams already: `SDL_PushEvent` for keys, mouse and wheel, and
  `SDL_AttachVirtualJoystick`, which the header describes as a way to "provide pre-recorded
  input" [S22]. A virtual joystick goes through SDL's joystick layer, so SDL's own state and
  events are real, which `SDL_PushEvent` does not give (`sdl-input.md`, R12).
- Fit: the debug harness already pushes `SDL_KEYDOWN`/`SDL_KEYUP` from an input script
  (`common/debug/input_script.h`: `key_down`, `key_up`, `end`), timed in harness ticks, with a
  fixed step. Gap: no mouse, wheel, joystick, attach or detach actions. The mouse and wheel
  actions belong to #43, the `joy_*` actions to #44; #92 carries only the SDL3 branch of
  `harness_push_input`, as a rename (Top 3, item 2). Recording is the second half: a real
  session (keyboard, mouse, stick at the Mac) captured once and replayed windowless, as
  Unity's `InputEventTrace` does; it belongs to #18, and the capture layer is open (Questions).

## 2. OS-level virtual devices

**Linux.** `uinput` "makes it possible to emulate input devices from userspace": a process
writes to `/dev/uinput`, creates a device, and events reach "userspace and in-kernel consumers"
[S9]. `uhid` goes one layer lower: "UHID allows user-space to implement HID transport drivers",
so the kernel parses a report descriptor you supply, then creates the HID device [S10]. Our
inference: `uhid` exercises HID core, hidraw (SDL's HIDAPI path) and evdev (SDL's Linux joystick
path) from one fake device with a chosen VID/PID; `uinput` exercises evdev only. SDL's Linux
backend enumerates through libudev or falls back (`ENUMERATION_LIBUDEV`, `ENUMERATION_FALLBACK`)
[S24], so a container without `udevd` may still work: **measure**.
`umockdev` needs no kernel support: it "mocks Linux devices", replays evdev events in the evemu
format, and works "without needing any particular privileges" [S12]. It is a candidate when
`/dev/uinput` is absent.

Does `/dev/uinput` exist in our container? The Linux leg runs under OrbStack (`container.sh`
header). An OrbStack user reported "I tried to mount /dev/uinput but it mounts as a directory
instead of device file" (2025-12-05); a maintainer closed the issue as completed on 2025-12-12
with no comment [S37]. Unknown; **measure** (one probe, Questions below).

**Windows.** ViGEmBus is retired: "THIS PROJECT HAS BEEN RETIRED" [S13], after a trademark
conflict, and it "will no longer receive any updates" [S14]. It emulated Xbox 360 and
DualShock 4 pads only, and the driver must be signed outside test mode [S13]. VIIPER is the
live alternative: "cross-platform virtual USB input framework", userspace, over USBIP, with
Xbox 360, keyboard and mouse devices, and on Windows it relies on usbip-win2's signed driver
[S15]; it is GPL-3.0 (repository metadata, 2026-10-27) and its README lists no flight stick.
Microsoft's Virtual HID Framework lets a driver author expose any HID descriptor, but "VHF
supports a HID source driver only in kernel mode" [S16]. Wine tests its own DirectInput with a
test kernel driver that starts a virtual HID device from a descriptor ("Plug and Play test
driver", `hid_device_start(&desc, 1)`) [S27]. Our inference: we reach Windows only through Wine, CrossOver and
the manual WARP workflow (ADR 4), none of which runs a signed kernel driver. The Windows input
path is then tested by real hardware at #93.

**macOS.** `IOHIDUserDeviceCreate` "Creates an virtual IOHIDDevice in the kernel" [S18]; an
entitlement `com.apple.developer.hid.virtual.device` exists [S19]. The working open-source
example is Karabiner's DriverKit device: keyboard and mouse only, it "will only accept commands
from processes running with root privileges", activation needs "macOS prompts to approve the
system extension", and building it needs a DriverKit entitlement from Apple [S17]. No gamepad or
joystick. `CGEvent.post(tap:)` "Posts a Quartz event into the event stream at a specified
location" [S20]; the page does not say what session or permission it needs, and our inference
is that it has no gamepad event type. Apple's `GCController.withExtendedGamepad()` returns "a snapshot" whose
values you can set [S21], an in-process object; whether SDL's Apple joystick backend sees it was
not read. All of these need a GUI session, root or an interactive approval, so none fits an agent
running windowless on this Mac. The macOS hot-plug and HID path are left to #93's hardware
rig (layer 4).

**What only this layer tests.** The virtual driver's auto mapping applies only to gamepad-type
virtual devices: `VIRTUAL_JoystickGetGamepadMapping` returns false unless the descriptor's
`type` is `SDL_JOYSTICK_TYPE_GAMEPAD` [S23, line 799]. For every joystick,
`SDL_PrivateGetGamepadMapping` looks the database up by name and GUID before it asks the driver
for an auto mapping [S39, lines 2366-2378; the lookup order is at 1424-1452]. A virtual GUID
carries the bus VIRTUAL, the descriptor's VID/PID and type, and `'v'` [S23, line 234], so
whether a database entry matches a virtual device, stick or pad, is **measure**.

## 3. Mapping and binding regression

- **Mapping database.** SDL_GameControllerDB is "A community sourced database of game controller
  mappings"; its only CI check is `python duplicates.py` [S26]. There is no behavioural suite to
  borrow. SDL accepts mappings from a hint, `SDL_HINT_GAMECONTROLLERCONFIG`, and
  `SDL_AddGamepadMapping` after init [S25]. Our inference: a test pins one mapping string,
  attaches a virtual gamepad of the same VID/PID, and asserts the button and axis outputs.
  Platform default: `SDL_AddGamepadMappingsFromIO` and `...FromFile` drop, without an error, any
  line lacking a `platform:` field that matches the running OS (`SDL_gamepad.h:361-363`;
  `sdl-input.md`, "Platform-only hints"), so a fixture written on the Mac silently vanishes in
  the Linux container; use `SDL_AddGamepadMapping` or the hint, which have no such filter.
- **Steam Input.** Valve checks Deck compatibility by review: "The default controller
  configuration must provide users with the ability to access all content" [S29]. The Steamworks
  Steam Input pages returned only site navigation to `curl`; no Valve statement on regression
  testing of bindings was read.
- **Studios.** No primary source found on how studios test that saved bindings survive an engine
  or SDK upgrade. Our approach follows `testing-games.md` §8 (Factorio's save/load round trip):
  a **corpus** of pilot files, each saved by an older build, loaded by the new build and checked
  for the same action per control. Cases from the Gamepad proposal's list [S28] are the ones that
  matter: layout differs by controller, one device appears twice, a missing button. For us the
  saved value is one `uint8_t` per control (`playsave.h:173`), the flattened index, so the
  corpus must include devices whose index changes (two sticks swapped, pad plus stick).
- **Corpus shapes (#44).** #44's pilot-file test covers default bindings and a file with
  gamepad axis-button and joystick bindings. New here: stick unplugged, two sticks swapped,
  pad plus stick, each loaded under a virtual device of the same shape.

## 4. Hardware in the loop

- **Latency rigs.** NVIDIA LDAT is "a discrete hardware analyzer that uses a luminance sensor to
  quickly and accurately measure the motion-to-photon (click-to-muzzle flash) latency", and
  "works with GPUs from all vendors" [S31]; access is through a license agreement and an
  application to a program [S31]. Reflex Analyzer is built into some G-SYNC monitors and "detects
  clicks coming from Reflex-compatible gaming mice" [S32]. Both are NVIDIA hardware aimed at
  Windows PCs, so they do not measure this Mac. Robot fingers and high-speed cameras: no primary
  source found. Our inference: a microcontroller can fire a press at a known time and read a
  photodiode taped to the screen, which is LDAT's principle; unproven.
- **USB HID emulators.** A PJRC Teensy "becomes a USB keyboard and mouse while running your
  program"; its joystick mode offers "32: Buttons", six axes and one hat [S33], which covers a
  HOTAS-shaped device (`sdl-input.md` uses a 3-axis, 32-button stick). The Linux USB HID gadget
  driver "provides emulation of USB Human Interface Devices (HID)" with reports written to
  `/dev/hidgX` [S11]; it needs a Linux board with a USB device controller (the kernel doc names
  none).
- **Fit (#93).** The host would see a real USB device, so the whole OS path runs: IOKit or
  HIDAPI enumeration, GUID, mapping, hot-plug, and under CrossOver Wine's `winebus` macOS
  backend, which reads devices through `IOHIDManager` [S38]. The owner buys no hardware for
  testing (Not adopted), so the real-device questions in the table below use only devices the
  owner already has.

## 5. Platform certification

Public text exists for Xbox. XR-115 requires: "If the player's controller that is driving
gameplay is removed during gameplay, titles must allow reestablishment of a new active controller
(for example, "Press A to continue" ...)", and gives the best practice "to pause gameplay when
an active player's controller is removed" [S30]. Its test 115-02 is manual: "At various points
during the title's use, remove the batteries from the controller", replace them, then repeat with
a second controller; pass means "The active user should be able to continue using the title"
[S30]. The Steam Deck review lists controller support, glyphs and text input [S29]; the page
states no disconnect rule. Sony's and Nintendo's requirements are under NDA and were not fetched.
No source on how studios automate these: the public test is a person pulling batteries.

For #44, beyond its pilot-file test: the XR-115 case maps to `SDL_DetachVirtualJoystick`. Run
the same script N times with the detach at a seeded tick ("at various points"), reattach, and
assert: no crash, keyboard still works, the pad's bindings return, a second pad can take over.
Today raw joysticks have no hot-plug and gamepads hot-plug only on SDL2 (`sdl-input.md`, "How the
game drives input today"); #44 designs both for SDL3 (`sdl-input.md`, "Hot-plug" row), so the
first run documents current behaviour and fixes the contract before the port changes it.

## 6. Input fuzzing

- **Practice.** Android's Monkey "sends a pseudo-random stream of user events into the system",
  stops when the app "crashes or receives any sort of unhandled exception", and a fixed
  seed "will generate the same sequence of events" [S34]. syzkaller fuzzes the kernel's USB layer
  "fully within a (potentially-virtualized) environment" [S35].
- **What it finds in input code.** syzbot found a "slab-out-of-bounds Write" because "the driver
  assumes the device must have an input report but some malicious devices violate this
  assumption" [S36], and an out-of-bounds shift "(32-bit integer shifted left by 32)" in HID
  report-size handling, fixed 2026-10-16 [S36]. Both are shape and range assumptions about
  device data. Our inference, same class here: a device with zero axes, more buttons or axes
  than `DXX_MAX_*_PER_JOYSTICK` allows, a huge `xrel`, a key code outside the 256-entry table, a
  device detached between events.
- **Fit.** `testing-games.md` §11 skips fuzzing because the port does not change parsers; that
  stands for the data files. Input is a different target: small, in-process, no licensed data.
  A seeded generator that attaches virtual joysticks of random shape and pushes random SDL
  events into `process_event_batch` on the dummy video driver, with the seed printed on failure
  (Monkey's rule), is, our inference, a few hundred lines.

## Top 3 changes for this port, by value per cost

1. **A hot-plug and disconnect test on a virtual joystick. Ticket #44.** New beyond #44's
   criteria (pilot-file test, virtual gamepad and stick, SDL3 round trip, open-on-ADDED): the
   corpus shapes (section 3), the detach/reattach script at a seeded tick, and the XR-115
   assertions (section 5). Cost: SDL API plus the existing harness and dummy driver, no OS
   support. Value: it is the failure class Xbox's certification tests, and no test covers
   hot-plug today.
2. **Grow the input script past keys, and record real sessions into it. Tickets #43, #44
   and #18.** #92 is compile-level renames and stubs, so it keeps only the SDL3 branch of
   `harness_push_input`, as a rename (#42 carries it too). #43 adds `mouse_move`,
   `mouse_button` and `wheel` to `input_script.h`; #44 adds `joy_attach`, `joy_detach`,
   `joy_axis` and `joy_button`. #18 ("Trace recording in the game") gets the recorder and its
   replay under `r1.sh`: one 60-second session on the owner's Mac replays windowless and silent,
   and SDL2-on-Linux and SDL3 frame hashes are compared. The recorder stores what the game
   consumes after quantisation (the Doom rule [S8]); which layer that is, raw events at
   `event_poll` or the game's controls after `kconfig`, is open (Questions). Cost: medium (a
   grammar, a recorder, a test). Value: one capture of real hardware input becomes a permanent
   whole-game regression.
3. **Probe the Linux reference container for `/dev/uinput` and `/dev/uhid`. Ticket #93.** One
   command. If both exist as device nodes, a `uhid` fixture with a real VID/PID covers the OS HID
   path on Linux (hidraw and evdev, the HOTAS shape from `sdl-input.md`) with no hardware and
   runs in CI. If not, `umockdev` is the fallback (section 2). Cost: the probe, then a small
   fixture. Value: the only layer below SDL that an agent can run on this machine.

Not adopted:

| Option | Reason |
|---|---|
| ViGEmBus | retired; Windows kernel driver |
| VIIPER | GPL; Windows needs a signed third-party driver; no stick |
| Karabiner, `CGEvent.post` | root, approval, GUI session; keyboard and mouse only |
| Unity and Unreal frameworks | engine-bound |
| LDAT, Reflex Analyzer | NVIDIA, Windows, gated |
| A bot | `testing-games.md` §13 |
| A Teensy-class USB HID board | owner 2026-10-27: no hardware purchase for testing |

## Where this extends the earlier docs

- `sdl-input.md` R12 says gamepad tests "attach a virtual gamepad and a virtual 3-axis, 32-button
  flight stick". Agreed, with limits added here: the driver's auto mapping applies only to gamepad-type
  virtual devices [S23], whether the mapping database matches a virtual device is unmeasured
  (section 2), and `SDL_PushEvent` does not change SDL's input state, so events do not replace
  a virtual device where state matters.
- `testing-games.md` §11 "Fuzzing: skip" is about data files and still holds; it does not cover
  an input event stream, which this doc recommends later (section 6).
- `testing-games.md` §7 lists SDL's virtual joystick as the platform input method; it is layer 1
  of six, and it is the only one that is not an OS path. Nothing there is wrong.

## How to extend this doc

Done when a new row has a fetched source, a stated date, a fetch date and a verdict.

1. Re-check the live-or-dead status of every tool in layer 2: ViGEmBus, VIIPER (latest release,
   licence), Karabiner (supported macOS list), umockdev. Use `gh api repos/<owner>/<repo>/releases/latest`.
2. Look for the sources not read: Steamworks Steam Input pages (login), Sony and Nintendo
   requirements (NDA), GDC Vault talks (the Retro City Rampage talk is recorded in `testing-games-sources.md`),
   a studio post on binding-upgrade tests.
3. Pin a new raw file by the commit that last touched it:
   `gh api "repos/<owner>/<repo>/commits?path=<file>&per_page=1"`, then add
   `PATH raw-URL-at-sha SHA256` to `tools/dev/reference-docs.txt`.
4. Move a recommendation into a ticket's acceptance criteria as soon as its measurement is in.
5. Keep web pages that state no date marked "none"; do not infer one.

## Questions only a measurement answers

| Question | How | Ticket |
|---|---|---|
| Does the OrbStack Linux VM give the reference container `/dev/uinput` and `/dev/uhid` (as device nodes, not directories), and does SDL3 enumerate a device made there without `udevd`? | `container.sh run` with a device mapping, `ls -l /dev/uinput /dev/uhid`, then a 30-line `uinput` program and `testcontroller`-style listing | #44, #93 |
| Does a virtual joystick of type flight stick with a real HOTAS's VID/PID get a GameControllerDB mapping (becoming a gamepad)? And does a virtual gamepad with a real pad's VID/PID get the database entry or the driver's auto mapping? | Attach, call `SDL_IsGamepad`, print `SDL_GetGamepadMapping` | #44 |
| Does a recorded stream replay to the same frame hashes as the live session it came from? | Record one scene, replay twice under `r1.sh` | #18 |
| At which layer does the recorder capture: raw SDL events at `event_poll`, or the game's controls after `kconfig`? Doom stores the quantised per-tic command, while `event_poll` sees raw events | Record one scene both ways; replay each twice under `r1.sh`; compare hashes and what each misses (`kconfig` changes, device loss) | #18 |
| Do `CGEvent.post` or `IOHIDUserDeviceCreate` work from an agent session on this Mac, and does an SDL3 hidden window see them? Expected no; confirm once to close the option | A 20-line tool run from an agent shell | #93 |
| Does a macOS-side USB stick reach a CrossOver game through `winebus`'s `IOHIDManager` backend? | Plug in, open a DirectInput test in CrossOver | #93 |
| Do injected motion events and real ones sum to the same `Mouse.x/y` after the accumulator? | Replay a recorded real session against live | #43 |
| Real input-to-photon latency of pump, draw, sleep on the Mac, Linux SDL2 against SDL3 | Needs a light sensor and a real or emulated press; no software answer (see section 4) | #93 |
