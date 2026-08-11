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

//! oakcommon C ABI calls (config access + ffmpeg format conversion) —
//! now direct Rust calls into the oakcommon crate (single-lib
//! unification, see `docs/zh/plans/riir/single-lib.md`).

use std::ffi::{c_char, c_int};

/// `oakcommon_config_get` — copy a config string value into `buf`
/// (two-stage; returns the required size including NUL).
pub unsafe fn oakcommon_config_get(
	group: *const c_char,
	key: *const c_char,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	unsafe { oakcommon::ffi::config::oakcommon_config_get(group, key, buf, buf_size) }
}

/// `oakcommon_config_get_int` — read an integer config value with a
/// default.
pub unsafe fn oakcommon_config_get_int(
	group: *const c_char,
	key: *const c_char,
	default: c_int,
) -> c_int {
	unsafe { oakcommon::ffi::config::oakcommon_config_get_int(group, key, default) }
}

/// `oakcommon_ffmpegutils_get_ffmpeg_sample_format` — map an ffmpeg
/// sample format enum to the oak core format, or the reverse.
pub unsafe fn oakcommon_ffmpegutils_get_ffmpeg_sample_format(
	smp_fmt: c_int,
	out: *mut c_int,
) -> c_int {
	unsafe {
		oakcommon::ffi::ffmpegutils::oakcommon_ffmpegutils_get_ffmpeg_sample_format(smp_fmt, out)
	}
}
