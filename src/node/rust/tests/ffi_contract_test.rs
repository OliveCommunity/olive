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

//! C ABI contract tests (ffi.rs). One normal + one error path per
//! export family; the exhaustive matrix is driven from the existing
//! C++ gtest suite (`src/node/tests`, unchanged) running against this
//! crate — these tests only pin Rust-side specifics.

/// Every exported handle-returning function returns ctx==NULL on
/// failure and a valid refcounted handle on success (abi_version
/// stamped).
#[test]
fn handle_contract_all_exports() {
	todo!()
}

/// free(NULL)/free(empty) are no-ops across every free export.
#[test]
fn free_null_noop_all_exports() {
	todo!()
}

/// Two-stage string functions: size query, short buffer truncation
/// rule, and exact-fit write — for every string getter.
#[test]
fn two_stage_string_contract() {
	todo!()
}

/// Identity registry: node_identity / node_from_identity round-trip;
/// freed nodes are rejected by from_identity.
#[test]
fn identity_registry_roundtrip() {
	todo!()
}

/// alive count: project create/destroy moves
/// oaknode_debug_alive_count predictably and returns to baseline.
#[test]
fn alive_count_accounting() {
	todo!()
}
