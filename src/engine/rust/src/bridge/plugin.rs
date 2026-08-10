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

extern "C" {
	/// `oakplugin_host_scan` — scan bundle directories (NULL/0 uses the
	/// default path set).
	pub fn oakplugin_host_scan(bundle_dirs: *const *const c_char, dir_count: c_int) -> c_int;
	/// `oakplugin_host_init` — initialize the OFX host (idempotent).
	pub fn oakplugin_host_init() -> c_int;
	/// `oakplugin_host_plugin_count` — number of discovered plugins.
	pub fn oakplugin_host_plugin_count() -> c_int;
	/// `oakplugin_host_plugin_id_at` — plugin id at index (two-stage).
	pub fn oakplugin_host_plugin_id_at(index: c_int, buf: *mut c_char, buf_size: c_int) -> c_int;
	/// `oakplugin_host_plugin_label` — label for an id (two-stage).
	pub fn oakplugin_host_plugin_label(plugin_id: *const c_char, buf: *mut c_char, buf_size: c_int) -> c_int;
}
