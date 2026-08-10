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

//! oakcodec C ABI calls (footage probing) — now direct Rust calls into
//! the oakcodec crate (single-lib unification, see
//! `docs/zh/plans/riir/single-lib.md`).

use std::ffi::c_char;

use crate::handle::CHandle;

/// `oakcodec_decoder_probe` — probe a media file, returning the
/// stream-list handle (`oakcodec_decoder_probe(filename)`). The caller
/// owns the returned handle.
pub fn decoder_probe(path: &str) -> Option<CHandle> {
	use std::ffi::CString;
	let c = CString::new(path).ok()?;
	Some(unsafe { oakcodec::ffi::decoder::oakcodec_decoder_probe(c.as_ptr()) })
}
