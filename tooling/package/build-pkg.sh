#!/bin/bash
# Oak Video Editor - Non-Linear Video Editor
# Copyright (C) 2026 Oak Team
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

# Build the Arch package: substitute the version into the PKGBUILD and
# run makepkg (as a non-root user — makepkg refuses root, and CI
# containers are root). Run from the repo root after
# `cargo build --release`. Usage: tooling/package/build-pkg.sh <version>
set -euo pipefail

VERSION="${1:?usage: build-pkg.sh <version>}"
WORK=target/pkg/arch
rm -rf "$WORK"
mkdir -p "$WORK"
sed "s/@VERSION@/$VERSION/" tooling/package/PKGBUILD > "$WORK/PKGBUILD"

# OCIO is linked dynamically whenever the build used the system package
# (Arch ships no static libOpenColorIO); declare the dependency then.
if ldd target/release/oak-editor 2>/dev/null | grep -q libOpenColorIO; then
	sed -i "s/'fontconfig' 'freetype2')/'fontconfig' 'freetype2' 'opencolorio')/" "$WORK/PKGBUILD"
fi

if [ "$(id -u)" = "0" ]; then
	useradd -m builder 2>/dev/null || true
	chown -R builder:builder "$WORK" target/release
	su builder -c "cd '$PWD/$WORK' && OAK_BIN='$PWD/target/release' OAK_ROOT='$PWD' makepkg -f --nodeps"
else
	(cd "$WORK" && OAK_BIN="$PWD/target/release" OAK_ROOT="$PWD" makepkg -f --nodeps)
fi

find "$WORK" -name '*.pkg.tar.zst' -exec mv {} target/release/ \;
ls target/release/*.pkg.tar.zst
