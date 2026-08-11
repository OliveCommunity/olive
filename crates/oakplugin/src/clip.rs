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

//! clip 实例：clip ↔ oakrender 纹理桥。
//!
//! 对应 C++ 的 `OliveClipInstance`。纹理数据经
//! [`crate::bridge::render`] 的 oakrender C ABI 流动；OFX 侧只看到
//! [`crate::image::Image`]（CPU 路径）。
//!
//! `#[repr(C)]` + props 在偏移 0（句柄约定，见 [`crate::suites::tag`]；
//! clip handle 即 `&props`）。
//!
//! `// TODO(bridge)`：`fetch_image`/`store_output_image` 依赖
//! bridge::render 的帧访问 C ABI（声明未冻结），保留 todo!()。
//! **已落地（M11 第 1 期）**：帧访问 C ABI 在 [`crate::bridge::render`]
//! 冻结（`oakrender_display_texture_*`/`oakrender_codec_frame_*`），
//! 两处桥实现完成。

use crate::instance::{OfxRangeD, OfxRectD, RenderScale};
use crate::property::PropertySet;

/// clip 实例。
#[repr(C)]
pub struct ClipInstance {
	/// 实例级 clip 属性（当前分量/位深/像素比，协商结果写入；
	/// 偏移 0，句柄约定）。
	pub props: PropertySet,
	/// clip 名。
	pub name: String,
	/// 当前输入纹理（oakrender 句柄的借用拷贝；输出 clip 为 None）。
	input_texture: std::sync::Mutex<Option<crate::bridge::render::TextureHandle>>,
	/// 当前输出纹理（C++ `output_textures_` 的 phase 1 单槽；
	/// [`store_output_image`](Self::store_output_image) 的回写目标；
	/// 输入 clip 为 None）。
	output_texture: std::sync::Mutex<Option<crate::bridge::render::TextureHandle>>,
}

/// 从 clip 属性读协商分量（getClipPreferences 写入）。
fn components_from_props(props: &PropertySet) -> Option<crate::image::Components> {
	use crate::property::Value;
	match props.get(crate::image::K_IMAGE_EFFECT_PROP_COMPONENTS, 0)? {
		Value::String(s) => match s.to_string_lossy().as_ref() {
			"OfxImageComponentRGBA" => Some(crate::image::Components::Rgba),
			"OfxImageComponentRGB" => Some(crate::image::Components::Rgb),
			"OfxImageComponentAlpha" => Some(crate::image::Components::Alpha),
			_ => None,
		},
		_ => None,
	}
}

impl ClipInstance {
	/// 按描述符实例化（createInstance 路径调用；公开：宿主与测试
	/// 都需要构造 clip 实例）。实例 props 是描述符 props 的深拷贝
	/// （HS: ClipBase 的实例构造，ofxhClip.cpp:57-70——插件在实例期
	/// 读 supported components 等）。
	///
	/// ofxColour（M11 §4）：输入 clip 的 kOfxImageClipPropColourspace
	/// 由宿主写为工作空间（ACEScg，ofxColour.h "Hosts should set this
	/// property to the colourspace of the input clip. Typically it will
	/// be set to the working colourspace"）；输出 clip 由
	/// GetOutputColourspace action 后写。
	pub fn from_descriptor(desc: &crate::descriptor::ClipDescriptor) -> Self {
		let props = desc.props.clone();
		let name = desc.name.clone();
		if name != "Output" {
			props.set_one(
				crate::host::PROP_CLIP_COLOURSPACE,
				crate::property::Value::String(
					std::ffi::CString::new(crate::host::WORKING_COLOURSPACE).unwrap(),
				),
			);
		}
		Self {
			props,
			name,
			input_texture: std::sync::Mutex::new(None),
			output_texture: std::sync::Mutex::new(None),
		}
	}

	/// 写协商后的像素格式（C++ `OliveClipInstance::setParams` 的 props
	/// 侧：setPixelDepth/setComponents，oliveclip.cpp:700-709）。
	/// `format` 为 olive::PixelFormat::Format（0=u8, 2=u16, 3=f16,
	/// 4=f32）；`channels` 为分量数。
	pub fn set_video_params(&self, format: i32, channels: i32) {
		let depth = match format {
			0 => "OfxBitDepthByte",
			2 => "OfxBitDepthShort",
			3 => "OfxBitDepthHalf",
			_ => "OfxBitDepthFloat",
		};
		let comps = match channels {
			1 => "OfxImageComponentAlpha",
			3 => "OfxImageComponentRGB",
			_ => "OfxImageComponentRGBA",
		};
		self.props.set_one(
			crate::image::K_IMAGE_EFFECT_PROP_PIXEL_DEPTH,
			crate::property::Value::String(std::ffi::CString::new(depth).unwrap()),
		);
		self.props.set_one(
			crate::image::K_IMAGE_EFFECT_PROP_COMPONENTS,
			crate::property::Value::String(std::ffi::CString::new(comps).unwrap()),
		);
	}

	/// 写协商 RoD（C++ `setRegionOfDefinition` 的单槽版，
	/// oliveclip.cpp:674-678——C++ 按 time 存 map，本驱动一帧一槽；
	/// 落点与 image effect suite 的 clipGetRegionOfDefinition 读取处
	/// 一致）。
	pub fn set_region_of_definition(&self, rod: OfxRectD, _time: f64) {
		use crate::property::Value;
		self.props.define(
			"OfxImageEffectPropRegionOfDefinition",
			vec![
				Value::Double(rod.x1),
				Value::Double(rod.y1),
				Value::Double(rod.x2),
				Value::Double(rod.y2),
			],
		);
	}

	/// 挂接输入纹理（oaknode 侧 clip 输入值变化时由 param/render 桥
	/// 调用）。`time` 用于多帧纹理选择。空句柄断开。
	pub fn set_input_texture(&self, texture: crate::bridge::render::TextureHandle, _time: f64) {
		let mut slot = self.input_texture.lock().unwrap_or_else(|e| e.into_inner());
		if texture.is_null() {
			*slot = None;
		} else {
			*slot = Some(texture);
		}
	}

	/// 挂接输出纹理（render 驱动创建并经句柄传入；C++
	/// `setOutputTexture` 的 phase 1 单槽版）。`time` 用于多帧纹理
	/// 选择（`// [P2]`）。空句柄断开。
	pub fn set_output_texture(&self, texture: crate::bridge::render::TextureHandle, _time: f64) {
		let mut slot = self
			.output_texture
			.lock()
			.unwrap_or_else(|e| e.into_inner());
		if texture.is_null() {
			*slot = None;
		} else {
			*slot = Some(texture);
		}
	}

	/// 抓取本 clip 在 `time` 的图像（OFX clipGetImage 的宿主侧）。
	/// CPU 路径：把 oakrender 纹理 readback 成 [`crate::image::Image`]
	/// （像素格式按协商结果，全链路 F32）。
	/// `// [P2]` GL 路径：clipLoadTexture 语义在此扩展。
	///
	/// 第 1 期约束：帧必须是 f32 格式（全链路 F32）；`region` 只支持
	/// None（整帧）——转换（u8→f32 等）与子区域随 renderer 桥落地。
	pub fn fetch_image(
		&self,
		time: f64,
		scale: RenderScale,
		region: Option<OfxRectD>,
	) -> crate::error::Result<crate::image::Image> {
		use crate::bridge::render::*;
		use crate::error::Error;

		let _ = (time, scale);
		if region.is_some() {
			return Err(Error::Failed("fetch_image 子区域第 1 期不支持".into()));
		}
		let texture = self
			.input_texture
			.lock()
			.unwrap_or_else(|e| e.into_inner())
			.clone()
			.ok_or(Error::NotFound)?;
		// 占位纹理（dummy）：视作无输入。
		if unsafe { crate::bridge::render::texture_is_dummy(texture) } != 0 {
			return Err(Error::NotFound);
		}
		let mut frame = FrameHandle::null();
		let r = unsafe { crate::bridge::render::texture_get_frame(texture, &mut frame) };
		if r != 0 || frame.is_null() {
			return Err(Error::Failed("纹理无 CPU 帧".into()));
		}
		let mut params = VideoParams::default();
		unsafe { crate::bridge::render::frame_get_params(frame, &mut params) };
		if params.format != PIXEL_FORMAT_F32 {
			unsafe { crate::bridge::render::frame_free(&mut frame) };
			return Err(Error::Failed(format!(
				"输入帧格式 {} 非 F32（第 1 期约束）",
				params.format
			)));
		}
		let (w, h) = (params.width as f64, params.height as f64);
		// 分量按协商结果（getClipPreferences 已写入 clip.props）。
		let components =
			components_from_props(&self.props).unwrap_or(crate::image::Components::Rgba);

		let mut image = crate::image::Image::allocate(
			crate::image::BitDepth::Float,
			components,
			OfxRectD {
				x1: 0.0,
				y1: 0.0,
				x2: w,
				y2: h,
			},
		);
		let src = unsafe { crate::bridge::render::frame_data(frame) };
		if src.is_null() {
			unsafe { crate::bridge::render::frame_free(&mut frame) };
			return Err(Error::Failed("帧无数据".into()));
		}
		// 行优先拷贝（帧行跨度经 linesize 读取——真实 oakrender 帧可
		// 有行填充；目标 Image 恒紧凑。M11 §4 修复：phase 1 假设紧凑
		// 行，对真实 oakrender 的填充帧会写错列）。
		let channels = components.channel_count();
		let tight = (w as usize) * channels * 4;
		let row = unsafe { crate::bridge::render::frame_linesize_bytes(frame) } as usize;
		let row = if row > 0 { row } else { tight };
		let src_bytes = unsafe { std::slice::from_raw_parts(src as *const u8, row * h as usize) };
		let dst = image.pixels_mut();
		for y in 0..h as usize {
			let s = y * row;
			let d = y * tight;
			dst[d..d + tight].copy_from_slice(&src_bytes[s..s + tight]);
		}
		unsafe { crate::bridge::render::frame_free(&mut frame) };
		Ok(image)
	}

	/// 把输出图像回写为 oakrender 纹理（render 完成后由
	/// [`crate::instance::Instance::render`] 的调用方使用）。
	///
	/// 输出纹理由 oakrender 侧创建并经 [`Self::set_output_texture`]
	/// 挂入——本函数取该纹理的 CPU 帧（`texture_get_frame`），按帧
	/// 参数校验 F32 与尺寸后整帧拷贝图像像素（全链路 F32；C++
	/// pluginrenderer 的 `readback/wrap` 路径第 1 期以 CPU 拷贝表达，
	/// GL 走 [`crate::bridge::render`] 的 `// [P2]`）。未挂输出纹理
	/// 或纹理为占位（dummy）→ [`crate::error::Error::NotFound`]。
	/// 成功返回纹理句柄（借用拷贝，调用方负责其生命周期）。
	pub fn store_output_image(
		&self,
		image: &crate::image::Image,
	) -> crate::error::Result<crate::bridge::render::TextureHandle> {
		use crate::bridge::render::*;
		use crate::error::Error;

		let texture = self
			.output_texture
			.lock()
			.unwrap_or_else(|e| e.into_inner())
			.clone()
			.ok_or(Error::NotFound)?;
		if unsafe { texture_is_dummy(texture) } != 0 {
			return Err(Error::NotFound);
		}
		let mut frame = FrameHandle::null();
		let r = unsafe { texture_get_frame(texture, &mut frame) };
		if r != 0 || frame.is_null() {
			return Err(Error::Failed("输出纹理无 CPU 帧".into()));
		}
		let mut params = VideoParams::default();
		unsafe { frame_get_params(frame, &mut params) };
		if params.format != PIXEL_FORMAT_F32 {
			unsafe { frame_free(&mut frame) };
			return Err(Error::Failed(format!(
				"输出帧格式 {} 非 F32（第 1 期约束）",
				params.format
			)));
		}
		let (w, h) = (params.width as usize, params.height as usize);
		// 图像与帧必须同尺寸（全链路 F32；宽高/行宽/总长逐项校验）。
		let tight = w * image.components().channel_count() * 4;
		if tight != image.row_bytes() || tight * h != image.pixels().len() {
			unsafe { frame_free(&mut frame) };
			return Err(Error::Failed("图像尺寸与输出帧不一致".into()));
		}
		let dst = unsafe { frame_data(frame) };
		if dst.is_null() {
			unsafe { frame_free(&mut frame) };
			return Err(Error::Failed("输出帧无数据".into()));
		}
		// 行优先拷贝（目标帧行跨度经 linesize 读取——真实 oakrender
		// 帧可有行填充；M11 §4 修复同 fetch_image）。
		let row = unsafe { frame_linesize_bytes(frame) } as usize;
		let row = if row > 0 { row } else { tight };
		let dst_bytes = unsafe { std::slice::from_raw_parts_mut(dst as *mut u8, row * h) };
		let pixels = image.pixels();
		for y in 0..h {
			let d = y * row;
			let s = y * tight;
			dst_bytes[d..d + tight].copy_from_slice(&pixels[s..s + tight]);
		}
		unsafe { frame_free(&mut frame) };
		Ok(texture)
	}

	/// 本 clip 的时间域（clipGetFrameRange）。
	///
	/// `// TODO(bridge)`：输入范围经 oakrender 帧的时间基推导
	/// （time_base）——随 renderer 桥落地。
	pub fn frame_range(&self) -> crate::error::Result<OfxRangeD> {
		let _ = OfxRangeD::default();
		Err(crate::error::Error::Failed(
			"frame_range 待 renderer 桥".into(),
		))
	}
}
