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

//! FFI-layer contract tests (ffi.rs). The exhaustive matrix runs against
//! the unchanged C++ gtest suite (`src/audio/tests`); these tests pin
//! Rust-side specifics (handle contracts, the singleton ledger, struct
//! layout).

mod common;

use std::mem::{align_of, size_of};
use std::sync::Mutex;

use oakaudio::error::{OAKAUDIO_E_INVALID, OAKAUDIO_OK};
use oakaudio::ffi::levelmeter::oakaudio_levelmeter_analyze;
use oakaudio::ffi::levelmeter::{ChannelStats, MeterStats};
use oakaudio::ffi::manager::{
	oakaudio_debug_alive_count, oakaudio_manager_create_instance,
	oakaudio_manager_destroy_instance, oakaudio_manager_free, oakaudio_manager_instance,
};
use oakaudio::ffi::processor::{oakaudio_processor_free, oakaudio_processor_init};
use oakaudio::ffi::sync::{OffsetResult, SourceClip};
use oakaudio::ffi::waveform::{oakaudio_waveform_free, oakaudio_waveform_init};

/// Serializes tests that touch the process-wide singleton and the alive
/// ledger.
static LOCK: Mutex<()> = Mutex::new(());

/// Every exported handle-returning function (processor_init, waveform_init)
/// returns ctx==NULL on failure and a valid refcounted handle on success,
/// with abi_version == OAKAUDIO_ABI_VERSION stamped.
#[test]
fn handle_contract_all_exports() {
	let _guard = LOCK.lock().unwrap();

	let mut p = unsafe { oakaudio_processor_init() };
	assert!(!p.ctx.is_null());
	assert_eq!(p.abi_version, oakaudio::handle::OAKAUDIO_ABI_VERSION);

	let mut w = unsafe { oakaudio_waveform_init() };
	assert!(!w.ctx.is_null());
	assert_eq!(w.abi_version, oakaudio::handle::OAKAUDIO_ABI_VERSION);

	unsafe { oakaudio_processor_free(&mut p) };
	unsafe { oakaudio_waveform_free(&mut w) };
}

/// free(NULL)/free(empty) are no-ops across every free export.
#[test]
fn free_null_noop_all_exports() {
	let _guard = LOCK.lock().unwrap();

	let mut p = oakaudio::handle::CHandle::null();
	unsafe { oakaudio_processor_free(&mut p) };
	assert!(p.ctx.is_null());

	let mut w = oakaudio::handle::CHandle::null();
	unsafe { oakaudio_waveform_free(&mut w) };
	assert!(w.ctx.is_null());

	let mut m = oakaudio::handle::CHandle::null();
	unsafe { oakaudio_manager_free(&mut m) };
	assert!(m.ctx.is_null());

	// NULL pointer itself is a no-op.
	unsafe { oakaudio_processor_free(std::ptr::null_mut()) };
	unsafe { oakaudio_waveform_free(std::ptr::null_mut()) };
	unsafe { oakaudio_manager_free(std::ptr::null_mut()) };
}

/// The manager singleton: instance() is the same borrowed handle across
/// calls; create/destroy flip validity; oakaudio_debug_alive_count moves
/// predictably and returns to baseline.
#[test]
fn manager_singleton_and_alive_count() {
	let _guard = LOCK.lock().unwrap();

	let before = unsafe { oakaudio_debug_alive_count() };

	unsafe { oakaudio_manager_destroy_instance() };
	let none = unsafe { oakaudio_manager_instance() };
	assert!(none.ctx.is_null());
	// The empty instance handle is the shared `null()` (no ABI version).
	assert_eq!(none.abi_version, 0);

	unsafe { oakaudio_manager_create_instance() };
	let m1 = unsafe { oakaudio_manager_instance() };
	let m2 = unsafe { oakaudio_manager_instance() };
	assert!(!m1.ctx.is_null());
	assert_eq!(
		m1.ctx, m2.ctx,
		"instance() must be the same borrowed handle"
	);

	// A processor bumps the ledger; freeing it returns to baseline.
	assert_eq!(unsafe { oakaudio_debug_alive_count() }, before);
	let mut p = unsafe { oakaudio_processor_init() };
	assert_eq!(unsafe { oakaudio_debug_alive_count() }, before + 1);
	unsafe { oakaudio_processor_free(&mut p) };
	assert_eq!(unsafe { oakaudio_debug_alive_count() }, before);

	// Destroy flips the singleton back to empty; create resurrects it.
	unsafe { oakaudio_manager_destroy_instance() };
	assert!(unsafe { oakaudio_manager_instance() }.ctx.is_null());
	unsafe { oakaudio_manager_create_instance() };
	assert!(!unsafe { oakaudio_manager_instance() }.ctx.is_null());
}

/// oakaudio_levelmeter_analyze with NULL summary still computes per-channel
/// stats, and a NULL channels array with capacity 0 is accepted when only
/// the summary is wanted.
#[test]
fn levelmeter_partial_outputs() {
	let data = [0.5f32; 64];
	let planes = [data.as_ptr()];

	// channels only (summary NULL)
	let mut channels = [ChannelStats {
		peak_linear: 0.0,
		peak_db: 0.0,
		rms_linear: 0.0,
		rms_db: 0.0,
		vu_db: 0.0,
	}];
	assert_eq!(
		unsafe {
			oakaudio_levelmeter_analyze(
				planes.as_ptr(),
				1,
				64,
				channels.as_mut_ptr(),
				1,
				std::ptr::null_mut(),
			)
		},
		OAKAUDIO_OK
	);
	assert!((channels[0].peak_linear - 0.5).abs() < 1e-9);

	// summary only (channels NULL, capacity 0)
	let mut summary = MeterStats {
		max_peak_linear: 0.0,
		integrated_lufs: 0.0,
		silence: 0,
	};
	assert_eq!(
		unsafe {
			oakaudio_levelmeter_analyze(
				planes.as_ptr(),
				1,
				64,
				std::ptr::null_mut(),
				0,
				&mut summary,
			)
		},
		OAKAUDIO_OK
	);
	assert!((summary.max_peak_linear - 0.5).abs() < 1e-9);

	// Both NULL is invalid.
	assert_eq!(
		unsafe {
			oakaudio_levelmeter_analyze(
				planes.as_ptr(),
				1,
				64,
				std::ptr::null_mut(),
				0,
				std::ptr::null_mut(),
			)
		},
		OAKAUDIO_E_INVALID
	);
}

/// sync value structs (offset_result/source_clip) are 24/40 bytes and
/// repr(C)-aligned as the C headers dictate, so layout never drifts.
#[test]
fn sync_struct_layout() {
	assert_eq!(size_of::<OffsetResult>(), 24);
	assert_eq!(align_of::<OffsetResult>(), 8);
	assert_eq!(size_of::<SourceClip>(), 40);
	assert_eq!(align_of::<SourceClip>(), 8);
	// The stretch result (f64, i64, f64, i32) pads to 32 bytes.
	assert_eq!(size_of::<oakaudio::ffi::sync::StretchOffsetResult>(), 32);
}
