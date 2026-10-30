# Checking a shader or render change against GL

You changed a shader or how the renderer draws, and want to know the new picture still matches
OpenGL's. This page shows how, on macOS, Linux and Windows. You need no game data for it.

The check replays a recorded **trace** (a `.rtr` file: the renderer's calls for a few frames) twice,
once on OpenGL and once on one of the SDL_GPU backends (Metal, Vulkan or D3D12), and compares the
frames. `tools/dev/compare-backends.sh` does the whole job and prints a verdict for every frame
and for the trace.

## What counts as a match

- **Metal** must be bit-exact: every pixel of every frame equal to GL's.
- **Vulkan and D3D12** may differ a little, because each GPU rounds in its own way. The
  difference is measured with [FLIP](https://github.com/NVlabs/flip), a metric for how visible an
  error is to a person (0 is identical, 1 is very different). Per trace, the 95th percentile of
  the frames' mean FLIP error must be 0.01 or less, and the worst frame's must be 0.03 or less.

The lines and filters traces differ from GL on Metal by a few pixels by design (their own
tolerances are pinned in `tools/dev/rtrace-pins.sh`), so `compare-backends.sh` fails them on Metal.
Judge a change with the other committed traces, or with your own scenes.

## What you need

### macOS (everything works today)

1. Xcode, with its Metal toolchain. `tools/dev/setup.sh` downloads the toolchain if it is missing.
2. The pinned tools and libraries: `tools/dev/deps.sh` builds SDL3 and the rest into
   `third_party/`, `tools/dev/deps.sh venv` makes the Python environment with FLIP, and
   `tools/dev/doctor.sh` tells you if anything is off.
3. The two replay programs:
   ```sh
   tools/dev/build.sh dev build/dev/common/rtrace
   tools/dev/build.sh dev sdl3=1 build/dev-sdl3/common/rtrace-gpu
   ```

### Linux

The Linux pieces are not all there yet, and the script says so with a `SKIP` line instead of
passing.

- The GL replay on Linux comes with ticket #81, and Vulkan on lavapipe (a software Vulkan) with
  ticket #25. Until then, take the frames from elsewhere (see "Frames from another machine").
- The reference container (`tools/dev/container.sh build`, then `container.sh run ...`) holds all
  the pinned tools. You need Docker and nothing else from the list above.
- Outside the container, Vulkan and D3D12 frames that differ from GL's (any non-Metal frame that is
  not identical) need Python 3.13 with numpy and flip-evaluator, at the versions pinned in
  `tools/dev/requirements.lock` (`pip install --require-hashes -r tools/dev/requirements.lock`
  in a virtual environment, or `uv pip sync` with it). Without them, `compare-backends.sh` fails
  such a frame with "cannot run FLIP". Metal needs only `python3`.

### Windows (Git Bash)

- The Windows GL replay program and the D3D12 run come with ticket #49. Until the GL replay exists,
  `compare-backends.sh` prints a `SKIP` that names #49.
- `tools/dev/windows.sh build OUT` cross-builds `rtrace-gpu.exe` and its DLLs in Docker. It writes
  them to `OUT`, never to `build/dev-sdl3/common/`, where `compare-backends.sh` looks by default,
  so name the program: `--rtrace-gpu OUT/rtrace-gpu.exe`. The GitHub workflow `d3d12-check.yml`
  (started by hand) runs it on WARP, Microsoft's software renderer.
- Judging D3D12 frames needs Python 3.13 and the packages of `tools/dev/requirements.lock` (see
  Linux above). Git Bash has no `python3` by default, and the script calls that name: python.org's
  installer gives `python` and `py`, so copy `python.exe` to `python3.exe` in a directory on your
  `PATH`.
- Judging D3D12 frames needs Python 3.13 and the packages of `tools/dev/requirements.lock` (see
  Linux above). Git Bash has no `python3` command by default: the installer from python.org gives
  `python`, or `py -3.13`, so put a `python3` on your `PATH` (for example `alias` does not reach
  scripts; copy `python.exe` to `python3.exe` in a directory on your `PATH`).

R6 (no SDL_GPU validation messages in any R4 run) is not checked by `compare-backends.sh`; run
`tools/dev/gpu-validate.sh` for that (Metal and Vulkan).

## Rebuilding the shaders

The shaders are written in HLSL (`shaders/hlsl/`). The compiled forms (SPIR-V, Metal and DXIL) are
committed as headers in `shaders/generated/`, and a check fails if they are not exactly what the
pinned compilers produce. After you edit an HLSL file:

```sh
tools/shaders/build.sh
```

DXC, the compiler for DXIL, runs only on Linux, so on a Mac the script builds in the reference
container for you (Docker must be running; on a Mac with OrbStack, `orb start`). `tools/shaders/check.sh`
confirms the headers are current. Rebuild the replay programs afterwards: the headers are
compiled into them.

## Comparing on the committed traces

The synthetic traces are in `common/render/rtrace/synthetic/` (`sample`, `overbright`, `flash`,
`walls`, `alpha`, `oversize`, `fonts`, `lines`, `filters`). From the repository root, on a Mac:

```sh
tools/dev/compare-backends.sh common/render/rtrace/synthetic/sample.rtr metal
```

```
compare-backends: sample, Metal against GL (R4: bit-exact)
  frame 0 (64x48): identical
  frame 1 (64x48): identical
compare-backends: sample: PASS (2 of 2 frames bit-exact)
```

The exit status is 0 for a pass, 1 for a failure and 2 for a mistake in the command. A trace with
no verdict (a missing piece, below) exits 0 but says `SKIP`; never read a missing `PASS` as one.
A script or gate step that checks only the exit status must give `--require-verdict`, which makes
a `SKIP` exit 1.
A failure also covers a missing frame, a frame of another size, a replay that crashed and a GPU
run on another driver than you asked for.

Useful options (all in the header of the script):

- `--out DIR` keeps the frames of both runs in `DIR/gl` and `DIR/gpu` (raw RGB, one file per frame,
  top row first, plus `frames.log`), so you can look at what differs. `DIR` must be empty or new.
- `--require-verdict` turns a `SKIP` into a failure (exit 1), for a gate step.
- `--rtrace PATH` and `--rtrace-gpu PATH` name the replay programs when they are not in `build/dev`
  and `build/dev-sdl3`.

## Frames from another machine

A backend that your machine cannot run is judged from frames recorded where it can. A frames
directory holds `0.rgb`, `1.rgb`, ... and `frames.log`, which is what the replay programs write
when you set `RTRACE_FRAMES_DIR=DIR` and give `DIR/frames.log` as their log argument:

```sh
# On Windows, in Git Bash, next to rtrace-gpu.exe (WARP or a GPU):
mkdir d3d12-frames
SDL_GPU_DRIVER=direct3d12 RTRACE_FRAMES_DIR=$PWD/d3d12-frames \
  ./rtrace-gpu.exe sample.rtr d3d12-frames/frames.log
```

Copy the directory to a machine with the GL replay and judge it there; the script replays GL for
you:

```sh
tools/dev/compare-backends.sh --gpu-frames d3d12-frames common/render/rtrace/synthetic/sample.rtr d3d12
```

If GL's frames also come from elsewhere, give both and no trace:

```sh
tools/dev/compare-backends.sh --gl-frames gl-frames --gpu-frames d3d12-frames d3d12
```

Frames from a WARP or CI run are only as good as the GL frames you compare them with: use GL
frames from the same trace, drawn by the same commit.

## Recording your own scene traces

Your own game data gives scenes the synthetic traces do not have. The data is licensed: **keep the
traces, the frames and the logs on your machine** (the repository's `traces/local/` is ignored by
git) and never commit or share them.

1. Build the game with the debug harness: `tools/dev/build.sh dev dbg_harness=1 builddir=build/harness`.
2. Run it with `DXX_DBG_TRACE=/absolute/path/scene.rtr` and `DXX_DBG_TRACE_FRAMES=FIRST-LAST` set,
   hidden, as `docs/agents/game-data.md` ("Recording a trace, invisibly") shows. The frame range
   counts frames from the start of the run, 0 first.
3. Compare it like a committed trace:
   `tools/dev/compare-backends.sh traces/local/scene.rtr metal`.

A trace of 1600 frames is about 200 MB. Compare a short range first.
