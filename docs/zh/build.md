# 构建指南

本文档介绍如何在 Windows、Linux 和 macOS 上从源码构建 Oak Video Editor。

## 依赖

- CMake 3.20+
- Ninja（推荐）
- Qt 6（含私有头文件）
- FFmpeg 6.0+ 开发库
- OpenTimelineIO（0.16+，按下文从源码构建——大多数平台没有发行版软件包）
- OpenImageIO
- OpenColorIO（2.x）
- OpenEXR
- Expat
- PortAudio
- OpenGL 头文件
- Vulkan SDK（可选，Vulkan 渲染后端需要）
- XKB common（Linux）

---

## Windows（MSYS2）

本指南使用 [MSYS2](https://www.msys2.org/) 的 UCRT64 工具链。

### 1. 安装 MSYS2

从 [https://www.msys2.org/](https://www.msys2.org/) 下载并安装 MSYS2，然后打开 **MSYS2 UCRT64** 终端。

### 2. 安装依赖

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

> **注意：** Qt 6 私有头文件可能需要额外安装。如果 CMake 报告找不到私有头文件，请尝试安装 `mingw-w64-ucrt-x86_64-qt6-base-private`（如果仓库中有）。

### 3. 构建 OpenTimelineIO（必需）

MSYS2 仓库没有 OpenTimelineIO 软件包，需从源码构建：

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

### 4. 克隆并构建

```bash
# 克隆仓库
git clone --recursive https://github.com/OakVideoEditorCommunity/oak.git
cd oak

# 配置
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DOTIO_LOCATION="/path/to/otio-install" \
  -DBUILD_QT6=ON

# 构建
cmake --build build --config Release
```

### 5. 运行测试（可选）

```bash
ctest --test-dir build --output-on-failure -C Release
```

---

## Linux

### Debian / Ubuntu

安装依赖（Ubuntu 24.04+ 自带的 FFmpeg 6.1 已满足 6.0 最低要求；更旧的发行版请按"故障排除"一节从源码编译 FFmpeg）：

```bash
sudo apt-get update
sudo apt-get install -y \
  cmake ninja-build pkg-config \
  qt6-base-dev qt6-base-dev-tools qt6-base-private-dev qt6-tools-dev qt6-tools-dev-tools \
  libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libswresample-dev libavfilter-dev \
  libopencolorio-dev libopenimageio-dev libopenexr-dev libexpat1-dev \
  portaudio19-dev libgl1-mesa-dev libvulkan-dev libxkbcommon-dev
```

从源码构建 OpenTimelineIO（必需，无发行版软件包）：

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

配置并构建：

```bash
cmake -S . -B build -G Ninja \
  -DBUILD_TESTS=ON -DBUILD_QT6=ON \
  -DOTIO_LOCATION="$PWD/otio-install"
cmake --build build --config Release
```

运行测试：

```bash
ctest --test-dir build --output-on-failure -C Release
```

### Fedora

安装依赖：

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

从源码构建 OpenTimelineIO（必需，无发行版软件包）：

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

配置并构建：

```bash
cmake -S . -B build -G Ninja -DBUILD_TESTS=ON -DBUILD_QT6=ON \
  -DOTIO_LOCATION="$PWD/otio-install"
cmake --build build --config Release
```

运行测试：

```bash
ctest --test-dir build --output-on-failure -C Release
```

### Arch Linux

安装依赖：

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

> **注意：** Arch Linux 的 `qt6-base` 包已经包含私有头文件。

配置并构建：

```bash
cmake -S . -B build -G Ninja -DBUILD_TESTS=ON -DBUILD_QT6=ON
cmake --build build --config Release
```

运行测试：

```bash
ctest --test-dir build --output-on-failure -C Release
```

---

## macOS

macOS 现在是正式支持的平台。更详细的逐步指南请参见 [`build_macos-zh.md`](build_macos-zh.md)。

安装依赖：

```bash
brew update
brew install cmake ninja pkg-config qt@6 ffmpeg openimageio opencolorio openexr portaudio expat molten-vk vulkan-headers vulkan-loader
```

构建 OpenTimelineIO（必需）：

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

配置并构建：

```bash
export PATH="$(brew --prefix qt@6)/bin:$PATH"
export CMAKE_PREFIX_PATH="$(brew --prefix qt@6)"
export OTIO_LOCATION="${PWD}/otio-install"
export OCIO_LOCATION="$(brew --prefix opencolorio)"
# 让 CMake 的 FindVulkan 找到 Homebrew 的 Vulkan loader（可选，
# 启用 Vulkan 渲染后端）
export VULKAN_SDK="$(brew --prefix vulkan-loader)"

cmake -S . -B build -G Ninja -DBUILD_TESTS=ON -DBUILD_QT6=ON \
  -DOTIO_LOCATION="${OTIO_LOCATION}" \
  -DOCIO_LOCATION="${OCIO_LOCATION}"
cmake --build build --config Release
```

运行测试：

```bash
ctest --test-dir build --output-on-failure -C Release
```

---

## 构建选项

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `BUILD_TESTS` | `OFF` | 构建单元测试 |
| `BUILD_DOXYGEN` | `OFF` | 构建 Doxygen 文档 |
| `USE_WERROR` | `OFF` | 将警告视为错误 |
| `BUILD_QT6` | `ON` | 使用 Qt 6 而非 Qt 5 |
| `OTIO_LOCATION` | - | OpenTimelineIO 安装路径（必需） |
| `OAK_BUNDLE_OTIO` | `ON` | 随 Oak 一并安装 OTIO 运行库（发行版原生打包且 `opentimelineio` 是包依赖时设 `OFF`，如 Arch） |
| `OCIO_LOCATION` | - | OpenColorIO 安装路径 |
| `OAK_ENABLE_DYNAMIC_RENDER_BACKEND` | `ON` | 构建动态渲染后端库（`liboakgl.so` / `liboakvulkan.so`） |

---

## 故障排除

### 找不到 Qt 6

确保 Qt 6 在 PATH 和 CMake prefix path 中：

```bash
# Linux / macOS
export PATH="/path/to/qt6/bin:$PATH"
export CMAKE_PREFIX_PATH="/path/to/qt6"

# Windows（MSYS2）
export PATH="/ucrt64/bin:$PATH"
```

### 缺少私有头文件

如果看到缺少 Qt 私有头文件的错误，请安装对应发行版的私有开发包（例如 Debian/Ubuntu 上的 `qt6-base-private-dev`，Fedora 上的 `qt6-qtbase-private-devel`）。

### FFmpeg 版本太旧

Oak 要求 FFmpeg 6.0 或更新版本；版本不足时 CMake 配置阶段会报 `Could NOT find FFMPEG ... (Required is at least version "6.0")`。Ubuntu 24.04+ / Fedora / Arch / Homebrew / MSYS2 自带的版本都足够新。如果发行版过旧，请从源码编译：

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

然后在 CMake 中加上 `-DFFMPEG_ROOT="$PWD/ffmpeg-install"`。

### 找不到 OpenTimelineIO

OpenTimelineIO 是必需依赖。按你所在平台章节的说明从源码构建，并在 CMake 中加上 `-DOTIO_LOCATION=/path/to/otio-install`。Arch Linux 可直接安装 `opentimelineio` 包。
