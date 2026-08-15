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

//! Unit-test aggregation for the oakengine facade (single-lib unification).
//!
//! The facade's crate-type is cdylib-only (no rlib), so the former
//! `tests/*.rs` integration tests cannot link `oakengine` as a crate.
//! They moved here (`src/test_support/`, pulled in by `src/lib.rs` under
//! `#[cfg(test)]`) and run as unit tests against `crate::*` instead of
//! `oakengine::*`. The old `#[path = "common/mod.rs"] mod common;` include
//! is replaced by the single [`common`] declaration below — the
//! `oakcore_*` mock symbols it defines may exist only once per binary.
//!
//! The node/timeline/render-graph families (and the graph-op tests that
//! built fixtures through the deleted handle-based module C ABIs) are
//! part of the pending domain-model redesign; their test files were
//! deleted with this migration (see the migration report).

#![allow(dead_code)]

#[path = "common/mod.rs"]
pub mod common;

mod audio;
mod codec;
mod common_smoke;
mod it_audio;
mod it_codec;
mod it_common;
mod it_export;
mod it_plugin;
mod it_task;
mod it_undo;
mod linkage;
mod node;
mod plugin;
mod render;
mod task;
mod undo;
