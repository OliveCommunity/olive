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

//! 错误码。与 `include/*/error.h` 逐字对应；项目统一 -MMCCCC 方案
//! （模块号注册表见 include/common/error.h），跨模块透传不翻译。

/// 成功。
pub const OAKPLUGIN_OK: i32 = 0;
/// 空句柄或非法参数。
pub const OAKPLUGIN_E_INVALID: i32 = -90001;
/// 当前状态不允许该调用（如未扫描就创建实例）。
pub const OAKPLUGIN_E_STATE: i32 = -90002;
/// 底层操作失败（OFX action 返回非 kOfxStatOK、插件入口拒绝等）。
pub const OAKPLUGIN_E_FAILED: i32 = -90003;
/// 索引越界 / 指定标识的插件不存在。
pub const OAKPLUGIN_E_NOT_FOUND: i32 = -90004;
/// 分配失败。
pub const OAKPLUGIN_E_NOMEM: i32 = -90005;

/// crate 内部统一的结果类型；FFI 层把它映射为上述 i32 码。
pub type Result<T> = std::result::Result<T, Error>;

/// crate 内部错误。`code()` 给出对外错误码。
#[derive(Debug)]
pub enum Error {
	/// 空句柄或非法参数。
	Invalid,
	/// 状态不允许。
	State,
	/// 底层失败，附人类可读上下文（仅日志，不出界）。
	Failed(String),
	/// 未找到。
	NotFound,
	/// 分配失败。
	NoMem,
}

impl Error {
	/// 映射为 `include/plugin/error.h` 的错误码。
	pub fn code(&self) -> i32 {
		match self {
			Error::Invalid => OAKPLUGIN_E_INVALID,
			Error::State => OAKPLUGIN_E_STATE,
			Error::Failed(_) => OAKPLUGIN_E_FAILED,
			Error::NotFound => OAKPLUGIN_E_NOT_FOUND,
			Error::NoMem => OAKPLUGIN_E_NOMEM,
		}
	}
}
