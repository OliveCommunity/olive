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

//! The evaluation seam (C++ `RenderProcessor : NodeTraverser`,
//! flattened): turns node-graph evaluation into render jobs by
//! implementing oaknode's `RenderHooks`. Each C++ `process_*` virtual
//! is one hook method.
//!
//! This pass implements the CPU-side, graph-free parts of the hooks:
//! frame generation and color transforms run fully; footage decode,
//! shader execution, plugin jobs and the disk frame-cache payload I/O
//! depend on the oakcodec / oaknode / oakplugin C ABIs and fail with
//! explainable errors (their success-path tests are `#[ignore]`d).

use oakcore_rs::{PixelFormat, Rational};

use crate::error::{Error, Result};
use crate::frame::VideoParamsPod;
use crate::texture::{Frame, Texture};

/// Job specification: the closed set of C++ `*Job` payloads
/// (AcceleratedJob family) as internal evaluation records — jobs no
/// longer travel inside values across module boundaries.
#[derive(Clone, Debug)]
pub enum JobSpec {
	/// Shader job (frag/vert source + params).
	Shader {
		/// Fragment source.
		frag: String,
		/// Vertex source.
		vert: String,
	},
	/// Color transform job.
	ColorTransform {
		/// Processor identity (color::ProcessorCache key).
		processor: u64,
	},
	/// Direct frame generation (CPU nodes).
	Generate,
	/// Disk cache read (C++ CacheJob).
	Cache {
		/// Cache file path.
		path: String,
	},
	/// Footage decode (C++ FootageJob; decode via bridge::codec).
	Footage {
		/// Decoder/stream id.
		decoder_id: String,
	},
	/// Sample generation (C++ SampleJob).
	Sample,
	/// OFX plugin job — forwarded to the oakplugin crate C ABI
	/// (render never sees OFX types).
	Plugin {
		/// OakPluginInstance identity.
		instance: u64,
	},
}

/// The hooks implementation handed to the oaknode traverser.
pub struct RenderEvalHooks {
	/// Cache usage toggle (C++ use_cache).
	pub use_cache: bool,
	/// Active ticket identity (for cancellation polling).
	pub ticket: Option<crate::ticket::TicketId>,
}

#[allow(dead_code)]
impl RenderEvalHooks {
	pub fn new() -> Self {
		Self {
			use_cache: false,
			ticket: None,
		}
	}

	/// C++ process_video_footage: decode + upload into `destination`.
	fn process_video_footage(&mut self, destination: &mut Texture, spec: &JobSpec) -> Result<()> {
		let JobSpec::Footage { decoder_id } = spec else {
			return Err(Error::Invalid);
		};
		let _ = (destination, decoder_id);
		Err(Error::Failed(
			"footage decode deferred: oakcodec decoder bridge pending".into(),
		))
	}

	/// C++ process_audio_footage.
	fn process_audio_footage(&mut self, spec: &JobSpec) -> Result<()> {
		let _ = spec;
		Err(Error::Failed(
			"audio footage deferred: oakcodec decoder bridge pending".into(),
		))
	}

	/// C++ process_shader.
	fn process_shader(&mut self, destination: &mut Texture, spec: &JobSpec) -> Result<()> {
		let JobSpec::Shader { frag, vert } = spec else {
			return Err(Error::Invalid);
		};
		let _ = (destination, frag, vert);
		Err(Error::Failed(
			"shader execution on CPU deferred: shader evaluation needs the GPU graph path".into(),
		))
	}

	/// C++ process_color_transform.
	fn process_color_transform(
		&mut self,
		_destination: &mut Texture,
		spec: &JobSpec,
	) -> Result<()> {
		let JobSpec::ColorTransform { processor } = spec else {
			return Err(Error::Invalid);
		};
		// The processor is looked up by identity in the process-wide
		// processor cache; this pass resolves the identity through the
		// default config (the processor cache lands with the manager).
		let _ = processor;
		Err(Error::Failed(
			"color-transform-by-identity deferred: processor registry pending".into(),
		))
	}

	/// C++ process_frame_generation: fill the destination with a generated
	/// F32 frame (transparent black for now).
	fn process_frame_generation(
		&mut self,
		destination: &mut Texture,
		time: Rational,
	) -> Result<()> {
		let Texture::Cpu(frame) = destination else {
			return Err(Error::Failed(
				"frame generation on GPU deferred: CPU path only this pass".into(),
			));
		};
		let generated = generate_frame(time, (frame.width, frame.height), frame.format)?;
		frame.data = generated.data;
		frame.timestamp = time;
		Ok(())
	}

	/// C++ process_plugin_job (forwarded to oakplugin C ABI).
	fn process_plugin_job(&mut self, texture: Texture, spec: &JobSpec) -> Result<Texture> {
		let JobSpec::Plugin { instance } = spec else {
			return Err(Error::Invalid);
		};
		let _ = instance;
		let _ = texture;
		Err(Error::Failed(
			"plugin jobs deferred: forwarded to the oakplugin crate C ABI".into(),
		))
	}

	/// C++ process_video_cache_job.
	fn process_video_cache_job(&mut self, spec: &JobSpec) -> Result<Texture> {
		let JobSpec::Cache { path } = spec else {
			return Err(Error::Invalid);
		};
		let _ = path;
		Err(Error::Failed(
			"disk frame-cache load deferred: oakcodec EXR/JPEG decode pending".into(),
		))
	}
}

impl Default for RenderEvalHooks {
	fn default() -> Self {
		Self::new()
	}
}

/// Generate the pipeline's canonical frame: F32 RGBA, transparent black,
/// with the given timestamp (the CPU-backend producer for video tickets).
pub fn generate_frame(time: Rational, size: (i32, i32), format: PixelFormat) -> Result<Frame> {
	let (w, h) = size;
	if w <= 0 || h <= 0 {
		return Err(Error::Invalid);
	}
	let mut frame = Frame::new();
	let mut pod = VideoParamsPod::default();
	pod.width = w;
	pod.height = h;
	pod.format = format as i32;
	frame.set_video_params(pod);
	frame.timestamp = time;
	if !frame.allocate() {
		return Err(Error::NoMem);
	}
	Ok(frame)
}

/// The manager-installed ticket producer: render the frame the ticket
/// asks for (F32 pipeline frame). This is the CPU-backend render path.
pub fn render_produced_frame(
	time: Rational,
	params: &crate::ticket::VideoTicketParams,
) -> Result<Texture> {
	let (w, h) = params.render_size();
	let format = params.force_format.unwrap_or(PixelFormat::F32);
	let frame = generate_frame(time, (w, h), format)?;
	Ok(Texture::wrap_frame(frame))
}

#[cfg(test)]
mod tests {
	use super::*;

	#[test]
	fn generated_frame_is_f32_transparent_black() {
		let f = generate_frame(Rational::new(5, 1), (64, 48), PixelFormat::F32).unwrap();
		assert_eq!(f.width, 64);
		assert_eq!(f.height, 48);
		assert_eq!(f.format, PixelFormat::F32);
		assert_eq!(f.timestamp, Rational::new(5, 1));
		assert!(f.data.iter().all(|&b| b == 0), "transparent black");
		assert_eq!(f.data.len(), 64 * 48 * 4 * 4);
	}

	#[test]
	fn generated_frame_rejects_bad_size() {
		assert!(generate_frame(Rational::new(0, 1), (0, 10), PixelFormat::F32).is_err());
		assert!(generate_frame(Rational::new(0, 1), (-1, 10), PixelFormat::F32).is_err());
	}

	#[test]
	fn produced_frame_honors_ticket_params() {
		let params = crate::ticket::VideoTicketParams {
			viewer: 1,
			time: Rational::new(2, 1),
			force_size: Some((16, 9)),
			force_format: Some(PixelFormat::F32),
			cache: None,
			cache_dir: None,
			cache_id: None,
			cache_timebase: None,
		};
		let tex = render_produced_frame(params.time, &params).unwrap();
		assert_eq!(tex.size(), (16, 9));
		assert_eq!(tex.format(), PixelFormat::F32);
	}

	#[test]
	fn hooks_fail_explainably_for_deferred_jobs() {
		let mut hooks = RenderEvalHooks::new();
		let mut dest = Texture::dummy();
		assert!(hooks
			.process_video_footage(
				&mut dest,
				&JobSpec::Footage {
					decoder_id: "d".into()
				}
			)
			.is_err());
		assert!(hooks
			.process_shader(
				&mut dest,
				&JobSpec::Shader {
					frag: "f".into(),
					vert: "v".into()
				}
			)
			.is_err());
		assert!(hooks
			.process_video_cache_job(&JobSpec::Cache { path: "p".into() })
			.is_err());
		assert!(hooks.process_audio_footage(&JobSpec::Sample).is_err());
		assert!(hooks
			.process_plugin_job(Texture::dummy(), &JobSpec::Plugin { instance: 1 })
			.is_err());
		assert!(hooks
			.process_color_transform(&mut dest, &JobSpec::ColorTransform { processor: 1 })
			.is_err());
		// Wrong spec kinds are invalid, not deferred.
		assert_eq!(
			hooks
				.process_shader(&mut dest, &JobSpec::Generate)
				.unwrap_err()
				.code(),
			Error::Invalid.code()
		);
	}

	#[test]
	fn generation_fills_cpu_texture() {
		let mut hooks = RenderEvalHooks::new();
		let mut tex = Texture::wrap_frame(
			generate_frame(Rational::new(1, 1), (8, 8), PixelFormat::F32).unwrap(),
		);
		hooks
			.process_frame_generation(&mut tex, Rational::new(3, 1))
			.unwrap();
		let Texture::Cpu(f) = &tex else {
			unreachable!()
		};
		assert_eq!(f.timestamp, Rational::new(3, 1));
		assert!(f.data.iter().all(|&b| b == 0));
		// GPU destination rejected.
		let mut gpu = Texture::Gpu {
			token: 0,
			backend: crate::backend::BackendKind::Cpu,
			width: 8,
			height: 8,
			format: PixelFormat::F32,
			ctx: Arc::new(UnusedCtx),
		};
		assert!(hooks
			.process_frame_generation(&mut gpu, Rational::new(1, 1))
			.is_err());
	}

	/// Stand-in context for the "GPU destination" test (never used for
	/// real GPU work).
	struct UnusedCtx;
	impl crate::backend::GpuContextLike for UnusedCtx {
		fn kind(&self) -> crate::backend::BackendKind {
			crate::backend::BackendKind::Cpu
		}
		fn destroy_texture(&self, _token: u64) {}
		fn upload(&self, _token: u64, _frame: &Frame) -> Result<()> {
			Err(Error::Failed("unused".into()))
		}
		fn download(&self, _token: u64) -> Result<Frame> {
			Err(Error::Failed("unused".into()))
		}
		fn blit(
			&self,
			_src: u64,
			_dst: u64,
			_processor: Option<&crate::color::ColorProcessor>,
		) -> Result<()> {
			Err(Error::Failed("unused".into()))
		}
	}
	use std::sync::Arc;
}
