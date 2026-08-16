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

//! # oakengine — the `liboakengine` facade (Rust)
//!
//! Re-exports the frozen `oakengine_*` C ABI
//! (`engine/include/oakengine/*.h`) verbatim on top of the module C ABIs
//! (`include/<mod>/*.h`, implemented by the oakundo/oaknode/oaktimeline/
//! oakcodec/oakaudio/oakrender/oaktask/oakcommon/oakplugin crates). It is
//! the M9 §4 assembly layer: every module call crosses the module C ABI as
//! an `extern "C"` import (see [`bridge`]); the facade itself owns only
//! cross-cutting state (the process-wide undo stack and the open undo
//! group, see [`undo`]).
//!
//! ## Handle mapping
//!
//! The engine headers' opaque pointers (`OakEngineNode*`, `OakEngineTrack*`,
//! ...) become thin newtype wrappers around module [`handle::CHandle`]
//! values (see [`handle`]). Each exported function keeps the exact
//! signature from the engine header; inside, it unboxes the module handle,
//! calls the module C ABI and boxes the result.
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
//! The module crates are real dependencies (see Cargo.toml) and
//! [`linkage`] anchors them into every link of this crate, so the module
//! C ABIs are embedded in the `liboakengine` cdylib next to the facade's
//! own exports. `cargo test` links the same crates' rlibs (plus the
//! `test-stubs` feature union declared in the dev-dependencies, which
//! compiles the oakcommon/oakplugin in-crate mocks); `tests/linkage.rs`
//! additionally references every crate for the integration-test binaries
//! and `test_link` (below) covers the unit-test binary. Where a wrapped
//! family needs module behavior the crates do not implement yet, the
//! engine function is a documented stub and its test carries `#[ignore]`
//! with a reason (see README.md).

#![deny(unsafe_op_in_unsafe_fn)]
#![warn(missing_docs)]

// Re-export the node crate so integration tests (and embedders) address
// the SAME compiled instance the facade uses: under `cargo test
// --workspace`, oaknode is built twice (oaknode's own dev-dependency
// enables oakcodec/test-stubs), and a test that mixes `oaknode::` direct
// imports with `oakengine::` re-exports gets two incompatible type
// instances (E0308 "multiple different versions of crate oaknode").
pub use oaknode;

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
	// The integration tests do the same through tests/common/mod.rs
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
