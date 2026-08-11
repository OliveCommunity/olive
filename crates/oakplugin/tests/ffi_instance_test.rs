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

//! FFI 出口层契约测试：instance.h 面（含 M11 §2.1 内省族）。
//!
//! 宿主单例无锁：全部用例经 [`common::with_host`] 串行化。
//! render 路径经 oakrender 测试桩（`--features test-stubs`）；
//! 未启用时相关用例 skip。

mod common;

use std::ffi::{c_char, c_int, c_void, CStr, CString};

use oakplugin::ffi::{
	node_value_type, oakplugin_debug_alive_count, oakplugin_instance_cancel,
	oakplugin_instance_clip_count, oakplugin_instance_clip_info, oakplugin_instance_clip_name,
	oakplugin_instance_create, oakplugin_instance_free, oakplugin_instance_get_param,
	oakplugin_instance_get_param_string, oakplugin_instance_param_choice_count,
	oakplugin_instance_param_choice_label, oakplugin_instance_param_count,
	oakplugin_instance_param_default_double, oakplugin_instance_param_default_string,
	oakplugin_instance_param_display_range, oakplugin_instance_param_hint,
	oakplugin_instance_param_label, oakplugin_instance_param_name, oakplugin_instance_param_parent,
	oakplugin_instance_param_secret, oakplugin_instance_param_type, oakplugin_instance_render,
	oakplugin_instance_set_param, oakplugin_instance_set_param_string,
	oakplugin_instance_set_progress_cb, OakNodeValue,
};
use oakplugin::handle::CHandle;

const E_INVALID: i32 = -90001;
const E_NOT_FOUND: i32 = -90004;
const OK: i32 = 0;

fn cs(s: &str) -> CString {
	CString::new(s).unwrap()
}

const TEST_PLUGIN_ID: &str = "org.oak.test-plugin";

/// 扫描测试插件并创建实例；不可用返回空句柄。
fn create_instance() -> CHandle {
	if common::test_plugin_scan_dir().is_none() {
		common::skip("最小测试插件未构建");
		return CHandle::null();
	}
	let dir = cs(common::test_plugin_scan_dir().unwrap().to_str().unwrap());
	let dirs = [dir.as_ptr()];
	unsafe { oakplugin_host_scan_import(dirs.as_ptr(), 1) };
	let id = cs(TEST_PLUGIN_ID);
	unsafe { oakplugin_instance_create(id.as_ptr()) }
}

// 避免重复 import host_scan（ffi 模块内路径统一）。
use oakplugin::ffi::oakplugin_host_scan as oakplugin_host_scan_import;

/// 两段式读字符串。
fn two_stage(
	get: unsafe extern "C" fn(CHandle, c_int, *mut c_char, c_int) -> c_int,
	h: CHandle,
	index: c_int,
) -> Option<String> {
	let len = unsafe { get(h, index, std::ptr::null_mut(), 0) };
	if len <= 0 {
		return None;
	}
	let mut buf = vec![0u8; len as usize];
	let r = unsafe { get(h, index, buf.as_mut_ptr() as *mut c_char, len) };
	if r != OK {
		return None;
	}
	Some(
		unsafe { CStr::from_ptr(buf.as_ptr() as *const c_char) }
			.to_string_lossy()
			.into_owned(),
	)
}

/// 简易实例句柄（测试桩纹理用）。
fn fake_texture(ctx: usize) -> CHandle {
	CHandle {
		ctx: ctx as *mut c_void,
		addref: None,
		release: None,
		abi_version: 1,
	}
}

/// create/free：已知插件返回非空句柄；free 后 alive 回基线；
/// free(NULL)/free(空) no-op。
#[test]
fn instance_create_free() {
	common::with_host(|| {
		unsafe { oakplugin_host_scan_import(std::ptr::null(), 0) };
		let base = unsafe { oakplugin_debug_alive_count() };
		let mut h = create_instance();
		assert!(!h.is_null());
		assert_eq!(unsafe { oakplugin_debug_alive_count() }, base + 1);
		unsafe { oakplugin_instance_free(&mut h) };
		assert_eq!(unsafe { oakplugin_debug_alive_count() }, base);
		// free(NULL)/free(空) no-op。
		unsafe { oakplugin_instance_free(std::ptr::null_mut()) };
		unsafe { oakplugin_instance_free(&mut h) };
	});
}

/// create 未知标识：返回空句柄（init 失败返回空，契约 §a）。
#[test]
fn instance_create_unknown_id() {
	common::with_host(|| {
		let id = cs("org.oak.does-not-exist");
		let h = unsafe { oakplugin_instance_create(id.as_ptr()) };
		assert!(h.is_null());
	});
}

/// set/get_param（double）：round-trip 一致；未知参数名
/// E_NOT_FOUND；空句柄 E_INVALID；out 为 NULL 时 E_INVALID。
#[test]
fn instance_param_double_roundtrip() {
	common::with_host(|| {
		let mut h = create_instance();
		if h.is_null() {
			return;
		}
		let name = cs("gain");
		let mut v = OakNodeValue::default();
		v.r#type = node_value_type::FLOAT;
		v.f = [1.25, 0.0, 0.0, 0.0];
		assert_eq!(
			unsafe { oakplugin_instance_set_param(h, name.as_ptr(), &v) },
			OK
		);
		let mut out = OakNodeValue::default();
		assert_eq!(
			unsafe { oakplugin_instance_get_param(h, name.as_ptr(), &mut out) },
			OK
		);
		assert_eq!(out.r#type, node_value_type::FLOAT);
		assert_eq!(out.f[0], 1.25);

		// 未知参数名 → E_NOT_FOUND。
		let nope = cs("nope");
		assert_eq!(
			unsafe { oakplugin_instance_set_param(h, nope.as_ptr(), &v) },
			E_NOT_FOUND
		);
		// 空句柄 → E_INVALID。
		assert_eq!(
			unsafe { oakplugin_instance_set_param(CHandle::null(), name.as_ptr(), &v) },
			E_INVALID
		);
		// out NULL → E_INVALID。
		assert_eq!(
			unsafe { oakplugin_instance_get_param(h, name.as_ptr(), std::ptr::null_mut()) },
			E_INVALID
		);
		unsafe { oakplugin_instance_free(&mut h) };
	});
}

/// set/get_param_string：round-trip 一致（含空串与长串两段式）。
#[test]
fn instance_param_string_roundtrip() {
	common::with_host(|| {
		let mut h = create_instance();
		if h.is_null() {
			return;
		}
		let name = cs("label");
		let value = cs("hello 你好");
		assert_eq!(
			unsafe { oakplugin_instance_set_param_string(h, name.as_ptr(), value.as_ptr()) },
			OK
		);
		// 两段式。
		let len = unsafe {
			oakplugin_instance_get_param_string(h, name.as_ptr(), std::ptr::null_mut(), 0)
		};
		assert!(len > 0);
		let mut buf = vec![0u8; len as usize];
		assert_eq!(
			unsafe {
				oakplugin_instance_get_param_string(
					h,
					name.as_ptr(),
					buf.as_mut_ptr() as *mut c_char,
					len,
				)
			},
			OK
		);
		assert_eq!(
			unsafe { CStr::from_ptr(buf.as_ptr() as *const c_char) }.to_string_lossy(),
			"hello 你好"
		);
		// 空串。
		let empty = cs("");
		assert_eq!(
			unsafe { oakplugin_instance_set_param_string(h, name.as_ptr(), empty.as_ptr()) },
			OK
		);
		assert_eq!(
			unsafe {
				oakplugin_instance_get_param_string(h, name.as_ptr(), std::ptr::null_mut(), 0)
			},
			1
		);
		unsafe { oakplugin_instance_free(&mut h) };
	});
}

/// render：对测试插件渲一帧，输出纹理像素符合插件的常量填充
/// （0.5 RGBA F32，alpha=1）。time 为 NaN 时 E_INVALID。
#[cfg(feature = "test-stubs")]
#[test]
fn instance_render_one_frame() {
	common::with_host(|| {
		use oakplugin::bridge::render::stub;
		let mut h = create_instance();
		if h.is_null() {
			return;
		}
		// 输出帧 320×240 F32。
		stub::setup_dst(320, 240, oakplugin::bridge::render::PIXEL_FORMAT_F32);
		let dst = fake_texture(0xA1);
		let src = CHandle::null(); // 无输入（插件只写常量）
		assert_eq!(unsafe { oakplugin_instance_render(h, dst, src, 0.0) }, OK);

		// 断言：常量 0.5，alpha 1.0（每像素 4 通道 × 4 字节 F32）。
		let pixels = stub::dst_pixels();
		let n = pixels.len() / 16;
		assert_eq!(n, 320 * 240, "像素数 = len/16 = {n}");
		for i in 0..n {
			// 像素基址 = i * 16（4 通道 × 4 字节）。
			let f = |o: usize| {
				f32::from_le_bytes(
					pixels[i * 16 + o * 4..i * 16 + o * 4 + 4]
						.try_into()
						.unwrap(),
				)
			};
			assert_eq!(f(0), 0.5, "pixel {i} r");
			assert_eq!(f(1), 0.5, "pixel {i} g");
			assert_eq!(f(2), 0.5, "pixel {i} b");
			assert_eq!(f(3), 1.0, "pixel {i} a");
		}
		unsafe { oakplugin_instance_free(&mut h) };
	});
}

/// render 默认构建（真实 oakrender 链接）：NULL 输出纹理在触达
/// oakrender 之前被插件层校验拒绝（E_INVALID），不崩溃。真实纹理
/// 路径需要 GL/GPU，属集成测试范畴。
#[cfg(not(feature = "test-stubs"))]
#[test]
fn instance_render_one_frame() {
	common::with_host(|| {
		let mut h = create_instance();
		if h.is_null() {
			return;
		}
		let dst = CHandle::null();
		assert_eq!(
			unsafe { oakplugin_instance_render(h, dst, CHandle::null(), 0.0) },
			-90001
		);
		unsafe { oakplugin_instance_free(&mut h) };
	});
}

/// progress 回调：render 期间被调用且进度单调不减；回调返回非 0
/// 时 render 以取消码结束（插件检查 progressUpdate 结果并中止）。
#[cfg(feature = "test-stubs")]
#[test]
fn instance_progress_callback() {
	common::with_host(|| {
		use oakplugin::bridge::render::stub;
		let mut h = create_instance();
		if h.is_null() {
			return;
		}
		let mut seen: Vec<f64> = Vec::new();
		unsafe extern "C" fn capture(p: f64, userdata: *mut c_void) -> c_int {
			let v = unsafe { &mut *(userdata as *mut Vec<f64>) };
			v.push(p);
			0
		}
		unsafe {
			oakplugin_instance_set_progress_cb(h, Some(capture), &mut seen as *mut _ as *mut c_void)
		};
		stub::setup_dst(64, 64, oakplugin::bridge::render::PIXEL_FORMAT_F32);
		let dst = fake_texture(0xA1);
		assert_eq!(
			unsafe { oakplugin_instance_render(h, dst, CHandle::null(), 0.0) },
			OK
		);
		assert_eq!(seen, vec![0.5], "插件在 render 内报一次 0.5");

		// 取消回调（非 0 = 中止）。
		unsafe extern "C" fn abort_cb(_p: f64, _u: *mut c_void) -> c_int {
			1
		}
		unsafe { oakplugin_instance_set_progress_cb(h, Some(abort_cb), std::ptr::null_mut()) };
		stub::setup_dst(64, 64, oakplugin::bridge::render::PIXEL_FORMAT_F32);
		let r = unsafe { oakplugin_instance_render(h, dst, CHandle::null(), 0.0) };
		assert_eq!(r, -90003, "取消应使 render 失败");
		unsafe { oakplugin_instance_free(&mut h) };
	});
}

/// cancel：无活动渲染时 no-op；置位后 render 立即以取消码返回。
#[test]
fn instance_cancel_semantics() {
	common::with_host(|| {
		let mut h = create_instance();
		if h.is_null() {
			return;
		}
		// 无活动渲染：OK no-op。
		assert_eq!(unsafe { oakplugin_instance_cancel(h) }, OK);
		// 置位后 render 入口即取消（先备好合法输出帧，确保走到
		// render 的取消检查而不是帧校验）。
		assert_eq!(unsafe { oakplugin_instance_cancel(h) }, OK);
		#[cfg(feature = "test-stubs")]
		oakplugin::bridge::render::stub::setup_dst(
			64,
			64,
			oakplugin::bridge::render::PIXEL_FORMAT_F32,
		);
		let dst = fake_texture(0xA1);
		assert_eq!(
			unsafe { oakplugin_instance_render(h, dst, CHandle::null(), 0.0) },
			-90003
		);
		unsafe { oakplugin_instance_free(&mut h) };
	});
}

/// 内省：param_count > 0；name/type/label/hint/parent 与插件定义
/// 一致；index 越界 E_NOT_FOUND。
#[test]
fn introspection_param_fields() {
	common::with_host(|| {
		let mut h = create_instance();
		if h.is_null() {
			return;
		}
		let count = unsafe { oakplugin_instance_param_count(h) };
		assert!(count >= 4, "gain/mode/debug/label，count={count}");

		// gain 的字段。
		assert_eq!(
			two_stage(oakplugin_instance_param_name, h, 0),
			Some("gain".into())
		);
		assert_eq!(
			two_stage(oakplugin_instance_param_type, h, 0),
			Some("OfxParamTypeDouble".into())
		);
		assert_eq!(
			two_stage(oakplugin_instance_param_label, h, 0),
			Some("Gain".into())
		);
		assert_eq!(
			two_stage(oakplugin_instance_param_hint, h, 0),
			Some(String::new())
		);
		assert_eq!(
			two_stage(oakplugin_instance_param_parent, h, 0),
			Some(String::new())
		);

		// 越界 → E_NOT_FOUND。
		assert_eq!(
			unsafe { oakplugin_instance_param_name(h, count, std::ptr::null_mut(), 0) },
			E_NOT_FOUND
		);
		unsafe { oakplugin_instance_free(&mut h) };
	});
}

/// 内省：secret/display_range/choice 三族在测试插件的对应参数上
/// 逐字段断言。
#[test]
fn introspection_secret_range_choice() {
	common::with_host(|| {
		let mut h = create_instance();
		if h.is_null() {
			return;
		}
		let count = unsafe { oakplugin_instance_param_count(h) };
		// 按名定位索引。
		let mut gain = -1;
		let mut mode = -1;
		let mut debug = -1;
		for i in 0..count {
			match two_stage(oakplugin_instance_param_name, h, i).as_deref() {
				Some("gain") => gain = i,
				Some("mode") => mode = i,
				Some("debug") => debug = i,
				_ => {}
			}
		}
		assert!(gain >= 0 && mode >= 0 && debug >= 0);

		// display range（gain：-2..2）。
		let (mut min, mut max) = (0.0, 0.0);
		assert_eq!(
			unsafe { oakplugin_instance_param_display_range(h, gain, &mut min, &mut max) },
			OK
		);
		assert_eq!((min, max), (-2.0, 2.0));

		// secret（debug = 1；gain = 0）。
		let mut s = 0;
		assert_eq!(
			unsafe { oakplugin_instance_param_secret(h, debug, &mut s) },
			OK
		);
		assert_eq!(s, 1);
		assert_eq!(
			unsafe { oakplugin_instance_param_secret(h, gain, &mut s) },
			OK
		);
		assert_eq!(s, 0);

		// choice（mode：2 个选项；选项内容见 introspection_choice_labels）。
		assert_eq!(unsafe { oakplugin_instance_param_choice_count(h, mode) }, 2);
		unsafe { oakplugin_instance_free(&mut h) };
	});
}

/// choice label/value 的带选项两段式。
#[test]
fn introspection_choice_labels() {
	common::with_host(|| {
		let mut h = create_instance();
		if h.is_null() {
			return;
		}
		let count = unsafe { oakplugin_instance_param_count(h) };
		let mut mode = -1;
		for i in 0..count {
			if two_stage(oakplugin_instance_param_name, h, i).as_deref() == Some("mode") {
				mode = i;
			}
		}
		assert!(mode >= 0);
		assert_eq!(unsafe { oakplugin_instance_param_choice_count(h, mode) }, 2);
		for c in 0..2 {
			let len = unsafe {
				oakplugin_instance_param_choice_label(h, mode, c, std::ptr::null_mut(), 0)
			};
			assert!(len > 0);
			let mut buf = vec![0u8; len as usize];
			assert_eq!(
				unsafe {
					oakplugin_instance_param_choice_label(
						h,
						mode,
						c,
						buf.as_mut_ptr() as *mut c_char,
						len,
					)
				},
				OK
			);
			let s = unsafe { CStr::from_ptr(buf.as_ptr() as *const c_char) }
				.to_string_lossy()
				.into_owned();
			assert!(s == "Fast" || s == "High", "选项 {c} = {s}");
		}
		unsafe { oakplugin_instance_free(&mut h) };
	});
}

/// 内省：默认值（double 族与字符串）。
#[test]
fn introspection_defaults() {
	common::with_host(|| {
		let mut h = create_instance();
		if h.is_null() {
			return;
		}
		let count = unsafe { oakplugin_instance_param_count(h) };
		let mut gain = -1;
		let mut label = -1;
		for i in 0..count {
			match two_stage(oakplugin_instance_param_name, h, i).as_deref() {
				Some("gain") => gain = i,
				Some("label") => label = i,
				_ => {}
			}
		}
		let mut d = 99.0;
		assert_eq!(
			unsafe { oakplugin_instance_param_default_double(h, gain, 0, &mut d) },
			OK
		);
		assert_eq!(d, 0.0);
		let s = two_stage(oakplugin_instance_param_default_string, h, label);
		assert_eq!(s, Some(String::new()));
		unsafe { oakplugin_instance_free(&mut h) };
	});
}

/// 内省：clip_count/name/info（含 Output clip；optional 标记）。
#[test]
fn introspection_clips() {
	common::with_host(|| {
		let mut h = create_instance();
		if h.is_null() {
			return;
		}
		assert_eq!(unsafe { oakplugin_instance_clip_count(h) }, 2);
		let names: Vec<String> = (0..2)
			.map(|i| two_stage(oakplugin_instance_clip_name, h, i).unwrap_or_default())
			.collect();
		assert!(names.contains(&"Source".into()), "{names:?}");
		assert!(names.contains(&"Output".into()), "{names:?}");

		for i in 0..2 {
			let mut optional = -1;
			let len = unsafe {
				oakplugin_instance_clip_info(h, i, &mut optional, std::ptr::null_mut(), 0)
			};
			assert!(len > 0);
			let mut buf = vec![0u8; len as usize];
			assert_eq!(
				unsafe {
					oakplugin_instance_clip_info(
						h,
						i,
						&mut optional,
						buf.as_mut_ptr() as *mut c_char,
						len,
					)
				},
				OK
			);
			assert!(optional == 0, "测试插件 clip 非 optional");
		}
		unsafe { oakplugin_instance_free(&mut h) };
	});
}

/// debug_alive_count：create 增、free 减；全部释放后回零基线。
#[test]
fn alive_count_accounting() {
	common::with_host(|| {
		unsafe { oakplugin_host_scan_import(std::ptr::null(), 0) };
		let base = unsafe { oakplugin_debug_alive_count() };
		let mut h = create_instance();
		if h.is_null() {
			return;
		}
		assert_eq!(unsafe { oakplugin_debug_alive_count() }, base + 1);
		unsafe { oakplugin_instance_free(&mut h) };
		assert_eq!(unsafe { oakplugin_debug_alive_count() }, base);
	});
}
