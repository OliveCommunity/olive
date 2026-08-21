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

# Bundle every non-system dynamic library an .app depends on into
# Contents/Frameworks and rewrite the install names to
# @executable_path/../Frameworks, so the app runs on machines without
# Homebrew. (FFmpeg/OCIO are already static; what remains are the
# Homebrew codec/filter dylibs the static FFmpeg references.)
#
# System libraries (/usr/lib, /System) are never copied. The set is
# computed to a fixpoint: freshly copied dylibs can themselves reference
# further non-system dylibs. @executable_path resolves to Contents/MacOS
# even when the reference lives in a Frameworks dylib, so one rewrite
# pattern fits both the executables and the libraries.
#
# Every modified Mach-O is ad-hoc re-signed afterwards: rewriting install
# names invalidates the code signature, and unsigned-but-damaged binaries
# are killed at launch on Apple Silicon.
#
# Usage: tooling/package/bundle-dylibs-macos.sh path/to/Oak.app
set -euo pipefail

APP="${1:?usage: bundle-dylibs-macos.sh path/to/Oak.app}"
MACOS_DIR="$APP/Contents/MacOS"
FW_DIR="$APP/Contents/Frameworks"
[ -d "$MACOS_DIR" ] || { echo "$APP: no Contents/MacOS directory" >&2; exit 1; }
mkdir -p "$FW_DIR"

is_system() {
	case "$1" in
		/usr/lib/*|/System/*|@*) return 0 ;;
		*) return 1 ;;
	esac
}

# Rewrite every non-system reference of the Mach-O file $1 to
# @executable_path/../Frameworks, copying the target in when new.
collect() {
	local file="$1" dep base
	otool -L "$file" | awk 'NR>1 {print $1}' | while read -r dep; do
		is_system "$dep" && continue
		[ -f "$dep" ] || continue
		base=$(basename "$dep")
		if [ ! -f "$FW_DIR/$base" ]; then
			cp -L "$dep" "$FW_DIR/$base" # -L: follow Homebrew's symlinks
			chmod u+w "$FW_DIR/$base"
			echo "bundled $base"
		fi
		install_name_tool -change "$dep" "@executable_path/../Frameworks/$base" "$file"
	done
}

# Fixpoint over the executables plus whatever the previous round copied.
prev=-1
for round in 1 2 3 4 5; do
	count=$(find "$FW_DIR" -name '*.dylib' | wc -l | tr -d ' ')
	[ "$count" = "$prev" ] && break
	prev=$count
	for f in "$MACOS_DIR"/*; do
		[ -f "$f" ] && collect "$f"
	done
	for f in "$FW_DIR"/*.dylib; do
		[ -f "$f" ] && collect "$f"
	done
done

# Re-sign: install_name_tool invalidates the seal. Per-file signing
# instead of `codesign --deep` (deprecated and unreliable with dylibs).
find "$FW_DIR" "$MACOS_DIR" -type f -exec codesign --force --sign - {} \;
codesign --force --sign - "$APP"

echo "bundled $prev dylibs into $FW_DIR"
