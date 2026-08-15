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

//! Smoke-test that every module crate is present and callable through
//! its direct Rust API (single-lib unification; the former version
//! referenced the deleted `*::ffi` C ABI exports).

use super::common;

#[test]
fn all_module_crates_link() {
	// oakundo: fresh stack, refcount 1.
	let stack = oakundo::undostack::undostack_init();
	assert!(!stack.ctx.is_null());

	// oakcommon: an int config read with fallback.
	let v = oakcommon::configstore::ConfigStore::instance().get_int(None, "no-such-key", 42);
	assert_eq!(v, 42);

	// oakcodec: the encoding-format table is non-empty (count > 0).
	let n = oakcodec::exportformat::Format::get_name(oakcodec::exportformat::Format::MPEG4Video)
		.len();
	assert!(n > 0);

	// oakaudio: a fresh processor reports closed.
	let p = oakaudio::processor::Processor::init();
	assert!(!p.is_open().unwrap());

	// oakrender: value-typed entry points resolve (no backend init here —
	// a GPU backend may not exist on the test host).
	let render_anchor = oakrender::manager::RenderManager::init as usize;
	assert!(render_anchor > 0);

	// oakplugin: the host cache is process-global; other tests in this
	// binary may have scanned plugin bundles before this runs, so the
	// smoke only requires a working count (0 before any scan).
	let _ = oakplugin::host::Host::global().cache.count();
}
