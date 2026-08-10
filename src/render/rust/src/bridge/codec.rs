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

//! oakcodec C ABI imports (frame payloads for texture upload, disk-cache
//! frame read/write).
//!
//! Written against the frozen `include/codec/*.h` contract and resolved
//! through [`crate::bridge::dlsym`]. The oakcodec crate is being finished
//! concurrently; until it lands, the missing symbols fail explainably and
//! the codec-dependent tests are `#[ignore = "needs oakcodec final"]`.

use crate::error::{Error, Result};
use crate::frame::VideoParamsPod;
use crate::handle::CHandle;
use crate::texture::Frame;

/// An `OakFrame` handle (include/codec/frame.h).
pub type CodecFrameHandle = CHandle;

/// Whether the oakcodec C ABI is present in the process.
pub fn codec_abi_available() -> bool {
	crate::bridge::dlsym::resolve("oakcodec_frame_free").is_some()
}

/// Release a codec frame handle (`oakcodec_frame_free`).
///
/// # Safety
/// `frame` must be a handle obtained from the codec module.
pub unsafe fn frame_free(frame: *mut CodecFrameHandle) {
	type F = unsafe extern "C" fn(*mut CodecFrameHandle);
	let _ = crate::bridge::dlsym::call::<F, ()>("oakcodec_frame_free", |f| unsafe { f(frame) });
}

/// Marshal an `OakFrame` handle into a CPU [`Frame`]
/// (`oakcodec_frame_get_params` / `_data` / `_linesize_bytes` /
/// `_get_timestamp`).
///
/// Fails with `Error::Failed` when the codec C ABI is absent, or with
/// `Error::Invalid` for a null frame handle.
///
/// # Safety
/// `frame` must be a valid `OakFrame` handle.
pub unsafe fn frame_to_cpu(frame: CodecFrameHandle) -> Result<Frame> {
	if frame.is_null() {
		return Err(Error::Invalid);
	}
	if !codec_abi_available() {
		return Err(Error::Failed(
			"oakcodec C ABI not present (oakcodec crate pending)".into(),
		));
	}
	let mut pod = oakcommon_video_params_zeroed();
	type GetParamsF = unsafe extern "C" fn(CodecFrameHandle, *mut OakVideoParamsPod) -> i32;
	let rc = crate::bridge::dlsym::call::<GetParamsF, i32>("oakcodec_frame_get_params", |f| unsafe {
		f(frame, &mut pod)
	})
	.ok_or_else(|| Error::Failed("oakcodec_frame_get_params missing".into()))?;
	if rc != 0 {
		return Err(Error::Failed(format!("oakcodec_frame_get_params rc={rc}")));
	}

	type WidthF = unsafe extern "C" fn(CodecFrameHandle) -> i32;
	let width = crate::bridge::dlsym::call::<WidthF, i32>("oakcodec_frame_width", |f| unsafe {
		f(frame)
	})
	.unwrap_or(0);
	let height = crate::bridge::dlsym::call::<WidthF, i32>("oakcodec_frame_height", |f| unsafe {
		f(frame)
	})
	.unwrap_or(0);
	let format = crate::bridge::dlsym::call::<WidthF, i32>("oakcodec_frame_format", |f| unsafe {
		f(frame)
	})
	.unwrap_or(-1);
	let channels =
		crate::bridge::dlsym::call::<WidthF, i32>("oakcodec_frame_channel_count", |f| unsafe {
			f(frame)
		})
		.unwrap_or(0);
	let linesize =
		crate::bridge::dlsym::call::<WidthF, i32>("oakcodec_frame_linesize_bytes", |f| unsafe {
			f(frame)
		})
		.unwrap_or(0);
	let data = crate::bridge::dlsym::call::<DataF, *mut u8>("oakcodec_frame_data", |f| unsafe {
		f(frame)
	})
	.unwrap_or(std::ptr::null_mut());
	if data.is_null() || linesize <= 0 || width <= 0 || height <= 0 {
		return Err(Error::Failed("frame not allocated".into()));
	}

	type TsF = unsafe extern "C" fn(CodecFrameHandle, *mut i32, *mut i32) -> i32;
	let (mut tn, mut td) = (0i32, 1i32);
	let _ = crate::bridge::dlsym::call::<TsF, i32>("oakcodec_frame_get_timestamp", |f| unsafe {
		f(frame, &mut tn, &mut td)
	});

	let mut cpu = Frame::new();
	let pod = VideoParamsPod {
		width,
		height,
		time_base_num: pod.time_base_num,
		time_base_den: pod.time_base_den,
		format,
		pixel_aspect_num: pod.pixel_aspect_num,
		pixel_aspect_den: pod.pixel_aspect_den,
		interlacing: pod.interlacing,
		color_range: pod.color_range,
		divider: pod.divider,
		video_type: pod.video_type,
		premultiplied_alpha: pod.premultiplied_alpha,
	};
	cpu.set_video_params(pod);
	cpu.channels = channels;
	cpu.timestamp = oakcore_rs::Rational::new(tn as i64, td as i64);
	let size = (linesize as usize)
		.saturating_mul(height as usize)
		.min(1usize << 31);
	cpu.data = unsafe { std::slice::from_raw_parts(data, size).to_vec() };
	Ok(cpu)
}

type DataF = unsafe extern "C" fn(CodecFrameHandle) -> *mut u8;

/// Mirror of the `OakVideoParams` POD (include/common/videoparams.h) used
/// by `oakcodec_frame_get_params`.
#[repr(C)]
#[derive(Clone, Copy)]
struct OakVideoParamsPod {
	width: i32,
	height: i32,
	time_base_num: i32,
	time_base_den: i32,
	format: i32,
	pixel_aspect_num: i32,
	pixel_aspect_den: i32,
	interlacing: i32,
	color_range: i32,
	divider: i32,
	video_type: i32,
	premultiplied_alpha: i32,
}

fn oakcommon_video_params_zeroed() -> OakVideoParamsPod {
	// Safe: POD of ints.
	unsafe { std::mem::zeroed() }
}

/// Frame-payload write for the disk frame cache. Fails with
/// `Error::Failed` when the codec C ABI is absent.
///
/// # Safety
/// `frame` must be a valid `OakFrame` handle with an allocated buffer.
pub unsafe fn frame_write_disk(frame: CodecFrameHandle, path: &str) -> Result<()> {
	if !codec_abi_available() {
		return Err(Error::Failed(
			"oakcodec C ABI not present (oakcodec crate pending)".into(),
		));
	}
	// The codec crate owns the EXR/JPEG disk format. The symbol name is
	// part of the codec contract; wrapped defensively.
	#[repr(C)]
	struct FsArg {
		codec_frame: CodecFrameHandle,
		path: *const std::ffi::c_char,
	}
	let path_c = std::ffi::CString::new(path)
		.map_err(|_| Error::Invalid)?;
	let arg = FsArg {
		codec_frame: frame,
		path: path_c.as_ptr(),
	};
	type F = unsafe extern "C" fn(*const FsArg) -> i32;
	let rc = crate::bridge::dlsym::call::<F, i32>("oakcodec_frame_write_file", |f| unsafe {
		f(&arg)
	})
	.ok_or_else(|| Error::Failed("oakcodec_frame_write_file missing".into()))?;
	if rc == 0 {
		Ok(())
	} else {
		Err(Error::Failed(format!("oakcodec_frame_write_file rc={rc}")))
	}
}

#[cfg(test)]
mod tests {
	use super::*;

	#[test]
	fn missing_codec_abi_fails_explainably() {
		let rc = unsafe { frame_to_cpu(CHandle::null()) };
		assert!(rc.is_err(), "null frame rejected");
		let rc = unsafe { frame_to_cpu(CHandle {
			ctx: 1 as *mut std::ffi::c_void,
			addref: None,
			release: None,
			abi_version: 1,
		}) };
		if !codec_abi_available() {
			assert!(rc.is_err());
		}
	}
}
