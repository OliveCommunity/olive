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

//! Hardware video decoding (user-mandated default): the platform's
//! hardware acceleration is preferred over pure software decoding, with
//! a config switch and an automatic software fallback.
//!
//! **FFmpeg 8 removed the standalone hardware decoders** (`h264_videotoolbox`,
//! `h264_vaapi`, `h264_nvdec`, `h264_d3d11va` are all gone from its
//! configure): hardware decode now only exists as a *hwaccel* attached
//! to the software decoder. The model here is therefore uniform across
//! platforms: create the platform's hardware device context
//! (`av_hwdevice_ctx_create`), set it as `hw_device_ctx` on the codec
//! context of the regular decoder, and FFmpeg automatically engages the
//! matching hwaccel (`h264_videotoolbox_hwaccel` & co) on open. Codecs
//! without a matching hwaccel silently stay software — the pipeline
//! below only transfers frames whose pixel format is actually a
//! hardware surface.
//!
//! - **macOS**: `AV_HWDEVICE_TYPE_VIDEOTOOLBOX`
//! - **Linux**: `VAAPI`, then `CUDA` (NVDEC)
//! - **Windows**: `D3D11VA`, then `CUDA` (NVDEC)
//!
//! Device creation can fail on machines without the device/driver (a
//! headless Linux box, no NVIDIA GPU) — the candidate is skipped and
//! the next one (or the software decoder) is used. Hardware frames
//! (`AV_PIX_FMT_VIDEOTOOLBOX` / `VAAPI` / `CUDA` / `D3D11VA_VLD` /
//! `D3D11`) are transferred to system memory with
//! `av_hwframe_transfer_data` before the swscale conversion.

use ffmpeg::ffi as sys;
use ffmpeg::Dictionary;
use ffmpeg_next as ffmpeg;

use crate::error::{Error, Result};

/// Number of hardware frames transferred to system memory so far
/// (process-wide). An observability counter: a hardware decode that
/// never produces a hardware surface stays at zero, so tests can prove
/// the hwaccel really engaged rather than silently staying software.
pub static HW_TRANSFERS: std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(0);

/// The config key of the hardware-decode switch (1 = prefer hardware,
/// 0 = force software). Default ON by user mandate.
pub const CONFIG_KEY_HARDWARE_DECODING: &str = "HardwareDecoding";

/// Whether hardware decoding is preferred (the config switch). Default
/// ON by user mandate; only an explicit `"false"` turns it off (the
/// string accessor, same convention as the app's config helpers — the
/// store's typed `get_bool` only parses pre-typed Bool entries).
pub fn hardware_decoding_enabled() -> bool {
	match oak_common::configstore::ConfigStore::instance()
		.get(None, CONFIG_KEY_HARDWARE_DECODING)
	{
		Ok(value) => value != "false",
		Err(_) => true,
	}
}

/// The hardware device types to try, most preferred first. On a machine
/// without the device/driver the candidate fails creation and the next
/// one is tried; the software decoder is the final fallback.
pub fn device_type_candidates() -> &'static [sys::AVHWDeviceType] {
	#[cfg(target_os = "macos")]
	{
		&[sys::AVHWDeviceType::AV_HWDEVICE_TYPE_VIDEOTOOLBOX]
	}
	#[cfg(target_os = "windows")]
	{
		&[
			sys::AVHWDeviceType::AV_HWDEVICE_TYPE_D3D11VA,
			sys::AVHWDeviceType::AV_HWDEVICE_TYPE_CUDA,
		]
	}
	#[cfg(all(unix, not(target_os = "macos")))]
	{
		&[
			sys::AVHWDeviceType::AV_HWDEVICE_TYPE_VAAPI,
			sys::AVHWDeviceType::AV_HWDEVICE_TYPE_CUDA,
		]
	}
}

/// A display name for a device type (status reporting and tests).
pub fn device_type_name(device_type: sys::AVHWDeviceType) -> &'static str {
	match device_type {
		sys::AVHWDeviceType::AV_HWDEVICE_TYPE_VIDEOTOOLBOX => "videotoolbox",
		sys::AVHWDeviceType::AV_HWDEVICE_TYPE_VAAPI => "vaapi",
		sys::AVHWDeviceType::AV_HWDEVICE_TYPE_CUDA => "cuda/nvdec",
		sys::AVHWDeviceType::AV_HWDEVICE_TYPE_D3D11VA => "d3d11va",
		_ => "unknown",
	}
}

/// Try to open the software codec with a hardware device context of
/// `device_type` attached — FFmpeg then engages the matching hwaccel
/// (e.g. `h264_videotoolbox_hwaccel`) on open. Returns the opened codec
/// context plus the device type in use, or `None` when the device is
/// unavailable (the caller tries the next candidate, then pure
/// software).
pub fn open_hw_accel(
	params: &ffmpeg::codec::Parameters,
	codec: ffmpeg::Codec,
	device_type: sys::AVHWDeviceType,
) -> Option<(ffmpeg::codec::decoder::Opened, sys::AVHWDeviceType)> {
	let mut context = ffmpeg::codec::Context::from_parameters(params.clone()).ok()?;
	let mut device: *mut sys::AVBufferRef = std::ptr::null_mut();
	// SAFETY: `device` is a valid out-pointer; on success it owns the
	// device reference, which is handed to the codec context below.
	let rc = unsafe {
		sys::av_hwdevice_ctx_create(
			&mut device,
			device_type,
			std::ptr::null(),
			std::ptr::null_mut(),
			0,
		)
	};
	if rc < 0 || device.is_null() {
		return None;
	}
	// SAFETY: `hw_device_ctx` takes ownership of the reference; the codec
	// context frees it with the context.
	unsafe { (*context.as_mut_ptr()).hw_device_ctx = device };
	let mut opts = Dictionary::new();
	opts.set("threads", "auto");
	context
		.decoder()
		.open_as_with(codec, opts)
		.ok()
		.map(|opened| (opened, device_type))
}

/// Whether a decoded frame's pixel format is a hardware surface that
/// must be transferred to system memory before swscale can consume it.
pub fn is_hw_format(format: sys::AVPixelFormat) -> bool {
	matches!(
		format,
		sys::AVPixelFormat::AV_PIX_FMT_VIDEOTOOLBOX
			| sys::AVPixelFormat::AV_PIX_FMT_VAAPI
			| sys::AVPixelFormat::AV_PIX_FMT_CUDA
			| sys::AVPixelFormat::AV_PIX_FMT_D3D11VA_VLD
			| sys::AVPixelFormat::AV_PIX_FMT_D3D11
	)
}

/// Transfer a hardware frame to system memory (`av_hwframe_transfer_data`
/// picks the software format: NV12 for 8-bit, P010LE for 10-bit sources;
/// swscale consumes both). Presentation metadata the frame cache relies
/// on is carried over.
pub fn transfer_to_cpu(frame: &ffmpeg::frame::Video) -> Result<ffmpeg::frame::Video> {
	let mut cpu = ffmpeg::frame::Video::empty();
	// SAFETY: `cpu` and `frame` are valid AVFrames; flags 0 = default
	// transfer direction (hardware -> system memory).
	let rc = unsafe { sys::av_hwframe_transfer_data(cpu.as_mut_ptr(), frame.as_ptr(), 0) };
	if rc < 0 {
		return Err(Error::Failed(format!(
			"hwframe transfer to CPU failed (av error {rc})"
		)));
	}
	HW_TRANSFERS.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
	cpu.set_pts(frame.pts());
	// SAFETY: plain field copies between valid AVFrames.
	unsafe {
		(*cpu.as_mut_ptr()).pkt_dts = (*frame.as_ptr()).pkt_dts;
		(*cpu.as_mut_ptr()).duration = (*frame.as_ptr()).duration;
		(*cpu.as_mut_ptr()).time_base = (*frame.as_ptr()).time_base;
	}
	Ok(cpu)
}

#[cfg(test)]
mod tests {
	use super::*;

	/// The current platform has at least one hardware device candidate
	/// (hardware decode is the mandated default on every supported
	/// platform).
	#[test]
	fn platform_has_a_hardware_candidate() {
		assert!(
			!device_type_candidates().is_empty(),
			"no hardware decode candidate on this platform"
		);
	}

	/// Device types round-trip through their display names.
	#[test]
	fn device_type_names_are_stable() {
		assert_eq!(
			device_type_name(sys::AVHWDeviceType::AV_HWDEVICE_TYPE_VIDEOTOOLBOX),
			"videotoolbox"
		);
		assert_eq!(device_type_name(sys::AVHWDeviceType::AV_HWDEVICE_TYPE_VAAPI), "vaapi");
		assert_eq!(device_type_name(sys::AVHWDeviceType::AV_HWDEVICE_TYPE_CUDA), "cuda/nvdec");
		assert_eq!(
			device_type_name(sys::AVHWDeviceType::AV_HWDEVICE_TYPE_D3D11VA),
			"d3d11va"
		);
	}

	/// Software and hardware pixel formats are classified correctly.
	#[test]
	fn hw_format_classification() {
		assert!(is_hw_format(sys::AVPixelFormat::AV_PIX_FMT_VIDEOTOOLBOX));
		assert!(is_hw_format(sys::AVPixelFormat::AV_PIX_FMT_VAAPI));
		assert!(is_hw_format(sys::AVPixelFormat::AV_PIX_FMT_CUDA));
		assert!(is_hw_format(sys::AVPixelFormat::AV_PIX_FMT_D3D11VA_VLD));
		assert!(!is_hw_format(sys::AVPixelFormat::AV_PIX_FMT_YUV420P));
		assert!(!is_hw_format(sys::AVPixelFormat::AV_PIX_FMT_NV12));
	}

	/// On macOS the VideoToolbox device context must be creatable (the
	/// mandated default decode path) and the H.264 demo must open with
	/// the hwaccel attached.
	#[cfg(target_os = "macos")]
	#[test]
	fn videotoolbox_device_opens_for_h264() {
		let mut input = ffmpeg::format::input(&"../../tests/demo.mp4".to_string())
			.expect("open demo.mp4");
		let fstream = input.stream(0).expect("stream 0");
		let params = fstream.parameters();
		let codec = ffmpeg::decoder::find(params.id()).expect("software h264 codec");
		let opened = open_hw_accel(
			&params,
			codec,
			sys::AVHWDeviceType::AV_HWDEVICE_TYPE_VIDEOTOOLBOX,
		);
		assert!(opened.is_some(), "VideoToolbox hwaccel must open for H.264");
	}
}
