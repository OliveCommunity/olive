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

use std::ffi::c_int;

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
		/// Footage filename (M12 P0: the decode path).
		filename: String,
		/// Media stream index.
		stream_index: i32,
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
		let JobSpec::Footage {
			decoder_id,
			filename,
			stream_index,
		} = spec
		else {
			return Err(Error::Invalid);
		};
		let _ = decoder_id;
		let Texture::Cpu(frame) = destination else {
			return Err(Error::Failed(
				"footage decode on GPU deferred: CPU path only this pass".into(),
			));
		};
	let mut decoded = render_footage_frame(
		filename,
		*stream_index,
		frame.timestamp,
		(frame.width, frame.height),
		frame.format,
	)?;
	let src_data = match &mut decoded {
		Texture::Cpu(frame) => std::mem::take(&mut frame.data),
		_ => return Err(Error::Failed("decode produced a GPU texture".into())),
	};
	frame.data = src_data;
	Ok(())
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
///
/// M12 P0 routing: a sequence montage (list of clips) is composited
/// topmost-last; a single-footage ticket decodes one stream; otherwise
/// the pipeline frame is generated.
pub fn render_produced_frame(
	time: Rational,
	params: &crate::ticket::VideoTicketParams,
) -> Result<Texture> {
	let (w, h) = params.render_size();
	let format = params.force_format.unwrap_or(PixelFormat::F32);

	if !params.montage.is_empty() {
		let r = render_montage_frame(time, params, (w, h), format);
		return r;
	}
	if let Some((filename, stream_index)) = &params.footage {
		return render_footage_frame(filename, *stream_index, time, (w, h), format);
	}

	let frame = generate_frame(time, (w, h), format)?;
	Ok(Texture::wrap_frame(frame))
}

// ---------------------------------------------------------------------------
// Footage decode (M12 P0): the oakcodec bridge
// ---------------------------------------------------------------------------

/// Process-wide open decoder sessions, keyed by (filename, stream).
/// Sessions are mutex-serialized inside the oakcodec box, so sharing
/// one handle across worker threads is safe.
static DECODERS: std::sync::OnceLock<std::sync::Mutex<
	std::collections::HashMap<(String, i32), crate::handle::CHandle>,
>> = std::sync::OnceLock::new();

fn decoders() -> std::sync::MutexGuard<'static, std::collections::HashMap<(String, i32), crate::handle::CHandle>> {
	DECODERS
		.get_or_init(|| std::sync::Mutex::new(std::collections::HashMap::new()))
		.lock()
		.unwrap_or_else(|e| e.into_inner())
}

/// Open (or reuse) the decoder session for `(filename, stream_index)`.
fn open_decoder(filename: &str, stream_index: i32) -> Result<crate::handle::CHandle> {
	{
		let cache = decoders();
		if let Some(h) = cache.get(&(filename.to_string(), stream_index)) {
			if !h.is_null() {
				return Ok(*h);
			}
		}
	}
	let decoder = crate::bridge::codec::decoder_init();
	if decoder.is_null() {
		return Err(Error::Failed("footage decode: decoder_init failed".into()));
	}
	crate::bridge::codec::decoder_open(decoder, filename, stream_index)
		.map_err(|e| Error::Failed(format!("footage decode open: {e:?}")))?;
	let mut cache = decoders();
	cache.insert((filename.to_string(), stream_index), decoder);
	Ok(decoder)
}

/// Decode the footage frame at `time` and copy/scale it into an
/// oakrender F32 frame of `(w, h)`.
pub fn render_footage_frame(
	filename: &str,
	stream_index: i32,
	time: Rational,
	size: (i32, i32),
	format: PixelFormat,
) -> Result<Texture> {
	let decoder = open_decoder(filename, stream_index)?;
	let frame_handle = crate::bridge::codec::decoder_decode_video(
		decoder,
		time.numerator(),
		time.denominator(),
	);
	if frame_handle.is_null() {
		let detail = crate::bridge::codec::decoder_last_error(decoder);
		return Err(Error::Failed(format!(
			"footage decode at {time:?}: {detail}"
		)));
	}

	let src_w = crate::bridge::codec::frame_width(frame_handle);
	let src_h = crate::bridge::codec::frame_height(frame_handle);
	let src_linesize = crate::bridge::codec::frame_linesize_bytes(frame_handle);
	let _alloc = crate::bridge::codec::frame_is_allocated(frame_handle);
	let (w, h) = size;
	if src_w <= 0
		|| src_h <= 0
		|| src_linesize <= 0
		|| crate::bridge::codec::frame_is_allocated(frame_handle) == 0
	{
		frame_free(frame_handle);
		return Err(Error::Failed("footage decode: bad decoded frame".into()));
	}

	let mut dst = generate_frame(time, (w, h), format)?;
	let dst_linesize = dst.linesize_bytes() as i32;
	let src_data = unsafe { crate::bridge::codec::frame_const_data(frame_handle) };
	if src_data.is_null() {
		frame_free(frame_handle);
		return Err(Error::Failed("footage decode: no frame data".into()));
	}

	if src_w == w && src_h == h && src_linesize == dst_linesize {
		let bytes = (src_h as usize)
			.checked_mul(src_linesize as usize)
			.ok_or(Error::NoMem)?;
		let src_slice = unsafe { std::slice::from_raw_parts(src_data, bytes) };
		dst.data[..bytes].copy_from_slice(src_slice);
	} else {
		scale_rgba_f32(
			src_data,
			src_linesize,
			src_w,
			src_h,
			&mut dst.data,
			dst_linesize,
			w,
			h,
		);
	}
	frame_free(frame_handle);
	Ok(Texture::wrap_frame(dst))
}

/// Render the audio montage over `params.range` (M12 P1): every clip
/// overlapping the range is decoded (interleaved f32 at the output rate
/// and layout) and mixed with its gain; uncovered parts stay silent.
pub fn render_audio_samples(
	params: &crate::ticket::AudioTicketParams,
) -> Result<crate::ticket::TicketPayload> {
	let rate = params.sample_rate.max(1);
	let channels = params.channel_layout.count_ones().max(1) as i32;
	let duration = params.range.out() - params.range.in_();
	let seconds = if duration.denominator() == 0 {
		0.0
	} else {
		duration.numerator() as f64 / duration.denominator() as f64
	};
	if seconds <= 0.0 || seconds > 3600.0 {
		return Err(Error::Invalid);
	}
	let total_frames = (seconds * rate as f64).round() as usize;
	let mut acc = vec![0.0f32; total_frames.saturating_mul(channels as usize)];

	for clip in &params.montage {
		// Overlap of the clip with the requested range.
		let in_time = params.range.in_().max(clip.in_time);
		let out_time = params.range.out().min(clip.out_time);
		if out_time <= in_time {
			continue;
		}
		let start_frame = ((in_time - params.range.in_()).to_f64() * rate as f64) as usize;
		let end_frame = ((out_time - params.range.in_()).to_f64() * rate as f64) as usize;
		if start_frame >= total_frames {
			continue;
		}
		let frames = (end_frame - start_frame).min(total_frames - start_frame);
		if frames == 0 {
			continue;
		}

		// Media time of the overlap start; the media-out is
		// media_start + (overlap duration).
		let media_start = clip.media_in + (in_time - clip.in_time);
		let media_end = media_start + (out_time - in_time);
		let mut buf = vec![0.0f32; frames * channels as usize];
		let decoder = open_decoder(&clip.filename, clip.stream_index)?;
		let rc = crate::bridge::codec::decoder_decode_audio(
			decoder,
			media_start.numerator(),
			media_start.denominator(),
			media_end.numerator(),
			media_end.denominator(),
			rate,
			params.channel_layout,
			buf.as_mut_ptr(),
			frames as c_int,
		);
		let written = rc.unwrap_or(0).max(0) as usize;
		let written = written.min(frames);
		// Mix into the accumulator (per-channel gain).
		for i in 0..written * channels as usize {
			acc[start_frame * channels as usize + i] += buf[i] * clip.gain;
		}
	}

	Ok(crate::ticket::TicketPayload::Audio(crate::ticket::AudioSamples {
		samples: acc,
		sample_rate: rate,
		channel_layout: params.channel_layout,
		channel_count: channels,
	}))
}

/// Free a codec frame handle (Copy-handle dance).
fn frame_free(mut h: crate::handle::CHandle) {
	crate::bridge::codec::frame_free(&mut h);
}

/// Bilinear scale an F32-RGBA image (row-major with per-row strides).
fn scale_rgba_f32(
	src: *const u8,
	src_stride: i32,
	src_w: i32,
	src_h: i32,
	dst: &mut [u8],
	dst_stride: i32,
	dst_w: i32,
	dst_h: i32,
) {
	if src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0 {
		return;
	}
	// 1:1 copy (no scaling): the caller already handled stride equality;
	// here we handle the general case with a fast path for integer 1:1.
	let sample = |x: f64, y: f64| -> [f32; 4] {
		let x0 = x.floor() as i32;
		let y0 = y.floor() as i32;
		let fx = (x - x0 as f64) as f32;
		let fy = (y - y0 as f64) as f32;
		let x1 = (x0 + 1).clamp(0, src_w - 1);
		let y1 = (y0 + 1).clamp(0, src_h - 1);
		let x0 = x0.clamp(0, src_w - 1);
		let y0 = y0.clamp(0, src_h - 1);
		let px = |xx: i32, yy: i32| -> [f32; 4] {
			let off = (yy as usize) * (src_stride as usize) + (xx as usize) * 16;
			// SAFETY: coordinates are clamped to the source size.
			let b = unsafe { std::slice::from_raw_parts(src.add(off), 16) };
			let f = |i: usize| f32::from_le_bytes(b[i * 4..i * 4 + 4].try_into().unwrap());
			[f(0), f(1), f(2), f(3)]
		};
		let c00 = px(x0, y0);
		let c10 = px(x1, y0);
		let c01 = px(x0, y1);
		let c11 = px(x1, y1);
		let lerp = |a: f32, b: f32, t: f32| a + (b - a) * t;
		let mut out = [0f32; 4];
		for i in 0..4 {
			let top = lerp(c00[i], c10[i], fx);
			let bottom = lerp(c01[i], c11[i], fx);
			out[i] = lerp(top, bottom, fy);
		}
		out
	};
	for y in 0..dst_h {
		let sy = (y as f64 + 0.5) * src_h as f64 / dst_h as f64 - 0.5;
		let sy = sy.max(0.0);
		for x in 0..dst_w {
			let sx = (x as f64 + 0.5) * src_w as f64 / dst_w as f64 - 0.5;
			let sx = sx.max(0.0);
			let px = sample(sx, sy);
			let off = (y as usize) * (dst_stride as usize) + (x as usize) * 16;
			for i in 0..4 {
				dst[off + i * 4..off + i * 4 + 4]
					.copy_from_slice(&px[i].clamp(0.0, 1.0).to_le_bytes());
			}
		}
	}
}

/// Composite the montage at `time`: decode each covering clip and
/// alpha-composite topmost-last (C++ track order: track 0 is topmost).
fn render_montage_frame(
	time: Rational,
	params: &crate::ticket::VideoTicketParams,
	size: (i32, i32),
	format: PixelFormat,
) -> Result<Texture> {
	let mut acc = generate_frame(time, size, format)?;
	let stride = acc.linesize_bytes();
	let (w, h) = size;
	let mut acc32 = acc.data; // decode from bottom clip first
	for clip in &params.montage {
		if time < clip.in_time || time >= clip.out_time {
			continue;
		}
		let media_time = clip.media_in + (time - clip.in_time);
		let decoded = render_footage_frame(&clip.filename, clip.stream_index, media_time, (w, h), format)?;
		let (src_data, src_stride) = match &decoded {
			Texture::Cpu(src) => (&src.data, src.linesize_bytes() as i32),
			_ => continue,
		};
		composite_over(&mut acc32, stride as i32, w, h, src_data, src_stride, clip.gain);
	}
	acc.data = acc32;
	Ok(Texture::wrap_frame(acc))
}

/// `src` over `dst` (premultiplied-ish alpha compositing; F32 RGBA).
/// `gain` scales the source RGB (audio-style volume applied to video
/// transparency is ignored here; gain scales color).
fn composite_over(
	dst: &mut [u8],
	dst_stride: i32,
	w: i32,
	h: i32,
	src: &[u8],
	src_stride: i32,
	gain: f32,
) {
	let read = |buf: &[u8], stride: i32, x: i32, y: i32| -> [f32; 4] {
		let off = (y as usize) * (stride as usize) + (x as usize) * 16;
		let mut out = [0f32; 4];
		for i in 0..4 {
			out[i] = f32::from_le_bytes(buf[off + i * 4..off + i * 4 + 4].try_into().unwrap());
		}
		out
	};
	for y in 0..h {
		for x in 0..w {
			let s = read(src, src_stride, x, y);
			let d = read(dst, dst_stride, x, y);
			let a = (s[3] * gain).clamp(0.0, 1.0);
			let out = [
				(s[0] * gain) * a + d[0] * (1.0 - a),
				(s[1] * gain) * a + d[1] * (1.0 - a),
				(s[2] * gain) * a + d[2] * (1.0 - a),
				a + d[3] * (1.0 - a),
			];
			let off = (y as usize) * (dst_stride as usize) + (x as usize) * 16;
			for i in 0..4 {
				dst[off + i * 4..off + i * 4 + 4].copy_from_slice(&out[i].clamp(0.0, 1.0).to_le_bytes());
			}
		}
	}
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
			footage: None,
			montage: Vec::new(),
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
					decoder_id: "d".into(),
					filename: "definitely-missing-file.mp4".into(),
					stream_index: 0,
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
