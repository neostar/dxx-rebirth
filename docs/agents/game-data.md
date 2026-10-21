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
no window on screen, from a fresh home; demo runs need the build without the hardened standard
library, which stops on a known upstream `std::sort` assertion on the first demo frame:

```sh
tools/dev/build.sh dev dbg_harness=1 builddir=build/nohard \
  compilation_database=build/nohard/compile_commands.json \
  CPPFLAGS=-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_NONE
DXX_DBG_HIDDEN=1 DXX_DBG_FIXED_FRAMETIME=30 DXX_DBG_FRAMEHASH=$PWD/traces/local/frames.log \
  D2X_REBIRTH_HOME=$PWD/traces/local/d2home tools/dev/run.sh build/nohard/d2x-rebirth/d2x-rebirth \
  -hogdir "$DXX_D2_DATA" -window -pilot harness -nomovies -autodemo
```

- A hidden run (`DXX_DBG_HIDDEN` set) reports an error on stderr and exits with status 1, no dialog.
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
