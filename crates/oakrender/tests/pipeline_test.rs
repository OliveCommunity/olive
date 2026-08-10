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

//! Pipeline invariant tests: F32 + ACEScg must survive end to end.
//! These are the regression alarms for "someone silently downgraded
//! the pipeline to 8-bit".
//!
//! GPU tests skip gracefully when no adapter is available.
//! FFI success paths for the display renderer / texture / frame /
//! color processor families live here too (they need a real OCIO config
//! and a renderer).

use oakcore_rs::{PixelFormat, Rational};

use oakrender::backend::{BackendKind, DisplayRenderer, GpuContext};
use oakrender::frame::VideoParamsPod;
use oakrender::texture::{Frame, Texture};

/// A frame rendered through the CPU backend is F32 RGBA (not u8) and
/// preserves out-of-[0,1] HDR values without clamping.
#[test]
fn cpu_path_stays_f32_unclamped() {
	// The ticket producer renders the pipeline frame: F32 RGBA.
	let frame = oakrender::eval::generate_frame(
		Rational::new(0, 1),
		(16, 16),
		PixelFormat::F32,
	)
	.unwrap();
	assert_eq!(frame.format, PixelFormat::F32);
	assert_eq!(frame.channels, 4);

	// Write out-of-range HDR values and round-trip through a CPU texture.
	let mut frame = frame;
	let n = frame.pixel_count();
	let f32s: &mut [f32] = unsafe {
		std::slice::from_raw_parts_mut(frame.data.as_mut_ptr() as *mut f32, n * 4)
	};
	f32s[0] = 2.5; // > 1.0 highlight
	f32s[1] = -0.5; // < 0.0 shadow
	f32s[2] = 1.0;
	f32s[3] = 0.0;

	let tex = Texture::wrap_frame(frame);
	let back = tex.to_frame().unwrap();
	let back_f32: &[f32] = unsafe {
		std::slice::from_raw_parts(back.data.as_ptr() as *const f32, n * 4)
	};
	assert_eq!(back_f32[0], 2.5, "no clamping of HDR values");
	assert_eq!(back_f32[1], -0.5, "no clamping of sub-black values");
	assert_eq!(back_f32[2], 1.0);
}

/// Blit with a color processor applies the OCIO transform in float
/// (CPU path; the GPU color-managed path is documented-deferred).
#[test]
fn blit_applies_ocio_in_float() {
	let _ = oakrender::color::set_up_default_config();
	let processor = oakrender::color::ColorProcessor::create(
		"ACEScg",
		"sRGB Encoded Rec.709 (sRGB)",
		oakrender::color::Direction::Normal,
	)
	.expect("handle always returned");
	if !processor.is_valid() {
		eprintln!("no valid processor for ACEScg→sRGB Encoded; skipping");
		return;
	}

	let mut renderer = DisplayRenderer::new(BackendKind::Cpu);
	let mut pod = VideoParamsPod::default();
	pod.width = 4;
	pod.height = 4;
	let mut src = renderer.create_texture(&pod, None).unwrap();
	let mut dst = renderer.create_texture(&pod, None).unwrap();

	// 18% grey (0.18) scene linear; the transform must change it and keep
	// alpha untouched (float math end to end).
	let Texture::Cpu(sf) = &mut src else { unreachable!() };
	let f32s: &mut [f32] = unsafe {
		std::slice::from_raw_parts_mut(sf.data.as_mut_ptr() as *mut f32, sf.pixel_count() * 4)
	};
	for px in f32s.chunks_exact_mut(4) {
		px[0] = 0.18;
		px[1] = 0.18;
		px[2] = 0.18;
		px[3] = 1.0;
	}

	renderer
		.blit_color_managed(Some(&src), &mut dst, Some(&processor))
		.unwrap();
	let Texture::Cpu(df) = &dst else { unreachable!() };
	let out: &[f32] = unsafe {
		std::slice::from_raw_parts(df.data.as_ptr() as *const f32, df.pixel_count() * 4)
	};
	assert!(
		(out[0] - 0.18).abs() > 1e-4,
		"the OCIO transform must actually change the pixel (got {})",
		out[0]
	);
	assert!((out[3] - 1.0).abs() < 1e-5, "alpha preserved");

	// Pass-through processor is a no-op copy.
	renderer
		.blit_color_managed(
			Some(&src),
			&mut dst,
			Some(&oakrender::color::ColorProcessor::pass_through()),
		)
		.unwrap();
}

/// Texture upload/download round-trip preserves F32 bit patterns
/// (CPU path; NaN-safe comparison excluded).
#[test]
fn texture_roundtrip_bit_exact_f32() {
	let mut frame = Frame::new();
	let mut pod = VideoParamsPod::default();
	pod.width = 4;
	pod.height = 2;
	frame.set_video_params(pod);
	frame.allocate();
	// Distinct bit patterns per pixel.
	for i in 0..frame.data.len() / 4 {
		frame.data[i * 4] = (i % 251) as u8;
		frame.data[i * 4 + 1] = (i * 3 % 251) as u8;
		frame.data[i * 4 + 2] = (i * 7 % 251) as u8;
		frame.data[i * 4 + 3] = (i * 11 % 251) as u8;
	}
	let tex = Texture::wrap_frame(frame.clone());
	let back = tex.to_frame().unwrap();
	assert_eq!(back.data, frame.data, "bit-exact F32 round-trip");
}

/// GPU path (skipped without a GPU): same invariants through the wgpu
/// backend; tolerance 1e-4 for driver variance.
#[test]
fn gpu_path_f32_invariants() {
	let Some(ctx) = GpuContext::create(BackendKind::Auto) else {
		eprintln!("no GPU adapter; skipping gpu_path_f32_invariants");
		return;
	};
	let w = 16;
	let h = 8;
	let token = ctx.create_texture(w, h).unwrap();

	let mut frame = Frame::new();
	let mut pod = VideoParamsPod::default();
	pod.width = w;
	pod.height = h;
	frame.set_video_params(pod);
	frame.allocate();
	let f32s: &mut [f32] = unsafe {
		std::slice::from_raw_parts_mut(frame.data.as_mut_ptr() as *mut f32, frame.pixel_count() * 4)
	};
	for (i, px) in f32s.chunks_exact_mut(4).enumerate() {
		px[0] = 0.1 * i as f32;
		px[1] = -0.25;
		px[2] = 1.5; // HDR out of [0,1]
		px[3] = 1.0;
	}

	ctx.upload(token, &frame).unwrap();
	let out = ctx.download(token).unwrap();
	let out_f32: &[f32] = unsafe {
		std::slice::from_raw_parts(out.data.as_ptr() as *const f32, out.pixel_count() * 4)
	};
	for (i, px) in out_f32.chunks_exact(4).enumerate() {
		assert!(
			(px[0] - 0.1 * i as f32).abs() < 1e-4,
			"R channel pixel {i} preserved"
		);
		assert!((px[1] + 0.25).abs() < 1e-4, "negative values unclamped");
		assert!((px[2] - 1.5).abs() < 1e-4, "HDR values unclamped");
		assert!((px[3] - 1.0).abs() < 1e-4);
	}

	// Blit preserves pixels through the WGSL pipeline.
	let dst = ctx.create_texture(w, h).unwrap();
	ctx.blit(token, dst, None).unwrap();
	let blit = ctx.download(dst).unwrap();
	assert_eq!(blit.data, out.data, "plain-copy blit is pixel-exact on GPU");

	ctx.destroy_texture(token);
	ctx.destroy_texture(dst);
}

/// FFI success paths: renderer → texture create/upload/download/params,
/// frame get/set, blit, color processors (real OCIO).
#[test]
fn ffi_display_renderer_texture_success_paths() {
	use oakrender::error::OAKRENDER_OK;
	use oakrender::ffi;

	unsafe {
		// Color processor create with a real config.
		let _ = ffi::color::oakrender_color_manager_set_up_default_config();

		let proc = ffi::color::oakrender_color_processor_create(
			c"ACEScg".as_ptr(),
			c"sRGB Encoded Rec.709 (sRGB)".as_ptr(),
			0,
		);
		if proc.is_null() {
			eprintln!("no OCIO config; skipping processor half");
		} else {
			assert_eq!(ffi::color::oakrender_color_processor_is_valid(proc), 1);
			let (mut r, mut g, mut b, mut a) = (0.0, 0.0, 0.0, 0.0);
			assert_eq!(
				ffi::color::oakrender_color_processor_convert(
					proc, 0.18, 0.18, 0.18, 1.0, &mut r, &mut g, &mut b, &mut a
				),
				OAKRENDER_OK
			);
			assert!(r != 0.18 || g != 0.18 || b != 0.18, "transform applied");
			assert!((a - 1.0).abs() < 1e-9, "alpha preserved");
			// convert_frame on an F32 frame.
			let frame = ffi::renderer::oakrender_codec_frame_create();
			let mut pod = std::mem::zeroed::<ffi::OakRenderVideoParams>();
			pod.width = 4;
			pod.height = 4;
			pod.format = 4;
			assert_eq!(ffi::renderer::oakrender_codec_frame_set_video_params(frame, &pod), OAKRENDER_OK);
			assert_eq!(ffi::renderer::oakrender_codec_frame_allocate(frame), OAKRENDER_OK);
			assert_eq!(ffi::renderer::oakrender_codec_frame_is_allocated(frame), 1);
			assert_eq!(ffi::renderer::oakrender_codec_frame_width(frame), 4);
			assert_eq!(ffi::renderer::oakrender_codec_frame_linesize_bytes(frame), 4 * 4 * 4);
			assert!(!ffi::renderer::oakrender_codec_frame_data(frame).is_null());
			// Fill 0.18 grey.
			let data = ffi::renderer::oakrender_codec_frame_data(frame) as *mut f32;
			for i in 0..(4 * 4 * 4) {
				*data.add(i) = 0.18;
			}
			assert_eq!(ffi::color::oakrender_color_processor_convert_frame(proc, frame), OAKRENDER_OK);
			let first = *data;
			assert!((first - 0.18).abs() > 1e-4, "convert_frame applied in place");
			// Error path: empty processor handle.
			assert_eq!(
				ffi::color::oakrender_color_processor_convert_frame(ffi::OakColorProcessor::null(), frame),
				-70001
			);
			let mut frame = frame;
			ffi::renderer::oakrender_codec_frame_free(&mut frame);
			let mut proc = proc;
			ffi::color::oakrender_color_processor_free(&mut proc);
		}

		// Display renderer + texture success path (CPU-fallback capable).
		let renderer = ffi::renderer::oakrender_display_renderer_create_opengl();
		assert!(!renderer.is_null());
		let rc = ffi::renderer::oakrender_display_renderer_init(renderer, std::ptr::null_mut());
		let is_open_gl = ffi::renderer::oakrender_display_renderer_is_open_gl(renderer);
		let is_vulkan = ffi::renderer::oakrender_display_renderer_is_vulkan(renderer);
		assert!(is_open_gl == 1 || rc == -70003, "GL renderer reports GL or init failed headless");
		assert_eq!(is_vulkan, 0);

		let mut vp = std::mem::zeroed::<ffi::OakRenderVideoParams>();
		vp.width = 8;
		vp.height = 4;
		vp.format = 4;
		vp.pixel_aspect_num = 1;
		vp.pixel_aspect_den = 1;
		vp.divider = 1;

		let tex = ffi::renderer::oakrender_display_texture_create(
			renderer, &vp, std::ptr::null(), 0,
		);
		assert!(!tex.is_null(), "texture created (GPU or CPU path)");
		assert_eq!(ffi::renderer::oakrender_display_texture_is_dummy(tex), 0);

		// Upload + download round-trip.
		let linesize: i32 = 8 * 4 * 4;
		let mut pixels = vec![0u8; linesize as usize * 4];
		for (i, px) in pixels.chunks_exact_mut(4).enumerate() {
			px[0] = (i % 251) as u8;
			px[1] = (i * 3 % 251) as u8;
			px[2] = (i * 7 % 251) as u8;
			px[3] = 255;
		}
		assert_eq!(
			ffi::renderer::oakrender_display_texture_upload(tex, pixels.as_ptr() as *const std::ffi::c_void, linesize),
			OAKRENDER_OK
		);
		let mut back = vec![0u8; linesize as usize * 4];
		assert_eq!(
			ffi::renderer::oakrender_display_texture_download(tex, back.as_mut_ptr() as *mut std::ffi::c_void, linesize),
			OAKRENDER_OK
		);
		assert_eq!(back, pixels, "upload/download round-trip");

		// get_params returns the pod.
		let mut out = std::mem::zeroed::<ffi::OakRenderVideoParams>();
		assert_eq!(ffi::renderer::oakrender_display_texture_get_params(tex, &mut out), OAKRENDER_OK);
		assert_eq!(out.width, 8);
		assert_eq!(out.height, 4);
		assert_eq!(out.format, 4);

		// get_frame returns an owned frame handle.
		let mut frame_h = ffi::OakCodecFrame::null();
		assert_eq!(ffi::renderer::oakrender_display_texture_get_frame(tex, &mut frame_h), OAKRENDER_OK);
		assert!(!frame_h.is_null());
		assert_eq!(ffi::renderer::oakrender_codec_frame_width(frame_h), 8);
		let mut frame_h = frame_h;
		ffi::renderer::oakrender_codec_frame_free(&mut frame_h);

		// retain + error paths.
		let retained = ffi::renderer::oakrender_display_texture_retain(tex);
		assert!(!retained.is_null());
		let mut retained = retained;
		ffi::renderer::oakrender_display_texture_free(&mut retained);
		assert_eq!(
			ffi::renderer::oakrender_display_texture_upload(tex, std::ptr::null(), 0),
			-70001
		);
		let mut tex = tex;
		ffi::renderer::oakrender_display_texture_free(&mut tex);
		assert!(tex.is_null());

		let mut renderer = renderer;
		ffi::renderer::oakrender_display_renderer_destroy(&mut renderer);
	}
}

/// FFI blit success path (CPU path applies the OCIO processor in float).
#[test]
fn ffi_blit_color_managed_success() {
	use oakrender::error::OAKRENDER_OK;
	use oakrender::ffi;

	unsafe {
		let _ = ffi::color::oakrender_color_manager_set_up_default_config();
		let proc = ffi::color::oakrender_color_processor_create(
			c"ACEScg".as_ptr(),
			c"sRGB Encoded Rec.709 (sRGB)".as_ptr(),
			0,
		);
		if proc.is_null() {
			eprintln!("no OCIO config; skipping blit transform check");
			return;
		}

		// CPU renderer: init fails (no GPU needed) but textures are CPU.
		let renderer = ffi::renderer::oakrender_display_renderer_create_dynamic(c"cpu".as_ptr());
		assert!(!renderer.is_null());
		let _ = ffi::renderer::oakrender_display_renderer_init(renderer, std::ptr::null_mut());

		let mut vp = std::mem::zeroed::<ffi::OakRenderVideoParams>();
		vp.width = 4;
		vp.height = 4;
		vp.format = 4;
		let src = ffi::renderer::oakrender_display_texture_create(renderer, &vp, std::ptr::null(), 0);
		let dst = ffi::renderer::oakrender_display_texture_create(renderer, &vp, std::ptr::null(), 0);
		assert!(!src.is_null() && !dst.is_null());

		// Fill source with 0.18 grey.
		let linesize: i32 = 4 * 4 * 4;
		let mut pixels = vec![0u8; linesize as usize * 4];
		let f32s: &mut [f32] = unsafe {
			std::slice::from_raw_parts_mut(pixels.as_mut_ptr() as *mut f32, 4 * 4 * 4)
		};
		for px in f32s.chunks_exact_mut(4) {
			px[0] = 0.18;
			px[1] = 0.18;
			px[2] = 0.18;
			px[3] = 1.0;
		}
		assert_eq!(ffi::renderer::oakrender_display_texture_upload(src, pixels.as_ptr() as *const std::ffi::c_void, linesize), OAKRENDER_OK);

		let job = ffi::OakColorTransformJob {
			processor: proc.ctx as *const std::ffi::c_void,
			input_texture: src.ctx,
			input_alpha_association: 0,
			clear_destination: 1,
			force_opaque: 0,
			matrix: [0.0; 16],
			crop_matrix: [0.0; 16],
		};
		assert_eq!(
			ffi::renderer::oakrender_display_renderer_blit_color_managed(renderer, &job, dst, std::ptr::null()),
			OAKRENDER_OK
		);
		// The blit applied the OCIO transform in float.
		let mut back = vec![0u8; linesize as usize * 4];
		assert_eq!(ffi::renderer::oakrender_display_texture_download(dst, back.as_mut_ptr() as *mut std::ffi::c_void, linesize), OAKRENDER_OK);
		let out_f32: &[f32] = unsafe {
			std::slice::from_raw_parts(back.as_ptr() as *const f32, 4 * 4 * 4)
		};
		assert!(
			(out_f32[0] - 0.18).abs() > 1e-4,
			"blit applies the color transform (got {})",
			out_f32[0]
		);
		assert!((out_f32[3] - 1.0).abs() < 1e-5);

		// Error paths.
		assert_eq!(
			ffi::renderer::oakrender_display_renderer_blit_color_managed(renderer, std::ptr::null(), dst, std::ptr::null()),
			-70001
		);

		let mut src = src;
		let mut dst = dst;
		ffi::renderer::oakrender_display_texture_free(&mut src);
		ffi::renderer::oakrender_display_texture_free(&mut dst);
		let mut proc = proc;
		ffi::color::oakrender_color_processor_free(&mut proc);
		let mut renderer = renderer;
		ffi::renderer::oakrender_display_renderer_destroy(&mut renderer);
	}
}
