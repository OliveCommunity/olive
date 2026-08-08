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

//! Project / sequence / track / block contract tests.

/// Project lifecycle: init → initialize → add nodes → clear →
/// re-initialize; modified flag transitions match C++.
#[test]
fn project_lifecycle() {
	todo!()
}

/// deep_copy: the copy is structurally identical (nodes/edges/params)
/// but shares no mutable state; editing the original does not leak
/// into the copy before sync_copy.
#[test]
fn project_deep_copy_isolation() {
	todo!()
}

/// sync_copy applies a recorded change set (add/remove node, edge
/// change, value change) and produces the same graph as a fresh
/// deep_copy.
#[test]
fn project_sync_copy_consistency() {
	todo!()
}

/// Sequence defaults: create → one video + one audio track via the
/// oaktimeline C ABI command path; default parameters match C++
/// (set_default_parameters parity).
#[test]
fn sequence_default_structure() {
	todo!()
}

/// Track block ordering: insert/append keep timeline order; removing
/// a middle block preserves the rest; gap insertion shifts successors
/// (C++ Track semantics, undoable through bridge::undo).
#[test]
fn track_block_ordering() {
	todo!()
}

/// ClipBlock cache passthrough: add_cache_passthrough_from wires the
/// four caches via the oakrender C ABI (passthrough list observable
/// through cache introspection).
#[test]
fn clip_cache_passthrough() {
	todo!()
}

/// Footage probe: a synthetic media file (fixture) reports the
/// expected streams; corrupt file yields E_FAILED without partial
/// state.
#[test]
fn footage_probe() {
	todo!()
}
