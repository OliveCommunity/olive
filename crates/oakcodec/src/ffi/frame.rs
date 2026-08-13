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

//! `include/codec/frame.h` exports.
//!
//! Complete inventory: frame_init / init_with_params / free / get_params /
//! set_params / allocate / is_allocated / data / const_data /
//! allocated_size / linesize_bytes / linesize_pixels / width / height /
//! format / channel_count / get_timestamp / set_timestamp /
//! debug_alive_count.
//!
//! # CPP-PARITY
//! The C++ `c_api/frame.cpp` boxes every `OakFrame` handle with an
//! `olive::FramePtr` (a shared pointer), so decoder-produced frames may
//! alias the decoder's internal cache. The Rust equivalent boxes
//! `Mutex<Frame>`; a decode that hands out a still-shared `Arc<Frame>`
//! therefore cannot be aliased here and reports an empty handle instead
//! (see `ffi::decoder`).

use std::ffi::{c_int, c_void};
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::Mutex;

use oakcore_rs::Rational;

use crate::bridge::common::OakVideoParams;
use crate::frame::Frame;
use crate::handle::{self, CHandle};

/// `OAKCOMMON_PIXEL_FORMAT_INVALID` (oakcommon `common/videoparams.h`).
const OAKCOMMON_PIXEL_FORMAT_INVALID: c_int = -1;

/// `oakcodec_frame_init`: new frame with default (invalid) params,
/// refcount 1.
#[no_mangle]
pub unsafe extern "C" fn oakcodec_frame_init() -> CHandle {
	handle::guard_handle(|| Ok(handle::make_owned(Mutex::new(Frame::new()))))
}

/// `oakcodec_frame_init_with_params`: new frame holding a copy of `params`
/// (the handle is addref'd internally); buffer unallocated.
#[no_mangle]
pub unsafe extern "C" fn oakcodec_frame_init_with_params(params: OakVideoParams) -> CHandle {
	handle::guard_handle(|| Ok(handle::make_owned(Mutex::new(Frame::with_params(params)))))
}

/// `oakcodec_frame_free`: NULL/empty no-op; nulls `ctx` afterwards.
#[no_mangle]
pub unsafe extern "C" fn oakcodec_frame_free(frame: *mut CHandle) {
	handle::guard_void(|| super::free_handle(frame));
}

/// `oakcodec_frame_get_params`: copy of the frame's parameter set.
///
/// The copy is addref'd: the caller must release it with
/// `oakcommon_videoparams_free` (see the header contract). Test-stub
/// handles carry no `addref`, so the caller must not free them.
#[no_mangle]
pub unsafe extern "C" fn oakcodec_frame_get_params(
	frame: CHandle,
	out: *mut OakVideoParams,
) -> c_int {
	handle::guard(|| {
		if out.is_null() {
			return Err(crate::error::Error::Invalid);
		}
		let f = super::get_box::<Mutex<Frame>>(&frame).ok_or(crate::error::Error::Invalid)?;
		let f = f.lock().unwrap();
		let p = f.params().cloned().ok_or(crate::error::Error::Invalid)?;
		crate::frame::params_addref(&p);
		// SAFETY: the caller guarantees `out` points to a writable
		// `OakVideoParams`.
		unsafe { *out = p };
		Ok(())
	})
}

/// `oakcodec_frame_set_params`: replace the parameter set (addref'd
/// internally); recomputes line sizes, does not reallocate.
#[no_mangle]
pub unsafe extern "C" fn oakcodec_frame_set_params(
	frame: CHandle,
	params: OakVideoParams,
) -> c_int {
	handle::guard(|| {
		let f = super::get_box::<Mutex<Frame>>(&frame).ok_or(crate::error::Error::Invalid)?;
		f.lock().unwrap().set_params(params);
		Ok(())
	})
}

/// `oakcodec_frame_allocate`: allocate the pixel buffer from the current
/// params; `OAKCODEC_E_STATE` when the params are invalid.
#[no_mangle]
pub unsafe extern "C" fn oakcodec_frame_allocate(frame: CHandle) -> c_int {
	handle::guard(|| {
		let f = super::get_box::<Mutex<Frame>>(&frame).ok_or(crate::error::Error::Invalid)?;
		f.lock().unwrap().allocate()
	})
}

/// `oakcodec_frame_is_allocated`: 1 when the buffer is allocated.
#[no_mangle]
pub unsafe extern "C" fn oakcodec_frame_is_allocated(frame: CHandle) -> c_int {
	handle::guard_raw(|| {
		let f = match super::get_box::<Mutex<Frame>>(&frame) {
			Some(f) => f,
			None => return 0,
		};
		let f = f.lock().unwrap();
		if f.is_allocated() {
			1
		} else {
			0
		}
	})
}

/// `oakcodec_frame_data`: writable pixel buffer, NULL when
/// unallocated/empty.
#[no_mangle]
pub unsafe extern "C" fn oakcodec_frame_data(frame: CHandle) -> *mut c_void {
	match catch_unwind(AssertUnwindSafe(|| unsafe { frame_data_inner(&frame) })) {
		Ok(p) => p,
		Err(_) => std::ptr::null_mut(),
	}
}

/// `oakcodec_frame_const_data`: const variant of `oakcodec_frame_data`.
#[no_mangle]
pub unsafe extern "C" fn oakcodec_frame_const_data(frame: CHandle) -> *const c_void {
	match catch_unwind(AssertUnwindSafe(|| unsafe {
		frame_const_data_inner(&frame)
	})) {
		Ok(p) => p,
		Err(_) => std::ptr::null_mut(),
	}
}

unsafe fn frame_data_inner(frame: &CHandle) -> *mut c_void {
	let f = match super::get_box::<Mutex<Frame>>(frame) {
		Some(f) => f,
		None => return std::ptr::null_mut(),
	};
	match f.lock().unwrap().data_mut() {
		Some(d) => d.as_mut_ptr() as *mut c_void,
		None => std::ptr::null_mut(),
	}
}

unsafe fn frame_const_data_inner(frame: &CHandle) -> *const c_void {
	let f = match super::get_box::<Mutex<Frame>>(frame) {
		Some(f) => f,
		None => return std::ptr::null(),
	};
	match f.lock().unwrap().data() {
		Some(d) => d.as_ptr() as *const c_void,
		None => std::ptr::null(),
	}
}

/// `oakcodec_frame_allocated_size`: size of the pixel buffer in bytes
/// (0 when unallocated).
#[no_mangle]
pub unsafe extern "C" fn oakcodec_frame_allocated_size(frame: CHandle) -> c_int {
	handle::guard_raw(|| {
		let f = match super::get_box::<Mutex<Frame>>(&frame) {
			Some(f) => f,
			None => return 0,
		};
		f.lock().unwrap().allocated_size() as c_int
	})
}

/// `oakcodec_frame_linesize_bytes`: distance between two rows in bytes
/// (0 when params are unset).
#[no_mangle]
pub unsafe extern "C" fn oakcodec_frame_linesize_bytes(frame: CHandle) -> c_int {
	handle::guard_raw(|| {
		let f = match super::get_box::<Mutex<Frame>>(&frame) {
			Some(f) => f,
			None => return 0,
		};
		f.lock().unwrap().linesize_bytes()
	})
}

/// `oakcodec_frame_linesize_pixels`: distance between two rows in pixels.
#[no_mangle]
pub unsafe extern "C" fn oakcodec_frame_linesize_pixels(frame: CHandle) -> c_int {
	handle::guard_raw(|| {
		let f = match super::get_box::<Mutex<Frame>>(&frame) {
			Some(f) => f,
			None => return 0,
		};
		f.lock().unwrap().linesize_pixels()
	})
}

/// `oakcodec_frame_width`: frame width (0 when params are empty).
#[no_mangle]
pub unsafe extern "C" fn oakcodec_frame_width(frame: CHandle) -> c_int {
	handle::guard_raw(|| {
		let f = match super::get_box::<Mutex<Frame>>(&frame) {
			Some(f) => f,
			None => return 0,
		};
		f.lock().unwrap().width()
	})
}

/// `oakcodec_frame_height`: frame height (0 when params are empty).
#[no_mangle]
pub unsafe extern "C" fn oakcodec_frame_height(frame: CHandle) -> c_int {
	handle::guard_raw(|| {
		let f = match super::get_box::<Mutex<Frame>>(&frame) {
			Some(f) => f,
			None => return 0,
		};
		f.lock().unwrap().height()
	})
}

/// `oakcodec_frame_format`: pixel format as an `OakPixelFormat` value;
/// `OAKCOMMON_PIXEL_FORMAT_INVALID` on an empty handle.
#[no_mangle]
pub unsafe extern "C" fn oakcodec_frame_format(frame: CHandle) -> c_int {
	handle::guard_raw(|| {
		let f = match super::get_box::<Mutex<Frame>>(&frame) {
			Some(f) => f,
			None => return OAKCOMMON_PIXEL_FORMAT_INVALID,
		};
		f.lock().unwrap().format() as c_int
	})
}

/// `oakcodec_frame_channel_count`: plane channel count of the params
/// format (0 on an empty handle).
#[no_mangle]
pub unsafe extern "C" fn oakcodec_frame_channel_count(frame: CHandle) -> c_int {
	handle::guard_raw(|| {
		let f = match super::get_box::<Mutex<Frame>>(&frame) {
			Some(f) => f,
			None => return 0,
		};
		f.lock().unwrap().channel_count()
	})
}

/// `oakcodec_frame_get_timestamp`: frame timestamp as a rational number
/// of seconds, written through `numerator`/`denominator`.
#[no_mangle]
pub unsafe extern "C" fn oakcodec_frame_get_timestamp(
	frame: CHandle,
	numerator: *mut c_int,
	denominator: *mut c_int,
) -> c_int {
	handle::guard(|| {
		if numerator.is_null() || denominator.is_null() {
			return Err(crate::error::Error::Invalid);
		}
		let f = super::get_box::<Mutex<Frame>>(&frame).ok_or(crate::error::Error::Invalid)?;
		let f = f.lock().unwrap();
		let ts = f.timestamp();
		// SAFETY: both pointers were range-checked above.
		unsafe {
			*numerator = ts.numerator() as c_int;
			*denominator = ts.denominator() as c_int;
		}
		Ok(())
	})
}

/// `oakcodec_frame_set_timestamp`: replace the frame timestamp.
#[no_mangle]
pub unsafe extern "C" fn oakcodec_frame_set_timestamp(
	frame: CHandle,
	numerator: c_int,
	denominator: c_int,
) -> c_int {
	handle::guard(|| {
		let f = super::get_box::<Mutex<Frame>>(&frame).ok_or(crate::error::Error::Invalid)?;
		f.lock()
			.unwrap()
			.set_timestamp(Rational::new(numerator as i64, denominator as i64));
		Ok(())
	})
}

/// `oakcodec_debug_alive_count`: number of live boxed handle objects
/// across all families (see `crate::handle::alive_count`).
#[no_mangle]
pub unsafe extern "C" fn oakcodec_debug_alive_count() -> c_int {
	handle::guard_raw(handle::alive_count)
}

#[cfg(test)]
mod tests {
	use super::*;
	use crate::bridge::common::{
		oakcommon_videoparams_get_height, oakcommon_videoparams_get_width,
		oakcommon_videoparams_init_basic,
	};
	use crate::error::OAKCODEC_E_INVALID;

	#[test]
	fn frame_lifecycle_golden() {
		let _g = crate::ffi::lock_tests();
		let params = unsafe { oakcommon_videoparams_init_basic(100, 50, 0, 4, 1, 1, 0, 1) };
		let before = handle::alive_count();
		let mut h = unsafe { oakcodec_frame_init_with_params(params) };
		assert!(!h.is_null());
		// init -> exactly one more live box.
		assert_eq!(handle::alive_count(), before + 1);

		// get_params round-trips width/height through the stub.
		let mut out = empty_params();
		let rc = unsafe { oakcodec_frame_get_params(h, &mut out) };
		assert_eq!(rc, crate::error::OAKCODEC_OK);
		// NOTE: stub handles carry no addref and `oakcommon_videoparams_free`
		// would drop the shared box; the test keeps the copy alive for the
		// frame's lifetime and does not free it.
		assert_eq!(unsafe { oakcommon_videoparams_get_width(out.clone()) }, 100);
		assert_eq!(unsafe { oakcommon_videoparams_get_height(out.clone()) }, 50);

		assert_eq!(unsafe { oakcodec_frame_width(h) }, 100);
		assert_eq!(unsafe { oakcodec_frame_height(h) }, 50);
		assert_eq!(unsafe { oakcodec_frame_is_allocated(h) }, 0);
		assert_eq!(unsafe { oakcodec_frame_data(h) }, std::ptr::null_mut());

		let rc = unsafe { oakcodec_frame_allocate(h) };
		assert_eq!(rc, crate::error::OAKCODEC_OK);
		assert_eq!(unsafe { oakcodec_frame_is_allocated(h) }, 1);
		assert!(!unsafe { oakcodec_frame_data(h) }.is_null());
		// U8 RGBA: 100px -> 4*128 bytes linesize.
		assert_eq!(unsafe { oakcodec_frame_linesize_bytes(h) }, 4 * 128);
		assert_eq!(unsafe { oakcodec_frame_allocated_size(h) }, (4 * 128) * 50);

		// set_timestamp round-trip.
		assert_eq!(
			unsafe { oakcodec_frame_set_timestamp(h, 1, 30) },
			crate::error::OAKCODEC_OK
		);
		let (mut num, mut den) = (0, 0);
		assert_eq!(
			unsafe { oakcodec_frame_get_timestamp(h, &mut num, &mut den) },
			crate::error::OAKCODEC_OK
		);
		assert_eq!((num, den), (1, 30));

		unsafe { oakcodec_frame_free(&mut h) };
		assert!(h.is_null());
		assert_eq!(handle::alive_count(), before);
	}

	#[test]
	fn frame_errors_and_empty_handles() {
		let _g = crate::ffi::lock_tests();
		let empty = CHandle::null();
		assert_eq!(unsafe { oakcodec_frame_width(empty) }, 0);
		assert_eq!(
			unsafe { oakcodec_frame_format(empty) },
			OAKCOMMON_PIXEL_FORMAT_INVALID
		);
		assert_eq!(
			unsafe { oakcodec_frame_allocate(empty) },
			OAKCODEC_E_INVALID
		);
		assert_eq!(
			unsafe { oakcodec_frame_get_params(empty, std::ptr::null_mut()) },
			OAKCODEC_E_INVALID
		);

		// init_basic(0, 0) is not valid -> allocate rejects with E_STATE.
		let params = unsafe { oakcommon_videoparams_init_basic(0, 0, 0, 4, 1, 1, 0, 1) };
		let mut h = unsafe { oakcodec_frame_init_with_params(params) };
		assert!(!h.is_null());
		assert_eq!(
			unsafe { oakcodec_frame_allocate(h) },
			crate::error::OAKCODEC_E_STATE
		);
		assert_eq!(unsafe { oakcodec_frame_is_allocated(h) }, 0);
		unsafe { oakcodec_frame_free(&mut h) };
	}

	#[test]
	fn free_null_and_empty_are_noops() {
		let _g = crate::ffi::lock_tests();
		let before = handle::alive_count();
		unsafe { oakcodec_frame_free(std::ptr::null_mut()) };
		let mut empty = CHandle::null();
		unsafe { oakcodec_frame_free(&mut empty) };
		assert!(empty.is_null());
		assert_eq!(handle::alive_count(), before);
	}

	#[test]
	fn init_set_params_and_query_helpers() {
		let _g = crate::ffi::lock_tests();
		let before = handle::alive_count();

		// Bare init (no params): invalid params, not allocated. The stub's
		// default MockParams carries format 0 (U8); the empty-handle -1
		// case is covered in `frame_errors_and_empty_handles`.
		let mut h = unsafe { oakcodec_frame_init() };
		assert!(!h.is_null());
		assert_eq!(handle::alive_count(), before + 1);
		assert_eq!(unsafe { oakcodec_frame_width(h) }, 0);
		assert_eq!(unsafe { oakcodec_frame_height(h) }, 0);
		assert_eq!(unsafe { oakcodec_frame_channel_count(h) }, 4);
		assert_eq!(unsafe { oakcodec_frame_is_allocated(h) }, 0);
		assert_eq!(unsafe { oakcodec_frame_allocated_size(h) }, 0);
		assert_eq!(unsafe { oakcodec_frame_linesize_bytes(h) }, 0);
		assert_eq!(unsafe { oakcodec_frame_linesize_pixels(h) }, 0);
		assert_eq!(unsafe { oakcodec_frame_data(h) }, std::ptr::null_mut());
		assert_eq!(unsafe { oakcodec_frame_const_data(h) }, std::ptr::null());
		assert_eq!(
			unsafe { oakcodec_frame_allocate(h) },
			crate::error::OAKCODEC_E_STATE
		);

		// set_params replaces the parameter set and recomputes line sizes.
		let params = unsafe { oakcommon_videoparams_init_basic(100, 50, 0, 4, 1, 1, 0, 1) };
		let rc = unsafe { oakcodec_frame_set_params(h, params) };
		assert_eq!(rc, crate::error::OAKCODEC_OK);
		assert_eq!(unsafe { oakcodec_frame_width(h) }, 100);
		assert_eq!(unsafe { oakcodec_frame_height(h) }, 50);
		assert_eq!(unsafe { oakcodec_frame_format(h) }, 0); // U8
		assert_eq!(unsafe { oakcodec_frame_channel_count(h) }, 4);
		assert_eq!(unsafe { oakcodec_frame_linesize_bytes(h) }, 4 * 128);
		assert_eq!(unsafe { oakcodec_frame_linesize_pixels(h) }, 128);

		// allocate -> data and const_data point at the buffer.
		assert_eq!(
			unsafe { oakcodec_frame_allocate(h) },
			crate::error::OAKCODEC_OK
		);
		assert!(!unsafe { oakcodec_frame_data(h) }.is_null());
		assert!(!unsafe { oakcodec_frame_const_data(h) }.is_null());
		assert_eq!(unsafe { oakcodec_frame_allocated_size(h) }, (4 * 128) * 50);

		// get_timestamp rejects NULL out pointers.
		assert_eq!(
			unsafe { oakcodec_frame_get_timestamp(h, std::ptr::null_mut(), std::ptr::null_mut()) },
			OAKCODEC_E_INVALID
		);

		// debug_alive_count reports the live boxes (>= our own).
		assert!(unsafe { oakcodec_debug_alive_count() } >= before + 1);

		unsafe { oakcodec_frame_free(&mut h) };
		assert_eq!(handle::alive_count(), before);
	}

	fn empty_params() -> OakVideoParams {
		OakVideoParams {
			ctx: std::ptr::null_mut(),
			addref: None,
			release: None,
			abi_version: 0,
		}
	}
}
