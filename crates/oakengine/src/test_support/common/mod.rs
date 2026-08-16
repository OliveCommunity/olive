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

//! Shared test support, included from every integration test via
//! `#[path = "common/mod.rs"] mod common;`.
//!
//! Two jobs:
//!
//! 1. **Force rustc to link every module crate's rlib** into the test
//!    binary ([`force_link`]). The test-support files import the module
//!    crates directly (single-lib; the deleted `src/bridge` no longer
//!    exists), and the array doubles as a compile-time proof that the
//!    anchor paths in `crates/oakengine/src/linkage.rs` match the current
//!    module layouts.
//!
//! 2. **Re-export the folded-in `oakcore_audioparams_*` accessors** for
//!    the former mock call sites (`common::oakcore_audioparams_*`). The
//!    facade used to leave those symbols as runtime lookups for a C++
//!    liboakcore host, and the tests defined in-memory mocks; M12 P5
//!    implemented them inside the dylib (crates/oakengine/src/stubs.rs,
//!    module `audio`), so the tests just use those implementations.

#![allow(dead_code, unused_variables)]

use std::sync::Mutex;

/// One public direct-Rust symbol per module crate (the module C ABIs are
/// deleted; this mirrors the anchors in `crates/oakengine/src/linkage.rs`).
/// Under unit tests the crates are real dependencies of the lib target and
/// are linked regardless; the array doubles as a compile-time proof that
/// the anchor paths match the current module layouts.
#[allow(unused)]
pub fn force_link() -> usize {
	let fns: [usize; 12] = [
		oakundo::undostack::undostack_init as usize,
		oakcodec::exportformat::Format::get_name as usize,
		oakaudio::processor::Processor::init as usize,
		oakrender::manager::RenderManager::init as usize,
		oakcommon::configstore::ConfigStore::instance as usize,
		oakplugin::host::Host::global as usize,
		oaknode::project::Project::new as usize,
		oaktimeline::marker::TimelineMarkerList::new as usize,
		oaktask::manager::TaskManager::init as usize,
		// oakundo/oakcommon no longer export a C ABI; their handle-level
		// Rust API functions anchor the rlibs into every test binary (the
		// same pattern as `crates/oakengine/src/linkage.rs`).
		oakcommon::xmlutils::XmlWriter::new as usize,
		oakcommon::xmlutils::XmlReader::new as usize,
		oakundo::undocommand::command_init as usize,
	];
	fns.iter().sum()
}

/// Serialize every test that touches the process-wide AudioManager
/// singleton. The former integration tests were separate processes; as
/// unit tests they share one process (and one singleton), so the manager
/// tests must take a shared lock instead of relying on process isolation.
pub fn with_manager(f: impl FnOnce()) {
	static LOCK: Mutex<()> = Mutex::new(());
	let _g = LOCK.lock().unwrap_or_else(|e| e.into_inner());
	f()
}

/// Serializes every test that reads or writes the `Storage` config group
/// (the facade's write-through library selection — see src/storage.rs).
/// The config store is process-global, so the write-through tests and the
/// tests that disable the backend must take this lock for their whole
/// body instead of racing on the shared store.
pub static STORAGE_CONFIG_LOCK: Mutex<()> = Mutex::new(());

/// Run `f` with the write-through storage backend disabled
/// (`Storage/Backend = "off"`). Tests that push undo commands on real
/// projects (e.g. `oakengine_project_add_node`) would otherwise bind them
/// to the default user library and write there; disabling the backend
/// keeps them side-effect-free. The value intentionally persists — every
/// storage test sets its own backend explicitly under
/// [`STORAGE_CONFIG_LOCK`].
pub fn with_storage_off<R>(f: impl FnOnce() -> R) -> R {
	let _g = storage_off_guard();
	f()
}

/// Take the storage-config lock AND disable the write-through backend,
/// returning the guard: a test that pushes undo commands throughout its
/// body holds the guard (and thus the lock) for its whole lifetime, so a
/// concurrently running storage test cannot flip the backend mid-test.
pub fn storage_off_guard() -> std::sync::MutexGuard<'static, ()> {
	let g = STORAGE_CONFIG_LOCK.lock().unwrap_or_else(|e| e.into_inner());
	oakcommon::configstore::ConfigStore::instance().set(Some("Storage"), "Backend", "off");
	g
}

// ---------------------------------------------------------------------------
// oakcore_audioparams_* (see module docs)
// ---------------------------------------------------------------------------

/// The facade's in-dylib `oakcore_audioparams_*` C ABI (see
/// `crate::stubs::audio`): the accessors were host-provided mocks until
/// M12 P5 folded them into the engine, so the tests now share the real
/// implementations instead of defining per-binary duplicates. Only the
/// two entry points the test files call (`create`/`free`) are re-exported;
/// the read accessors are reached through `crate::stubs::audio` where the
/// tests need them.
pub use crate::stubs::audio::{oakcore_audioparams_create, oakcore_audioparams_free};
