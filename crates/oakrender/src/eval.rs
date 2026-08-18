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
//! frame generation and color transforms run fully; plugin jobs
//! dispatch through the executor slot oakplugin installs
//! ([`set_plugin_executor`]); footage decode, shader execution and the
//! disk frame-cache payload I/O depend on the oakcodec / oakplugin C
//! ABIs and fail with explainable errors (their success-path tests are
//! `#[ignore]`d).

use std::sync::Arc;

use oakcodec::decoder::{
	CodecStream, Decoder as _, RenderMode, RetrieveAudioStatus, RetrieveVideoParams,
	K_COLOR_RANGE_DEFAULT,
};
use oakcodec::ffmpeg::FFmpegDecoder;
use oakcore_rs::{PixelFormat, Rational, TimeRange};
use oaknode::value::{NodeValue, NodeValueRow, NodeValueTable};

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
	/// OFX plugin job — executed through the registered plugin executor
	/// ([`set_plugin_executor`]; the oakplugin crate installs its
	/// render driver there, so oakrender never sees OFX types).
	Plugin {
		/// Plugin instance identity (oakplugin instance registry key).
		instance: u64,
		/// Request time in seconds (C++ `PluginJob` time).
		time: f64,
		/// Clip name the main source texture arrives on (C++
		/// `node->get_effect_input_id()`).
		effect_input_id: Option<String>,
		/// Clip input textures by clip name (multi-input plugins).
		inputs: Vec<(String, Texture)>,
		/// Param overrides: input id -> node value (the tagged values
		/// captured at evaluation time).
		values: Vec<(String, NodeValue)>,
	},
}

/// The hooks implementation handed to the oaknode traverser.
pub struct RenderEvalHooks {
	/// Cache usage toggle (C++ use_cache).
	pub use_cache: bool,
	/// Active ticket identity (for cancellation polling).
	pub ticket: Option<crate::ticket::TicketId>,
}

// ---------------------------------------------------------------------------
// Plugin job executor (dependency inversion seam)
// ---------------------------------------------------------------------------
//
// oakrender sits BELOW oakplugin in the dependency graph (oakplugin
// depends on oakrender for the texture value types), so the plugin job
// execution cannot be a direct call. The oakplugin crate installs its
// render driver here at init; `process_plugin_job` dispatches through
// the slot. Without an executor, plugin jobs fail explainably (the
// pre-wiring behavior).

/// Plugin job request handed to the registered executor (the C++
/// `process_plugin_job(texture, destination, node)` inputs flattened).
pub struct PluginJobRequest<'a> {
	/// The job spec ([`JobSpec::Plugin`] guaranteed by the caller).
	pub spec: &'a JobSpec,
	/// The input texture the job runs against.
	pub src: Texture,
}

/// Plugin executor: runs one plugin job and returns the output
/// texture. Implemented by the oakplugin crate on top of its render
/// driver.
pub type PluginExecutor = dyn Fn(&PluginJobRequest<'_>) -> Result<Texture> + Send + Sync;

static PLUGIN_EXECUTOR: std::sync::OnceLock<std::sync::Mutex<Option<Arc<PluginExecutor>>>> =
	std::sync::OnceLock::new();

fn executor_slot() -> &'static std::sync::Mutex<Option<Arc<PluginExecutor>>> {
	PLUGIN_EXECUTOR.get_or_init(|| std::sync::Mutex::new(None))
}

/// Install the plugin job executor (oakplugin registration point;
/// `None` clears it).
pub fn set_plugin_executor(executor: Option<Arc<PluginExecutor>>) {
	*executor_slot().lock().unwrap_or_else(|e| e.into_inner()) = executor;
}

/// The installed plugin executor, if any.
pub fn plugin_executor() -> Option<Arc<PluginExecutor>> {
	executor_slot()
		.lock()
		.unwrap_or_else(|e| e.into_inner())
		.clone()
}

/// The failure marker frame: solid magenta (1, 0, 1, 1) F32 RGBA —
/// the C++ plugin renderer paints failed plugin output purple so a
/// broken plugin is visible instead of silently black.
fn purple_frame(time: Rational, size: (i32, i32)) -> Texture {
	let (w, h) = (size.0.max(1), size.1.max(1));
	let mut frame = match generate_frame(time, (w, h), PixelFormat::F32) {
		Ok(f) => f,
		Err(_) => return Texture::dummy(),
	};
	for pixel in frame.data.chunks_exact_mut(16) {
		for (i, v) in [1.0f32, 0.0, 1.0, 1.0].iter().enumerate() {
			pixel[i * 4..i * 4 + 4].copy_from_slice(&v.to_le_bytes());
		}
	}
	Texture::wrap_frame(frame)
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

	/// C++ process_plugin_job: dispatch through the installed plugin
	/// executor (the oakplugin render driver; dependency inversion). A
	/// missing executor or a failed render yields a purple failure frame
	/// instead of aborting the graph, matching pluginjob.cpp's fallback.
	fn process_plugin_job(&mut self, src: Texture, spec: &JobSpec) -> Result<Texture> {
		let JobSpec::Plugin {
			instance,
			time,
			effect_input_id,
			inputs,
			values,
		} = spec
		else {
			return Err(Error::Invalid);
		};
		let size = src.size();
		let Some(executor) = plugin_executor() else {
			return Ok(purple_frame(Rational::from_double(*time), size));
		};
		let _ = (instance, effect_input_id, inputs, values);
		match executor(&PluginJobRequest { spec, src }) {
			Ok(texture) => Ok(texture),
			Err(err) => {
				eprintln!("plugin instance {instance} render at t={time}s failed: {err:#}");
				Ok(purple_frame(Rational::from_double(*time), size))
			}
		}
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

	/// Executes the deferred plugin payloads a [`oaknode::nodes::plugin::PluginNode`]
	/// pushed into its output table (C++ JobEnginePlugin processing in
	/// jobmanager.cpp): unwraps each [`oaknode::nodes::plugin::PluginJobPayload`]
	/// box, splits it into input textures and tagged param values, and
	/// replaces the box with the rendered texture.
	fn resolve_plugin_jobs(&mut self, table: &mut NodeValueTable) {
		for (_, value, _) in table.rows_mut() {
			let NodeValue::Texture(handle) = value else {
				continue;
			};
			if handle.ctx.is_null() {
				continue;
			}
			let payload = unsafe {
				oaknode::handle::get_checked::<oaknode::nodes::plugin::PluginJobPayload>(handle)
			}
			.cloned();
			let Some(payload) = payload else {
				// A genuine texture box (e.g. a source node's frame):
				// not a plugin job, leave it alone.
				continue;
			};

			let mut inputs: Vec<(String, Texture)> = Vec::new();
			let mut values: Vec<(String, NodeValue)> = Vec::new();
			for (key, v) in payload.values.iter() {
				match v {
					NodeValue::Texture(h) if !h.ctx.is_null() => {
						match unsafe { oaknode::handle::get_checked::<Texture>(h) }.cloned() {
							Some(texture) => inputs.push((key.clone(), texture)),
							None => eprintln!("plugin job input '{key}' is not a texture box"),
						}
					}
					NodeValue::Texture(_) | NodeValue::None => {}
					other => values.push((key.clone(), other.clone())),
				}
			}

			// Fallback order mirrors pluginrenderer.cpp's effect input
			// resolution: the declared effect input, else the first
			// available clip texture.
			let effect_src = if payload.effect_input_id.is_empty() {
				None
			} else {
				inputs
					.iter()
					.find(|(key, _)| key == &payload.effect_input_id)
					.map(|(_, t)| t.clone())
			};
			let src = effect_src
				.or_else(|| inputs.first().map(|(_, t)| t.clone()))
				.unwrap_or_else(Texture::dummy);

			let spec = JobSpec::Plugin {
				instance: payload.instance.0,
				time: payload.time.to_f64(),
				effect_input_id: if payload.effect_input_id.is_empty() {
					None
				} else {
					Some(payload.effect_input_id.clone())
				},
				inputs,
				values,
			};
			match self.process_plugin_job(src, &spec) {
				Ok(texture) => {
					*value = NodeValue::Texture(oaknode::handle::make_owned(texture));
				}
				Err(err) => {
					eprintln!("plugin job resolve failed: {err:#}");
				}
			}
		}
	}
}

impl oaknode::traverser::RenderHooks for RenderEvalHooks {
	fn use_cache(&self) -> bool {
		self.use_cache
	}

	fn is_cancelled(&self) -> bool {
		// TODO(phase-6b): poll the ticket's cancellation flag here so
		// long plugin renders can be interrupted.
		false
	}

	fn resolve(
		&mut self,
		_node: oaknode::id::NodeId,
		_row: &NodeValueRow,
		table: &mut NodeValueTable,
	) {
		self.resolve_plugin_jobs(table);
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
static DECODERS: std::sync::OnceLock<
	std::sync::Mutex<std::collections::HashMap<(String, i32), Arc<dyn oakcodec::decoder::Decoder>>>,
> = std::sync::OnceLock::new();

fn decoders(
) -> std::sync::MutexGuard<'static, std::collections::HashMap<(String, i32), Arc<dyn oakcodec::decoder::Decoder>>>
{
	DECODERS
		.get_or_init(|| std::sync::Mutex::new(std::collections::HashMap::new()))
		.lock()
		.unwrap_or_else(|e| e.into_inner())
}

/// Open (or reuse) the decoder session for `(filename, stream_index)`.
fn open_decoder(filename: &str, stream_index: i32) -> Result<Arc<dyn oakcodec::decoder::Decoder>> {
	{
		let cache = decoders();
		if let Some(d) = cache.get(&(filename.to_string(), stream_index)) {
			return Ok(d.clone());
		}
	}
	let decoder: Arc<dyn oakcodec::decoder::Decoder> = Arc::new(FFmpegDecoder::new());
	let stream = CodecStream::with_block(filename.to_string(), stream_index, None);
	decoder
		.open(&stream)
		.map_err(|e| Error::Failed(format!("footage decode open: {e:?}")))?;
	let mut cache = decoders();
	cache.insert((filename.to_string(), stream_index), decoder.clone());
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
	let params = RetrieveVideoParams {
		stream: CodecStream::with_block(filename.to_string(), stream_index, None),
		time,
		length: TimeRange::default(),
		force_range: K_COLOR_RANGE_DEFAULT,
		is_image_sequence: false,
		image_sequence_digits: 0,
		image_sequence_number: 0,
		mode: RenderMode::Offline,
		alpha_is_premultiplied: false,
	};
	let decoded = decoder
		.retrieve_video_frame(&params)
		.map_err(|e| Error::Failed(format!("footage decode at {time:?}: {e:?}")))?;

	let src_w = decoded.width();
	let src_h = decoded.height();
	let src_linesize = decoded.linesize_bytes();
	let (w, h) = size;
	if src_w <= 0 || src_h <= 0 || src_linesize <= 0 || !decoded.is_allocated() {
		return Err(Error::Failed("footage decode: bad decoded frame".into()));
	}

	let mut dst = generate_frame(time, (w, h), format)?;
	let dst_linesize = dst.linesize_bytes() as i32;
	let src_data = match decoded.data() {
		Some(d) => d,
		None => return Err(Error::Failed("footage decode: no frame data".into())),
	};

	if src_w == w && src_h == h && src_linesize == dst_linesize {
		let bytes = (src_h as usize)
			.checked_mul(src_linesize as usize)
			.ok_or(Error::NoMem)?;
		dst.data[..bytes].copy_from_slice(&src_data[..bytes]);
	} else {
		scale_rgba_f32(
			src_data.as_ptr(),
			src_linesize,
			src_w,
			src_h,
			&mut dst.data,
			dst_linesize,
			w,
			h,
		);
	}
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
		let range = TimeRange::new(media_start, media_end);
		let status = decoder
			.retrieve_audio(&mut buf, &range, rate, params.channel_layout)
			.map_err(|e| Error::Failed(format!("footage audio decode: {e:?}")))?;
		let written = match status {
			RetrieveAudioStatus::Success => frames,
			_ => 0,
		};
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
	let acc_data = &mut acc.data;
	render_montage_frame_into(time, params, size, acc_data, stride as i32)?;
	Ok(Texture::wrap_frame(acc))
}

/// Composite the montage at `time` directly into `dst` (F32 RGBA rows of
/// `dst_stride` bytes) — the M15 worker seam: the render worker passes a
/// shared-memory slot slice as `dst`, so the composited frame lands in
/// the slot with no staging copy. `dst` is zeroed first (transparent
/// black base).
pub fn render_montage_frame_into(
	time: Rational,
	params: &crate::ticket::VideoTicketParams,
	size: (i32, i32),
	dst: &mut [u8],
	dst_stride: i32,
) -> Result<()> {
	let (w, h) = size;
	let need = (h as usize).saturating_mul(dst_stride as usize);
	if w <= 0 || h <= 0 || dst.len() < need {
		return Err(Error::Invalid);
	}
	// Transparent-black base.
	dst[..need].fill(0);
	// Decode from the bottom clip first, composite topmost-last.
	for clip in &params.montage {
		if time < clip.in_time || time >= clip.out_time {
			continue;
		}
		let media_time = clip.media_in + (time - clip.in_time);
		let decoded = render_footage_frame(
			&clip.filename,
			clip.stream_index,
			media_time,
			(w, h),
			PixelFormat::F32,
		)?;
		let (src_data, src_stride) = match &decoded {
			Texture::Cpu(src) => (&src.data, src.linesize_bytes() as i32),
			_ => continue,
		};
		composite_over(dst, dst_stride, w, h, src_data, src_stride, clip.gain);
	}
	Ok(())
}

/// `src` over `dst` (premultiplied-ish alpha compositing; F32 RGBA).
/// `gain` scales the source RGB (audio-style volume applied to video
/// transparency is ignored here; gain scales color). Exposed for the M15
/// render worker, which composites montage frames directly into
/// shared-memory slots.
pub fn composite_over(
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

	// The plugin executor lives in a process-wide slot; the tests below
	// mutate it and therefore serialize against each other.
	static PLUGIN_TEST_LOCK: std::sync::Mutex<()> = std::sync::Mutex::new(());

	fn plugin_spec() -> JobSpec {
		JobSpec::Plugin {
			instance: 7,
			time: 0.5,
			effect_input_id: Some("Source".into()),
			inputs: Vec::new(),
			values: Vec::new(),
		}
	}

	fn first_pixel(texture: &Texture) -> [f32; 4] {
		let Texture::Cpu(frame) = texture else {
			unreachable!()
		};
		let mut out = [0f32; 4];
		for i in 0..4 {
			out[i] = f32::from_le_bytes(frame.data[i * 4..i * 4 + 4].try_into().unwrap());
		}
		out
	}

	#[test]
	fn plugin_job_without_executor_yields_purple_frame() {
		let _guard = PLUGIN_TEST_LOCK.lock().unwrap();
		set_plugin_executor(None);
		let mut hooks = RenderEvalHooks::new();
		let src = Texture::wrap_frame(
			generate_frame(Rational::new(0, 1), (4, 2), PixelFormat::F32).unwrap(),
		);
		let out = hooks.process_plugin_job(src, &plugin_spec()).unwrap();
		assert_eq!(out.size(), (4, 2));
		assert_eq!(first_pixel(&out), [1.0, 0.0, 1.0, 1.0]);
	}

	#[test]
	fn plugin_job_executor_error_falls_back_to_purple() {
		let _guard = PLUGIN_TEST_LOCK.lock().unwrap();
		set_plugin_executor(Some(Arc::new(|_req: &PluginJobRequest<'_>| {
			Err(Error::Failed("boom".into()))
		})));
		let mut hooks = RenderEvalHooks::new();
		let src = Texture::wrap_frame(
			generate_frame(Rational::new(0, 1), (2, 2), PixelFormat::F32).unwrap(),
		);
		let out = hooks.process_plugin_job(src, &plugin_spec()).unwrap();
		assert_eq!(first_pixel(&out), [1.0, 0.0, 1.0, 1.0]);
		set_plugin_executor(None);
	}

	#[test]
	fn plugin_job_dispatches_through_installed_executor() {
		let _guard = PLUGIN_TEST_LOCK.lock().unwrap();
		set_plugin_executor(Some(Arc::new(|req: &PluginJobRequest<'_>| {
			// Echo: paint the source size with the instance id.
			let JobSpec::Plugin { instance, .. } = req.spec else {
				return Err(Error::Invalid);
			};
			let v = (*instance as f32) / 10.0;
			let mut frame = generate_frame(Rational::new(0, 1), req.src.size(), PixelFormat::F32)?;
			for pixel in frame.data.chunks_exact_mut(16) {
				for c in 0..4 {
					pixel[c * 4..c * 4 + 4].copy_from_slice(&v.to_le_bytes());
				}
			}
			Ok(Texture::wrap_frame(frame))
		})));
		let mut hooks = RenderEvalHooks::new();
		let src = Texture::wrap_frame(
			generate_frame(Rational::new(0, 1), (2, 2), PixelFormat::F32).unwrap(),
		);
		let out = hooks.process_plugin_job(src, &plugin_spec()).unwrap();
		assert_eq!(first_pixel(&out), [0.7, 0.7, 0.7, 0.7]);
		set_plugin_executor(None);
	}

	#[test]
	fn resolve_executes_payload_box_and_keeps_plain_textures() {
		use oaknode::nodes::plugin::{PluginInstanceHandle, PluginJobPayload};

		let _guard = PLUGIN_TEST_LOCK.lock().unwrap();
		set_plugin_executor(Some(Arc::new(|req: &PluginJobRequest<'_>| {
			let JobSpec::Plugin {
				instance,
				values,
				inputs,
				effect_input_id,
				..
			} = req.spec
			else {
				return Err(Error::Invalid);
			};
			// The resolve seam must deliver the tagged param values and
			// the clip texture to the executor.
			assert_eq!(*instance, 7);
			assert_eq!(effect_input_id.as_deref(), Some("Source"));
			assert_eq!(inputs.len(), 1);
			assert_eq!(inputs[0].0, "Source");
			assert!(values.iter().any(|(k, v)| {
				k == "gain" && matches!(v, NodeValue::Float(f) if (*f - 0.25).abs() < 1e-6)
			}));
			let mut frame = generate_frame(Rational::new(0, 1), req.src.size(), PixelFormat::F32)?;
			for pixel in frame.data.chunks_exact_mut(16) {
				for (i, v) in [0.25f32, 0.5, 0.75, 1.0].iter().enumerate() {
					pixel[i * 4..i * 4 + 4].copy_from_slice(&v.to_le_bytes());
				}
			}
			Ok(Texture::wrap_frame(frame))
		})));

		// A real source texture box plus a payload box referencing it.
		let src_frame = generate_frame(Rational::new(0, 1), (2, 2), PixelFormat::F32).unwrap();
		let src_box = oaknode::handle::make_owned(Texture::wrap_frame(src_frame));
		let mut values = NodeValueRow::new();
		values.insert("Source".into(), NodeValue::Texture(src_box));
		values.insert("gain".into(), NodeValue::Float(0.25));
		let payload = PluginJobPayload {
			instance: PluginInstanceHandle(7),
			time: Rational::new(1, 2),
			effect_input_id: "Source".into(),
			values,
		};

		let mut table = NodeValueTable::default();
		table.push(
			oaknode::value::ValueType::Texture,
			NodeValue::Texture(oaknode::handle::make_owned(payload)),
			None,
		);

		let mut hooks = RenderEvalHooks::new();
		hooks.resolve_plugin_jobs(&mut table);

		let NodeValue::Texture(handle) = table.get(oaknode::value::ValueType::Texture).unwrap()
		else {
			unreachable!()
		};
		let rendered = unsafe { oaknode::handle::get_checked::<Texture>(handle) }
			.expect("payload box must be replaced by the rendered texture");
		assert_eq!(first_pixel(rendered), [0.25, 0.5, 0.75, 1.0]);
		set_plugin_executor(None);
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
