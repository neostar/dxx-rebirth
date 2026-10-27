# 4. Local CI is the only CI; Metal checks stay local

Date: 2026-10-20. Status: accepted.
Amended by ADR 6: the merge gate (`ci.sh --gate`) decides a merge; the full run (`ci.sh --clean`) runs nightly.
Amended 2026-10-27 (owner decision on #49): one manual D3D12 workflow, below.

## Context

Ticket #57 makes the toolchain reproducible (`tools/dev/toolchain.pins`, `doctor.sh`,
`deps.sh`, `container.sh`). The SDL_GPU port renders through Metal on the Mac and through Vulkan
elsewhere. CI should run the same checks as a developer, on a machine with a GPU API that SDL_GPU
can drive. Whether GitHub's hosted macOS runners expose Metal is undocumented (the runner reference
says nothing about a GPU; actions/runner-images#7085, #9918 and #14836 disagree over time).

## Decision

- **Local CI is the only CI.** `tools/dev/ci.sh` runs two legs at once: `check.sh` natively on the
  Mac, and `check.sh` in the Linux reference container, offline. `ci.sh --clean` runs the
  container leg on a fresh `git clone` of the commit under test with no build caches; uncommitted
  changes are excluded. Its verdict is the one that counts. Nothing is pushed to a registry.
- **No GitHub CI.** A GitHub mirror only on the owner's explicit go.
- **One manual D3D12 workflow** (amended 2026-10-27, #49): `.github/workflows/d3d12-check.yml`.
  D3D12 needs a real Windows runtime with the D3D12 debug layer; Wine has neither (vkd3d-proton
  translates D3D12 to Vulkan). The workflow cross-builds `rtrace-gpu.exe` with MinGW-w64 on
  `ubuntu-24.04` (`tools/dev/windows.sh build`, its own pinned Dockerfile in `tools/dev/windows/`,
  built on the runner, never pushed), then replays the synthetic traces on `windows-2025`
  (`tools/dev/rtrace-d3d12.sh`).
  - **Pass condition**: the run fails on any D3D12 debug-layer message; on SDL's "Validation layers
    enabled" line missing; on GPU-based validation not confirmed from SDL's device (below); on a
    trace that does not render all its frames; on an adapter other than WARP (Microsoft Basic Render Driver).
  - **Record, not compare**: WARP's frame hash lines go to the `d3d12-frame-hashes` artifact and
    the log, for #37's FLIP compare. WARP rounds UNORM ties differently from a GPU, so equality
    with GL's hashes is the wrong test.
  - **GPU-based validation**: WARP renders correct pixels with wrong barriers; only the debug layer
    with GPU-based validation catches them. SDL 3.4.18 never calls `SetEnableGPUBasedValidation`,
    so on Windows `rtrace-gpu.exe` calls it through `ID3D12Debug1` before SDL creates the device,
    when `DXX_D3D12_GPU_VALIDATION=1` (the script sets it). That call returns nothing, so once SDL
    has created its device, `rtrace-gpu.exe` calls `D3D12CreateDevice` on the adapter SDL picks,
    enumerated as SDL does, which returns SDL's device (D3D12 devices are singletons per
    adapter). It checks the device is SDL's (its info queue carries SDL's storage filter), prints
    the debug layer's stored startup-options message (`CREATEDEVICE_DEBUG_LAYER_STARTUP_OPTIONS`)
    and exits 1 unless that text says GPU-based validation is on
    (`common/render/rtrace/startup_options.h`, unit-tested). The run fails without
    `rtrace-gpu`'s line `D3D12 GPU-based validation confirmed on SDL's device`. The startup
    text's exact form is first seen on the first GitHub run.
  - **ADR 5 exception**: `rtrace-gpu`'s `_WIN32` code is built only by the MinGW cross-build with
    `-Werror`, outside ADR 5's clang-tidy and sanitizer gates (owner decision: no new tooling).
  - **Timeouts**: each trace run has 300 s (`timeout -k 10 300`), the Wine run 300 s, the
    workflow's R3 step 15 minutes, so a hang fails and the hashes artifact still uploads.
  - **Wine** (`tools/dev/windows.sh wine`, local) is a smoke test: the exe runs, SDL picks D3D12
    and every trace renders all its frames. No hash compare, no validation requirement.
  - **Trigger**: `workflow_dispatch` only, started by hand when the D3D12 path changes (SDL3's
    D3D12 backend, the DXIL shaders, `sdlgpu_device`); never on push, pull request or schedule.
    `check.sh`'s workflows step allows exactly this file and fails when it gains any other
    trigger. `workflow_dispatch` needs the file on the default branch, so `sdl-gpu-port` is the
    fork's default branch and `master` stays a pure upstream mirror. Actions stay off between
    runs: for each run, enable Actions, disable every inherited upstream workflow, dispatch once,
    disable Actions again.
  - **Limits**: synthetic traces only, so licensed game data never reaches GitHub. WARP is
    Microsoft's software rasterizer: a pass says nothing of NVIDIA, AMD or Intel drivers,
    swapchains or present. It holds no merge (ADR 6); its run is evidence on the ticket that
    changed the D3D12 path.
- **Secret scanning is local too** (owner decision 2026-10-21): no GitHub workflow. The layers
  are ADR 5's pre-commit and pre-push tiers (gitleaks at the pinned version, failing without it;
  pre-push catches a commit made with `--no-verify`), the full-history scan in every `ci.sh` run,
  and GitHub push protection, a repository setting (on), not an Action.
  `check.sh`'s workflows step fails on any workflow upstream does not have, the manual D3D12
  workflow excepted. The workflows
  inherited from upstream stay in the tree, so rebases do not conflict, and are disabled on the
  fork one by one in GitHub's settings.
- **The Linux reference container** (`tools/dev/container/Dockerfile`, built by `container.sh`):
  Ubuntu pinned by digest, apt pinned by an archive snapshot, GCC 14 and Clang 19 from that
  snapshot, the dependencies built offline from the sha256-verified cache, Mesa lavapipe/llvmpipe
  with the Khronos validation layers. It covers SDL_GPU through Vulkan on lavapipe.
- **Metal checks stay local**, on the developer's Mac.
- **Two kinds of library input.** The legacy SDL1.2 and SDL2 builds link system packages at pinned
  versions (`pkg` lines: Homebrew on the Mac, the Ubuntu snapshot in the container), which
  `doctor.sh` checks with pkg-config. The pinned, source-built SDL3 stack (`deps.sh`) is consumed
  from the SDL3 build scaffold ticket (#38) on.
- **One LLVM.** clangd, clang-format and clang-tidy come from LLVM's own release archive for each
  platform, pinned by sha256 and unpacked by `deps.sh`, on the Mac and in the container alike;
  `doctor.sh` checks that the programs are still the unpacked ones, of one release and one source.
- **Bumping the Ubuntu snapshot** (`image ubuntu-snapshot`) is deliberate and gets its own commit,
  with the GCC, Clang and `pkg` pins it moves; it never rides along with another change.
- **The `Verify-Peer false` bootstrap is safe**: it only lets apt fetch `ca-certificates` before
  the base image can check snapshot.ubuntu.com's TLS certificate. apt verifies every package
  against the archive's signed Release files with the base image's keyring either way, and the
  setting is removed before anything else is installed.
- **Reproducible dependency builds.** The same pins give the same bytes at any prefix, so every
  worktree's prefix matches: `deps.sh` maps the build, cache and prefix paths to fixed names
  (`-ffile-prefix-map`, `-fdebug-prefix-map` for C, C++, Objective-C(++) and assembler;
  `-Wl,-oso_prefix` on the Mac, with links resolved, as `ld` records `/private/var`), links with a
  prefix-relative rpath (`@loader_path/../lib`, `$ORIGIN/../lib`) from the start, as rewriting the
  build rpath at install changes the bytes and the Mach-O UUID left from the link, and writes
  archives without timestamps (`ZERO_AR_DATE=1`; GNU ar is deterministic by default). SPIRV-Cross
  stamps its version string with CMake's `TIMESTAMP`, the build time, which is why
  `libspirv-cross-c-shared` differed between two builds at one path; `SOURCE_DATE_EPOCH=0` fixes it.
  FluidSynth's CMake overrides `CMAKE_INSTALL_NAME_DIR`, so on the Mac it named itself by its
  absolute path, which SDL3_mixer and the `fluidsynth` program recorded; deps.sh renames every
  install name into the prefix to `@rpath` after each install and signs the file again. One leak
  stays, allowlisted in the test: FluidSynth compiles its prefix into `libfluidsynth` and the
  `fluidsynth` program (the default soundfont path). Masking the prefix inside those files would
  not make them comparable on the Mac: the linker's ad-hoc code signature hashes the bytes that
  hold it. The fix is `-DDEFAULT_SOUNDFONT=`. pkg-config
  and CMake files name their prefix by design. `DXX_REPRO_TESTS=1 tools/dev/tests/repro_test.sh`
  builds the pins twice at two prefixes and compares every installed file.
- **The venv step downloads at image build time**, hash-locked (`requirements.lock`, `uv pip sync
  --require-hashes`); every later step and every check runs offline.

## Consequences

- Linux, GCC and Clang, Vulkan and the validation layers are checked in the container leg; Metal is
  checked in the native leg on the developer's Mac only.
- The platforms differ on purpose: DXC (DXIL) exists only in the container, as the pinned release
  is Linux x86-64 only; the container's SDL3 is headless (offscreen video, Vulkan, no X11 or
  Wayland).
- A result is only as good as the machine's pins: `doctor.sh`, the first step of `check.sh` after
  the repository config, fails both legs on any drift from `tools/dev/toolchain.pins`.
- The container is linux/amd64: the pinned DXC, FLIP and shellcheck binaries are x86-64 only, and
  the later Windows leg under Wine (#49) needs x86-64 too. On Apple silicon it runs under
  emulation and is slower; an image rebuild takes about 25 minutes.
- Disk is the scarce resource, and the Docker daemon is shared: `container.sh` keeps one image
  and one export under `third_party/images/` (its header says what `build`, `prune` and `save`
  remove and never touch), so `container.sh load` restores the toolchain without
  snapshot.ubuntu.com.
