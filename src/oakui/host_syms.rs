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

//! In-process stand-ins for the C++ host symbols the module crates call.
//!
//! The oakcodec / oakcommon crates reference a handful of symbols that in
//! the real desktop product live in the C++ host process (`liboakcore` and
//! `ffmpeg_bridge`): `oakcore_audioparams_*`, `oakcore_rational_*` and
//! `fb_find_best_pix_fmt_of_list`. The facade's own test binaries provide
//! the same stubs in `crates/oakengine/tests/common/mod.rs`; this module is
//! the equivalent for the app binary, so linking oakengine (and through it
//! oakcodec/oakcommon) never leaves undefined symbols.
//!
//! The stubs are small, in-memory and functional enough for the app's use:
//! the audio-params handle carries the fields the codec probes read back,
//! and the rational helpers store the (num, den) pair behind an opaque
//! pointer.

use std::collections::HashMap;
use std::ffi::{c_char, c_int, c_void};
use std::sync::{Mutex, OnceLock};

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
	rational_store().lock().unwrap().remove(&(rational as usize));
	// SAFETY: produced by `oakcore_audioparams_time_base`; we hold the only
	// reference after removal.
	unsafe { drop(Box::from_raw(rational as *mut (i32, i32))) };
}

/// `fb_find_best_pix_fmt_of_list` — pick the entry of a
/// `FB_PIX_FMT_NONE`-terminated list closest to `pix_fmt` (the real
/// implementation lives in ffmpeg_bridge). Stub: exact matches win,
/// otherwise the first (most desirable) candidate.
#[no_mangle]
pub extern "C" fn fb_find_best_pix_fmt_of_list(
	list: *const c_int,
	pix_fmt: c_int,
) -> c_int {
	if list.is_null() {
		return 0;
	}
	let mut i = 0;
	let mut first: c_int = 0;
	loop {
		// SAFETY: `list` is `FB_PIX_FMT_NONE`-terminated; the read is within
		// bounds by construction.
		let entry = unsafe { *list.add(i) };
		if entry == 0 {
			return first;
		}
		if i == 0 {
			first = entry;
		}
		if entry == pix_fmt {
			return entry;
		}
		i += 1;
	}
}
