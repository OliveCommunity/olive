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

//! FFI integration tests for the `config` submodule of the C ABI layer
//! (`oakcommon::ffi::config`, backed by `include/common/config.h`).
//!
//! Every exported symbol is exercised with both a success and a failure
//! path. The store is a process-wide singleton and the C-ABI `config`
//! functions have no handle, so the failure paths are the documented
//! behaviors: null/empty keys, invalid buffers, and absent entries.
//!
//! Tests use per-test unique keys so they stay independent of each other
//! and of the domain unit tests, and every test that mutates the singleton
//! (or the `OAK_CONFIG_DIR` env override) serializes on a local mutex. The
//! crate's `test_support::env_lock` is `#[cfg(test)]` and therefore not
//! visible to this integration-test crate, and the domain unit tests run in
//! a separate process anyway, so a local lock is sufficient.

use std::ffi::{c_char, c_void, CStr, CString};
use std::path::{Path, PathBuf};
use std::ptr::{null, null_mut};
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::{Mutex, MutexGuard};

use oakcommon::error::{
	OAKCOMMON_E_FAILED, OAKCOMMON_E_INVALID, OAKCOMMON_E_NOT_FOUND, OAKCOMMON_OK,
};
use oakcommon::ffi::config::*;

// ---- Test infrastructure --------------------------------------------------

/// Serializes tests that mutate the process-wide singleton store or the
/// `OAK_CONFIG_DIR` env override. Without this, a `reset_defaults`/`load`
/// running between another test's `set` and `get` would clear its key.
static LOCK: Mutex<()> = Mutex::new(());

fn lock() -> MutexGuard<'static, ()> {
	LOCK.lock().unwrap()
}

static KEY_SEQ: AtomicUsize = AtomicUsize::new(0);

/// A per-test unique key, so parallel tests cannot collide on the shared
/// store.
fn unique_key(name: &str) -> CString {
	let n = KEY_SEQ.fetch_add(1, Ordering::Relaxed);
	CString::new(format!("itest_{}_{}_{}", name, std::process::id(), n)).unwrap()
}

fn cstr(s: &str) -> CString {
	CString::new(s).unwrap()
}

/// Cleans up the `OAK_CONFIG_DIR` override and its temp dir even when the
/// test body panics.
struct TempConfigDir(PathBuf);

impl Drop for TempConfigDir {
	fn drop(&mut self) {
		std::env::remove_var("OAK_CONFIG_DIR");
		let _ = std::fs::remove_dir_all(&self.0);
	}
}

/// Clears the process-global error handler on drop so a panicking test
/// cannot leak it to the rest of the suite.
struct HandlerGuard;

impl Drop for HandlerGuard {
	fn drop(&mut self) {
		oakcommon_config_set_error_handler(None, std::ptr::null_mut());
	}
}

/// Point `OAK_CONFIG_DIR` at an isolated temp dir, run `f`, then clean up.
/// Mirrors the `with_temp_config` pattern of the domain unit tests.
fn with_temp_config<T>(f: impl FnOnce(&Path) -> T) -> T {
	let _guard = lock();
	let dir =
		std::env::temp_dir().join(format!("oakcommon_ffi_config_test_{}", std::process::id()));
	let _ = std::fs::create_dir_all(&dir);
	std::env::set_var("OAK_CONFIG_DIR", &dir);
	let _cleanup = TempConfigDir(dir.clone());
	f(&dir)
}

// ---- Error handler recording ---------------------------------------------

static HANDLER_HITS: AtomicUsize = AtomicUsize::new(0);
static HANDLER_USERDATA: AtomicUsize = AtomicUsize::new(0);
static HANDLER_TITLE: Mutex<Option<String>> = Mutex::new(None);

unsafe extern "C" fn record_handler(
	title: *const c_char,
	_message: *const c_char,
	userdata: *mut c_void,
) {
	HANDLER_HITS.fetch_add(1, Ordering::SeqCst);
	HANDLER_USERDATA.store(userdata as usize, Ordering::SeqCst);
	if !title.is_null() {
		let s = unsafe { CStr::from_ptr(title) }
			.to_string_lossy()
			.into_owned();
		*HANDLER_TITLE.lock().unwrap() = Some(s);
	}
}

// ---- load / save ----------------------------------------------------------

/// `oakcommon_config_load` treats a missing `config.ini` as a non-error.
#[test]
fn config_load_missing_file_is_ok() {
	with_temp_config(|_| {
		assert_eq!(oakcommon_config_load(), OAKCOMMON_OK);
	});
}

/// `save` persists the store; `load` re-reads it (resetting to defaults
/// first), so the round trip restores the custom key.
#[test]
fn config_save_load_roundtrip() {
	with_temp_config(|_| {
		let key = unique_key("save_roundtrip");
		oakcommon_config_set(null(), key.as_ptr(), cstr("persisted").as_ptr());
		assert_eq!(oakcommon_config_save(), OAKCOMMON_OK);
		assert_eq!(oakcommon_config_load(), OAKCOMMON_OK);
		let mut buf = [0i8; 32];
		let n = oakcommon_config_get(null(), key.as_ptr(), buf.as_mut_ptr(), 32);
		assert_eq!(n, 10); // "persisted" + NUL
		assert_eq!(
			unsafe { CStr::from_ptr(buf.as_ptr()) }.to_str().unwrap(),
			"persisted"
		);
	});
}

/// A save that cannot write reports through the registered error handler
/// and returns `OAKCOMMON_E_FAILED`. `OAK_CONFIG_DIR` points at a regular
/// file so `"<dir>/config.ini.tmp"` cannot be created (mirrors the domain
/// unit test).
#[test]
fn config_save_failure_reports_and_returns_failed() {
	let _guard = lock();
	let dir = std::env::temp_dir().join(format!(
		"oakcommon_ffi_config_blocked_{}",
		std::process::id()
	));
	let _ = std::fs::create_dir_all(&dir);
	let blocker = dir.join("not_a_dir");
	std::fs::write(&blocker, b"x").unwrap();
	std::env::set_var("OAK_CONFIG_DIR", &blocker);
	let _cleanup = TempConfigDir(dir);

	HANDLER_HITS.store(0, Ordering::SeqCst);
	HANDLER_USERDATA.store(0, Ordering::SeqCst);
	*HANDLER_TITLE.lock().unwrap() = None;
	let userdata: usize = 0xdead_beef;
	let _handler_guard = HandlerGuard;
	assert_eq!(
		oakcommon_config_set_error_handler(Some(record_handler), userdata as *mut c_void),
		OAKCOMMON_OK
	);

	assert_eq!(oakcommon_config_save(), OAKCOMMON_E_FAILED);
	assert_eq!(HANDLER_HITS.load(Ordering::SeqCst), 1);
	assert_eq!(HANDLER_USERDATA.load(Ordering::SeqCst), userdata);
	assert_eq!(
		HANDLER_TITLE.lock().unwrap().as_deref(),
		Some("Error saving settings")
	);
}

// ---- reset_defaults -------------------------------------------------------

/// `reset_defaults` drops custom keys and restores the compiled-in
/// defaults.
#[test]
fn config_reset_defaults_restores_defaults() {
	let _guard = lock();
	let custom = unique_key("reset_custom");
	oakcommon_config_set_int(null(), custom.as_ptr(), 1);
	assert_eq!(oakcommon_config_get_int(null(), custom.as_ptr(), -1), 1);

	assert_eq!(oakcommon_config_reset_defaults(), OAKCOMMON_OK);

	// The custom key is gone...
	assert_eq!(oakcommon_config_get_int(null(), custom.as_ptr(), -1), -1);
	assert_eq!(
		oakcommon_config_entry_type(null(), custom.as_ptr()),
		OAKCOMMON_E_NOT_FOUND
	);
	// ...and the compiled-in defaults are back.
	assert_eq!(
		oakcommon_config_get_int(null(), cstr("DefaultSequenceWidth").as_ptr(), -1),
		1920
	);
	assert_eq!(
		oakcommon_config_get_bool(null(), cstr("UseProxyMedia").as_ptr(), -1),
		1
	);
}

// ---- string set / get -----------------------------------------------------

/// Null keys/values are silently ignored by `oakcommon_config_set`.
#[test]
fn config_set_null_key_is_noop() {
	let _guard = lock();
	oakcommon_config_set(null(), null(), cstr("v").as_ptr());
	oakcommon_config_set(null(), cstr("k").as_ptr(), null());
}

/// Two-stage getter, stage 1: a null buffer with size 0 returns the
/// required size (length + NUL) without writing.
#[test]
fn config_get_size_query_returns_required() {
	let _guard = lock();
	let key = unique_key("size_query");
	let value = "hello";
	oakcommon_config_set(null(), key.as_ptr(), cstr(value).as_ptr());
	let required = oakcommon_config_get(null(), key.as_ptr(), null_mut(), 0);
	assert_eq!(required, value.len() as i32 + 1);
}

/// Two-stage getter, short buffer: the full required size is returned but
/// the buffer is NOT touched (non-truncating, unlike `copy_setting`).
#[test]
fn config_get_short_buffer_is_non_truncating() {
	let _guard = lock();
	let key = unique_key("short_buf");
	let value = "hello world"; // required = 12
	oakcommon_config_set(null(), key.as_ptr(), cstr(value).as_ptr());
	let mut buf = [0x7a as c_char; 8];
	let n = oakcommon_config_get(null(), key.as_ptr(), buf.as_mut_ptr(), buf.len() as i32);
	assert_eq!(n, value.len() as i32 + 1);
	assert_eq!(buf, [0x7a as c_char; 8]); // sentinel untouched
}

/// Two-stage getter, exact fit: a buffer of exactly `len + 1` receives the
/// string plus NUL terminator.
#[test]
fn config_get_exact_fit_buffer() {
	let _guard = lock();
	let key = unique_key("exact_fit");
	let value = "hello";
	oakcommon_config_set(null(), key.as_ptr(), cstr(value).as_ptr());
	let required = value.len() as i32 + 1;
	let mut buf = vec![0i8; required as usize];
	let n = oakcommon_config_get(null(), key.as_ptr(), buf.as_mut_ptr(), required);
	assert_eq!(n, required);
	assert_eq!(
		unsafe { CStr::from_ptr(buf.as_ptr()) }.to_str().unwrap(),
		value
	);
}

/// A missing key yields `OAKCOMMON_E_NOT_FOUND`.
#[test]
fn config_get_missing_key_returns_not_found() {
	let _guard = lock();
	let key = unique_key("missing");
	let mut buf = [0i8; 16];
	let n = oakcommon_config_get(null(), key.as_ptr(), buf.as_mut_ptr(), buf.len() as i32);
	assert_eq!(n, OAKCOMMON_E_NOT_FOUND);
}

/// A null key yields `OAKCOMMON_E_INVALID`.
#[test]
fn config_get_null_key_returns_invalid() {
	let n = oakcommon_config_get(null(), null(), null_mut(), 0);
	assert_eq!(n, OAKCOMMON_E_INVALID);
}

/// An empty key is invalid (matches the C++ `is_valid_key` check, handled
/// here by the domain layer).
#[test]
fn config_get_empty_key_returns_invalid() {
	let mut buf = [0i8; 8];
	let n = oakcommon_config_get(null(), cstr("").as_ptr(), buf.as_mut_ptr(), 8);
	assert_eq!(n, OAKCOMMON_E_INVALID);
}

/// A negative buffer size is an invalid output buffer.
#[test]
fn config_get_invalid_buffer_returns_invalid() {
	let _guard = lock();
	let key = unique_key("bad_buf");
	oakcommon_config_set(null(), key.as_ptr(), cstr("v").as_ptr());
	let n = oakcommon_config_get(null(), key.as_ptr(), null_mut(), -1);
	assert_eq!(n, OAKCOMMON_E_INVALID);
}

// ---- int / int64 ----------------------------------------------------------

/// INT set/get round trip; the string getter formats the int.
#[test]
fn config_set_get_int_roundtrip() {
	let _guard = lock();
	let key = unique_key("int");
	oakcommon_config_set_int(null(), key.as_ptr(), 42);
	assert_eq!(oakcommon_config_get_int(null(), key.as_ptr(), -1), 42);
	assert_eq!(oakcommon_config_entry_type(null(), key.as_ptr()), 2);

	let mut buf = [0i8; 16];
	let n = oakcommon_config_get(null(), key.as_ptr(), buf.as_mut_ptr(), 16);
	assert_eq!(n, 3); // "42" + NUL
	assert_eq!(
		unsafe { CStr::from_ptr(buf.as_ptr()) }.to_str().unwrap(),
		"42"
	);
}

/// INT getter fallback paths: absent key, wrong type, null key.
#[test]
fn config_get_int_fallback_paths() {
	let _guard = lock();
	let missing = unique_key("int_missing");
	let wrong = unique_key("int_wrongtype");
	oakcommon_config_set(null(), wrong.as_ptr(), cstr("abc").as_ptr());
	assert_eq!(oakcommon_config_get_int(null(), missing.as_ptr(), 7), 7);
	assert_eq!(oakcommon_config_get_int(null(), wrong.as_ptr(), 7), 7);
	assert_eq!(oakcommon_config_get_int(null(), null(), 9), 9);
}

/// Setting a string onto an existing typed entry parses it into that type;
/// an unparseable string leaves the entry unchanged (CPP-PARITY with
/// `config.cpp`).
#[test]
fn config_set_string_parses_into_typed_entry() {
	let _guard = lock();
	let key = unique_key("typed_parse");
	oakcommon_config_set_int(null(), key.as_ptr(), 42);
	oakcommon_config_set(null(), key.as_ptr(), cstr("7").as_ptr());
	assert_eq!(oakcommon_config_get_int(null(), key.as_ptr(), -1), 7);
	oakcommon_config_set(null(), key.as_ptr(), cstr("notanumber").as_ptr());
	assert_eq!(oakcommon_config_get_int(null(), key.as_ptr(), -1), 7);
}

/// INT64 set/get round trip and fallback paths.
#[test]
fn config_set_get_int64_roundtrip() {
	let _guard = lock();
	let key = unique_key("int64");
	let big: i64 = 3_000_000_000; // exceeds i32 range
	oakcommon_config_set_int64(null(), key.as_ptr(), big);
	assert_eq!(oakcommon_config_get_int64(null(), key.as_ptr(), -1), big);

	let missing = unique_key("int64_missing");
	assert_eq!(
		oakcommon_config_get_int64(null(), missing.as_ptr(), -99),
		-99
	);
	assert_eq!(oakcommon_config_get_int64(null(), null(), -99), -99);
}

// ---- double ---------------------------------------------------------------

/// DOUBLE set/get round trip and fallback paths.
#[test]
fn config_set_get_double_roundtrip() {
	let _guard = lock();
	let key = unique_key("double");
	oakcommon_config_set_double(null(), key.as_ptr(), 3.5);
	assert_eq!(oakcommon_config_get_double(null(), key.as_ptr(), -1.0), 3.5);
	assert_eq!(oakcommon_config_entry_type(null(), key.as_ptr()), 3);

	let missing = unique_key("double_missing");
	let wrong = unique_key("double_wrongtype");
	oakcommon_config_set_int(null(), wrong.as_ptr(), 1);
	assert_eq!(
		oakcommon_config_get_double(null(), missing.as_ptr(), 2.5),
		2.5
	);
	assert_eq!(
		oakcommon_config_get_double(null(), wrong.as_ptr(), 2.5),
		2.5
	);
	assert_eq!(oakcommon_config_get_double(null(), null(), 2.5), 2.5);
}

// ---- bool -----------------------------------------------------------------

/// BOOL set/get round trip (0/1) and string formatting as "true"/"false".
#[test]
fn config_set_get_bool_roundtrip() {
	let _guard = lock();
	let key = unique_key("bool");
	oakcommon_config_set_bool(null(), key.as_ptr(), 1);
	assert_eq!(oakcommon_config_get_bool(null(), key.as_ptr(), -1), 1);
	assert_eq!(oakcommon_config_entry_type(null(), key.as_ptr()), 4);

	let mut buf = [0i8; 8];
	let n = oakcommon_config_get(null(), key.as_ptr(), buf.as_mut_ptr(), 8);
	assert_eq!(n, 5); // "true" + NUL
	assert_eq!(
		unsafe { CStr::from_ptr(buf.as_ptr()) }.to_str().unwrap(),
		"true"
	);

	oakcommon_config_set_bool(null(), key.as_ptr(), 0);
	assert_eq!(oakcommon_config_get_bool(null(), key.as_ptr(), -1), 0);
	let n = oakcommon_config_get(null(), key.as_ptr(), buf.as_mut_ptr(), 8);
	assert_eq!(n, 6); // "false" + NUL
	assert_eq!(
		unsafe { CStr::from_ptr(buf.as_ptr()) }.to_str().unwrap(),
		"false"
	);
}

/// BOOL getter fallback paths: absent key, wrong type, null key.
#[test]
fn config_get_bool_fallback_paths() {
	let _guard = lock();
	let missing = unique_key("bool_missing");
	let wrong = unique_key("bool_wrongtype");
	oakcommon_config_set_int(null(), wrong.as_ptr(), 1);
	assert_eq!(oakcommon_config_get_bool(null(), missing.as_ptr(), 1), 1);
	assert_eq!(oakcommon_config_get_bool(null(), wrong.as_ptr(), 0), 0);
	assert_eq!(oakcommon_config_get_bool(null(), null(), 1), 1);
}

/// Null keys are silently ignored by every typed setter.
#[test]
fn config_typed_setters_null_key_are_noop() {
	let _guard = lock();
	oakcommon_config_set_int(null(), null(), 1);
	oakcommon_config_set_int64(null(), null(), 1);
	oakcommon_config_set_bool(null(), null(), 1);
	oakcommon_config_set_double(null(), null(), 1.0);
}

// ---- groups ---------------------------------------------------------------

/// A non-empty group prefixes the stored key (`group/key`), so the flat
/// lookup must not find it.
#[test]
fn config_group_prefixes_keys() {
	let _guard = lock();
	let key = unique_key("grouped");
	let group = cstr("ffi_test_group");
	oakcommon_config_set_int(group.as_ptr(), key.as_ptr(), 5);
	assert_eq!(oakcommon_config_get_int(null(), key.as_ptr(), -1), -1);
	assert_eq!(
		oakcommon_config_get_int(group.as_ptr(), key.as_ptr(), -1),
		5
	);
	assert_eq!(oakcommon_config_entry_type(group.as_ptr(), key.as_ptr()), 2);
}

// ---- entry_type -----------------------------------------------------------

/// Entry type codes: 1 = String, 2 = Int, 3 = Double, 4 = Bool.
#[test]
fn config_entry_type_codes() {
	let _guard = lock();
	let s = unique_key("et_str");
	let i = unique_key("et_int");
	let d = unique_key("et_double");
	let b = unique_key("et_bool");
	oakcommon_config_set(null(), s.as_ptr(), cstr("x").as_ptr());
	oakcommon_config_set_int(null(), i.as_ptr(), 1);
	oakcommon_config_set_double(null(), d.as_ptr(), 1.0);
	oakcommon_config_set_bool(null(), b.as_ptr(), 1);
	assert_eq!(oakcommon_config_entry_type(null(), s.as_ptr()), 1);
	assert_eq!(oakcommon_config_entry_type(null(), i.as_ptr()), 2);
	assert_eq!(oakcommon_config_entry_type(null(), d.as_ptr()), 3);
	assert_eq!(oakcommon_config_entry_type(null(), b.as_ptr()), 4);
}

/// Missing, null, and empty keys for `entry_type`.
#[test]
fn config_entry_type_missing_and_invalid() {
	let _guard = lock();
	let missing = unique_key("et_missing");
	assert_eq!(
		oakcommon_config_entry_type(null(), missing.as_ptr()),
		OAKCOMMON_E_NOT_FOUND
	);
	assert_eq!(
		oakcommon_config_entry_type(null(), null()),
		OAKCOMMON_E_INVALID
	);
	assert_eq!(
		oakcommon_config_entry_type(null(), cstr("").as_ptr()),
		OAKCOMMON_E_INVALID
	);
}

// ---- error handler --------------------------------------------------------

/// Registering and clearing the error handler both succeed; the handler
/// itself is exercised by `config_save_failure_reports_and_returns_failed`.
#[test]
fn config_set_error_handler_roundtrip() {
	let _guard = lock();
	let userdata = 0x1234 as *mut c_void;
	let _handler_guard = HandlerGuard;
	assert_eq!(
		oakcommon_config_set_error_handler(Some(record_handler), userdata),
		OAKCOMMON_OK
	);
	assert_eq!(
		oakcommon_config_set_error_handler(None, std::ptr::null_mut()),
		OAKCOMMON_OK
	);
}
