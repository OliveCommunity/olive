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

//! oakrender C ABI imports (caches, textures, color processors).

use crate::handle::CHandle;

/// oakrender cache handle (value type).
pub type CacheHandle = CHandle;
/// oakrender texture handle (value type).
pub type TextureHandle = CHandle;
/// oakrender color processor handle (value type).
pub type ColorProcessorHandle = CHandle;

extern "C" {
	/// `oakrender_cache_create_for_node`.
	pub fn oakrender_cache_create_for_node(parent: CHandle, kind: i32) -> CHandle;
	/// `oakrender_cache_free`.
	pub fn oakrender_cache_free(cache: *mut CHandle);
	/// `oakrender_cache_invalidate_range`.
	pub fn oakrender_cache_invalidate_range(
		cache: CHandle,
		in_num: i64,
		in_den: i64,
		out_num: i64,
		out_den: i64,
	);
	/// `oakrender_cache_set_uuid`.
	pub fn oakrender_cache_set_uuid(cache: CHandle, uuid: *const std::ffi::c_char) -> i32;
	/// `oakrender_cache_get_uuid` (two-stage).
	pub fn oakrender_cache_get_uuid(cache: CHandle, buf: *mut std::ffi::c_char, buf_size: i32) -> i32;
}
