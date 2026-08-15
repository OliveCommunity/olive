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

//! 引用计数句柄脚手架。
//!
//! 对应 C 侧布局（`include/plugin/instance.h`，与 oak 全项目约定一致）：
//!
//! ```c
//! typedef struct OakPluginInstance {
//!     void *ctx;
//!     void (*addref)(void *ctx);
//!     void (*release)(void *ctx);
//!     uint32_t abi_version;
//! } OakPluginInstance;
//! ```
//!
//! 句柄按值传；`ctx` 指向本 crate 堆上的 [`RefBox<T>`]。`addref`/
//! `release` 函数指针永远指向本 crate 的代码（所有权不出 DLL）。

use std::any::Any;
use std::collections::HashMap;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::atomic::{AtomicU32, Ordering};
use std::sync::{Arc, Mutex, Weak};

use crate::error::OAKPLUGIN_E_FAILED;

/// 当前 ABI 版本，写进每个句柄的 `abi_version` 字段。
pub const OAKPLUGIN_ABI_VERSION: u32 = 1;

/// 句柄背后的堆盒子。`owns == false` 的盒子（借用包装）在计数归零时
/// 只释放盒子本身，不销毁内含对象。
pub struct RefBox<T: ?Sized> {
	/// 引用计数（原子；release 可在任意线程发生）。
	pub refs: AtomicU32,
	/// 内含对象。
	pub value: T,
}

/// C 句柄的 Rust 镜像。`#[repr(C)]`，与 C 头文件布局一致。
///
/// 生命周期：`*_init`/`*_create` 返回计数 1 的拥有型句柄；
/// The shared ABI value-handle type (single-lib unification, see
/// `docs/zh/plans/riir/single-lib.md`): one canonical
/// `{ctx, addref, release, abi_version}` type in `oakcore-rs`, re-exported
/// here so the crate's handle scaffolding stays source-compatible.
/// `Send + Sync` come from the shared type.
pub use oakcore_rs::handle::CHandle;

/// addref 的实现：原子 +1。拥有型与借用型共用——借用型只延长盒子
/// 的寿命，不延长被借用对象。
unsafe extern "C" fn refbox_addref<T: Any + Send>(ctx: *mut std::ffi::c_void) {
	unsafe {
		let rb = ctx as *const RefBox<T>;
		// 调用方保证句柄在借用期内有效（ctx 非空且未被释放）。
		(*rb).refs.fetch_add(1, Ordering::Relaxed);
	}
}

/// release 的实现（拥有型）：原子 -1，归零时回收盒子并销毁内含对象。
unsafe extern "C" fn refbox_release_owned<T: Any + Send>(ctx: *mut std::ffi::c_void) {
	unsafe {
		let rb = ctx as *mut RefBox<T>;
		// AcqRel：归零这一侧要能看见最后一次引用前的全部写（含对象
		// 析构所需的内部状态）。
		if (*rb).refs.fetch_sub(1, Ordering::AcqRel) == 1 {
			drop(Box::from_raw(rb));
		}
	}
}

/// release 的实现（借用型，[`make_borrowed`] 的产物）：归零时只回收
/// 盒子内存，把内含对象原样忘掉——其所有权仍在借用方手里。
unsafe extern "C" fn refbox_release_borrowed<T: Any + Send>(ctx: *mut std::ffi::c_void) {
	unsafe {
		let rb = ctx as *mut RefBox<T>;
		if (*rb).refs.fetch_sub(1, Ordering::AcqRel) == 1 {
			// 部分 move：把 value 移出临时 Box，Box 析构只释放分配；
			// value 用 forget 放弃析构（double-free 防线）。
			std::mem::forget((Box::from_raw(rb)).value);
		}
	}
}

/// 为 `T` 制作拥有型句柄（计数 1）。分配失败返回空句柄并销毁对象。
///
/// 注：Rust 默认分配失败（OOM）直接 abort，不会走到"返回空句柄"
/// 路径；此处语义保留给未来接入自定义分配器的场景。
pub fn make_owned<T: Any + Send>(value: T) -> CHandle {
	let rb = Box::into_raw(Box::new(RefBox {
		refs: AtomicU32::new(1),
		value,
	}));
	CHandle {
		ctx: rb as *mut std::ffi::c_void,
		addref: Some(refbox_addref::<T>),
		release: Some(refbox_release_owned::<T>),
		abi_version: OAKPLUGIN_ABI_VERSION,
	}
}

/// 为已有对象制作借用句柄（计数归零只释放盒子）。`ptr` 必须在本
/// 句柄被释放前保持有效。
///
/// 语义：按位拷贝（"借用拷贝"，如纹理句柄的快照）；被借用对象
/// 的析构完全由调用方负责，盒子从不碰它。拷贝即快照——借出后
/// 修改 `*ptr` 不会反映到句柄内。
///
/// # Safety
/// 调用方保证 `ptr` 的生命周期覆盖所有派生句柄，且其值在借用期内
/// 不被 move/析构。
pub unsafe fn make_borrowed<T: Any + Send>(ptr: *mut T) -> CHandle {
	if ptr.is_null() {
		return CHandle::null();
	}
	let rb = Box::into_raw(Box::new(RefBox {
		refs: AtomicU32::new(1),
		value: unsafe { std::ptr::read(ptr) },
	}));
	CHandle {
		ctx: rb as *mut std::ffi::c_void,
		addref: Some(refbox_addref::<T>),
		release: Some(refbox_release_borrowed::<T>),
		abi_version: OAKPLUGIN_ABI_VERSION,
	}
}

/// 取回盒子内对象的不可变引用；空句柄返回 `None`。
///
/// # Safety
/// 调用方必须保证 `T` 与创建句柄时的类型一致。
pub unsafe fn get<T: Any>(h: &CHandle) -> Option<&T> {
	if h.is_null() {
		return None;
	}
	unsafe { Some(&(*(h.ctx as *const RefBox<T>)).value) }
}

/// FFI 兜底：捕获 panic，把 `Result<i32>` 映射为对外错误码
/// （[`crate::error`]）。所有返回 i32 的导出函数必须经它。
///
/// panic 路径返回 `OAKPLUGIN_E_FAILED`；panic 详情暂不落日志
/// （message 桥接入后补 TODO）。
pub fn guard<F>(f: F) -> i32
where
	F: FnOnce() -> crate::error::Result<()>,
{
	match catch_unwind(AssertUnwindSafe(f)) {
		Ok(Ok(())) => crate::error::OAKPLUGIN_OK,
		Ok(Err(e)) => e.code(),
		Err(_) => OAKPLUGIN_E_FAILED,
	}
}

/// 指针/句柄返回值版本的 [`guard`]：panic 或 Err 时返回空句柄
/// （指针类返回 NULL）。
pub fn guard_handle<F>(f: F) -> CHandle
where
	F: FnOnce() -> crate::error::Result<CHandle>,
{
	match catch_unwind(AssertUnwindSafe(f)) {
		Ok(Ok(h)) => h,
		Ok(Err(_)) | Err(_) => CHandle::null(),
	}
}

/// 无返回值版本：panic 被吞（日志回调待 message 出口接入后补）。
pub fn guard_void<F>(f: F)
where
	F: FnOnce(),
{
	let _ = catch_unwind(AssertUnwindSafe(f));
}

/// 句柄身份注册表：`usize` 身份 ↔ 弱引用。供 param↔node 等需要
/// "按身份找回对象"的桥使用（替代 M9 C++ 版的
/// `oaknode_node_identity()` 注册表）。
pub struct Registry<T: Any + Send> {
	map: Mutex<HashMap<usize, Weak<RefBox<T>>>>,
}

impl<T: Any + Send> Registry<T> {
	/// 空注册表。
	pub fn new() -> Self {
		Self {
			map: Mutex::new(HashMap::new()),
		}
	}

	/// 登记对象，返回其身份（地址语义，进程内唯一）。
	pub fn register(&self, obj: &Arc<RefBox<T>>) -> usize {
		// Arc 分配地址即身份：同一 RefBox 恒稳定，进程内唯一。
		let id = Arc::as_ptr(obj) as *const () as usize;
		lock(&self.map).insert(id, Arc::downgrade(obj));
		id
	}

	/// 按身份取对象；对象已销毁或身份未知返回 `None`。
	pub fn lookup(&self, id: usize) -> Option<Arc<RefBox<T>>> {
		lock(&self.map).get(&id).and_then(|w| w.upgrade())
	}

	/// 摘除身份（对象销毁路径调用）。未知身份是 no-op。
	pub fn unregister(&self, id: usize) {
		lock(&self.map).remove(&id);
	}
}

/// 取锁。毒锁（本 crate 代码持锁时 panic）时接管内部状态继续——
/// 一次 panic 不级联成后续所有 FFI 调用失败。
fn lock<T>(m: &Mutex<T>) -> std::sync::MutexGuard<'_, T> {
	m.lock().unwrap_or_else(|e| e.into_inner())
}
