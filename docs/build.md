# Build Guide

This document describes how to build Oak Video Editor from source.  
For the Chinese version, see [`build-zh.md`](./build-zh.md).

## Prerequisites

- CMake 3.20+
- Ninja (recommended)
- Qt 6 (with private headers and Quick)
- FFmpeg development libraries (6.0+)
- OpenImageIO
- OpenColorIO (2.x)
- OpenEXR
- Expat
- PortAudio
- OpenGL headers
- XKB common (Linux)
- OpenCV (for OpenImageIO headers, Linux only)

---

## Linux

### Debian/Ubuntu

Install dependencies:

```bash
sudo apt-get update
sudo apt-get install -y \
  ninja-build pkg-config \
  qt6-base-dev qt6-base-dev-tools qt6-base-private-dev qt6-tools-dev qt6-tools-dev-tools qt6-declarative-dev \
  libavcodec-dev libavformat-dev libavfilter-dev libavutil-dev libswscale-dev libswresample-dev \
  libopencolorio-dev libopenimageio-dev libopenexr-dev libexpat1-dev libopencv-dev \
  portaudio19-dev libgl1-mesa-dev libxkbcommon-dev openimageio-tools
```

Install OpenTimelineIO:

```bash
git clone --recursive https://github.com/AcademySoftwareFoundation/OpenTimelineIO.git
cd OpenTimelineIO
mkdir build
cd build
cmake ..
make -j8
make install
```

Configure and build:

```bash
cmake -S . -B build -G Ninja -DBUILD_TESTS=ON
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
  ninja-build pkgconfig \
  qt6-qtbase-devel qt6-qttools-devel qt6-qtdeclarative-devel qt6-qtbase-private-devel \
  ffmpeg-devel \
  OpenColorIO-devel OpenImageIO-devel OpenEXR-devel expat-devel opencv-devel \
  portaudio-devel mesa-libGL-devel libxkbcommon-devel
```
Install OpenTimelineIO:

```bash
git clone --recursive https://github.com/AcademySoftwareFoundation/OpenTimelineIO.git
cd OpenTimelineIO
mkdir build
cd build
cmake ..
make -j8
make install
```

Configure and build:

```bash
cmake -S . -B build -G Ninja -DBUILD_TESTS=ON
cmake --build build --config Release
```

Run tests:

```bash
ctest --test-dir build --output-on-failure -C Release
```

### Arch Linux

Install dependencies:

```bash
sudo pacman -S \
  ninja pkgconf \
  qt6-base qt6-tools qt6-declarative \
  ffmpeg \
  opencolorio openimageio openexr expat opencv \
  portaudio mesa libxkbcommon opentimelineio
```

Configure and build:

```bash
cmake -S . -B build -G Ninja -DBUILD_TESTS=ON
cmake --build build --config Release
```

Run tests:

```bash
ctest --test-dir build --output-on-failure -C Release
```

---

## Windows (MSYS2 - Recommended)

MSYS2 is the recommended way to build Oak Video Editor on Windows.

### 1. Install MSYS2

Download and install MSYS2 from [https://www.msys2.org/](https://www.msys2.org/)

### 2. Open MSYS2 UCRT64 terminal

From the Start menu, open "MSYS2 UCRT64" terminal.

### 3. Clone and build

```bash
# Clone the repository
git clone https://github.com/OakVideoEditorCommunity/oak.git
cd oak

# Install dependencies
./setup-msys2-windows.sh

# Configure
cmake -S . -B build -G Ninja \
  -DBUILD_TESTS=ON \
  -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build --config Release
```

### 4. Run tests (optional)

```bash
ctest --test-dir build --output-on-failure -C Release
```


## Build Options

| Option | Default | Description |
|--------|---------|-------------|

| `BUILD_TESTS` | `OFF` | Build unit tests |
| `BUILD_DOXYGEN` | `OFF` | Build Doxygen documentation |
| `USE_WERROR` | `OFF` | Treat warnings as errors |

## OpenTimelineIO not found

OpenTimelineIO is optional. If you don't need OTIO support, you can build without it. If you need it, you may need to build it from source.