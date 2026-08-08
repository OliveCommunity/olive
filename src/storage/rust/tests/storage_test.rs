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

//! oakstorage contract tests (M10 §4 mapping).

/// Byte-exact round-trip: open a golden .ove, save to a temp URI, the
/// two files are byte-identical; reopening yields a non-empty project.
#[test]
fn ove_xml_roundtrip_byte_exact() {
	todo!()
}

/// Compressed .ove round-trip (OAKSTORAGE_SAVE_COMPRESS) likewise
/// loads to the identical project content.
#[test]
fn ove_xml_compressed_roundtrip() {
	todo!()
}

/// probe: .ove (plain/compressed), .otio, unknown scheme →
/// E_NO_BACKEND; NULL/empty URI → E_INVALID.
#[test]
fn probe_dispatch() {
	todo!()
}

/// Error paths: nonexistent file → open fails with last_error set;
/// too-new version header → OAKSTORAGE_TOO_NEW; corrupt XML →
/// E_FORMAT.
#[test]
fn open_error_paths() {
	todo!()
}

/// Pluggability proof (M10 §4): register a mock `mem://` backend,
/// probe/open/save all route through its vtable; after unregister,
/// probe reports E_NO_BACKEND. This test IS the database-swap
/// interface verification.
#[test]
fn backend_vtable_pluggability() {
	todo!()
}

/// SQLite backend: save → load round-trip through
/// `oakdb+sqlite:///…` yields the same project payload as ove-xml
/// (one serialization truth).
#[test]
fn sqlite_roundtrip() {
	todo!()
}

/// PostgreSQL backend: same round-trip against a local test database;
/// skipped when no PG is reachable (env-gated).
#[test]
fn postgres_roundtrip() {
	todo!()
}

/// alive count: open/take/free pairing leaves no leak.
#[test]
fn alive_count_accounting() {
	todo!()
}
