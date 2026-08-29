#!/usr/bin/env bash
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

# Print the OpenColorIO build environment for cargo, one VAR=value per
# line — append to $GITHUB_ENV in CI/CD (`tooling/ocio-env.sh >> "$GITHUB_ENV"`)
# or eval locally.
#
# Policy:
#   1. Prefer a SYSTEM OpenColorIO >= 2.5 (the bridge's API floor). The
#      vendored 2.5.2 source build FAILS on GCC >= 16 — its bundled
#      yaml-cpp relied on stdint.h being included transitively, which
#      GCC 16 no longer does (measured: yaml-cpp emitterutils.cpp).
#      Link the system OCIO STATICALLY when the package ships
#      libOpenColorIO.a (e.g. MSYS2); distro packages that only ship the
#      shared object (Arch, Debian, Fedora) leave dynamic as the only
#      way to use the prebuilt library.
#   2. With no suitable system OCIO (too old or absent), fall back to
#      the vendored-source static build (ocio-sys `bundled`, no
#      OCIO_INSTALL_DIR) — which needs GCC < 16.
#
# Pre-set OCIO_RS_LINK / OCIO_INSTALL_DIR are honored, never clobbered.

set -euo pipefail

if [ -n "${OCIO_RS_LINK:-}" ]; then
	# The caller already chose a link mode; only make sure the real
	# bridge (not the stub) is on.
	echo "OCIO_RS_ENABLE_REAL=1"
	exit 0
fi

echo "OCIO_RS_ENABLE_REAL=1"

if pkg-config --exists 'OpenColorIO >= 2.5' 2>/dev/null; then
	libdir=$(pkg-config --variable=libdir OpenColorIO)
	prefix=$(pkg-config --variable=prefix OpenColorIO)
	link=dynamic
	for dir in "$libdir" "$prefix/lib" "$prefix"; do
		if [ -f "$dir/libOpenColorIO.a" ]; then
			link=static
			break
		fi
	done
	echo "OCIO_INSTALL_DIR=$prefix"
	echo "OCIO_RS_LINK=$link"
	exit 0
fi

# No usable system OCIO: vendored-source static build (needs GCC < 16).
echo "OCIO_RS_LINK=static"
