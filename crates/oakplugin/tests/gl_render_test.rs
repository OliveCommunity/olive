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

//! OpenGL 渲染路径测试（M11 §4）。
//!
//! GL 测试策略：
//! - `--features test-stubs`：库内桩 GL 渲染器（renderer_is_open_gl /
//!   texture_create / texture_id 等）模拟 GPU——suite 往返、GL render
//!   路径、错误路径全链路可跑（无需真实 GPU/liboakrender）；
//! - 默认模式（无 liboakrender 符号）：GL 决策回退 CPU、GL suite 在
//!   无上下文时返回 kOfxStatErrMissingHostFeature——用例断言该优雅
//!   降级（"无 GPU 优雅跳过"）。
//! - 真实 GPU golden（EXR 容差比对）为 M11 0 期基建 + 本机人工确认
//!   项：`OAK_GPU_TESTS` 环境变量门，见 common::gpu_available。

mod common;

use std::ffi::{c_char, c_void, CString};

use oakplugin::ffi::{
	oakplugin_host_scan, oakplugin_host_set_message_handler, oakplugin_instance_create,
	oakplugin_instance_free, oakplugin_instance_render_begin_sequence,
	oakplugin_instance_render_end_sequence, oakplugin_instance_render_job,
};
use oakplugin::handle::CHandle;
use oakplugin::suites::gl_render::GlRenderSuiteV1;
use oakplugin::suites::{fetch_suite, status, tag};

const OK: i32 = 0;
const GL_PLUGIN_ID: &str = "org.oak.test-plugin.gl";

fn cs(s: &str) -> CString {
	CString::new(s).unwrap()
}

/// 简易句柄（桩纹理/渲染器）。
fn fake_handle(ctx: usize) -> CHandle {
	CHandle {
		ctx: ctx as *mut c_void,
		addref: None,
		release: None,
		abi_version: 1,
	}
}

/// 扫描测试插件并创建 GL 变体实例；不可用返回空句柄。
fn create_gl_instance() -> CHandle {
	if common::test_plugin_scan_dir().is_none() {
		common::skip("最小测试插件未构建");
		return CHandle::null();
	}
	let dir = cs(common::test_plugin_scan_dir().unwrap().to_str().unwrap());
	let dirs = [dir.as_ptr()];
	unsafe { oakplugin_host_scan(dirs.as_ptr(), 1) };
	let id = cs(GL_PLUGIN_ID);
	unsafe { oakplugin_instance_create(id.as_ptr()) }
}

/// GL suite 表（fetchSuite 分发表）。
fn gl_suite() -> Option<&'static GlRenderSuiteV1> {
	unsafe { fetch_suite("OfxImageEffectOpenGLRenderSuite", 1) }
		.map(|p| unsafe { &*(p as *const GlRenderSuiteV1) })
}

/// 构造一个带输入纹理的 Source clip（实例期 clip 句柄）。
fn source_clip_handle(
	tex: CHandle,
) -> (*mut c_void, std::sync::Arc<oakplugin::clip::ClipInstance>) {
	let desc = oakplugin::descriptor::ClipDescriptor::new("Source");
	let clip = std::sync::Arc::new(oakplugin::clip::ClipInstance::from_descriptor(&desc));
	clip.set_input_texture(tex, 0.0);
	let h = tag::make(
		&clip.props as *const oakplugin::property::PropertySet,
		tag::CLIP,
	);
	(h, clip)
}

/// 当前渲染/GL 上下文注入（suite 的 TLS 依赖）。
fn inject_gl_ctx(renderer: CHandle, output: CHandle) {
	oakplugin::suites::set_render_ctx(Some(oakplugin::suites::RenderCtx {
		time: 0.0,
		scale: oakplugin::instance::RenderScale { x: 1.0, y: 1.0 },
		range: oakplugin::instance::OfxRangeD { min: 0.0, max: 1.0 },
	}));
	oakplugin::suites::set_gl_ctx(Some(oakplugin::suites::GlCtx {
		renderer,
		output_texture: output,
		gl_pixel_depth: "OfxBitDepthFloat",
	}));
}

/// 读纹理句柄（属性集）的 int 属性。
fn tex_int(props: &oakplugin::property::PropertySet, name: &str) -> Option<i32> {
	match props.get(name, 0)? {
		oakplugin::property::Value::Int(i) => Some(i),
		_ => None,
	}
}

fn tex_string(props: &oakplugin::property::PropertySet, name: &str) -> Option<String> {
	match props.get(name, 0)? {
		oakplugin::property::Value::String(s) => Some(s.to_string_lossy().into_owned()),
		_ => None,
	}
}

/// 清理 TLS（每个用例结尾）。
fn clear_ctxs() {
	oakplugin::suites::set_gl_ctx(None);
	oakplugin::suites::set_render_ctx(None);
}

/// clipLoadTexture 往返：输入 clip 建纹理、属性齐全、clipFreeTexture
/// 释放；Output clip 返回附着输出纹理；flushResources → ReplyDefault。
#[cfg(feature = "test-stubs")]
#[test]
fn clip_load_texture_roundtrip() {
	use oakplugin::bridge::render::stub;
	common::with_host(|| {
		stub::reset();
		stub::set_gl_available(true);
		let renderer = stub::make_gl_renderer();
		let dst = stub::make_gl_texture(4, 4, oakplugin::bridge::render::PIXEL_FORMAT_F32);
		let src = stub::make_gl_texture(4, 4, oakplugin::bridge::render::PIXEL_FORMAT_F32);
		inject_gl_ctx(renderer, dst);
		let suite = gl_suite().expect("GL suite 已注册");

		// 输入 clip。
		let (clip_h, _clip) = source_clip_handle(src);
		let before = stub::gl_texture_ids();
		let mut tex: *mut c_void = std::ptr::null_mut();
		unsafe {
			assert_eq!(
				(suite.clip_load_texture)(
					clip_h,
					0.0,
					std::ptr::null(),
					std::ptr::null(),
					&mut tex
				),
				0
			);
		}
		assert!(!tex.is_null(), "应返回纹理属性集句柄");
		let props = unsafe { &*(tag::strip(tex) as *const oakplugin::property::PropertySet) };
		let idx = tex_int(props, "OfxImageEffectPropOpenGLTextureIndex").expect("texture index");
		assert!(idx > 0, "新建纹理 id 为正：{idx}");
		assert_eq!(
			tex_int(props, "OfxImageEffectPropOpenGLTextureTarget"),
			Some(0x0DE1),
			"GL_TEXTURE_2D"
		);
		assert_eq!(
			tex_string(props, "OfxImageEffectPropPixelDepth").as_deref(),
			Some("OfxBitDepthFloat")
		);
		assert_eq!(
			tex_string(props, "OfxImageEffectPropComponents").as_deref(),
			Some("OfxImageComponentRGBA")
		);
		assert!(props.get("OfxImagePropBounds", 0).is_some(), "Bounds");
		assert!(props.get("OfxImagePropRowBytes", 0).is_some(), "RowBytes");
		assert!(
			props.get("OfxImagePropUniqueIdentifier", 0).is_some(),
			"UniqueIdentifier"
		);
		// 注册表新增一个纹理。
		let after = stub::gl_texture_ids();
		assert_eq!(after.len(), before.len() + 1, "clipLoadTexture 建新纹理");
		assert!(after.contains(&idx), "新纹理 id 在注册表");

		// clipFreeTexture：删输入纹理。
		unsafe {
			assert_eq!((suite.clip_free_texture)(tex), 0);
		}
		assert!(!stub::gl_texture_ids().contains(&idx), "输入纹理已删除");
		// 重复释放 → BadHandle。
		unsafe {
			assert_eq!((suite.clip_free_texture)(tex), status::ERR_BAD_HANDLE);
		}

		// Output clip：返回附着输出纹理；free 不删纹理（宿主读）。
		let mut out: *mut c_void = std::ptr::null_mut();
		let out_clip_instance = oakplugin::clip::ClipInstance::from_descriptor(
			&oakplugin::descriptor::ClipDescriptor::new("Output"),
		);
		let out_clip = tag::make(
			&out_clip_instance.props as *const oakplugin::property::PropertySet,
			tag::CLIP,
		);
		unsafe {
			assert_eq!(
				(suite.clip_load_texture)(
					out_clip,
					0.0,
					std::ptr::null(),
					std::ptr::null(),
					&mut out
				),
				0
			);
		}
		assert!(!out.is_null());
		let props = unsafe { &*(tag::strip(out) as *const oakplugin::property::PropertySet) };
		let dst_id = tex_int(props, "OfxImageEffectPropOpenGLTextureIndex").unwrap();
		assert!(
			stub::gl_texture_ids().contains(&dst_id),
			"输出纹理 id 来自附着目标"
		);
		unsafe {
			assert_eq!((suite.clip_free_texture)(out), 0);
		}
		assert!(
			stub::gl_texture_ids().contains(&dst_id),
			"Output free 不删纹理"
		);

		// flushResources：无宿主缓存 → ReplyDefault。
		unsafe {
			assert_eq!((suite.flush_resources)(), status::REPLY_DEFAULT);
		}

		clear_ctxs();
	});
}

/// 错误路径：无 GL 上下文 → MissingHostFeature；空 out/BadHandle；
/// 子区域 → Failed；请求分量不匹配 → Failed。
#[cfg(feature = "test-stubs")]
#[test]
fn clip_load_texture_error_paths() {
	use oakplugin::bridge::render::stub;
	common::with_host(|| {
		stub::reset();
		stub::set_gl_available(true);
		let renderer = stub::make_gl_renderer();
		let dst = stub::make_gl_texture(4, 4, oakplugin::bridge::render::PIXEL_FORMAT_F32);
		let src = stub::make_gl_texture(4, 4, oakplugin::bridge::render::PIXEL_FORMAT_F32);
		let suite = gl_suite().expect("GL suite 已注册");

		// 无 GL 上下文（非 GL 渲染期）→ MissingHostFeature。
		let (clip_h, _clip) = source_clip_handle(src);
		let mut tex: *mut c_void = std::ptr::null_mut();
		unsafe {
			assert_eq!(
				(suite.clip_load_texture)(
					clip_h,
					0.0,
					std::ptr::null(),
					std::ptr::null(),
					&mut tex
				),
				status::ERR_MISSING_HOST_FEATURE
			);
			// 空 out → BadHandle；空 clip → BadHandle。
			assert_eq!(
				(suite.clip_load_texture)(
					clip_h,
					0.0,
					std::ptr::null(),
					std::ptr::null(),
					std::ptr::null_mut()
				),
				status::ERR_BAD_HANDLE
			);
			assert_eq!(
				(suite.clip_load_texture)(
					std::ptr::null_mut(),
					0.0,
					std::ptr::null(),
					std::ptr::null(),
					&mut tex
				),
				status::ERR_BAD_HANDLE
			);
			// clipFreeTexture 空句柄 → BadHandle。
			assert_eq!(
				(suite.clip_free_texture)(std::ptr::null_mut()),
				status::ERR_BAD_HANDLE
			);
		}

		// 有 GL 上下文后：子区域（Phase 2 不支持）→ Failed。
		inject_gl_ctx(renderer, dst);
		let region = oakplugin::instance::OfxRectD {
			x1: 0.0,
			y1: 0.0,
			x2: 2.0,
			y2: 2.0,
		};
		unsafe {
			assert_eq!(
				(suite.clip_load_texture)(
					clip_h,
					0.0,
					std::ptr::null(),
					&region as *const _ as *const c_void,
					&mut tex
				),
				status::FAILED
			);
		}

		// 请求分量不匹配（RGBA 输入 + GLFormatRGB）→ Failed。
		let fmt = cs("OfxImageEffectGLFormatRGB");
		unsafe {
			assert_eq!(
				(suite.clip_load_texture)(clip_h, 0.0, fmt.as_ptr(), std::ptr::null(), &mut tex),
				status::FAILED
			);
		}

		// 匹配请求（GLFormatRGBA）→ OK。
		let fmt = cs("OfxImageEffectGLFormatRGBA");
		unsafe {
			assert_eq!(
				(suite.clip_load_texture)(clip_h, 0.0, fmt.as_ptr(), std::ptr::null(), &mut tex),
				0
			);
			assert_eq!((suite.clip_free_texture)(tex), 0);
		}
		clear_ctxs();
	});
}

/// 无桩模式（默认构建）：GL suite 在无上下文时返回 MissingHostFeature
/// （优雅降级，不崩）；render_job 带渲染器句柄时回退 CPU 路径。
#[cfg(not(feature = "test-stubs"))]
#[test]
fn gl_path_graceful_without_gpu() {
	common::with_host(|| {
		let suite = gl_suite().expect("GL suite 已注册");
		let desc = oakplugin::descriptor::ClipDescriptor::new("Source");
		let clip = oakplugin::clip::ClipInstance::from_descriptor(&desc);
		let clip_h = tag::make(&clip.props as *const _, tag::CLIP);
		let mut tex: *mut c_void = std::ptr::null_mut();
		unsafe {
			// 无 GL 上下文 → MissingHostFeature（规范语义）。
			assert_eq!(
				(suite.clip_load_texture)(
					clip_h,
					0.0,
					std::ptr::null(),
					std::ptr::null(),
					&mut tex
				),
				status::ERR_MISSING_HOST_FEATURE
			);
			// flushResources → ReplyDefault。
			assert_eq!((suite.flush_resources)(), status::REPLY_DEFAULT);
		}
	});
}

/// GL render 路径端到端（test-stubs）：render_job 带 GL 渲染器 →
/// 插件收到 OpenGLEnabled=1、attach/detach 配对、clipLoadTexture
/// 取 Source/Output 纹理（插件经 message suite 上报索引）。
#[cfg(feature = "test-stubs")]
#[test]
fn gl_render_path_drives_plugin() {
	use oakplugin::bridge::render::stub;
	common::with_host(|| {
		stub::reset();
		let mut h = create_gl_instance();
		if h.is_null() {
			return;
		}
		// 消息捕获（插件 GL 行为的上报通道）。
		let captured: std::sync::Arc<std::sync::Mutex<Vec<String>>> =
			std::sync::Arc::new(std::sync::Mutex::new(Vec::new()));
		{
			let cap = captured.clone();
			unsafe extern "C" fn capture(
				_type: *const c_char,
				message: *const c_char,
				userdata: *mut c_void,
			) -> std::ffi::c_int {
				let v = unsafe { &mut *(userdata as *mut std::sync::Mutex<Vec<String>>) };
				if !message.is_null() {
					let m = unsafe { std::ffi::CStr::from_ptr(message) }
						.to_string_lossy()
						.into_owned();
					v.lock().unwrap_or_else(|e| e.into_inner()).push(m);
				}
				0
			}
			unsafe {
				oakplugin_host_set_message_handler(Some(capture), &*cap as *const _ as *mut c_void)
			};
		}

		// GL 环境：渲染器可用；dst/src 均为 GL 纹理（2×2 F32）。
		stub::set_gl_available(true);
		let renderer = stub::make_gl_renderer();
		let dst = stub::make_gl_texture(2, 2, oakplugin::bridge::render::PIXEL_FORMAT_F32);
		let src = stub::make_gl_texture(2, 2, oakplugin::bridge::render::PIXEL_FORMAT_F32);

		// begin → job → end（序列括号）。
		assert_eq!(
			unsafe { oakplugin_instance_render_begin_sequence(h, 0.0, 1.0, 0) },
			OK
		);
		let r = unsafe {
			oakplugin_instance_render_job(
				h,
				dst,
				0.0,
				0,
				0,
				cs("Source").as_ptr(),
				src,
				std::ptr::null(),
				0,
				std::ptr::null(),
				0,
				renderer,
			)
		};
		assert_eq!(r, OK, "GL render_job 应成功");
		assert_eq!(
			unsafe { oakplugin_instance_render_end_sequence(h, 0.0, 1.0, 0) },
			OK
		);

		// 插件上报：attach/detach 配对、Source 纹理索引、Output 纹理索引。
		let msgs = captured.lock().unwrap_or_else(|e| e.into_inner()).clone();
		let joined = msgs.join("|");
		assert!(joined.contains("gl-attached"), "attach 应调用：{joined}");
		assert!(joined.contains("gl-detached"), "detach 应调用：{joined}");
		assert!(
			joined.contains("gl-source-index="),
			"Source 纹理索引：{joined}"
		);
		assert!(
			joined.contains("gl-output-index="),
			"Output 纹理索引：{joined}"
		);

		unsafe { oakplugin_instance_free(&mut h) };
		unsafe { oakplugin_host_set_message_handler(None, std::ptr::null_mut()) };
	});
}
