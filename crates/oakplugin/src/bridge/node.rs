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

//! oaknode C ABI 导入（include/node/node.h 中 param 桥用到的子集）。
//!
//! ## Value 布局冻结（M11 第 1 期）
//!
//! [`Value`] 即 include/node/node.h:93 的 `oaknode_value` POD，字段
//! 逐字一致（type/num/den/f[4]；`type` 取值见 [`node_value_type`]）。
//! 字符串族输入（k_file/k_text/k_font/k_str_combo，node.h:48-52）没有
//! POD 表示——走 `*_input_string_*` 专用函数（本桥的
//! [`set_input_string_undoable`]）；`OAKNODE_VALUE_STRING` 的 POD 里
//! 不携带字符串数据。`crate::ffi::OakNodeValue` 与此同布局（出口层
//! 的镜像，两处独立声明避免模块环）。
//!
//! ## 双态实现（同 [`crate::bridge::render`]）
//!
//! - 默认：直接 Rust 调用 oaknode 的 `ffi`（单库化，见
//!   `docs/zh/plans/riir/single-lib.md`）；
//! - `--features test-stubs`：库内状态桩（[`stub`]，纯 Rust、无
//!   `#[no_mangle]`，与真实 oaknode 的导出不冲突）——节点值、undo
//!   命令全链路可在 cargo test 跑通。两种形态的调用面完全一致。

use std::ffi::c_char;

use crate::bridge::undo::CommandHandle;
use crate::handle::CHandle;

/// oaknode 节点句柄（值型）。
pub type NodeHandle = crate::handle::CHandle;

/// oaknode_value_type 的取值（node.h:74；与
/// `crate::ffi::node_value_type` 逐值一致）。
pub mod node_value_type {
	/// OAKNODE_VALUE_NONE。
	pub const NONE: i32 = 0;
	/// OAKNODE_VALUE_INT。
	pub const INT: i32 = 1;
	/// OAKNODE_VALUE_FLOAT。
	pub const FLOAT: i32 = 2;
	/// OAKNODE_VALUE_BOOL。
	pub const BOOL: i32 = 3;
	/// OAKNODE_VALUE_RATIONAL。
	pub const RATIONAL: i32 = 4;
	/// OAKNODE_VALUE_COLOR。
	pub const COLOR: i32 = 5;
	/// OAKNODE_VALUE_VEC2。
	pub const VEC2: i32 = 6;
	/// OAKNODE_VALUE_VEC3。
	pub const VEC3: i32 = 7;
	/// OAKNODE_VALUE_VEC4。
	pub const VEC4: i32 = 8;
	/// OAKNODE_VALUE_COMBO。
	pub const COMBO: i32 = 9;
	/// OAKNODE_VALUE_STRING。
	pub const STRING: i32 = 10;
}

/// oaknode_value（include/node/node.h:93，字段逐字一致）。
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Value {
	/// 类型（[`node_value_type`]）。
	pub r#type: i32,
	/// INT/COMBO 值、BOOL 0/1、RATIONAL 分子。
	pub num: i64,
	/// RATIONAL 分母。
	pub den: i64,
	/// FLOAT f[0]；VEC2/3/4 f[0..n-1]；COLOR r,g,b,a。
	pub f: [f64; 4],
}

impl Value {
	/// 类型化构造：整数 / choice 索引。
	pub const fn int(v: i64) -> Self {
		Self {
			r#type: node_value_type::INT,
			num: v,
			den: 0,
			f: [0.0; 4],
		}
	}

	/// 类型化构造：浮点。
	pub const fn float(v: f64) -> Self {
		Self {
			r#type: node_value_type::FLOAT,
			num: 0,
			den: 0,
			f: [v, 0.0, 0.0, 0.0],
		}
	}

	/// 类型化构造：布尔。
	pub const fn bool_(v: bool) -> Self {
		Self {
			r#type: node_value_type::BOOL,
			num: v as i64,
			den: 0,
			f: [0.0; 4],
		}
	}

	/// 类型化构造：choice（COMBO）。
	pub const fn combo(v: i64) -> Self {
		Self {
			r#type: node_value_type::COMBO,
			num: v,
			den: 0,
			f: [0.0; 4],
		}
	}

	/// 类型化构造：颜色。
	pub const fn color(r: f64, g: f64, b: f64, a: f64) -> Self {
		Self {
			r#type: node_value_type::COLOR,
			num: 0,
			den: 0,
			f: [r, g, b, a],
		}
	}

	/// 类型化构造：vec2/3/4（长度按 f 数组尾部 0 判定）。
	pub const fn vec(v: &[f64]) -> Self {
		let t = match v.len() {
			2 => node_value_type::VEC2,
			3 => node_value_type::VEC3,
			_ => node_value_type::VEC4,
		};
		let mut f = [0.0; 4];
		let mut i = 0;
		while i < v.len() && i < 4 {
			f[i] = v[i];
			i += 1;
		}
		Self {
			r#type: t,
			num: 0,
			den: 0,
			f,
		}
	}

	/// 类型化构造：字符串族（POD 不携带数据；值经
	/// [`set_input_string_undoable`] 传递）。
	pub const fn string() -> Self {
		Self {
			r#type: node_value_type::STRING,
			num: 0,
			den: 0,
			f: [0.0; 4],
		}
	}
}

// ---- 桥调用面 ------------------------------------------------------------

/// 按身份取节点句柄（M9 身份注册表；`oaknode_node_from_identity`）。
/// 身份未登记 / 符号缺失 → 空句柄。
pub(crate) unsafe fn node_from_identity(id: usize) -> NodeHandle {
	#[cfg(feature = "test-stubs")]
	{
		unsafe { stub::node_from_identity_impl(id) }
	}
	#[cfg(not(feature = "test-stubs"))]
	{
		unsafe { oaknode::ffi::node::oaknode_node_from_identity(id) }
	}
}

/// 写输入值的标准值并产出一条 undo 命令（`*out` 收到拥有型命令句柄；
/// `oaknode_node_set_input_undoable`，node.h:327）。字符串族输入走
/// [`set_input_string_undoable`]。失败（含符号缺失）返回负错误码。
///
/// # Safety
/// `input`/`value`/`out` 必须指向有效内存；`node` 是有效句柄。
pub(crate) unsafe fn set_input_undoable(
	node: NodeHandle,
	input: *const c_char,
	value: *const Value,
	out: *mut CommandHandle,
) -> i32 {
	#[cfg(feature = "test-stubs")]
	{
		unsafe { stub::set_input_undoable_impl(node, input, value, out) }
	}
	#[cfg(not(feature = "test-stubs"))]
	{
		unsafe {
			oaknode::ffi::node::oaknode_node_set_input_undoable(
				node,
				input,
				value as *const oaknode::value::OakNodeValue,
				out,
			)
		}
	}
}

/// 写字符串族输入的标准值并产出一条 undo 命令
/// （`oaknode_node_set_input_string_undoable`，node.h:346）。
///
/// # Safety
/// `input`/`value`/`out` 必须指向有效内存；`node` 是有效句柄。
pub(crate) unsafe fn set_input_string_undoable(
	node: NodeHandle,
	input: *const c_char,
	value: *const c_char,
	out: *mut CommandHandle,
) -> i32 {
	#[cfg(feature = "test-stubs")]
	{
		unsafe { stub::set_input_string_undoable_impl(node, input, value, out) }
	}
	#[cfg(not(feature = "test-stubs"))]
	{
		unsafe {
			oaknode::ffi::node::oaknode_node_set_input_string_undoable(node, input, value, out)
		}
	}
}

// ---- 测试桩（--features test-stubs）--------------------------------------

/// oaknode 测试桩：库内符号 + 节点值状态。
///
/// 节点句柄的 ctx 约定（桩内约定）：`ctx = 身份 id`。undo 命令的
/// 登记与 undo/redo 语义在 [`crate::bridge::undo::stub`]（两边共享
/// 同一张命令表）。
#[cfg(feature = "test-stubs")]
pub mod stub {
	use super::*;
	use std::collections::HashMap;
	use std::ffi::CStr;
	use std::sync::LazyLock;
	use std::sync::Mutex;

	/// 一个输入的标准值（数值走 [`Value`] POD；字符串族走 `string`）。
	#[derive(Clone, Debug, PartialEq)]
	pub struct StubInput {
		/// 数值值（字符串族为 STRING 类型空 POD）。
		pub value: Value,
		/// 字符串值（仅字符串族输入）。
		pub string: Option<String>,
	}

	/// 全部桩节点：身份 → 输入名 → 值。
	static NODES: LazyLock<Mutex<HashMap<usize, HashMap<String, StubInput>>>> =
		LazyLock::new(|| Mutex::new(HashMap::new()));

	fn lock() -> std::sync::MutexGuard<'static, HashMap<usize, HashMap<String, StubInput>>> {
		NODES.lock().unwrap_or_else(|e| e.into_inner())
	}

	/// 重置全部桩节点（测试隔离）。
	pub fn reset() {
		lock().clear();
	}

	/// 登记一个可被 [`super::node_from_identity`] 找到的节点。
	pub fn register_node(id: usize) {
		lock().entry(id).or_default();
	}

	/// 直接设置输入值（测试前置）。
	pub fn set_input(id: usize, input: &str, value: Value) {
		lock().entry(id).or_default().insert(
			input.to_string(),
			StubInput {
				value,
				string: None,
			},
		);
	}

	/// 直接设置字符串输入值（测试前置）。
	pub fn set_input_string(id: usize, input: &str, value: &str) {
		lock().entry(id).or_default().insert(
			input.to_string(),
			StubInput {
				value: Value::string(),
				string: Some(value.to_string()),
			},
		);
	}

	/// 读输入当前值（断言用）。
	pub fn input(id: usize, input: &str) -> Option<StubInput> {
		lock().get(&id)?.get(input).cloned()
	}

	/// 命令回写用的应用入口（redo/undo 落值；由
	/// [`crate::bridge::undo::stub`] 调用）。
	pub(crate) fn apply(node: usize, input: &str, value: Value, string: Option<String>) {
		let mut m = lock();
		if let Some(n) = m.get_mut(&node) {
			n.insert(input.to_string(), StubInput { value, string });
		}
	}

	/// 读输入当前值（命令创建时取 prev）。
	pub(crate) fn current(node: usize, input: &str) -> StubInput {
		lock()
			.get(&node)
			.and_then(|n| n.get(input))
			.cloned()
			.unwrap_or(StubInput {
				value: Value::default(),
				string: None,
			})
	}

	pub(super) unsafe fn node_from_identity_impl(id: usize) -> NodeHandle {
		if lock().contains_key(&id) {
			CHandle {
				ctx: id as *mut std::ffi::c_void,
				addref: None,
				release: None,
				abi_version: crate::handle::OAKPLUGIN_ABI_VERSION,
			}
		} else {
			CHandle::null()
		}
	}

	pub(super) unsafe fn set_input_undoable_impl(
		node: NodeHandle,
		input: *const c_char,
		value: *const Value,
		out: *mut CommandHandle,
	) -> i32 {
		if node.is_null() || input.is_null() || value.is_null() || out.is_null() {
			return -30001;
		}
		let id = node.ctx as usize;
		let name = unsafe { CStr::from_ptr(input) }
			.to_str()
			.map_err(|_| ())
			.unwrap_or_default();
		// 输入名必须已存在（node.h: 未知输入 id → OAKNODE_E_NOT_FOUND）。
		if !lock().get(&id).is_some_and(|n| n.contains_key(name)) {
			return -30004;
		}
		let next = unsafe { *value };
		let prev = current(id, &name).value;
		let h = crate::bridge::undo::stub::create_numeric(id, name.to_string(), prev, next);
		unsafe { *out = h };
		0
	}

	pub(super) unsafe fn set_input_string_undoable_impl(
		node: NodeHandle,
		input: *const c_char,
		value: *const c_char,
		out: *mut CommandHandle,
	) -> i32 {
		if node.is_null() || input.is_null() || value.is_null() || out.is_null() {
			return -30001;
		}
		let id = node.ctx as usize;
		let name = unsafe { CStr::from_ptr(input) }
			.to_str()
			.map_err(|_| ())
			.unwrap_or_default();
		// 输入名必须已存在（node.h: 未知输入 id → OAKNODE_E_NOT_FOUND）。
		if !lock().get(&id).is_some_and(|n| n.contains_key(name)) {
			return -30004;
		}
		let next = unsafe { CStr::from_ptr(value) }
			.to_string_lossy()
			.into_owned();
		let prev = current(id, &name).string.unwrap_or_default();
		let h = crate::bridge::undo::stub::create_string(
			id,
			name.to_string(),
			prev.clone(),
			next.clone(),
		);
		unsafe { *out = h };
		0
	}
}

// ---- 默认模式单测（无桩；空/NULL 句柄经真实 crate 的可解释失败路径）----------------------

#[cfg(all(test, not(feature = "test-stubs")))]
mod tests {
	use super::*;

	/// 空/NULL 句柄经真实 oaknode 容错：未登记身份 → 空句柄；空句柄
	/// 与空指针参数被拒（OAKNODE_E_INVALID）。
	#[test]
	fn empty_handle_error_paths() {
		let mut cmd = CommandHandle::null();
		unsafe {
			assert!(node_from_identity(0xDEAD).is_null(), "未登记身份 → 空句柄");
			assert_eq!(
				set_input_undoable(
					NodeHandle::null(),
					std::ptr::null(),
					std::ptr::null(),
					&mut cmd
				),
				-30001
			);
			assert_eq!(
				set_input_string_undoable(
					NodeHandle::null(),
					std::ptr::null(),
					std::ptr::null(),
					&mut cmd
				),
				-30001
			);
		}
	}
}
