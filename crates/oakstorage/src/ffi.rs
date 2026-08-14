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

//! C ABI export layer: implements M10 §2.2/§2.3 verbatim
//! (`oakstorage_probe/open/save/project_free/project_take_project/
//! project_project/project_uri/last_error/debug_alive_count` and the
//! backend vtable registration pair).
//!
//! `oakstorage_open` returns a session handle (alive-counted). The
//! project inside is an `OakNodeProject*`-compatible handle boxed via
//! the shared `oakcore_rs::handle::CHandle`, so `take_project` hands
//! back something `oaknode_*` functions accept, and `save` accepts
//! handles produced by `oaknode_project_init`.

use std::cell::RefCell;
use std::ffi::{c_char, c_int, c_uint, CStr, CString};
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::atomic::{AtomicI32, Ordering};
use std::sync::Arc;

use crate::backend::StorageBackend;
use crate::bridge::node;
use crate::error::{Error, OAKSTORAGE_E_FAILED, OAKSTORAGE_OK, Result};
use crate::handle::CHandle;
use crate::session::Session;
use crate::uri::StorageUri;

/// Save options bitmask: compress the ove-xml container. Accepted but
/// not implemented by the built-in backends (the oaknode serializer
/// emits plain XML only); unknown bits are ignored (M10 §2.2).
pub const OAKSTORAGE_SAVE_COMPRESS: c_uint = 0x1;

/// Live session count (`oakstorage_debug_alive_count`).
static ALIVE: AtomicI32 = AtomicI32::new(0);

// Per-thread last-error detail (`oakstorage_last_error`).
thread_local! {
	static LAST_ERROR: RefCell<String> = const { RefCell::new(String::new()) };
}

/// Record the thread's last error detail.
fn set_last_error(msg: &str) {
	LAST_ERROR.with(|e| *e.borrow_mut() = msg.to_string());
}

/// Read the thread's last error detail.
fn take_last_error() -> String {
	LAST_ERROR.with(|e| e.borrow().clone())
}

/// Release for session handles (alive-counted; the session's Drop
/// releases the held project handle).
unsafe extern "C" fn session_release(ctx: *mut std::ffi::c_void) {
	unsafe {
		let rb = ctx as *mut crate::handle::RefBox<Session>;
		if (*rb).refs.fetch_sub(1, Ordering::AcqRel) == 1 {
			drop(Box::from_raw(rb));
			ALIVE.fetch_sub(1, Ordering::Relaxed);
		}
	}
}

/// `oakstorage_probe`: which backend claims this URI (two-stage
/// string; negative = E_NO_BACKEND).
#[no_mangle]
pub unsafe extern "C" fn oakstorage_probe(
	uri: *const c_char,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	let uri_str = unsafe { cstr(uri) }.map(str::to_string);
	match catch_unwind(AssertUnwindSafe(|| {
		let s = uri_str.as_deref().ok_or(Error::Invalid)?;
		let parsed = StorageUri::parse(s)?;
		let backend = crate::registry::Registry::global().resolve(&parsed)?;
		Ok::<String, Error>(backend.name().to_string())
	})) {
		Ok(Ok(name)) => unsafe { node::copy_string_out(&name, buf, buf_size) },
		Ok(Err(e)) => {
			set_last_error(&e.to_string());
			e.code()
		}
		Err(_) => {
			set_last_error("storage: panic in oakstorage_probe");
			OAKSTORAGE_E_FAILED
		}
	}
}

/// `oakstorage_open`: open a project session by URI (owned handle;
/// `result_code` receives an OAKSTORAGE_* code incl. the positive
/// version info codes).
#[no_mangle]
pub unsafe extern "C" fn oakstorage_open(uri: *const c_char, result_code: *mut c_int) -> CHandle {
	let uri_str = unsafe { cstr(uri) }.map(str::to_string);
	match catch_unwind(AssertUnwindSafe(|| open_inner(uri_str.as_deref()))) {
		Ok(Ok((handle, info))) => {
			if !result_code.is_null() {
				unsafe { *result_code = info };
			}
			handle
		}
		Ok(Err(e)) => {
			set_last_error(&e.to_string());
			if !result_code.is_null() {
				unsafe { *result_code = e.code() };
			}
			CHandle::null()
		}
		Err(_) => {
			set_last_error("storage: panic in oakstorage_open");
			if !result_code.is_null() {
				unsafe { *result_code = OAKSTORAGE_E_FAILED };
			}
			CHandle::null()
		}
	}
}

/// Open a session: parse + resolve + backend load, then wrap the
/// project in an alive-counted session handle. The returned info code
/// is the backend's version verdict (OK or a positive info code).
fn open_inner(uri: Option<&str>) -> Result<(CHandle, i32)> {
	let s = uri.ok_or(Error::Invalid)?;
	let parsed = StorageUri::parse(s)?;
	let backend = crate::registry::Registry::global().resolve(&parsed)?;
	let loaded = backend.load(&parsed)?;
	let info = loaded.version_info;
	if loaded.project.is_null() {
		// Version probing declined to load (TOO_NEW / UNKNOWN_VERSION).
		return Ok((CHandle::null(), info));
	}
	ALIVE.fetch_add(1, Ordering::Relaxed);
	let session = Session::new(parsed, loaded.project);
	Ok((crate::handle::make_owned_with(session, session_release), info))
}

/// `oakstorage_save`: save a project to a URI (options bitmask;
/// unknown bits ignored by backends).
#[no_mangle]
pub unsafe extern "C" fn oakstorage_save(
	project: CHandle,
	uri: *const c_char,
	options: c_uint,
) -> c_int {
	let uri_str = unsafe { cstr(uri) }.map(str::to_string);
	match catch_unwind(AssertUnwindSafe(|| {
		let s = uri_str.as_deref().ok_or(Error::Invalid)?;
		let parsed = StorageUri::parse(s)?;
		let backend = crate::registry::Registry::global().resolve(&parsed)?;
		backend.save(project, &parsed, options)
	})) {
		Ok(Ok(())) => OAKSTORAGE_OK,
		Ok(Err(e)) => {
			set_last_error(&e.to_string());
			e.code()
		}
		Err(_) => {
			set_last_error("storage: panic in oakstorage_save");
			OAKSTORAGE_E_FAILED
		}
	}
}

/// `oakstorage_project_free` (NULL/empty no-op).
#[no_mangle]
pub unsafe extern "C" fn oakstorage_project_free(session: *mut CHandle) {
	crate::handle::guard_void(|| unsafe {
		if session.is_null() || (*session).ctx.is_null() {
			return;
		}
		let h = (*session).clone();
		if let Some(f) = h.release {
			f(h.ctx);
		}
		(*session).ctx = std::ptr::null_mut();
	});
}

/// `oakstorage_project_take_project`: ownership transfer out of the
/// session (the session becomes an empty shell; still free it).
#[no_mangle]
pub unsafe extern "C" fn oakstorage_project_take_project(session: CHandle) -> CHandle {
	match catch_unwind(AssertUnwindSafe(|| unsafe { session_take(session) })) {
		Ok(Ok(h)) => h,
		Ok(Err(_)) | Err(_) => CHandle::null(),
	}
}

/// Mutate the boxed session to take its project out (empty after an
/// earlier take — then an empty handle, not an error).
unsafe fn session_take(session: CHandle) -> Result<CHandle> {
	let rb = session.ctx as *mut crate::handle::RefBox<Session>;
	if rb.is_null() {
		return Err(Error::Invalid);
	}
	Ok(unsafe { (*rb).value.take() }.unwrap_or_else(CHandle::null))
}

/// `oakstorage_project_project`: borrowed project handle (empty after
/// take). A fresh box over a clone of the project Arc, so releasing it
/// (or not) cannot disturb the session's own handle.
#[no_mangle]
pub unsafe extern "C" fn oakstorage_project_project(session: CHandle) -> CHandle {
	match catch_unwind(AssertUnwindSafe(|| -> crate::error::Result<CHandle> {
		unsafe {
			let sess = crate::handle::get::<Session>(&session).ok_or(Error::Invalid)?;
			let ph = sess.project().ok_or(Error::State)?;
			let arc = oaknode::handle::get::<node::ProjectArc>(ph)
				.ok_or(Error::Invalid)?
				.clone();
			Ok(oaknode::handle::make_owned(arc))
		}
	})) {
		Ok(Ok(h)) => h,
		Ok(Err(_)) | Err(_) => CHandle::null(),
	}
}

/// `oakstorage_project_uri` (two-stage string).
#[no_mangle]
pub unsafe extern "C" fn oakstorage_project_uri(
	session: CHandle,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	let uri_str = match unsafe { crate::handle::get::<Session>(&session) } {
		Some(s) => s.uri().to_uri_string(),
		None => return crate::error::OAKSTORAGE_E_INVALID,
	};
	unsafe { node::copy_string_out(&uri_str, buf, buf_size) }
}

/// `oakstorage_last_error` (two-stage string; per-thread last error
/// detail).
#[no_mangle]
pub unsafe extern "C" fn oakstorage_last_error(buf: *mut c_char, buf_size: c_int) -> c_int {
	let msg = take_last_error();
	unsafe { node::copy_string_out(&msg, buf, buf_size) }
}

/// `oakstorage_debug_alive_count` (leak assertions in tests).
#[no_mangle]
pub unsafe extern "C" fn oakstorage_debug_alive_count() -> c_int {
	ALIVE.load(Ordering::Relaxed)
}

// ---------------------------------------------------------------------
// Foreign (C-side) backend vtable registration — M10 §2.3
// ---------------------------------------------------------------------

/// C-side backend vtable (M10 §2.3; the layout is the C header's
/// struct — implementer copies it field-for-field).
#[repr(C)]
pub struct OakStorageBackendVtable {
	/// Backend name (static string).
	pub name: *const c_char,
	/// URI scheme served (static string).
	pub uri_scheme: *const c_char,
	/// can_handle.
	pub can_handle: Option<unsafe extern "C" fn(uri: *const c_char) -> c_int>,
	/// load.
	pub load: Option<
		unsafe extern "C" fn(
			uri: *const c_char,
			result_code: *mut c_int,
			err_buf: *mut c_char,
			err_buf_size: c_int,
		) -> CHandle,
	>,
	/// save.
	pub save: Option<
		unsafe extern "C" fn(
			project: CHandle,
			uri: *const c_char,
			options: c_uint,
			err_buf: *mut c_char,
			err_buf_size: c_int,
		) -> c_int,
	>,
}

/// A backend registered from C: every call funnels through the vtable
/// function pointers (the vtable itself is not copied — the caller
/// guarantees it outlives unregistration).
struct ForeignBackend {
	name: String,
	uri_scheme: String,
	can_handle: unsafe extern "C" fn(uri: *const c_char) -> c_int,
	load: unsafe extern "C" fn(
		uri: *const c_char,
		result_code: *mut c_int,
		err_buf: *mut c_char,
		err_buf_size: c_int,
	) -> CHandle,
	save: unsafe extern "C" fn(
		project: CHandle,
		uri: *const c_char,
		options: c_uint,
		err_buf: *mut c_char,
		err_buf_size: c_int,
	) -> c_int,
}

impl StorageBackend for ForeignBackend {
	fn name(&self) -> &str {
		&self.name
	}

	fn uri_scheme(&self) -> &str {
		&self.uri_scheme
	}

	fn can_handle(&self, uri: &StorageUri) -> bool {
		let c = match CString::new(uri.to_uri_string()) {
			Ok(c) => c,
			Err(_) => return false,
		};
		let claimed = unsafe { (self.can_handle)(c.as_ptr()) };
		claimed != 0
	}

	fn load(&self, uri: &StorageUri) -> Result<crate::backend::LoadResult> {
		let c = CString::new(uri.to_uri_string()).map_err(|_| Error::Invalid)?;
		let mut result_code: c_int = OAKSTORAGE_OK;
		let handle = unsafe {
			(self.load)(c.as_ptr(), &mut result_code, std::ptr::null_mut(), 0)
		};
		if !handle.is_null() {
			Ok(crate::backend::LoadResult::with_info(handle, result_code))
		} else if result_code > 0 {
			// Version info code without a project.
			Ok(crate::backend::LoadResult::info_only(result_code))
		} else {
			Err(code_to_error(result_code))
		}
	}

	fn save(
		&self,
		project: CHandle,
		uri: &StorageUri,
		options: u32,
	) -> Result<()> {
		let c = CString::new(uri.to_uri_string()).map_err(|_| Error::Invalid)?;
		let rc = unsafe { (self.save)(project, c.as_ptr(), options, std::ptr::null_mut(), 0) };
		if rc == OAKSTORAGE_OK {
			Ok(())
		} else {
			Err(code_to_error(rc))
		}
	}
}

/// Map a foreign backend's return code to the crate error.
fn code_to_error(code: c_int) -> Error {
	match code {
		crate::error::OAKSTORAGE_E_INVALID => Error::Invalid,
		crate::error::OAKSTORAGE_E_STATE => Error::State,
		crate::error::OAKSTORAGE_E_NOT_FOUND => Error::NotFound,
		crate::error::OAKSTORAGE_E_FAILED => Error::Failed("foreign backend failed".to_string()),
		crate::error::OAKSTORAGE_E_NO_BACKEND => Error::NoBackend,
		crate::error::OAKSTORAGE_E_FORMAT => Error::Format("foreign backend format error".to_string()),
		crate::error::OAKSTORAGE_E_IO => Error::Io("foreign backend I/O error".to_string()),
		crate::error::OAKSTORAGE_E_NOMEM => Error::NoMem,
		_ => Error::Failed(format!("foreign backend error code {code}")),
	}
}

/// `oakstorage_backend_register`: register a foreign backend (the
/// vtable is not copied — the caller guarantees it outlives
/// unregistration).
#[no_mangle]
pub unsafe extern "C" fn oakstorage_backend_register(
	backend: *const OakStorageBackendVtable,
) -> c_int {
	crate::handle::guard(|| unsafe {
		if backend.is_null() {
			return Err(Error::Invalid);
		}
		let v = &*backend;
		let name = cstr(v.name).ok_or(Error::Invalid)?.to_string();
		let uri_scheme = cstr(v.uri_scheme).ok_or(Error::Invalid)?.to_string();
		let can_handle = v.can_handle.ok_or(Error::Invalid)?;
		let load = v.load.ok_or(Error::Invalid)?;
		let save = v.save.ok_or(Error::Invalid)?;
		let foreign = ForeignBackend {
			name,
			uri_scheme,
			can_handle,
			load,
			save,
		};
		crate::registry::Registry::global().register(Arc::new(foreign))
	})
}

/// `oakstorage_backend_unregister`.
#[no_mangle]
pub unsafe extern "C" fn oakstorage_backend_unregister(name: *const c_char) -> c_int {
	crate::handle::guard(|| unsafe {
		let name = cstr(name).ok_or(Error::Invalid)?;
		crate::registry::Registry::global().unregister(name)
	})
}

/// Safe read of a NUL-terminated C string argument.
///
/// # Safety
/// `p` must be a valid NUL-terminated C string for the returned
/// reference's lifetime.
unsafe fn cstr<'a>(p: *const c_char) -> Option<&'a str> {
	if p.is_null() {
		return None;
	}
	unsafe { CStr::from_ptr(p) }.to_str().ok()
}
