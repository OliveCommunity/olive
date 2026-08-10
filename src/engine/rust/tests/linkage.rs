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

//! Forces rustc to link every module crate's rlib into the test binary.
//!
//! The facade itself only references the modules through `extern "C"`
//! imports (see src/bridge), so rustc would otherwise drop the
//! dev-dependency rlibs from the link and leave the imports undefined.
//! Referencing one exported item per crate makes the linker pull the
//! crate's objects in; the `#[no_mangle]` exports then satisfy the
//! bridge imports.

/// Smoke-test that every module crate links: each referenced export is
/// called once and must return a sane value or a handle.
#[path = "common/mod.rs"]
mod common;


#[test]
fn all_module_crates_link() {
	// oakundo: fresh stack, refcount 1.
	let stack = unsafe { oakundo::ffi::undostack::oakundo_undostack_init() };
	assert!(!stack.ctx.is_null());

	// oakcommon: an int config read with fallback.
	let v = unsafe {
		oakcommon::ffi::config::oakcommon_config_get_int(
			std::ptr::null(),
			c"no-such-key".as_ptr(),
			42,
		)
	};
	assert_eq!(v, 42);

	// oakcodec: format count (must be positive).
	let n = unsafe { oakcodec::ffi::format::oakcodec_encoding_format_count() };
	assert!(n > 0);

	// oakaudio: waveform length of a null handle is an error code, not a
	// crash (module validates the handle).
	let rc = unsafe {
		oakaudio::ffi::waveform::oakaudio_waveform_length(
			oakaudio::handle::CHandle::null(),
			std::ptr::null_mut(),
			std::ptr::null_mut(),
		)
	};
	assert!(rc < 0);

	// oakrender: cache indicator height is a positive constant.
	let h = unsafe { oakrender::ffi::cache::oakrender_cache_indicator_height() };
	assert!(h > 0);

	// oakplugin: host plugin count with no scan is 0.
	let n = unsafe { oakplugin::ffi::oakplugin_host_plugin_count() };
	assert_eq!(n, 0);
}
