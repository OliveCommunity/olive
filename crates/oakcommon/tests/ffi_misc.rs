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

//! FFI-level integration tests for the C-ABI `misc`, `debug`, and `current`
//! exports in `oakcommon::ffi`, asserted against the C++ oracle
//! (`src/common/c_api/*.cpp`) and the Rust domain modules
//! (`src/common/rust/src/{miscutils,debug}.rs`).
//!
//! The contract under test (each point matches the implementations):
//! - decibel/lerp/power helpers write into a non-null out-param and reject a
//!   null one with `E_INVALID`; domain helpers never fail, so the only
//!   failure path is the null out-param;
//! - `drop_workflow_behavior_is_valid` returns 1/0 without error codes, and
//!   `drop_workflow_behavior_name` is a non-truncating two-stage getter
//!   ("UNKNOWN" for out-of-range codes);
//! - `debug` exports: null messages are `E_INVALID`, the level-name getter
//!   is a non-truncating two-stage getter, level codes outside 0..=4 are
//!   rejected by `log_set_level` with `E_INVALID`, and the get-level export
//!   rejects a null out-param;
//! - `current` exports: the singleton handle is stamped and `free` is
//!   idempotent, setters/getters reject a null handle (getters also reject a
//!   null out-param), empty slots read back as null, and replacing an
//!   occupied slot runs the previous owner's destructor exactly once.

use std::ffi::{c_char, c_void, CString};
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::Mutex;

use oakcommon::error::{OAKCOMMON_E_INVALID, OAKCOMMON_OK};
use oakcommon::ffi::current::*;
use oakcommon::ffi::debug::*;
use oakcommon::ffi::misc::*;
use oakcommon::handle::{CHandle, OAKCOMMON_ABI_VERSION};

/// Serialises tests that mutate the process-wide `Current` singleton (cargo
/// runs tests on threads).
static CURRENT_LOCK: Mutex<()> = Mutex::new(());

/// Serialises tests that mutate the process-wide logging level.
static DEBUG_LOCK: Mutex<()> = Mutex::new(());

/// Destroy tally for the slot-replacement test.
static DESTROY_COUNT: AtomicUsize = AtomicUsize::new(0);

/// A `DestroyFn` that bumps [`DESTROY_COUNT`].
unsafe extern "C" fn count_destroy(_p: *mut c_void) {
	DESTROY_COUNT.fetch_add(1, Ordering::SeqCst);
}

/// Convert a string slice to a NUL-terminated C string for FFI inputs.
fn to_cstring(s: &str) -> CString {
	CString::new(s).expect("test string must not contain NUL")
}

/// Cheap struct copy: `CHandle` is neither `Clone` nor `Copy`, but every
/// getter takes it by value. Rebuilding from the same fields duplicates
/// only the handle value — the box stays alive as long as the original
/// handle lives, and the getters never release.
fn dup(h: &CHandle) -> CHandle {
	CHandle {
		ctx: h.ctx,
		addref: h.addref,
		release: h.release,
		abi_version: h.abi_version,
	}
}

/// Assert two f64 values agree within `tol`.
fn assert_close(actual: f64, expected: f64, tol: f64) {
	assert!(
		(actual - expected).abs() <= tol,
		"expected {expected} ± {tol}, got {actual}"
	);
}

/// Drive a two-stage string getter against the standard `copy_string`
/// convention: a null-buffer size query, a short buffer that must stay
/// untouched (no truncation), an exact-fit copy with its NUL, and an
/// oversized copy with the tail untouched.
fn assert_two_stage_getter(getter: impl Fn(*mut c_char, i32) -> i32, expected: &str) {
	let required = (expected.len() + 1) as i32;

	// Size query: a null buffer returns the required size, NUL included.
	assert_eq!(getter(std::ptr::null_mut(), 0), required);

	// Short buffer: too small, so nothing is written to it.
	let short_size = (required - 1).max(0);
	let mut short = vec![0xABu8; short_size as usize];
	assert_eq!(
		getter(short.as_mut_ptr() as *mut c_char, short_size),
		required
	);
	assert!(
		short.iter().all(|&b| b == 0xAB),
		"short buffer must stay untouched"
	);

	// Exact fit: payload followed by a NUL.
	let mut exact = vec![0xCDu8; required as usize];
	assert_eq!(
		getter(exact.as_mut_ptr() as *mut c_char, required),
		required
	);
	assert_eq!(&exact[..expected.len()], expected.as_bytes());
	assert_eq!(exact[expected.len()], 0);

	// Oversized: payload and NUL written, tail left as initialized.
	let mut big = vec![0u8; (required + 8) as usize];
	assert_eq!(
		getter(big.as_mut_ptr() as *mut c_char, required + 8),
		required
	);
	assert_eq!(&big[..expected.len()], expected.as_bytes());
	assert_eq!(big[expected.len()], 0);
	assert!(big[(required + 1) as usize..].iter().all(|&b| b == 0));
}

// ---- debug ----

/// A null message is `E_INVALID`.
#[test]
fn debug_log_null_msg_is_invalid() {
	assert_eq!(
		oakcommon_debug_log(0, std::ptr::null()),
		OAKCOMMON_E_INVALID
	);
}

/// Any non-null message returns `OK`, including out-of-range level codes
/// (`log_raw` never fails on the level itself).
#[test]
fn debug_log_returns_ok_for_any_level() {
	let msg = to_cstring("ffi_misc debug_log probe");
	assert_eq!(oakcommon_debug_log(0, msg.as_ptr()), OAKCOMMON_OK);

	let raw = to_cstring("out-of-range level");
	assert_eq!(oakcommon_debug_log(99, raw.as_ptr()), OAKCOMMON_OK);
}

/// Level-name getter is a non-truncating two-stage getter; out-of-range
/// codes yield "UNKNOWN".
#[test]
fn debug_level_name_two_stage() {
	assert_two_stage_getter(
		|buf, size| oakcommon_debug_level_name(0, buf, size),
		"DEBUG",
	);
	assert_two_stage_getter(
		|buf, size| oakcommon_debug_level_name(2, buf, size),
		"WARNING",
	);
	assert_two_stage_getter(
		|buf, size| oakcommon_debug_level_name(4, buf, size),
		"FATAL",
	);
	assert_two_stage_getter(
		|buf, size| oakcommon_debug_level_name(5, buf, size),
		"UNKNOWN",
	);
	assert_two_stage_getter(
		|buf, size| oakcommon_debug_level_name(-1, buf, size),
		"UNKNOWN",
	);
}

/// `log_set_level` accepts 0..=4, rejects everything else with `E_INVALID`
/// without changing the stored level, and `log_get_level` reads it back.
#[test]
fn log_set_get_level_roundtrip_and_invalid() {
	let _guard = DEBUG_LOCK.lock().unwrap_or_else(|e| e.into_inner());

	assert_eq!(oakcommon_log_set_level(0), OAKCOMMON_OK);
	let mut level = -1;
	assert_eq!(oakcommon_log_get_level(&mut level), OAKCOMMON_OK);
	assert_eq!(level, 0);

	assert_eq!(oakcommon_log_set_level(4), OAKCOMMON_OK);
	assert_eq!(oakcommon_log_get_level(&mut level), OAKCOMMON_OK);
	assert_eq!(level, 4);

	assert_eq!(oakcommon_log_set_level(5), OAKCOMMON_E_INVALID);
	assert_eq!(oakcommon_log_set_level(-1), OAKCOMMON_E_INVALID);
	// The rejected set leaves the stored level untouched.
	assert_eq!(oakcommon_log_get_level(&mut level), OAKCOMMON_OK);
	assert_eq!(level, 4);

	// Restore the default so other tests see a clean level.
	assert_eq!(oakcommon_log_set_level(1), OAKCOMMON_OK);
	assert_eq!(oakcommon_log_get_level(&mut level), OAKCOMMON_OK);
	assert_eq!(level, 1);
}

/// The get-level export rejects a null out-param.
#[test]
fn log_get_level_null_out_is_invalid() {
	assert_eq!(
		oakcommon_log_get_level(std::ptr::null_mut()),
		OAKCOMMON_E_INVALID
	);
}

// ---- misc: decibel / lerp ----

/// Linear -> decibels for exact and mid-range inputs; zero (log10 = -inf)
/// collapses to the decibel minimum, negative inputs yield NaN.
#[test]
fn decibel_from_linear_known_values() {
	let mut out = -1.0;

	assert_eq!(oakcommon_decibel_from_linear(1.0, &mut out), OAKCOMMON_OK);
	assert_close(out, 0.0, 1e-9);
	assert_eq!(oakcommon_decibel_from_linear(10.0, &mut out), OAKCOMMON_OK);
	assert_close(out, 20.0, 1e-9);
	assert_eq!(oakcommon_decibel_from_linear(0.5, &mut out), OAKCOMMON_OK);
	assert_close(out, -6.020599913, 1e-6);
	assert_eq!(oakcommon_decibel_from_linear(0.0, &mut out), OAKCOMMON_OK);
	assert_eq!(out, -200.0);
	assert_eq!(oakcommon_decibel_from_linear(-1.0, &mut out), OAKCOMMON_OK);
	assert!(
		out.is_nan(),
		"negative linear input must produce NaN, got {out}"
	);
}

/// Decibels -> linear for exact inputs; results below 1e-6 clamp to 0.0.
#[test]
fn decibel_to_linear_known_values() {
	let mut out = -1.0;

	assert_eq!(oakcommon_decibel_to_linear(0.0, &mut out), OAKCOMMON_OK);
	assert_close(out, 1.0, 1e-9);
	assert_eq!(oakcommon_decibel_to_linear(20.0, &mut out), OAKCOMMON_OK);
	assert_close(out, 10.0, 1e-9);
	assert_eq!(oakcommon_decibel_to_linear(-100.0, &mut out), OAKCOMMON_OK);
	assert_close(out, 1e-5, 1e-12);
	assert_eq!(oakcommon_decibel_to_linear(-200.0, &mut out), OAKCOMMON_OK);
	assert_eq!(out, 0.0);
}

/// Logarithmic position -> decibels: saturates to the minimum below 0.001,
/// to 0 dB above 0.99, and follows the linearization formula in between.
#[test]
fn decibel_from_logarithmic_known_values() {
	let mut out = -1.0;

	assert_eq!(
		oakcommon_decibel_from_logarithmic(0.0, &mut out),
		OAKCOMMON_OK
	);
	assert_eq!(out, -200.0);
	assert_eq!(
		oakcommon_decibel_from_logarithmic(1.0, &mut out),
		OAKCOMMON_OK
	);
	assert_eq!(out, 0.0);
	assert_eq!(
		oakcommon_decibel_from_logarithmic(0.99, &mut out),
		OAKCOMMON_OK
	);
	assert_close(out, 0.0, 1e-6);
	assert_eq!(
		oakcommon_decibel_from_logarithmic(0.5, &mut out),
		OAKCOMMON_OK
	);
	assert_close(out, -16.45, 0.05);
}

/// Decibels -> logarithmic position; `|db| <= 1e-12` snaps to 1.0.
#[test]
fn decibel_to_logarithmic_known_values() {
	let mut out = -1.0;

	assert_eq!(
		oakcommon_decibel_to_logarithmic(0.0, &mut out),
		OAKCOMMON_OK
	);
	assert_eq!(out, 1.0);
	assert_eq!(
		oakcommon_decibel_to_logarithmic(20.0, &mut out),
		OAKCOMMON_OK
	);
	assert_close(out, 1.0, 1e-9);
	assert_eq!(
		oakcommon_decibel_to_logarithmic(-120.0, &mut out),
		OAKCOMMON_OK
	);
	assert_close(out, 4.605e-6, 1e-9);
}

/// Linear amplitude -> logarithmic position (`1 - exp(-linear * lo_g100)`).
#[test]
fn decibel_linear_to_logarithmic_known_values() {
	let mut out = -1.0;

	assert_eq!(
		oakcommon_decibel_linear_to_logarithmic(0.0, &mut out),
		OAKCOMMON_OK
	);
	assert_eq!(out, 0.0);
	assert_eq!(
		oakcommon_decibel_linear_to_logarithmic(1.0, &mut out),
		OAKCOMMON_OK
	);
	assert_close(out, 0.99, 1e-9);
}

/// Logarithmic position -> linear amplitude; values above 0.99 snap to 1.0.
#[test]
fn decibel_logarithmic_to_linear_known_values() {
	let mut out = -1.0;

	assert_eq!(
		oakcommon_decibel_logarithmic_to_linear(0.0, &mut out),
		OAKCOMMON_OK
	);
	assert_eq!(out, 0.0);
	assert_eq!(
		oakcommon_decibel_logarithmic_to_linear(1.0, &mut out),
		OAKCOMMON_OK
	);
	assert_eq!(out, 1.0);
	assert_eq!(
		oakcommon_decibel_logarithmic_to_linear(0.99, &mut out),
		OAKCOMMON_OK
	);
	assert_close(out, 1.0, 1e-6);
}

/// `lerp(a, b, t) = a*(1-t) + b*t` at the endpoints and midpoints.
#[test]
fn lerp_known_values() {
	let mut out = -1.0;

	assert_eq!(oakcommon_lerp(0.0, 10.0, 0.5, &mut out), OAKCOMMON_OK);
	assert_close(out, 5.0, 1e-9);
	assert_eq!(oakcommon_lerp(0.0, 10.0, 0.0, &mut out), OAKCOMMON_OK);
	assert_close(out, 0.0, 1e-9);
	assert_eq!(oakcommon_lerp(0.0, 10.0, 1.0, &mut out), OAKCOMMON_OK);
	assert_close(out, 10.0, 1e-9);
	assert_eq!(oakcommon_lerp(2.0, 4.0, 0.25, &mut out), OAKCOMMON_OK);
	assert_close(out, 2.5, 1e-9);
}

/// Every decibel/lerp export rejects a null out-param with `E_INVALID`.
#[test]
fn decibel_lerp_reject_null_out() {
	assert_eq!(
		oakcommon_decibel_from_linear(1.0, std::ptr::null_mut()),
		OAKCOMMON_E_INVALID
	);
	assert_eq!(
		oakcommon_decibel_to_linear(0.0, std::ptr::null_mut()),
		OAKCOMMON_E_INVALID
	);
	assert_eq!(
		oakcommon_decibel_from_logarithmic(0.5, std::ptr::null_mut()),
		OAKCOMMON_E_INVALID
	);
	assert_eq!(
		oakcommon_decibel_to_logarithmic(0.0, std::ptr::null_mut()),
		OAKCOMMON_E_INVALID
	);
	assert_eq!(
		oakcommon_decibel_linear_to_logarithmic(0.5, std::ptr::null_mut()),
		OAKCOMMON_E_INVALID
	);
	assert_eq!(
		oakcommon_decibel_logarithmic_to_linear(0.5, std::ptr::null_mut()),
		OAKCOMMON_E_INVALID
	);
	assert_eq!(
		oakcommon_lerp(0.0, 1.0, 0.5, std::ptr::null_mut()),
		OAKCOMMON_E_INVALID
	);
}

// ---- misc: drop-workflow behavior / power ----

/// Codes 0..=3 are valid; everything else returns 0.
#[test]
fn drop_workflow_behavior_is_valid() {
	for value in 0..=3 {
		assert_eq!(oakcommon_drop_workflow_behavior_is_valid(value), 1);
	}
	assert_eq!(oakcommon_drop_workflow_behavior_is_valid(4), 0);
	assert_eq!(oakcommon_drop_workflow_behavior_is_valid(-1), 0);
	assert_eq!(oakcommon_drop_workflow_behavior_is_valid(i32::MAX), 0);
}

/// Behavior-name getter is a non-truncating two-stage getter; out-of-range
/// codes yield "UNKNOWN".
#[test]
fn drop_workflow_behavior_name_two_stage() {
	assert_two_stage_getter(
		|buf, size| oakcommon_drop_workflow_behavior_name(0, buf, size),
		"ASK",
	);
	assert_two_stage_getter(
		|buf, size| oakcommon_drop_workflow_behavior_name(1, buf, size),
		"AUTO",
	);
	assert_two_stage_getter(
		|buf, size| oakcommon_drop_workflow_behavior_name(2, buf, size),
		"MANUAL",
	);
	assert_two_stage_getter(
		|buf, size| oakcommon_drop_workflow_behavior_name(3, buf, size),
		"DISABLE",
	);
	assert_two_stage_getter(
		|buf, size| oakcommon_drop_workflow_behavior_name(4, buf, size),
		"UNKNOWN",
	);
	assert_two_stage_getter(
		|buf, size| oakcommon_drop_workflow_behavior_name(-1, buf, size),
		"UNKNOWN",
	);
}

/// Round `value` up to a power of two (wrapping overflow -> 0); a null
/// out-param is `E_INVALID`.
#[test]
fn power_ceil_to_power_of_2() {
	let mut out = 0u32;
	for (input, expected) in [
		(0u32, 0u32),
		(1, 1),
		(2, 2),
		(3, 4),
		(5, 8),
		(9, 16),
		(0x8000_0001, 0),
	] {
		assert_eq!(
			oakcommon_power_ceil_to_power_of_2(input, &mut out),
			OAKCOMMON_OK
		);
		assert_eq!(out, expected, "ceil({input})");
	}
	assert_eq!(
		oakcommon_power_ceil_to_power_of_2(7, std::ptr::null_mut()),
		OAKCOMMON_E_INVALID
	);
}

/// Round `value` down to a power of two; a null out-param is `E_INVALID`.
#[test]
fn power_floor_to_power_of_2() {
	let mut out = 0u32;
	for (input, expected) in [
		(0u32, 0u32),
		(1, 1),
		(4, 4),
		(5, 4),
		(9, 8),
		(0x8000_0000, 0x8000_0000),
	] {
		assert_eq!(
			oakcommon_power_floor_to_power_of_2(input, &mut out),
			OAKCOMMON_OK
		);
		assert_eq!(out, expected, "floor({input})");
	}
	assert_eq!(
		oakcommon_power_floor_to_power_of_2(7, std::ptr::null_mut()),
		OAKCOMMON_E_INVALID
	);
}

// ---- current ----

/// The singleton handle is stamped; `free` nullifies it, is idempotent, and
/// tolerates a null pointer or an explicit null handle.
#[test]
fn current_instance_and_free_lifecycle() {
	let mut h = oakcommon_current_instance();
	assert!(!h.is_null());
	assert_eq!(h.abi_version, OAKCOMMON_ABI_VERSION);
	assert!(h.addref.is_some());
	assert!(h.release.is_some());

	oakcommon_current_free(&mut h);
	assert!(h.is_null());
	// A second free of the now-empty handle is safe.
	oakcommon_current_free(&mut h);
	assert!(h.is_null());
	// Freeing a null pointer is safe.
	oakcommon_current_free(std::ptr::null_mut());
	// Freeing an explicit null handle is safe.
	let mut null_h = CHandle::null();
	oakcommon_current_free(&mut null_h);
	assert!(null_h.is_null());
}

/// All four slots round-trip set -> get; null handles and null out-params
/// are `E_INVALID`, and clearing a slot makes it read back as null.
#[test]
fn current_set_get_all_slots_roundtrip() {
	let _guard = CURRENT_LOCK.lock().unwrap_or_else(|e| e.into_inner());
	let h = oakcommon_current_instance();

	let video = 0x1000usize as *mut c_void;
	let audio = 0x2000usize as *mut c_void;
	let host = 0x3000usize as *mut c_void;
	let cache = 0x4000usize as *mut c_void;

	// Empty slots read back as null before anything is stored.
	let mut got: *mut c_void = std::ptr::null_mut();
	assert_eq!(
		oakcommon_current_get_video_params(dup(&h), &mut got),
		OAKCOMMON_OK
	);
	assert!(got.is_null());

	assert_eq!(
		oakcommon_current_set_video_params(dup(&h), video, None),
		OAKCOMMON_OK
	);
	assert_eq!(
		oakcommon_current_set_audio_params(dup(&h), audio, None),
		OAKCOMMON_OK
	);
	assert_eq!(
		oakcommon_current_set_plugin_host(dup(&h), host, None),
		OAKCOMMON_OK
	);
	assert_eq!(
		oakcommon_current_set_plugin_cache(dup(&h), cache, None),
		OAKCOMMON_OK
	);

	assert_eq!(
		oakcommon_current_get_video_params(dup(&h), &mut got),
		OAKCOMMON_OK
	);
	assert_eq!(got, video);
	assert_eq!(
		oakcommon_current_get_audio_params(dup(&h), &mut got),
		OAKCOMMON_OK
	);
	assert_eq!(got, audio);
	assert_eq!(
		oakcommon_current_get_plugin_host(dup(&h), &mut got),
		OAKCOMMON_OK
	);
	assert_eq!(got, host);
	assert_eq!(
		oakcommon_current_get_plugin_cache(dup(&h), &mut got),
		OAKCOMMON_OK
	);
	assert_eq!(got, cache);

	// Getters reject a null out-param and a null handle.
	assert_eq!(
		oakcommon_current_get_video_params(dup(&h), std::ptr::null_mut()),
		OAKCOMMON_E_INVALID
	);
	assert_eq!(
		oakcommon_current_get_video_params(CHandle::null(), &mut got),
		OAKCOMMON_E_INVALID
	);
	// Setters reject a null handle.
	assert_eq!(
		oakcommon_current_set_video_params(CHandle::null(), video, None),
		OAKCOMMON_E_INVALID
	);

	// Clear every slot so other tests see a clean singleton.
	assert_eq!(
		oakcommon_current_set_video_params(dup(&h), std::ptr::null_mut(), None),
		OAKCOMMON_OK
	);
	assert_eq!(
		oakcommon_current_set_audio_params(dup(&h), std::ptr::null_mut(), None),
		OAKCOMMON_OK
	);
	assert_eq!(
		oakcommon_current_set_plugin_host(dup(&h), std::ptr::null_mut(), None),
		OAKCOMMON_OK
	);
	assert_eq!(
		oakcommon_current_set_plugin_cache(dup(&h), std::ptr::null_mut(), None),
		OAKCOMMON_OK
	);
	assert_eq!(
		oakcommon_current_get_video_params(dup(&h), &mut got),
		OAKCOMMON_OK
	);
	assert!(got.is_null());
}

/// Replacing an occupied slot runs the previous owner's destructor exactly
/// once; clearing a slot whose occupant has no destructor runs nothing.
#[test]
fn current_set_destroys_replaced_pointer() {
	let _guard = CURRENT_LOCK.lock().unwrap_or_else(|e| e.into_inner());
	let h = oakcommon_current_instance();
	let base = DESTROY_COUNT.load(Ordering::SeqCst);

	assert_eq!(
		oakcommon_current_set_video_params(
			dup(&h),
			0xAAAAusize as *mut c_void,
			Some(count_destroy)
		),
		OAKCOMMON_OK
	);
	assert_eq!(DESTROY_COUNT.load(Ordering::SeqCst), base);

	// Replacing the slot invokes the stored destructor exactly once.
	assert_eq!(
		oakcommon_current_set_video_params(dup(&h), 0xBBBBusize as *mut c_void, None),
		OAKCOMMON_OK
	);
	assert_eq!(DESTROY_COUNT.load(Ordering::SeqCst), base + 1);

	let mut got: *mut c_void = std::ptr::null_mut();
	assert_eq!(
		oakcommon_current_get_video_params(dup(&h), &mut got),
		OAKCOMMON_OK
	);
	assert_eq!(got, 0xBBBBusize as *mut c_void);

	// Clearing a slot with no destructor invokes nothing.
	assert_eq!(
		oakcommon_current_set_video_params(dup(&h), std::ptr::null_mut(), None),
		OAKCOMMON_OK
	);
	assert_eq!(DESTROY_COUNT.load(Ordering::SeqCst), base + 1);
	assert_eq!(
		oakcommon_current_get_video_params(dup(&h), &mut got),
		OAKCOMMON_OK
	);
	assert!(got.is_null());
}

/// `is_interactive` writes 1; null handle/out-param are `E_INVALID`.
#[test]
fn current_is_interactive_writes_one() {
	let _guard = CURRENT_LOCK.lock().unwrap_or_else(|e| e.into_inner());
	let h = oakcommon_current_instance();

	let mut out = 0i32;
	assert_eq!(
		oakcommon_current_is_interactive(dup(&h), &mut out),
		OAKCOMMON_OK
	);
	assert_eq!(out, 1);
	assert_eq!(
		oakcommon_current_is_interactive(dup(&h), std::ptr::null_mut()),
		OAKCOMMON_E_INVALID
	);
	assert_eq!(
		oakcommon_current_is_interactive(CHandle::null(), &mut out),
		OAKCOMMON_E_INVALID
	);
}
