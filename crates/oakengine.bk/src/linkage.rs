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

//! Linkage anchors — force the module crates' rlibs into every link.
//!
//! The facade calls the module crates' direct Rust APIs (single-lib; the
//! deleted `src/bridge/` no longer exists), and most modules reach the
//! linker through those normal references. The anchors below reference one
//! direct-Rust symbol of every module crate (and `oakcore-rs`) from a
//! `#[used]` static, which (a) marks each crate as used so its rlib
//! reaches the linker even when the facade only touches it indirectly and
//! (b) keeps the anchor alive so the referenced object files are pulled.
//! For the `liboakengine` cdylib this is what embeds the module crates
//! next to the facade's own `oakengine_*` exports — including the
//! oakcommon XML/undo symbols oaknode's serializer resolves at runtime
//! via dlsym(RTLD_DEFAULT) (see the per-anchor comments in `force_link`).
//!
//! The per-crate symbol mirrors the test-force-link in
//! test_support/common/mod.rs (same paths, same `as usize` cast idiom),
//! so the crate/module paths are proven against the current module
//! layouts.

#![allow(dead_code)]

/// Pull every module crate into the link. Mirrors
/// `tests/common/mod.rs::force_link`; the oakcommon XML/undo anchors are
/// repeated because oaknode's serializer resolves those C ABI symbols at
/// runtime via dlsym(RTLD_DEFAULT) and they must be present in the dylib
/// for that lookup to succeed.
fn force_link() -> usize {
	let fns: [usize; 11] = [
		// oakcore-rs (pure value types; referenced so its rlib is linked).
		oakcore_rs::Rational::new(1, 2).numerator() as usize,
		// One public direct-Rust symbol per module crate. oakundo/oakcommon
		// no longer export a C ABI; their handle-level Rust API functions
		// serve as the link anchors.
		oakundo::undostack::undostack_init as usize,
		oakcommon::configstore::ConfigStore::instance as usize,
		oaktimeline::marker::TimelineMarkerList::new as usize,
		oakcodec::exportformat::Format::get_name as usize,
		oakrender::manager::RenderManager::init as usize,
		oaktask::manager::TaskManager::init as usize,
		oaknode::project::Project::new as usize,
		// oaknode's serializer resolves oakcommon XML/undo symbols at
		// runtime; anchors for the dylib.
		oakcommon::xmlutils::XmlWriter::new as usize,
		oakcommon::xmlutils::XmlReader::new as usize,
		oakundo::undocommand::command_init as usize,
	];
	fns.iter().sum()
}

/// Keeps [`force_link`] (and through it every referenced export) alive in
/// the cdylib/staticlib even though nothing calls it directly.
#[used]
static FORCE_LINK_ANCHOR: fn() -> usize = force_link;
