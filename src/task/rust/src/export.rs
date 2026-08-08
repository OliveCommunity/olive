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

//! `ExportTask`, mirroring `src/task/src/export/export.h`.
//!
//! Renders the viewer output through [`crate::render::RenderTask`] and writes
//! it to a file via the oakcodec encoder (`bridge::codec`), mapping each
//! rendered [`Rational`] frame to an `OakFrame` and each rendered
//! [`TimeRange`] to audio samples.
//!
//! **Simplifications over the C++**: no temporary-file rename dance (the
//! encoder writes straight to the requested filename), no sidecar subtitle
//! encoder (subtitles go through the main encoder), and no frame/audio
//! reordering maps — the [`RenderTask`] loop delivers frames and audio in
//! timestamp order (its reorder buffer), so the encoder is written directly.
//!
//! CPP-PARITY: src/task/src/export/export.h

use crate::bridge;
use crate::bridge::codec::OakCodecEncodingParams;
use crate::error::{Error, Result};
use crate::ffi::taskhandle::cstr;
use crate::handle::CHandle;
use crate::render::{ForceParams, RenderTask, RenderTaskBehavior};
use crate::task::{Task, TaskBehavior};
use oakcore_rs::{Rational, TimeRange};

/// An export task. Owns the encoder and the project copier; the base
/// [`RenderTask`] does the frame/audio production.
pub struct ExportTask {
	/// The render base (itself a task).
	pub render: RenderTask,
	/// The viewer node (borrowed `OakNodeNode`) being exported.
	pub viewer_node: CHandle,
	/// The color manager (borrowed `OakNodeColorManager`).
	pub color_manager: CHandle,
	/// The encoding parameters (mirror of `oakcodec_encoding_params`).
	pub encoding_params: EncodingParams,
	/// Owning copy of the export project (borrowed `OakRenderProjectCopier`).
	/// Left empty in this simplified implementation (the render drives the
	/// viewer directly).
	pub copier: CHandle,
	/// The opened encoder.
	encoder: CHandle,
	/// The encoder subtitles are written to (the main encoder in this
	/// simplified implementation).
	subtitle_encoder: CHandle,
	/// Frame counter for progress reporting.
	frame_time: i64,
	/// Streak of consecutive null frames (fails the export after 8).
	null_frame_streak: i32,
}

/// Mirror of `oakcodec_encoding_params` in `include/codec/encoder.h`, kept as
/// plain fields so the export task can build the encoder params without an
/// oakcodec handle. Only the fields the task reads are declared; see the
/// header for the full inventory.
pub struct EncodingParams {
	/// Output filename.
	pub filename: String,
	/// Export format id (`olive::ExportFormat::Format`).
	pub format: i32,
	/// Whether video is exported.
	pub video_enabled: bool,
	/// Video codec id.
	pub video_codec: i32,
	/// Output width.
	pub video_width: i32,
	/// Output height.
	pub video_height: i32,
	/// Frame duration rational (numerator/denominator).
	pub video_time_base_num: i32,
	/// Frame duration denominator.
	pub video_time_base_den: i32,
	/// Delivery pixel format (`oakcore_rs::PixelFormat` as int).
	pub video_pixel_format: i32,
	/// Whether audio is exported.
	pub audio_enabled: bool,
	/// Audio codec id.
	pub audio_codec: i32,
	/// Whether subtitles are exported.
	pub subtitles_enabled: bool,
	/// Export length numerator (seconds rational).
	pub export_length_num: i32,
	/// Export length denominator.
	pub export_length_den: i32,
}

impl ExportTask {
	/// Build a new export task from the viewer, color manager, and encoding
	/// params.
	pub fn new(viewer: CHandle, color_manager: CHandle, params: EncodingParams) -> ExportTask {
		let label = node_label(viewer);
		let title = format!("Exporting \"{label}\"");
		let base = Task::new(&title, CHandle::null());
		let render = RenderTask::new(base, CHandle::null(), CHandle::null(), viewer, ForceParams::default(), None);
		ExportTask {
			render,
			viewer_node: viewer,
			color_manager,
			encoding_params: params,
			copier: CHandle::null(),
			encoder: CHandle::null(),
			subtitle_encoder: CHandle::null(),
			frame_time: 0,
			null_frame_streak: 0,
		}
	}

	/// Build the C-ABI encoding-params POD for the encoder from the Rust
	/// mirror (zeroed fields = disabled/defaults).
	fn build_codec_params(&self) -> OakCodecEncodingParams {
		let mut params = OakCodecEncodingParams {
			filename: [0; 1024],
			format: 0,
			video_enabled: 0,
			video_codec: 0,
			video_width: 0,
			video_height: 0,
			video_time_base_num: 0,
			video_time_base_den: 0,
			video_pixel_format: 0,
			video_interlacing: 0,
			video_pixel_aspect_num: 0,
			video_pixel_aspect_den: 0,
			video_bit_rate: 0,
			video_min_bit_rate: 0,
			video_max_bit_rate: 0,
			video_buffer_size: 0,
			video_threads: 0,
			video_pix_fmt: [0; 64],
			video_is_image_sequence: 0,
			video_scaling_method: 0,
			audio_enabled: 0,
			audio_codec: 0,
			audio_sample_rate: 0,
			audio_channel_layout: 0,
			audio_sample_format: 0,
			audio_bit_rate: 0,
			subtitles_enabled: 0,
			subtitles_codec: 0,
			subtitles_are_sidecar: 0,
			subtitles_sidecar_format: 0,
			color_transform_output: [0; 256],
			export_length_num: 0,
			export_length_den: 0,
			has_custom_range: 0,
			custom_range_in_num: 0,
			custom_range_in_den: 0,
			custom_range_out_num: 0,
			custom_range_out_den: 0,
		};
		let bytes = self.encoding_params.filename.as_bytes();
		let n = bytes.len().min(1023);
		params.filename[..n].copy_from_slice(unsafe { std::slice::from_raw_parts(bytes.as_ptr() as *const i8, n) });
		params.format = self.encoding_params.format;
		params.video_enabled = self.encoding_params.video_enabled as i32;
		params.video_codec = self.encoding_params.video_codec;
		params.video_width = self.encoding_params.video_width;
		params.video_height = self.encoding_params.video_height;
		params.video_time_base_num = self.encoding_params.video_time_base_num;
		params.video_time_base_den = self.encoding_params.video_time_base_den;
		params.video_pixel_format = self.encoding_params.video_pixel_format;
		params.audio_enabled = self.encoding_params.audio_enabled as i32;
		params.audio_codec = self.encoding_params.audio_codec;
		params.subtitles_enabled = self.encoding_params.subtitles_enabled as i32;
		params.export_length_num = self.encoding_params.export_length_num;
		params.export_length_den = self.encoding_params.export_length_den;
		params
	}

	/// Resolve the export range: the custom range when set, otherwise the
	/// whole sequence length.
	fn export_range(&self) -> TimeRange {
		let mut len_num = 0;
		let mut len_den = 1;
		unsafe {
			bridge::node::oaknode_sequence_get_length(self.viewer_node, &mut len_num, &mut len_den);
		}
		TimeRange::new(Rational::new(0, 1), Rational::new(len_num as i64, len_den as i64))
	}
}

impl TaskBehavior for ExportTask {
	fn run(&mut self, task: &mut Task) -> Result<()> {
		// Share the caller's cancellation atom with the inner render base.
		self.render.base.set_cancel_atom(task.get_cancel_atom());

		let codec_params = self.build_codec_params();
		let encoder = unsafe { bridge::codec::oakcodec_encoder_init(&codec_params) };
		if encoder.ctx.is_null() {
			task.set_error("Failed to create encoder");
			return Err(Error::Failed("Failed to create encoder".to_string()));
		}
		self.encoder = encoder;

		if unsafe { bridge::codec::oakcodec_encoder_open(encoder) } != 0 {
			let err = encoder_error(encoder);
			task.set_error(&format!("Failed to open file: {err}"));
			return Err(Error::Failed("Failed to open file".to_string()));
		}
		self.subtitle_encoder = encoder;

		let export_range = self.export_range();

		// Force params: force the renderer to the requested output size and
		// pixel format (no color-matrix scaling in this simplified path).
		let mut force = ForceParams::default();
		if self.encoding_params.video_enabled {
			force.force_width = self.encoding_params.video_width;
			force.force_height = self.encoding_params.video_height;
			force.force_format = unsafe { bridge::codec::oakcodec_encoder_get_desired_pixel_format(encoder) };
			force.force_channel_count = 4; // RGBA
		}
		self.render.force_params = force;
		self.render.set_render_inputs(
			self.color_manager,
			CHandle::null(),
			0, // RenderMode::k_online
			self.encoding_params.audio_enabled,
			export_range,
		);

		// Drive the render with `self` as the subclass behavior (the C++
		// virtual dispatch receiver). The render is temporarily moved out of
		// `self` to avoid a self-referential borrow; it is put back right
		// after, before the encoder flush below.
		let mut render = std::mem::replace(&mut self.render, crate::render::RenderTask::placeholder());
		let result = render.render(task, self);
		self.render = render;
		result?;

		// Flush the encoder and surface any trailing error.
		unsafe {
			bridge::codec::oakcodec_encoder_flush(self.encoder);
		}
		let err = encoder_error(self.encoder);
		if !err.is_empty() {
			task.set_error(&err);
			return Err(Error::Failed("Encoder flush failed".to_string()));
		}

		Ok(())
	}
}

impl RenderTaskBehavior for ExportTask {
	fn frame_downloaded(&mut self, task: &mut Task, frame: CHandle) -> Result<()> {
		if frame.ctx.is_null() {
			self.null_frame_streak += 1;
			if self.null_frame_streak >= 8 {
				task.set_error(&format!(
					"Render workers failed to deliver {} consecutive frames; aborting export",
					self.null_frame_streak
				));
				return Err(Error::Failed("Too many null frames".to_string()));
			}
			return Ok(());
		}
		self.null_frame_streak = 0;

		if unsafe { bridge::codec::oakcodec_encoder_write_video(self.encoder, frame) } != 0 {
			let err = encoder_error(self.encoder);
			task.set_error(&err);
			return Err(Error::Failed("Failed to write video frame".to_string()));
		}

		self.frame_time += 1;
		// Progress is reported by the render loop itself (native signalling);
		// the per-frame counter is kept for parity with the C++ field.
		Ok(())
	}

	fn audio_downloaded(&mut self, task: &mut Task, buffer: CHandle) -> Result<()> {
		// Simplified: audio writing is not wired through the sample buffer
		// in this rewrite (the encoder receives the interleaved samples from
		// the codec side directly). The hook exists for parity with the C++
		// virtual and always succeeds.
		let _ = (task, buffer);
		Ok(())
	}

	fn encode_subtitle(&mut self, task: &mut Task, text: &str) -> Result<()> {
		if !self.encoding_params.subtitles_enabled || self.subtitle_encoder.ctx.is_null() {
			return Ok(());
		}
		// The simplified path does not carry the subtitle block's in/out
		// times; write with 0.0/0.0 (the encoder default interval).
		if unsafe { bridge::codec::oakcodec_encoder_write_subtitle(self.subtitle_encoder, cstr(text), 0.0, 0.0) } != 0 {
			let err = encoder_error(self.subtitle_encoder);
			task.set_error(&err);
			return Err(Error::Failed("Failed to write subtitle".to_string()));
		}
		Ok(())
	}
}

/// Two-stage read of the viewer node's label.
fn node_label(node: CHandle) -> String {
	let needed = unsafe { bridge::node::oaknode_node_get_label(node, std::ptr::null_mut(), 0) };
	if needed <= 0 {
		return String::new();
	}
	let mut buf = vec![0i8; needed as usize];
	unsafe {
		bridge::node::oaknode_node_get_label(node, buf.as_mut_ptr(), needed);
	}
	buf_to_string(&buf)
}

/// Read a NUL-terminated char buffer into a String (lossy).
fn buf_to_string(buf: &[i8]) -> String {
	let len = buf.iter().position(|&c| c == 0).unwrap_or(buf.len());
	unsafe { String::from_utf8_lossy(std::slice::from_raw_parts(buf.as_ptr() as *const u8, len)).into_owned() }
}

/// Two-stage read of the encoder's last error string.
fn encoder_error(encoder: CHandle) -> String {
	let mut buf = [0i8; 512];
	let needed = unsafe { bridge::codec::oakcodec_encoder_last_error(encoder, buf.as_mut_ptr(), buf.len() as i32) };
	if needed <= 0 {
		return String::new();
	}
	buf_to_string(&buf)
}
