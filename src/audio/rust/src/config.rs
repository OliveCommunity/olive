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

//! The `audio_config` namespace from `src/audio/src/configbridge.*`:
//! audio-specific configuration read through the oakcommon C ABI.

use std::ffi::CString;

/// PortAudio output buffer size in frames; 0 = let PortAudio choose.
///
/// `// CPP-PARITY: src/audio/src/configbridge.cpp:30`
/// (`audio_config::output_buffer_size`).
pub fn output_buffer_size() -> i32 {
	unsafe {
		crate::bridge::common::oakcommon_config_get_int(
			std::ptr::null(),
			c"AudioOutputBufferSize".as_ptr(),
			0,
		)
	}
}

/// Name of the configured audio device for `is_output_device`
/// (key "AudioOutput" / "AudioInput"); empty when absent.
///
/// `// CPP-PARITY: src/audio/src/configbridge.cpp:36`
/// (`audio_config::device_name`): two-stage size query; `size <= 1`
/// (absent or empty string) yields the empty string.
pub fn device_name(is_output_device: bool) -> CString {
	let key = if is_output_device {
		c"AudioOutput"
	} else {
		c"AudioInput"
	};
	unsafe {
		let size = crate::bridge::common::oakcommon_config_get(
			std::ptr::null(),
			key.as_ptr(),
			std::ptr::null_mut(),
			0,
		);
		if size <= 1 {
			// Absent (OAKCOMMON_E_NOT_FOUND) or empty
			return CString::default();
		}
		let mut buf = vec![0u8; size as usize];
		if crate::bridge::common::oakcommon_config_get(
			std::ptr::null(),
			key.as_ptr(),
			buf.as_mut_ptr() as *mut std::ffi::c_char,
			size,
		) < 0
		{
			return CString::default();
		}
		// The buffer is NUL-terminated by the callee.
		CString::from_vec_with_nul(buf)
			.unwrap_or_else(|e| CString::new(e.into_bytes()).unwrap_or_default())
	}
}
