#!/bin/bash
set -e

echo "=== 更新 MSYS2 ==="
pacman -Syu --noconfirm

echo "=== 安装基础工具链 ==="
pacman -S --needed --noconfirm \
    base-devel \
    mingw-w64-ucrt-x86_64-toolchain \
    mingw-w64-ucrt-x86_64-cmake \
    mingw-w64-ucrt-x86_64-ninja \
    mingw-w64-ucrt-x86_64-pkgconf \
    git

echo "=== 安装核心依赖 ==="
pacman -S --needed --noconfirm \
    mingw-w64-ucrt-x86_64-ffmpeg \
    mingw-w64-ucrt-x86_64-openimageio \
    mingw-w64-ucrt-x86_64-opencolorio \
    mingw-w64-ucrt-x86_64-openexr \
    mingw-w64-ucrt-x86_64-imath \
    mingw-w64-ucrt-x86_64-expat \
    mingw-w64-ucrt-x86_64-portaudio \
    mingw-w64-ucrt-x86_64-pugixml \
    mingw-w64-ucrt-x86_64-rapidjson \
    mingw-w64-ucrt-x86_64-spdlog \
    mingw-w64-ucrt-x86_64-fmt \
    mingw-w64-ucrt-x86_64-opentimelineio \
    mingw-w64-ucrt-x86_64-gtest
echo "=== 安装 Qt6 + Qt Quick ==="
pacman -S --needed --noconfirm \
    mingw-w64-ucrt-x86_64-qt6-base \
    mingw-w64-ucrt-x86_64-qt6-declarative \
    mingw-w64-ucrt-x86_64-qt6-multimedia \
    mingw-w64-ucrt-x86_64-qt6-tools \
    mingw-w64-ucrt-x86_64-qt6-svg \
    mingw-w64-ucrt-x86_64-qt6-imageformats

echo "=== 环境配置完成 ==="
echo "注意：opentimelineio 和 crashpad 需要从源码编译"