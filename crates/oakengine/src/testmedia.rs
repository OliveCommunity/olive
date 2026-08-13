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

//! Test-media generation facade export (M12 P0).
//!
//! `oakengine_testmedia_write_clip` encodes a small MPEG-2 clip of known
//! content through the engine's own FFmpeg (oakcodec's encoder). App
//! tests call this instead of linking oakcodec directly: the app test
//! binary already loads `liboakengine` (which statically embeds FFmpeg),
//! and a second FFmpeg copy in the test binary duplicates the
//! AVFoundation Objective-C classes and crashes the encoder's device
//! probing.

use std::ffi::{c_char, c_int};

use crate::error::Error;
use crate::handle::guard_int;

/// `oakengine_testmedia_write_clip` — encode `frame_count` frames of the
/// known test pattern (left half red / right half blue on frame 0) into
/// `path` (MPEG-2 in an MP4 container, `fps` frames per second).
///
/// Returns 0 on success; a negative `OAKENGINE_E_*` otherwise (invalid
/// arguments or an encoder failure). Only used by tests and tooling.
#[no_mangle]
pub extern "C" fn oakengine_testmedia_write_clip(
	path: *const c_char,
	width: c_int,
	height: c_int,
	frame_count: c_int,
	fps: c_int,
) -> c_int {
	guard_int(|| unsafe {
		if path.is_null() {
			return Err(Error::Invalid);
		}
		let path_str = crate::handle::read_cstr(path);
		if path_str.is_empty() || width <= 0 || height <= 0 || frame_count <= 0 || fps <= 0 {
			return Err(Error::Invalid);
		}
		oakcodec::testmedia::write_test_clip(
			std::path::Path::new(&path_str),
			width,
			height,
			frame_count,
			fps,
		)
		.map_err(|e| Error::Failed(format!("test clip encode failed: {e:?}")))?;
		Ok(0)
	})
}
