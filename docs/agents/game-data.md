# Game data and the D2 demo

## Rules

- The Descent 2 data (`DESCENT2.HOG`, `.HAM`, `.PIG`, `.S11`/`.S22`, movies, demos, missions) is
  licensed. It and everything derived from it (captures, traces, screenshots, frame dumps,
  goldens, logs that quote it) never go into git, an issue, a PR or a report. `check.sh`'s
  guard step rejects the known file types when staged; it is a net, not the rule.
- Derived files live in `traces/local/`, which `.gitignore` excludes.

## Where it is

`DXX_D2_DATA`, an environment variable you set in your shell (no script reads it), names the
directory holding `DESCENT2.HOG`. On the owner's Mac it is
`~/Developer/descent-installer/data/common/D2`, with the demo in `DEMOS/descent2.dem`. Nothing in
the repository holds the data.

## Running the demo windowless

Build the game with `tools/dev/build.sh dev`, then, from the repository root:

```sh
export DXX_D2_DATA=~/Developer/descent-installer/data/common/D2
mkdir -p traces/local/d2home
D2X_REBIRTH_HOME=$PWD/traces/local/d2home tools/dev/run.sh build/dev/d2x-rebirth/d2x-rebirth \
  -hogdir "$DXX_D2_DATA" -window -autodemo
```

- `run.sh` runs it under `no-window.sh` (no focus, no crash report) with `env.sh`'s sanitizer
  settings.
- `D2X_REBIRTH_HOME` is the user directory (config, saves, player files): point it at a scratch
  directory under `traces/local/`, never the real one. Upstream has no `-userdir` option.
- Always `-window`, never full screen.
- Startup frames depend on the wall clock, so align any frame comparison on the demo's start,
  not on the first frame or a fixed time after launch.

## Hashing a demo, invisibly

A `dbg_harness=1` build writes the frame hash log (`GLOSSARY.md`, debug harness) of a demo with
no window on screen, from a fresh home; like every `build.sh` profile it has the hardened
standard library (ADR 5):

```sh
tools/dev/build.sh dev dbg_harness=1 builddir=build/harness \
  compilation_database=build/harness/compile_commands.json
DXX_DBG_HIDDEN=1 DXX_DBG_FIXED_FRAMETIME=30 DXX_DBG_FRAMEHASH=$PWD/traces/local/frames.log \
  D2X_REBIRTH_HOME=$PWD/traces/local/d2home tools/dev/run.sh build/harness/d2x-rebirth/d2x-rebirth \
  -hogdir "$DXX_D2_DATA" -window -pilot harness -nomovies -autodemo
```

- A hidden run (`DXX_DBG_HIDDEN` set) reports an error on stderr and exits with status 1, no dialog.
- A hidden run proves it is silent: each time it opens audio (`digi_init`, and the movie player's
  own `SDL_OpenAudio` under `-nosdlmixer`) it writes `DXX_DBG_HIDDEN: audio driver <name>` to
  stderr, the driver SDL opened, and on another driver than `dummy` or `disk`, or none, ends at
  once with status 1 and the reason on stderr, before it plays anything, unless
  `NO_WINDOW_SOUND=1`. `run.sh` checks again after the run: it fails one whose stderr holds a line
  with that report that is not exactly the `dummy` or the `disk` one. Not covered: a run of
  `no-window.sh` launched directly (no after-the-fact check), a build without `dbg_harness=1` (no
  report, no stop), SDL 1.2 builds, and a run that never opens audio (`-nosound`), which writes no
  line and passes.
- An R12 audio capture runs on SDL's `disk` driver, which writes the mix to a file and plays
  nothing: set `SDL_AUDIO_DRIVER=disk` and an absolute `SDL_AUDIO_DISK_OUTPUT_FILE` per run,
  under your scratch folder (the capture is licensed-data derived); `no-window.sh` keeps that
  driver, fails a relative or missing path before the run, and forces `dummy` for any other.
- `-nomovies`: Descent 2 plays its intro movie even with `-notitles`, for a wall-clock time.
- `-pilot` loads an existing pilot as upstream does, whatever its name, and never overwrites it;
  a missing one it creates only with a name the new-pilot menu accepts (1 to 8 letters, digits,
  `_`, `-`), else it stops with an error.
- `-autodemo` loops the demos; end the run with TERM to `run.sh` once the log holds the
  sections you need, and compare whole `# demo playback <file>` sections of the same demo, never
  a section cut by the end.
- `-autodemo` picks each demo with `d_rand() % count` (`newdemo_start_playback`). The owner's
  `DEMOS/` holds one demo, so the pick is always it (`x % 1` is 0), which is why runs matched.
  With several, the pick depends on every `d_rand` call before it: the seed is fixed (no
  single-player `d_srand`), but nothing makes the calls during the wall-clock-timed screens
  between demos repeat. Compare sections of the same demo, by the name in their header.

## Recording a trace, invisibly

A `dbg_harness=1` build records a trace (`GLOSSARY.md`) of the frames `DXX_DBG_TRACE_FRAMES`
names (`FIRST-LAST`) into the file `DXX_DBG_TRACE` names; without `DXX_DBG_TRACE` nothing records.
Add both to a hidden run above, for example the demo's:

```sh
DXX_DBG_TRACE=$PWD/traces/local/s2.rtr DXX_DBG_TRACE_FRAMES=0-1600 \
  DXX_DBG_HIDDEN=1 DXX_DBG_FIXED_FRAMETIME=30 DXX_DBG_FRAMEHASH=$PWD/traces/local/frames.log \
  D2X_REBIRTH_HOME=$PWD/traces/local/d2home tools/dev/run.sh build/harness/d2x-rebirth/d2x-rebirth \
  -hogdir "$DXX_D2_DATA" -window -pilot harness -nomovies -maxfps 60 -autodemo
```

- Frames count presents from the start of the run, 0 first: a frame's number is its place among
  the frame hash log's frame lines, section headers left out (each section counts from 0 again).
- Give every path absolute, in a directory that exists: the game changes its working directory,
  and an unwritable path ends the run with status 1 and the reason on stderr.
- The whole S2 run, frames 0-1600, is about 200 MB. The trace is licensed-data derived.
- Replay it with `rtrace` (GL) and `rtrace-gpu` (SDL_GPU) through `no-window.sh`; with
  `RTRACE_FRAMES_DIR=<dir>` each also writes every frame's RGB pixels, top row first, to
  `<dir>/<frame>.rgb`, for comparing frames pixel by pixel.
- A replay's log never equals the game's frame hash log as written: rtrace hashes rows top first,
  the game hashes GL's readback bottom first. With `RTRACE_GAME_LOG=<file>` the replay also
  writes its lines with the bottom-first hash, which compare with the game's log line by line:
  drop the log's `#` section headers, take its lines FIRST to LAST, and compare the size and hash
  columns (a section's frame numbers restart at 0).
- At exit the run writes on stderr what it recorded, `DXX_DBG_TRACE: recorded frames X-Y of
  FIRST-LAST`, or `recorded no frame of FIRST-LAST: the run presented N frames`: a run that ends
  before LAST gives a shorter trace, and one that never reaches FIRST an empty file.

## Playing a scene, invisibly

The scripted scenes S1, S3 and S4 (`GLOSSARY.md`, input script) are in `common/debug/scenes/`,
with S5 (`s5-mouse-turn.txt`, #43), which moves a menu choice with the wheel and turns the ship
by mouse, and S6 (`s6-gamepad-flight.txt`, #44), which flies it with a virtual gamepad; `r1.sh`
plays S1-S4 only.
A `dbg_harness=1` build plays one with no window on screen and writes its frames to a
`# scene <script name>` section of the frame hash log:

```sh
tools/dev/build.sh dev dbg_harness=1 builddir=build/harness \
  compilation_database=build/harness/compile_commands.json
rm -rf traces/local/s1home && mkdir -p traces/local/s1home
DXX_DBG_HIDDEN=1 DXX_DBG_FIXED_FRAMETIME=30 DXX_DBG_FRAMEHASH=$PWD/traces/local/s1.log \
  DXX_DBG_INPUT_SCRIPT=$PWD/common/debug/scenes/s1-title-menu.txt \
  D2X_REBIRTH_HOME=$PWD/traces/local/s1home tools/dev/run.sh build/harness/d2x-rebirth/d2x-rebirth \
  -hogdir "$DXX_D2_DATA" -window -pilot harness -nomovies
```

- The run ends by itself at the script's `end` line, with status 0. Two runs of a scene write the
  same log: compare whole logs.
- Use a fresh home for every run: the scripts count on a new pilot's defaults (the resolution,
  each menu's first choice, no level played yet).
- The scripts are timed for `DXX_DBG_FIXED_FRAMETIME=30`: the title screen's 3 seconds are 90
  ticks at 30, and other screens time themselves the same way.
- While a script plays, the harness clock advances one frame step per `timer_update` call, not
  per frame, and the frame loop calls it until a frame is due: scene logs compare only between
  runs at the same `-maxfps` and VSync. Real-time timeouts count calls too and collapse, to about
  30 calls at 30 (the MIDI reset in `hmp.cpp`, the timeouts of `net_udp.cpp`).
- A script's `joy_attach` lines plug virtual joysticks into SDL's virtual driver. A hidden run
  opens no real controller: the harness sets `SDL_JOYSTICK_HIDAPI`, `SDL_JOYSTICK_IOKIT` and
  `SDL_JOYSTICK_MFI` to 0 before `SDL_Init` (`common/debug/harness_joysticks.h`), and `run.sh`
  exports the same three for every binary. Set `SDL_ASSERT=ignore` so an SDL assertion logs
  instead of opening a dialog: fail a run whose output holds `Assertion`.
- On SDL3 a pad plugged in at tick 0 opens at the first `event_poll`, so a pilot that `-pilot`
  creates before any frame (harness builds with `-notitles` only) gets the keyboard's defaults
  where SDL2 gives the pad's. S6 plugs its pad in during the titles; release builds create pilots
  in the menus, after the pad is open.
- The `end` line exits with `_Exit(0)`: no `atexit` handlers or static destructors run, and
  LeakSanitizer prints no report, so ASan scene runs do not check leaks.
- To write a scene, play a draft script with `press PRSCR` lines and look at the screenshots in
  the home's `screenshots/`; they are derived from the licensed data and stay there.

## Recording input, and replaying it

A `dbg_harness=1` build records the input a run consumed into the input script `DXX_DBG_RECORD_INPUT`
names (#99; `common/debug/input_recorder.h`, `common/sdl/harness_record.cpp`), which
`DXX_DBG_INPUT_SCRIPT` replays; without the variable nothing records. The recorder takes each SDL event where `event_poll` hands it to the game, at that `event_process`
call's harness tick, so the replay pushes it at the same tick. Recording needs
`DXX_DBG_FIXED_FRAMETIME` and runs the harness clock, as a script does; with no script it paces the
ticks to the wall clock (one tick per frame step), so a player plays at the real speed.
`DXX_DBG_RECORD_INPUT_TICKS=N` ends the run at tick N, as a script's `end` line does (1800 is 60 s
at 30); without it the recording ends where the run stops. At exit the run writes on stderr
`DXX_DBG_RECORD_INPUT: recorded N input events over ticks 0-T; M not recorded`. A recording is
licensed-data derived like a frame log (it replays a session of the data): keep it out of git.

What a recording holds: keys, mouse motion, buttons and wheel, and the script's joystick lines when
they reach the recorder as events (its tests write them). Not held: the events of a real joystick
or gamepad (a real device is not one of the script's two virtual shapes, whose axis, button and
hat counts the game's bindings read; SDL sends a joystick and a gamepad event for one gamepad
action, so replaying both would act twice; a device plugged in at start has no attach to
record), a quit (closing the window), and on SDL3 text other than a key's own character; each
becomes a `# tick N: not recorded: ...` comment, and the replay differs from that tick on. Also not held: where the cursor is when a menu reads it
(`mouse_flush`), Caps, Num and Scroll Lock (`key_flush` restores them from SDL's state), a window
resize, and on SDL3 the fractions of mouse motion the game's accumulator carries across a
`mouse_flush`. A joystick or gamepad plugged in at startup sends a device-added event the live run
consumes and the replay never sends: disconnect controllers before recording. On SDL3 a key down
that ends a 128-event batch, or comes just before the front window changes, gets its text in the
next batch live, while the replay pushes both into one batch; such a session can differ there.

The owner's session (the Mac, shown window, keyboard and mouse; use the keyboard in menus, the
mouse in flight; keep the window's size and leave it open). This block is the owner's alone: it
shows a window and takes the real keyboard and mouse. Agents record hidden through `run.sh` (last
item below).

```sh
tools/dev/build.sh dev dbg_harness=1 builddir=build/r1 compilation_database=build/r1/compile_commands.json
mkdir -p traces/local/rec && rm -rf traces/local/rec/home && mkdir traces/local/rec/home
SDL_AUDIODRIVER=dummy SDL_AUDIO_DRIVER=dummy SDL_VIDEO_HIGHDPI_DISABLED=1 \
  DXX_DBG_FIXED_FRAMETIME=30 DXX_DBG_RECORD_INPUT=$PWD/traces/local/rec/session.txt \
  DXX_DBG_RECORD_INPUT_TICKS=1800 DXX_DBG_FRAMEHASH=$PWD/traces/local/rec/session.log \
  D2X_REBIRTH_HOME=$PWD/traces/local/rec/home build/r1/d2x-rebirth/d2x-rebirth \
  -hogdir "$DXX_D2_DATA" -window -pilot harness -nomovies -maxfps 60
R1_FPS=60 tools/dev/r1.sh replay traces/local/rec/session.txt traces/local/rec/session.log
```

- The game closes itself after 60 s (tick 1800); the dummy audio driver keeps it silent and is
  the driver the replay runs with.
- `SDL_VIDEO_HIGHDPI_DISABLED=1` is what `run.sh` sets for the replay: without it a Retina Mac
  renders at pixel density 2, which changes the frames and the cursor position menus read.
- `r1.sh replay` builds the working tree in `build/r1`, plays the recording hidden through
  `run.sh` with a fresh home at the one `-maxfps` in `R1_FPS`, and compares every frame with the
  live run's log: `session.txt at 60 fps: N/N frames identical`, exit 0. Replay at the live run's
  `-maxfps`, from the same fresh-home start (`-pilot harness`, no `descent.cfg`).
- Agents record the same way, hidden through `run.sh`, with a script driving the run
  (`DXX_DBG_INPUT_SCRIPT` beside `DXX_DBG_RECORD_INPUT`, which then records unpaced). The frame
  hash log names its section after the recording's file, not the script's, so the replay's log
  compares whole whatever the two files are called.

## Checking R1, invisibly

`tools/dev/r1.sh` plays S1, S2, S3 and S4 hidden, as above, at `-maxfps` 60 and 200, and compares
each frame with a baseline in `traces/local/r1/baseline/`, which records the commit it was built
from in its `commit` file, with the hardened dev profile:

```sh
export DXX_D2_DATA=~/Developer/descent-installer/data/common/D2
tools/dev/r1.sh capture   # once per branch point
tools/dev/r1.sh check     # after each change
```

- The baseline is the branch point's, the merge-base of `HEAD` and `sdl-gpu-port`; `--base REV`
  names another commit, in both modes. `check` fails before it builds on a baseline of another
  commit.
- It prints `<scene> at <fps> fps: N/N frames identical` per scene and fps, or the first frame
  that differs (its section and index), and exits 0 only when every frame matches. A log with
  no frames, or with a line the harness does not write, fails too.
- The scenes have one baseline per fps: the harness clock makes their logs differ between
  `-maxfps` values. S2's demo also has to match itself across them.
- S2 stands in for the coverage demo with the installed demo until that demo exists; the first
  whole `# demo playback` section of each run is compared.
- The merge gate (`ci.sh --gate`, ADR 6) runs `r1.sh check` against the base commit's baseline,
  captured once per base commit and R1 inputs (the scenes, `r1.sh`, `toolchain.pins`) into the
  clone's git directory (`dxx-r1/<commit>-<inputs>`, shared by its worktrees, never committed)
  and reused; its runs go to `traces/local/r1-gate/`. Clear the cache by removing `dxx-r1` there.
