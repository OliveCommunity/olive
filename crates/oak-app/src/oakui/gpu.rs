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

//! The 10-bit display path's GPU uploads.
//!
//! The real and mock engines both produce their viewer picture as F32 RGBA
//! samples (the pipeline's internal format). Instead of downconverting to
//! BGRA8 and going through gpui's 8-bit sprite atlas, this module packs the
//! samples into a half-float RGBA16F texture and hands it to the viewer as a
//! [`SurfaceSource::Texture`](gpui::SurfaceSource) — the wgpu renderer
//! samples it straight into the (10-bit, `Rgb10a2Unorm`) swapchain, so no
//! 8-bit quantization happens between the render and the panel.
//!
//! The wgpu device/queue come from the window the app opens; the window
//! builder registers them once via [`register_context`]. Uploads are
//! best-effort: without a registered context (tests, headless runs) they
//! return `None` and the caller falls back to the BGRA8 CPU-frame path,
//! which keeps the pre-existing behavior.

use std::sync::Mutex;

/// The window's wgpu device/queue, registered by the app's window builder
/// (the same pair gpui's renderer draws with, so textures created here are
/// visible to it). `None` before the first window opens — uploads no-op.
static GPU_CONTEXT: Mutex<Option<(std::sync::Arc<wgpu::Device>, std::sync::Arc<wgpu::Queue>)>> =
	Mutex::new(None);

/// Register the window's wgpu device/queue for the 10-bit display path.
/// The app's window builder calls this once per window (last one wins; the
/// renderers all share the same device).
pub fn register_context(device: std::sync::Arc<wgpu::Device>, queue: std::sync::Arc<wgpu::Queue>) {
	if let Ok(mut ctx) = GPU_CONTEXT.lock() {
		*ctx = Some((device, queue));
	}
}

/// Whether a GPU context is registered (a window is open). The viewer uses
/// this to decide between the 10-bit surface path and the BGRA8 fallback.
pub fn context_ready() -> bool {
	GPU_CONTEXT.lock().map(|ctx| ctx.is_some()).unwrap_or(false)
}

/// Upload F32 RGBA samples (tightly packed, `width * height * 4` values) as
/// a half-float RGBA16F GPU texture for the 10-bit display path. Returns
/// `None` when no context is registered or the samples are malformed — the
/// caller then falls back to the BGRA8 CPU-frame path.
pub fn upload_rgba16f(
	width: u32,
	height: u32,
	samples: &[f32],
) -> Option<std::sync::Arc<wgpu::Texture>> {
	if width == 0 || height == 0 || samples.len() != (width * height * 4) as usize {
		return None;
	}
	let (device, queue) = {
		let ctx = GPU_CONTEXT.lock().ok()?;
		ctx.as_ref().map(|(d, q)| (d.clone(), q.clone()))?
	};
	let bytes = super::frames::f32_rgba_to_16f_bytes(width, height, samples)?;
	let texture = device.create_texture(&wgpu::TextureDescriptor {
		label: Some("oak_display_rgba16f"),
		size: wgpu::Extent3d {
			width,
			height,
			depth_or_array_layers: 1,
		},
		mip_level_count: 1,
		sample_count: 1,
		dimension: wgpu::TextureDimension::D2,
		format: wgpu::TextureFormat::Rgba16Float,
		usage: wgpu::TextureUsages::TEXTURE_BINDING | wgpu::TextureUsages::COPY_DST,
		view_formats: &[],
	});
	queue.write_texture(
		wgpu::TexelCopyTextureInfo {
			texture: &texture,
			mip_level: 0,
			origin: wgpu::Origin3d::ZERO,
			aspect: wgpu::TextureAspect::All,
		},
		&bytes,
		wgpu::TexelCopyBufferLayout {
			offset: 0,
			bytes_per_row: None,
			rows_per_image: None,
		},
		wgpu::Extent3d {
			width,
			height,
			depth_or_array_layers: 1,
		},
	);
	Some(std::sync::Arc::new(texture))
}

/// Upload F32 display samples as an RGBA16F texture and register it for
/// `image_id` (the `RenderImage` it replaces) so the viewer switches to the
/// 10-bit surface path. Best-effort: without a registered context it's a
/// no-op and the viewer keeps the BGRA8 CPU-frame fallback.
pub fn register_display_frame(image_id: usize, width: u32, height: u32, samples: &[f32]) {
	let Some(texture) = upload_rgba16f(width, height, samples) else {
		return;
	};
	gpui_widgets::viewer::register_gpu_frame(
		image_id,
		texture,
		gpui::Size {
			width: gpui::DevicePixels::from(width),
			height: gpui::DevicePixels::from(height),
		},
	);
}
