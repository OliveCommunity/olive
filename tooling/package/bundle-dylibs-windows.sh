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

# Collect every MSYS2/MinGW runtime DLL the given executables depend on
# into a staging directory. cargo packager's NSIS/WiX resources then
# place them next to the .exe — the first place Windows resolves DLLs
# from — so the installer works on machines without MSYS2.
#
# ntldd -R already recurses through a binary's own dependency tree; the
# outer loop reaches the fixpoint for DLLs copied in earlier rounds (a
# freshly copied DLL can itself reference further MSYS2 DLLs that the
# initial scan did not surface through the executables alone).
# C:\Windows\System32 entries are filtered out by the mingw/ucrt/clang
# path match — only the toolchain runtime lands in the package.
#
# Must run inside the MSYS2 UCRT64 shell with
# mingw-w64-ucrt-x86_64-ntldd installed.
#
# Usage: tooling/package/bundle-dylibs-windows.sh <out-dir> <exe>...
set -euo pipefail

OUT="${1:?usage: bundle-dylibs-windows.sh <out-dir> <exe>...}"
shift
[ "$#" -ge 1 ] || { echo "no executables given" >&2; exit 1; }
mkdir -p "$OUT"

if ! command -v ntldd >/dev/null; then
	echo "ntldd not found; install with: pacman -S mingw-w64-ucrt-x86_64-ntldd" >&2
	exit 1
fi

for round in 1 2 3 4 5; do
	changed=0
	# shellcheck disable=SC2046
	while IFS= read -r dep; do
		[ -n "$dep" ] || continue
		unix_dep=$(cygpath -u "$dep")
		base=$(basename "$unix_dep")
		if [ ! -f "$OUT/$base" ]; then
			cp -v "$unix_dep" "$OUT/$base"
			changed=1
		fi
	done < <(ntldd -R "$@" $(ls "$OUT"/*.dll 2>/dev/null) 2>/dev/null \
		| grep -E 'mingw|ucrt|clang' | cut -d'>' -f2 | cut -d' ' -f2 | sort -u)
	[ "$changed" = 1 ] || break
done

echo "bundled $(ls "$OUT" | wc -l | tr -d ' ') DLLs into $OUT"
