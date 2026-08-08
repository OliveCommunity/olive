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

//! Codec task submitter registration, mirroring
//! `src/task/src/codecbridge.h`.
//!
//! Wires oakcodec's task-submit callback (`oakcodec_set_task_submit_cb`) so
//! conform/proxy requests from the codec side land back in the task module.
//! This is a two-module coupling; the actual callback signatures live in
//! `crate::bridge::codec` mirroring `include/codec/task.h` verbatim.
//!
//! CPP-PARITY: src/task/src/codecbridge.h

use std::ffi::{c_char, c_int, c_void};

use crate::bridge;
use crate::conform::ConformTask;
use crate::error::{Error, Result};
use crate::proxy::{ProxyParams, ProxyTask};
use crate::task::Task;

/// Convert an oakcodec proxy-params POD into the Rust [`ProxyParams`].
fn proxy_params_from_codec(params: &bridge::codec::OakCodecProxyParams) -> ProxyParams {
	ProxyParams {
		width: params.width,
		height: params.height,
		divider: params.divider,
		version: params.version,
		crf: params.crf,
		include_audio: params.include_audio != 0,
		extension: unsafe { cstr_buf_to_string(&params.extension) },
		preset: unsafe { cstr_buf_to_string(&params.preset) },
	}
}

/// Read a NUL-terminated char array into a String (lossy).
unsafe fn cstr_buf_to_string(buf: &[c_char]) -> String {
	let len = buf.iter().position(|&c| c == 0).unwrap_or(buf.len());
	let bytes = unsafe { std::slice::from_raw_parts(buf.as_ptr() as *const u8, len) };
	String::from_utf8_lossy(bytes).into_owned()
}

/// The installed submit callback, mirroring `submit_codec_task` in
/// codecbridge.cpp (interim contract: submission is synchronous).
///
/// # Safety
/// `req` must be a valid `OakCodecTaskRequest` or null.
unsafe extern "C" fn submit_codec_task(req: *const bridge::codec::OakCodecTaskRequest, _userdata: *mut c_void) -> c_int {
	if req.is_null() {
		return bridge::codec::OAKCODEC_E_INVALID;
	}
	let request = unsafe { &*req };

	// Build the concrete task on an outer base so the behavior can be
	// driven by `start()`; both share the cancellation atom.
	match request.kind {
		bridge::codec::OAKCODEC_TASK_CONFORM => {
			let task = ConformTask::new(request);
			let atom = task.base.get_cancel_atom();
			let title = task.base.title().to_string();
			let mut outer = Task::new(&title, atom);
			outer.set_behavior(Box::new(task));
			match outer.start() {
				Ok(()) => 0,
				Err(_) => bridge::codec::OAKCODEC_E_FAILED,
			}
		}
		bridge::codec::OAKCODEC_TASK_PROXY => {
			let mut codec_params = bridge::codec::OakCodecProxyParams {
				width: 0,
				height: 0,
				divider: 0,
				version: 0,
				crf: 0,
				include_audio: 0,
				extension: [0; 32],
				preset: [0; 32],
			};
			unsafe {
				bridge::codec::oakcodec_proxy_params_default(&mut codec_params);
			}
			let params = proxy_params_from_codec(&codec_params);
			let task = ProxyTask::new(request, params);
			let atom = task.base.get_cancel_atom();
			let title = task.base.title().to_string();
			let mut outer = Task::new(&title, atom);
			outer.set_behavior(Box::new(task));
			match outer.start() {
				Ok(()) => 0,
				Err(_) => bridge::codec::OAKCODEC_E_FAILED,
			}
		}
		_ => bridge::codec::OAKCODEC_E_INVALID,
	}
}

/// Install the task-module submitter as oakcodec's callback. Returns
/// `Err(Error::State)` if already registered.
pub fn register_codec_task_submitter() -> Result<()> {
	if is_codec_task_submitter_registered() {
		return Err(Error::State);
	}
	unsafe {
		bridge::codec::oakcodec_set_task_submit_cb(Some(submit_codec_task), std::ptr::null_mut());
	}
	Ok(())
}

/// Remove the task-module submitter from oakcodec. Idempotent.
pub fn unregister_codec_task_submitter() -> Result<()> {
	unsafe {
		bridge::codec::oakcodec_set_task_submit_cb(None, std::ptr::null_mut());
	}
	Ok(())
}

/// Whether a submitter is currently installed (queries oakcodec).
pub fn is_codec_task_submitter_registered() -> bool {
	unsafe { bridge::codec::oakcodec_task_submit_is_registered() != 0 }
}
