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

//! bridge：oak 其余模块的 C ABI 调用——直接 Rust 调用（单库化，见
//! `docs/zh/plans/riir/single-lib.md`）。
//!
//! 每个子模块（node/render/undo）调用对应 crate 的 `ffi` 导出（同名
//! `#[no_mangle]` 符号仍从 dylib 导出供外部 C ABI 使用；内部调用绕过
//! 它们）。句柄全部是共享的 [`crate::handle::CHandle`]。
//!
//! ## 双态实现（所有子模块统一）
//!
//! - 默认：直接调用 oaknode/oakrender/oakundo 的 ffi；
//! - `--features test-stubs`：桥走库内状态桩（各子模块的
//!   [`node::stub`]/[`render::stub`]/[`undo::stub`]，纯 Rust、无
//!   `#[no_mangle]`，与真实 crate 的导出不冲突）——像素路径可在无
//!   真实 GL/GPU 的环境下跑通。两种形态的调用面完全一致。

pub mod node;
pub mod render;
pub mod undo;
