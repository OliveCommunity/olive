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

//! oakcodec C ABI bridge: direct Rust calls into the `oakcodec` crate
//! (M12 P0: the footage decode path).
//!
//! Single-lib unification (see `docs/zh/plans/riir/single-lib.md`): every
//! call below is a compile-time Rust call into `oakcodec`'s `ffi` — the
//! `#[no_mangle]` exports stay in the dylib for the external C ABI;
//! internal callers bypass them. Handles cross as the shared
//! [`crate::handle::CHandle`] (`oakcore_rs::handle::CHandle`). There is no
//! runtime ABI probe: the crate is a path dependency, so the functions are
//! always present; failure semantics are oakcodec's own (empty handle /
//! negative error code).

use std::ffi::{c_char, c_int};

use crate::error::{Error, Result};
use crate::handle::CHandle;

/// `oakcodec_video_stream_info` — POD probe output (one video stream).
///
/// Layout-identical to `oakcodec::decoder::OakCodecVideoStreamInfo` (both
/// `#[repr(C)]`); kept as a local mirror so the bridge API is self-contained.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct VideoStreamInfo {
	/// Stream index.
	pub stream_index: c_int,
	/// Width in pixels.
	pub width: c_int,
	/// Height in pixels.
	pub height: c_int,
	/// Frame-rate numerator.
	pub frame_rate_num: c_int,
	/// Frame-rate denominator.
	pub frame_rate_den: c_int,
	/// Stream length in time-base units.
	pub duration_ts: i64,
	/// Time-base numerator (seconds per time-base unit).
	pub time_base_num: c_int,
	/// Time-base denominator.
	pub time_base_den: c_int,
	/// Native delivery `OakPixelFormat`.
	pub format: c_int,
	/// Plane channel count.
	pub channel_count: c_int,
	/// ISO/IEC 23001-8 color-primaries code point (0 = unknown).
	pub color_primaries: c_int,
	/// ISO/IEC 23001-8 color-transfer code point (0 = unknown).
	pub color_trc: c_int,
	/// 1 when the stream is interlaced.
	pub interlaced: c_int,
}

/// Decoder handle (owned session).
pub type DecoderHandle = CHandle;
/// Probe handle (owned).
pub type ProbeHandle = CHandle;
/// Decoded frame handle (owned).
pub type CodecFrameHandle = CHandle;

/// `oakcodec_decoder_probe(filename)` — owned probe handle; null on
/// failure.
pub fn decoder_probe(filename: &str) -> ProbeHandle {
	let c = std::ffi::CString::new(filename).unwrap_or_default();
	// Direct call into the `oakcodec` crate (single-lib unification; the
	// `#[no_mangle]` export stays for the external C ABI).
	unsafe { oakcodec::ffi::decoder::oakcodec_decoder_probe(c.as_ptr()) }
}

/// `oakcodec_decoder_probe_video_stream_count(probe)`; 0 on an empty
/// probe.
pub fn probe_video_stream_count(probe: ProbeHandle) -> c_int {
	// Direct call into the `oakcodec` crate (single-lib unification; the
	// `#[no_mangle]` export stays for the external C ABI).
	unsafe { oakcodec::ffi::decoder::oakcodec_decoder_probe_video_stream_count(probe) }
}

/// `oakcodec_decoder_probe_get_video_stream(probe, index, out)`.
pub fn probe_get_video_stream(probe: ProbeHandle, index: c_int, out: &mut VideoStreamInfo) -> Result<()> {
	// Direct call into the `oakcodec` crate (single-lib unification; the
	// `#[no_mangle]` export stays for the external C ABI). `VideoStreamInfo`
	// is layout-identical to oakcodec's `OakCodecVideoStreamInfo`, so the
	// pointer crosses via a cast.
	let rc = unsafe {
		oakcodec::ffi::decoder::oakcodec_decoder_probe_get_video_stream(
			probe,
			index,
			out as *mut VideoStreamInfo as *mut oakcodec::decoder::OakCodecVideoStreamInfo,
		)
	};
	if rc == 0 {
		Ok(())
	} else {
		Err(Error::Failed(format!("probe_get_video_stream rc={rc}")))
	}
}

/// `oakcodec_decoder_init()` — owned decoder session; null on failure.
pub fn decoder_init() -> DecoderHandle {
	// Direct call into the `oakcodec` crate (single-lib unification; the
	// `#[no_mangle]` export stays for the external C ABI).
	unsafe { oakcodec::ffi::decoder::oakcodec_decoder_init() }
}

/// `oakcodec_decoder_free(decoder: *mut CHandle)` — NULL no-op.
pub fn decoder_free(decoder: &mut DecoderHandle) {
	// Direct call into the `oakcodec` crate (single-lib unification; the
	// `#[no_mangle]` export stays for the external C ABI).
	unsafe { oakcodec::ffi::decoder::oakcodec_decoder_free(decoder) }
}

/// `oakcodec_decoder_open(decoder, filename, stream_index)`.
pub fn decoder_open(decoder: DecoderHandle, filename: &str, stream_index: c_int) -> Result<()> {
	let c = std::ffi::CString::new(filename).unwrap_or_default();
	// Direct call into the `oakcodec` crate (single-lib unification; the
	// `#[no_mangle]` export stays for the external C ABI).
	let rc = unsafe { oakcodec::ffi::decoder::oakcodec_decoder_open(decoder, c.as_ptr(), stream_index) };
	if rc == 0 {
		Ok(())
	} else {
		Err(Error::Failed(format!("decoder_open rc={rc}")))
	}
}

/// `oakcodec_decoder_close(decoder)`.
pub fn decoder_close(decoder: DecoderHandle) -> Result<()> {
	// Direct call into the `oakcodec` crate (single-lib unification; the
	// `#[no_mangle]` export stays for the external C ABI).
	let rc = unsafe { oakcodec::ffi::decoder::oakcodec_decoder_close(decoder) };
	if rc == 0 {
		Ok(())
	} else {
		Err(Error::Failed(format!("decoder_close rc={rc}")))
	}
}

/// `oakcodec_decoder_decode_video(decoder, num, den)` — owned frame
/// handle; null when not decodable at `time`.
pub fn decoder_decode_video(decoder: DecoderHandle, num: i64, den: i64) -> CodecFrameHandle {
	// Direct call into the `oakcodec` crate (single-lib unification; the
	// `#[no_mangle]` export stays for the external C ABI).
	unsafe { oakcodec::ffi::decoder::oakcodec_decoder_decode_video(decoder, num as c_int, den as c_int) }
}

/// `oakcodec_decoder_decode_audio` — decode interleaved f32 covering
/// `[in, out)` into `buf` (at least `buf_frames` frames, interleaved by
/// `channel_layout`). Returns the number of frames written or a negative
/// error.
pub fn decoder_decode_audio(
	decoder: DecoderHandle,
	in_num: i64,
	in_den: i64,
	out_num: i64,
	out_den: i64,
	sample_rate: c_int,
	channel_layout: u64,
	buf: *mut f32,
	buf_frames: c_int,
) -> Result<c_int> {
	// Direct call into the `oakcodec` crate (single-lib unification; the
	// `#[no_mangle]` export stays for the external C ABI).
	let rc = unsafe {
		oakcodec::ffi::decoder::oakcodec_decoder_decode_audio(
			decoder,
			in_num as c_int,
			in_den as c_int,
			out_num as c_int,
			out_den as c_int,
			sample_rate,
			channel_layout,
			buf,
			buf_frames,
		)
	};
	if rc < 0 {
		return Err(Error::Failed(format!("decode_audio rc={rc}")));
	}
	Ok(rc)
}

/// `oakcodec_decoder_last_error(decoder, buf, size)` — error detail.
pub fn decoder_last_error(decoder: DecoderHandle) -> String {
	let mut buf = [0 as c_char; 512];
	// Direct call into the `oakcodec` crate (single-lib unification; the
	// `#[no_mangle]` export stays for the external C ABI).
	let n = unsafe {
		oakcodec::ffi::decoder::oakcodec_decoder_last_error(decoder, buf.as_mut_ptr(), buf.len() as c_int)
	};
	if n <= 0 {
		return String::new();
	}
	let len = (n as usize).min(buf.len());
	let bytes: Vec<u8> = buf[..len]
		.iter()
		.take_while(|&&b| b != 0)
		.map(|&b| b as u8)
		.collect();
	String::from_utf8_lossy(&bytes).into_owned()
}

// ---------------------------------------------------------------------------
// Decoded frame accessors (`oakcodec_frame_*`)
// ---------------------------------------------------------------------------

/// `oakcodec_frame_width(frame)`; 0 on an empty frame.
pub fn frame_width(frame: CodecFrameHandle) -> c_int {
	// Direct call into the `oakcodec` crate (single-lib unification; the
	// `#[no_mangle]` export stays for the external C ABI).
	unsafe { oakcodec::ffi::frame::oakcodec_frame_width(frame) }
}

/// `oakcodec_frame_height(frame)`; 0 on an empty frame.
pub fn frame_height(frame: CodecFrameHandle) -> c_int {
	// Direct call into the `oakcodec` crate (single-lib unification; the
	// `#[no_mangle]` export stays for the external C ABI).
	unsafe { oakcodec::ffi::frame::oakcodec_frame_height(frame) }
}

/// `oakcodec_frame_format(frame)`; `OAKCOMMON_PIXEL_FORMAT_INVALID` on an
/// empty frame.
pub fn frame_format(frame: CodecFrameHandle) -> c_int {
	// Direct call into the `oakcodec` crate (single-lib unification; the
	// `#[no_mangle]` export stays for the external C ABI).
	unsafe { oakcodec::ffi::frame::oakcodec_frame_format(frame) }
}

/// `oakcodec_frame_linesize_bytes(frame)`; 0 on an empty frame.
pub fn frame_linesize_bytes(frame: CodecFrameHandle) -> c_int {
	// Direct call into the `oakcodec` crate (single-lib unification; the
	// `#[no_mangle]` export stays for the external C ABI).
	unsafe { oakcodec::ffi::frame::oakcodec_frame_linesize_bytes(frame) }
}

/// `oakcodec_frame_is_allocated(frame)`; 0 on an empty frame.
pub fn frame_is_allocated(frame: CodecFrameHandle) -> c_int {
	// Direct call into the `oakcodec` crate (single-lib unification; the
	// `#[no_mangle]` export stays for the external C ABI).
	unsafe { oakcodec::ffi::frame::oakcodec_frame_is_allocated(frame) }
}

/// `oakcodec_frame_const_data(frame)` — read-only pixel buffer pointer.
///
/// # Safety
/// The returned pointer is valid while `frame` is alive and allocated.
pub unsafe fn frame_const_data(frame: CodecFrameHandle) -> *const u8 {
	// Direct call into the `oakcodec` crate (single-lib unification; the
	// `#[no_mangle]` export stays for the external C ABI).
	unsafe { oakcodec::ffi::frame::oakcodec_frame_const_data(frame) as *const u8 }
}

/// `oakcodec_frame_get_timestamp(frame, num, den)`.
pub fn frame_timestamp(frame: CodecFrameHandle) -> Option<(i64, i64)> {
	let mut num: c_int = 0;
	let mut den: c_int = 0;
	// Direct call into the `oakcodec` crate (single-lib unification; the
	// `#[no_mangle]` export stays for the external C ABI).
	let rc = unsafe { oakcodec::ffi::frame::oakcodec_frame_get_timestamp(frame, &mut num, &mut den) };
	if rc == 0 && den != 0 {
		Some((num as i64, den as i64))
	} else {
		None
	}
}

/// `oakcodec_frame_free(frame: *mut CHandle)` — NULL no-op.
pub fn frame_free(frame: &mut CodecFrameHandle) {
	// Direct call into the `oakcodec` crate (single-lib unification; the
	// `#[no_mangle]` export stays for the external C ABI).
	unsafe { oakcodec::ffi::frame::oakcodec_frame_free(frame) }
}

#[cfg(test)]
mod tests {
	use super::*;

	#[test]
	fn bad_inputs_fail_explainably() {
		// Wrapper contract with bad input: every call returns the
		// documented fallback rather than panicking (the real oakcodec
		// guards each export against empty handles / missing files).
		assert!(decoder_probe("/nonexistent_oak_test.mp4").is_null());

		// decoder_init always succeeds (a closed session handle) and must
		// be released.
		let mut d = decoder_init();
		assert!(!d.is_null());
		assert!(decoder_open(d, "f", 0).is_err());
		// Closing a session that is not open is a successful no-op.
		assert!(decoder_close(d).is_ok());
		assert!(decoder_decode_video(d, 0, 1).is_null());
		decoder_free(&mut d);
		assert!(d.is_null());

		// Frame accessors on an empty handle hit the documented fallbacks.
		let empty = CHandle::null();
		assert_eq!(frame_width(empty), 0);
		assert_eq!(frame_height(empty), 0);
		assert_eq!(frame_linesize_bytes(empty), 0);
		assert_eq!(frame_format(empty), -1);
		assert!(frame_timestamp(empty).is_none());
	}
}
