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
//! 2. **Provide the `oakcore_*` symbols** that the
//!    oakcodec rlib references: `oakcore_audioparams_*` /
//!    `oakcore_rational_*` live in the C++ liboakcore (only linked in the
//!    real build), so cargo tests define minimal in-memory mocks — the
//!    same mock the oakcodec crate itself compiles under `#[cfg(test)]`
//!    (src/bridge/test_stubs.rs). The real dylib behavior is required
//!    for actual media decode; those facade tests are `#[ignore]`.

#![allow(dead_code, unused_variables)]

use std::collections::HashMap;
use std::ffi::{c_int, c_void};
use std::sync::{Mutex, OnceLock};

/// One public direct-Rust symbol per module crate (the module C ABIs are
/// deleted; this mirrors the anchors in `crates/oakengine/src/linkage.rs`).
/// Under unit tests the crates are real dependencies of the lib target and
/// are linked regardless; the array doubles as a compile-time proof that
/// the anchor paths match the current module layouts.
#[allow(unused)]
pub fn force_link() -> usize {
	let fns: [usize; 12] = [
		oakundo::undostack::undostack_init as usize,
		oakcodec::exportformat::Format::get_name as usize,
		oakaudio::processor::Processor::init as usize,
		oakrender::manager::RenderManager::init as usize,
		oakcommon::configstore::ConfigStore::instance as usize,
		oakplugin::host::Host::global as usize,
		oaknode::project::Project::new as usize,
		oaktimeline::marker::TimelineMarkerList::new as usize,
		oaktask::manager::TaskManager::init as usize,
		// oakundo/oakcommon no longer export a C ABI; their handle-level
		// Rust API functions anchor the rlibs into every test binary (the
		// same pattern as `crates/oakengine/src/linkage.rs`).
		oakcommon::xmlutils::XmlWriter::new as usize,
		oakcommon::xmlutils::XmlReader::new as usize,
		oakundo::undocommand::command_init as usize,
	];
	fns.iter().sum()
}

/// Serialize every test that touches the process-wide AudioManager
/// singleton. The former integration tests were separate processes; as
/// unit tests they share one process (and one singleton), so the manager
/// tests must take a shared lock instead of relying on process isolation.
pub fn with_manager(f: impl FnOnce()) {
	static LOCK: Mutex<()> = Mutex::new(());
	let _g = LOCK.lock().unwrap_or_else(|e| e.into_inner());
	f()
}

/// Serializes every test that reads or writes the `Storage` config group
/// (the facade's write-through library selection — see src/storage.rs).
/// The config store is process-global, so the write-through tests and the
/// tests that disable the backend must take this lock for their whole
/// body instead of racing on the shared store.
pub static STORAGE_CONFIG_LOCK: Mutex<()> = Mutex::new(());

/// Run `f` with the write-through storage backend disabled
/// (`Storage/Backend = "off"`). Tests that push undo commands on real
/// projects (e.g. `oakengine_project_add_node`) would otherwise bind them
/// to the default user library and write there; disabling the backend
/// keeps them side-effect-free. The value intentionally persists — every
/// storage test sets its own backend explicitly under
/// [`STORAGE_CONFIG_LOCK`].
pub fn with_storage_off<R>(f: impl FnOnce() -> R) -> R {
	let _g = storage_off_guard();
	f()
}

/// Take the storage-config lock AND disable the write-through backend,
/// returning the guard: a test that pushes undo commands throughout its
/// body holds the guard (and thus the lock) for its whole lifetime, so a
/// concurrently running storage test cannot flip the backend mid-test.
pub fn storage_off_guard() -> std::sync::MutexGuard<'static, ()> {
	let g = STORAGE_CONFIG_LOCK.lock().unwrap_or_else(|e| e.into_inner());
	oakcommon::configstore::ConfigStore::instance().set(Some("Storage"), "Backend", "off");
	g
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
