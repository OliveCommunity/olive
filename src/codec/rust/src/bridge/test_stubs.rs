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

//! `#[cfg(test)]` in-memory mocks for the oakcommon / oakrender C ABI.
//!
//! The real symbols live in `liboakcommon` / `liboakrender` and are only
//! linked when the crate is built against those dylibs (ctest). Under
//! `cargo test` those libraries are not linked, so `extern "C"` call
//! sites in the crate would otherwise fail to resolve. These
//! `#[no_mangle] extern "C"` definitions provide the symbols and back the
//! two stateful pieces the crate actually reads (video params and cancel
//! atoms) with real in-memory state, so `Frame`, `FootageDescription`
//! and friends are meaningfully testable. Everything else returns a
//! deterministic neutral value.
//!
//! Kept strictly under `#[cfg(test)]`; never compiled into a real build.

#![allow(dead_code, unused_variables)]

use std::collections::HashMap;
use std::ffi::{c_char, c_int, c_void, CStr};
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Mutex, OnceLock};

use crate::bridge::common::{
	OakAudioParams, OakNodeBlock, OakSubtitleParams, OakVideoParams,
};
use crate::bridge::render::{OakCancelAtom, OakCodecFrame, OakRenderRenderer, OakRenderTexture};
use crate::handle::OAKCODEC_ABI_VERSION;

/// Per-`OakVideoParams` backing state.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
struct MockParams {
	width: i32,
	height: i32,
	format: i32,
	time_base_num: i64,
	time_base_den: i64,
	stream_index: i32,
	divider: i32,
	frame_rate_num: i32,
	frame_rate_den: i32,
	duration: i64,
	channel_count: i32,
	color_primaries: i32,
	color_trc: i32,
	interlacing: i32,
	pixel_aspect_num: i32,
	pixel_aspect_den: i32,
	start_time: i64,
	color_range: i32,
	video_type: i32,
	premultiplied_alpha: i32,
	enabled: i32,
}

fn params_store() -> &'static Mutex<HashMap<usize, MockParams>> {
	static S: OnceLock<Mutex<HashMap<usize, MockParams>>> = OnceLock::new();
	S.get_or_init(|| Mutex::new(HashMap::new()))
}

/// Build a handle whose `ctx` owns a leaked `MockParams`; the map entry
/// keeps the storage alive until `free` is called.
fn new_params_handle(p: MockParams) -> OakVideoParams {
	let raw = Box::into_raw(Box::new(p.clone()));
	params_store().lock().unwrap().insert(raw as usize, p);
	OakVideoParams {
		ctx: raw as *mut c_void,
		addref: None,
		release: None,
		abi_version: OAKCODEC_ABI_VERSION,
	}
}

fn params_ref(ctx: *mut c_void) -> Option<&'static mut MockParams> {
	let store = params_store().lock().unwrap();
	store.get(&(ctx as usize))?;
	drop(store);
	// SAFETY: entries are only removed by oakcommon_videoparams_free while
	// the caller still holds the handle, so the box outlives this borrow.
	Some(unsafe { &mut *(ctx as *mut MockParams) })
}

fn params_get(ctx: *mut c_void) -> MockParams {
	let store = params_store().lock().unwrap();
	store
		.get(&(ctx as usize))
		.cloned()
		.unwrap_or_default()
}

fn params_set(ctx: *mut c_void, f: impl FnOnce(&mut MockParams)) {
	let mut store = params_store().lock().unwrap();
	if let Some(p) = store.get_mut(&(ctx as usize)) {
		f(p);
	}
}

// ---------------------------------------------------------------------------
// oakcommon_videoparams_*
// ---------------------------------------------------------------------------

#[no_mangle]
pub extern "C" fn oakcommon_videoparams_init() -> OakVideoParams {
	new_params_handle(MockParams {
		channel_count: 4, // internal RGBA pipeline layout
		..Default::default()
	})
}

#[no_mangle]
pub extern "C" fn oakcommon_videoparams_init_basic(width: c_int, height: c_int) -> OakVideoParams {
	new_params_handle(MockParams {
		width,
		height,
		divider: 1,
		channel_count: 4, // internal RGBA pipeline layout
		..Default::default()
	})
}

#[no_mangle]
pub extern "C" fn oakcommon_videoparams_init_with_time_base(
	width: c_int,
	height: c_int,
	time_base_num: i64,
	time_base_den: i64,
) -> OakVideoParams {
	new_params_handle(MockParams {
		width,
		height,
		time_base_num,
		time_base_den,
		divider: 1,
		channel_count: 4, // internal RGBA pipeline layout
		..Default::default()
	})
}

#[no_mangle]
pub extern "C" fn oakcommon_videoparams_free(params: *mut OakVideoParams) {
	if params.is_null() {
		return;
	}
	unsafe {
		let ctx = (*params).ctx;
		params_store().lock().unwrap().remove(&(ctx as usize));
		if !ctx.is_null() {
			drop(Box::from_raw(ctx as *mut MockParams));
		}
	}
}

#[no_mangle]
pub extern "C" fn oakcommon_videoparams_get_width(params: OakVideoParams) -> c_int {
	params_get(params.ctx).width
}

#[no_mangle]
pub extern "C" fn oakcommon_videoparams_get_height(params: OakVideoParams) -> c_int {
	params_get(params.ctx).height
}

#[no_mangle]
pub extern "C" fn oakcommon_videoparams_get_format(params: OakVideoParams) -> c_int {
	params_get(params.ctx).format
}

#[no_mangle]
pub extern "C" fn oakcommon_videoparams_get_time_base(
	params: OakVideoParams,
	out_num: *mut i64,
	out_den: *mut i64,
) -> c_int {
	let p = params_get(params.ctx);
	if !out_num.is_null() {
		unsafe { *out_num = p.time_base_num };
	}
	if !out_den.is_null() {
		unsafe { *out_den = p.time_base_den };
	}
	1
}

#[no_mangle]
pub extern "C" fn oakcommon_videoparams_set_width(params: OakVideoParams, width: c_int) {
	params_set(params.ctx, |p| p.width = width);
}

#[no_mangle]
pub extern "C" fn oakcommon_videoparams_set_height(params: OakVideoParams, height: c_int) {
	params_set(params.ctx, |p| p.height = height);
}

#[no_mangle]
pub extern "C" fn oakcommon_videoparams_set_format(params: OakVideoParams, format: c_int) {
	params_set(params.ctx, |p| p.format = format);
}

#[no_mangle]
pub extern "C" fn oakcommon_videoparams_get_is_valid(params: OakVideoParams) -> c_int {
	let p = params_get(params.ctx);
	((p.width > 0) && (p.height > 0)) as c_int
}

#[no_mangle]
pub extern "C" fn oakcommon_videoparams_equals(a: OakVideoParams, b: OakVideoParams) -> c_int {
	(params_get(a.ctx) == params_get(b.ctx)) as c_int
}

#[no_mangle]
pub extern "C" fn oakcommon_videoparams_static_get_bytes_per_pixel(format: c_int) -> c_int {
	// U10 packs to 4 bytes; U8 to 1; U16/F16 to 2; F32 to 4.
	match format {
		0 => 1,  // U8
		1 => 4,  // U10
		2 => 2,  // U16
		3 => 2,  // F16
		4 => 4,  // F32
		_ => 0,
	}
}

#[no_mangle]
pub extern "C" fn oakcommon_videoparams_frame_rate_as_time_base(
	frame_rate_num: i64,
	frame_rate_den: i64,
	out_num: *mut i64,
	out_den: *mut i64,
) {
	if !out_num.is_null() {
		unsafe { *out_num = frame_rate_den };
	}
	if !out_den.is_null() {
		unsafe { *out_den = frame_rate_num };
	}
}

#[no_mangle]
pub extern "C" fn oakcommon_videoparams_get_stream_index(params: OakVideoParams) -> c_int {
	params_get(params.ctx).stream_index
}

#[no_mangle]
pub extern "C" fn oakcommon_videoparams_set_stream_index(params: OakVideoParams, index: c_int) {
	params_set(params.ctx, |p| p.stream_index = index);
}

#[no_mangle]
pub extern "C" fn oakcommon_videoparams_get_divider(params: OakVideoParams) -> c_int {
	params_get(params.ctx).divider
}

#[no_mangle]
pub extern "C" fn oakcommon_videoparams_set_divider(params: OakVideoParams, divider: c_int) {
	params_set(params.ctx, |p| p.divider = divider);
}

#[no_mangle]
pub extern "C" fn oakcommon_videoparams_get_frame_rate(
	params: OakVideoParams,
	out_num: *mut c_int,
	out_den: *mut c_int,
) -> c_int {
	let p = params_get(params.ctx);
	if !out_num.is_null() {
		unsafe { *out_num = p.frame_rate_num };
	}
	if !out_den.is_null() {
		unsafe { *out_den = p.frame_rate_den };
	}
	1
}

#[no_mangle]
pub extern "C" fn oakcommon_videoparams_get_duration(params: OakVideoParams) -> i64 {
	params_get(params.ctx).duration
}

#[no_mangle]
pub extern "C" fn oakcommon_videoparams_get_channel_count(params: OakVideoParams) -> c_int {
	params_get(params.ctx).channel_count
}

#[no_mangle]
pub extern "C" fn oakcommon_videoparams_get_color_primaries(params: OakVideoParams) -> c_int {
	params_get(params.ctx).color_primaries
}

#[no_mangle]
pub extern "C" fn oakcommon_videoparams_get_color_transfer(params: OakVideoParams) -> c_int {
	params_get(params.ctx).color_trc
}

#[no_mangle]
pub extern "C" fn oakcommon_videoparams_get_interlacing(params: OakVideoParams) -> c_int {
	params_get(params.ctx).interlacing
}

#[no_mangle]
pub extern "C" fn oakcommon_videoparams_set_time_base(
	params: OakVideoParams,
	num: i64,
	den: i64,
) {
	params_set(params.ctx, |p| {
		p.time_base_num = num;
		p.time_base_den = den;
	});
}

#[no_mangle]
pub extern "C" fn oakcommon_videoparams_set_frame_rate(
	params: OakVideoParams,
	num: i64,
	den: i64,
) {
	params_set(params.ctx, |p| {
		p.frame_rate_num = num as i32;
		p.frame_rate_den = den as i32;
	});
}

#[no_mangle]
pub extern "C" fn oakcommon_videoparams_set_pixel_aspect_ratio(
	params: OakVideoParams,
	num: i64,
	den: i64,
) {
	params_set(params.ctx, |p| {
		p.pixel_aspect_num = num as i32;
		p.pixel_aspect_den = den as i32;
	});
}

#[no_mangle]
pub extern "C" fn oakcommon_videoparams_set_interlacing(params: OakVideoParams, interlacing: c_int) {
	params_set(params.ctx, |p| p.interlacing = interlacing);
}

#[no_mangle]
pub extern "C" fn oakcommon_videoparams_set_duration(params: OakVideoParams, duration: i64) {
	params_set(params.ctx, |p| p.duration = duration);
}

#[no_mangle]
pub extern "C" fn oakcommon_videoparams_set_start_time(params: OakVideoParams, start_time: i64) {
	params_set(params.ctx, |p| p.start_time = start_time);
}

#[no_mangle]
pub extern "C" fn oakcommon_videoparams_set_color_range(params: OakVideoParams, color_range: c_int) {
	params_set(params.ctx, |p| p.color_range = color_range);
}

#[no_mangle]
pub extern "C" fn oakcommon_videoparams_set_video_type(params: OakVideoParams, video_type: c_int) {
	params_set(params.ctx, |p| p.video_type = video_type);
}

#[no_mangle]
pub extern "C" fn oakcommon_videoparams_set_channel_count(params: OakVideoParams, channels: c_int) {
	params_set(params.ctx, |p| p.channel_count = channels);
}

#[no_mangle]
pub extern "C" fn oakcommon_videoparams_set_color_primaries(params: OakVideoParams, primaries: c_int) {
	params_set(params.ctx, |p| p.color_primaries = primaries);
}

#[no_mangle]
pub extern "C" fn oakcommon_videoparams_set_color_transfer(params: OakVideoParams, transfer: c_int) {
	params_set(params.ctx, |p| p.color_trc = transfer);
}

#[no_mangle]
pub extern "C" fn oakcommon_videoparams_set_premultiplied_alpha(
	params: OakVideoParams,
	premultiplied: c_int,
) {
	params_set(params.ctx, |p| p.premultiplied_alpha = premultiplied);
}

#[no_mangle]
pub extern "C" fn oakcommon_videoparams_set_enabled(params: OakVideoParams, enabled: c_int) {
	params_set(params.ctx, |p| p.enabled = enabled);
}

// ---------------------------------------------------------------------------
// oakcore_audioparams_* / oakcore_rational_* (pointer-based in-memory state)
// ---------------------------------------------------------------------------

/// Per-`OakAudioParams` backing state. The real oakcore C ABI is
/// pointer-based (`core/include/olive/core/oakcore/audioparams.h`), so
/// these stubs own a boxed struct and return its raw pointer.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
struct MockAudioParams {
	sample_rate: i32,
	channel_layout: u64,
	format: i32,
	stream_index: i32,
	duration: i64,
	time_base_num: i32,
	time_base_den: i32,
}

fn audio_params_store() -> &'static Mutex<HashMap<usize, MockAudioParams>> {
	static S: OnceLock<Mutex<HashMap<usize, MockAudioParams>>> = OnceLock::new();
	S.get_or_init(|| Mutex::new(HashMap::new()))
}

fn audio_params_get(ctx: *const c_void) -> MockAudioParams {
	let store = audio_params_store().lock().unwrap();
	store
		.get(&(ctx as usize))
		.cloned()
		.unwrap_or_default()
}

/// Per-`OakRational` backing state (an owned `(num, den)` pair).
fn rational_store() -> &'static Mutex<HashMap<usize, (i32, i32)>> {
	static S: OnceLock<Mutex<HashMap<usize, (i32, i32)>>> = OnceLock::new();
	S.get_or_init(|| Mutex::new(HashMap::new()))
}

#[no_mangle]
pub extern "C" fn oakcore_audioparams_create(
	sample_rate: c_int,
	channel_layout: u64,
	format: c_int,
) -> *mut OakAudioParams {
	// The timebase starts at 1/sample_rate, mirroring the real header.
	let p = MockAudioParams {
		sample_rate,
		channel_layout,
		format,
		stream_index: 0,
		duration: 0,
		time_base_num: 1,
		time_base_den: sample_rate,
	};
	let raw = Box::into_raw(Box::new(p.clone()));
	audio_params_store().lock().unwrap().insert(raw as usize, p);
	raw as *mut OakAudioParams
}

#[no_mangle]
pub extern "C" fn oakcore_audioparams_free(params: *mut OakAudioParams) {
	if params.is_null() {
		return;
	}
	audio_params_store()
		.lock()
		.unwrap()
		.remove(&(params as usize));
	// SAFETY: `params` was produced by `oakcore_audioparams_create` as a
	// boxed `MockAudioParams`; we hold the only reference after removal.
	unsafe { drop(Box::from_raw(params as *mut MockAudioParams)) };
}

#[no_mangle]
pub extern "C" fn oakcore_audioparams_sample_rate(params: *const OakAudioParams) -> c_int {
	audio_params_get(params as *const c_void).sample_rate
}

fn audio_params_set(ctx: *mut c_void, f: impl FnOnce(&mut MockAudioParams)) {
	let mut store = audio_params_store().lock().unwrap();
	if let Some(p) = store.get_mut(&(ctx as usize)) {
		f(p);
	}
}

#[no_mangle]
pub extern "C" fn oakcore_audioparams_set_sample_rate(
	params: *mut OakAudioParams,
	sample_rate: c_int,
) {
	audio_params_set(params as *mut c_void, |p| p.sample_rate = sample_rate);
}

#[no_mangle]
pub extern "C" fn oakcore_audioparams_channel_layout(params: *const OakAudioParams) -> u64 {
	audio_params_get(params as *const c_void).channel_layout
}

#[no_mangle]
pub extern "C" fn oakcore_audioparams_set_channel_layout(
	params: *mut OakAudioParams,
	layout: u64,
) {
	audio_params_set(params as *mut c_void, |p| p.channel_layout = layout);
}

#[no_mangle]
pub extern "C" fn oakcore_audioparams_set_time_base(
	params: *mut OakAudioParams,
	num: c_int,
	den: c_int,
) {
	audio_params_set(params as *mut c_void, |p| {
		p.time_base_num = num;
		p.time_base_den = den;
	});
}

#[no_mangle]
pub extern "C" fn oakcore_audioparams_set_format(params: *mut OakAudioParams, format: c_int) {
	audio_params_set(params as *mut c_void, |p| p.format = format);
}

#[no_mangle]
pub extern "C" fn oakcore_audioparams_set_stream_index(
	params: *mut OakAudioParams,
	index: c_int,
) {
	audio_params_set(params as *mut c_void, |p| p.stream_index = index);
}

#[no_mangle]
pub extern "C" fn oakcore_audioparams_set_duration(params: *mut OakAudioParams, duration: i64) {
	audio_params_set(params as *mut c_void, |p| p.duration = duration);
}

#[no_mangle]
pub extern "C" fn oakcore_audioparams_channel_count(params: *const OakAudioParams) -> c_int {
	audio_params_get(params as *const c_void)
		.channel_layout
		.count_ones() as c_int
}

#[no_mangle]
pub extern "C" fn oakcore_audioparams_format(params: *const OakAudioParams) -> c_int {
	audio_params_get(params as *const c_void).format
}

#[no_mangle]
pub extern "C" fn oakcore_audioparams_stream_index(params: *const OakAudioParams) -> c_int {
	audio_params_get(params as *const c_void).stream_index
}

#[no_mangle]
pub extern "C" fn oakcore_audioparams_duration(params: *const OakAudioParams) -> i64 {
	audio_params_get(params as *const c_void).duration
}

#[no_mangle]
pub extern "C" fn oakcore_audioparams_is_valid(params: *const OakAudioParams) -> c_int {
	let p = audio_params_get(params as *const c_void);
	(p.sample_rate > 0 && p.channel_layout != 0 && p.format >= 0) as c_int
}

#[no_mangle]
pub extern "C" fn oakcore_audioparams_time_base(params: *const OakAudioParams) -> *mut c_void {
	let p = audio_params_get(params as *const c_void);
	let r = (p.time_base_num, p.time_base_den);
	let raw = Box::into_raw(Box::new(r));
	rational_store().lock().unwrap().insert(raw as usize, r);
	raw as *mut c_void
}

#[no_mangle]
pub extern "C" fn oakcore_rational_numerator(rational: *const c_void) -> c_int {
	rational_store()
		.lock()
		.unwrap()
		.get(&(rational as usize))
		.map(|r| r.0)
		.unwrap_or(0)
}

#[no_mangle]
pub extern "C" fn oakcore_rational_denominator(rational: *const c_void) -> c_int {
	rational_store()
		.lock()
		.unwrap()
		.get(&(rational as usize))
		.map(|r| r.1)
		.unwrap_or(0)
}

#[no_mangle]
pub extern "C" fn oakcore_rational_free(rational: *mut c_void) {
	if rational.is_null() {
		return;
	}
	rational_store().lock().unwrap().remove(&(rational as usize));
	// SAFETY: `rational` was produced by `oakcore_audioparams_time_base` as a
	// boxed `(i32, i32)` pair; we hold the only reference after removal.
	unsafe { drop(Box::from_raw(rational as *mut (i32, i32))) };
}

// ---------------------------------------------------------------------------
// oakcommon_subtitleparams_*
// ---------------------------------------------------------------------------

#[no_mangle]
pub extern "C" fn oakcommon_subtitleparams_get_stream_index(_params: OakSubtitleParams) -> c_int {
	0
}

#[no_mangle]
pub extern "C" fn oakcommon_subtitleparams_generate_ass_header(
	_params: OakSubtitleParams,
	_width: c_int,
	_height: c_int,
) {
}

#[no_mangle]
pub extern "C" fn oakcommon_subtitleparams_add_subtitle(
	_params: OakSubtitleParams,
	_text: *const c_char,
) {
}

// ---------------------------------------------------------------------------
// oakcommon_config_*
// ---------------------------------------------------------------------------

#[no_mangle]
pub extern "C" fn oakcommon_config_get_int(
	_group: *const c_char,
	_key: *const c_char,
	default: c_int,
) -> c_int {
	default
}

#[no_mangle]
pub extern "C" fn oakcommon_config_get_bool(
	_group: *const c_char,
	_key: *const c_char,
	default: c_int,
) -> c_int {
	default
}

#[no_mangle]
pub extern "C" fn oakcommon_config_get(
	_group: *const c_char,
	_key: *const c_char,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	// Empty value: report the size needed (1 for NUL) and write NUL.
	if buf_size <= 0 {
		return 1;
	}
	if !buf.is_null() {
		unsafe { *buf = 0 };
	}
	1
}

// ---------------------------------------------------------------------------
// oakcommon_filefunctions_*
// ---------------------------------------------------------------------------

static FAKE_PATH: &[u8] = b"/mock/config/oak\0";

#[no_mangle]
pub extern "C" fn oakcommon_filefunctions_init() {}

#[no_mangle]
pub extern "C" fn oakcommon_filefunctions_get_configuration_location(
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	copy_cstr(FAKE_PATH, buf, buf_size)
}

#[no_mangle]
pub extern "C" fn oakcommon_filefunctions_get_unique_file_identifier(path: *const c_char) -> i64 {
	if path.is_null() {
		return 0;
	}
	unsafe { CStr::from_ptr(path) }
		.to_bytes()
		.iter()
		.fold(14695981039346656037u64, |acc, &b| (acc ^ b as u64).wrapping_mul(1099511628211))
		as i64
}

#[no_mangle]
pub extern "C" fn oakcommon_filefunctions_get_application_path(
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	copy_cstr(b"/mock/app/oak\0", buf, buf_size)
}

#[no_mangle]
pub extern "C" fn oakcommon_filefunctions_free(_ptr: *mut c_void) {}

// ---------------------------------------------------------------------------
// oakcommon_colortransform_*
// ---------------------------------------------------------------------------

#[no_mangle]
pub extern "C" fn oakcommon_colortransform_init_output(
	_src_colorspace: c_int,
	_src_trc: c_int,
	_dst_colorspace: c_int,
	_dst_trc: c_int,
	_premultiplied: c_int,
	_chroma_coeffs: *const c_void,
) -> OakVideoParams {
	oakcommon_videoparams_init()
}

#[no_mangle]
pub extern "C" fn oakcommon_colortransform_get_output(params: OakVideoParams, out: *mut OakVideoParams) {
	if !out.is_null() {
		unsafe { *out = params.clone() };
	}
}

#[no_mangle]
pub extern "C" fn oakcommon_colortransform_free(params: *mut OakVideoParams) {
	oakcommon_videoparams_free(params);
}

// ---------------------------------------------------------------------------
// oakcommon_ffmpegutils_* (pure enum mapping; identity is a safe default)
// ---------------------------------------------------------------------------

#[no_mangle]
pub extern "C" fn oakcommon_ffmpegutils_get_native_sample_format(sample_format: c_int) -> c_int {
	sample_format
}

#[no_mangle]
pub extern "C" fn oakcommon_ffmpegutils_get_compatible_pixel_format(format: c_int) -> c_int {
	format
}

#[no_mangle]
pub extern "C" fn oakcommon_ffmpegutils_get_ffmpeg_pixel_format(format: c_int) -> c_int {
	format
}

#[no_mangle]
pub extern "C" fn oakcommon_ffmpegutils_get_ffmpeg_sample_format(format: c_int) -> c_int {
	format
}

#[no_mangle]
pub extern "C" fn oakcommon_ffmpegutils_get_compatible_bridge_pixel_format(format: c_int) -> c_int {
	format
}

#[no_mangle]
pub extern "C" fn oakcommon_ffmpegutils_convert_jpeg_space_to_regular_space(format: c_int) -> c_int {
	format
}

// ---------------------------------------------------------------------------
// oakcommon_oiioutils_*
// ---------------------------------------------------------------------------

#[no_mangle]
pub extern "C" fn oakcommon_oiioutils_init() {}

#[no_mangle]
pub extern "C" fn oakcommon_oiioutils_get_oiio_base_type_from_format(format: c_int) -> c_int {
	format
}

#[no_mangle]
pub extern "C" fn oakcommon_oiioutils_get_format_from_oiio_basetype(basetype: c_int) -> c_int {
	basetype
}

#[no_mangle]
pub extern "C" fn oakcommon_oiioutils_get_pixel_aspect_ratio(
	_width: c_int,
	_height: c_int,
	out_num: *mut c_int,
	out_den: *mut c_int,
) -> c_int {
	if !out_num.is_null() {
		unsafe { *out_num = 1 };
	}
	if !out_den.is_null() {
		unsafe { *out_den = 1 };
	}
	1
}

#[no_mangle]
pub extern "C" fn oakcommon_oiioutils_free() {}

// ---------------------------------------------------------------------------
// oakrender_cancelatom_* (real in-memory cancel state)
// ---------------------------------------------------------------------------

static CANCEL_FLAGS: OnceLock<Mutex<HashMap<usize, bool>>> = OnceLock::new();

fn cancel_flags() -> &'static Mutex<HashMap<usize, bool>> {
	CANCEL_FLAGS.get_or_init(|| Mutex::new(HashMap::new()))
}

#[no_mangle]
pub extern "C" fn oakrender_cancelatom_init() -> OakCancelAtom {
	static COUNTER: AtomicU64 = AtomicU64::new(1);
	let id = COUNTER.fetch_add(1, Ordering::SeqCst) as usize;
	cancel_flags().lock().unwrap().insert(id, false);
	OakCancelAtom {
		ctx: id as *mut c_void,
		addref: None,
		release: None,
		abi_version: OAKCODEC_ABI_VERSION,
	}
}

#[no_mangle]
pub extern "C" fn oakrender_cancelatom_free(atom: *mut OakCancelAtom) {
	if atom.is_null() {
		return;
	}
	unsafe { cancel_flags().lock().unwrap().remove(&((*atom).ctx as usize)) };
}

#[no_mangle]
pub extern "C" fn oakrender_cancelatom_is_cancelled(atom: OakCancelAtom) -> c_int {
	(*cancel_flags().lock().unwrap().get(&(atom.ctx as usize)).unwrap_or(&false)) as c_int
}

#[no_mangle]
pub extern "C" fn oakrender_cancelatom_heard_cancel(atom: OakCancelAtom) -> c_int {
	oakrender_cancelatom_is_cancelled(atom)
}

#[no_mangle]
pub extern "C" fn oakrender_cancelatom_cancel(atom: OakCancelAtom) {
	if let Some(f) = cancel_flags().lock().unwrap().get_mut(&(atom.ctx as usize)) {
		*f = true;
	}
}

#[no_mangle]
pub extern "C" fn oakrender_cancelatom_get_native(atom: OakCancelAtom) -> *mut c_void {
	atom.ctx
}

// ---------------------------------------------------------------------------
// oakrender_display_texture_* / codec_frame_* (neutral)
// ---------------------------------------------------------------------------

#[no_mangle]
pub extern "C" fn oakrender_display_texture_create(
	_renderer: OakRenderRenderer,
	_params: *const crate::bridge::render::oakrender_video_params,
	_data: *const c_void,
	_linesize: c_int,
) -> OakRenderTexture {
	OakRenderTexture {
		ctx: std::ptr::null_mut(),
		addref: None,
		release: None,
		abi_version: OAKCODEC_ABI_VERSION,
	}
}

#[no_mangle]
pub extern "C" fn oakrender_display_texture_retain(texture: OakRenderTexture) -> OakRenderTexture {
	texture
}

#[no_mangle]
pub extern "C" fn oakrender_display_texture_free(_texture: *mut OakRenderTexture) {}

#[no_mangle]
pub extern "C" fn oakrender_display_texture_upload(_texture: OakRenderTexture) -> c_int {
	0
}

#[no_mangle]
pub extern "C" fn oakrender_display_texture_download(
	_texture: OakRenderTexture,
	_pixels: *mut c_void,
	_linesize: c_int,
) -> c_int {
	0
}

#[no_mangle]
pub extern "C" fn oakrender_display_texture_get_params(
	_texture: OakRenderTexture,
	_out: *mut crate::bridge::render::oakrender_video_params,
) -> c_int {
	0
}

#[no_mangle]
pub extern "C" fn oakrender_display_texture_id(_texture: OakRenderTexture) -> c_int {
	0
}

#[no_mangle]
pub extern "C" fn oakrender_display_texture_is_dummy(_texture: OakRenderTexture) -> c_int {
	1
}

#[no_mangle]
pub extern "C" fn oakrender_display_texture_get_frame(
	_texture: OakRenderTexture,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	if buf_size <= 0 {
		return 1;
	}
	if !buf.is_null() {
		unsafe { *buf = 0 };
	}
	1
}

#[no_mangle]
pub extern "C" fn oakrender_codec_frame_width(_frame: OakCodecFrame) -> c_int {
	0
}

#[no_mangle]
pub extern "C" fn oakrender_codec_frame_height(_frame: OakCodecFrame) -> c_int {
	0
}

#[no_mangle]
pub extern "C" fn oakrender_codec_frame_fb_format(_frame: OakCodecFrame) -> c_int {
	0
}

#[no_mangle]
pub extern "C" fn oakrender_codec_frame_free(_frame: *mut OakCodecFrame) {}

#[no_mangle]
pub extern "C" fn oakrender_codec_frame_allocate(_frame: OakCodecFrame) -> c_int {
	1
}

#[no_mangle]
pub extern "C" fn oakrender_codec_frame_linesize_bytes(_frame: OakCodecFrame) -> c_int {
	0
}

#[no_mangle]
pub extern "C" fn oakrender_codec_frame_is_allocated(_frame: OakCodecFrame) -> c_int {
	1
}

#[no_mangle]
pub extern "C" fn oakrender_display_renderer_blit_color_managed(
	_renderer: OakRenderRenderer,
	_job: *const c_void,
	_dst_texture: OakRenderTexture,
	_params: *const crate::bridge::render::oakrender_video_params,
) -> c_int {
	0
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Two-stage string copy helper used by the mock getters. Returns the
/// total size needed (including the trailing NUL), or truncates the buffer
/// and writes a NUL terminator when the buffer is too small.
fn copy_cstr(src: &[u8], buf: *mut c_char, buf_size: c_int) -> c_int {
	let needed = src.len() as c_int;
	if buf.is_null() || buf_size < needed {
		return needed;
	}
	unsafe {
		for (i, &b) in src.iter().enumerate() {
			*buf.add(i) = b as c_char;
		}
	}
	needed
}

// Silence unused-import warnings when the by-value handle types are not
// referenced by every build; they are part of the mock's public surface.
#[allow(unused)]
fn _keep(_: OakAudioParams, _: OakNodeBlock) {}
