# The Windows cross-build (#49): MinGW-w64 builds rtrace-gpu.exe and the
# pinned SDL3 as SDL3.dll, for the manual D3D12 check on GitHub
# (.github/workflows/d3d12-check.yml, ADR 4) and for a local run under
# Wine.  Built by tools/dev/windows.sh from tools/dev/toolchain.pins (base
# image by digest, apt by the Ubuntu archive snapshot, SDL3 by sha256),
# locally and on the workflow's ubuntu-24.04 runner; never pushed to a
# registry.  Separate from the reference image (tools/dev/container/),
# whose inputs it does not change.  Two stages:
#   cross  MinGW-w64 (GCC 13, noble), SDL3.dll in /opt/sdl3-win64 and
#          WinPixEventRuntime.dll in /opt/winpix;
#   wine   cross plus x64 Wine 9.0, Xvfb, lavapipe with the Vulkan
#          validation layers and vkd3d-proton (pinned below), where
#          windows.sh wine runs the D3D12 check through vkd3d-proton
#          (D3D12 on Vulkan, spike FINDINGS.md: Wine 9.0's own vkd3d
#          refuses DXIL).
ARG BASE
FROM ${BASE} AS cross
ARG SNAPSHOT
ARG SDL_URL
ARG SDL_SHA256

ENV LANG=C.UTF-8 LC_ALL=C.UTF-8 TZ=UTC DEBIAN_FRONTEND=noninteractive

# As the reference Dockerfile: the base has no CA certificates for
# snapshot.ubuntu.com's https, so they come first with peer verification
# off; apt checks every package against the archive's signed Release files
# either way.
RUN echo "APT::Snapshot \"$SNAPSHOT\";" > /etc/apt/apt.conf.d/50snapshot \
    && echo 'Acquire::https::snapshot.ubuntu.com::Verify-Peer "false";' \
        > /etc/apt/apt.conf.d/99bootstrap \
    && apt-get update \
    && apt-get install -y --no-install-recommends ca-certificates \
    && rm /etc/apt/apt.conf.d/99bootstrap \
    && apt-get update \
    && apt-get install -y --no-install-recommends \
        curl cmake ninja-build make \
        gcc-mingw-w64-x86-64 g++-mingw-w64-x86-64 binutils-mingw-w64-x86-64 \
    && rm -rf /var/lib/apt/lists/*

# SDL3 from its release tarball, checked against the pin, with the MinGW
# toolchain file it ships; D3D12 and Vulkan GPU backends.
RUN [ -n "$SDL_URL" ] && [ -n "$SDL_SHA256" ] \
    && curl -fsSL -o /tmp/sdl.tgz "$SDL_URL" \
    && echo "$SDL_SHA256  /tmp/sdl.tgz" | sha256sum -c - \
    && mkdir /tmp/sdl && tar -xzf /tmp/sdl.tgz -C /tmp/sdl --strip-components=1 \
    && cmake -S /tmp/sdl -B /tmp/sdl/build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DCMAKE_TOOLCHAIN_FILE=/tmp/sdl/build-scripts/cmake-toolchain-mingw64-x86_64.cmake \
        -DCMAKE_INSTALL_PREFIX=/opt/sdl3-win64 \
        -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF \
        -DSDL_GPU=ON -DSDL_VULKAN=ON -DSDL_OFFSCREEN=ON \
    && cmake --build /tmp/sdl/build && cmake --install /tmp/sdl/build \
    && grep -q '^#define SDL_GPU_D3D12 1' \
        /tmp/sdl/build/include-config-relwithdebinfo/build_config/SDL_build_config.h \
    && rm -rf /tmp/sdl /tmp/sdl.tgz

# WinPixEventRuntime (MIT, its license.txt kept beside it): SDL's D3D12
# backend loads it for debug groups and labels and warns at every device
# creation without it, a warning gpu-validate.sh would fail.
ARG WINPIX_URL=https://api.nuget.org/v3-flatcontainer/winpixeventruntime/1.0.240308001/winpixeventruntime.1.0.240308001.nupkg
ARG WINPIX_SHA256=726acc93d6968e2146261a1e415521747d50ad69894c2b42b5d0d4c29fd66ec4
RUN apt-get update \
    && apt-get install -y --no-install-recommends unzip \
    && rm -rf /var/lib/apt/lists/* \
    && curl -fsSL -o /tmp/pix.nupkg "$WINPIX_URL" \
    && echo "$WINPIX_SHA256  /tmp/pix.nupkg" | sha256sum -c - \
    && mkdir -p /opt/winpix \
    && unzip -j /tmp/pix.nupkg bin/x64/WinPixEventRuntime.dll license.txt -d /opt/winpix \
    && rm /tmp/pix.nupkg

FROM cross AS wine
ARG VKD3D_PROTON_URL=https://github.com/HansKristian-Work/vkd3d-proton/releases/download/v3.0.1/vkd3d-proton-3.0.1.tar.zst
ARG VKD3D_PROTON_SHA256=3cf2315522af5e43605ef6d3c41dad91387040bf97199934f3f7ab76caaa2f0c
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        zstd wine wine64 xvfb xauth \
        libvulkan1 mesa-vulkan-drivers vulkan-validationlayers \
    && rm -rf /var/lib/apt/lists/* \
    && curl -fsSL -o /tmp/v.tar.zst "$VKD3D_PROTON_URL" \
    && echo "$VKD3D_PROTON_SHA256  /tmp/v.tar.zst" | sha256sum -c - \
    && mkdir -p /opt/vkd3d-proton \
    && tar --zstd -xf /tmp/v.tar.zst -C /opt/vkd3d-proton --strip-components=1 \
    && rm /tmp/v.tar.zst
# One prefix, made at build time; no Mono or Gecko prompts.
ENV WINEPREFIX=/wine WINEARCH=win64 WINEDEBUG=-all \
    VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json
RUN WINEDLLOVERRIDES="mscoree,mshtml=" wineboot --init && wineserver -w
