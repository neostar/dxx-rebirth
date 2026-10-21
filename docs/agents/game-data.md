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
