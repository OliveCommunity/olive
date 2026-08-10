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

//! oakcodec C ABI imports (footage probing). dlsym-resolved (see
//! [`super`]).

use std::ffi::c_char;
use std::ffi::c_int;

use crate::handle::CHandle;

/// `oakcodec_decoder_probe` — fills stream info for a media file.
pub fn decoder_probe(path: &str, out: *mut CHandle) -> Option<c_int> {
	use crate::bridge::dlsym;
	use std::ffi::CString;
	type F = unsafe extern "C" fn(*const c_char, *mut CHandle) -> c_int;
	let c = CString::new(path).ok()?;
	dlsym::call::<F, c_int>("oakcodec_decoder_probe", |f| unsafe {
		f(c.as_ptr(), out)
	})
}
