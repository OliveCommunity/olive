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

//! bridge：oak 其余模块的 C ABI 导入。
//!
//! 每个子模块（node/render/undo）只声明对应公共头的子集并包一层
//! 极薄的 safe 封装。链接在模块 dylib 装配时完成（liboaknode/
//! liboakrender/liboakundo）。
//!
//! ## 双态实现（所有子模块统一）
//!
//! - 默认：`dlsym(RTLD_DEFAULT)` 运行时解析（本模块被 force_load
//!   进宿主进程，符号在全局作用域；cargo test 无这些库时符号缺失
//!   → 可解释错误，测试可走通至桥边界）；
//! - `--features test-stubs`：库内 no_mangle 桩 + 状态访问器
//!   （各子模块的 [`node::stub`]/[`render::stub`]/[`undo::stub`]）——
//!   桥路径在 cargo test 里全链路可跑。两种形态的调用面完全一致。
//!
//! [`dlsym`] 是三个子模块共享的解析实现（macOS/Linux 的 RTLD_DEFAULT
//! 取值不同；函数指针按调用方签名转换）。

pub mod node;
pub mod render;
pub mod undo;

/// 共享的 dlsym 运行时解析（`#[cfg(not(feature = "test-stubs"))]`）。
#[cfg(not(feature = "test-stubs"))]
pub(crate) mod dlsym {
	use std::ffi::{c_char, c_void};

	/// RTLD_DEFAULT（macOS: -2；Linux: 0）。
	#[cfg(target_os = "macos")]
	pub(crate) const RTLD_DEFAULT: *mut c_void = -2isize as *mut c_void;
	#[cfg(target_os = "linux")]
	pub(crate) const RTLD_DEFAULT: *mut c_void = 0isize as *mut c_void;

	extern "C" {
		fn dlsym(handle: *mut c_void, symbol: *const c_char) -> *mut c_void;
	}

	/// 解析全局作用域符号；缺失返回 None。
	pub(crate) fn resolve(name: &str) -> Option<*mut c_void> {
		let c = std::ffi::CString::new(name).ok()?;
		let p = unsafe { dlsym(RTLD_DEFAULT, c.as_ptr()) };
		if p.is_null() {
			None
		} else {
			Some(p)
		}
	}

	/// 解析并按签名调用；符号缺失返回 None。
	///
	/// # Safety
	/// 调用方保证 `T` 与符号的真实函数类型一致。
	pub(crate) fn call<T, R>(name: &str, f: impl FnOnce(T) -> R) -> Option<R>
	where
		T: Copy,
	{
		let p = resolve(name)?;
		let f_ptr: T = unsafe { std::mem::transmute_copy(&p) };
		Some(f(f_ptr))
	}
}
