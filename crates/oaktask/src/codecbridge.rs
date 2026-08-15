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
//! Wires oakcodec's task-submit callback (`oakcodec::task::set_task_submit_cb`)
//! so conform/proxy requests from the codec side land back in the task
//! module. The registration is a direct Rust closure (single-lib
//! unification: the old extern-C submit callback is gone).
//!
//! CPP-PARITY: src/task/src/codecbridge.h

use oakcodec::error::Error as CodecError;
use oakcodec::proxymanager::ProxyManager;
use oakcodec::task::{
	set_task_submit_cb, task_submit_is_registered, TaskKind, TaskRequest, TaskSubmitFn,
};

use crate::conform::ConformTask;
use crate::error::{Error, Result};
use crate::proxy::{ProxyParams, ProxyTask};
use crate::task::Task;

/// Convert oakcodec proxy params into the Rust [`ProxyParams`].
fn proxy_params_from_codec(params: &oakcodec::proxymanager::ProxyParams) -> ProxyParams {
	ProxyParams {
		width: params.width,
		height: params.height,
		divider: params.divider,
		version: params.version,
		crf: params.crf,
		include_audio: params.include_audio != 0,
		extension: cstr_buf_to_string(&params.extension),
		preset: cstr_buf_to_string(&params.preset),
	}
}

/// Read a NUL-terminated byte array into a String (lossy).
fn cstr_buf_to_string(buf: &[u8]) -> String {
	let len = buf.iter().position(|&c| c == 0).unwrap_or(buf.len());
	String::from_utf8_lossy(&buf[..len]).into_owned()
}

/// The installed submit callback, mirroring `submit_codec_task` in
/// codecbridge.cpp (interim contract: submission is synchronous).
fn submit_codec_task(
	req: &TaskRequest,
	_userdata: *mut std::ffi::c_void,
) -> oakcodec::error::Result<()> {
	// Build the concrete task on an outer base so the behavior can be
	// driven by `start()`; both share the cancellation atom.
	match req.kind {
		TaskKind::Conform => {
			let task = ConformTask::new(req);
			let atom = task.base.get_cancel_atom();
			let title = task.base.title().to_string();
			let mut outer = Task::new(&title, Some(atom));
			outer.set_behavior(Box::new(task));
			outer
				.start()
				.map_err(|_| CodecError::Failed("conform task failed".to_string()))
		}
		TaskKind::Proxy => {
			let codec_params = ProxyManager::proxy_params_from_config();
			let params = proxy_params_from_codec(&codec_params);
			let task = ProxyTask::new(req, params);
			let atom = task.base.get_cancel_atom();
			let title = task.base.title().to_string();
			let mut outer = Task::new(&title, Some(atom));
			outer.set_behavior(Box::new(task));
			outer
				.start()
				.map_err(|_| CodecError::Failed("proxy task failed".to_string()))
		}
	}
}

/// Install the task-module submitter as oakcodec's callback. Returns
/// `Err(Error::State)` if already registered.
pub fn register_codec_task_submitter() -> Result<()> {
	if is_codec_task_submitter_registered() {
		return Err(Error::State);
	}
	let cb: &'static TaskSubmitFn = Box::leak(Box::new(submit_codec_task));
	set_task_submit_cb(Some(cb), std::ptr::null_mut());
	Ok(())
}

/// Remove the task-module submitter from oakcodec. Idempotent.
pub fn unregister_codec_task_submitter() -> Result<()> {
	set_task_submit_cb(None, std::ptr::null_mut());
	Ok(())
}

/// Whether a submitter is currently installed (queries oakcodec).
pub fn is_codec_task_submitter_registered() -> bool {
	task_submit_is_registered()
}
