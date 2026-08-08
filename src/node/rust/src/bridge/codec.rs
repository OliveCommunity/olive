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

//! oakcodec C ABI imports (footage probing).

use std::ffi::{c_char, c_int};

use crate::handle::CHandle;

extern "C" {
	/// `oakcodec_decoder_probe` — fills stream info for a media file.
	pub fn oakcodec_decoder_probe(path: *const c_char, out: *mut CHandle) -> c_int;
}
