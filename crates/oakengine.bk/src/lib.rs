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

//! # oakengine — the `liboakengine` cdylib (plugin / external C ABI)
//!
//! The frozen `oakengine_*` C ABI (`engine/include/oakengine/*.h`) as a
//! **pure cdylib** (M14 R4). This is the plugin / external-consumer layer:
//! OFX plugins and third-party embedders link `liboakengine` and call the
//! C ABI; the app, oak-cli and oak-worker do not use it anymore — they
//! link the module crates directly as Rust rlibs.
//!
//! Downward, every `oakengine_*` export is a direct Rust call into the
//! module crates (oakundo/oaknode/oaktimeline/oakcodec/oakaudio/oakrender/
//! oaktask/oakcommon/oakplugin/oakstorage/oakcore-rs) through [`stubs`]
//! (the rewired replacement for the deleted `bridge/`); the C ABI itself
//! stays frozen (only additive changes + major version bumps).
//! Cross-cutting state that used to live here (the process-wide undo stack,
//! the open undo group) has sunk into the modules (M14 R1:
//! [`oakundo::global`]); [`undo`] is a thin forward that adds the engine's
//! box/unbox, buf/size and error-code conventions.
//!
//! ## Handle mapping
//!
//! The engine headers' opaque pointers (`OakEngineNode*`, `OakEngineTrack*`,
//! ...) become thin newtype wrappers around module [`handle::CHandle`]
//! values (see [`handle`]). Each exported function keeps the exact
//! signature from the engine header; inside, it unboxes the module value,
//! calls the module's direct Rust API and boxes the result.
//!
//! ## FFI discipline
//!
//! Every export goes through a `catch_unwind` guard ([`handle::guard*`]),
//! `free` functions are NULL no-ops, strings use the two-stage buf/size
//! convention ([`handle::write_string`]), and module error codes pass
//! through untranslated ([`error`], facade module 00 → -1..-6).
//!
//! ## Testing
//!
//! The crate is cdylib-only, so the former `tests/*.rs` integration tests
//! run as in-crate unit tests under `src/test_support/` (pulled in from
//! here under `#[cfg(test)]`; they address the crate's modules through
//! `crate::*`). The module crates are real dependencies (see Cargo.toml),
//! so the test binary statically links the same rlibs the cdylib embeds;
//! [`linkage`] anchors every crate into the cdylib link, and the test-only
//! [`test_link`] module does the same for the unit-test binary. Where a
//! wrapped family needs module behavior the crates do not implement yet,
//! the engine function is a documented stub with its reason (see
//! `deferred.rs` and README.md).

#![deny(unsafe_op_in_unsafe_fn)]
#![warn(missing_docs)]

pub mod audio;
pub mod codec;
pub mod common;
pub mod deferred;
pub mod error;
pub mod handle;
pub mod ipc;
#[cfg(not(test))]
pub mod linkage;
pub mod library;
pub mod node;
pub mod plugin;
pub mod pods;
pub mod render;
pub mod stubs;
pub mod storage;
pub mod task;
pub mod testmedia;
pub mod timeline;
pub mod undo;
pub mod worker;

/// The former `tests/*.rs` integration tests, now unit tests (the facade
/// is cdylib-only, so integration tests cannot link it as an rlib crate;
/// see `test_support/mod.rs`).
#[cfg(test)]
#[path = "test_support/mod.rs"]
mod tests;
#[cfg(test)]
mod test_link {
	// The lib's own unit-test binary must link the module crates' rlibs to
	// satisfy the facade's imports that the unit tests compile in — e.g.
	// the render family's oakrender display renderer (src/render.rs).
	// The in-crate tests do the same through test_support/common/mod.rs
	// `force_link()`; this covers the `cargo test` unit-test binary.
	#![allow(dead_code)]
	fn force_link() -> usize {
		let fns: [usize; 4] = [
			oakrender::backend::DisplayRenderer::new as *const () as usize,
			oaknode::project::Project::new as *const () as usize,
			oaktimeline::marker::TimelineMarkerList::new as *const () as usize,
			oaktask::manager::TaskManager::init as *const () as usize,
		];
		fns.iter().sum()
	}
}
