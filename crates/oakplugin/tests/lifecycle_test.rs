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

//! 生命周期测试：实例创建/销毁配对、重复扫描、渲染中取消、
//! alive 泄漏断言。对应 M11 §3.5 验收线。
//!
//! 宿主单例无锁：全部用例经 [`common::with_host`] 串行化。

mod common;

use std::ffi::{c_char, c_int, c_void, CString};

use oakplugin::ffi::{
	oakplugin_debug_alive_count, oakplugin_host_init, oakplugin_host_plugin_count,
	oakplugin_host_scan, oakplugin_host_shutdown, oakplugin_instance_cancel,
	oakplugin_instance_create, oakplugin_instance_free, oakplugin_instance_render,
	oakplugin_instance_set_progress_cb,
};
use oakplugin::handle::CHandle;

const OK: i32 = 0;
const E_FAILED: i32 = -90003;

fn cs(s: &str) -> CString {
	CString::new(s).unwrap()
}

const TEST_PLUGIN_ID: &str = "org.oak.test-plugin";

fn fake_texture(ctx: usize) -> CHandle {
	CHandle {
		ctx: ctx as *mut c_void,
		addref: None,
		release: None,
		abi_version: 1,
	}
}

/// 扫描并创建实例；不可用返回空句柄。
fn create_instance() -> CHandle {
	if common::test_plugin_scan_dir().is_none() {
		common::skip("最小测试插件未构建");
		return CHandle::null();
	}
	let dir = cs(common::test_plugin_scan_dir().unwrap().to_str().unwrap());
	let dirs = [dir.as_ptr()];
	unsafe { oakplugin_host_scan(dirs.as_ptr(), 1) };
	let id = cs(TEST_PLUGIN_ID);
	unsafe { oakplugin_instance_create(id.as_ptr()) }
}

/// createInstance → destroyInstance 严格配对：alive 计数回到基线。
#[test]
fn instance_create_destroy_pairing() {
	common::with_host(|| {
		unsafe { oakplugin_host_init() };
		let base = unsafe { oakplugin_debug_alive_count() };
		let mut h = create_instance();
		if h.is_null() {
			unsafe { oakplugin_host_shutdown() };
			return;
		}
		assert_eq!(unsafe { oakplugin_debug_alive_count() }, base + 1);
		unsafe { oakplugin_instance_free(&mut h) };
		assert_eq!(unsafe { oakplugin_debug_alive_count() }, base, "destroy 后 alive 必须回基线");
		unsafe { oakplugin_host_shutdown() };
	});
}

/// 重复 scan 同一路径：插件列表不重复、顺序稳定（缓存去重语义，
/// HS: ofxhPluginCache.cpp）。
#[test]
fn rescan_is_idempotent() {
	common::with_host(|| {
		unsafe { oakplugin_host_init() };
		if common::test_plugin_scan_dir().is_none() {
			common::skip("最小测试插件未构建");
			unsafe { oakplugin_host_shutdown() };
			return;
		}
		let dir = cs(common::test_plugin_scan_dir().unwrap().to_str().unwrap());
		let dirs = [dir.as_ptr()];
		assert_eq!(unsafe { oakplugin_host_scan(dirs.as_ptr(), 1) }, OK);
		let first = unsafe { oakplugin_host_plugin_count() };
		assert!(first >= 1);
		// 再扫两次：数量不变。
		assert_eq!(unsafe { oakplugin_host_scan(dirs.as_ptr(), 1) }, OK);
		assert_eq!(unsafe { oakplugin_host_scan(dirs.as_ptr(), 1) }, OK);
		assert_eq!(unsafe { oakplugin_host_plugin_count() }, first, "重复扫描不得重复加载");
		unsafe { oakplugin_host_shutdown() };
	});
}

/// render 中途取消：进度回调返回非 0 后，插件的 progressUpdate 收到
/// 取消并中止 render；输出纹理不写半帧（测试插件在取消路径返回
/// Failed，宿主不落帧）。
#[cfg(feature = "test-stubs")]
#[test]
fn render_cancellation_is_atomic() {
	common::with_host(|| {
		use oakplugin::bridge::render::stub;
		unsafe { oakplugin_host_init() };
		let mut h = create_instance();
		if h.is_null() {
			unsafe { oakplugin_host_shutdown() };
			return;
		}
		unsafe extern "C" fn abort_cb(_p: f64, _u: *mut c_void) -> c_int {
			1
		}
		unsafe { oakplugin_instance_set_progress_cb(h, Some(abort_cb), std::ptr::null_mut()) };

		// 预置输出帧并填 0xAA 哨兵：取消路径不得写半帧。
		stub::setup_dst(64, 64, oakplugin::bridge::render::PIXEL_FORMAT_F32);
		let before = stub::dst_pixels();
		assert!(!before.is_empty());
		let dst = fake_texture(0xA1);
		let r = unsafe { oakplugin_instance_render(h, dst, CHandle::null(), 0.0) };
		assert_eq!(r, E_FAILED, "取消应使 render 失败");
		// 全有或全无：失败时不写。
		assert_eq!(stub::dst_pixels(), before, "取消后输出不得被写");
		unsafe { oakplugin_instance_free(&mut h) };
		unsafe { oakplugin_host_shutdown() };
	});
}

/// 压测：同一插件 256 次 create/render/destroy 循环后 alive 回到
/// 基线（当年内存问题的回归防线）。
#[cfg(feature = "test-stubs")]
#[test]
fn create_render_destroy_loop_no_leak() {
	common::with_host(|| {
		use oakplugin::bridge::render::stub;
		unsafe { oakplugin_host_init() };
		let base = unsafe { oakplugin_debug_alive_count() };
		if common::test_plugin_scan_dir().is_none() {
			common::skip("最小测试插件未构建");
			unsafe { oakplugin_host_shutdown() };
			return;
		}
		stub::setup_dst(16, 16, oakplugin::bridge::render::PIXEL_FORMAT_F32);
		let dst = fake_texture(0xA1);
		for i in 0..256 {
			let mut h = create_instance();
			assert!(!h.is_null());
			let r = unsafe { oakplugin_instance_render(h, dst, CHandle::null(), i as f64) };
			assert_eq!(r, OK);
			unsafe { oakplugin_instance_free(&mut h) };
			assert_eq!(
				unsafe { oakplugin_debug_alive_count() },
				base,
				"第 {i} 次循环后泄漏"
			);
		}
		unsafe { oakplugin_host_shutdown() };
	});
}

/// host_shutdown 时仍有活实例：宿主发 destroy 通知并卸载 bundle，
/// 之后该实例的任何 render 立即取消（不碰已卸载入口）；再 create
/// 返回空句柄（缓存已清空——声明原期望 E_STATE，本实现的 create
/// 契约是"失败返回空句柄"，以实例化为准）。
#[test]
fn shutdown_with_live_instances() {
	common::with_host(|| {
		unsafe { oakplugin_host_init() };
		let mut h = create_instance();
		if h.is_null() {
			unsafe { oakplugin_host_shutdown() };
			return;
		}
		// 带活实例 shutdown：不崩；实例被打取消标记。
		unsafe { oakplugin_host_shutdown() };
		// C 侧句柄仍持 Arc：实例内存存活，但 render 必须失败。
		let dst = fake_texture(0xA1);
		assert_eq!(unsafe { oakplugin_instance_render(h, dst, CHandle::null(), 0.0) }, E_FAILED);
		unsafe { oakplugin_instance_free(&mut h) };
		assert_eq!(unsafe { oakplugin_debug_alive_count() }, 0, "句柄释放后归零");
		// 之后再 create：插件已卸载 → 空句柄。
		let id = cs(TEST_PLUGIN_ID);
		let h2 = unsafe { oakplugin_instance_create(id.as_ptr()) };
		assert!(h2.is_null());
	});
}

/// 多实例并发：同一插件 16 实例并发 render（宿主侧不串——插件
/// 全局状态由插件自保）。
#[cfg(feature = "test-stubs")]
#[test]
fn concurrent_instances_render() {
	common::with_host(|| {
		use oakplugin::bridge::render::stub;
		unsafe { oakplugin_host_init() };
		if common::test_plugin_scan_dir().is_none() {
			common::skip("最小测试插件未构建");
			unsafe { oakplugin_host_shutdown() };
			return;
		}
		// 16 个实例（句柄数组；经线程并发 render）。
		let mut handles: Vec<CHandle> = (0..16)
			.map(|_| create_instance())
			.collect();
		assert!(handles.iter().all(|h| !h.is_null()));
		stub::setup_dst(32, 32, oakplugin::bridge::render::PIXEL_FORMAT_F32);
		let dst = fake_texture(0xA1);
		let threads: Vec<_> = handles
			.iter()
			.enumerate()
			.map(|(i, h)| {
				let h = *h;
				std::thread::spawn(move || unsafe {
					oakplugin_instance_render(h, dst, CHandle::null(), i as f64)
				})
			})
			.collect();
		for t in threads {
			assert_eq!(t.join().unwrap(), OK);
		}
		for h in handles.iter_mut() {
			unsafe { oakplugin_instance_free(h) };
		}
		assert_eq!(unsafe { oakplugin_debug_alive_count() }, 0);
		unsafe { oakplugin_host_shutdown() };
	});
}
