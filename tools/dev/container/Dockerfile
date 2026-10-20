# syntax=docker/dockerfile:1
# The Linux reference container (ticket #57, ADR 4): where every check runs
# as on the Mac but on Linux, offline, with Mesa lavapipe/llvmpipe and the
# Khronos validation layers; tools/dev/ci.sh runs its leg here.  Built
# locally by tools/dev/container.sh from tools/dev/toolchain.pins, which
# pins the base image and uv by digest and apt by an Ubuntu archive
# snapshot; never pushed to a registry.  linux/amd64: the pinned DXC, FLIP
# and shellcheck binaries are x86-64 only, and the Windows leg under Wine
# (#49) needs x86-64 too.
ARG BASE
ARG UV
FROM ${UV} AS uv
FROM ${BASE}
ARG SNAPSHOT

ENV LANG=C.UTF-8 LC_ALL=C.UTF-8 TZ=UTC \
    DEBIAN_FRONTEND=noninteractive \
    DXX_VENV=/opt/dxx/venv DXX_DEPS_PREFIX=/opt/dxx/deps \
    UV_PYTHON_INSTALL_DIR=/opt/dxx/python UV_NO_CACHE=1

# Compilers (GCC 14 and Clang 19, as gcc/g++/cc/c++ and clang/clang++),
# ccache, build tools and the libraries of the legacy SDL1.2 and SDL2
# builds; Vulkan on lavapipe with the validation layers.  Every package
# comes from the Ubuntu archive as it was at the snapshot.  The base image
# has no CA certificates for snapshot.ubuntu.com's https, so they come
# first with peer verification off: apt checks every package against the
# archive's signed Release files either way, with the keyring in the base.
RUN echo "APT::Snapshot \"$SNAPSHOT\";" > /etc/apt/apt.conf.d/50snapshot \
    && echo 'Acquire::https::snapshot.ubuntu.com::Verify-Peer "false";' \
        > /etc/apt/apt.conf.d/99bootstrap \
    && apt-get update \
    && apt-get install -y --no-install-recommends ca-certificates \
    && rm /etc/apt/apt.conf.d/99bootstrap \
    && apt-get update \
    && apt-get install -y --no-install-recommends \
        curl git make gcc-14 g++-14 clang-19 ccache pkgconf scons python3 perl zstd \
        libsdl1.2-dev libsdl-image1.2-dev libsdl-mixer1.2-dev \
        libsdl2-dev libsdl2-image-dev libsdl2-mixer-dev libphysfs-dev libpng-dev \
        libgl-dev libglu1-mesa-dev libboost-test-dev \
        libvulkan1 mesa-vulkan-drivers vulkan-validationlayers vulkan-tools \
    && rm -rf /var/lib/apt/lists/* \
    && for t in gcc g++; do ln -s "/usr/bin/$t-14" "/usr/local/bin/$t"; done \
    && ln -s /usr/bin/gcc-14 /usr/local/bin/cc && ln -s /usr/bin/g++-14 /usr/local/bin/c++ \
    && for t in clang clang++; do ln -s "/usr/bin/$t-19" "/usr/local/bin/$t"; done

# The source cache is bind-mounted from the host, owned by another user.
RUN git config --system safe.directory '*'

COPY --from=uv /uv /usr/local/bin/uv
# Only what deps.sh reads, so that the image depends on exactly the files
# container.sh hashes into its tag.
COPY deps.sh env.sh toolchain.pins requirements.lock /opt/dxx/dev/

# The venv from the hashed lockfile: the one step that downloads, at image
# build time, every wheel checked against its hash.
RUN /opt/dxx/dev/deps.sh venv

# The pinned dependencies, LLVM's tools among them, from the verified
# source cache, offline; deps.sh removes its build tree.
RUN --network=none --mount=type=bind,from=deps-cache,target=/cache,rw \
    PATH=/opt/dxx/venv/bin:$PATH /opt/dxx/dev/deps.sh --cache /cache build

ENV PATH=/opt/dxx/venv/bin:$PATH \
    SDL_VIDEO_DRIVER=offscreen SDL_GPU_DRIVER=vulkan \
    VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json \
    VK_KHRONOS_VALIDATION_REPORT_FLAGS=error,warn,perf
