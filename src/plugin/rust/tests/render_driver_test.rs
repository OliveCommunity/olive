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

//! render 驱动测试（M11 §4）：pluginrenderer.cpp 语义收编的 CPU 路径
//! ——序列括号、render_job 多输入/参数覆盖、isIdentity 短路、输出
//! 装配像素断言。GL 路径见 gl_render_test.rs。

mod common;

use std::ffi::{c_char, c_int, c_void, CString};

use oakplugin::ffi::{
	oakplugin_host_scan, oakplugin_instance_create, oakplugin_instance_free,
	oakplugin_instance_get_param, oakplugin_instance_render_begin_sequence,
	oakplugin_instance_render_end_sequence, oakplugin_instance_render_job,
	OakPluginJobTexture, OakPluginJobValue, OakNodeValue, node_value_type,
};
use oakplugin::handle::CHandle;

const OK: i32 = 0;
const TEST_PLUGIN_ID: &str = "org.oak.test-plugin";
const IDENTITY_PLUGIN_ID: &str = "org.oak.test-plugin.identity";

fn cs(s: &str) -> CString {
	CString::new(s).unwrap()
}

fn fake_texture(ctx: usize) -> CHandle {
	CHandle {
		ctx: ctx as *mut c_void,
		addref: None,
		release: None,
		abi_version: 1,
	}
}

fn scan_and_create(id: &str) -> CHandle {
	if common::test_plugin_scan_dir().is_none() {
		common::skip("最小测试插件未构建");
		return CHandle::null();
	}
	let dir = cs(common::test_plugin_scan_dir().unwrap().to_str().unwrap());
	let dirs = [dir.as_ptr()];
	unsafe { oakplugin_host_scan(dirs.as_ptr(), 1) };
	let id = cs(id);
	unsafe { oakplugin_instance_create(id.as_ptr()) }
}

/// 序列括号 + render_job（CPU）：begin → job → end；输出像素为测试
/// 插件常量填充（0.5 RGBA F32，alpha=1）；RoI 计算成功。
#[cfg(feature = "test-stubs")]
#[test]
fn render_job_cpu_path_and_sequence_brackets() {
	use oakplugin::bridge::render::stub;
	common::with_host(|| {
		stub::reset();
		let mut h = scan_and_create(TEST_PLUGIN_ID);
		if h.is_null() {
			return;
		}
		stub::setup_dst(8, 4, oakplugin::bridge::render::PIXEL_FORMAT_F32);
		let dst = fake_texture(0xA1);

		// 未 begin 序列直接 job 也可用（单帧；序列括号是优化语义）。
		assert_eq!(
			unsafe { oakplugin_instance_render_job(h, dst, 0.0, 0, 0, std::ptr::null(), CHandle::null(), std::ptr::null(), 0, std::ptr::null(), 0, CHandle::null()) },
			OK
		);
		// begin/end 括号配对。
		assert_eq!(unsafe { oakplugin_instance_render_begin_sequence(h, 0.0, 10.0, 0) }, OK);
		assert_eq!(unsafe { oakplugin_instance_render_end_sequence(h, 0.0, 10.0, 0) }, OK);

		// 像素断言：8×4 常量 0.5 / alpha 1。
		let pixels = stub::dst_pixels();
		let n = pixels.len() / 16;
		assert_eq!(n, 32);
		for i in 0..n {
			let f = |o: usize| f32::from_le_bytes(pixels[i * 16 + o * 4..i * 16 + o * 4 + 4].try_into().unwrap());
			assert_eq!(f(0), 0.5, "pixel {i} r");
			assert_eq!(f(3), 1.0, "pixel {i} a");
		}
		unsafe { oakplugin_instance_free(&mut h) };
	});
}

/// 多输入 + 参数覆盖：inputs 表按 clip 名挂输入纹理；values 覆盖
/// gain 参数（驱动注入实例参数，插件经 param 读取——测试插件 render
/// 不读 gain，故断言回写成功即可）。
#[cfg(feature = "test-stubs")]
#[test]
fn render_job_multi_input_and_param_overrides() {
	use oakplugin::bridge::render::stub;
	common::with_host(|| {
		stub::reset();
		let mut h = scan_and_create(TEST_PLUGIN_ID);
		if h.is_null() {
			return;
		}
		stub::setup_dst(4, 4, oakplugin::bridge::render::PIXEL_FORMAT_F32);
		stub::setup_src(4, 4, oakplugin::bridge::render::PIXEL_FORMAT_F32, vec![0u8; 4 * 4 * 16]);
		let dst = fake_texture(0xA1);
		let src = fake_texture(0xA2);

		// 参数覆盖：gain = 1.5（FLOAT）。
		let mut value = OakNodeValue::default();
		value.r#type = node_value_type::FLOAT;
		value.f = [1.5, 0.0, 0.0, 0.0];
		let gain = cs("gain");
		let job_values = [OakPluginJobValue {
			key: gain.as_ptr(),
			value,
		}];

		// 多输入表：Source → src 纹理。
		let source = cs("Source");
		let job_inputs = [OakPluginJobTexture {
			clip: source.as_ptr(),
			texture: src,
		}];

		assert_eq!(
			unsafe {
				oakplugin_instance_render_job(
					h,
					dst,
					0.0,
					0,
					0,
					std::ptr::null(),
					CHandle::null(),
					job_inputs.as_ptr(),
					1,
					job_values.as_ptr(),
					1,
					CHandle::null(),
				)
			},
			OK
		);

		// 覆盖已生效：经 C ABI 读回 gain。
		let mut out = OakNodeValue::default();
		let g = cs("gain");
		assert_eq!(unsafe { oakplugin_instance_get_param(h, g.as_ptr(), &mut out) }, OK);
		assert_eq!(out.f[0], 1.5, "gain 覆盖应已注入实例参数");

		unsafe { oakplugin_instance_free(&mut h) };
	});
}

/// isIdentity 短路：identity 变体插件返回 "Source" → render_job 不调
/// render action，直接把 Source 的帧拷入输出（像素与输入一致）。
#[cfg(feature = "test-stubs")]
#[test]
fn render_job_is_identity_shortcircuit() {
	use oakplugin::bridge::render::stub;
	common::with_host(|| {
		stub::reset();
		let mut h = scan_and_create(IDENTITY_PLUGIN_ID);
		if h.is_null() {
			return;
		}
		// 输入帧：每像素 r=0.1,g=0.2,b=0.3,a=1.0。
		let mut pixels = Vec::new();
		for _ in 0..4 * 4 {
			for v in [0.1f32, 0.2, 0.3, 1.0] {
				pixels.extend_from_slice(&v.to_le_bytes());
			}
		}
		stub::setup_dst(4, 4, oakplugin::bridge::render::PIXEL_FORMAT_F32);
		stub::setup_src(4, 4, oakplugin::bridge::render::PIXEL_FORMAT_F32, pixels.clone());
		let dst = fake_texture(0xA1);
		let src = fake_texture(0xA2);

		assert_eq!(
			unsafe { oakplugin_instance_render_job(h, dst, 0.0, 0, 0, cs("Source").as_ptr(), src, std::ptr::null(), 0, std::ptr::null(), 0, CHandle::null()) },
			OK
		);

		// 输出 == 输入（透传帧）。
		let out_pixels = stub::dst_pixels();
		assert_eq!(out_pixels, pixels, "isIdentity 透传应逐字节拷贝输入帧");
		unsafe { oakplugin_instance_free(&mut h) };
	});
}

/// 默认构建（无桩）：render_job 缺桥符号 → 明确失败码而非崩溃
/// （优雅降级；GL 用例同）。
#[cfg(not(feature = "test-stubs"))]
#[test]
fn render_job_graceful_without_bridge() {
	common::with_host(|| {
		let mut h = scan_and_create(TEST_PLUGIN_ID);
		if h.is_null() {
			return;
		}
		let dst = fake_texture(0xA1);
		let r = unsafe {
			oakplugin_instance_render_job(h, dst, 0.0, 0, 0, std::ptr::null(), CHandle::null(), std::ptr::null(), 0, std::ptr::null(), 0, CHandle::null())
		};
		assert_eq!(r, -90003, "无 liboakrender 时输出纹理无 CPU 帧 → E_FAILED");
		unsafe { oakplugin_instance_free(&mut h) };
	});
}

/// 驱动错误路径：非 F32 输出 → 明确失败；取消标记 → 明确失败。
#[cfg(feature = "test-stubs")]
#[test]
fn render_job_error_paths() {
	use oakplugin::bridge::render::stub;
	use oakplugin::ffi::{oakplugin_instance_cancel, oakplugin_instance_render_job};
	common::with_host(|| {
		stub::reset();
		let mut h = scan_and_create(TEST_PLUGIN_ID);
		if h.is_null() {
			return;
		}
		// 非 F32 输出（U8）→ read_dst 拒绝。
		stub::setup_dst(4, 4, oakplugin::bridge::render::PIXEL_FORMAT_U8);
		let dst = fake_texture(0xA1);
		let r = unsafe {
			oakplugin_instance_render_job(h, dst, 0.0, 0, 0, std::ptr::null(), CHandle::null(), std::ptr::null(), 0, std::ptr::null(), 0, CHandle::null())
		};
		assert_eq!(r, -90003, "非 F32 输出 → E_FAILED");

		// 取消标记 → 驱动入口短路。
		stub::setup_dst(4, 4, oakplugin::bridge::render::PIXEL_FORMAT_F32);
		assert_eq!(unsafe { oakplugin_instance_cancel(h) }, OK);
		let r = unsafe {
			oakplugin_instance_render_job(h, dst, 0.0, 0, 0, std::ptr::null(), CHandle::null(), std::ptr::null(), 0, std::ptr::null(), 0, CHandle::null())
		};
		assert_eq!(r, -90003, "已取消 → E_FAILED");
		unsafe { oakplugin_instance_free(&mut h) };
	});
}
