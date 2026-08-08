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

//! oakundo C ABI 导入（参数回写打包用到的子集；include/undo/
//! undocommand.h）。
//!
//! ## 语义（对照 C++ 的 oliveplugininstance.cpp `submit_undo_command`）
//!
//! 写回一律以"命令"为单位：数值/字符串 set 各产出一条命令
//! （node 桥的 `*_undoable`），立即 `redo_now` 生效；编辑事务
//! （paramEditBegin/End）内多条命令并入一条 multi
//! （[`command_init_multi`] + [`command_multi_add_child`]），
//! editEnd 时整体 `redo_now` 后释放。
//!
//! ## 双态实现（同 [`crate::bridge::render`]）
//!
//! - 默认：`dlsym(RTLD_DEFAULT)` 运行时解析；
//! - `--features test-stubs`：库内桩（[`stub`]）——命令表 + undo/redo
//!   语义在 cargo test 里全链路可跑。两种形态的调用面完全一致。

use crate::handle::CHandle;

/// oakundo 命令句柄（值型）。
pub type CommandHandle = crate::handle::CHandle;

// ---- 桥调用面 ------------------------------------------------------------

/// 创建 multi 命令（`oakundo_command_init_multi`，undocommand.h:85）。
pub(crate) unsafe fn command_init_multi() -> CommandHandle {
	#[cfg(feature = "test-stubs")]
	{
		unsafe { stub::command_init_multi_impl() }
	}
	#[cfg(not(feature = "test-stubs"))]
	{
		type F = unsafe fn() -> CommandHandle;
		crate::bridge::dlsym::call::<F, CommandHandle>("oakundo_command_init_multi", |f| unsafe { f() })
			.unwrap_or_else(CHandle::null)
	}
}

/// 直接 redo（不进栈的立即执行路径；`oakundo_command_redo_now`）。
pub(crate) unsafe fn command_redo_now(command: CommandHandle) -> i32 {
	#[cfg(feature = "test-stubs")]
	{
		unsafe { stub::command_redo_now_impl(command) }
	}
	#[cfg(not(feature = "test-stubs"))]
	{
		type F = unsafe fn(CommandHandle) -> i32;
		crate::bridge::dlsym::call::<F, i32>("oakundo_command_redo_now", |f| unsafe { f(command) })
			.unwrap_or(-40001)
	}
}

/// 把子命令并入 multi（`oakundo_command_multi_add_child`）。
pub(crate) unsafe fn command_multi_add_child(multi: CommandHandle, child: CommandHandle) -> i32 {
	#[cfg(feature = "test-stubs")]
	{
		unsafe { stub::command_multi_add_child_impl(multi, child) }
	}
	#[cfg(not(feature = "test-stubs"))]
	{
		type F = unsafe fn(CommandHandle, CommandHandle) -> i32;
		crate::bridge::dlsym::call::<F, i32>("oakundo_command_multi_add_child", |f| unsafe {
			f(multi, child)
		})
		.unwrap_or(-40001)
	}
}

/// 释放命令句柄（`oakundo_command_free`；NULL/空句柄 no-op）。
pub(crate) unsafe fn command_free(command: *mut CommandHandle) {
	#[cfg(feature = "test-stubs")]
	{
		unsafe { stub::command_free_impl(command) }
	}
	#[cfg(not(feature = "test-stubs"))]
	{
		type F = unsafe fn(*mut CommandHandle);
		if let Some(f) = crate::bridge::dlsym::call::<F, ()>("oakundo_command_free", |f| {
			unsafe { f(command) }
		}) {
			let _ = f;
		}
	}
}

// ---- 测试桩（--features test-stubs）--------------------------------------

/// oakundo 测试桩：命令表 + undo/redo 语义。
///
/// 命令句柄的 ctx 约定（桩内约定）：`ctx = 命令 id`。记录在
/// [`command_free`] 后仍保留（`freed` 标记），供测试检查命令捕获的
/// prev/next 并驱动 undo/redo——真实 oakundo 的 free 会销毁命令，
/// 这是桩的刻意简化（记录仅为测试保留）。
#[cfg(feature = "test-stubs")]
pub mod stub {
	use super::*;
	use crate::bridge::node;
	use std::collections::HashMap;
	use std::sync::atomic::{AtomicUsize, Ordering};
	use std::sync::{LazyLock, Mutex};

	/// 一条命令的记录（redo 应用 next、undo 恢复 prev）。
	#[derive(Clone, Debug)]
	pub struct CommandRecord {
		/// 命令 id（= 句柄 ctx）。
		pub id: usize,
		/// 目标节点身份。
		pub node: usize,
		/// 目标输入名。
		pub input: String,
		/// 数值回写的旧值。
		pub prev: node::Value,
		/// 数值回写的新值。
		pub next: node::Value,
		/// 字符串回写的旧值。
		pub prev_string: Option<String>,
		/// 字符串回写的新值。
		pub next_string: Option<String>,
		/// redo 是否已应用。
		pub applied: bool,
		/// 是否 multi 命令。
		pub is_multi: bool,
		/// multi 的子命令 id。
		pub children: Vec<usize>,
		/// free 是否已调用（记录保留给测试检查）。
		pub freed: bool,
	}

	static COMMANDS: LazyLock<Mutex<HashMap<usize, CommandRecord>>> =
		LazyLock::new(|| Mutex::new(HashMap::new()));
	static NEXT_ID: AtomicUsize = AtomicUsize::new(1);

	fn lock() -> std::sync::MutexGuard<'static, HashMap<usize, CommandRecord>> {
		COMMANDS.lock().unwrap_or_else(|e| e.into_inner())
	}

	fn magic(id: usize) -> CommandHandle {
		CHandle {
			ctx: id as *mut std::ffi::c_void,
			addref: None,
			release: None,
			abi_version: crate::handle::OAKPLUGIN_ABI_VERSION,
		}
	}

	fn next_id() -> usize {
		NEXT_ID.fetch_add(1, Ordering::Relaxed)
	}

	/// 重置命令表（测试隔离）。
	pub fn reset() {
		lock().clear();
	}

	/// 全部命令记录快照（含已 free 的；id 升序）。
	pub fn records() -> Vec<CommandRecord> {
		let mut v: Vec<CommandRecord> = lock().values().cloned().collect();
		v.sort_by_key(|r| r.id);
		v
	}

	/// 最近一次创建的命令记录。
	pub fn last_command() -> Option<CommandRecord> {
		let m = lock();
		m.values().max_by_key(|r| r.id).cloned()
	}

	/// 撤销一条命令（测试助手：验证命令捕获的 prev 正确）。multi 按
	/// 子命令逆序撤销；单命令仅在其已应用时恢复 prev。
	pub fn undo(id: usize) {
		let (is_multi, children) = lock()
			.get(&id)
			.map(|r| (r.is_multi, r.children.clone()))
			.unwrap_or((false, Vec::new()));
		if is_multi {
			for c in children.iter().rev() {
				undo(*c);
			}
			if let Some(r) = lock().get_mut(&id) {
				r.applied = false;
			}
			return;
		}
		let mut m = lock();
		let Some(r) = m.get_mut(&id) else { return };
		if r.applied {
			node::stub::apply(r.node, &r.input, r.prev, r.prev_string.clone());
			r.applied = false;
		}
	}

	/// 重做一条命令（测试助手）。redo 幂等（已应用则 no-op）。
	pub fn redo(id: usize) {
		let (is_multi, children) = lock()
			.get(&id)
			.map(|r| (r.is_multi, r.children.clone()))
			.unwrap_or((false, Vec::new()));
		if is_multi {
			for c in &children {
				redo(*c);
			}
			if let Some(r) = lock().get_mut(&id) {
				r.applied = true;
			}
			return;
		}
		let mut m = lock();
		let Some(r) = m.get_mut(&id) else { return };
		if !r.applied {
			node::stub::apply(r.node, &r.input, r.next, r.next_string.clone());
			r.applied = true;
		}
	}

	/// 登记一条数值写回命令（node 桥调用）。
	pub(crate) fn create_numeric(
		node_id: usize,
		input: String,
		prev: node::Value,
		next: node::Value,
	) -> CommandHandle {
		let id = next_id();
		lock().insert(
			id,
			CommandRecord {
				id,
				node: node_id,
				input,
				prev,
				next,
				prev_string: None,
				next_string: None,
				applied: false,
				is_multi: false,
				children: Vec::new(),
				freed: false,
			},
		);
		magic(id)
	}

	/// 登记一条字符串写回命令（node 桥调用）。
	pub(crate) fn create_string(
		node_id: usize,
		input: String,
		prev: String,
		next: String,
	) -> CommandHandle {
		let id = next_id();
		lock().insert(
			id,
			CommandRecord {
				id,
				node: node_id,
				input,
				prev: node::Value::string(),
				next: node::Value::string(),
				prev_string: Some(prev),
				next_string: Some(next),
				applied: false,
				is_multi: false,
				children: Vec::new(),
				freed: false,
			},
		);
		magic(id)
	}

	pub(super) unsafe fn command_init_multi_impl() -> CommandHandle {
		let id = next_id();
		lock().insert(
			id,
			CommandRecord {
				id,
				node: 0,
				input: String::new(),
				prev: node::Value::default(),
				next: node::Value::default(),
				prev_string: None,
				next_string: None,
				applied: false,
				is_multi: true,
				children: Vec::new(),
				freed: false,
			},
		);
		magic(id)
	}

	pub(super) unsafe fn command_redo_now_impl(command: CommandHandle) -> i32 {
		if command.is_null() {
			return -40001;
		}
		// 先取 multi 的子命令列表，再逐条递归（避免持锁递归）。
		let (is_multi, children) = lock()
			.get(&(command.ctx as usize))
			.map(|r| (r.is_multi, r.children.clone()))
			.unwrap_or((false, Vec::new()));
		if is_multi {
			for c in &children {
				let h = magic(*c);
				unsafe { command_redo_now_impl(h) };
			}
			if let Some(r) = lock().get_mut(&(command.ctx as usize)) {
				r.applied = true;
			}
			return 0;
		}
		let mut m = lock();
		let Some(r) = m.get_mut(&(command.ctx as usize)) else {
			return -40004;
		};
		if !r.applied {
			node::stub::apply(r.node, &r.input, r.next, r.next_string.clone());
			r.applied = true;
		}
		0
	}

	pub(super) unsafe fn command_multi_add_child_impl(multi: CommandHandle, child: CommandHandle) -> i32 {
		if multi.is_null() || child.is_null() {
			return -40001;
		}
		let mut m = lock();
		let Some(r) = m.get_mut(&(multi.ctx as usize)) else {
			return -40004;
		};
		if !r.is_multi {
			return -40002;
		}
		r.children.push(child.ctx as usize);
		0
	}

	pub(super) unsafe fn command_free_impl(command: *mut CommandHandle) {
		if command.is_null() {
			return;
		}
		let h = unsafe { &mut *command };
		if h.is_null() {
			return;
		}
		if let Some(r) = lock().get_mut(&(h.ctx as usize)) {
			// 记录保留给测试检查（undo/redo 仍可按 id 驱动）。
			r.freed = true;
		}
		h.ctx = std::ptr::null_mut();
	}
}

// ---- 默认模式单测（无桩；dlsym 缺失的可解释失败路径）----------------------

#[cfg(all(test, not(feature = "test-stubs")))]
mod tests {
	use super::*;

	/// cargo test 无 liboakundo：符号缺失 → 空句柄/负错误码，命令
	/// 生命周期函数对空句柄/NULL 容错不崩。
	#[test]
	fn dlsym_missing_error_paths() {
		unsafe {
			let mut m = command_init_multi();
			assert!(m.is_null(), "无 liboakundo 时 init_multi 应返回空句柄");
			assert_eq!(command_redo_now(CommandHandle::null()), -40001);
			assert_eq!(
				command_multi_add_child(CommandHandle::null(), CommandHandle::null()),
				-40001
			);
			command_free(&mut m); // 空句柄 no-op
			command_free(std::ptr::null_mut()); // NULL no-op
		}
	}
}
