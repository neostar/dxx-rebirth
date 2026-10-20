# 4. Local CI is the only CI; Metal checks stay local

Date: 2026-10-20. Status: accepted.

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
- Disk is the scarce resource: `container.sh build` keeps only the current image,
  `container.sh prune` drops dangling images and unused build cache, and `container.sh save` keeps
  exactly one exported image under `third_party/images/`, so `container.sh load` restores the
  toolchain without snapshot.ubuntu.com.
