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
  stderr, the driver SDL opened, and on another driver than `dummy`, or none, ends at once with
  status 1 and the reason on stderr, before it plays anything, unless `NO_WINDOW_SOUND=1`.
  `run.sh` checks again after the run: it fails one whose stderr holds a line with that report
  that is not exactly the `dummy` one. Not covered: a run of `no-window.sh` launched directly (no
  after-the-fact check), a build without `dbg_harness=1` (no report, no stop), SDL 1.2 builds, and
  a run that never opens audio (`-nosound`), which writes no line and passes.
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

## Playing a scene, invisibly

The scripted scenes S1, S3 and S4 (`GLOSSARY.md`, input script) are in `common/debug/scenes/`.
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
- The `end` line exits with `_Exit(0)`: no `atexit` handlers or static destructors run, and
  LeakSanitizer prints no report, so ASan scene runs do not check leaks.
- To write a scene, play a draft script with `press PRSCR` lines and look at the screenshots in
  the home's `screenshots/`; they are derived from the licensed data and stay there.

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
