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

//! oakplugin C ABI bridge: direct Rust calls into the `oakplugin` crate.
//!
//! Single-lib unification (see `docs/zh/plans/riir/single-lib.md`): every
//! call below is a compile-time Rust call into `oakplugin`'s `ffi` (the
//! `#[no_mangle]` exports stay in the dylib for the external C ABI;
//! internal callers bypass them). Handles cross as the shared
//! [`crate::handle::CHandle`]. Exceptions that keep an `extern "C"`
//! declaration (resolved at link time against the sibling crate in the
//! same dylib) are the host `oakcore_*` symbols and the encoding-params
//! C ABI POD crossings (the facade keeps its own POD mirrors there).

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

//! oakplugin C ABI imports, mirroring the oakplugin crate's exports
//! (`src/plugin/rust/src/ffi.rs`; headers `include/plugin/*.h`).

use std::ffi::{c_char, c_int};

/// Direct call into the `oakplugin` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakplugin_host_scan(bundle_dirs: *const *const c_char, dir_count: c_int) -> c_int {
	unsafe { oakplugin::ffi::oakplugin_host_scan(bundle_dirs, dir_count) }
}

/// Direct call into the `oakplugin` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakplugin_host_init() -> c_int {
	unsafe { oakplugin::ffi::oakplugin_host_init() }
}

/// Direct call into the `oakplugin` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakplugin_host_plugin_count() -> c_int {
	unsafe { oakplugin::ffi::oakplugin_host_plugin_count() }
}

/// Direct call into the `oakplugin` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakplugin_host_plugin_id_at(index: c_int, buf: *mut c_char, buf_size: c_int) -> c_int {
	unsafe { oakplugin::ffi::oakplugin_host_plugin_id_at(index, buf, buf_size) }
}

/// Direct call into the `oakplugin` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakplugin_host_plugin_label(
	plugin_id: *const c_char,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	unsafe { oakplugin::ffi::oakplugin_host_plugin_label(plugin_id, buf, buf_size) }
}
