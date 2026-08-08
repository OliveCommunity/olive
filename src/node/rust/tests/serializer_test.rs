// Oak Video Editor - Non-Linear Video Editor
// Copyright (C) 2026 Oak Team
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

//! Serializer golden tests. Golden project files captured from the
//! C++ implementation live in tests/golden/ (committed).

/// Save format parity: a fixture project saves byte-identical XML to
/// the C++ 230220 writer output (attribute order included).
#[test]
fn save_matches_cpp_byte_exact() {
	todo!()
}

/// Round-trip: load(save(p)) yields a project whose re-saved XML is
/// identical (idempotence).
#[test]
fn roundtrip_idempotent() {
	todo!()
}

/// Version ladder: the golden files of every historical version
/// (210528/210907/211228/220403) load and upgrade to the current
/// model; node counts, edges, params, markers, work area all match
/// the C++ loader's result.
#[test]
fn historical_versions_upgrade() {
	todo!()
}

/// Corrupt XML and newer-than-build versions are rejected with the
/// documented error codes, never a panic.
#[test]
fn corrupt_and_future_files_rejected() {
	todo!()
}
