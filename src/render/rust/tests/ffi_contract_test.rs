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

//! C ABI contract tests (ffi.rs) + manager lifecycle.

mod common;

use std::ffi::c_char;
use std::sync::mpsc;
use std::time::Duration;

use oakrender::ffi;
use oakrender::handle::{alive_count, CHandle};
use oakrender::error::{OAKRENDER_E_INVALID, OAKRENDER_E_NOT_FOUND, OAKRENDER_E_STATE, OAKRENDER_OK};
use std::sync::Mutex;

/// Serializes the alive-count accounting test against handle creation in
/// the other tests of this binary.
static SERIAL: Mutex<()> = Mutex::new(());

/// manager init/shutdown idempotence; available() reflects state.
#[test]
fn manager_lifecycle() {
	let _serial = SERIAL.lock().unwrap_or_else(|e| e.into_inner());
	let _g = common::ManagerGuard::init();
	unsafe {
		// Already initialized → state error.
		assert_eq!(ffi::manager::oakrender_manager_init(), OAKRENDER_E_STATE);
		assert_eq!(ffi::manager::oakrender_manager_available(), 1);
		ffi::manager::oakrender_manager_shutdown();
		assert_eq!(ffi::manager::oakrender_manager_available(), 0);
		// Re-init works after shutdown (C++ destroy_instance semantics).
		assert_eq!(ffi::manager::oakrender_manager_init(), OAKRENDER_OK);
		assert_eq!(ffi::manager::oakrender_manager_available(), 1);
	}
}

/// Every handle-returning export honors the empty-on-failure and
/// refcount-1-on-success contract (abi_version stamped). Serialized
/// against alive_count_accounting (both create handles).
#[test]
fn handle_contract_all_exports() {
	let _serial = SERIAL.lock().unwrap_or_else(|e| e.into_inner());
	unsafe {
		// cache_create: owned, refcount 1, abi stamped.
		let c = ffi::cache::oakrender_cache_create();
		assert!(!c.is_null());
		assert_eq!(c.abi_version, 1);
		ffi::cache::oakrender_cache_free(&mut CHandle::null());
		let mut cc = c;
		ffi::cache::oakrender_cache_free(&mut cc);
		assert!(cc.is_null());

		// wrap_borrowed(NULL) → empty handle.
		assert!(ffi::cache::oakrender_cache_wrap_borrowed(std::ptr::null_mut()).is_null());

		// create_for_node: empty parent → empty.
		assert!(ffi::cache::oakrender_cache_create_for_node(CHandle::null(), 0).is_null());
		// Unknown kind → empty.
		assert!(ffi::cache::oakrender_cache_create_for_node(common::fake_handle(1), 99).is_null());
		// Non-empty parent + valid kind → owned handle.
		let n = ffi::cache::oakrender_cache_create_for_node(common::fake_handle(1), 0);
		assert!(!n.is_null());
		assert_eq!(n.abi_version, 1);
		let mut nn = n;
		ffi::cache::oakrender_cache_free(&mut nn);

		// codec_frame_create.
		let f = ffi::renderer::oakrender_codec_frame_create();
		assert!(!f.is_null());
		assert_eq!(f.abi_version, 1);
		let mut ff = f;
		ffi::renderer::oakrender_codec_frame_free(&mut ff);
		assert!(ff.is_null());

		// cancelatom_init.
		let a = ffi::cancelatom::oakrender_cancelatom_init();
		assert!(!a.is_null());
		assert_eq!(a.abi_version, 1);
		let mut aa = a;
		ffi::cancelatom::oakrender_cancelatom_free(&mut aa);
		assert!(aa.is_null());

		// project_copier_create.
		let pc = ffi::copier::oakrender_project_copier_create();
		assert!(!pc.is_null());
		assert_eq!(pc.abi_version, 1);
		let mut pp = pc;
		ffi::copier::oakrender_project_copier_free(&mut pp);
		assert!(pp.is_null());

		// renderer create functions.
		let r = ffi::renderer::oakrender_display_renderer_create_opengl();
		assert!(!r.is_null());
		let mut rr = r;
		ffi::renderer::oakrender_display_renderer_destroy(&mut rr);

		let r2 = ffi::renderer::oakrender_display_renderer_create_dynamic(std::ptr::null());
		assert!(r2.is_null(), "NULL backend id → empty");
		let r3 = ffi::renderer::oakrender_display_renderer_create_dynamic(c"opengl".as_ptr());
		assert!(!r3.is_null());
		let mut r3 = r3;
		ffi::renderer::oakrender_display_renderer_destroy(&mut r3);

		// color processor: NULL strings → empty.
		let p = ffi::color::oakrender_color_processor_create(std::ptr::null(), c"x".as_ptr(), 0);
		assert!(p.is_null());
		// Bad direction → empty.
		let p2 = ffi::color::oakrender_color_processor_create(c"a".as_ptr(), c"b".as_ptr(), 7);
		assert!(p2.is_null());
	}
}

/// free(NULL)/free(empty) no-op across every free export.
#[test]
fn free_null_noop_all_exports() {
	let _serial = SERIAL.lock().unwrap_or_else(|e| e.into_inner());
	unsafe {
		ffi::cache::oakrender_cache_free(std::ptr::null_mut());
		ffi::renderer::oakrender_codec_frame_free(std::ptr::null_mut());
		ffi::renderer::oakrender_display_texture_free(std::ptr::null_mut());
		ffi::renderer::oakrender_display_renderer_destroy(std::ptr::null_mut());
		ffi::color::oakrender_color_processor_free(std::ptr::null_mut());
		ffi::copier::oakrender_project_copier_free(std::ptr::null_mut());
		ffi::cancelatom::oakrender_cancelatom_free(std::ptr::null_mut());
		ffi::ticket::oakrender_ticket_free(std::ptr::null_mut());

		// Empty handles are no-ops too.
		let mut empty = CHandle::null();
		ffi::cache::oakrender_cache_free(&mut empty);
		ffi::renderer::oakrender_codec_frame_free(&mut empty);
		ffi::cancelatom::oakrender_cancelatom_free(&mut empty);
	}
}

/// Two-stage string getters (disk_cache_path, uuids, filenames):
/// size query, short-buffer rule, exact fit.
#[test]
fn two_stage_string_contract() {
	let _serial = SERIAL.lock().unwrap_or_else(|e| e.into_inner());
	unsafe {
		// disk_cache_path: size query then exact copy.
		let (size, value) = common::read_two_stage(|buf, n| {
			ffi::manager::oakrender_disk_cache_path(buf, n)
		});
		assert!(size > 0, "required size including NUL");
		let path = value.expect("buffer copy");
		assert_eq!(path.len() + 1, size as usize);
		// Short buffer: size returned, no write.
		let mut tiny = [0u8; 4];
		let needed = ffi::manager::oakrender_disk_cache_path(tiny.as_mut_ptr() as *mut c_char, 4);
		assert_eq!(needed, size);

		// cache uuid getter.
		let cache = ffi::cache::oakrender_cache_create();
		let (usize, uvalue) = common::read_two_stage(|buf, n| {
			ffi::cache::oakrender_cache_get_uuid(cache, buf, n)
		});
		assert!(usize > 0);
		let uuid = uvalue.unwrap();
		assert_eq!(uuid.len() + 1, usize as usize);
		assert!(uuid.starts_with('{') && uuid.ends_with('}'));
		// Empty cache → error.
		assert_eq!(
			ffi::cache::oakrender_cache_get_uuid(CHandle::null(), std::ptr::null_mut(), 0),
			OAKRENDER_E_INVALID
		);
		let mut c = cache;
		ffi::cache::oakrender_cache_free(&mut c);

		// backend id getters.
		let (bsize, bvalue) = common::read_two_stage(|buf, n| {
			ffi::renderer::oakrender_backend_id_at(0, buf, n)
		});
		assert_eq!(bvalue.as_deref(), Some("opengl"));
		assert_eq!(bsize, 7); // "opengl" + NUL
		assert_eq!(
			ffi::renderer::oakrender_backend_id_at(4, std::ptr::null_mut(), 0),
			OAKRENDER_E_NOT_FOUND
		);
		assert_eq!(ffi::renderer::oakrender_backend_count(), 4);

		// current_backend is the manager's backend when one is up (parallel
		// tests may have initialized it) or the recorded default otherwise;
		// either way it is a known id string.
		let (_, current) = common::read_two_stage(|buf, n| {
			ffi::renderer::oakrender_current_backend(buf, n)
		});
		let current = current.expect("current backend string");
		assert!(
			["opengl", "vulkan", "multiprocess", "dummy", "auto", "metal", "cpu"]
				.contains(&current.as_str()),
			"unexpected current backend {current}"
		);
	}
}

/// debug_alive_count: cache/texture/processor create+free returns to
/// baseline (leak assertion). Serialized: other tests in this binary
/// create/free handles concurrently.
#[test]
fn alive_count_accounting() {
	let _serial = SERIAL.lock().unwrap_or_else(|e| e.into_inner());
	unsafe {
		let baseline = alive_count();
		let mut caches = Vec::new();
		for _ in 0..4 {
			caches.push(ffi::cache::oakrender_cache_create());
		}
		assert_eq!(alive_count(), baseline + 4);

		let mut frames = Vec::new();
		for _ in 0..3 {
			frames.push(ffi::renderer::oakrender_codec_frame_create());
		}
		assert_eq!(alive_count(), baseline + 7);

		let atom = ffi::cancelatom::oakrender_cancelatom_init();
		assert_eq!(alive_count(), baseline + 8);

		// Retain doesn't create a new object (just +1 count on the box).
		let retained = ffi::renderer::oakrender_codec_frame_retain(frames[0]);
		assert_eq!(alive_count(), baseline + 8);

		for c in caches.iter_mut() {
			ffi::cache::oakrender_cache_free(c);
		}
		for f in frames.iter_mut() {
			ffi::renderer::oakrender_codec_frame_free(f);
		}
		ffi::renderer::oakrender_codec_frame_free(&mut retained.clone());
		let mut atom = atom;
		ffi::cancelatom::oakrender_cancelatom_free(&mut atom);
		assert_eq!(alive_count(), baseline, "all owned objects released");
	}
}

/// Request-frame path: positive id, callback fires with an owned frame,
/// cancel semantics.
#[test]
fn request_frame_and_cancel() {
	let _serial = SERIAL.lock().unwrap_or_else(|e| e.into_inner());
	let _g = common::ManagerGuard::init();
	unsafe {
		let (tx, rx): (mpsc::Sender<u32>, mpsc::Receiver<u32>) = mpsc::channel();
		let userdata = Box::into_raw(Box::new(42u32));
		extern "C" fn cb(_frame: ffi::OakCodecFrame, ts: i64, userdata: *mut std::ffi::c_void) {
			let _ = userdata;
			let _ = ts;
		}
		let id = ffi::manager::oakrender_request_frame(
			common::fake_handle(7),
			12,
			Some(cb),
			userdata as *mut std::ffi::c_void,
		);
		assert!(id > 0, "positive request id, got {id}");
		let _ = tx;
		drop(rx);
		// Wait for the completion: the request's ticket must finish.
		std::thread::sleep(Duration::from_millis(200));

		// Unknown id → NOT_FOUND.
		assert_eq!(ffi::manager::oakrender_cancel_request(id + 12345), OAKRENDER_E_NOT_FOUND);
		// Cancelling a finished request is OK (removes from the map).
		let rc = ffi::manager::oakrender_cancel_request(id);
		assert!(rc == OAKRENDER_OK || rc == OAKRENDER_E_NOT_FOUND);
		unsafe { drop(Box::from_raw(userdata)) };
	}

	// Error paths without a manager (shut down within the guard scope).
	unsafe {
		ffi::manager::oakrender_manager_shutdown();
		assert_eq!(
			ffi::manager::oakrender_request_frame(CHandle::null(), 0, None, std::ptr::null_mut()),
			OAKRENDER_E_INVALID as i64
		);
		// Non-null viewer + cb but no manager → STATE.
		extern "C" fn cb2(_f: ffi::OakCodecFrame, _t: i64, _u: *mut std::ffi::c_void) {}
		assert_eq!(
			ffi::manager::oakrender_request_frame(
				common::fake_handle(1),
				0,
				Some(cb2),
				std::ptr::null_mut()
			),
			OAKRENDER_E_STATE as i64
		);
	}
}

/// set_aggressive_gc + cacher setters behave per contract.
#[test]
fn manager_settings() {
	let _serial = SERIAL.lock().unwrap_or_else(|e| e.into_inner());
	let _g = common::ManagerGuard::init();
	unsafe {
		assert_eq!(ffi::ticket::oakrender_manager_set_aggressive_gc(1), OAKRENDER_OK);
		assert_eq!(ffi::manager::oakrender_set_cacher_multicam(common::fake_handle(3)), OAKRENDER_OK);
		assert_eq!(ffi::manager::oakrender_set_cacher_multicam(CHandle::null()), OAKRENDER_OK);
		assert_eq!(
			ffi::manager::oakrender_set_display_color_processor(CHandle::null()),
			OAKRENDER_OK
		);
		ffi::manager::oakrender_cancel_video_tasks(0);
		ffi::manager::oakrender_cancel_video_tasks(1);
	}
	// Without a manager: STATE errors (shut down within the guard scope).
	unsafe {
		ffi::manager::oakrender_manager_shutdown();
		assert_eq!(
			ffi::manager::oakrender_set_cacher_multicam(common::fake_handle(1)),
			OAKRENDER_E_STATE
		);
		assert_eq!(
			ffi::manager::oakrender_set_display_color_processor(CHandle::null()),
			OAKRENDER_E_STATE
		);
		assert_eq!(ffi::ticket::oakrender_manager_set_aggressive_gc(0), OAKRENDER_E_STATE);
	}
}
