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

//! render 驱动：`src/render/src/plugin/pluginrenderer.cpp`
//! `PluginRenderer::render_plugin`（1857 行 C++ 的一部分）的渲染流程
//! 语义收编（M11 §4）。
//!
//! 目标：oakrender 的 PluginJob 退化为一次 C ABI 调用（
//! [`crate::ffi::oakplugin_instance_render_job`]），本模块承载全部
//! OFX 宿主渲染流程。逐段对照的 C++ 行号已注释。
//!
//! ## 流程（render_frame，对应 render_plugin）
//!
//! 1. 实例锁（pluginrenderer.cpp:1436-1444）+ 取消检查；
//! 2. use_opengl 决策（pluginrenderer.cpp:1446-1457）：插件描述符
//!    kOfxImageEffectPropOpenGLRenderSupported ∈ {true, needed} 且
//!    渲染器是 OpenGL 且目标纹理有 GL id；
//! 3. 输入纹理收集（pluginrenderer.cpp:1518-1552）：effect_input_id
//!    匹配 → job.src；否则按 clip 名的纹理表；SimpleSource 回退；
//! 4. getClipPreferences（pluginrenderer.cpp:1554-1594）；
//! 5. RoI/RoD 设定（pluginrenderer.cpp:1596-1607）：region_of_interest
//!    = 目标尺寸（规范坐标）；输出 clip 的 RoD 与输出纹理挂接；
//! 6. 输入 clip 的 RoD 与格式（pluginrenderer.cpp:1627-1665；Phase 2
//!    全链路 F32 → 无转换）；
//! 7. getRegionOfInterest（pluginrenderer.cpp:1666-1680）：RoI 为
//!    建议值，任何失败按默认整帧继续（C++ 对 BadHandle 即如此；
//!    Phase 2 其余状态同样继续——RoI 仅影响上游渲染范围，本管线
//!    输入由 oakrender 整帧提供）；
//! 8. 输出 clip 格式（pluginrenderer.cpp:1686-1697）；
//! 9. **isIdentity 短路**（ofxRendering "Identity Effects"）：
//!    isIdentity 命中 → 直接把所引输入 clip 在透传时间的帧拷入
//!    输出（不调 render action）；
//! 10. 参数覆盖（pluginrenderer.cpp:132-290 apply_param_overrides）；
//! 11. render action：CPU 路径经 [`crate::instance::Instance::render`]
//!     （输出装配：图像 → 目标纹理帧，行跨度感知）；GL 路径经
//!     [`crate::instance::Instance::render_gl`]（插件直接画进已附着
//!     的输出纹理，无 CPU 回读）。
//!
//! ## begin/end 序列括号
//!
//! ofxRendering 文档："All calls to the render action are bracketed by
//! a pair of begin/end sequence render actions"。oakrender 对同一实例
//! 的一批帧先 [`begin_sequence`] 后 [`end_sequence`]，中间逐帧
//! [`render_frame`]。

use crate::bridge::render::{self, FrameHandle, RendererHandle, TextureHandle};
use crate::image::Image;
use crate::instance::{Instance, OfxRangeD, OfxRectD, RenderScale};
use crate::property::Value;

/// 一帧渲染任务的输入（oakrender PluginJob 的 C ABI 载体；
/// [`crate::ffi::OakPluginJob`] 的 Rust 侧视图）。
pub struct RenderJob {
	/// 帧时间（秒）。
	pub time: f64,
	/// 目标纹理（输出；oakrender 侧创建并经句柄传入）。
	pub dst: TextureHandle,
	/// 主输入纹理（effect_input_id / SimpleSource；可空）。
	pub src: TextureHandle,
	/// effect 输入 clip 名（job.src 的落点；C++ `node->get_effect_input_id()`）。
	pub effect_input_id: Option<String>,
	/// 其余输入 clip 的纹理表（clip 名 → 纹理）。
	pub inputs: Vec<(String, TextureHandle)>,
	/// 参数覆盖（参数名 → oaknode_value POD；对应 NodeValueRow）。
	pub values: Vec<(String, crate::ffi::OakNodeValue)>,
	/// GL 渲染器（None → CPU 路径；Some → 视 use_opengl 决策）。
	pub renderer: Option<RendererHandle>,
	/// 渲染前是否清空目标（信息性——C++ render_plugin 亦不处理，
	/// 由上层渲染器负责；见插件渲染器注释）。
	pub clear_destination: bool,
	/// 交互式渲染标记（信息性；Phase 2 的 render in args 恒 0，见
	/// [`crate::instance::Instance::render`]）。
	pub interactive: bool,
}

impl Default for RenderJob {
	fn default() -> Self {
		Self {
			time: 0.0,
			dst: TextureHandle::null(),
			src: TextureHandle::null(),
			effect_input_id: None,
			inputs: Vec::new(),
			values: Vec::new(),
			renderer: None,
			clear_destination: false,
			interactive: false,
		}
	}
}

/// beginSequenceRender 括号（ofxRendering："bracketed by a pair of
/// begin/end sequence render actions"；ofxGPURender.h 的 GL 模式下该
/// action 的 in args 也带 kOfxImageEffectPropOpenGLEnabled）。
/// `gl` 非空时按 GL 模式调用。
pub fn begin_sequence(
	inst: &Instance,
	range: OfxRangeD,
	gl: Option<RendererHandle>,
) -> crate::error::Result<()> {
	if gl.is_some() {
		inst.begin_sequence_render_gl(range)
	} else {
		inst.begin_sequence_render(range)
	}
}

/// endSequenceRender 括号（与 [`begin_sequence`] 配对）。
pub fn end_sequence(
	inst: &Instance,
	range: OfxRangeD,
	gl: Option<RendererHandle>,
) -> crate::error::Result<()> {
	if gl.is_some() {
		inst.end_sequence_render_gl(range)
	} else {
		inst.end_sequence_render(range)
	}
}

/// 渲染一帧（`render_plugin` 的 Rust 移植；逐段行号对照见模块文档）。
///
/// 返回各输入 clip 的 RoI（clip 名 → 矩形；Phase 2 供测试断言，
/// 宿主不据此裁剪输入——输入由 oakrender 整帧提供，与 C++ 渲染器
/// 行为一致）。
pub fn render_frame(
	inst: &Instance,
	job: &RenderJob,
) -> crate::error::Result<Vec<(String, OfxRectD)>> {
	use crate::error::Error;

	// 1. 实例锁（pluginrenderer.cpp:1436-1444：OlivePluginInstance 非
	// 线程安全，并发 setInputTexture/renderAction 会毁内部状态）。
	let _lock = inst.render_lock.lock().unwrap_or_else(|e| e.into_inner());
	// 取消检查必须在任何插件调用之前（shutdown 后入口已卸载）。
	if inst.cancel.load(std::sync::atomic::Ordering::Relaxed) {
		return Err(Error::Failed("已取消".into()));
	}

	// 2. use_opengl（pluginrenderer.cpp:1446-1457）：插件声明 GL 支持
	// 且渲染器是 OpenGL 且目标纹理有 GL id 且像素深度协商可行
	// （管线 F32 满足插件 kOfxOpenGLPropPixelDepth 声明）。
	let use_opengl = match job.renderer {
		Some(r) if unsafe { render::renderer_is_open_gl(r) } == 1 => {
			let plugin_gl = plugin_supports_opengl(inst);
			let depth_ok =
				crate::suites::gl_render::pick_gl_pixel_depth(&inst.plugin.descriptor.props)
					.is_some();
			let dst_id = unsafe { render::texture_id(job.dst) };
			plugin_gl && depth_ok && dst_id != 0
		}
		_ => false,
	};

	// 目标帧与参数（F32 校验；输出装配的依据）。
	let (dst_frame, dst_params, w, h) = read_dst(job.dst)?;
	let mut dst_frame = dst_frame;
	let par = pixel_aspect(&dst_params);
	// 规范坐标的 RoI/RoD（pluginrenderer.cpp:1595-1603：x2 = 宽 × PAR）。
	let region_of_interest = OfxRectD {
		x1: 0.0,
		y1: 0.0,
		x2: w * par,
		y2: h,
	};

	// 3. 输入纹理收集（pluginrenderer.cpp:1518-1552）。
	for clip in &inst.clips {
		if clip.name == "Output" {
			continue;
		}
		let tex = pick_input(&clip.name, job);
		if usable(tex) {
			clip.set_input_texture(tex, job.time);
		}
	}

	// 4. getClipPreferences（pluginrenderer.cpp:1554-1594）。
	let prefs = inst.get_clip_preferences()?;
	let components = match prefs.output_components.as_str() {
		"OfxImageComponentRGBA" => crate::image::Components::Rgba,
		"OfxImageComponentRGB" => crate::image::Components::Rgb,
		"OfxImageComponentAlpha" => crate::image::Components::Alpha,
		_ => return Err(Error::Failed("协商分量未知".into())),
	};
	if prefs.output_bit_depth != "OfxBitDepthFloat" {
		return Err(Error::Failed("Phase 2 仅支持 F32 输出".into()));
	}

	// 5. 输出 clip：RoD + 输出纹理挂接（pluginrenderer.cpp:1603-1606）。
	let output_clip = inst
		.clips
		.iter()
		.find(|c| c.name == "Output")
		.ok_or_else(|| Error::Failed("实例无 Output clip".into()))?;
	output_clip.set_region_of_definition(region_of_interest, job.time);
	output_clip.set_output_texture(job.dst, job.time);

	// 6. 输入 clip：RoD 与格式（pluginrenderer.cpp:1627-1665；Phase 2
	// 全链路 F32 → 格式选择恒等，无转换路径）。
	for clip in &inst.clips {
		if clip.name == "Output" {
			continue;
		}
		if usable(pick_input(&clip.name, job)) {
			clip.set_region_of_definition(region_of_interest, job.time);
			clip.set_video_params(render::PIXEL_FORMAT_F32, 4);
		}
	}

	// 7. getRegionOfInterest（pluginrenderer.cpp:1666-1680）。RoI 为
	// 建议值：失败按默认整帧继续（C++ 对 BadHandle 如此；本管线
	// 输入整帧提供，RoI 只作记录）。
	let rois = inst
		.get_regions_of_interest(job.time, RenderScale { x: 1.0, y: 1.0 }, region_of_interest)
		.unwrap_or_else(|_| inst.clips.iter().map(|_| region_of_interest).collect());

	// 8. 输出 clip 格式（pluginrenderer.cpp:1686-1697）。
	output_clip.set_video_params(render::PIXEL_FORMAT_F32, components.channel_count() as i32);

	// 渲染窗口（像素坐标；pluginrenderer.cpp:1699-1704）。
	let render_window = OfxRectD {
		x1: 0.0,
		y1: 0.0,
		x2: w,
		y2: h,
	};

	// 9. isIdentity 短路（ofxRendering "Identity Effects"）：插件声明
	// 本帧等价于某输入 clip → 直接透传该 clip 在透传时间的帧。
	if let Some((t, clip_name)) = inst.is_identity(job.time)? {
		passthrough(inst, &clip_name, t, job.dst)?;
		unsafe { render::frame_free(&mut (dst_frame)) };
		return Ok(zip_rois(inst, &rois));
	}

	// 10. 参数覆盖（pluginrenderer.cpp:1729-1731 + 132-290）。
	apply_param_overrides(inst, &job.values);

	// 11. render action。
	if !use_opengl {
		let output = std::sync::Arc::new(Image::allocate(
			crate::image::BitDepth::Float,
			components,
			OfxRectD {
				x1: 0.0,
				y1: 0.0,
				x2: w,
				y2: h,
			},
		));
		inst.render(
			job.time,
			RenderScale { x: 1.0, y: 1.0 },
			render_window,
			output.clone(),
		)?;
		// 输出装配（pluginrenderer.cpp:1762-1834 的 CPU 路径）。
		write_output_frame(job.dst, &output)?;
	} else {
		// GL 路径：插件直接画进已附着的输出纹理（
		// pluginrenderer.cpp:1784-1834 的 GL 分支）；无 CPU 回读。
		inst.render_gl(
			job.time,
			RenderScale { x: 1.0, y: 1.0 },
			render_window,
			job.renderer.unwrap(),
			job.dst,
		)?;
	}

	unsafe { render::frame_free(&mut (dst_frame)) };
	Ok(zip_rois(inst, &rois))
}

/// 把输入 clip 名与 RoI 列表配对（与 `clips` 顺序一致）。
fn zip_rois(inst: &Instance, rois: &[OfxRectD]) -> Vec<(String, OfxRectD)> {
	inst.clips
		.iter()
		.enumerate()
		.filter(|(_, c)| c.name != "Output")
		.map(|(i, c)| (c.name.clone(), rois.get(i).copied().unwrap_or_default()))
		.collect()
}

/// 读目标纹理的帧与参数（F32 校验）。返回 (帧句柄, 参数, 宽, 高)。
fn read_dst(
	dst: TextureHandle,
) -> crate::error::Result<(FrameHandle, render::VideoParams, f64, f64)> {
	use crate::error::Error;
	let mut frame = FrameHandle::null();
	if unsafe { render::texture_get_frame(dst, &mut frame) } != 0 || frame.is_null() {
		return Err(Error::Failed("输出纹理无 CPU 帧".into()));
	}
	let mut params = render::VideoParams::default();
	if unsafe { render::frame_get_params(frame, &mut params) } != 0 {
		unsafe { render::frame_free(&mut frame) };
		return Err(Error::Failed("输出帧无参数".into()));
	}
	if params.format != render::PIXEL_FORMAT_F32 {
		unsafe { render::frame_free(&mut frame) };
		return Err(Error::Failed(format!(
			"输出帧格式 {} 非 F32（Phase 2 约束）",
			params.format
		)));
	}
	let (w, h) = (params.width as f64, params.height as f64);
	if w <= 0.0 || h <= 0.0 {
		unsafe { render::frame_free(&mut frame) };
		return Err(Error::Invalid);
	}
	Ok((frame, params, w, h))
}

/// 目标参数的像素比（缺失 1.0）。
fn pixel_aspect(params: &render::VideoParams) -> f64 {
	if params.pixel_aspect_den != 0 {
		params.pixel_aspect_num as f64 / params.pixel_aspect_den as f64
	} else {
		1.0
	}
}

/// 插件描述符是否声明 GL 渲染支持（ofxGPURender.h:397-408：
/// "true"/"needed"）。
fn plugin_supports_opengl(inst: &Instance) -> bool {
	inst.plugin
		.descriptor
		.props
		.get(crate::host::PROP_GL_RENDER_SUPPORTED, 0)
		.map(|v| match v {
			Value::String(s) => {
				let s = s.to_string_lossy();
				s == "true" || s == "needed"
			}
			_ => false,
		})
		.unwrap_or(false)
}

/// 输入纹理是否可用（非空且非占位；pluginrenderer.cpp:1504-1513 的
/// is_usable_input——Phase 2 只看非 dummy，帧/Renderer 由 oakrender
/// 保证）。
fn usable(tex: TextureHandle) -> bool {
	!tex.is_null() && unsafe { render::texture_is_dummy(tex) } == 0
}

/// 按 C++ pluginrenderer.cpp:1527-1543 的规则选输入纹理。
fn pick_input(clip_name: &str, job: &RenderJob) -> TextureHandle {
	if job.effect_input_id.as_deref() == Some(clip_name) && !job.src.is_null() {
		return job.src;
	}
	for (name, tex) in &job.inputs {
		if name == clip_name {
			return *tex;
		}
	}
	// SimpleSource 回退（pluginrenderer.cpp:1534-1543：
	// kOfxImageEffectSimpleSourceClipName 取 k_texture_input，再回退
	// job.src）。
	if clip_name == "Source" && !job.src.is_null() {
		return job.src;
	}
	TextureHandle::null()
}

/// isIdentity 透传：把所引输入 clip 在 `t` 的帧拷入输出（CPU 拷贝；
/// 目标帧 F32 校验）。
fn passthrough(
	inst: &Instance,
	clip_name: &str,
	t: f64,
	dst: TextureHandle,
) -> crate::error::Result<()> {
	use crate::error::Error;
	let clip = inst
		.clips
		.iter()
		.find(|c| c.name == clip_name)
		.ok_or_else(|| Error::Failed(format!("isIdentity 引用未知 clip {clip_name}")))?;
	let image = clip.fetch_image(t, RenderScale { x: 1.0, y: 1.0 }, None)?;
	write_output_frame(dst, &image)
}

/// 参数覆盖（pluginrenderer.cpp:132-290 `apply_param_overrides` 的
/// Rust 移植）：把每帧的节点值注入实例参数。字符串族（String/
/// StrChoice）经专用 C ABI（set_param_string），不在此表的 oaknode
/// POD 表达范围内 → 跳过（与 C++ 的 k_file/k_text/k_font/k_str_combo
/// 走专用桥一致）。
fn apply_param_overrides(inst: &Instance, values: &[(String, crate::ffi::OakNodeValue)]) {
	for (key, v) in values {
		let Some(p) = inst.params.find(key) else {
			continue;
		};
		let Some(pv) = crate::ffi::node_value_to_param(v, &p.def.ofx_type) else {
			continue;
		};
		p.set_ofx(pv);
	}
}

/// 把 CPU 图像写入目标纹理的帧（行优先、行跨度感知；F32 校验）。
/// Phase 2 输出装配的公共落点（CPU render 路径与 isIdentity 透传
/// 共用）。
pub(crate) fn write_output_frame(dst: TextureHandle, image: &Image) -> crate::error::Result<()> {
	use crate::error::Error;
	let mut frame = FrameHandle::null();
	if unsafe { render::texture_get_frame(dst, &mut frame) } != 0 || frame.is_null() {
		return Err(Error::Failed("输出纹理无 CPU 帧".into()));
	}
	let mut params = render::VideoParams::default();
	if unsafe { render::frame_get_params(frame, &mut params) } != 0 {
		unsafe { render::frame_free(&mut frame) };
		return Err(Error::Failed("输出帧无参数".into()));
	}
	if params.format != render::PIXEL_FORMAT_F32 {
		unsafe { render::frame_free(&mut frame) };
		return Err(Error::Failed("输出帧格式非 F32（Phase 2 约束）".into()));
	}
	let (w, h) = (params.width as usize, params.height as usize);
	let tight = w * image.components().channel_count() * 4;
	if tight != image.row_bytes() || tight * h != image.pixels().len() {
		unsafe { render::frame_free(&mut frame) };
		return Err(Error::Failed("图像尺寸与输出帧不一致".into()));
	}
	let dst_ptr = unsafe { render::frame_data(frame) };
	if dst_ptr.is_null() {
		unsafe { render::frame_free(&mut frame) };
		return Err(Error::Failed("输出帧无数据".into()));
	}
	let row = unsafe { render::frame_linesize_bytes(frame) } as usize;
	let row = if row > 0 { row } else { tight };
	let dst_bytes = unsafe { std::slice::from_raw_parts_mut(dst_ptr as *mut u8, row * h) };
	let pixels = image.pixels();
	for y in 0..h {
		let d = y * row;
		let s = y * tight;
		dst_bytes[d..d + tight].copy_from_slice(&pixels[s..s + tight]);
	}
	unsafe { render::frame_free(&mut frame) };
	Ok(())
}
