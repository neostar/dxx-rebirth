# Input testing research: sources

Sources for `input-testing.md`. Fetched 2026-10-27 (the fetch date of every row) with `curl` and `gh api`; the text was extracted from HTML by a script, so quotes are verbatim up to
whitespace, curly quotes, markdown link syntax and list bullets (joined with commas). "Stated date" is what the source says: a commit date for a pinned
file, a page's own date or "none". Raw files are pinned with SHA-256 in `tools/dev/reference-docs.txt`
and mirrored by `tools/dev/fetch-docs.sh` under `docs/reference/input-testing/` (`docs/reference/sdl3/`
for SDL); pages are not pinned. GitHub files are pinned at the commit that last touched them.

## Sources

| Id | Path or URL | Version | Stated date |
|---|---|---|---|
| S1 | `input-testing/unity-input-system-testing.md` (Unity-Technologies/InputSystem, `Documentation~/Testing.md`) | commit ee735e63f4 | commit 2026-07-13 |
| S2 | `input-testing/unity-input-system-test-fixtures.md` (`set-up-test-fixtures.md`); write-tests page: https://raw.githubusercontent.com/Unity-Technologies/InputSystem/develop/Packages/com.unity.inputsystem/Documentation~/write-tests.md (not pinned) | commit ee735e63f4 | commit 2026-07-13 |
| S3 | `input-testing/unity-input-system-record.md` (pinned; shows the recording code, not the quoted sentence); https://docs.unity3d.com/Packages/com.unity.inputsystem@1.14/api/UnityEngine.InputSystem.LowLevel.InputEventTrace.html (not pinned; holds the quoted sentence) | commit ee735e63f4; Input System 1.14 API page | commit 2026-07-13; API page none |
| S4 | https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/EnhancedInput/IEnhancedInputSubsystemInterface/InjectInputForAction | "Unreal Engine 5.8 Documentation" | none |
| S5 | https://dev.epicgames.com/documentation/unreal-engine/automation-driver-in-unreal-engine | "Unreal Engine 5.8 Documentation" | none |
| S6 | https://dev.epicgames.com/documentation/unreal-engine/gauntlet-automation-framework-overview-in-unreal-engine | "Unreal Engine 5.8 Documentation" | none |
| S7 | `input-testing/godot-Input.xml` (godotengine/godot, `doc/classes/Input.xml`) | commit 90d334c27f | commit 2026-09-25 |
| S8 | `input-testing/doom-g_game.c` (id-Software/DOOM, `linuxdoom-1.10/g_game.c`) | commit 4eb368a960 | commit 2012-01-31 |
| S9 | `input-testing/linux-uinput.rst` (torvalds/linux, `Documentation/input/uinput.rst`) | commit 8bd490e4fc | commit 2021-03-09 |
| S10 | `input-testing/linux-uhid.rst` (`Documentation/hid/uhid.rst`) | commit 356006a6cf | commit 2021-01-04 |
| S11 | `input-testing/linux-gadget_hid.rst` (`Documentation/usb/gadget_hid.rst`) | commit ffeb1e9e89 | commit 2020-07-21 |
| S12 | `input-testing/umockdev-README.md` (martinpitt/umockdev) | commit a08e391da4 | commit 2023-12-18 |
| S13 | `input-testing/vigembus-README.md` (nefarius/ViGEmBus) | commit d986e1d937 | commit 2023-09-08 |
| S14 | https://docs.nefarius.at/projects/ViGEm/End-of-Life/ | page | text says "In May 2023 ... starting July 2023 and ending approximately December 2023" |
| S15 | `input-testing/viiper-README.md` (Alia5/VIIPER); `gh api repos/Alia5/VIIPER` for licence and release | commit 8420705a43; latest release v0.9.0 | commit 2026-09-27; release 2026-10-10 |
| S16 | https://learn.microsoft.com/en-us/windows-hardware/drivers/hid/virtual-hid-framework--vhf- | git 70cc98358f | ms.date 2025-04-22, updated_at 2025-04-23 |
| S17 | `input-testing/karabiner-README.md` (pqrs-org/Karabiner-DriverKit-VirtualHIDDevice) | commit c96c1a46fb | commit 2026-09-26 |
| S18 | `input-testing/apple-IOHIDUserDevice.h` (apple-oss-distributions/IOKitUser, `hid.subproj/`) | commit 37725a5563 | commit 2021-10-06 |
| S19 | https://developer.apple.com/tutorials/data/documentation/bundleresources/entitlements/com.apple.developer.hid.virtual.device.json | page JSON | none ("Copyright © 2026") |
| S20 | https://developer.apple.com/tutorials/data/documentation/coregraphics/cgevent/post(tap:).json | page JSON | none ("Copyright © 2026") |
| S21 | https://developer.apple.com/tutorials/data/documentation/gamecontroller/gccontroller/withextendedgamepad().json | page JSON | none ("Copyright © 2026") |
| S22 | `sdl3/SDL_joystick.h` | 3.4.18 | none |
| S23 | `sdl3/src/joystick/virtual/SDL_virtualjoystick.c` | 3.4.18 | none |
| S24 | `sdl3/src/joystick/linux/SDL_sysjoystick.c` | 3.4.18 | none |
| S25 | `sdl3/SDL_hints.h`, `sdl3/SDL_gamepad.h` | 3.4.18 | none |
| S26 | `input-testing/gamecontrollerdb-README.md`, `input-testing/gamecontrollerdb-check.yml` (mdqinc/SDL_GameControllerDB) | commits 0b5d8f12ba, 2d306e1e85 | commits 2026-09-27, 2025-04-19 |
| S27 | `input-testing/wine-dinput-hotplug.c`, `input-testing/wine-dinput-driver_bus.c` (wine-mirror/wine, `dlls/dinput/tests/`) | commits a4a85b6070, 13f6d643de | commits 2026-04-22, 2026-03-30 |
| S28 | https://github.com/web-platform-tests/interop/issues/1454 (`gh api repos/web-platform-tests/interop/issues/1454`) | issue | created 2026-09-23 |
| S29 | https://partner.steamgames.com/doc/steamdeck/compat ("Steam Deck and Steam Machine Compatibility Review") | page | none |
| S30 | https://devdocs.xbox.com/publishing/certification/xr/xr-115 | "Version 1.1, 5/1/2021" | page JSON-LD `dateModified` 2026-10-20T16:02:21Z |
| S31 | https://developer.nvidia.com/nvidia-latency-display-analysis-tool | page | none |
| S32 | https://www.nvidia.com/en-us/geforce/technologies/reflex/ | page | none |
| S33 | https://www.pjrc.com/teensy/td_keyboard.html, https://www.pjrc.com/teensy/td_joystick.html | pages | HTTP `Last-Modified` of td_joystick.html 2026-05-13 |
| S34 | https://developer.android.com/studio/test/other-testing-tools/monkey | page | "Last updated 2023-04-12 UTC" |
| S35 | `input-testing/syzkaller-external_fuzzing_usb.md` (google/syzkaller, `docs/linux/external_fuzzing_usb.md`) | commit b55bd4c326 | commit 2026-08-24 |
| S36 | torvalds/linux commits 5fe63b415c63 ("HID: core: Fix out-of-bounds left shift by 32 in s32ton()"), 689e453a9b9c ("HID: betop: fix slab-out-of-bounds Write in betop_probe") via `gh api repos/torvalds/linux/commits/<sha>` | commits | 2026-10-16; 2021-09-30 |
| S37 | https://github.com/orbstack/orbstack/issues/2261 | issue | created 2025-12-05; closed 2025-12-12, `state_reason` completed |
| S38 | `input-testing/wine-bus_iohid.c` (wine-mirror/wine, `dlls/winebus.sys/bus_iohid.c`) | commit f7c945c8c5 | commit 2026-10-06 |
| S39 | `sdl3/src/joystick/SDL_gamepad.c` | 3.4.18 | none |

## What each source supports

- **S1** line 9: "Generated input in tests is treated the same as input from platform backends at runtime, so you can verify gameplay and UI logic without depending on connected hardware."
- **S2** line 7: "restores the Input System to its original state after the test completes". The write-tests page names `Press`, `Release`, `PressAndRelease`, `Set` and `Trigger`.
- **S3** (the unpinned API page, not the pinned record page): "InputEventTrace lets you record input events for later processing. It also has features for writing traces to disk, for loading them from disk, and for playing back previously recorded traces."
- **S4**: "Runs modifiers and triggers delegates as if the input had come through the underlying input system as FKeys."
- **S5**: "because the Automation Driver simulates input at the platform layer, its public API is not dependent on Slate"; "In this initial revision of the Automation Driver, it supports the full suite of traditional desktop input, basically all keyboard and mouse-related inputs."; "Although we are not actively developing this feature, we may add support for the following: Scene/Actor interaction, Controller Input, Touch Input/Gesture, Motion Detection".
- **S6**: "Gauntlet does not require any specific game-side automation code or test framework".
- **S7** lines 606, 621, 927-929, 51-56: "Feeds an [InputEvent] to the game. Can be used to artificially trigger input events from code."; "Calling this function has no influence on the operating system."; "all input events generated during a frame will be merged and emitted when the frame is done rendering"; `flush_buffered_events` "Sends all buffered input events to the game loop."
- **S8** lines 667, 1495, 1522: `if (demoplayback) G_ReadDemoTiccmd (cmd);` and `if (demorecording) G_WriteDemoTiccmd (cmd);` in the tic loop; the recorder ends `G_ReadDemoTiccmd (cmd);         // make SURE it is exactly the same`; the command is four bytes (forward, side, turn high byte, buttons).
- **S9** lines 8-9, 16: "uinput is a kernel module that makes it possible to emulate input devices from userspace."; "that will be delivered to userspace and in-kernel consumers."
- **S10** lines 5-6, 48: "UHID allows user-space to implement HID transport drivers."; "The first thing you should do is send a UHID_CREATE2 event."
- **S11** lines 8-10: "The HID Gadget driver provides emulation of USB Human Interface Devices (HID). The basic HID handling is done in the kernel, and HID reports can be sent/received through I/O on the /dev/hidgX character devices."
- **S12** lines 5-17, 39, 57: "umockdev mocks Linux devices for creating integration tests for hardware related libraries and programs."; "without needing any particular privileges or disturbing the whole system"; "Recording and replay of evdev input events using the evemu events format."
- **S13** line 9, 34, "How to build": "THIS PROJECT HAS BEEN RETIRED"; "Emulation of the following USB Gamepads is supported: Microsoft Xbox 360 Controller, Sony DualShock 4 Controller"; use case "You want to test/benchmark your game and need a replay mechanism for your user inputs."; "you'll need to **sign** the driver to use it without test mode".
- **S14**: "As a direct result the ViGEmBus (and client libraries) project will no longer receive any updates and has been archived/retired."
- **S15** lines 26-37, 58-60, 79: "A cross-platform virtual USB input framework for creating virtual USB input devices (game controllers, keyboards, mice and more)"; "VIIPER is portable and runs entirely in userspace."; on Windows "usbip-win2 provides a signed kernel mode driver"; devices listed: Xbox 360 controller, HID Keyboard, HID Mouse. Licence GPL-3.0 and latest release v0.9.0 (2026-10-10) from `gh api repos/Alia5/VIIPER`.
- **S16**: "In this release, VHF supports a HID source driver only in kernel mode."
- **S17** lines 6, 14-15, 47, 103: "This project implements a virtual keyboard and virtual mouse using DriverKit on macOS."; "the virtual devices will only accept commands from processes running with root privileges"; "Follow any macOS prompts to approve the system extension."; building needs the `com.apple.developer.driverkit` entitlement obtained from Apple.
- **S18** lines 46, 70: "Creates an virtual IOHIDDevice in the kernel."
- **S19**: "A Boolean value that indicates whether the driver creates a virtual HID device."
- **S20**: "Posts a Quartz event into the event stream at a specified location."; the page text does not mention a session or permission.
- **S21**: "Returns a snapshot of a newly created controller with an extended gamepad profile."; "Unlike other controllers, you can set the values of a snapshot's" elements.
- **S22** line 514: "used to make other things look like joysticks, or provide pre-recorded input, etc."; `SDL_VirtualJoystickDesc` has `vendor_id`, `product_id`, `naxes`, `nbuttons`, `nhats`, `type` (lines 465-487); `SDL_DetachVirtualJoystick` line 555.
- **S23** line 799: `if (!hwdata || hwdata->desc.type != SDL_JOYSTICK_TYPE_GAMEPAD) { return false; }`.
- **S24** lines 149-151, 1036-1045: `ENUMERATION_LIBUDEV`, `ENUMERATION_FALLBACK`; the fallback is chosen when libudev is unavailable (lines 1032-1045 select the method).
- **S25** `SDL_hints.h` line 953: `SDL_HINT_GAMECONTROLLERCONFIG`; lines 955-967: "A variable that lets you provide a file with extra gamecontroller db entries."; "You can update mappings after SDL is initialized with SDL_GetGamepadMappingForGUID() and SDL_AddGamepadMapping()". `SDL_gamepad.h` line 347: `SDL_AddGamepadMapping`.
- **S26** README line 3: "A community sourced database of game controller mappings to be used with SDL2 and SDL3 Game Controller functionality."; `check.yml`: the only job runs `python duplicates.py`.
- **S27** `hotplug.c` lines 171, 211: `hid_device_start( &desc, 1 )`; `driver_bus.c` line 2: "Plug and Play test driver".
- **S28**: "Most gamepad behavior relies on manual testing due to the API's inability to natively connect a gamepad or generate realistic axis and button input. While existing browser test infrastructure utilizes mock-gamepad implementations, these are typically tied to engine-specific test APIs – limiting them primarily to unit testing."; bug list: "Extended controllers without a Home button exposed the wrong button layout", "DualSense controllers on Android 12+ were labelled as nonstandard", "Windows could enumerate one controller twice"; task "Allow a WPT to disconnect the gamepad and clean it up."
- **S29**: "Input Controller Support : On Steam Deck, your game must support Steam Deck's physical controls."; "The default controller configuration must provide users with the ability to access all content." No sentence on disconnect or hot-plug in the fetched text.
- **S30**: "Controller removal: If the player's controller that is driving gameplay is removed during gameplay, titles must allow reestablishment of a new active controller (for example, "Press A to continue" or receiving a ControllerPairingChanged ... )"; "A best practice is to pause gameplay when an active player's controller is removed."; test 115-02: "At various points during the title's use, remove the batteries from the controller. Replace the batteries in the controller and verify that the user can continue to use the title. Repeat Steps [1] - [3] but connect a second controller rather than replacing the batteries."; pass: "The active user should be able to continue using the title following the removal or addition of a controller."
- **S31**: "LDAT is a discrete hardware analyzer that uses a luminance sensor to quickly and accurately measure the motion-to-photon (click-to-muzzle flash) latency in a game or application. LDAT is cross platform and works with GPUs from all vendors."; "Please review the license agreement and check the box below to indicate that you accept it and wish to proceed to applying for the program".
- **S32**: "NVIDIA G-SYNC displays with Reflex Analyzer feature the world's only system latency measurement that detects clicks coming from Reflex-compatible gaming mice".
- **S33**: "becomes a USB keyboard and mouse while running your program"; joystick page: "becomes a USB joystick (or gamepad) which supports these features: 32: Buttons ... 1: Hat Switch", "six functions to control the Joystick's 6 axes".
- **S34**: "It sends a pseudo-random stream of user events into the system"; "If your application crashes or receives any sort of unhandled exception, the Monkey will stop and report the error."; "-s <seed> Seed value for pseudo-random number generator. If you re-run the Monkey with the same seed value, it will generate the same sequence of events."
- **S35** line 5: "syzkaller fuzzes USB fully within a (potentially-virtualized) environment".
- **S36**: "Syzbot reported an out-of-bounds shift (32-bit integer shifted left by 32) in the s32ton() routine in the HID core."; "Syzbot reported slab-out-of-bounds Write bug in hid-betopff driver. The problem is the driver assumes the device must have an input report but some malicious devices violate this assumption."
- **S37**: body "I tried to mount /dev/uinput but it mounts as a directory instead of device file"; no comments; closed by a maintainer as completed, 2025-12-12.
- **S39** lines 2366-2378, 1424-1452: `SDL_PrivateGetGamepadMapping` calls `SDL_PrivateGetGamepadMappingForNameAndGUID(name, guid)` first and only `if (!mapping && create_mapping)` builds a mapping from the driver; `SDL_PrivateGetGamepadMappingForGUID` tries exact version and CRC, then without CRC, then without version. S23 line 234: `SDL_CreateJoystickGUID(SDL_HARDWARE_BUS_VIRTUAL, vendor_id, product_id, 0, NULL, name, 'v', type)`.
- **S38** line 1: "Bus like function for mac HID devices"; line 193: `IOHIDDeviceRegisterInputReportCallback`; line 128: `static IOHIDManagerRef hid_manager;`.

## Sources not fetched or not usable

- Steamworks Steam Input pages (`partner.steamgames.com/doc/features/steam_controller`, `.../gamepad_emulation_bestpractices`): returned site navigation only; the content needs a login. No Valve statement on binding regression.
- Doom Wiki (`doomwiki.org/wiki/Demo`): HTTP 403. The Doom claims rest on the source file S8.
- Epic's 4.27 Gauntlet page (`docs.unrealengine.com`): a JavaScript challenge. The 5.8 page S6 was used.
- Sony and Nintendo certification requirements: under NDA.
- NVIDIA's Reflex Analyzer news page (`nvidia.com/.../reflex-analyzer-in-360hz-g-sync-monitors/`): HTTP 404; S32 is the replacement.
- Microsoft GDK controller-disconnect page: HTTP 404; XR-115 (S30) covers it.
- Apple's Human Interface Guidelines "Game controls": fetched, no statement on controller disconnect, so not cited.
- XInput2 and ViGEm client libraries, Apple TCC documentation for `CGEvent.post`: not read.
- Facts from a web search summary (the `hid-gaff` bug, VIIPER version, VirtualPad as a ViGEm successor) were not used unless a primary page above confirms them; "VirtualPad" is unconfirmed and omitted.
