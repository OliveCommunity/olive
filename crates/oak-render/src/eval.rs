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

use oak_codec::decoder::{
	CodecStream, Decoder as _, RenderMode, RetrieveAudioStatus, RetrieveVideoParams,
	K_COLOR_RANGE_DEFAULT,
};
use oak_codec::ffmpeg::FFmpegDecoder;
use oak_core::{PixelFormat, Rational, TimeRange};
use oak_node::value::{NodeValue, NodeValueRow, NodeValueTable};

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

/// Plugin instance factory: resolves an OFX plugin identifier to a live
/// instance-registry id, creating (and caching) the instance lazily.
/// Implemented by the oakplugin crate — the montage effect path carries
/// plugin identifiers, not instance ids (the montage is resolved from
/// the timeline in the main process; the instance lives in whichever
/// process renders the frame).
pub type PluginInstanceFactory = dyn Fn(&str) -> Option<u64> + Send + Sync;

static PLUGIN_INSTANCE_FACTORY: std::sync::OnceLock<
	std::sync::Mutex<Option<Arc<PluginInstanceFactory>>>,
> = std::sync::OnceLock::new();

fn instance_factory_slot() -> &'static std::sync::Mutex<Option<Arc<PluginInstanceFactory>>> {
	PLUGIN_INSTANCE_FACTORY.get_or_init(|| std::sync::Mutex::new(None))
}

/// Install the plugin instance factory (oakplugin registration point;
/// `None` clears it).
pub fn set_plugin_instance_factory(factory: Option<Arc<PluginInstanceFactory>>) {
	*instance_factory_slot().lock().unwrap_or_else(|e| e.into_inner()) = factory;
}

/// The installed plugin instance factory, if any.
pub fn plugin_instance_factory() -> Option<Arc<PluginInstanceFactory>> {
	instance_factory_slot()
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

	/// Executes the deferred plugin payloads a [`oak_node::nodes::plugin::PluginNode`]
	/// pushed into its output table (C++ JobEnginePlugin processing in
	/// jobmanager.cpp): unwraps each [`oak_node::nodes::plugin::PluginJobPayload`]
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
				oak_node::handle::get_checked::<oak_node::nodes::plugin::PluginJobPayload>(handle)
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
						match unsafe { oak_node::handle::get_checked::<Texture>(h) }.cloned() {
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
					*value = NodeValue::Texture(oak_node::handle::make_owned(texture));
				}
				Err(err) => {
					eprintln!("plugin job resolve failed: {err:#}");
				}
			}
		}
	}
}

impl oak_node::traverser::RenderHooks for RenderEvalHooks {
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
		_node: oak_node::id::NodeId,
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
/// one handle across worker threads is safe. The value carries an LRU
/// tick: the map is capped ([`MAX_CACHED_DECODERS`]) because every
/// session pins an FFmpeg context plus up to two native decoded frames
/// (~50 MB at 4K) — before the cap, scrubbing a footage bin grew the
/// map without bound.
static DECODERS: std::sync::OnceLock<
	std::sync::Mutex<
		std::collections::HashMap<(String, i32), (Arc<dyn oak_codec::decoder::Decoder>, u64)>,
	>,
> = std::sync::OnceLock::new();

/// Cap on cached decoder sessions per process (LRU beyond this). 16
/// covers heavy multi-clip montages without reopen thrash; eviction only
/// drops the map entry — an in-flight render keeps its Arc alive and the
/// session dies with the last reference (Drop releases FFmpeg).
const MAX_CACHED_DECODERS: usize = 16;

/// LRU tick source for [`DECODERS`].
static DECODER_TICK: std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(1);

fn decoders(
) -> std::sync::MutexGuard<
	'static,
	std::collections::HashMap<(String, i32), (Arc<dyn oak_codec::decoder::Decoder>, u64)>,
> {
	DECODERS
		.get_or_init(|| std::sync::Mutex::new(std::collections::HashMap::new()))
		.lock()
		.unwrap_or_else(|e| e.into_inner())
}

/// Open (or reuse) the decoder session for `(filename, stream_index)`.
fn open_decoder(filename: &str, stream_index: i32) -> Result<Arc<dyn oak_codec::decoder::Decoder>> {
	let key = (filename.to_string(), stream_index);
	let tick = DECODER_TICK.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
	{
		let mut cache = decoders();
		if let Some((d, t)) = cache.get_mut(&key) {
			*t = tick;
			return Ok(d.clone());
		}
	}
	let decoder: Arc<dyn oak_codec::decoder::Decoder> = Arc::new(FFmpegDecoder::new());
	let stream = CodecStream::with_block(filename.to_string(), stream_index, None);
	decoder
		.open(&stream)
		.map_err(|e| Error::Failed(format!("footage decode open: {e:?}")))?;
	let mut cache = decoders();
	// LRU eviction: the session is dropped here, but an in-flight render
	// holds its own Arc — the FFmpeg context dies with the last reference.
	while cache.len() >= MAX_CACHED_DECODERS {
		let victim = cache
			.iter()
			.min_by_key(|(_, (_, t))| *t)
			.map(|(k, _)| k.clone());
		let Some(victim) = victim else {
			break;
		};
		cache.remove(&victim);
	}
	cache.insert(key, (decoder.clone(), tick));
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
	let (w, h) = size;
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
		// 直接按目标尺寸出帧：swscale 一次完成格式转换 + 缩放，不再
		// 产生全分辨率 F32 中间帧（4K 预览每帧省 ~260MB 瞬时拷贝）。
		target_size: if w > 0 && h > 0 {
			Some((w as u32, h as u32))
		} else {
			None
		},
	};
	let decoded = decoder
		.retrieve_video_frame(&params)
		.map_err(|e| Error::Failed(format!("footage decode at {time:?}: {e:?}")))?;

	let src_w = decoded.width();
	let src_h = decoded.height();
	let src_linesize = decoded.linesize_bytes();
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
	let (rate, layout, channels, total_frames) = audio_layout(params)?;
	let mut acc = vec![0.0f32; total_frames.saturating_mul(channels as usize)];
	mix_audio_montage(params, rate, channels, total_frames, &mut acc)?;
	Ok(crate::ticket::TicketPayload::Audio(crate::ticket::AudioSamples {
		samples: acc,
		sample_rate: rate,
		channel_layout: layout,
		channel_count: channels,
	}))
}

/// The output layout an audio render produces: `(sample_rate,
/// channel_layout, channel_count, total_sample_frames)`.
fn audio_layout(params: &crate::ticket::AudioTicketParams) -> Result<(i32, u64, i32, usize)> {
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
	Ok((rate, params.channel_layout, channels, total_frames))
}

/// The byte length (interleaved f32, little-endian) an audio render of
/// `params` writes into a shm slot — the worker's slot-geometry check
/// (M15 S3). Mirrors [`render_audio_samples_into`]'s layout math.
pub fn audio_samples_byte_len(params: &crate::ticket::AudioTicketParams) -> Result<usize> {
	let (_rate, _layout, channels, total_frames) = audio_layout(params)?;
	Ok(total_frames
		.saturating_mul(channels as usize)
		.saturating_mul(4))
}

/// Mix the audio montage into `acc` (`total_frames * channels` samples,
/// zero-initialized by the caller). Shared by the heap
/// [`render_audio_samples`] and the shm-slot [`render_audio_samples_into`]
/// paths so the decode/mix logic exists once.
fn mix_audio_montage(
	params: &crate::ticket::AudioTicketParams,
	rate: i32,
	channels: i32,
	total_frames: usize,
	acc: &mut [f32],
) -> Result<()> {
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
	Ok(())
}

/// Render the audio montage over `params.range` directly into `dst` as
/// little-endian f32 bytes (M15 S3 worker seam): the render worker passes
/// a shared-memory slot slice as `dst`, so the samples land in the slot
/// with no staging allocation. `dst.len()` must hold
/// `frame_count * channels * 4` bytes.
pub fn render_audio_samples_into(
	params: &crate::ticket::AudioTicketParams,
	dst: &mut [u8],
) -> Result<()> {
	let (rate, _layout, channels, total_frames) = audio_layout(params)?;
	let need = total_frames
		.saturating_mul(channels as usize)
		.saturating_mul(4);
	if dst.len() < need {
		return Err(Error::NoMem);
	}
	let mut acc = vec![0.0f32; total_frames.saturating_mul(channels as usize)];
	mix_audio_montage(params, rate, channels, total_frames, &mut acc)?;
	// Interleaved f32 -> little-endian bytes in the slot.
	for (out, sample) in dst[..need].chunks_exact_mut(4).zip(&acc) {
		out.copy_from_slice(&sample.to_le_bytes());
	}
	Ok(())
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
		// The clip's effect stack runs between decode and compositing
		// (C++ semantics: the clip texture passes through the chain
		// bottom-up, the chain top feeds the track composite).
		let effected = apply_clip_effects(decoded, clip, time);
		let (src_data, src_stride) = match &effected {
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

// ---------------------------------------------------------------------------
// Montage clip effect stacks
// ---------------------------------------------------------------------------

/// The built-in Opacity effect's type id (oaknode `OpacityEffect`) — the
/// one built-in video effect with a CPU evaluator on the montage path.
const OPACITY_EFFECT_TYPE_ID: &str = "org.olivevideoeditor.Olive.opacity";

/// The Opacity effect's value input id (oaknode `opacity_in`).
const OPACITY_VALUE_INPUT: &str = "opacity_in";

/// The effect type ids the montage path already warned about (one log
/// line per type per process instead of one per frame).
fn unsupported_warned() -> std::sync::MutexGuard<'static, std::collections::HashSet<String>> {
	static WARNED: std::sync::OnceLock<std::sync::Mutex<std::collections::HashSet<String>>> =
		std::sync::OnceLock::new();
	WARNED
		.get_or_init(|| std::sync::Mutex::new(std::collections::HashSet::new()))
		.lock()
		.unwrap_or_else(|e| e.into_inner())
}

/// Log an unsupported-effect passthrough once per type id.
fn warn_unsupported_once(type_id: &str, reason: &str) {
	if unsupported_warned().insert(type_id.to_string()) {
		eprintln!("montage effect \"{type_id}\" passes through unchanged: {reason}");
	}
}

/// Run a clip's effect stack over its decoded frame (source-first order;
/// disabled effects are bypassed — the C++ traverser's bypass pushes the
/// effect input through unchanged). Effects the montage path cannot
/// evaluate log a warning once and pass the frame through.
fn apply_clip_effects(
	src: Texture,
	clip: &crate::ticket::MontageClip,
	time: Rational,
) -> Texture {
	let mut tex = src;
	for effect in &clip.effects {
		if !effect.enabled {
			continue;
		}
		tex = apply_montage_effect(tex, effect, time);
	}
	tex
}

/// Apply one effect to `src` (an F32 RGBA CPU frame of the montage
/// pipeline). `time` is the sequence time the frame is rendered at (the
/// C++ node evaluation time).
fn apply_montage_effect(
	src: Texture,
	effect: &crate::ticket::MontageEffect,
	time: Rational,
) -> Texture {
	// Built-in Opacity: multiply every channel by the opacity factor
	// (C++ `:/shaders/opacity.frag`: `frag_color = texture(tex_in, …) *
	// opacity_in` — the shader scales the whole vec4, alpha included).
	if effect.type_id == OPACITY_EFFECT_TYPE_ID {
		let factor = effect
			.params
			.iter()
			.find(|(id, _)| id == OPACITY_VALUE_INPUT)
			.map(|(_, v)| v.to_double())
			.unwrap_or(1.0);
		// Unity is a pass-through (C++ `qFuzzyCompare(opacity, 1.0)`).
		if (factor - 1.0).abs() * 1e12 <= factor.abs().min(1.0) {
			return src;
		}
		// Texture implements Drop, so scale in place through a mutable
		// borrow instead of moving the frame out.
		let mut tex = src;
		match &mut tex {
			Texture::Cpu(frame) => {
				let stride = frame.linesize_bytes();
				for row in frame.data.chunks_exact_mut(stride).take(frame.height.max(0) as usize) {
					for px in row[..(frame.width.max(0) as usize) * 16].chunks_exact_mut(16) {
						for c in px.chunks_exact_mut(4) {
							let v = f32::from_le_bytes(c.try_into().unwrap());
							c.copy_from_slice(&(v * factor as f32).to_le_bytes());
						}
					}
				}
			}
			_ => {
				warn_unsupported_once(
					&effect.type_id,
					"opacity on a non-CPU texture is not supported by the montage path",
				);
			}
		}
		return tex;
	}

	// Everything else: an OFX plugin effect. The montage carries the
	// plugin identifier; the rendering process resolves it to a live
	// instance through the oakplugin-installed factory (lazily created
	// and cached per identifier), then dispatches through the plugin
	// executor exactly like the graph path's plugin jobs.
	let Some(factory) = plugin_instance_factory() else {
		warn_unsupported_once(
			&effect.type_id,
			"no plugin instance factory installed (oakplugin init missing in this process)",
		);
		return src;
	};
	let Some(instance) = factory(&effect.type_id) else {
		warn_unsupported_once(
			&effect.type_id,
			"no evaluator: unknown built-in effect or OFX plugin unavailable in this process",
		);
		return src;
	};
	let spec = JobSpec::Plugin {
		instance,
		time: time.to_f64(),
		effect_input_id: effect.effect_input_id.clone(),
		inputs: Vec::new(),
		values: effect.params.clone(),
	};
	let size = src.size();
	match RenderEvalHooks::new().process_plugin_job(src, &spec) {
		Ok(texture) => texture,
		Err(err) => {
			// Unreachable for a Plugin spec (the executor failure path
			// yields a purple frame); stay loud rather than silent.
			eprintln!("montage plugin job failed to dispatch: {err:#}");
			purple_frame(time, size)
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
		use oak_node::nodes::plugin::{PluginInstanceHandle, PluginJobPayload};

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
		let src_box = oak_node::handle::make_owned(Texture::wrap_frame(src_frame));
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
			oak_node::value::ValueType::Texture,
			NodeValue::Texture(oak_node::handle::make_owned(payload)),
			None,
		);

		let mut hooks = RenderEvalHooks::new();
		hooks.resolve_plugin_jobs(&mut table);

		let NodeValue::Texture(handle) = table.get(oak_node::value::ValueType::Texture).unwrap()
		else {
			unreachable!()
		};
		let rendered = unsafe { oak_node::handle::get_checked::<Texture>(handle) }
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

	// ---- Audio (M12 P1 / M15 S3) -----------------------------------------

	fn audio_params(range: TimeRange) -> crate::ticket::AudioTicketParams {
		crate::ticket::AudioTicketParams {
			viewer: 1,
			range,
			sample_rate: 48000,
			channel_layout: 0x3,
			montage: Vec::new(),
		}
	}

	#[test]
	fn render_audio_samples_produces_silence_for_empty_montage() {
		// M12 P1: an empty montage renders total silence at the requested
		// layout.
		let params = audio_params(TimeRange::new(Rational::new(0, 1), Rational::new(1, 24)));
		match render_audio_samples(&params).unwrap() {
			crate::ticket::TicketPayload::Audio(samples) => {
				// 1/24 s at 48 kHz = 2000 sample frames, stereo.
				assert_eq!(samples.sample_rate, 48000);
				assert_eq!(samples.channel_count, 2);
				assert_eq!(samples.samples.len(), 2000 * 2);
				assert!(samples.samples.iter().all(|&v| v == 0.0), "silence");
			}
			other => panic!("expected Audio payload, got {other:?}"),
		}
	}

	#[test]
	fn render_audio_samples_into_matches_heap_path_byte_for_byte() {
		// M15 S3: the shm-slot writer must produce exactly the same
		// little-endian f32 bytes as the heap path, so a worker's slot and
		// the in-process fallback agree for the same montage.
		let params = audio_params(TimeRange::new(Rational::new(0, 1), Rational::new(1, 48)));
		let heap = match render_audio_samples(&params).unwrap() {
			crate::ticket::TicketPayload::Audio(samples) => samples,
			other => panic!("expected Audio payload, got {other:?}"),
		};
		let mut dst = vec![0u8; heap.samples.len() * 4];
		render_audio_samples_into(&params, &mut dst).unwrap();
		let expected: Vec<u8> = heap
			.samples
			.iter()
			.flat_map(|v| v.to_le_bytes())
			.collect();
		assert_eq!(dst, expected);
		// And the into-path output parses back into the same samples.
		let parsed: Vec<f32> = dst
			.chunks_exact(4)
			.map(|c| f32::from_le_bytes([c[0], c[1], c[2], c[3]]))
			.collect();
		assert_eq!(parsed, heap.samples);
	}

	#[test]
	fn render_audio_samples_into_rejects_small_buffer() {
		let params = audio_params(TimeRange::new(Rational::new(0, 1), Rational::new(1, 24)));
		let mut dst = [0u8; 8]; // far too small for 2000x2 f32 samples
		assert!(render_audio_samples_into(&params, &mut dst).is_err());
	}

	// ---- Montage clip effect stacks -------------------------------------

	/// A 2x1 F32 texture filled with a known color.
	fn solid_texture(r: f32, g: f32, b: f32, a: f32) -> Texture {
		let mut frame = generate_frame(Rational::new(0, 1), (2, 1), PixelFormat::F32).unwrap();
		for px in frame.data.chunks_exact_mut(16) {
			for (c, v) in px.chunks_exact_mut(4).zip([r, g, b, a]) {
				c.copy_from_slice(&v.to_le_bytes());
			}
		}
		Texture::Cpu(frame)
	}

	fn opacity_effect(enabled: bool, value: f64) -> crate::ticket::MontageEffect {
		crate::ticket::MontageEffect {
			type_id: OPACITY_EFFECT_TYPE_ID.to_string(),
			enabled,
			effect_input_id: Some("tex_in".to_string()),
			params: vec![(OPACITY_VALUE_INPUT.to_string(), NodeValue::Float(value))],
		}
	}

	fn clip_with_effects(effects: Vec<crate::ticket::MontageEffect>) -> crate::ticket::MontageClip {
		crate::ticket::MontageClip {
			filename: String::new(),
			stream_index: 0,
			in_time: Rational::new(0, 1),
			out_time: Rational::new(1, 1),
			media_in: Rational::new(0, 1),
			gain: 1.0,
			effects,
		}
	}

	/// The built-in Opacity effect really scales the decoded pixels
	/// (every channel, C++ `opacity.frag` parity).
	#[test]
	fn montage_opacity_effect_scales_pixels() {
		let clip = clip_with_effects(vec![opacity_effect(true, 0.5)]);
		let out = apply_clip_effects(solid_texture(0.8, 0.4, 0.2, 1.0), &clip, Rational::new(0, 1));
		assert_eq!(first_pixel(&out), [0.4, 0.2, 0.1, 0.5]);
	}

	/// Disabled effects are bypassed (C++ traverser parity), and unity
	/// opacity is a pass-through.
	#[test]
	fn montage_disabled_or_unity_effects_pass_through() {
		let disabled = clip_with_effects(vec![opacity_effect(false, 0.5)]);
		let out = apply_clip_effects(solid_texture(0.8, 0.4, 0.2, 1.0), &disabled, Rational::new(0, 1));
		assert_eq!(first_pixel(&out), [0.8, 0.4, 0.2, 1.0]);

		let unity = clip_with_effects(vec![opacity_effect(true, 1.0)]);
		let out = apply_clip_effects(solid_texture(0.8, 0.4, 0.2, 1.0), &unity, Rational::new(0, 1));
		assert_eq!(first_pixel(&out), [0.8, 0.4, 0.2, 1.0]);
	}

	/// An OFX effect (any non-built-in type id) resolves its instance
	/// through the installed factory and dispatches through the executor,
	/// carrying the montage's parameter values.
	#[test]
	fn montage_plugin_effect_dispatches_with_params() {
		let _guard = PLUGIN_TEST_LOCK.lock().unwrap();
		set_plugin_instance_factory(Some(Arc::new(|identifier: &str| {
			(identifier == "com.example.darken").then_some(7)
		})));
		set_plugin_executor(Some(Arc::new(|req: &PluginJobRequest<'_>| {
			let JobSpec::Plugin { instance, values, .. } = req.spec else {
				return Err(Error::Invalid);
			};
			assert_eq!(*instance, 7);
			// The injected parameter drives the output: paint the frame
			// with the "gain" value so the test observes the param path.
			let gain = values
				.iter()
				.find(|(k, _)| k == "gain")
				.map(|(_, v)| v.to_double() as f32)
				.unwrap_or(1.0);
			let mut frame = generate_frame(Rational::new(0, 1), req.src.size(), PixelFormat::F32)?;
			for px in frame.data.chunks_exact_mut(16) {
				for (c, v) in px.chunks_exact_mut(4).zip([gain, gain, gain, 1.0]) {
					c.copy_from_slice(&v.to_le_bytes());
				}
			}
			Ok(Texture::Cpu(frame))
		})));
		let clip = clip_with_effects(vec![crate::ticket::MontageEffect {
			type_id: "com.example.darken".to_string(),
			enabled: true,
			effect_input_id: Some("Source".to_string()),
			params: vec![("gain".to_string(), NodeValue::Float(0.25))],
		}]);
		let out = apply_clip_effects(solid_texture(0.8, 0.4, 0.2, 1.0), &clip, Rational::new(0, 1));
		assert_eq!(first_pixel(&out), [0.25, 0.25, 0.25, 1.0]);
		set_plugin_executor(None);
		set_plugin_instance_factory(None);
	}

	/// An effect nobody can evaluate (no factory / unknown type) passes
	/// the frame through unchanged — loudly (the warn-once log), never a
	/// silent no-op.
	#[test]
	fn montage_unknown_effect_passes_through() {
		let _guard = PLUGIN_TEST_LOCK.lock().unwrap();
		set_plugin_instance_factory(None);
		let clip = clip_with_effects(vec![crate::ticket::MontageEffect {
			type_id: "com.example.missing".to_string(),
			enabled: true,
			effect_input_id: Some("Source".to_string()),
			params: Vec::new(),
		}]);
		let out = apply_clip_effects(solid_texture(0.8, 0.4, 0.2, 1.0), &clip, Rational::new(0, 1));
		assert_eq!(first_pixel(&out), [0.8, 0.4, 0.2, 1.0]);
	}
}
