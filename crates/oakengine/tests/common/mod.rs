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

//! Shared test support, included from every integration test via
//! `#[path = "common/mod.rs"] mod common;`.
//!
//! Two jobs:
//!
//! 1. **Force rustc to link every module crate's rlib** into the test
//!    binary ([`force_link`]). The facade itself only references the
//!    modules through `extern "C"` imports (see src/bridge), so rustc
//!    would otherwise drop the dev-dependency rlibs from the link and
//!    leave the imports undefined.
//!
//! 2. **Provide the `oakcore_*` symbols** ([`oakcore_stubs`]) that the
//!    oakcodec rlib references: `oakcore_audioparams_*` /
//!    `oakcore_rational_*` live in the C++ liboakcore (only linked in the
//!    real build), so cargo tests define minimal in-memory mocks — the
//!    same mock the oakcodec crate itself compiles under `#[cfg(test)]`
//!    (src/bridge/test_stubs.rs). The real dylib behavior is required
//!    for actual media decode; those facade tests are `#[ignore]`.

#![allow(dead_code, unused_variables)]

use std::collections::HashMap;
use std::ffi::{c_char, c_int, c_void};
use std::sync::{Mutex, OnceLock};

/// Force the module crates into the link.
#[allow(unused)]
pub fn force_link() -> usize {
	let fns: [usize; 12] = [
		oakundo::ffi::undostack::oakundo_undostack_init as usize,
		oakcodec::ffi::format::oakcodec_encoding_format_count as usize,
		oakaudio::ffi::waveform::oakaudio_waveform_length as usize,
		oakrender::ffi::cache::oakrender_cache_indicator_height as usize,
		oakcommon::ffi::config::oakcommon_config_get_int as usize,
		oakplugin::ffi::oakplugin_host_plugin_count as usize,
		oaknode::ffi::project::oaknode_project_init as usize,
		oaktimeline::ffi::marker::oaktimeline_marker_list_create as usize,
		oaktask::ffi::manager::oaktask_manager_init as usize,
		// The oaknode serializer bridge resolves these at runtime via
		// dlsym(RTLD_DEFAULT); force them into the link so the oaknode
		// serializer's XML writer/reader and the undo command factory are
		// visible to dlsym in every test binary (the oaknode rlib only
		// references them through dlsym, so the linker would otherwise drop
		// them from the oakcommon/oakundo rlib objects).
		oakcommon::ffi::xmlutils::oakcommon_xml_writer_init as usize,
		oakcommon::ffi::xmlutils::oakcommon_xml_reader_init as usize,
		oakundo::ffi::command::oakundo_command_init as usize,
	];
	fns.iter().sum()
}

// ---------------------------------------------------------------------------
// oakcore_* stubs (see module docs)
// ---------------------------------------------------------------------------

/// Opaque `OakAudioParams` handle type (the real one lives in liboakcore).
#[repr(C)]
pub struct OakAudioParams {
	_opaque: [u8; 0],
}

/// Per-`OakAudioParams` backing state.
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
	store.get(&(ctx as usize)).cloned().unwrap_or_default()
}

fn audio_params_set(ctx: *mut c_void, f: impl FnOnce(&mut MockAudioParams)) {
	let mut store = audio_params_store().lock().unwrap();
	if let Some(p) = store.get_mut(&(ctx as usize)) {
		f(p);
	}
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
	// SAFETY: produced by `oakcore_audioparams_create`; we hold the only
	// reference after removal.
	unsafe { drop(Box::from_raw(params as *mut MockAudioParams)) };
}

#[no_mangle]
pub extern "C" fn oakcore_audioparams_sample_rate(params: *const OakAudioParams) -> c_int {
	audio_params_get(params as *const c_void).sample_rate
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
pub extern "C" fn oakcore_audioparams_set_channel_layout(params: *mut OakAudioParams, layout: u64) {
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
pub extern "C" fn oakcore_audioparams_set_stream_index(params: *mut OakAudioParams, index: c_int) {
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
	rational_store()
		.lock()
		.unwrap()
		.remove(&(rational as usize));
	// SAFETY: produced by `oakcore_audioparams_time_base` as a boxed
	// `(i32, i32)` pair; we hold the only reference after removal.
	unsafe { drop(Box::from_raw(rational as *mut (i32, i32))) };
}

// ---------------------------------------------------------------------------
// ffmpeg_bridge (`fb_*`) stubs
//
// The oakaudio processor family drives the C++ libffmpeg_bridge audio
// graph (`src/audio/rust/src/bridge/ffmpeg.rs`), which is not linked
// under `cargo test`. These minimal mocks keep the link green; the
// processor family's real behavior requires libffmpeg_bridge and its
// tests are `#[ignore]`d with that reason.
// ---------------------------------------------------------------------------

/// Opaque audio graph handle.
#[repr(C)]
pub struct AudioGraph {
	_opaque: [u8; 0],
}
/// Opaque frame handle.
#[repr(C)]
pub struct Frame {
	_opaque: [u8; 0],
}
/// Opaque packet handle.
#[repr(C)]
pub struct Packet {
	_opaque: [u8; 0],
}
/// Opaque decoder handle.
#[repr(C)]
pub struct Decoder {
	_opaque: [u8; 0],
}
/// Opaque graph config.
#[repr(C)]
pub struct AudioGraphConfig {
	_opaque: [u8; 0],
}
/// Opaque stream-info out struct.
#[repr(C)]
pub struct FBStreamInfo {
	_opaque: [u8; 0],
}

#[no_mangle]
pub extern "C" fn fb_audio_graph_create(_config: *const AudioGraphConfig) -> *mut AudioGraph {
	std::ptr::null_mut()
}
#[no_mangle]
pub extern "C" fn fb_audio_graph_free(graph: *mut *mut AudioGraph) {
	if !graph.is_null() {
		unsafe { *graph = std::ptr::null_mut() };
	}
}
#[no_mangle]
pub extern "C" fn fb_audio_graph_push(
	_graph: *mut AudioGraph,
	_channel_data: *const *const u8,
	_nb_samples: c_int,
) -> c_int {
	-1
}
#[no_mangle]
pub extern "C" fn fb_audio_graph_pull(_graph: *mut AudioGraph, _out_frame: *mut Frame) -> c_int {
	0
}
#[no_mangle]
pub extern "C" fn fb_channel_layout_get_channels(_mask: u64) -> c_int {
	0
}
#[no_mangle]
pub extern "C" fn fb_channel_layout_default(_nb_channels: c_int) -> u64 {
	0
}
#[no_mangle]
pub extern "C" fn fb_frame_alloc() -> *mut Frame {
	std::ptr::null_mut()
}
#[no_mangle]
pub extern "C" fn fb_frame_free(frame: *mut *mut Frame) {
	if !frame.is_null() {
		unsafe { *frame = std::ptr::null_mut() };
	}
}
#[no_mangle]
pub extern "C" fn fb_frame_unref(_frame: *mut Frame) {}
#[no_mangle]
pub extern "C" fn fb_frame_get_nb_samples(_frame: *const Frame) -> c_int {
	0
}
#[no_mangle]
pub extern "C" fn fb_frame_set_nb_samples(_frame: *mut Frame, _nb_samples: c_int) {}
#[no_mangle]
pub extern "C" fn fb_frame_get_sample_rate(_frame: *const Frame) -> c_int {
	0
}
#[no_mangle]
pub extern "C" fn fb_frame_get_format(_frame: *const Frame) -> c_int {
	0
}
#[no_mangle]
pub extern "C" fn fb_frame_get_channel_layout_mask(_frame: *const Frame) -> u64 {
	0
}
#[no_mangle]
pub extern "C" fn fb_frame_get_data(_frame: *mut Frame, _plane: c_int) -> *mut u8 {
	std::ptr::null_mut()
}
#[no_mangle]
pub extern "C" fn fb_frame_get_data_const(_frame: *const Frame, _plane: c_int) -> *const u8 {
	std::ptr::null()
}
#[no_mangle]
pub extern "C" fn fb_frame_get_linesize(_frame: *const Frame, _plane: c_int) -> c_int {
	0
}
#[no_mangle]
pub extern "C" fn fb_packet_alloc() -> *mut Packet {
	std::ptr::null_mut()
}
#[no_mangle]
pub extern "C" fn fb_packet_free(packet: *mut *mut Packet) {
	if !packet.is_null() {
		unsafe { *packet = std::ptr::null_mut() };
	}
}
#[no_mangle]
pub extern "C" fn fb_packet_unref(_packet: *mut Packet) {}
#[no_mangle]
pub extern "C" fn fb_decoder_create() -> *mut Decoder {
	std::ptr::null_mut()
}
#[no_mangle]
pub extern "C" fn fb_decoder_free(decoder: *mut *mut Decoder) {
	if !decoder.is_null() {
		unsafe { *decoder = std::ptr::null_mut() };
	}
}
#[no_mangle]
pub extern "C" fn fb_decoder_open(
	_decoder: *mut Decoder,
	_filename: *const c_char,
	_stream_index: c_int,
) -> c_int {
	-1
}
#[no_mangle]
pub extern "C" fn fb_decoder_close(_decoder: *mut Decoder) {}
#[no_mangle]
pub extern "C" fn fb_decoder_get_frame(
	_decoder: *mut Decoder,
	_packet: *mut Packet,
	_frame: *mut Frame,
) -> c_int {
	-1
}
#[no_mangle]
pub extern "C" fn fb_decoder_get_packet(_decoder: *mut Decoder, _packet: *mut Packet) -> c_int {
	-1
}
#[no_mangle]
pub extern "C" fn fb_decoder_get_stream_info(
	_decoder: *const Decoder,
	_out: *mut FBStreamInfo,
) -> c_int {
	-1
}
#[no_mangle]
pub extern "C" fn fb_decoder_get_format_start_time(_decoder: *const Decoder) -> i64 {
	0
}
#[no_mangle]
pub extern "C" fn fb_decoder_get_format_duration(_decoder: *const Decoder) -> i64 {
	0
}
