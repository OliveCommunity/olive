# Build Guide

This document describes how to build Oak Video Editor from source on Windows, Linux, and macOS.

> **Note (2026):** Oak is now a Rust workspace; `cargo build` at the
> repository root produces the app, the CLI and the worker
> (`liboakengine` — the plugin/external-consumer cdylib — is not a
> default member, M14 R4; build it explicitly with
> `cargo build -p oakengine`). The CMake instructions below are kept for
> historical
> reference only. Rust dependencies are pulled from crates.io; the only
> native libraries still needed are FFmpeg (see the next section),
> OpenColorIO (optional; `ocio-sys` builds a stub without it) and a
> C/C++ toolchain for the FFI shims.
>
> ### FFmpeg for the Rust build
>
> `ffmpeg-next` 9.x pairs with FFmpeg 8.x headers, which most distros do
> not ship yet. Build a project-owned FFmpeg — GPL parts enabled, every
> free-license external codec library that is installed, per-OS hardware
> acceleration, static+PIC — with the project scripts:
>
> ```sh
> tooling/install-deps.sh           # Homebrew / MSYS2 UCRT64 / Debian / Fedora / Arch
> tooling/ffmpeg/build-ffmpeg.sh    # clones release/8.0, installs into .cache/ffmpeg
> export FFMPEG_DIR="$(pwd)/.cache/ffmpeg"
> cargo build
> ```
>
> External libraries are probed with `pkg-config` and silently skipped
> when missing. `FFMPEG_DIR` is mandatory (the `oakffmpeg-link` build
> script panics without it): silently binding a system FFmpeg risks
> stale `.pc` paths after package upgrades. IDEs that cannot inject
> environment variables into cargo (RustRover) can instead put
> `FFMPEG_DIR=...` (and `PKG_CONFIG_PATH=...` where needed) into a
> `.env` file at the workspace root — it is git-ignored.
> ffmpeg-next's `build` cargo feature is deliberately not used: it
> clones release/<crate-version>, and every such pairing is broken
> upstream (9.0.0 → FFmpeg 9.0 removed AVCodec fields; 8.1.0 → FFmpeg
> 8.1 added enum variants; 8.0.0 → FFmpeg 8.0 renamed FF_PROFILE_*).

## Prerequisites

- CMake 3.20+
- Ninja (recommended)
- Qt 6 (with private headers)
- FFmpeg 6.0+ development libraries
- OpenTimelineIO (0.16+, built from source below — no distro package on most platforms)
- OpenImageIO
- OpenColorIO (2.x)
- OpenEXR
- Expat
- PortAudio
- OpenGL headers
- Vulkan SDK (optional, required for the Vulkan render backend)
- XKB common (Linux)

---

## Windows (MSYS2)

This guide uses [MSYS2](https://www.msys2.org/) with the UCRT64 toolchain.

### 1. Install MSYS2

Download and install MSYS2 from [https://www.msys2.org/](https://www.msys2.org/). Then open the **MSYS2 UCRT64** terminal.

### 2. Install Dependencies

```bash
pacman -Syu
pacman -S --needed \
  mingw-w64-ucrt-x86_64-cmake \
  mingw-w64-ucrt-x86_64-ninja \
  mingw-w64-ucrt-x86_64-qt6-base \
  mingw-w64-ucrt-x86_64-qt6-tools \
  mingw-w64-ucrt-x86_64-ffmpeg \
  mingw-w64-ucrt-x86_64-openimageio \
  mingw-w64-ucrt-x86_64-opencolorio \
  mingw-w64-ucrt-x86_64-openexr \
  mingw-w64-ucrt-x86_64-fmt \
  mingw-w64-ucrt-x86_64-expat \
  mingw-w64-ucrt-x86_64-portaudio \
  mingw-w64-ucrt-x86_64-vulkan-headers \
  mingw-w64-ucrt-x86_64-vulkan-loader \
  mingw-w64-ucrt-x86_64-gcc
```

> **Note:** Qt 6 private headers may require additional packages depending on the MSYS2 repository state. If CMake reports missing private headers, install `mingw-w64-ucrt-x86_64-qt6-base-private` if available.

### 3. Build OpenTimelineIO (required)

There is no MSYS2 package for OpenTimelineIO, so build it from source:

```bash
git clone --depth 1 --branch v0.16.0 https://github.com/PixarAnimationStudios/OpenTimelineIO.git
cmake -S OpenTimelineIO -B OpenTimelineIO/build -G Ninja \
  -DOTIO_SHARED_LIBS=ON \
  -DOTIO_PYTHON_BINDINGS=OFF \
  -DOTIO_FIND_IMATH=ON \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="${PWD}/otio-install"
cmake --build OpenTimelineIO/build
cmake --install OpenTimelineIO/build
```

### 4. Clone and Build

```bash
# Clone the repository
git clone --recursive https://github.com/OakVideoEditorCommunity/oak.git
cd oak

# Configure
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DOTIO_LOCATION="/path/to/otio-install" \
  -DBUILD_QT6=ON

# Build
cmake --build build --config Release
```

### 5. Run Tests (Optional)

```bash
ctest --test-dir build --output-on-failure -C Release
```

---

## Linux

### Debian / Ubuntu

Install dependencies (Ubuntu 24.04+ ships FFmpeg 6.1, which satisfies the 6.0 minimum; on older releases build FFmpeg from source as described in Troubleshooting):

```bash
sudo apt-get update
sudo apt-get install -y \
  cmake ninja-build pkg-config \
  qt6-base-dev qt6-base-dev-tools qt6-base-private-dev qt6-tools-dev qt6-tools-dev-tools \
  libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libswresample-dev libavfilter-dev \
  libopencolorio-dev libopenimageio-dev libopenexr-dev libexpat1-dev \
  portaudio19-dev libgl1-mesa-dev libvulkan-dev libxkbcommon-dev
```

Build OpenTimelineIO (required, no distro package):

```bash
git clone --depth 1 --branch v0.16.0 https://github.com/PixarAnimationStudios/OpenTimelineIO.git
cmake -S OpenTimelineIO -B OpenTimelineIO/build -G Ninja \
  -DOTIO_SHARED_LIBS=ON \
  -DOTIO_PYTHON_BINDINGS=OFF \
  -DOTIO_FIND_IMATH=ON \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="${PWD}/otio-install"
cmake --build OpenTimelineIO/build
cmake --install OpenTimelineIO/build
```

Configure and build:

```bash
cmake -S . -B build -G Ninja \
  -DBUILD_TESTS=ON -DBUILD_QT6=ON \
  -DOTIO_LOCATION="$PWD/otio-install"
cmake --build build --config Release
```

Run tests:

```bash
ctest --test-dir build --output-on-failure -C Release
```

### Fedora

Install dependencies:

```bash
sudo dnf install -y \
  cmake ninja-build pkgconf-pkg-config \
  qt6-qtbase-devel qt6-qtbase-private-devel qt6-qttools-devel \
  ffmpeg-free-devel \
  OpenImageIO-devel \
  OpenColorIO-devel \
  openexr-devel \
  expat-devel \
  portaudio-devel \
  mesa-libGL-devel \
  vulkan-headers \
  vulkan-loader-devel \
  libxkbcommon-devel \
  gcc-c++ \
  bzip2-devel
```

Build OpenTimelineIO (required, no distro package):

```bash
git clone --depth 1 --branch v0.16.0 https://github.com/PixarAnimationStudios/OpenTimelineIO.git
cmake -S OpenTimelineIO -B OpenTimelineIO/build -G Ninja \
  -DOTIO_SHARED_LIBS=ON \
  -DOTIO_PYTHON_BINDINGS=OFF \
  -DOTIO_FIND_IMATH=ON \
  -DOTIO_FIND_IMATH=ON \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="${PWD}/otio-install"
cmake --build OpenTimelineIO/build
cmake --install OpenTimelineIO/build
```

Configure and build:

```bash
cmake -S . -B build -G Ninja -DBUILD_TESTS=ON -DBUILD_QT6=ON \
  -DOTIO_LOCATION="$PWD/otio-install"
cmake --build build --config Release
```

Run tests:

```bash
ctest --test-dir build --output-on-failure -C Release
```

### Arch Linux

Install dependencies:

```bash
sudo pacman -Syu
sudo pacman -S --needed \
  cmake ninja pkgconf \
  qt6-base qt6-tools \
  ffmpeg \
  openimageio \
  opencolorio \
  openexr \
  expat \
  portaudio \
  opentimelineio \
  mesa \
  vulkan-headers \
  vulkan-icd-loader \
  libxkbcommon \
  fmt \
  gcc
```

> **Note:** On Arch Linux, Qt 6 private headers are included in the `qt6-base` package.

Configure and build:

```bash
cmake -S . -B build -G Ninja -DBUILD_TESTS=ON -DBUILD_QT6=ON
cmake --build build --config Release
```

Run tests:

```bash
ctest --test-dir build --output-on-failure -C Release
```

---

## macOS

macOS is now a fully supported platform. See [`build_macos.md`](build_macos.md) for a dedicated, step-by-step guide.

Install dependencies:

```bash
brew update
brew install cmake ninja pkg-config qt@6 ffmpeg openimageio opencolorio openexr portaudio expat molten-vk vulkan-headers vulkan-loader
```

Build OpenTimelineIO (required):

```bash
git clone --depth 1 --branch v0.16.0 https://github.com/PixarAnimationStudios/OpenTimelineIO.git
cmake -S OpenTimelineIO -B OpenTimelineIO/build -G Ninja \
  -DOTIO_SHARED_LIBS=ON \
  -DOTIO_PYTHON_BINDINGS=OFF \
  -DOTIO_FIND_IMATH=ON \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="${PWD}/otio-install"
cmake --build OpenTimelineIO/build
cmake --install OpenTimelineIO/build
```

Configure and build:

```bash
export PATH="$(brew --prefix qt@6)/bin:$PATH"
export CMAKE_PREFIX_PATH="$(brew --prefix qt@6)"
export OTIO_LOCATION="${PWD}/otio-install"
export OCIO_LOCATION="$(brew --prefix opencolorio)"
# Let CMake's FindVulkan locate the Homebrew Vulkan loader (optional,
# enables the Vulkan render backend)
export VULKAN_SDK="$(brew --prefix vulkan-loader)"

cmake -S . -B build -G Ninja -DBUILD_TESTS=ON -DBUILD_QT6=ON \
  -DOTIO_LOCATION="${OTIO_LOCATION}" \
  -DOCIO_LOCATION="${OCIO_LOCATION}"
cmake --build build --config Release
```

Run tests:

```bash
ctest --test-dir build --output-on-failure -C Release
```

---

## Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `BUILD_TESTS` | `OFF` | Build unit tests |
| `BUILD_DOXYGEN` | `OFF` | Build Doxygen documentation |
| `USE_WERROR` | `OFF` | Treat warnings as errors |
| `BUILD_QT6` | `ON` | Build with Qt 6 instead of Qt 5 |
| `OTIO_LOCATION` | - | Path to OpenTimelineIO installation (required) |
| `OAK_BUNDLE_OTIO` | `ON` | Install OTIO runtime libraries alongside Oak (set `OFF` for distro-native packaging where `opentimelineio` is a package dependency, e.g. Arch) |
| `OCIO_LOCATION` | - | Path to OpenColorIO installation |
| `OAK_ENABLE_DYNAMIC_RENDER_BACKEND` | `ON` | Build dynamic render backend libraries (`liboakgl.so` / `liboakvulkan.so`) |

---

## Troubleshooting

### Qt 6 Not Found

Ensure Qt 6 is in your PATH and CMake prefix path:

```bash
# Linux / macOS
export PATH="/path/to/qt6/bin:$PATH"
export CMAKE_PREFIX_PATH="/path/to/qt6"

# Windows (MSYS2)
export PATH="/ucrt64/bin:$PATH"
```

### Missing Private Headers

If you see errors about missing Qt private headers, install the corresponding private development package for your distribution (e.g., `qt6-base-private-dev` on Debian/Ubuntu, `qt6-qtbase-private-devel` on Fedora).

### FFmpeg Not Found

Make sure FFmpeg development libraries are installed and `pkg-config` can locate them:

```bash
pkg-config --exists libavcodec && echo "Found" || echo "Not found"
```

### FFmpeg Version Too Old

Oak requires FFmpeg 6.0 or newer; CMake configure fails with `Could NOT find FFMPEG (missing: FFMPEG_VERSION) (Required is at least version "6.0")` on older versions. Ubuntu 24.04+ / Fedora / Arch / Homebrew / MSYS2 all ship new enough FFmpeg. If your distro is older, build from source:

```bash
git clone --branch n8.1.1 --depth 1 https://git.ffmpeg.org/ffmpeg.git ffmpeg-src
cd ffmpeg-src
./configure \
  --prefix="$PWD/../ffmpeg-install" \
  --enable-static \
  --disable-shared \
  --disable-doc \
  --disable-programs \
  --disable-avdevice \
  --disable-network \
  --enable-pic \
  --enable-gpl \
  --enable-version3
make -j$(nproc)
make install
cd ..
```

Then pass `-DFFMPEG_ROOT="$PWD/ffmpeg-install"` to CMake.

### OpenTimelineIO Not Found

OpenTimelineIO is a required dependency. Build it from source as shown in your platform's section above and pass `-DOTIO_LOCATION=/path/to/otio-install` to CMake. On Arch Linux the `opentimelineio` package provides it directly.
