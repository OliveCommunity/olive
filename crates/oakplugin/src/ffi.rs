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

//! C ABI 出口层：逐字实现 `include/plugin/{host,instance,error}.h`。
//!
//! 本模块的函数体只允许做三件事：解句柄、调 safe Rust 实现、经
//! [`crate::handle::guard*`] 映射结果。签名与公共头逐字一致；
//! 行为契约以头文件文档注释为准。
//!
//! 骨架声明与头文件多处不符，以此文件为准（均已核对
//! include/plugin/{host,instance}.h 与 include/node/node.h）：
//! - `set/get_param` 是 `oaknode_value` POD（非 f64）；
//! - `render` 是 `(dst, src) 纹理 + time`；
//! - `progress_fn` 是 `(progress, userdata)`，非 0 = 中止；
//! - `message_fn` 是 `(type, message, userdata)`。
//! 内省族（param_count 等）为 M11 §2.1 新增，声明随本文件冻结，
//! 头文件同步补（骨架注释约定）。
//!
//! ## 返回值约定（两段式字符串）
//!
//! `buf == NULL` → 返回所需长度（含 NUL，正值）；`buf != NULL` →
//! 写截断拷贝并返回 `OAKPLUGIN_OK`。count 类查询直接返回数值。
//! 这两类不走 [`crate::handle::guard`]（其 Ok 恒映射 0），统一用
//! 本模块的 `catch`/`string_query` 助手。

use std::ffi::{c_char, c_int, c_void, CStr, CString};

use crate::handle::{get, guard, guard_handle, guard_void, make_owned, CHandle};
use crate::param::ParamValue;

// ---- oaknode_value（include/node/node.h:93，字段逐字一致）----

/// oaknode_value_type 的取值（node.h:74）。
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

/// oaknode_value（include/node/node.h:93）。
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct OakNodeValue {
	/// 类型（[`node_value_type`]）。
	pub r#type: c_int,
	/// INT/COMBO 值、BOOL 0/1、RATIONAL 分子。
	pub num: i64,
	/// RATIONAL 分母。
	pub den: i64,
	/// FLOAT f[0]；VEC2/3/4 f[0..n-1]；COLOR r,g,b,a。
	pub f: [f64; 4],
}

impl Default for OakNodeValue {
	fn default() -> Self {
		Self {
			r#type: node_value_type::NONE,
			num: 0,
			den: 0,
			f: [0.0; 4],
		}
	}
}

/// panic 兜底（count/两段式等 guard 不适用的路径）。
fn catch(f: impl FnOnce() -> c_int) -> c_int {
	std::panic::catch_unwind(std::panic::AssertUnwindSafe(f))
		.unwrap_or(crate::error::OAKPLUGIN_E_FAILED)
}

/// 两段式字符串查询公共路径：`buf == NULL` 返回所需长度（含 NUL，
/// 正值）；否则写截断拷贝返回 `OAKPLUGIN_OK`；`get` 失败返回其
/// 错误码。
fn string_query(
	buf: *mut c_char,
	buf_size: c_int,
	get: impl FnOnce() -> crate::error::Result<String>,
) -> c_int {
	catch(|| match get() {
		Ok(s) => {
			let need = (s.len() + 1) as c_int;
			if buf.is_null() {
				return need;
			}
			if buf_size <= 0 {
				return crate::error::OAKPLUGIN_E_INVALID;
			}
			// 留 1 字节给 NUL（buf_size 满时不得写 [buf_size]）。
			let n = (buf_size as usize - 1).min(s.len());
			unsafe {
				std::ptr::copy_nonoverlapping(s.as_ptr(), buf as *mut u8, n);
				*buf.add(n) = 0;
			}
			// 装得下 → OK；装不下 → 返回所需长度（snprintf 风格，
			// 调用方据以扩容重试）。
			if n == s.len() {
				crate::error::OAKPLUGIN_OK
			} else {
				need
			}
		}
		Err(e) => e.code(),
	})
}

// ---- host.h ---------------------------------------------------------------

/// `oakplugin_host_init`：初始化宿主单例（幂等；扫描默认路径集）。
#[no_mangle]
pub unsafe extern "C" fn oakplugin_host_init() -> c_int {
	guard(|| {
		let host = crate::host::Host::global();
		host.cache.scan()?;
		Ok(())
	})
}

/// `oakplugin_host_shutdown`：销毁全部实例并卸载插件。
#[no_mangle]
pub unsafe extern "C" fn oakplugin_host_shutdown() {
	guard_void(|| {
		crate::host::Host::global().shutdown();
	});
}

/// `oakplugin_host_scan`：扫描 bundle 目录（NULL/0 用默认路径集）。
#[no_mangle]
pub unsafe extern "C" fn oakplugin_host_scan(
	bundle_dirs: *const *const c_char,
	dir_count: c_int,
) -> c_int {
	guard(|| {
		let host = crate::host::Host::global();
		if bundle_dirs.is_null() || dir_count <= 0 {
			host.cache.scan()?;
			return Ok(());
		}
		for i in 0..dir_count {
			let dir = unsafe { *bundle_dirs.add(i as usize) };
			if dir.is_null() {
				continue;
			}
			let dir = unsafe { CStr::from_ptr(dir) }
				.to_str()
				.map_err(|_| crate::error::Error::Invalid)?;
			host.cache.scan_path(std::path::Path::new(dir))?;
		}
		Ok(())
	})
}

/// `oakplugin_host_plugin_count`（count 查询：直接取值，panic 兜底）。
#[no_mangle]
pub unsafe extern "C" fn oakplugin_host_plugin_count() -> c_int {
	catch(|| crate::host::Host::global().cache.count() as c_int)
}

/// `oakplugin_host_plugin_id_at`：两段式字符串。
#[no_mangle]
pub unsafe extern "C" fn oakplugin_host_plugin_id_at(
	index: c_int,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	string_query(buf, buf_size, || {
		if index < 0 {
			return Err(crate::error::Error::Invalid);
		}
		let id = crate::host::Host::global()
			.cache
			.at(index as usize)
			.ok_or(crate::error::Error::NotFound)?
			.identifier
			.clone();
		Ok(id)
	})
}

/// `oakplugin_host_plugin_label`：两段式字符串（phase 1：标识本身）。
#[no_mangle]
pub unsafe extern "C" fn oakplugin_host_plugin_label(
	plugin_id: *const c_char,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	string_query(buf, buf_size, || {
		if plugin_id.is_null() {
			return Err(crate::error::Error::Invalid);
		}
		let id = unsafe { CStr::from_ptr(plugin_id) }
			.to_str()
			.map_err(|_| crate::error::Error::Invalid)?;
		let plugin = crate::host::Host::global()
			.cache
			.find(id)
			.ok_or(crate::error::Error::NotFound)?;
		Ok(plugin.identifier.clone())
	})
}

/// 消息回调类型（host.h oakplugin_message_fn：
/// `(type, message, userdata) -> OAKPLUGIN_MESSAGE_ANSWER_NO/YES`）。
pub type MessageFn = unsafe extern "C" fn(
	*const c_char,
	*const c_char,
	*mut c_void,
) -> c_int;

/// `oakplugin_host_set_message_handler`：facade 注册消息出口
/// （message suite 的最终落点）。
#[no_mangle]
pub unsafe extern "C" fn oakplugin_host_set_message_handler(
	f: Option<MessageFn>,
	userdata: *mut c_void,
) {
	guard_void(|| {
		crate::suites::message::set_handler(f, userdata);
	});
}

// ---- instance.h -----------------------------------------------------------

/// 实例句柄的装箱类型（make_owned 的 T）。
pub(crate) type InstanceRef = std::sync::Arc<crate::handle::RefBox<crate::instance::Instance>>;

/// 实例句柄 → 实例的取回。
fn instance_of(h: &CHandle) -> crate::error::Result<InstanceRef> {
	unsafe { get::<InstanceRef>(h) }.cloned().ok_or(crate::error::Error::Invalid)
}

/// `oakplugin_instance_create`：按插件标识创建实例（计数 1；
/// 未知标识返回空句柄）。
#[no_mangle]
pub unsafe extern "C" fn oakplugin_instance_create(plugin_id: *const c_char) -> CHandle {
	guard_handle(|| {
		if plugin_id.is_null() {
			return Err(crate::error::Error::Invalid);
		}
		let id = unsafe { CStr::from_ptr(plugin_id) }
			.to_str()
			.map_err(|_| crate::error::Error::Invalid)?;
		let instance = crate::host::Host::global().create_instance(id, None)?;
		Ok(make_owned(instance))
	})
}

/// `oakplugin_instance_free`：释放一次引用；NULL/空句柄 no-op。
#[no_mangle]
pub unsafe extern "C" fn oakplugin_instance_free(instance: *mut CHandle) {
	guard_void(|| {
		if instance.is_null() {
			return;
		}
		let h = unsafe { &mut *instance };
		if h.is_null() {
			return;
		}
		unsafe { (h.release.expect("release fn 缺失"))(h.ctx) };
		h.ctx = std::ptr::null_mut();
	});
}

/// node_value → ParamValue（按参数类型解释；字符串走专用函数）。
/// pub(crate)：render 驱动的参数覆盖（apply_param_overrides 移植）
/// 复用同一转换。
pub(crate) fn node_value_to_param(
	v: &OakNodeValue,
	ofx_type: &str,
) -> Option<ParamValue> {
	match ofx_type {
		crate::param::TYPE_DOUBLE => {
			if v.r#type == node_value_type::FLOAT {
				Some(ParamValue::Double([v.f[0], 0.0, 0.0], 1))
			} else {
				None
			}
		}
		crate::param::TYPE_DOUBLE2D => {
			if v.r#type == node_value_type::VEC2 {
				Some(ParamValue::Double([v.f[0], v.f[1], 0.0], 2))
			} else {
				None
			}
		}
		crate::param::TYPE_DOUBLE3D => {
			if v.r#type == node_value_type::VEC3 {
				Some(ParamValue::Double([v.f[0], v.f[1], v.f[2]], 3))
			} else {
				None
			}
		}
		crate::param::TYPE_INTEGER => {
			if v.r#type == node_value_type::INT {
				Some(ParamValue::Int([v.num as i32, 0, 0], 1))
			} else {
				None
			}
		}
		crate::param::TYPE_INTEGER2D | crate::param::TYPE_INTEGER3D => {
			let dim = if ofx_type == crate::param::TYPE_INTEGER2D { 2 } else { 3 };
			let a = [v.f[0] as i32, v.f[1] as i32, v.f[2] as i32];
			Some(ParamValue::Int(a, dim))
		}
		crate::param::TYPE_BOOLEAN => {
			if v.r#type == node_value_type::BOOL {
				Some(ParamValue::Bool(v.num != 0))
			} else {
				None
			}
		}
		crate::param::TYPE_CHOICE => {
			if v.r#type == node_value_type::COMBO {
				Some(ParamValue::Choice(v.num as i32))
			} else {
				None
			}
		}
		crate::param::TYPE_RGB => {
			if v.r#type == node_value_type::COLOR {
				Some(ParamValue::Color([v.f[0], v.f[1], v.f[2], 0.0], 3))
			} else {
				None
			}
		}
		crate::param::TYPE_RGBA => {
			if v.r#type == node_value_type::COLOR {
				Some(ParamValue::Color([v.f[0], v.f[1], v.f[2], v.f[3]], 4))
			} else {
				None
			}
		}
		_ => None,
	}
}

/// ParamValue → node_value（字符串走专用函数）。
fn param_to_node_value(v: &ParamValue) -> Option<OakNodeValue> {
	let mut out = OakNodeValue::default();
	match v {
		ParamValue::Double(d, 1) => {
			out.r#type = node_value_type::FLOAT;
			out.f = [d[0], 0.0, 0.0, 0.0];
			Some(out)
		}
		ParamValue::Double(d, 2) => {
			out.r#type = node_value_type::VEC2;
			out.f = [d[0], d[1], 0.0, 0.0];
			Some(out)
		}
		ParamValue::Double(d, 3) => {
			out.r#type = node_value_type::VEC3;
			out.f = [d[0], d[1], d[2], 0.0];
			Some(out)
		}
		ParamValue::Double(_, _) => {
			out.r#type = node_value_type::FLOAT;
			out.f = [0.0; 4];
			Some(out)
		}
		ParamValue::Int(a, 1) => {
			out.r#type = node_value_type::INT;
			out.num = a[0] as i64;
			Some(out)
		}
		ParamValue::Int(a, 2) => {
			out.r#type = node_value_type::VEC2;
			out.f = [a[0] as f64, a[1] as f64, 0.0, 0.0];
			Some(out)
		}
		ParamValue::Int(a, 3) => {
			out.r#type = node_value_type::VEC3;
			out.f = [a[0] as f64, a[1] as f64, a[2] as f64, 0.0];
			Some(out)
		}
		ParamValue::Bool(b) => {
			out.r#type = node_value_type::BOOL;
			out.num = *b as i64;
			Some(out)
		}
		ParamValue::Choice(c) => {
			out.r#type = node_value_type::COMBO;
			out.num = *c as i64;
			Some(out)
		}
		ParamValue::Color(c, 3) => {
			out.r#type = node_value_type::COLOR;
			// alpha=1.0：与 C++ RGBInstance::set 的 value_color(r,g,b,1.0)
			// 一致（RGB 参数视为不透明）。
			out.f = [c[0], c[1], c[2], 1.0];
			Some(out)
		}
		ParamValue::Color(c, 4) => {
			out.r#type = node_value_type::COLOR;
			out.f = *c;
			Some(out)
		}
		_ => None,
	}
}

/// `oakplugin_instance_set_param`：oaknode_value POD 写参数
/// （字符串参数走 set_param_string）。
#[no_mangle]
pub unsafe extern "C" fn oakplugin_instance_set_param(
	instance: CHandle,
	param: *const c_char,
	value: *const OakNodeValue,
) -> c_int {
	guard(|| {
		let inst = instance_of(&instance)?;
		if param.is_null() || value.is_null() {
			return Err(crate::error::Error::Invalid);
		}
		let name = unsafe { CStr::from_ptr(param) }
			.to_str()
			.map_err(|_| crate::error::Error::Invalid)?;
		let v = unsafe { &*value };
		let p = inst.value.params.find(name).ok_or(crate::error::Error::NotFound)?;
		let param_value = node_value_to_param(v, &p.def.ofx_type)
			.ok_or(crate::error::Error::Invalid)?;
		p.set_ofx(param_value);
		Ok(())
	})
}

/// `oakplugin_instance_get_param`：oaknode_value POD 读参数。
#[no_mangle]
pub unsafe extern "C" fn oakplugin_instance_get_param(
	instance: CHandle,
	param: *const c_char,
	out: *mut OakNodeValue,
) -> c_int {
	guard(|| {
		let inst = instance_of(&instance)?;
		if param.is_null() || out.is_null() {
			return Err(crate::error::Error::Invalid);
		}
		let name = unsafe { CStr::from_ptr(param) }
			.to_str()
			.map_err(|_| crate::error::Error::Invalid)?;
		let p = inst.value.params.find(name).ok_or(crate::error::Error::NotFound)?;
		let v = p.get();
		let node = param_to_node_value(&v).ok_or(crate::error::Error::Invalid)?;
		unsafe { *out = node };
		Ok(())
	})
}

/// `oakplugin_instance_set_param_string`。
#[no_mangle]
pub unsafe extern "C" fn oakplugin_instance_set_param_string(
	instance: CHandle,
	param: *const c_char,
	value: *const c_char,
) -> c_int {
	guard(|| {
		let inst = instance_of(&instance)?;
		if param.is_null() || value.is_null() {
			return Err(crate::error::Error::Invalid);
		}
		let name = unsafe { CStr::from_ptr(param) }
			.to_str()
			.map_err(|_| crate::error::Error::Invalid)?;
		let v = unsafe { CStr::from_ptr(value) }.to_bytes();
		let p = inst.value.params.find(name).ok_or(crate::error::Error::NotFound)?;
		match p.def.ofx_type.as_str() {
			crate::param::TYPE_STRING => {
				p.set_ofx(ParamValue::String(CString::new(v).unwrap()));
				Ok(())
			}
			crate::param::TYPE_STRCHOICE => {
				p.set_ofx(ParamValue::StrChoice(CString::new(v).unwrap()));
				Ok(())
			}
			_ => Err(crate::error::Error::Invalid),
		}
	})
}

/// `oakplugin_instance_get_param_string`：两段式字符串。
#[no_mangle]
pub unsafe extern "C" fn oakplugin_instance_get_param_string(
	instance: CHandle,
	param: *const c_char,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	string_query(buf, buf_size, || {
		let inst = instance_of(&instance)?;
		if param.is_null() {
			return Err(crate::error::Error::Invalid);
		}
		let name = unsafe { CStr::from_ptr(param) }
			.to_str()
			.map_err(|_| crate::error::Error::Invalid)?;
		let p = inst.value.params.find(name).ok_or(crate::error::Error::NotFound)?;
		match p.get() {
			ParamValue::String(s) | ParamValue::StrChoice(s) => {
				Ok(s.to_string_lossy().into_owned())
			}
			_ => Err(crate::error::Error::Invalid),
		}
	})
}

/// `oakplugin_instance_render`：渲染一帧（dst 纹理输出、src 纹理
/// 输入可空）。CPU 路径的单输入便捷入口；多输入/GL/序列括号走
/// [`oakplugin_instance_render_job`]。
#[no_mangle]
pub unsafe extern "C" fn oakplugin_instance_render(
	instance: CHandle,
	dst: CHandle,
	src: CHandle,
	time_seconds: f64,
) -> c_int {
	guard(|| {
		let inst = instance_of(&instance)?;
		// 取消检查必须在任何插件调用（协商/渲染）之前——
		// shutdown 后插件入口已卸载，协商会踩已卸载的 lib。
		if inst.value.cancel.load(std::sync::atomic::Ordering::Relaxed) {
			return Err(crate::error::Error::Failed("已取消".into()));
		}
		if dst.is_null() {
			return Err(crate::error::Error::Invalid);
		}

		// 输出纹理的帧与参数（宽/高）。
		use crate::bridge::render::{self, FrameHandle, VideoParams};
		let mut frame = FrameHandle::null();
		let r = unsafe { render::texture_get_frame(dst, &mut frame) };
		if r != 0 || frame.is_null() {
			return Err(crate::error::Error::Failed("输出纹理无 CPU 帧".into()));
		}
		let mut params = VideoParams::default();
		if unsafe { render::frame_get_params(frame, &mut params) } != 0 {
			unsafe { render::frame_free(&mut frame) };
			return Err(crate::error::Error::Failed("输出帧无参数".into()));
		}
		let (w, h) = (params.width as f64, params.height as f64);
		if w <= 0.0 || h <= 0.0 {
			unsafe { render::frame_free(&mut frame) };
			return Err(crate::error::Error::Invalid);
		}

		// 协商（写回 clip props；决定输出格式）。
		let prefs = inst.value.get_clip_preferences()?;
		let components = match prefs.output_components.as_str() {
			"OfxImageComponentRGBA" => crate::image::Components::Rgba,
			"OfxImageComponentRGB" => crate::image::Components::Rgb,
			"OfxImageComponentAlpha" => crate::image::Components::Alpha,
			_ => return Err(crate::error::Error::Failed("协商分量未知".into())),
		};
		if prefs.output_bit_depth != "OfxBitDepthFloat" {
			return Err(crate::error::Error::Failed("第 1 期仅支持 F32 输出".into()));
		}

		// 输入纹理挂到第一个输入 clip（generator 的 src 为空句柄）。
		if !src.is_null() {
			if let Some(input) = inst.value.clips.iter().find(|c| c.name != "Output") {
				input.set_input_texture(src, time_seconds);
			}
		}

		let output = std::sync::Arc::new(crate::image::Image::allocate(
			crate::image::BitDepth::Float,
			components,
			crate::instance::OfxRectD { x1: 0.0, y1: 0.0, x2: w, y2: h },
		));
		let window = crate::instance::OfxRectD { x1: 0.0, y1: 0.0, x2: w, y2: h };
		let scale = crate::instance::RenderScale { x: 1.0, y: 1.0 };
		let render = inst.value.render(time_seconds, scale, window, output.clone());
		// 结果写回输出帧（行跨度感知；M11 §4 修复：phase 1 假设紧凑
		// 行并泄漏 frame 句柄——统一走驱动装配路径）。
		let result = match render {
			Ok(()) => crate::render_driver::write_output_frame(dst, &output),
			Err(e) => Err(e),
		};
		unsafe { render::frame_free(&mut frame) };
		result
	})
}

/// `oakplugin_instance_set_progress_cb`。
#[no_mangle]
pub unsafe extern "C" fn oakplugin_instance_set_progress_cb(
	instance: CHandle,
	cb: Option<crate::progress::ProgressFn>,
	userdata: *mut c_void,
) -> c_int {
	guard(|| {
		let inst = instance_of(&instance)?;
		*inst
			.value
			.progress_cb
			.lock()
			.unwrap_or_else(|e| e.into_inner()) = cb.map(|f| (f, userdata as usize));
		Ok(())
	})
}

// ---- render 驱动 C ABI（M11 §4 新增；声明同步补 include/plugin/instance.h）----

/// `oakplugin_job_value`（instance.h 新增）：参数覆盖条目
/// （key = 参数名；value = oaknode_value POD；字符串参数走
/// oakplugin_instance_set_param_string）。
#[repr(C)]
#[derive(Clone, Copy)]
pub struct OakPluginJobValue {
	/// 参数名（NUL 结尾）。
	pub key: *const c_char,
	/// 参数值。
	pub value: OakNodeValue,
}

/// `oakplugin_job_texture`（instance.h 新增）：输入 clip 纹理条目。
#[repr(C)]
#[derive(Clone, Copy)]
pub struct OakPluginJobTexture {
	/// clip 名（NUL 结尾）。
	pub clip: *const c_char,
	/// 纹理句柄（借用，job 内有效）。
	pub texture: CHandle,
}

/// `oakplugin_instance_render_begin_sequence`（instance.h 新增）：
/// beginSequenceRender 括号。oakrender 对同一实例的一批帧先
/// begin 后 end，中间逐帧 render_job（ofxRendering："All calls to the
/// render action are bracketed by a pair of begin/end sequence render
/// actions"）。`interactive` 为信息性标记（Phase 2 不传入 action）。
#[no_mangle]
pub unsafe extern "C" fn oakplugin_instance_render_begin_sequence(
	instance: CHandle,
	start_time: f64,
	end_time: f64,
	_interactive: c_int,
) -> c_int {
	guard(|| {
		let inst = instance_of(&instance)?;
		if inst.value.cancel.load(std::sync::atomic::Ordering::Relaxed) {
			return Err(crate::error::Error::Failed("已取消".into()));
		}
		let range = crate::instance::OfxRangeD { min: start_time, max: end_time };
		inst.value.begin_sequence_render(range)
	})
}

/// `oakplugin_instance_render_end_sequence`（instance.h 新增）：
/// endSequenceRender 括号。
#[no_mangle]
pub unsafe extern "C" fn oakplugin_instance_render_end_sequence(
	instance: CHandle,
	start_time: f64,
	end_time: f64,
	_interactive: c_int,
) -> c_int {
	guard(|| {
		let inst = instance_of(&instance)?;
		let range = crate::instance::OfxRangeD { min: start_time, max: end_time };
		inst.value.end_sequence_render(range)
	})
}

/// `oakplugin_instance_render_job`（instance.h 新增；M11 §4 的目标
/// 形态）：一帧渲染的**唯一** C ABI 调用——oakrender 的 PluginJob
/// 语义全部收编进 [`crate::render_driver`]。
///
/// 参数：
/// - `dst`：目标纹理（oakrender 创建；GL 模式下调用方须先附着为
///   渲染器输出目标并保持 GL 上下文 current——ofxGPURender.h
///   "OpenGL Current Context" 规则）；
/// - `src`：主输入纹理（effect_input_id / SimpleSource；可空）；
/// - `effect_input_id`：job.src 落点的 clip 名（可空）；
/// - `inputs`/`input_count`：其余输入 clip 的纹理表；
/// - `values`/`value_count`：参数覆盖；
/// - `renderer`：GL 渲染器（空句柄 → CPU 路径）；
/// - `clear_destination`/`interactive`：信息性（Phase 2，见
///   [`crate::render_driver::RenderJob`]）。
///
/// 驱动流程见 [`crate::render_driver::render_frame`]：RoI/RoD、
/// 多输入收集、isIdentity 短路、参数覆盖、CPU/GL 渲染与输出装配。
#[no_mangle]
pub unsafe extern "C" fn oakplugin_instance_render_job(
	instance: CHandle,
	dst: CHandle,
	time_seconds: f64,
	clear_destination: c_int,
	interactive: c_int,
	effect_input_id: *const c_char,
	src: CHandle,
	inputs: *const OakPluginJobTexture,
	input_count: c_int,
	values: *const OakPluginJobValue,
	value_count: c_int,
	renderer: CHandle,
) -> c_int {
	guard(|| {
		let inst = instance_of(&instance)?;
		if dst.is_null() {
			return Err(crate::error::Error::Invalid);
		}

		let effect_input_id = if effect_input_id.is_null() {
			None
		} else {
			Some(
				unsafe { CStr::from_ptr(effect_input_id) }
					.to_str()
					.map_err(|_| crate::error::Error::Invalid)?
					.to_string(),
			)
		};

		let mut job_inputs = Vec::new();
		if !inputs.is_null() && input_count > 0 {
			for i in 0..input_count {
				let entry = unsafe { &*inputs.add(i as usize) };
				if entry.clip.is_null() {
					continue;
				}
				let name = unsafe { CStr::from_ptr(entry.clip) }
					.to_str()
					.map_err(|_| crate::error::Error::Invalid)?
					.to_string();
				job_inputs.push((name, entry.texture));
			}
		}

		let mut job_values = Vec::new();
		if !values.is_null() && value_count > 0 {
			for i in 0..value_count {
				let entry = unsafe { &*values.add(i as usize) };
				if entry.key.is_null() {
					continue;
				}
				let key = unsafe { CStr::from_ptr(entry.key) }
					.to_str()
					.map_err(|_| crate::error::Error::Invalid)?
					.to_string();
				job_values.push((key, entry.value));
			}
		}

		let job = crate::render_driver::RenderJob {
			time: time_seconds,
			dst,
			src,
			effect_input_id,
			inputs: job_inputs,
			values: job_values,
			renderer: if renderer.is_null() { None } else { Some(renderer) },
			clear_destination: clear_destination != 0,
			interactive: interactive != 0,
		};
		// 驱动内部已做取消检查与实例锁；RoI 返回值 Phase 2 供宿主
		// 记录（输入由 oakrender 整帧提供，不做 RoI 裁剪）。
		crate::render_driver::render_frame(&inst.value, &job)?;
		Ok(())
	})
}

/// `oakplugin_instance_cancel`。
#[no_mangle]
pub unsafe extern "C" fn oakplugin_instance_cancel(instance: CHandle) -> c_int {
	guard(|| {
		let inst = instance_of(&instance)?;
		inst.value
			.cancel
			.store(true, std::sync::atomic::Ordering::Relaxed);
		Ok(())
	})
}

/// `oakplugin_debug_alive_count`：活跃对象数（泄漏断言）。
#[no_mangle]
pub unsafe extern "C" fn oakplugin_debug_alive_count() -> c_int {
	crate::host::Host::global().alive_count() as c_int
}

// ---- 内省族（M11 §2.1，0 期新增；声明随本文件冻结，头文件同步补）----

/// 取第 `index` 个参数实例。
fn param_at(
	inst: &crate::instance::Instance,
	index: c_int,
) -> crate::error::Result<&crate::param::ParamInstance> {
	if index < 0 {
		return Err(crate::error::Error::Invalid);
	}
	inst.params
		.params
		.get(index as usize)
		.map(|b| b.as_ref())
		.ok_or(crate::error::Error::NotFound)
}

/// 参数数量。
#[no_mangle]
pub unsafe extern "C" fn oakplugin_instance_param_count(instance: CHandle) -> c_int {
	catch(|| match instance_of(&instance) {
		Ok(inst) => inst.value.params.params.len() as c_int,
		Err(_) => crate::error::OAKPLUGIN_E_INVALID,
	})
}

/// 参数名（两段式）。
#[no_mangle]
pub unsafe extern "C" fn oakplugin_instance_param_name(
	instance: CHandle,
	index: c_int,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	string_query(buf, buf_size, || {
		let inst = instance_of(&instance)?;
		let p = param_at(&inst.value, index)?;
		Ok(p.def.name.clone())
	})
}

/// 参数 OFX 类型（两段式，如 "OfxParamTypeDouble"）。
#[no_mangle]
pub unsafe extern "C" fn oakplugin_instance_param_type(
	instance: CHandle,
	index: c_int,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	string_query(buf, buf_size, || {
		let inst = instance_of(&instance)?;
		let p = param_at(&inst.value, index)?;
		Ok(p.def.ofx_type.clone())
	})
}

/// 参数标签（两段式）。
#[no_mangle]
pub unsafe extern "C" fn oakplugin_instance_param_label(
	instance: CHandle,
	index: c_int,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	string_query(buf, buf_size, || {
		let inst = instance_of(&instance)?;
		let p = param_at(&inst.value, index)?;
		Ok(prop_str(&p.def.props, crate::param::PROP_LABEL))
	})
}

/// 参数 hint（两段式）。
#[no_mangle]
pub unsafe extern "C" fn oakplugin_instance_param_hint(
	instance: CHandle,
	index: c_int,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	string_query(buf, buf_size, || {
		let inst = instance_of(&instance)?;
		let p = param_at(&inst.value, index)?;
		Ok(prop_str(&p.def.props, crate::param::P_HINT))
	})
}

/// 参数父组名（两段式；无父组返回空串）。
#[no_mangle]
pub unsafe extern "C" fn oakplugin_instance_param_parent(
	instance: CHandle,
	index: c_int,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	string_query(buf, buf_size, || {
		let inst = instance_of(&instance)?;
		let p = param_at(&inst.value, index)?;
		Ok(prop_str(&p.def.props, crate::param::P_PARENT))
	})
}

/// 参数 secret 标记（out 0/1）。
#[no_mangle]
pub unsafe extern "C" fn oakplugin_instance_param_secret(
	instance: CHandle,
	index: c_int,
	out: *mut c_int,
) -> c_int {
	guard(|| {
		let inst = instance_of(&instance)?;
		if out.is_null() {
			return Err(crate::error::Error::Invalid);
		}
		let p = param_at(&inst.value, index)?;
		unsafe { *out = prop_int(&p.def.props, crate::param::P_SECRET) };
		Ok(())
	})
}

/// 参数 display min/max（double out 两参）。
#[no_mangle]
pub unsafe extern "C" fn oakplugin_instance_param_display_range(
	instance: CHandle,
	index: c_int,
	out_min: *mut f64,
	out_max: *mut f64,
) -> c_int {
	guard(|| {
		let inst = instance_of(&instance)?;
		if out_min.is_null() || out_max.is_null() {
			return Err(crate::error::Error::Invalid);
		}
		let p = param_at(&inst.value, index)?;
		unsafe {
			*out_min = prop_double(&p.def.props, crate::param::P_DISPLAY_MIN);
			*out_max = prop_double(&p.def.props, crate::param::P_DISPLAY_MAX);
		}
		Ok(())
	})
}

/// choice 参数的选项数。
#[no_mangle]
pub unsafe extern "C" fn oakplugin_instance_param_choice_count(
	instance: CHandle,
	index: c_int,
) -> c_int {
	catch(|| match instance_of(&instance) {
		Ok(inst) => match param_at(&inst.value, index) {
			Ok(p) => p.def.props.dimension(crate::param::P_CHOICE_OPTION) as c_int,
			Err(_) => crate::error::OAKPLUGIN_E_NOT_FOUND,
		},
		Err(_) => crate::error::OAKPLUGIN_E_INVALID,
	})
}

/// `oakplugin_instance_param_choice_label`（两段式）。
#[no_mangle]
pub unsafe extern "C" fn oakplugin_instance_param_choice_label(
	instance: CHandle,
	index: c_int,
	choice: c_int,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	string_query(buf, buf_size, || {
		let inst = instance_of(&instance)?;
		let p = param_at(&inst.value, index)?;
		Ok(choice_str(&p.def.props, choice))
	})
}

/// `oakplugin_instance_param_choice_value`（两段式；第 1 期与 label
/// 相同——插件未设 ChoiceOrder 时 OFX 约定 value == label）。
#[no_mangle]
pub unsafe extern "C" fn oakplugin_instance_param_choice_value(
	instance: CHandle,
	index: c_int,
	choice: c_int,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	string_query(buf, buf_size, || {
		let inst = instance_of(&instance)?;
		let p = param_at(&inst.value, index)?;
		Ok(choice_str(&p.def.props, choice))
	})
}

/// 参数默认值（double 族；字符串/字节组走专用函数）。
#[no_mangle]
pub unsafe extern "C" fn oakplugin_instance_param_default_double(
	instance: CHandle,
	index: c_int,
	component: c_int,
	out: *mut f64,
) -> c_int {
	guard(|| {
		let inst = instance_of(&instance)?;
		if out.is_null() {
			return Err(crate::error::Error::Invalid);
		}
		let p = param_at(&inst.value, index)?;
		unsafe { *out = prop_any(&p.def.props, crate::param::P_DEFAULT, component) };
		Ok(())
	})
}

/// 参数默认字符串值（两段式）。
#[no_mangle]
pub unsafe extern "C" fn oakplugin_instance_param_default_string(
	instance: CHandle,
	index: c_int,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	string_query(buf, buf_size, || {
		let inst = instance_of(&instance)?;
		let p = param_at(&inst.value, index)?;
		Ok(prop_str(&p.def.props, crate::param::P_DEFAULT))
	})
}

/// clip 数量（含 Output）。
#[no_mangle]
pub unsafe extern "C" fn oakplugin_instance_clip_count(instance: CHandle) -> c_int {
	catch(|| match instance_of(&instance) {
		Ok(inst) => inst.value.clips.len() as c_int,
		Err(_) => crate::error::OAKPLUGIN_E_INVALID,
	})
}

/// clip 名（两段式）。
#[no_mangle]
pub unsafe extern "C" fn oakplugin_instance_clip_name(
	instance: CHandle,
	index: c_int,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	string_query(buf, buf_size, || {
		let inst = instance_of(&instance)?;
		if index < 0 {
			return Err(crate::error::Error::Invalid);
		}
		let name = inst
			.value
			.clips
			.get(index as usize)
			.map(|c| c.name.clone())
			.ok_or(crate::error::Error::NotFound)?;
		Ok(name)
	})
}

/// clip 标签与可选性（label 两段式；optional out 0/1）。
/// label 的两段式返回值不能经 guard（Ok 恒 0）——整体用 catch。
#[no_mangle]
pub unsafe extern "C" fn oakplugin_instance_clip_info(
	instance: CHandle,
	index: c_int,
	optional: *mut c_int,
	label: *mut c_char,
	label_size: c_int,
) -> c_int {
	catch(|| {
		let inst = match instance_of(&instance) {
			Ok(i) => i,
			Err(_) => return crate::error::OAKPLUGIN_E_INVALID,
		};
		if optional.is_null() || index < 0 {
			return crate::error::OAKPLUGIN_E_INVALID;
		}
		let clip = match inst.value.clips.get(index as usize) {
			Some(c) => c,
			None => return crate::error::OAKPLUGIN_E_NOT_FOUND,
		};
		unsafe { *optional = prop_int(&clip.props, crate::descriptor::CLIP_OPTIONAL) };
		string_query(label, label_size, || Ok(prop_str(&clip.props, crate::param::PROP_LABEL)))
	})
}

// ---- 属性读取助手 ---------------------------------------------------------

/// 取字符串属性（缺失返回空串）。
fn prop_str(props: &crate::property::PropertySet, name: &str) -> String {
	props
		.get(name, 0)
		.map(|v| match v {
			crate::property::Value::String(s) => s.to_string_lossy().into_owned(),
			_ => String::new(),
		})
		.unwrap_or_default()
}

/// 取 int 属性（缺失 0）。
fn prop_int(props: &crate::property::PropertySet, name: &str) -> c_int {
	props
		.get(name, 0)
		.map(|v| match v {
			crate::property::Value::Int(i) => i,
			_ => 0,
		})
		.unwrap_or(0)
}

/// 取 double 属性（缺失 0；Int 转 f64）。
fn prop_double(props: &crate::property::PropertySet, name: &str) -> f64 {
	prop_any(props, name, 0)
}

/// 取属性第 `component` 个元素为 f64（Double/Int；越界 0）。
fn prop_any(props: &crate::property::PropertySet, name: &str, component: c_int) -> f64 {
	if component < 0 {
		return 0.0;
	}
	props
		.get(name, component as usize)
		.map(|v| match v {
			crate::property::Value::Double(d) => d,
			crate::property::Value::Int(i) => i as f64,
			_ => 0.0,
		})
		.unwrap_or(0.0)
}

/// choice 选项字符串（越界返回空串）。
fn choice_str(props: &crate::property::PropertySet, choice: c_int) -> String {
	if choice < 0 {
		return String::new();
	}
	props
		.get(crate::param::P_CHOICE_OPTION, choice as usize)
		.map(|v| match v {
			crate::property::Value::String(s) => s.to_string_lossy().into_owned(),
			_ => String::new(),
		})
		.unwrap_or_default()
}
