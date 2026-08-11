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

//! GPU backend: **wgpu**, used directly.
//!
//! The C++ tree splits GL/Vulkan into dlopened backend plugins behind
//! `renderbackend_c.h` because C++ had no portable GPU abstraction.
//! wgpu (Metal/Vulkan/GL/DX12 in one safe API) removes that whole
//! layer: no backend plugins, no C interface, no unsafe dlopen glue.
//!
//! This module owns the `wgpu::Instance`/`Device`/`Queue` lifecycle and
//! the texture/shader operations the rest of the crate needs. It is the
//! only module that talks to wgpu, and the only module with `unsafe`
//! (the wgpu map_async callback marshalling) besides `bridge/`.
//!
//! Headless status: `GpuContext::create` requests an adapter without any
//! surface; when no adapter is available (headless CI, VMs) it returns
//! `None` and every consumer falls back to the CPU path. GPU tests skip
//! with no adapter. Verified on macOS Metal (wgpu 25.0.2).

use std::collections::HashMap;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Mutex, MutexGuard};

use oakcore_rs::PixelFormat;

use crate::error::{Error, Result};
use crate::frame::VideoParamsPod;
use crate::texture::{Frame, Texture};

/// Backend selection preference (mapped onto wgpu backends).
///
/// The choice is user-visible: the settings panel exposes a renderer
/// dropdown (Auto/Metal/Vulkan/OpenGL/CPU) persisted through the
/// oakcommon config C ABI under the "GraphicsBackend" key
/// (C++ parity: `RenderManager::backend_from_string` config round-trip).
/// "auto" resolves Metal → Vulkan → GL → CPU at runtime.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum BackendKind {
	/// Resolve automatically (Metal → Vulkan → GL → CPU).
	Auto,
	/// Metal (macOS primary).
	Metal,
	/// Vulkan.
	Vulkan,
	/// OpenGL (legacy fallback).
	Gl,
	/// CPU fallback (no adapter found; dummy textures + CPU blits).
	Cpu,
}

impl BackendKind {
	/// Parse the settings string (C++ `backend_from_string` semantics for
	/// the legacy values; unknown values yield Auto).
	pub fn from_config_string(s: &str) -> BackendKind {
		match s.trim().to_ascii_lowercase().as_str() {
			"" | "auto" => BackendKind::Auto,
			"metal" => BackendKind::Metal,
			"vulkan" => BackendKind::Vulkan,
			"opengl" | "gl" => BackendKind::Gl,
			// C++ "dummy" backend and the multiprocess pool: no GPU path in
			// this pass — the CPU fallback is the honest equivalent.
			"cpu" | "dummy" | "multiprocess" => BackendKind::Cpu,
			_ => BackendKind::Auto,
		}
	}

	/// Serialize for the settings panel.
	pub fn to_config_string(self) -> &'static str {
		match self {
			BackendKind::Auto => "auto",
			BackendKind::Metal => "metal",
			BackendKind::Vulkan => "vulkan",
			BackendKind::Gl => "opengl",
			BackendKind::Cpu => "cpu",
		}
	}

	/// Read the user's persisted choice through the oakcommon config C ABI
	/// ("GraphicsBackend"). `OAK_RENDER_BACKEND` overrides the config
	/// (tests / headless environments).
	pub fn from_user_config() -> BackendKind {
		if let Ok(v) = std::env::var("OAK_RENDER_BACKEND") {
			if !v.is_empty() {
				return BackendKind::from_config_string(&v);
			}
		}
		let configured =
			crate::bridge::common::config_get_string(None, "GraphicsBackend").unwrap_or_default();
		BackendKind::from_config_string(&configured)
	}

	/// The wgpu backends this kind resolves to, in fallback order.
	fn wgpu_fallbacks(self) -> Vec<wgpu::Backends> {
		match self {
			BackendKind::Auto | BackendKind::Metal => {
				vec![
					wgpu::Backends::METAL,
					wgpu::Backends::VULKAN,
					wgpu::Backends::GL,
				]
			}
			BackendKind::Vulkan => {
				vec![
					wgpu::Backends::VULKAN,
					wgpu::Backends::METAL,
					wgpu::Backends::GL,
				]
			}
			BackendKind::Gl => {
				vec![
					wgpu::Backends::GL,
					wgpu::Backends::METAL,
					wgpu::Backends::VULKAN,
				]
			}
			BackendKind::Cpu => Vec::new(),
		}
	}

	/// The resolved kind for a wgpu backend id.
	fn from_wgpu_backend(b: wgpu::Backend) -> BackendKind {
		match b {
			wgpu::Backend::Metal => BackendKind::Metal,
			wgpu::Backend::Vulkan => BackendKind::Vulkan,
			wgpu::Backend::Gl => BackendKind::Gl,
			_ => BackendKind::Cpu,
		}
	}
}

/// A GPU-resident texture in the context registry.
#[derive(Clone)]
struct GpuTexture {
	texture: wgpu::Texture,
	width: u32,
	height: u32,
}

fn lock<T>(m: &Mutex<T>) -> MutexGuard<'_, T> {
	m.lock().unwrap_or_else(|e| e.into_inner())
}

/// The context surface textures depend on (trait object in
/// [`Texture::Gpu`]). `GpuContext` is the production implementor; tests
/// can supply a fake.
pub trait GpuContextLike: Send + Sync {
	/// The backend kind in use.
	fn kind(&self) -> BackendKind;
	/// Destroy a texture token (idempotent).
	fn destroy_texture(&self, token: u64);
	/// Upload a CPU frame into a texture (F32 RGBA).
	fn upload(&self, token: u64, frame: &Frame) -> Result<()>;
	/// Download a texture into a CPU frame.
	fn download(&self, token: u64) -> Result<Frame>;
	/// Blit texture → texture (plain copy; color-managed deferred).
	fn blit(
		&self,
		src: u64,
		dst: u64,
		processor: Option<&crate::color::ColorProcessor>,
	) -> Result<()>;
}

/// The GPU context: one wgpu instance/device/queue for the process,
/// plus the texture registry and the blit pipeline.
pub struct GpuContext {
	// Kept alive for the whole context: the instance must outlive the
	// adapter on native backends.
	_instance: wgpu::Instance,
	_adapter: wgpu::Adapter,
	device: wgpu::Device,
	queue: wgpu::Queue,
	kind: BackendKind,
	textures: Mutex<HashMap<u64, GpuTexture>>,
	next_token: AtomicU64,
	blit: Mutex<Option<wgpu::RenderPipeline>>,
}

// SAFETY check: wgpu Device/Queue/Instance are Send+Sync; the rest is
// behind Mutexes. The context is shared across worker threads.
unsafe impl Send for GpuContext {}
unsafe impl Sync for GpuContext {}

impl GpuContext {
	/// Create for the preferred backend; falls back across the backend
	/// order. `None` when no adapter is available (callers use the CPU
	/// path).
	pub fn create(prefer: BackendKind) -> Option<Arc<Self>> {
		for backends in prefer.wgpu_fallbacks() {
			let instance = wgpu::Instance::new(&wgpu::InstanceDescriptor {
				backends,
				..Default::default()
			});
			let adapter =
				match pollster_block_on(instance.request_adapter(&wgpu::RequestAdapterOptions {
					power_preference: wgpu::PowerPreference::HighPerformance,
					compatible_surface: None,
					force_fallback_adapter: false,
				})) {
					Ok(a) => a,
					Err(_) => continue, // try the next backend in the fallback order
				};
			let info = adapter.get_info();
			let (device, queue) =
				match pollster_block_on(adapter.request_device(&wgpu::DeviceDescriptor {
					label: Some("oakrender"),
					required_features: wgpu::Features::empty(),
					required_limits: wgpu::Limits::default(),
					memory_hints: wgpu::MemoryHints::default(),
					trace: wgpu::Trace::Off,
				})) {
					Ok(dq) => dq,
					Err(_) => continue,
				};
			let kind = BackendKind::from_wgpu_backend(info.backend);
			if kind == BackendKind::Cpu {
				continue;
			}
			return Some(Arc::new(Self {
				_instance: instance,
				_adapter: adapter,
				device,
				queue,
				kind,
				textures: Mutex::new(HashMap::new()),
				next_token: AtomicU64::new(1),
				blit: Mutex::new(None),
			}));
		}
		None
	}

	/// The backend actually in use.
	pub fn kind(&self) -> BackendKind {
		self.kind
	}

	/// True when this context hosts a Metal/Vulkan/GL adapter (never true
	/// for the CPU fallback).
	pub fn is_gpu(&self) -> bool {
		self.kind != BackendKind::Cpu
	}

	/// Create an F32 RGBA texture (the pipeline's canonical format).
	pub fn create_texture(&self, width: i32, height: i32) -> Result<u64> {
		if width <= 0 || height <= 0 {
			return Err(Error::Invalid);
		}
		let size = wgpu::Extent3d {
			width: width as u32,
			height: height as u32,
			depth_or_array_layers: 1,
		};
		let texture = self.device.create_texture(&wgpu::TextureDescriptor {
			label: Some("oakrender-texture"),
			size,
			mip_level_count: 1,
			sample_count: 1,
			dimension: wgpu::TextureDimension::D2,
			format: wgpu::TextureFormat::Rgba32Float,
			usage: wgpu::TextureUsages::TEXTURE_BINDING
				| wgpu::TextureUsages::RENDER_ATTACHMENT
				| wgpu::TextureUsages::COPY_DST
				| wgpu::TextureUsages::COPY_SRC,
			view_formats: &[],
		});
		let token = self.next_token.fetch_add(1, Ordering::Relaxed);
		lock(&self.textures).insert(
			token,
			GpuTexture {
				texture,
				width: width as u32,
				height: height as u32,
			},
		);
		Ok(token)
	}

	/// Destroy a texture token (idempotent).
	pub fn destroy_texture(&self, token: u64) {
		lock(&self.textures).remove(&token);
	}

	/// Look up a texture's (width, height).
	pub fn texture_size(&self, token: u64) -> Option<(u32, u32)> {
		lock(&self.textures)
			.get(&token)
			.map(|t| (t.width, t.height))
	}

	/// True when the registry holds the token.
	pub fn has_texture(&self, token: u64) -> bool {
		lock(&self.textures).contains_key(&token)
	}

	/// Upload a CPU frame into a texture (F32 RGBA).
	pub fn upload(&self, token: u64, frame: &Frame) -> Result<()> {
		if frame.format != PixelFormat::F32 {
			return Err(Error::Invalid);
		}
		let entry = lock(&self.textures)
			.get(&token)
			.cloned()
			.ok_or(Error::NotFound)?;
		let w = entry.width as usize;
		let h = entry.height as usize;
		if frame.width != w as i32 || frame.height != h as i32 {
			return Err(Error::Invalid);
		}
		let linesize = frame.linesize_bytes();
		if frame.data.len() < linesize * h {
			return Err(Error::Invalid);
		}
		self.queue.write_texture(
			wgpu::TexelCopyTextureInfo {
				texture: &entry.texture,
				mip_level: 0,
				origin: wgpu::Origin3d::ZERO,
				aspect: wgpu::TextureAspect::All,
			},
			&frame.data[..linesize * h],
			wgpu::TexelCopyBufferLayout {
				offset: 0,
				bytes_per_row: Some(linesize as u32),
				rows_per_image: None,
			},
			wgpu::Extent3d {
				width: w as u32,
				height: h as u32,
				depth_or_array_layers: 1,
			},
		);
		Ok(())
	}

	/// Download a texture into a CPU frame.
	pub fn download(&self, token: u64) -> Result<Frame> {
		let entry = lock(&self.textures)
			.get(&token)
			.cloned()
			.ok_or(Error::NotFound)?;
		let w = entry.width as usize;
		let h = entry.height as usize;
		let linesize = w * 4 * 4; // Rgba32Float
							// copy_texture_to_buffer requires a 256-byte-aligned row stride.
		let padded = (linesize + 255) & !255;

		let buffer = self.device.create_buffer(&wgpu::BufferDescriptor {
			label: Some("oakrender-download"),
			size: (padded * h) as u64,
			usage: wgpu::BufferUsages::COPY_DST | wgpu::BufferUsages::MAP_READ,
			mapped_at_creation: false,
		});

		let mut encoder = self
			.device
			.create_command_encoder(&wgpu::CommandEncoderDescriptor {
				label: Some("oakrender-download"),
			});
		encoder.copy_texture_to_buffer(
			wgpu::TexelCopyTextureInfo {
				texture: &entry.texture,
				mip_level: 0,
				origin: wgpu::Origin3d::ZERO,
				aspect: wgpu::TextureAspect::All,
			},
			wgpu::TexelCopyBufferInfo {
				buffer: &buffer,
				layout: wgpu::TexelCopyBufferLayout {
					offset: 0,
					bytes_per_row: Some(padded as u32),
					rows_per_image: None,
				},
			},
			wgpu::Extent3d {
				width: w as u32,
				height: h as u32,
				depth_or_array_layers: 1,
			},
		);
		self.queue.submit(Some(encoder.finish()));

		// Map + block until the callback fires (headless-safe: no surface,
		// no event loop; PollType::Wait drives the callbacks).
		let (tx, rx) = std::sync::mpsc::channel();
		buffer
			.slice(..)
			.map_async(wgpu::MapMode::Read, move |result| {
				let _ = tx.send(result.is_ok());
			});
		let _ = self.device.poll(wgpu::PollType::wait());
		if !rx.recv().unwrap_or(false) {
			return Err(Error::Failed("texture download map failed".into()));
		}

		let mapped = buffer.slice(..).get_mapped_range();
		let mut data = vec![0u8; linesize * h];
		for row in 0..h {
			let src = &mapped[row * padded..row * padded + linesize];
			data[row * linesize..(row + 1) * linesize].copy_from_slice(src);
		}
		drop(mapped);
		buffer.unmap();

		let mut frame = Frame::new();
		let mut pod = VideoParamsPod::default();
		pod.width = w as i32;
		pod.height = h as i32;
		pod.format = PixelFormat::F32 as i32;
		frame.set_video_params(pod);
		frame.data = data;
		Ok(frame)
	}

	/// Blit texture → texture. `processor` selects the color-managed path,
	/// which is **deferred**: the OCIO→WGSL shader generation is not part
	/// of this pass (see README §4), so a `Some` processor returns
	/// `Error::Failed` and the plain-copy WGSL pipeline is used for `None`.
	pub fn blit(
		&self,
		src: u64,
		dst: u64,
		processor: Option<&crate::color::ColorProcessor>,
	) -> Result<()> {
		if processor.is_some() {
			return Err(Error::Failed(
				"color-managed GPU blit deferred: OCIO→WGSL generation not in this pass".into(),
			));
		}
		let (src_tex, dst_tex) = {
			let reg = lock(&self.textures);
			let s = reg.get(&src).ok_or(Error::NotFound)?;
			let d = reg.get(&dst).ok_or(Error::NotFound)?;
			(s.texture.clone(), d.texture.clone())
		};

		let pipeline = {
			let mut cache = lock(&self.blit);
			if cache.is_none() {
				*cache = Some(self.create_blit_pipeline()?);
			}
			cache.clone().unwrap()
		};

		let src_view = src_tex.create_view(&wgpu::TextureViewDescriptor::default());
		let dst_view = dst_tex.create_view(&wgpu::TextureViewDescriptor::default());
		let bind_group = self.device.create_bind_group(&wgpu::BindGroupDescriptor {
			label: Some("oakrender-blit-bg"),
			layout: &self.blit_bind_group_layout(),
			entries: &[wgpu::BindGroupEntry {
				binding: 0,
				resource: wgpu::BindingResource::TextureView(&src_view),
			}],
		});

		let mut encoder = self
			.device
			.create_command_encoder(&wgpu::CommandEncoderDescriptor {
				label: Some("oakrender-blit"),
			});
		{
			let mut pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
				label: Some("oakrender-blit-pass"),
				color_attachments: &[Some(wgpu::RenderPassColorAttachment {
					view: &dst_view,
					resolve_target: None,
					ops: wgpu::Operations {
						load: wgpu::LoadOp::Clear(wgpu::Color::TRANSPARENT),
						store: wgpu::StoreOp::Store,
					},
				})],
				depth_stencil_attachment: None,
				timestamp_writes: None,
				occlusion_query_set: None,
			});
			pass.set_pipeline(&pipeline);
			pass.set_bind_group(0, &bind_group, &[]);
			pass.draw(0..3, 0..1);
		}
		self.queue.submit(Some(encoder.finish()));
		Ok(())
	}

	fn blit_bind_group_layout(&self) -> wgpu::BindGroupLayout {
		self.device
			.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
				label: Some("oakrender-blit-layout"),
				entries: &[wgpu::BindGroupLayoutEntry {
					binding: 0,
					visibility: wgpu::ShaderStages::FRAGMENT,
					ty: wgpu::BindingType::Texture {
						sample_type: wgpu::TextureSampleType::Float { filterable: false },
						view_dimension: wgpu::TextureViewDimension::D2,
						multisampled: false,
					},
					count: None,
				}],
			})
	}

	fn create_blit_pipeline(&self) -> Result<wgpu::RenderPipeline> {
		let shader = self
			.device
			.create_shader_module(wgpu::ShaderModuleDescriptor {
				label: Some("oakrender-blit-shader"),
				source: wgpu::ShaderSource::Wgsl(std::borrow::Cow::Borrowed(BLIT_WGSL)),
			});
		let layout = self.blit_bind_group_layout();
		let pipeline_layout = self
			.device
			.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
				label: Some("oakrender-blit-layout"),
				bind_group_layouts: &[&layout],
				push_constant_ranges: &[],
			});
		let pipeline = self
			.device
			.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
				label: Some("oakrender-blit"),
				layout: Some(&pipeline_layout),
				vertex: wgpu::VertexState {
					module: &shader,
					entry_point: Some("vs_main"),
					compilation_options: Default::default(),
					buffers: &[],
				},
				primitive: wgpu::PrimitiveState::default(),
				depth_stencil: None,
				multisample: wgpu::MultisampleState::default(),
				fragment: Some(wgpu::FragmentState {
					module: &shader,
					entry_point: Some("fs_main"),
					compilation_options: Default::default(),
					targets: &[Some(wgpu::ColorTargetState {
						format: wgpu::TextureFormat::Rgba32Float,
						blend: None,
						write_mask: wgpu::ColorWrites::ALL,
					})],
				}),
				multiview: None,
				cache: None,
			});
		Ok(pipeline)
	}
}

impl GpuContextLike for GpuContext {
	fn kind(&self) -> BackendKind {
		self.kind()
	}

	fn destroy_texture(&self, token: u64) {
		self.destroy_texture(token);
	}

	fn upload(&self, token: u64, frame: &Frame) -> Result<()> {
		self.upload(token, frame)
	}

	fn download(&self, token: u64) -> Result<Frame> {
		self.download(token)
	}

	fn blit(
		&self,
		src: u64,
		dst: u64,
		processor: Option<&crate::color::ColorProcessor>,
	) -> Result<()> {
		self.blit(src, dst, processor)
	}
}

/// Plain-copy blit shader: fullscreen triangle, `textureLoad` (no
/// filtering — Rgba32Float is not filterable), 1:1 pixel mapping with
/// edge clamping.
const BLIT_WGSL: &str = r#"
@group(0) @binding(0) var src_tex: texture_2d<f32>;

@vertex
fn vs_main(@builtin(vertex_index) vi: u32) -> @builtin(position) vec4<f32> {
    var pos = array<vec2<f32>, 3>(
        vec2<f32>(-1.0, -1.0),
        vec2<f32>(3.0, -1.0),
        vec2<f32>(-1.0, 3.0),
    );
    return vec4<f32>(pos[vi], 0.0, 1.0);
}

@fragment
fn fs_main(@builtin(position) frag: vec4<f32>) -> @location(0) vec4<f32> {
    let dims = textureDimensions(src_tex);
    let coord = clamp(
        vec2<u32>(u32(i32(frag.x)), u32(i32(frag.y))),
        vec2<u32>(0u, 0u),
        dims - vec2<u32>(1u, 1u),
    );
    return textureLoad(src_tex, coord, 0);
}
"#;

/// Run a wgpu future to completion on the current thread. wgpu's adapter/
/// device futures complete immediately for the native backends (no async
/// runtime needed); this is the same block_on the examples use.
fn pollster_block_on<F: std::future::Future>(future: F) -> F::Output {
	// Local minimal block_on: native wgpu futures are already complete
	// after creation; polling to readiness is enough.
	futures_executor::block_on(future)
}

// Minimal futures executor (wgpu brings futures-core transitively; a tiny
// block_on is enough for the immediately-ready adapter/device futures).
mod futures_executor {
	use std::future::Future;
	use std::pin::pin;
	use std::task::{Context, Poll, RawWaker, RawWakerVTable, Waker};

	fn noop_raw_waker() -> RawWaker {
		fn no_op(_: *const ()) {}
		fn clone(_: *const ()) -> RawWaker {
			noop_raw_waker()
		}
		static VTABLE: RawWakerVTable = RawWakerVTable::new(clone, no_op, no_op, no_op);
		RawWaker::new(std::ptr::null(), &VTABLE)
	}

	fn noop_waker() -> Waker {
		// SAFETY: the no-op vtable is valid for any data pointer.
		unsafe { Waker::from_raw(noop_raw_waker()) }
	}

	/// Drive `future` until completion (panics on a genuinely pending
	/// future — native wgpu futures never are).
	pub fn block_on<F: Future>(future: F) -> F::Output {
		let mut future = pin!(future);
		let waker = noop_waker();
		let mut cx = Context::from_waker(&waker);
		loop {
			match future.as_mut().poll(&mut cx) {
				Poll::Ready(out) => return out,
				Poll::Pending => std::thread::yield_now(),
			}
		}
	}
}

/// The display renderer (C++ `olive::Renderer`): a wgpu context (when an
/// adapter exists) or the CPU path. Owns nothing else — textures hold
/// their own `Arc<GpuContext>`.
pub struct DisplayRenderer {
	ctx: Option<Arc<GpuContext>>,
	backend: BackendKind,
}

impl DisplayRenderer {
	/// A renderer for the given backend kind (not yet initialized).
	pub fn new(backend: BackendKind) -> Self {
		Self { ctx: None, backend }
	}

	/// Initialize: create the GPU context for the configured backend.
	/// `gl_context` must be null — a foreign OpenGL context cannot be
	/// adopted by wgpu (documented limitation).
	pub fn init(&mut self, gl_context: *mut std::ffi::c_void) -> Result<()> {
		if !gl_context.is_null() {
			return Err(Error::Invalid);
		}
		self.ctx = GpuContext::create(self.backend);
		if self.ctx.is_none() {
			return Err(Error::Failed("no GPU adapter available".into()));
		}
		Ok(())
	}

	/// The backend kind.
	pub fn backend(&self) -> BackendKind {
		self.backend
	}

	/// The live GPU context (None on the CPU path / before init).
	pub fn context(&self) -> Option<&Arc<GpuContext>> {
		self.ctx.as_ref()
	}

	/// True for an OpenGL-backed renderer.
	pub fn is_open_gl(&self) -> bool {
		self.backend == BackendKind::Gl
	}

	/// True for a Vulkan-backed renderer.
	pub fn is_vulkan(&self) -> bool {
		self.backend == BackendKind::Vulkan
	}

	/// True after successful init.
	pub fn is_initialized(&self) -> bool {
		self.ctx.is_some()
	}

	/// Create a texture (GPU when initialized, else a CPU frame). `pixels`
	/// (with `linesize` bytes per row) initializes the data.
	pub fn create_texture(
		&self,
		params: &VideoParamsPod,
		pixels: Option<(*const u8, usize)>,
	) -> Result<Texture> {
		let width = params.effective_width();
		let height = params.effective_height();
		if width <= 0 || height <= 0 {
			return Err(Error::Invalid);
		}
		if let Some(ctx) = &self.ctx {
			let token = ctx.create_texture(width, height)?;
			if let Some((ptr, linesize)) = pixels {
				let mut frame = Frame::new();
				let mut pod = *params;
				pod.width = width;
				pod.height = height;
				pod.format = PixelFormat::F32 as i32;
				frame.set_video_params(pod);
				let frame_linesize = frame.linesize_bytes();
				if linesize != frame_linesize {
					ctx.destroy_texture(token);
					return Err(Error::Invalid);
				}
				frame.data = unsafe {
					std::slice::from_raw_parts(ptr, frame_linesize * height as usize).to_vec()
				};
				ctx.upload(token, &frame)?;
			}
			Ok(Texture::Gpu {
				token,
				backend: ctx.kind(),
				width,
				height,
				format: PixelFormat::F32,
				ctx: ctx.clone(),
			})
		} else {
			let mut frame = Frame::new();
			let mut pod = *params;
			pod.width = width;
			pod.height = height;
			pod.format = PixelFormat::F32 as i32;
			frame.set_video_params(pod);
			frame.allocate();
			if let Some((ptr, linesize)) = pixels {
				let frame_linesize = frame.linesize_bytes();
				if linesize != frame_linesize {
					return Err(Error::Invalid);
				}
				frame.data = unsafe {
					std::slice::from_raw_parts(ptr, frame_linesize * height as usize).to_vec()
				};
			}
			Ok(Texture::wrap_frame(frame))
		}
	}

	/// Upload pixels into a texture (GPU: backend upload; CPU: buffer copy).
	pub fn upload_texture(
		&self,
		texture: &mut Texture,
		pixels: *const u8,
		linesize: usize,
	) -> Result<()> {
		let size = texture.size();
		match texture {
			Texture::Gpu { token, ctx, .. } => {
				let frame = frame_from_pixels_for_upload(size, pixels, linesize)?;
				ctx.upload(*token, &frame)
			}
			Texture::Cpu(frame) => {
				let stride = frame.linesize_bytes();
				if linesize != stride {
					return Err(Error::Invalid);
				}
				let h = frame.height as usize;
				frame
					.data
					.copy_from_slice(unsafe { std::slice::from_raw_parts(pixels, stride * h) });
				Ok(())
			}
		}
	}

	/// Download a texture's pixels into `dst` (with `linesize` stride).
	pub fn download_texture(&self, texture: &Texture, dst: *mut u8, linesize: usize) -> Result<()> {
		let frame = texture.to_frame()?;
		let stride = frame.linesize_bytes();
		if linesize != stride {
			return Err(Error::Invalid);
		}
		let h = frame.height as usize;
		unsafe {
			std::ptr::copy_nonoverlapping(frame.data.as_ptr(), dst, stride * h);
		}
		Ok(())
	}

	/// Color-managed blit. CPU path: copy + in-place OCIO conversion.
	/// GPU path: plain-copy WGSL blit; a color processor on the GPU path is
	/// deferred (`Error::Failed`, see [`GpuContext::blit`]).
	pub fn blit_color_managed(
		&self,
		src: Option<&Texture>,
		dst: &mut Texture,
		processor: Option<&crate::color::ColorProcessor>,
	) -> Result<()> {
		match (src, dst) {
			(
				Some(Texture::Gpu { token: s, ctx, .. }),
				Texture::Gpu {
					token: d,
					ctx: dctx,
					..
				},
			) if Arc::ptr_eq(ctx, dctx) => ctx.blit(*s, *d, processor),
			(Some(Texture::Cpu(sf)), Texture::Cpu(df)) => {
				if sf.width != df.width || sf.height != df.height {
					return Err(Error::Invalid);
				}
				df.data.clear();
				df.data.extend_from_slice(&sf.data);
				if let Some(p) = processor {
					p.convert_frame(df)?;
				}
				Ok(())
			}
			_ => Err(Error::Failed(
				"mixed CPU/GPU texture blit unsupported".into(),
			)),
		}
	}

	/// Cross-backend texture download by id (GPU registry only; CPU
	/// textures have no id registry — documented).
	pub fn download_from_texture(
		&self,
		texture_id: i32,
		params: &VideoParamsPod,
		dst: *mut u8,
		linesize: usize,
	) -> Result<()> {
		let ctx = self
			.ctx
			.as_ref()
			.ok_or(Error::Failed("no GPU context".into()))?;
		let frame = ctx.download(texture_id as u64)?;
		let want_linesize = params.effective_width() as usize * 4 * 4;
		if linesize != want_linesize {
			return Err(Error::Invalid);
		}
		let h = frame.height as usize;
		unsafe {
			std::ptr::copy_nonoverlapping(frame.data.as_ptr(), dst, want_linesize * h);
		}
		Ok(())
	}
}

/// The GPU texture id of a texture (0 for CPU textures; the ffi
/// `oakrender_display_texture_id` export).
pub fn texture_id_of(t: &Texture) -> i32 {
	match t {
		Texture::Gpu { token, .. } => *token as i32,
		Texture::Cpu(_) => 0,
	}
}

/// Build an F32 frame from raw pixels (for GPU upload).
pub fn frame_from_pixels_for_upload(
	size: (i32, i32),
	pixels: *const u8,
	linesize: usize,
) -> Result<Frame> {
	let (w, h) = size;
	if w <= 0 || h <= 0 || pixels.is_null() {
		return Err(Error::Invalid);
	}
	let mut frame = Frame::new();
	let mut pod = VideoParamsPod::default();
	pod.width = w;
	pod.height = h;
	pod.format = PixelFormat::F32 as i32;
	frame.set_video_params(pod);
	let stride = frame.linesize_bytes();
	if linesize != stride {
		return Err(Error::Invalid);
	}
	frame.data = unsafe { std::slice::from_raw_parts(pixels, stride * h as usize).to_vec() };
	Ok(frame)
}

#[cfg(test)]
mod tests {
	use super::*;

	#[test]
	fn backend_string_roundtrip() {
		for (s, kind) in [
			("auto", BackendKind::Auto),
			("metal", BackendKind::Metal),
			("vulkan", BackendKind::Vulkan),
			("opengl", BackendKind::Gl),
			("gl", BackendKind::Gl),
			("cpu", BackendKind::Cpu),
		] {
			assert_eq!(BackendKind::from_config_string(s), kind);
			assert_eq!(BackendKind::from_config_string(&s.to_uppercase()), kind);
		}
		// C++ legacy values map onto the CPU fallback (documented).
		assert_eq!(BackendKind::from_config_string("dummy"), BackendKind::Cpu);
		assert_eq!(
			BackendKind::from_config_string("multiprocess"),
			BackendKind::Cpu
		);
		// Unknown → Auto.
		assert_eq!(BackendKind::from_config_string("bogus"), BackendKind::Auto);
		assert_eq!(BackendKind::from_config_string(""), BackendKind::Auto);
		// to_config_string round-trips.
		assert_eq!(
			BackendKind::from_config_string(BackendKind::Metal.to_config_string()),
			BackendKind::Metal
		);
	}

	#[test]
	fn user_config_env_override() {
		std::env::set_var("OAK_RENDER_BACKEND", "vulkan");
		assert_eq!(BackendKind::from_user_config(), BackendKind::Vulkan);
		std::env::remove_var("OAK_RENDER_BACKEND");
	}

	#[test]
	fn cpu_backend_has_no_adapter() {
		assert!(GpuContext::create(BackendKind::Cpu).is_none());
	}

	fn any_gpu() -> Option<Arc<GpuContext>> {
		GpuContext::create(BackendKind::Auto)
	}

	#[test]
	fn gpu_texture_upload_download_roundtrip() {
		let Some(ctx) = any_gpu() else {
			eprintln!("no adapter; skipping GPU round-trip");
			return;
		};
		let w = 8;
		let h = 4;
		let token = ctx.create_texture(w, h).unwrap();
		let mut frame = Frame::new();
		let mut pod = VideoParamsPod::default();
		pod.width = w;
		pod.height = h;
		frame.set_video_params(pod);
		frame.allocate();
		// Distinct bit pattern per pixel (F32 RGBA).
		for i in 0..frame.data.len() / 4 {
			let f = (i as f32) * 0.25 + 0.125;
			let bytes = f.to_le_bytes();
			frame.data[i * 4] = bytes[0];
			frame.data[i * 4 + 1] = bytes[1];
			frame.data[i * 4 + 2] = bytes[2];
			frame.data[i * 4 + 3] = bytes[3];
		}
		ctx.upload(token, &frame).unwrap();
		let out = ctx.download(token).unwrap();
		assert_eq!(out.data.len(), frame.data.len());
		assert_eq!(out.data, frame.data, "F32 bit-exact round-trip");
		ctx.destroy_texture(token);
		assert!(!ctx.has_texture(token));
	}

	#[test]
	fn gpu_blit_copies_pixels() {
		let Some(ctx) = any_gpu() else {
			eprintln!("no adapter; skipping blit");
			return;
		};
		let w = 8;
		let h = 4;
		let src = ctx.create_texture(w, h).unwrap();
		let dst = ctx.create_texture(w, h).unwrap();
		let mut frame = Frame::new();
		let mut pod = VideoParamsPod::default();
		pod.width = w;
		pod.height = h;
		frame.set_video_params(pod);
		frame.allocate();
		for i in 0..frame.data.len() / 4 {
			frame.data[i * 4] = (i % 251) as u8;
			frame.data[i * 4 + 1] = (i * 3 % 251) as u8;
			frame.data[i * 4 + 2] = (i * 7 % 251) as u8;
			frame.data[i * 4 + 3] = 255;
		}
		ctx.upload(src, &frame).unwrap();
		ctx.blit(src, dst, None).unwrap();
		let out = ctx.download(dst).unwrap();
		assert_eq!(out.data, frame.data, "plain-copy blit is pixel-exact");
		// Color-managed blit is documented-deferred.
		assert!(ctx
			.blit(
				src,
				dst,
				Some(&crate::color::ColorProcessor::pass_through())
			)
			.is_err());
		ctx.destroy_texture(src);
		ctx.destroy_texture(dst);
	}

	#[test]
	fn gpu_missing_texture_errors() {
		let Some(ctx) = any_gpu() else {
			return;
		};
		assert_eq!(
			ctx.download(999999).unwrap_err().code(),
			Error::NotFound.code()
		);
		assert!(!ctx.has_texture(999999));
		ctx.destroy_texture(999999); // idempotent
	}

	#[test]
	fn display_renderer_cpu_path() {
		let mut r = DisplayRenderer::new(BackendKind::Cpu);
		// CPU renderer has no GPU context.
		assert!(r.init(std::ptr::null_mut()).is_err());
		let _ = &mut r;
		let mut pod = VideoParamsPod::default();
		pod.width = 4;
		pod.height = 4;
		let tex = r.create_texture(&pod, None).unwrap();
		assert!(matches!(tex, Texture::Cpu(_)));
		let frame = tex.to_frame().unwrap();
		assert_eq!(frame.width, 4);
		assert_eq!(frame.format, PixelFormat::F32);
	}

	#[test]
	fn display_renderer_rejects_foreign_gl_context() {
		let mut r = DisplayRenderer::new(BackendKind::Gl);
		let fake = 0x1 as *mut std::ffi::c_void;
		assert_eq!(r.init(fake).unwrap_err().code(), Error::Invalid.code());
	}

	#[test]
	fn display_renderer_queries_and_texture_id() {
		let r = DisplayRenderer::new(BackendKind::Gl);
		assert!(r.is_open_gl());
		assert!(!r.is_vulkan());
		assert!(!r.is_initialized());
		let r2 = DisplayRenderer::new(BackendKind::Vulkan);
		assert!(r2.is_vulkan());
		assert!(!r2.is_open_gl());

		let mut r3 = DisplayRenderer::new(BackendKind::Cpu);
		let mut pod = VideoParamsPod::default();
		pod.width = 4;
		pod.height = 4;
		let tex = r3.create_texture(&pod, None).unwrap();
		// CPU textures have no GPU id.
		assert_eq!(crate::backend::texture_id_of(&tex), 0);
		// Invalid params rejected.
		let mut bad = VideoParamsPod::default();
		bad.width = 0;
		assert!(r3.create_texture(&bad, None).is_err());
		// upload/download round-trip through the display renderer.
		let mut frame = Frame::new();
		frame.set_video_params(pod);
		frame.allocate();
		frame.data[0] = 0x77;
		let mut upload_target = r3.create_texture(&pod, None).unwrap();
		r3.upload_texture(
			&mut upload_target,
			frame.data.as_ptr(),
			frame.linesize_bytes(),
		)
		.unwrap();
		let mut buf = vec![0u8; frame.linesize_bytes() * 4];
		r3.download_texture(&upload_target, buf.as_mut_ptr(), frame.linesize_bytes())
			.unwrap();
		assert_eq!(buf[0], 0x77);
		// Stride mismatch rejected.
		assert!(r3
			.upload_texture(&mut upload_target, frame.data.as_ptr(), 1)
			.is_err());
	}

	#[test]
	fn cpu_blit_applies_color_and_copy() {
		let mut r = DisplayRenderer::new(BackendKind::Cpu);
		let mut pod = VideoParamsPod::default();
		pod.width = 2;
		pod.height = 2;
		let mut src = r.create_texture(&pod, None).unwrap();
		let mut dst = r.create_texture(&pod, None).unwrap();
		let Texture::Cpu(sf) = &mut src else {
			unreachable!()
		};
		sf.data[0] = 0x11;
		sf.data[4] = 0x22;
		// Plain copy.
		r.blit_color_managed(Some(&src), &mut dst, None).unwrap();
		let Texture::Cpu(df) = &dst else {
			unreachable!()
		};
		assert_eq!(df.data[0], 0x11);
		assert_eq!(df.data[4], 0x22);
		// Pass-through processor is a no-op.
		r.blit_color_managed(
			Some(&src),
			&mut dst,
			Some(&crate::color::ColorProcessor::pass_through()),
		)
		.unwrap();
		// Size mismatch rejected.
		let mut pod2 = VideoParamsPod::default();
		pod2.width = 3;
		pod2.height = 2;
		let mut other = r.create_texture(&pod2, None).unwrap();
		assert!(r.blit_color_managed(Some(&src), &mut other, None).is_err());
	}
}
