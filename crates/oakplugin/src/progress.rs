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

//! 进度上报（Progress suite 的宿主侧）。
//!
//! 对应 C++ 的 `PluginProgressReporter`。进度/取消经 facade 注册的
//! 回调出 crate（M9 的 facade 回调模式，不设全局状态）。
//! 取消是粘滞的：一旦回调答 false，本报告器的 [`is_cancelled`]
//! （image effect suite 的 abort 查询）持续为真。

use std::ffi::c_int;

use std::sync::atomic::{AtomicBool, Ordering};

/// 进度回调：签名与 `include/plugin/instance.h` 的
/// `oakplugin_progress_fn` 逐字一致——`(progress, userdata)`，
/// 返回非 0 表示应中止（骨架声明为 `(userdata, progress) -> bool`，
/// 与头文件不符，以此为准）。
pub type ProgressFn = unsafe extern "C" fn(progress: f64, userdata: *mut std::ffi::c_void) -> c_int;

/// 进度上报器。render 路径持有一份，Progress suite 的
/// progressStart/Update/End 转发到这里。
pub struct ProgressReporter {
	callback: Option<ProgressFn>,
	userdata: usize,
	/// 取消标志（粘滞；update 返回 false 时置位）。
	cancelled: AtomicBool,
}

impl ProgressReporter {
	/// 无回调（渲染静默进行）。
	pub fn silent() -> Self {
		Self {
			callback: None,
			userdata: 0,
			cancelled: AtomicBool::new(false),
		}
	}

	/// 带回调。
	///
	/// # Safety
	/// `userdata` 的生命周期由注册方保证。
	pub unsafe fn new(callback: ProgressFn, userdata: *mut std::ffi::c_void) -> Self {
		Self {
			callback: Some(callback),
			// usize 存（裸指针破坏 Send 推导；值语义不变）。
			userdata: userdata as usize,
			cancelled: AtomicBool::new(false),
		}
	}

	/// 报告进度；返回 false 表示应取消（映射
	/// kOfxStatReplyNo/action 失败由调用点决定）。
	pub fn update(&self, progress: f64) -> bool {
		match self.callback {
			Some(cb) => {
				// 头文件契约：非 0 = 中止。
				let abort = unsafe { cb(progress, self.userdata as *mut std::ffi::c_void) } != 0;
				if abort {
					self.cancelled.store(true, Ordering::Relaxed);
				}
				!abort
			}
			None => true,
		}
	}

	/// 本次渲染是否已被取消（image effect suite 的 abort 透传）。
	pub(crate) fn is_cancelled(&self) -> bool {
		self.cancelled.load(Ordering::Relaxed)
	}
}
