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

//! FFI 出口层（ffi.rs）契约测试：host.h 面。
//!
//! 每个导出函数至少一正常一错误路径（项目铁律）。
//! 宿主单例无锁：全部用例经 [`common::with_host`] 串行化。
//! 需要最小测试插件（build.rs 编，common::test_plugin_dir 装配）；
//! 不可用时 skip。

mod common;

use std::ffi::{c_char, c_int, CStr, CString};

use oakplugin::ffi::{
	oakplugin_host_init, oakplugin_host_plugin_count, oakplugin_host_plugin_id_at,
	oakplugin_host_plugin_label, oakplugin_host_scan, oakplugin_host_set_message_handler,
	oakplugin_host_shutdown,
};

const E_INVALID: i32 = -90001;
const E_NOT_FOUND: i32 = -90004;
const OK: i32 = 0;

fn cs(s: &str) -> CString {
	CString::new(s).unwrap()
}

/// 扫描测试插件；失败/缺失返回 false（调用方 skip）。
fn scan_test_plugin() -> bool {
	let Some(dir) = common::test_plugin_scan_dir() else {
		common::skip("最小测试插件未构建");
		return false;
	};
	let dir = cs(dir.to_str().unwrap());
	let dirs = [dir.as_ptr()];
	unsafe { oakplugin_host_scan(dirs.as_ptr(), 1) == OK }
}

const TEST_PLUGIN_ID: &str = "org.oak.test-plugin";

/// init/shutdown 幂等：重复 init 无副作用；shutdown 后再 init 可用。
#[test]
fn host_init_shutdown_idempotent() {
	common::with_host(|| {
		assert_eq!(unsafe { oakplugin_host_init() }, OK);
		assert_eq!(unsafe { oakplugin_host_init() }, OK);
		unsafe { oakplugin_host_shutdown() };
		assert_eq!(unsafe { oakplugin_host_init() }, OK);
		unsafe { oakplugin_host_shutdown() };
	});
}

/// scan 默认路径（NULL/0）：返回 OAKPLUGIN_OK 或 E_FAILED（无插件
/// 目录的机器），二者之外不允许别的码。
#[test]
fn host_scan_default_paths() {
	common::with_host(|| {
		unsafe { oakplugin_host_init() };
		let r = unsafe { oakplugin_host_scan(std::ptr::null(), 0) };
		assert!(r == OK || r == -90003, "scan(NULL,0) = {r}");
		unsafe { oakplugin_host_shutdown() };
	});
}

/// scan 指定目录（测试插件所在）：插件计数 ≥1 且能找到测试插件
/// 标识；目录不存在返回 E_FAILED 而不崩。
#[test]
fn host_scan_explicit_dir() {
	common::with_host(|| {
		unsafe { oakplugin_host_init() };
		assert!(scan_test_plugin(), "测试插件扫描失败");
		assert!(unsafe { oakplugin_host_plugin_count() } >= 1);

		// 标识可见（两段式第一段）。
		let len = unsafe { oakplugin_host_plugin_id_at(0, std::ptr::null_mut(), 0) };
		assert!(len > 0);
		let mut buf = vec![0u8; len as usize];
		let r = unsafe { oakplugin_host_plugin_id_at(0, buf.as_mut_ptr() as *mut c_char, len) };
		assert_eq!(r, OK);
		assert_eq!(
			unsafe { CStr::from_ptr(buf.as_ptr() as *const c_char) }.to_str().unwrap(),
			TEST_PLUGIN_ID
		);

		// 目录不存在：静默跳过返回 OK 而不崩（C++ olivehost.cpp:59-62
		// 的 add_plugin_path 语义——声明原期望 E_FAILED，与参照系
		// 不符，以 C++ 行为为准）。
		let nope = cs("/nonexistent/ofx/plugins");
		let dirs = [nope.as_ptr()];
		assert_eq!(unsafe { oakplugin_host_scan(dirs.as_ptr(), 1) }, OK);
		unsafe { oakplugin_host_shutdown() };
	});
}

/// plugin_id_at：两段式（先问长度再取内容）；越界索引返回
/// E_NOT_FOUND；缓冲区不足时返回所需长度且不越界写。
#[test]
fn host_plugin_id_at_two_stage() {
	common::with_host(|| {
		unsafe { oakplugin_host_init() };
		if !scan_test_plugin() {
			unsafe { oakplugin_host_shutdown() };
			return;
		}
		// 越界 → E_NOT_FOUND。
		assert_eq!(
			unsafe { oakplugin_host_plugin_id_at(99, std::ptr::null_mut(), 0) },
			E_NOT_FOUND
		);

		// 缓冲区不足：返回所需长度（> 0），不越界写（哨兵在 [4]——
		// [3] 是 4 字节缓冲内的 NUL 位）。
		let mut small = [0x7fu8; 5];
		let r = unsafe { oakplugin_host_plugin_id_at(0, small.as_mut_ptr() as *mut c_char, 4) };
		assert!(r > 4);
		assert_eq!(small[4], 0x7f, "缓冲区外不应被写");
		unsafe { oakplugin_host_shutdown() };
	});
}

/// plugin_label：已知插件返回非空 label；未知标识 E_NOT_FOUND；
/// NULL 参数 E_INVALID。
#[test]
fn host_plugin_label_paths() {
	common::with_host(|| {
		unsafe { oakplugin_host_init() };
		if !scan_test_plugin() {
			unsafe { oakplugin_host_shutdown() };
			return;
		}
		let id = cs(TEST_PLUGIN_ID);
		let len = unsafe { oakplugin_host_plugin_label(id.as_ptr(), std::ptr::null_mut(), 0) };
		assert!(len > 0);
		let mut buf = vec![0u8; len as usize];
		assert_eq!(
			unsafe { oakplugin_host_plugin_label(id.as_ptr(), buf.as_mut_ptr() as *mut c_char, len) },
			OK
		);
		assert!(!unsafe { CStr::from_ptr(buf.as_ptr() as *const c_char) }.to_bytes().is_empty());

		// 未知标识。
		let nope = cs("org.oak.nope");
		assert_eq!(
			unsafe { oakplugin_host_plugin_label(nope.as_ptr(), std::ptr::null_mut(), 0) },
			E_NOT_FOUND
		);
		// NULL 参数。
		assert_eq!(
			unsafe { oakplugin_host_plugin_label(std::ptr::null(), std::ptr::null_mut(), 0) },
			E_INVALID
		);
		unsafe { oakplugin_host_shutdown() };
	});
}

/// message handler：注册后测试插件的 message 回调到达（级别与
/// 文本透传，含 %d 格式化）；注册 NULL 后消息被丢弃而不崩。
#[test]
fn host_message_handler_dispatch() {
	common::with_host(|| {
		use oakplugin::ffi::{oakplugin_instance_create, oakplugin_instance_free};

		unsafe extern "C" fn capture(
			type_: *const c_char,
			message: *const c_char,
			userdata: *mut std::ffi::c_void,
		) -> c_int {
			let v = unsafe { &mut *(userdata as *mut Vec<(String, String)>) };
			v.push((
				unsafe { CStr::from_ptr(type_) }.to_string_lossy().into_owned(),
				unsafe { CStr::from_ptr(message) }.to_string_lossy().into_owned(),
			));
			0
		}

		unsafe { oakplugin_host_init() };
		if !scan_test_plugin() {
			unsafe { oakplugin_host_shutdown() };
			return;
		}

		// 注册捕获器 → 创建实例（插件在 createInstance 发 message）。
		let mut captured: Vec<(String, String)> = Vec::new();
		unsafe {
			oakplugin_host_set_message_handler(
				Some(capture),
				&mut captured as *mut _ as *mut std::ffi::c_void,
			)
		};
		let id = cs(TEST_PLUGIN_ID);
		let mut h = unsafe { oakplugin_instance_create(id.as_ptr()) };
		assert!(!h.is_null());
		assert_eq!(captured.len(), 1);
		assert_eq!(captured[0].0, "OfxMessageMessage");
		assert_eq!(captured[0].1, "created id=7", "v1 变长格式化应生效");

		// 注销 → 再创建不崩、无捕获。
		unsafe { oakplugin_host_set_message_handler(None, std::ptr::null_mut()) };
		captured.clear();
		let mut h2 = unsafe { oakplugin_instance_create(id.as_ptr()) };
		assert!(!h2.is_null());
		assert!(captured.is_empty());
		unsafe { oakplugin_instance_free(&mut h2) };
		unsafe { oakplugin_instance_free(&mut h) };
		unsafe { oakplugin_host_shutdown() };
	});
}
