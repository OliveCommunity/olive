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

//! oakrender C ABI 导入（clip↔纹理桥用到的子集）。
//!
//! 声明以 `include/render/renderer.h` 为准（骨架的
//! `oakrender_texture_get_frame`/`oakrender_texture_wrap_native`/
//! `oakrender_texture_is_dummy` 与头文件不符，弃用；真实符号为
//! `oakrender_display_texture_*` 与 `oakrender_codec_frame_*`）。
//! `oakrender_display_texture_wrap_native` 是 C++ 专属符号
//! （TexturePtr 引用），Rust 不可调用——输出纹理由 oakrender 侧
//! 创建并经句柄传入，宿主只写其帧。
//!
//! ## 双态实现
//!
//! - 默认：直接 Rust 调用 oakrender 的 `ffi`（单库化，见
//!   `docs/zh/plans/riir/single-lib.md`）；
//! - `--features test-stubs`：库内状态桩 + 状态访问器（[`stub`]，纯
//!   Rust、无 `#[no_mangle]`，与真实 oakrender 的导出不冲突）——像素
//!   路径在 cargo test 里全链路可跑。两种形态的调用面
//!   （`texture_get_frame` 等）完全一致。

use std::ffi::c_void;

/// `oakrender_video_params` POD — single-lib unification: aliases the
/// oakrender crate's struct (identical layout; include/render/renderer.h:78).
pub type VideoParams = oakrender::ffi::OakRenderVideoParams;


/// olive::PixelFormat::Format 的 f32 值。
pub const PIXEL_FORMAT_F32: i32 = 4;
/// olive::PixelFormat::Format 的 u8 值。
pub const PIXEL_FORMAT_U8: i32 = 0;

/// oakrender 渲染器句柄（`OakRenderRenderer`，值型）。
pub type RendererHandle = crate::handle::CHandle;

/// oakrender 纹理句柄（`OakRenderTexture`，值型）。
pub type TextureHandle = crate::handle::CHandle;

/// oakrender 帧句柄（`OakCodecFrame`，值型；布局与 CHandle 一致）。
pub type FrameHandle = crate::handle::CHandle;

// ---- 桥调用面 ------------------------------------------------------------

/// 纹理的 CPU 帧（Texture::frame()）；`*out` 收到保留引用。
pub(crate) unsafe fn texture_get_frame(texture: TextureHandle, out: *mut FrameHandle) -> i32 {
	#[cfg(feature = "test-stubs")]
	{
		unsafe { stub::texture_get_frame(texture, out) }
	}
	#[cfg(not(feature = "test-stubs"))]
	{
		unsafe { oakrender::ffi::renderer::oakrender_display_texture_get_frame(texture, out) }
	}
}

/// 纹理是否占位（dummy）；符号缺失 → 1（视为占位）。
pub(crate) unsafe fn texture_is_dummy(texture: TextureHandle) -> i32 {
	#[cfg(feature = "test-stubs")]
	{
		unsafe { stub::texture_is_dummy(texture) }
	}
	#[cfg(not(feature = "test-stubs"))]
	{
		unsafe { oakrender::ffi::renderer::oakrender_display_texture_is_dummy(texture) }
	}
}

/// 帧宽（空帧为 0）。
pub(crate) unsafe fn frame_width(frame: FrameHandle) -> i32 {
	#[cfg(feature = "test-stubs")]
	{
		unsafe { stub::frame_width(frame) }
	}
	#[cfg(not(feature = "test-stubs"))]
	{
		unsafe { oakrender::ffi::renderer::oakrender_codec_frame_width(frame) }
	}
}

/// 帧高。
pub(crate) unsafe fn frame_height(frame: FrameHandle) -> i32 {
	#[cfg(feature = "test-stubs")]
	{
		unsafe { stub::frame_height(frame) }
	}
	#[cfg(not(feature = "test-stubs"))]
	{
		unsafe { oakrender::ffi::renderer::oakrender_codec_frame_height(frame) }
	}
}

/// 借用的像素数据指针（最终 release 前有效）。
pub(crate) unsafe fn frame_data(frame: FrameHandle) -> *mut c_void {
	#[cfg(feature = "test-stubs")]
	{
		unsafe { stub::frame_data(frame) }
	}
	#[cfg(not(feature = "test-stubs"))]
	{
		unsafe { oakrender::ffi::renderer::oakrender_codec_frame_data(frame) }
	}
}

/// 帧的视频参数。
pub(crate) unsafe fn frame_get_params(frame: FrameHandle, out: *mut VideoParams) -> i32 {
	#[cfg(feature = "test-stubs")]
	{
		unsafe { stub::frame_get_params(frame, out) }
	}
	#[cfg(not(feature = "test-stubs"))]
	{
		unsafe { oakrender::ffi::renderer::oakrender_codec_frame_get_params(frame, out) }
	}
}

/// 按参数分配像素缓冲。
pub(crate) unsafe fn frame_allocate(frame: FrameHandle) -> i32 {
	#[cfg(feature = "test-stubs")]
	{
		unsafe { stub::frame_allocate(frame) }
	}
	#[cfg(not(feature = "test-stubs"))]
	{
		unsafe { oakrender::ffi::renderer::oakrender_codec_frame_allocate(frame) }
	}
}

/// 释放一次帧引用并清空句柄（NULL/空句柄 no-op）。
pub(crate) unsafe fn frame_free(frame: *mut FrameHandle) {
	#[cfg(feature = "test-stubs")]
	{
		unsafe { stub::frame_free(frame) }
	}
	#[cfg(not(feature = "test-stubs"))]
	{
		unsafe { oakrender::ffi::renderer::oakrender_codec_frame_free(frame) }
	}
}

/// 帧的行跨度（字节；空帧 0）。M11 第 2 期新增导入
/// （`oakrender_codec_frame_linesize_bytes`，renderer.h:311）：CPU
/// 拷贝路径（fetch/store/驱动输出装配）用它兼容真实 oakrender 的
/// 行填充，不再假设紧凑行布局。
pub(crate) unsafe fn frame_linesize_bytes(frame: FrameHandle) -> i32 {
	#[cfg(feature = "test-stubs")]
	{
		unsafe { stub::frame_linesize_bytes(frame) }
	}
	#[cfg(not(feature = "test-stubs"))]
	{
		unsafe { oakrender::ffi::renderer::oakrender_codec_frame_linesize_bytes(frame) }
	}
}

// ---- 渲染器族（GL 路径；M11 §4）--------------------------------------------

/// 按后端名创建渲染器（`oakrender_display_renderer_create_dynamic`，
/// renderer.h:155）。空指针/空串 → 空句柄。
#[allow(dead_code)] // 契约完整导入：GL 测试/后续路径按需使用（renderer.h 同签名）
pub(crate) unsafe fn renderer_create_dynamic(backend: *const std::ffi::c_char) -> RendererHandle {
	#[cfg(feature = "test-stubs")]
	{
		unsafe { stub::renderer_create_dynamic(backend) }
	}
	#[cfg(not(feature = "test-stubs"))]
	{
		unsafe { oakrender::ffi::renderer::oakrender_display_renderer_create_dynamic(backend) }
	}
}

/// 初始化渲染器（`oakrender_display_renderer_init`，renderer.h:175）。
/// `gl_context` 为借用指针（NULL = 后端默认上下文路径）。
#[allow(dead_code)] // 契约完整导入：GL 测试/后续路径按需使用（renderer.h 同签名）
pub(crate) unsafe fn renderer_init(
	renderer: RendererHandle,
	gl_context: *mut std::ffi::c_void,
) -> i32 {
	#[cfg(feature = "test-stubs")]
	{
		unsafe { stub::renderer_init(renderer, gl_context) }
	}
	#[cfg(not(feature = "test-stubs"))]
	{
		unsafe { oakrender::ffi::renderer::oakrender_display_renderer_init(renderer, gl_context) }
	}
}

/// 渲染器是否 OpenGL 后端（`oakrender_display_renderer_is_open_gl`，
/// renderer.h:189）。
pub(crate) unsafe fn renderer_is_open_gl(renderer: RendererHandle) -> i32 {
	#[cfg(feature = "test-stubs")]
	{
		unsafe { stub::renderer_is_open_gl(renderer) }
	}
	#[cfg(not(feature = "test-stubs"))]
	{
		unsafe { oakrender::ffi::renderer::oakrender_display_renderer_is_open_gl(renderer) }
	}
}

/// 释放一次渲染器引用并清空句柄（`oakrender_display_renderer_destroy`，
/// renderer.h:183）。
#[allow(dead_code)] // 契约完整导入：GL 测试/后续路径按需使用（renderer.h 同签名）
pub(crate) unsafe fn renderer_destroy(renderer: *mut RendererHandle) {
	#[cfg(feature = "test-stubs")]
	{
		unsafe { stub::renderer_destroy(renderer) }
	}
	#[cfg(not(feature = "test-stubs"))]
	{
		unsafe { oakrender::ffi::renderer::oakrender_display_renderer_destroy(renderer) }
	}
}

/// 在渲染器上创建纹理（`oakrender_display_texture_create`，
/// renderer.h:204）。`pixels` 可空（未初始化）；`linesize` 为行跨度
/// 字节数（0 = 紧凑行；pixels 为空时 0）。
///
/// 单位约定（M11 第 2 期）：**字节**——以 renderer.h:206 的明文
/// 契约为准（`Stride of pixels in bytes`）。C++ 调用点传像素行跨度，
/// 由 oakrender 侧实现 C ABI 时换算；本 crate 侧一律传字节。
pub(crate) unsafe fn texture_create(
	renderer: RendererHandle,
	params: *const VideoParams,
	pixels: *const std::ffi::c_void,
	linesize: i32,
) -> TextureHandle {
	#[cfg(feature = "test-stubs")]
	{
		unsafe { stub::texture_create(renderer, params, pixels, linesize) }
	}
	#[cfg(not(feature = "test-stubs"))]
	{
		unsafe {
			oakrender::ffi::renderer::oakrender_display_texture_create(
				renderer, params, pixels, linesize,
			)
		}
	}
}

/// 纹理的原生 GL id（`oakrender_display_texture_id`，renderer.h:245；
/// 空/占位/无 id 纹理为 0）。
pub(crate) unsafe fn texture_id(texture: TextureHandle) -> i32 {
	#[cfg(feature = "test-stubs")]
	{
		unsafe { stub::texture_id(texture) }
	}
	#[cfg(not(feature = "test-stubs"))]
	{
		unsafe { oakrender::ffi::renderer::oakrender_display_texture_id(texture) }
	}
}

/// 纹理参数（`oakrender_display_texture_get_params`，renderer.h:233）。
pub(crate) unsafe fn texture_get_params(texture: TextureHandle, out: *mut VideoParams) -> i32 {
	#[cfg(feature = "test-stubs")]
	{
		unsafe { stub::texture_get_params(texture, out) }
	}
	#[cfg(not(feature = "test-stubs"))]
	{
		unsafe { oakrender::ffi::renderer::oakrender_display_texture_get_params(texture, out) }
	}
}

/// 从纹理下载像素（`oakrender_display_texture_download`，
/// renderer.h:228；`linesize` 行跨度字节数，0 = 紧凑行）。
#[allow(dead_code)] // 契约完整导入：GL 测试/后续路径按需使用（renderer.h 同签名）
pub(crate) unsafe fn texture_download(
	texture: TextureHandle,
	pixels: *mut std::ffi::c_void,
	linesize: i32,
) -> i32 {
	#[cfg(feature = "test-stubs")]
	{
		unsafe { stub::texture_download(texture, pixels, linesize) }
	}
	#[cfg(not(feature = "test-stubs"))]
	{
		unsafe { oakrender::ffi::renderer::oakrender_display_texture_download(texture, pixels, linesize) }
	}
}

/// 再取一次引用（`oakrender_display_texture_retain`，renderer.h:215）。
#[allow(dead_code)] // 契约完整导入：GL 测试/后续路径按需使用（renderer.h 同签名）
pub(crate) unsafe fn texture_retain(texture: TextureHandle) -> TextureHandle {
	#[cfg(feature = "test-stubs")]
	{
		unsafe { stub::texture_retain(texture) }
	}
	#[cfg(not(feature = "test-stubs"))]
	{
		unsafe { oakrender::ffi::renderer::oakrender_display_texture_retain(texture) }
	}
}

/// 释放一次纹理引用并清空句柄（`oakrender_display_texture_free`，
/// renderer.h:223）。
pub(crate) unsafe fn texture_free(texture: *mut TextureHandle) {
	#[cfg(feature = "test-stubs")]
	{
		unsafe { stub::texture_free(texture) }
	}
	#[cfg(not(feature = "test-stubs"))]
	{
		unsafe { oakrender::ffi::renderer::oakrender_display_texture_free(texture) }
	}
}

/// 上传像素到纹理（`oakrender_display_texture_upload`，renderer.h:225；
/// `linesize` 行跨度字节数，0 = 紧凑行）。
#[allow(dead_code)] // 契约完整导入：GL 测试/后续路径按需使用（renderer.h 同签名）
pub(crate) unsafe fn texture_upload(
	texture: TextureHandle,
	pixels: *const std::ffi::c_void,
	linesize: i32,
) -> i32 {
	#[cfg(feature = "test-stubs")]
	{
		unsafe { stub::texture_upload(texture, pixels, linesize) }
	}
	#[cfg(not(feature = "test-stubs"))]
	{
		unsafe { oakrender::ffi::renderer::oakrender_display_texture_upload(texture, pixels, linesize) }
	}
}

/// 按 GL id 从渲染器下载像素（`oakrender_display_renderer_download_from_texture`，
/// renderer.h:333；`linesize` 行跨度字节数，0 = 紧凑行）。
#[allow(dead_code)] // 契约完整导入：GL 测试/后续路径按需使用（renderer.h 同签名）
pub(crate) unsafe fn renderer_download_from_texture(
	renderer: RendererHandle,
	texture_id: i32,
	params: *const VideoParams,
	dst: *mut std::ffi::c_void,
	linesize: i32,
) -> i32 {
	#[cfg(feature = "test-stubs")]
	{
		unsafe { stub::renderer_download_from_texture(renderer, texture_id, params, dst, linesize) }
	}
	#[cfg(not(feature = "test-stubs"))]
	{
		unsafe {
			oakrender::ffi::renderer::oakrender_display_renderer_download_from_texture(
				renderer, texture_id, params, dst, linesize,
			)
		}
	}
}

// ---- 测试桩（--features test-stubs）--------------------------------------

/// oakrender 测试桩：库内 no_mangle 符号 + 状态访问器。
///
/// 纹理/帧句柄的 ctx 约定（桩内约定，与真实句柄无冲突）：
/// - dst 纹理 ctx = 0xA1 → 输出帧 ctx = 0xB1；
/// - src 纹理 ctx = 0xA2 → 输入帧 ctx = 0xB2。
#[cfg(feature = "test-stubs")]
pub mod stub {
	use super::*;
	use crate::handle::CHandle;
	use std::sync::Mutex;

	/// 测试帧（数据 + 参数）。
	#[derive(Clone)]
	pub struct StubFrame {
		/// 视频参数。
		pub params: VideoParams,
		/// 像素缓冲（行优先）。
		pub data: Vec<u8>,
	}

	impl StubFrame {
		fn new(width: i32, height: i32, format: i32) -> Self {
			let len = width as usize * height as usize * 4 * bytes_per_pixel(format);
			Self {
				params: VideoParams {
					width,
					height,
					format,
					..Default::default()
				},
				data: vec![0u8; len],
			}
		}
	}

	/// 桩 GL 纹理（渲染器上的 GPU 纹理的 CPU 镜像；GL 纹理同时
	/// 扮演 CPU 帧载体——与真实 olive Texture 包装 CPU 帧同构）。
	#[derive(Clone)]
	struct StubGlTexture {
		/// 原生 id（texture_id 的返回值）。
		id: i32,
		/// CPU 镜像帧（数据 + 参数）。
		frame: StubFrame,
	}

	fn bytes_per_pixel(format: i32) -> usize {
		match format {
			PIXEL_FORMAT_F32 => 4,
			_ => 1,
		}
	}

	/// 按参数分配紧凑像素缓冲（宽×高×4 通道×每分量字节）。
	fn tight_len(params: &VideoParams) -> usize {
		params.width as usize * params.height as usize * 4 * bytes_per_pixel(params.format)
	}

	struct StubState {
		dst: StubFrame,
		src: StubFrame,
		/// 标记为占位（dummy）的纹理 ctx（texture_is_dummy 用）。
		dummy: std::collections::HashSet<usize>,
		/// GL 渲染器是否可用（renderer_is_open_gl 的返回值）。
		gl_available: bool,
		/// GL 纹理注册表（ctx = GL_TEX_BASE + id）。
		gl_textures: Vec<StubGlTexture>,
		/// 下一个 GL 纹理 id（从 1 起）。
		next_gl_id: i32,
	}

	static STATE: std::sync::LazyLock<Mutex<StubState>> = std::sync::LazyLock::new(|| {
		Mutex::new(StubState {
			dst: StubFrame::new(0, 0, 0),
			src: StubFrame::new(0, 0, 0),
			dummy: std::collections::HashSet::new(),
			gl_available: false,
			gl_textures: Vec::new(),
			next_gl_id: 1,
		})
	});

	fn lock() -> std::sync::MutexGuard<'static, StubState> {
		STATE.lock().unwrap_or_else(|e| e.into_inner())
	}

	/// 重置全部桩状态（测试隔离）。
	pub fn reset() {
		let mut s = lock();
		s.dst = StubFrame::new(0, 0, 0);
		s.src = StubFrame::new(0, 0, 0);
		s.dummy.clear();
		s.gl_available = false;
		s.gl_textures.clear();
		s.next_gl_id = 1;
	}

	/// 把某纹理 ctx 标记为占位（dummy）；`dummy=false` 取消标记。
	/// clip 桥把 dummy 输入视作空输入（NotFound）。
	pub fn set_dummy(ctx: usize, dummy: bool) {
		let mut s = lock();
		if dummy {
			s.dummy.insert(ctx);
		} else {
			s.dummy.remove(&ctx);
		}
	}

	/// 配置输出帧（宽/高/格式；F32 = 全链路主路径）。
	pub fn setup_dst(width: i32, height: i32, format: i32) {
		lock().dst = StubFrame::new(width, height, format);
	}

	/// 配置输入帧并填充像素。
	pub fn setup_src(width: i32, height: i32, format: i32, pixels: Vec<u8>) {
		let mut s = lock();
		s.src = StubFrame::new(width, height, format);
		s.src.data = pixels;
	}

	/// 输出帧像素（断言用）。
	pub fn dst_pixels() -> Vec<u8> {
		lock().dst.data.clone()
	}

	/// 输入帧像素（断言用）。
	pub fn src_pixels() -> Vec<u8> {
		lock().src.data.clone()
	}

	/// 输出帧参数。
	pub fn dst_params() -> VideoParams {
		lock().dst.params
	}

	/// GL 渲染器可用标记（renderer_is_open_gl 的桩返回值）。
	pub fn set_gl_available(available: bool) {
		lock().gl_available = available;
	}

	/// 构造桩 GL 渲染器句柄（ctx = 0xD1）。
	pub fn make_gl_renderer() -> RendererHandle {
		magic_handle(GL_RENDERER)
	}

	/// 已注册的 GL 纹理 id 列表（断言用）。
	pub fn gl_texture_ids() -> Vec<i32> {
		lock().gl_textures.iter().map(|t| t.id).collect()
	}

	/// 某 GL 纹理 id 的像素（断言用；未知 id 返回空）。
	pub fn gl_texture_data(id: i32) -> Vec<u8> {
		lock()
			.gl_textures
			.iter()
			.find(|t| t.id == id)
			.map(|t| t.frame.data.clone())
			.unwrap_or_default()
	}

	/// 某 GL 纹理 id 的参数（断言用）。
	pub fn gl_texture_params(id: i32) -> Option<VideoParams> {
		lock()
			.gl_textures
			.iter()
			.find(|t| t.id == id)
			.map(|t| t.frame.params)
	}

	/// 直接构造一个 GL 纹理（GL 测试的目标纹理/输入纹理模拟；
	/// oakrender 侧创建纹理的 C ABI 等价物）。返回句柄（ctx =
	/// GL_TEX_BASE + id）。
	pub fn make_gl_texture(width: i32, height: i32, format: i32) -> TextureHandle {
		let mut s = lock();
		let id = s.next_gl_id;
		s.next_gl_id += 1;
		s.gl_textures.push(StubGlTexture {
			id,
			frame: StubFrame::new(width, height, format),
		});
		magic_handle(GL_TEX_BASE + id as usize)
	}

	fn frame_of(state: &mut StubState, ctx: usize) -> &mut StubFrame {
		if ctx == SRC_FRAME {
			&mut state.src
		} else if let Some(id) = gl_id_of_ctx(ctx) {
			match state.gl_textures.iter_mut().find(|t| t.id == id) {
				Some(t) => &mut t.frame,
				None => &mut state.dst,
			}
		} else {
			&mut state.dst
		}
	}

	fn magic_handle(ctx: usize) -> CHandle {
		CHandle {
			ctx: ctx as *mut c_void,
			addref: None,
			release: None,
			abi_version: 1,
		}
	}

	const DST_TEX: usize = 0xA1;
	const SRC_TEX: usize = 0xA2;
	const DST_FRAME: usize = 0xB1;
	const SRC_FRAME: usize = 0xB2;
	/// 桩 GL 渲染器 ctx。
	const GL_RENDERER: usize = 0xD1;
	/// 桩 GL 纹理 ctx 基址（ctx = GL_TEX_BASE + id）。
	const GL_TEX_BASE: usize = 0xC0;

	/// GL 纹理 ctx → id。
	fn gl_id_of_ctx(ctx: usize) -> Option<i32> {
		if ctx > GL_TEX_BASE {
			Some((ctx - GL_TEX_BASE) as i32)
		} else {
			None
		}
	}

	/// 行拷贝：`src`（行跨度 src_linesize 字节）→ `dst`（行跨度
	/// dst_linesize 字节），共 `rows` 行、每行 `row_bytes` 字节。
	fn copy_rows(
		src: &[u8],
		src_linesize: usize,
		dst: &mut [u8],
		dst_linesize: usize,
		row_bytes: usize,
		rows: usize,
	) {
		for y in 0..rows {
			let s = y * src_linesize;
			let d = y * dst_linesize;
			if row_bytes == src_linesize && src_linesize == dst_linesize {
				dst[d..d + row_bytes].copy_from_slice(&src[s..s + row_bytes]);
			} else {
				dst[d..d + row_bytes].copy_from_slice(&src[s..s + row_bytes.min(src.len().saturating_sub(s))]);
			}
		}
	}

	pub(super) unsafe fn texture_get_frame(texture: TextureHandle, out: *mut FrameHandle) -> i32 {
		if out.is_null() {
			return -1;
		}
		// GL 纹理：帧句柄即纹理自身 ctx（frame_of 按 ctx 反查镜像帧）。
		let frame_ctx = if texture.ctx as usize == SRC_TEX {
			SRC_FRAME
		} else if gl_id_of_ctx(texture.ctx as usize).is_some() {
			texture.ctx as usize
		} else {
			DST_FRAME
		};
		unsafe { *out = magic_handle(frame_ctx) };
		0
	}

	pub(super) unsafe fn texture_is_dummy(texture: TextureHandle) -> i32 {
		lock().dummy.contains(&(texture.ctx as usize)) as i32
	}

	pub(super) unsafe fn frame_width(frame: FrameHandle) -> i32 {
		let mut s = lock();
		frame_of(&mut s, frame.ctx as usize).params.width
	}

	pub(super) unsafe fn frame_height(frame: FrameHandle) -> i32 {
		let mut s = lock();
		frame_of(&mut s, frame.ctx as usize).params.height
	}

	pub(super) unsafe fn frame_data(frame: FrameHandle) -> *mut c_void {
		let mut s = lock();
		let f = frame_of(&mut s, frame.ctx as usize);
		f.data.as_mut_ptr() as *mut c_void
	}

	pub(super) unsafe fn frame_get_params(frame: FrameHandle, out: *mut VideoParams) -> i32 {
		if out.is_null() {
			return -1;
		}
		let mut s = lock();
		unsafe { *out = frame_of(&mut s, frame.ctx as usize).params };
		0
	}

	pub(super) unsafe fn frame_allocate(frame: FrameHandle) -> i32 {
		let mut s = lock();
		let f = frame_of(&mut s, frame.ctx as usize);
		let len = f.params.width as usize
			* f.params.height as usize
			* 4
			* bytes_per_pixel(f.params.format);
		f.data.resize(len, 0);
		0
	}

	pub(super) unsafe fn frame_free(frame: *mut FrameHandle) {
		if !frame.is_null() {
			unsafe { (*frame).ctx = std::ptr::null_mut() };
		}
	}

	pub(super) unsafe fn frame_linesize_bytes(frame: FrameHandle) -> i32 {
		let mut s = lock();
		let f = frame_of(&mut s, frame.ctx as usize);
		(f.params.width * 4 * bytes_per_pixel(f.params.format) as i32) as i32
	}

	// ---- 渲染器族桩 ---------------------------------------------------------

	pub(super) unsafe fn renderer_create_dynamic(backend: *const std::ffi::c_char) -> RendererHandle {
		if backend.is_null() {
			return RendererHandle::null();
		}
		let id = unsafe { std::ffi::CStr::from_ptr(backend) }.to_str().unwrap_or("");
		if id != "opengl" {
			return RendererHandle::null();
		}
		magic_handle(GL_RENDERER)
	}

	pub(super) unsafe fn renderer_init(
		_renderer: RendererHandle,
		_gl_context: *mut std::ffi::c_void,
	) -> i32 {
		0
	}

	pub(super) unsafe fn renderer_is_open_gl(renderer: RendererHandle) -> i32 {
		if renderer.ctx as usize != GL_RENDERER {
			return 0;
		}
		lock().gl_available as i32
	}

	pub(super) unsafe fn renderer_destroy(renderer: *mut RendererHandle) {
		if !renderer.is_null() {
			unsafe { (*renderer).ctx = std::ptr::null_mut() };
		}
	}

	// ---- 纹理族桩（GL 纹理注册表）------------------------------------------

	pub(super) unsafe fn texture_create(
		renderer: RendererHandle,
		params: *const VideoParams,
		pixels: *const std::ffi::c_void,
		linesize: i32,
	) -> TextureHandle {
		if renderer.ctx as usize != GL_RENDERER || params.is_null() {
			return TextureHandle::null();
		}
		let params = unsafe { *params };
		if params.width <= 0 || params.height <= 0 {
			return TextureHandle::null();
		}
		let mut s = lock();
		let id = s.next_gl_id;
		s.next_gl_id += 1;
		let mut data = vec![0u8; tight_len(&params)];
		if !pixels.is_null() {
			// linesize 字节/行（renderer.h 的 bytes 契约；0 → 紧凑行）。
			let bpp = bytes_per_pixel(params.format);
			let row_bytes = (params.width as usize) * 4 * bpp;
			let src_linesize = if linesize > 0 { linesize as usize } else { row_bytes };
			let src = unsafe { std::slice::from_raw_parts(pixels as *const u8, src_linesize * params.height as usize) };
			copy_rows(src, src_linesize, &mut data, row_bytes, row_bytes.min(src_linesize), params.height as usize);
		}
		s.gl_textures.push(StubGlTexture {
			id,
			frame: StubFrame {
				params,
				data,
			},
		});
		magic_handle(GL_TEX_BASE + id as usize)
	}

	pub(super) unsafe fn texture_id(texture: TextureHandle) -> i32 {
		gl_id_of_ctx(texture.ctx as usize).unwrap_or(0)
	}

	pub(super) unsafe fn texture_get_params(texture: TextureHandle, out: *mut VideoParams) -> i32 {
		if out.is_null() {
			return -1;
		}
		let mut s = lock();
		let Some(id) = gl_id_of_ctx(texture.ctx as usize) else {
			return -1;
		};
		let Some(t) = s.gl_textures.iter().find(|t| t.id == id) else {
			return -1;
		};
		unsafe { *out = t.frame.params };
		0
	}

	pub(super) unsafe fn texture_download(
		texture: TextureHandle,
		pixels: *mut std::ffi::c_void,
		linesize: i32,
	) -> i32 {
		let mut s = lock();
		let Some(id) = gl_id_of_ctx(texture.ctx as usize) else {
			return -1;
		};
		let Some(t) = s.gl_textures.iter().find(|t| t.id == id) else {
			return -1;
		};
		if pixels.is_null() {
			return -1;
		}
		let bpp = bytes_per_pixel(t.frame.params.format);
		let row_bytes = (t.frame.params.width as usize) * 4 * bpp;
		let dst_linesize = if linesize > 0 { linesize as usize } else { row_bytes };
		let dst = unsafe { std::slice::from_raw_parts_mut(pixels as *mut u8, dst_linesize * t.frame.params.height as usize) };
		let src = t.frame.data.clone();
		copy_rows(&src, row_bytes, dst, dst_linesize, row_bytes, t.frame.params.height as usize);
		0
	}

	pub(super) unsafe fn texture_upload(
		texture: TextureHandle,
		pixels: *const std::ffi::c_void,
		linesize: i32,
	) -> i32 {
		let mut s = lock();
		let Some(id) = gl_id_of_ctx(texture.ctx as usize) else {
			return -1;
		};
		let Some(t) = s.gl_textures.iter_mut().find(|t| t.id == id) else {
			return -1;
		};
		if pixels.is_null() {
			return -1;
		}
		let bpp = bytes_per_pixel(t.frame.params.format);
		let row_bytes = (t.frame.params.width as usize) * 4 * bpp;
		let src_linesize = if linesize > 0 { linesize as usize } else { row_bytes };
		let src = unsafe { std::slice::from_raw_parts(pixels as *const u8, src_linesize * t.frame.params.height as usize) };
		let mut data = vec![0u8; tight_len(&t.frame.params)];
		copy_rows(src, src_linesize, &mut data, row_bytes, row_bytes.min(src_linesize), t.frame.params.height as usize);
		t.frame.data = data;
		0
	}

	pub(super) unsafe fn texture_retain(texture: TextureHandle) -> TextureHandle {
		texture
	}

	pub(super) unsafe fn texture_free(texture: *mut TextureHandle) {
		if texture.is_null() {
			return;
		}
		let ctx = unsafe { (*texture).ctx as usize };
		if let Some(id) = gl_id_of_ctx(ctx) {
			let mut s = lock();
			s.gl_textures.retain(|t| t.id != id);
		}
		unsafe { (*texture).ctx = std::ptr::null_mut() };
	}

	pub(super) unsafe fn renderer_download_from_texture(
		renderer: RendererHandle,
		texture_id: i32,
		params: *const VideoParams,
		dst: *mut std::ffi::c_void,
		linesize: i32,
	) -> i32 {
		if renderer.ctx as usize != GL_RENDERER || params.is_null() || dst.is_null() {
			return -1;
		}
		let req = unsafe { *params };
		let s = lock();
		let Some(t) = s.gl_textures.iter().find(|t| t.id == texture_id) else {
			return -1;
		};
		let bpp = bytes_per_pixel(req.format);
		let row_bytes = (req.width as usize) * 4 * bpp;
		let dst_linesize = if linesize > 0 { linesize as usize } else { row_bytes };
		let out = unsafe { std::slice::from_raw_parts_mut(dst as *mut u8, dst_linesize * req.height as usize) };
		let src = t.frame.data.clone();
		copy_rows(&src, row_bytes, out, dst_linesize, row_bytes, req.height as usize);
		0
	}
}

// ---- 默认模式单测（无桩；空/NULL 句柄经真实 crate 的可解释失败路径）----------------------

#[cfg(all(test, not(feature = "test-stubs")))]
mod tests {
	use super::*;

	/// 空/NULL 句柄经真实 oakrender 全部容错不崩：返回
	/// `OAKRENDER_E_INVALID`（-70001）或空值。
	#[test]
	fn empty_handle_error_paths() {
		let mut frame = FrameHandle::null();
		let tex = TextureHandle::null();
		unsafe {
			assert_eq!(texture_get_frame(tex, &mut frame), -70001);
			// 空句柄不是占位纹理：真实 oakrender 返回 0。
			assert_eq!(texture_is_dummy(tex), 0);
			assert_eq!(frame_width(frame), 0);
			assert_eq!(frame_height(frame), 0);
			assert!(frame_data(frame).is_null());
			assert_eq!(frame_get_params(frame, &mut VideoParams::default()), -70001);
			assert_eq!(frame_allocate(frame), -70001);
			frame_free(&mut frame); // 空句柄 no-op
			frame_free(std::ptr::null_mut()); // NULL no-op
		}
	}
}
