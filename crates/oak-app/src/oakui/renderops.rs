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

//! Montage resolution, ticket rendering and the export driver (M14 R3).
//!
//! The facade's renderer box (`crates/oakengine/src/render.rs`: geometry
//! validation, `build_video_montage` / `build_audio_montage`,
//! `clip_media`) and its export path (`oakengine_task_create_export` +
//! `oakengine_task_start_sync` + the task subscription) are app-side now:
//! the montage builders read the oaknode graph directly and the renders
//! go through the oakrender ticket arena, exactly like the CLI's
//! `engine.rs` (M14 R2). The export drives `oak_task::export::ExportTask`
//! synchronously on a background thread with the module task's event
//! listener and cancel atom wired to the app's [`ExportSession`].

use std::sync::mpsc;

use gpui::RenderImage;
use oak_core::{Rational, TimeRange};
use oak_node::id::NodeId;
use oak_node::track::TrackType;
use oak_render::manager::RenderManager;
use oak_render::procpool::ShmFrameRef;
use oak_render::texture::Texture;
use oak_render::ticket::{AudioTicketParams, MontageClip, TicketPayload, VideoTicketParams};

use super::engine::{ExportEvent, ExportSession};
use super::frames::{bgra_bytes_to_render_image, f32_rgba_to_bgra_image};
use super::graphops::{
	clip_behavior, lock, sequence_behavior, track_behavior, track_list_behavior, ProjectRef,
};
use super::scopes::{analyze_bgra8, analyze_f32_rgba, ScopeData};

/// The pixel format the viewers render in (`oak_core::PixelFormat::F32`,
/// the pipeline's internal format; the app downconverts to BGRA itself).
pub const PIXEL_FORMAT_F32: i32 = 4;

/// The BGRA8 slot wire format the process backend renders into (M15 S2):
/// the worker converts its F32 pipeline output to BGRA8 at the end of the
/// render, so the main process reads display-ready bytes from the slot.
pub const SLOT_FORMAT_BGRA8: i32 = oak_render::ipc::SLOT_FORMAT_BGRA8;

// ---------------------------------------------------------------------------
// Render manager
// ---------------------------------------------------------------------------

/// Bring up the process-wide render manager if it is not running yet
/// (without it the ticket arena rejects submissions). Returns false when
/// the manager could not be started.
pub fn ensure_render_manager() -> bool {
	if RenderManager::global().is_some() {
		return true;
	}
	// Already-initialized is success (the module reports State for a
	// second init).
	let _ = RenderManager::init();
	RenderManager::global().is_some()
}

// ---------------------------------------------------------------------------
// Montage resolution (the facade's build_video_montage / build_audio_montage
// over the module graph)
// ---------------------------------------------------------------------------

/// Whether preview decoding may substitute proxy media: the global
/// `UseProxyMedia` config switch (C++ `Tools > Use Proxy Media`; the
/// export path never consults it — exports always decode the original).
pub fn use_proxy_media() -> bool {
	oak_common::configstore::ConfigStore::instance().get_bool(None, "UseProxyMedia", 1) != 0
}

/// The preview media of a footage node with the three-level proxy switch
/// applied (C++ `Footage::value` proxy arms + `FootageJob::should_use_proxy`):
/// the global switch, the footage's proxy-enabled flag and an on-disk
/// `Ready` proxy all have to line up, otherwise the original is returned.
/// Video decodes from the proxy's stream 0 (the proxy's only video
/// stream); audio from stream 1 when the proxy was generated with audio
/// (the first source audio stream's rank + 1). A missing proxy file falls
/// back to the original.
pub fn preview_footage_media(
	f: &oak_node::footage::FootageBehavior,
	is_video: bool,
) -> (String, i32) {
	// The original media decodes from the footage's actual first stream of
	// the kind (C++ maps per-stream); the 0/1 fallbacks cover footage
	// whose streams were never probed (legacy project files).
	let original_stream = f
		.streams
		.iter()
		.find(|s| s.is_video == is_video)
		.map(|s| s.index)
		.unwrap_or(if is_video { 0 } else { 1 });
	let original = (f.filename.clone(), original_stream);
	if !use_proxy_media() || !f.proxy_enabled || f.proxy.is_empty() {
		return original;
	}
	if oak_codec::proxymanager::ProxyManager::get_proxy_state(&f.proxy)
		!= oak_codec::proxymanager::ProxyState::Ready
	{
		return original;
	}
	if is_video {
		// The proxy stands in for the footage's first video stream only
		// (C++ matches proxy_video_stream_index against the stream index).
		let first_video = f.streams.iter().find(|s| s.is_video);
		match first_video {
			Some(stream) if f.proxy_video_stream_index == stream.index => (f.proxy.clone(), 0),
			_ => original,
		}
	} else if oak_codec::proxymanager::ProxyManager::proxy_filename_has_audio(&f.proxy) {
		(f.proxy.clone(), 1)
	} else {
		original
	}
}

/// The preview media feeding a clip: the clip's footage with the proxy
/// switch applied ([`preview_footage_media`]).
fn clip_preview_media(
	g: &oak_node::graph::Graph,
	block_id: NodeId,
	is_video: bool,
) -> Option<(String, i32)> {
	let footage_id = super::graphops::find_input_footage(g, block_id)?;
	let f = super::graphops::footage_behavior(g, footage_id)?;
	if f.filename.is_empty() {
		return None;
	}
	Some(preview_footage_media(f, is_video))
}

/// The clip block's effect stack as montage effect descriptors
/// (source-first order — the chain walk's signal order, so the renderer
/// applies them media-side first). The chain walk runs all the way to
/// the media source, so its first element is the footage node feeding
/// the clip; the montage decodes that footage itself, so the source
/// node (the one WITHOUT an effect input) is dropped here. Disabled
/// effects are carried with `enabled = false`; the renderer bypasses
/// them (the C++ traverser's bypass pushes the effect input through
/// unchanged). Parameters are the inspector's parameter set (non-hidden,
/// non-connection inputs at their standard values; keyframed values are
/// not time-resolved on this path).
fn clip_effects(g: &oak_node::graph::Graph, block_id: NodeId) -> Vec<oak_render::ticket::MontageEffect> {
	use super::effectchain;
	effectchain::chain(g, block_id)
		.into_iter()
		.filter(|&fx| effectchain::effect_input_of(g, fx).is_some())
		.filter_map(|fx| {
			let entry = g.get(fx)?;
			Some(oak_render::ticket::MontageEffect {
				type_id: entry.behavior.type_id().to_string(),
				enabled: effectchain::is_enabled(g, fx),
				effect_input_id: effectchain::effect_input_of(g, fx),
				params: effectchain::effect_params(g, fx)
					.unwrap_or_default()
					.into_iter()
					.map(|p| (p.input_id, p.value))
					.collect(),
			})
		})
		.collect()
}

/// The video montage at sequence time `time`: every clip covering `time`
/// on video tracks, ordered bottom-to-top (track index 0 is topmost, so
/// it is composited last). Hidden tracks (the muted flag doubles as the
/// video visibility toggle, Olive parity) contribute nothing.
pub fn video_montage(p: &ProjectRef, seq: NodeId, time: Rational) -> Vec<MontageClip> {
	let g = lock(p);
	let mut clips = Vec::new();
	let Some(s) = sequence_behavior(&g.graph, seq) else {
		return clips;
	};
	for &list_id in &s.track_lists {
		let Some(list) = track_list_behavior(&g.graph, list_id) else {
			continue;
		};
		if list.kind != TrackType::Video {
			continue;
		}
		for &track_id in list.tracks.iter().rev() {
			let Some(track) = track_behavior(&g.graph, track_id) else {
				continue;
			};
			if track.muted {
				continue;
			}
			for &block_id in &track.blocks {
				let Some(clip) = clip_behavior(&g.graph, block_id) else {
					continue;
				};
				let in_ = clip.core.in_();
				let out = clip.core.out();
				if time < in_ || time >= out {
					continue;
				}
				let Some((filename, stream_index)) =
					clip_preview_media(&g.graph, block_id, true)
				else {
					continue;
				};
				clips.push(MontageClip {
					filename,
					stream_index,
					in_time: in_,
					out_time: out,
					media_in: clip.core.media_in,
					gain: 1.0,
					effects: clip_effects(&g.graph, block_id),
				});
			}
		}
	}
	clips
}

/// The video montage at sequence time `time` containing ONLY the clip on
/// `track` (a track of the sequence's video track list), if any covers
/// `time`. This is the multicam angle render: each angle is the source
/// sequence's track `i` at the playhead, so the montage carries just that
/// track's clip instead of the whole stack.
pub fn single_track_video_montage(
	p: &ProjectRef,
	seq: NodeId,
	track: NodeId,
	time: Rational,
) -> Vec<MontageClip> {
	let g = lock(p);
	let mut clips = Vec::new();
	let Some(s) = sequence_behavior(&g.graph, seq) else {
		return clips;
	};
	// The track must belong to the sequence's video track list.
	let in_list = s.track_lists.iter().any(|&list_id| {
		track_list_behavior(&g.graph, list_id)
			.map(|l| l.kind == TrackType::Video && l.tracks.contains(&track))
			.unwrap_or(false)
	});
	if !in_list {
		return clips;
	}
	let Some(track) = track_behavior(&g.graph, track) else {
		return clips;
	};
	if track.muted {
		return clips;
	}
	for &block_id in &track.blocks {
		let Some(clip) = clip_behavior(&g.graph, block_id) else {
			continue;
		};
		let in_ = clip.core.in_();
		let out = clip.core.out();
		if time < in_ || time >= out {
			continue;
		}
		let Some((filename, stream_index)) = clip_preview_media(&g.graph, block_id, true) else {
			continue;
		};
		clips.push(MontageClip {
			filename,
			stream_index,
			in_time: in_,
			out_time: out,
			media_in: clip.core.media_in,
			gain: 1.0,
			effects: clip_effects(&g.graph, block_id),
		});
	}
	clips
}

/// Build the video ticket params for one multicam angle: the clip on
/// `track` (a video track of `seq`, the multicam's source sequence) at the
/// playhead timestamp `frame_ts`.
pub fn multicam_angle_frame_params(
	p: &ProjectRef,
	seq: NodeId,
	track: NodeId,
	frame_ts: i64,
	tb: (i64, i64),
	width: i32,
	height: i32,
) -> Result<VideoTicketParams, String> {
	validate_geometry(width, height, tb)?;
	let time = Rational::new(frame_ts * tb.0, tb.1);
	Ok(VideoTicketParams {
		viewer: seq.identity(),
		time,
		force_size: Some((width, height)),
		force_format: None,
		cache: None,
		cache_dir: None,
		cache_id: None,
		cache_timebase: None,
		footage: None,
		montage: single_track_video_montage(p, seq, track, time),
	})
}

/// Render one multicam angle frame (the clip on `track` of the source
/// sequence at `frame_ts`) into a `(width, height)` frame.
pub fn render_multicam_angle_frame(
	p: &ProjectRef,
	seq: NodeId,
	track: NodeId,
	frame_ts: i64,
	tb: (i64, i64),
	width: i32,
	height: i32,
) -> Result<RenderedFrame, String> {
	render_video(multicam_angle_frame_params(p, seq, track, frame_ts, tb, width, height)?)
}

/// The audio montage over `range`: every audio clip overlapping the
/// range, media times resolved from the clip ranges, audio stream 1.
/// Muted tracks are silenced (skipped entirely).
pub fn audio_montage(p: &ProjectRef, seq: NodeId, range: TimeRange) -> Vec<MontageClip> {
	let g = lock(p);
	let mut clips = Vec::new();
	let Some(s) = sequence_behavior(&g.graph, seq) else {
		return clips;
	};
	for &list_id in &s.track_lists {
		let Some(list) = track_list_behavior(&g.graph, list_id) else {
			continue;
		};
		if list.kind != TrackType::Audio {
			continue;
		}
		for &track_id in &list.tracks {
			let Some(track) = track_behavior(&g.graph, track_id) else {
				continue;
			};
			if track.muted {
				continue;
			}
			for &block_id in &track.blocks {
				let Some(clip) = clip_behavior(&g.graph, block_id) else {
					continue;
				};
				let in_ = clip.core.in_();
				let out = clip.core.out();
				if out <= range.in_() || in_ >= range.out() {
					continue;
				}
				let Some((filename, stream_index)) =
					clip_preview_media(&g.graph, block_id, false)
				else {
					continue;
				};
				clips.push(MontageClip {
					filename,
					stream_index,
					in_time: in_,
					out_time: out,
					media_in: clip.core.media_in,
					gain: 1.0,
					// Audio clips: the effect stack is a video-side
					// concept; the mixer never consults it.
					effects: Vec::new(),
				});
			}
		}
	}
	clips
}

// ---------------------------------------------------------------------------
// Ticket rendering
// ---------------------------------------------------------------------------

/// A rendered frame's pixel payload (M15 S2): a process-backend shm slot
/// (BGRA8, zero-copy read) or an in-process F32 CPU frame (the test-only
/// inline backend).
pub enum RenderedFrame {
	/// Process backend: a BGRA8 frame in a worker's shared-memory slot.
	/// Read with `shm.slot_bytes(slot)` (no counted copy on the preview
	/// path), build the display image, then release the slot through the
	/// render manager.
	Shm(ShmFrameRef),
	/// In-process (test) backend: an F32 RGBA CPU frame.
	CpuF32 {
		/// Width in pixels.
		width: i32,
		/// Height in pixels.
		height: i32,
		/// Bytes per scanline (stride).
		linesize: i32,
		/// Pixel data (at least `linesize * height` bytes).
		data: Vec<u8>,
	},
}

impl RenderedFrame {
	/// Width in pixels.
	pub fn width(&self) -> i32 {
		match self {
			RenderedFrame::Shm(f) => f.meta.width,
			RenderedFrame::CpuF32 { width, .. } => *width,
		}
	}

	/// Height in pixels.
	pub fn height(&self) -> i32 {
		match self {
			RenderedFrame::Shm(f) => f.meta.height,
			RenderedFrame::CpuF32 { height, .. } => *height,
		}
	}

	/// The pixel format: the BGRA8 slot wire format for the shm variant,
	/// `PIXEL_FORMAT_F32` for the in-process variant.
	pub fn format(&self) -> i32 {
		match self {
			RenderedFrame::Shm(_) => SLOT_FORMAT_BGRA8,
			RenderedFrame::CpuF32 { .. } => PIXEL_FORMAT_F32,
		}
	}

	/// True for the process-backend shm variant.
	pub fn is_shm(&self) -> bool {
		matches!(self, RenderedFrame::Shm(_))
	}

	/// Build the viewer display image plus the scope samples (M15 S2
	/// zero-copy onscreen path). For the shm variant the slot's BGRA8
	/// bytes are wrapped into the display buffer — the GPU-upload staging
	/// copy, the single permitted main-process copy on the preview path
	/// (design §3.5). The display color transform (display ICC) is applied
	/// in place on that staging copy / on the F32 samples, so it costs no
	/// extra copy. The caller releases the slot afterwards.
	pub fn to_display(&self) -> Option<(RenderImage, ScopeData)> {
		match self {
			RenderedFrame::Shm(f) => {
				let meta = &f.meta;
				let (w, h) = (meta.width.max(0) as u32, meta.height.max(0) as u32);
				let pixels = f.shm.slot_bytes(f.slot);
				let data = pixels.get(..meta.data_size.max(0) as usize)?;
				let scope = analyze_bgra8(w, h, data);
				// The display transform edits the staging copy in place.
				let mut owned = data.to_vec();
				super::displaycolor::apply_bgra8(&mut owned, (w * h) as i64);
				let image = bgra_bytes_to_render_image(w, h, &owned)?;
				Some((image, scope))
			}
			RenderedFrame::CpuF32 {
				width,
				height,
				linesize,
				data,
			} => {
				let (w, h) = ((*width).max(0) as u32, (*height).max(0) as u32);
				let mut samples = repack_f32_rows(*width, *height, *linesize, data)?;
				let scope = analyze_f32_rgba(w, h, &samples);
				super::displaycolor::apply_f32_rgba(&mut samples, (w * h) as i64);
				Some((
					f32_rgba_to_bgra_image(w, h, &samples),
					scope,
				))
			}
		}
	}
}

/// Repack one F32 RGBA rendered frame (rows padded to `linesize`) into
/// tightly packed samples. Returns `(width, height, samples)`-style
/// samples only; geometry is validated by the caller.
fn repack_f32_rows(width: i32, height: i32, linesize: i32, data: &[u8]) -> Option<Vec<f32>> {
	if width <= 0 || height <= 0 {
		return None;
	}
	let row_bytes = (width * 4 * 4) as usize;
	let linesize = (linesize as usize).max(row_bytes);
	if data.len() < linesize * height as usize {
		return None;
	}
	let mut samples = vec![0.0f32; (width * height * 4) as usize];
	for y in 0..height as usize {
		let row = &data[y * linesize..y * linesize + row_bytes];
		for (i, px) in row.chunks_exact(4).enumerate() {
			let v = f32::from_ne_bytes([px[0], px[1], px[2], px[3]]);
			samples[y * (width as usize) * 4 + i] = v;
		}
	}
	Some(samples)
}

/// The renderer geometry / format validation (the facade's
/// `make_renderer_box` argument checks).
fn validate_geometry(width: i32, height: i32, tb: (i64, i64)) -> Result<(), String> {
	if width <= 0 || height <= 0 || tb.0 <= 0 || tb.1 <= 0 {
		return Err("invalid render geometry or frame rate".to_string());
	}
	Ok(())
}

/// Drive one video ticket synchronously (M15 S2: returns the payload
/// variant without copying frame bytes — the shm slot is read zero-copy
/// by the caller and released after building the display image).
fn render_video(params: VideoTicketParams) -> Result<RenderedFrame, String> {
	let m = RenderManager::global().ok_or_else(|| "render manager is not initialized".to_string())?;
	let id = m.tickets.next_id();
	if std::env::var_os("OAK_DEBUG_DISPATCH").is_some() {
		eprintln!("renderops: sync render submit arena ticket {}", id.0);
	}
	m.tickets.submit_video_with_id(id, params, Box::new(|_| {}));
	m.tickets.wait(id).map_err(|e| e.to_string())?;
	if std::env::var_os("OAK_DEBUG_DISPATCH").is_some() {
		eprintln!("renderops: sync render wait done arena ticket {}", id.0);
	}
	let result = m
		.tickets
		.result(id)
		.ok_or_else(|| "render ticket produced no result".to_string())?;
	match &result {
		Ok(TicketPayload::Video(Texture::Cpu(frame))) => Ok(RenderedFrame::CpuF32 {
			width: frame.width,
			height: frame.height,
			linesize: frame.linesize_bytes() as i32,
			data: frame.data.clone(),
		}),
		Ok(TicketPayload::ShmFrame(frame)) => Ok(RenderedFrame::Shm(frame.clone())),
		Ok(TicketPayload::Video(_)) => Err("render produced a non-CPU frame".to_string()),
		_ => Err("render produced no video frame".to_string()),
	}
}

/// Build the video ticket params for one sequence-montage frame (M15 S2:
/// shared by the synchronous render and the playback pre-render window).
pub fn sequence_frame_params(
	p: &ProjectRef,
	seq: NodeId,
	frame_ts: i64,
	tb: (i64, i64),
	width: i32,
	height: i32,
) -> Result<VideoTicketParams, String> {
	validate_geometry(width, height, tb)?;
	let time = Rational::new(frame_ts * tb.0, tb.1);
	Ok(VideoTicketParams {
		viewer: seq.identity(),
		time,
		force_size: Some((width, height)),
		force_format: None,
		cache: None,
		cache_dir: None,
		cache_id: None,
		cache_timebase: None,
		footage: None,
		montage: video_montage(p, seq, time),
	})
}

/// Render one frame of the sequence's montage at `frame_ts` (a timestamp
/// in the `(tb.1 / tb.0)`-per-frame timebase) into a `(width, height)`
/// frame.
pub fn render_sequence_frame(
	p: &ProjectRef,
	seq: NodeId,
	frame_ts: i64,
	tb: (i64, i64),
	width: i32,
	height: i32,
) -> Result<RenderedFrame, String> {
	render_video(sequence_frame_params(p, seq, frame_ts, tb, width, height)?)
}

/// Build the video ticket params for one single-footage frame (M15 S2:
/// shared by the synchronous render and the source-monitor pre-render
/// window).
pub fn footage_frame_params(
	p: &ProjectRef,
	footage: NodeId,
	frame_ts: i64,
	tb: (i64, i64),
	width: i32,
	height: i32,
) -> Result<VideoTicketParams, String> {
	validate_geometry(width, height, tb)?;
	let (filename, stream_index) = {
		let g = lock(p);
		super::graphops::footage_behavior(&g.graph, footage)
			.map(|f| preview_footage_media(f, true))
			.ok_or_else(|| "the node is not footage".to_string())?
	};
	let time = Rational::new(frame_ts * tb.0, tb.1);
	Ok(VideoTicketParams {
		viewer: footage.identity(),
		time,
		force_size: Some((width, height)),
		force_format: None,
		cache: None,
		cache_dir: None,
		cache_id: None,
		cache_timebase: None,
		footage: Some((filename, stream_index)),
		montage: Vec::new(),
	})
}

/// Render one frame of a single footage node (the source monitor) at
/// `frame_ts`, decoded straight from the media file.
pub fn render_footage_frame(
	p: &ProjectRef,
	footage: NodeId,
	frame_ts: i64,
	tb: (i64, i64),
	width: i32,
	height: i32,
) -> Result<RenderedFrame, String> {
	render_video(footage_frame_params(p, footage, frame_ts, tb, width, height)?)
}

/// Rendered interleaved f32 audio (the module audio ticket payload).
pub struct RenderedAudio {
	/// Interleaved samples (`frame_count * channel_count` values).
	pub data: Vec<f32>,
	/// Sample rate (Hz).
	pub sample_rate: i32,
	/// Channel count.
	pub channel_count: i32,
}

/// Render the audio range `[start_ts, start_ts + len_ts)` (frame
/// timestamps in the `(tb.1 / tb.0)`-per-frame timebase) as interleaved
/// f32 at 48 kHz stereo (the facade's `oakengine_renderer_render_audio`
/// defaults; uncovered parts are silent).
pub fn render_audio_range(
	p: &ProjectRef,
	seq: NodeId,
	start_ts: i64,
	len_ts: i64,
	tb: (i64, i64),
) -> Result<RenderedAudio, String> {
	if tb.0 <= 0 || tb.1 <= 0 {
		return Err("the sequence has no valid frame rate".to_string());
	}
	let range = TimeRange::new(
		Rational::new(start_ts * tb.0, tb.1),
		Rational::new((start_ts + len_ts) * tb.0, tb.1),
	);
	let montage = audio_montage(p, seq, range);
	let m = RenderManager::global().ok_or_else(|| "render manager is not initialized".to_string())?;
	let id = m.tickets.next_id();
	m.tickets.submit_audio_with_id(
		id,
		AudioTicketParams {
			viewer: seq.identity(),
			range,
			sample_rate: 48000,
			channel_layout: 0x3,
			montage,
		},
		Box::new(|_| {}),
	);
	m.tickets.wait(id).map_err(|e| e.to_string())?;
	let result = m
		.tickets
		.result(id)
		.ok_or_else(|| "audio ticket produced no result".to_string())?;
	match result {
		Ok(TicketPayload::Audio(samples)) => Ok(RenderedAudio {
			data: samples.samples,
			sample_rate: samples.sample_rate,
			channel_count: samples.channel_count,
		}),
		// M15 S3 process backend: the audio sits in a worker shm slot; copy
		// the samples out and release the slot (the bytes must outlive it).
		Ok(TicketPayload::ShmAudio(audio)) => {
			let samples = audio.to_audio_samples();
			if let Some(m) = RenderManager::global() {
				m.release_audio_frame(&audio);
			}
			Ok(RenderedAudio {
				data: samples.samples,
				sample_rate: samples.sample_rate,
				channel_count: samples.channel_count,
			})
		}
		_ => Err("audio render produced no samples".to_string()),
	}
}

/// Submit one audio-chunk render for the async playback prefetch (M15
/// S3): the chunk `[start_ts, start_ts + len_ts)` is rendered through the
/// process dispatcher; the completion copies the samples out of the shm
/// slot, releases it and sends `(start_ts, samples)` on `tx`. Render
/// errors send silence of the expected length so the playback buffer stays
/// aligned (the real-time path must never stall the UI thread on a worker).
pub fn submit_audio_chunk(
	p: &ProjectRef,
	seq: NodeId,
	start_ts: i64,
	len_ts: i64,
	tb: (i64, i64),
	tx: mpsc::Sender<(i64, RenderedAudio)>,
) -> Result<(), String> {
	let range = TimeRange::new(
		Rational::new(start_ts * tb.0, tb.1),
		Rational::new((start_ts + len_ts) * tb.0, tb.1),
	);
	let montage = audio_montage(p, seq, range);
	let sample_rate = 48000;
	let channel_layout = 0x3u64;
	let channels = channel_layout.count_ones().max(1) as i32;
	let m = RenderManager::global().ok_or_else(|| "render manager is not initialized".to_string())?;
	let id = m.tickets.next_id();
	m.tickets.submit_audio_with_id(
		id,
		AudioTicketParams {
			viewer: seq.identity(),
			range,
			sample_rate,
			channel_layout,
			montage,
		},
		Box::new(move |result| {
			let data = match result {
				Ok(TicketPayload::Audio(samples)) => RenderedAudio {
					data: samples.samples,
					sample_rate: samples.sample_rate,
					channel_count: samples.channel_count,
				},
				Ok(TicketPayload::ShmAudio(audio)) => {
					let samples = audio.to_audio_samples();
					if let Some(m) = RenderManager::global() {
						m.release_audio_frame(&audio);
					}
					RenderedAudio {
						data: samples.samples,
						sample_rate: samples.sample_rate,
						channel_count: samples.channel_count,
					}
				}
				_ => {
					// Silence of the expected length keeps the output buffer
					// aligned (an underrun here just plays zeros).
					let seconds = len_ts as f64 * tb.0 as f64 / tb.1 as f64;
					let frames = (seconds * sample_rate as f64).round() as usize;
					RenderedAudio {
						data: vec![0.0; frames * channels as usize],
						sample_rate,
						channel_count: channels,
					}
				}
			};
			let _ = tx.send((start_ts, data));
		}),
	);
	Ok(())
}

// ---------------------------------------------------------------------------
// Export (the facade's `oakengine_task_create_export` + sync run over the
// module export task)
// ---------------------------------------------------------------------------

/// The export audio defaults (the facade's): 48 kHz stereo.
const EXPORT_SAMPLE_RATE: i32 = 48000;
/// Stereo channel-layout bitmask (`OLIVE_CHANNEL_LAYOUT_STEREO`).
const EXPORT_CHANNEL_LAYOUT: u64 = 0x3;

/// Build the export encoding params for `seq` from the app's export
/// dialog inputs: the container `format`, the target `path`, and the
/// work-area range when enabled (frame timestamps). The codecs are the
/// format's first video/audio codecs (the facade's
/// `oakengine_encoding_format_*_codec_at(format, 0)`).
pub fn encoding_params(
	p: &ProjectRef,
	seq: NodeId,
	format: i32,
	path: &std::path::Path,
	workarea: Option<(i64, i64)>,
	length_frames: i64,
) -> Result<oak_task::export::EncodingParams, String> {
	let container = oak_codec::exportformat::Format::from_i32(format)
		.ok_or_else(|| format!("unknown export format {format}"))?;
	let video_codec = oak_codec::exportformat::Format::get_video_codecs(container)
		.first()
		.copied()
		.ok_or_else(|| format!("format {format} has no video codec"))?;
	let audio_codec = oak_codec::exportformat::Format::get_audio_codecs(container)
		.first()
		.copied()
		.ok_or_else(|| format!("format {format} has no audio codec"))?;
	let (width, height, rate) = {
		let g = lock(p);
		super::graphops::sequence_video_params(&g.graph, seq)
			.ok_or_else(|| "the sequence has no video parameters".to_string())?
	};
	let rate_num = rate.numerator().max(1) as i32;
	let rate_den = rate.denominator().max(1) as i32;

	// Export range: the work area when enabled, otherwise the whole
	// sequence. Frames -> seconds rationals in the sequence's frame-rate
	// timebase (frame duration = rate_den / rate_num).
	let (has_custom_range, range_in, range_out, length) =
		match workarea.filter(|(s, e)| e > s) {
			Some((in_ts, out_ts)) => (
				true,
				Rational::new(in_ts * i64::from(rate_den), i64::from(rate_num)),
				Rational::new(out_ts * i64::from(rate_den), i64::from(rate_num)),
				Rational::new((out_ts - in_ts) * i64::from(rate_den), i64::from(rate_num)),
			),
			None => (
				false,
				Rational::new(0, 1),
				Rational::new(0, 1),
				Rational::new(length_frames.max(0) * i64::from(rate_den), i64::from(rate_num)),
			),
		};
	Ok(oak_task::export::EncodingParams {
		filename: path.to_string_lossy().into_owned(),
		format,
		video_enabled: true,
		video_codec: video_codec as i32,
		video_width: width.max(1),
		video_height: height.max(1),
		video_time_base_num: rate_den,
		video_time_base_den: rate_num,
		video_pixel_format: 0,
		audio_enabled: true,
		audio_codec: audio_codec as i32,
		audio_sample_rate: EXPORT_SAMPLE_RATE,
		audio_channel_layout: EXPORT_CHANNEL_LAYOUT,
		subtitles_enabled: false,
		export_length_num: length.numerator() as i32,
		export_length_den: length.denominator() as i32,
		has_custom_range,
		custom_range_in_num: range_in.numerator() as i32,
		custom_range_in_den: range_in.denominator() as i32,
		custom_range_out_num: range_out.numerator() as i32,
		custom_range_out_den: range_out.denominator() as i32,
	})
}

/// Start an export of `seq` with `params` on a background thread; the
/// returned session carries the event channel and the cancel handle (the
/// facade's export task wiring over the module task's event listener and
/// cancel atom).
pub fn spawn_export(
	p: &ProjectRef,
	seq: NodeId,
	params: oak_task::export::EncodingParams,
) -> ExportSession {
	let (tx, rx) = mpsc::channel::<ExportEvent>();
	let mut driver = oak_task::task::Task::new("Exporting...", None);
	let cancel_atom = driver.get_cancel_atom();
	{
		let tx = tx.clone();
		driver.set_event_listener(Box::new(move |event| {
			let event = match event {
				oak_task::task::TaskEvent::Started => ExportEvent::Started,
				oak_task::task::TaskEvent::Progress(value) => ExportEvent::Progress(value),
				// Finished is reported by the worker below (with the error).
				oak_task::task::TaskEvent::Finished => return,
			};
			let _ = tx.send(event);
		}));
	}
	driver.set_behavior(Box::new(oak_task::export::ExportTask::new(
		(p.clone(), seq),
		params,
	)));
	std::thread::spawn(move || {
		let result = driver.start();
		let error = if result.is_ok() {
			String::new()
		} else {
			driver
				.error()
				.map(|s| s.to_string())
				.unwrap_or_else(|| "export failed".to_string())
		};
		let _ = tx.send(ExportEvent::Finished(result.is_ok(), error));
	});
	ExportSession {
		events: rx,
		cancel: Box::new(move || cancel_atom.cancel()),
	}
}

#[cfg(test)]
mod tests {
	use super::*;
	use crate::oakui::graphops;

	/// Serializes the media/FFmpeg-heavy tests (the codec library is not
	/// thread-safe against concurrent decode sessions) and shares the
	/// process-global undo stack with the other app test modules.
	fn media_lock() -> std::sync::MutexGuard<'static, ()> {
		crate::oakui::graphops::test_lock()
	}

	/// A project with an HD sequence carrying one clip of the generated
	/// media on its video track.
	fn project_with_clip(media: &std::path::Path) -> (ProjectRef, NodeId, NodeId) {
		let project = graphops::create_project();
		let seq = graphops::create_sequence(&project, "Montage Test");
		let footage = graphops::import_footage(&project, media).expect("import the generated media");
		graphops::add_track(&project, seq, TrackType::Video).expect("add a video track");
		graphops::place_footage_clip(&project, seq, footage, TrackType::Video, 0, 0, 10, 0)
			.expect("place the clip");
		(project, seq, footage)
	}

	#[test]
	fn video_montage_covers_the_clip_range() {
		let _media = media_lock();
		let media = std::env::temp_dir().join(format!("oakapp_montage_{}.mp4", std::process::id()));
		oak_codec::testmedia::write_test_clip(&media, 64, 64, 10, 10).expect("generate test media");

		let (project, seq, _) = project_with_clip(&media);
		let tb = graphops::sequence_time_base(&lock(&project).graph, seq).unwrap();
		let at = |frame: i64| graphops::ts_to_rational(frame, tb);

		let montage = video_montage(&project, seq, at(0));
		assert_eq!(montage.len(), 1, "one clip covers frame 0");
		assert_eq!(montage[0].filename, media.to_string_lossy());
		assert!(video_montage(&project, seq, at(9)).len() == 1, "frame 9 is still covered");
		assert!(
			video_montage(&project, seq, at(10)).is_empty(),
			"frame 10 is past the clip's out point"
		);
		assert!(
			video_montage(&project, seq, at(-1)).is_empty(),
			"a negative time covers nothing"
		);
		oak_undo::global::clear().unwrap();
		let _ = std::fs::remove_file(&media);
	}

	/// The acceptance gate for montage effect rendering: a 50% Opacity on
	/// a real clip (generated media, real graph, real decode, real
	/// compositing — nothing mocked) must change the rendered pixels, and
	/// disabling the effect must restore the plain render byte-for-byte.
	#[test]
	fn montage_effect_stack_reaches_the_rendered_pixels() {
		let _media = media_lock();
		oak_undo::global::clear().unwrap();
		let media =
			std::env::temp_dir().join(format!("oakapp_montage_fx_{}.mp4", std::process::id()));
		oak_codec::testmedia::write_test_clip(&media, 64, 64, 10, 10).expect("generate test media");

		let project = graphops::create_project();
		let seq = graphops::create_sequence(&project, "Effect Montage");
		let footage = graphops::import_footage(&project, &media).expect("import the generated media");
		graphops::add_track(&project, seq, TrackType::Video).expect("add a video track");
		let block = graphops::place_footage_clip(&project, seq, footage, TrackType::Video, 0, 0, 10, 0)
			.expect("place the clip");

		// The effect-library double-click flow: append a 50% Opacity to the
		// footage-backed clip (the insert rewires footage -> effect -> clip).
		let fx = crate::oakui::effectchain::insert(
			&project,
			block,
			usize::MAX,
			"org.olivevideoeditor.Olive.opacity",
		)
		.expect("append the opacity effect");
		crate::oakui::effectchain::set_input_value(
			&project,
			fx,
			"opacity_in",
			oak_node::value::NodeValue::Float(0.5),
		)
		.expect("set the opacity value");

		let tb = graphops::sequence_time_base(&lock(&project).graph, seq).unwrap();
		let time = graphops::ts_to_rational(0, tb);

		// Render the montage through the same entry point the render worker
		// uses (`render_montage_frame_into`, F32 RGBA rows).
		let render = |montage: Vec<MontageClip>| {
			let params = VideoTicketParams {
				viewer: 0,
				time,
				force_size: Some((64, 64)),
				force_format: Some(oak_core::PixelFormat::F32),
				cache: None,
				cache_dir: None,
				cache_id: None,
				cache_timebase: None,
				footage: None,
				montage,
			};
			let mut dst = vec![0u8; 64 * 64 * 16];
			oak_render::eval::render_montage_frame_into(time, &params, (64, 64), &mut dst, 64 * 16)
				.expect("montage render");
			dst
		};
		// A left-half pixel of the test pattern (known content, r ~= 0.9).
		let pixel = |frame: &[u8]| {
			let off = (8 * 64 + 8) * 16;
			[0, 1, 2, 3].map(|i| f32::from_le_bytes(frame[off + i * 4..off + i * 4 + 4].try_into().unwrap()))
		};

		let with_fx = video_montage(&project, seq, time);
		assert_eq!(with_fx.len(), 1, "one clip covers frame 0");
		assert_eq!(
			with_fx[0].effects.len(),
			1,
			"the montage carries the clip's effect stack (not the footage source node)"
		);
		assert_eq!(with_fx[0].effects[0].type_id, "org.olivevideoeditor.Olive.opacity");
		assert!(with_fx[0].effects[0].enabled);

		let plain = render(
			with_fx
				.iter()
				.cloned()
				.map(|mut c| {
					c.effects.clear();
					c
				})
				.collect(),
		);
		let effected = render(with_fx);
		let (p, e) = (pixel(&plain), pixel(&effected));
		assert!(p[0] > 0.6, "the plain render shows the test pattern (r={})", p[0]);
		// 50% opacity: the shader halves every channel, then the composite
		// over transparent black halves it again via the halved alpha.
		assert!(
			(e[0] - p[0] * 0.25).abs() < 0.1,
			"50% opacity quarters the output ({} vs {})",
			e[0],
			p[0] * 0.25
		);
		assert!(
			(e[3] - 0.5).abs() < 0.1,
			"the composited alpha halves (a={})",
			e[3]
		);

		// Disabled effect = bypass: pixels match the plain render.
		crate::oakui::effectchain::set_enabled(&project, fx, false).expect("disable the effect");
		let disabled_montage = video_montage(&project, seq, time);
		assert_eq!(disabled_montage[0].effects.len(), 1);
		assert!(!disabled_montage[0].effects[0].enabled);
		let disabled = render(disabled_montage);
		let d = pixel(&disabled);
		assert!(
			(d[0] - p[0]).abs() < 0.05,
			"a disabled effect leaves the pixels alone ({} vs {})",
			d[0],
			p[0]
		);

		oak_undo::global::clear().unwrap();
		let _ = std::fs::remove_file(&media);
	}

	/// The original media decodes from the footage's actual first stream of
	/// the kind (not the hardcoded 0/1 of a typical layout): a file whose
	/// video stream is not stream 0 must still decode its own video when
	/// the proxy switch is off or the proxy is not ready.
	#[test]
	fn preview_media_uses_the_probed_stream_indices() {
		let stream = |index: i32, is_video: bool| oak_node::footage::StreamInfo {
			index,
			is_video,
			video: None,
			audio: None,
			duration: oak_core::Rational::new(0, 1),
		};
		// An audio-first container: audio at 0, video at 1… plus a second
		// audio track at 2. The video must come from stream 1 and the audio
		// from stream 0, not the legacy 0/1 assumption.
		let mut f = oak_node::footage::FootageBehavior::new("/tmp/audio-first.mov");
		f.streams = vec![stream(0, false), stream(1, true), stream(2, false)];
		assert_eq!(preview_footage_media(&f, true).1, 1);
		assert_eq!(preview_footage_media(&f, false).1, 0);
		// Unprobed footage (no stream metadata) keeps the 0/1 fallbacks.
		let unprobed = oak_node::footage::FootageBehavior::new("/tmp/legacy.mov");
		assert_eq!(preview_footage_media(&unprobed, true).1, 0);
		assert_eq!(preview_footage_media(&unprobed, false).1, 1);
	}

	/// The multicam angle montage ([`single_track_video_montage`]) carries
	/// ONLY the clip on the requested track — the whole-stack `video_montage`
	/// is the parity reference. This is the montage the angle-frame ticket
	/// renders for each grid cell.
	#[test]
	fn single_track_montage_isolates_its_track() {
		let _media = media_lock();
		let media =
			std::env::temp_dir().join(format!("oakapp_montage_ang_{}.mp4", std::process::id()));
		oak_codec::testmedia::write_test_clip(&media, 64, 64, 10, 10).expect("generate test media");

		let (project, seq, footage) = project_with_clip(&media);
		graphops::add_track(&project, seq, TrackType::Video).expect("add a second video track");
		// A second clip on track 1 overlapping the same time.
		graphops::place_footage_clip(&project, seq, footage, TrackType::Video, 1, 0, 10, 0)
			.expect("place the second clip");
		let tb = graphops::sequence_time_base(&lock(&project).graph, seq).unwrap();
		let at = |frame: i64| graphops::ts_to_rational(frame, tb);
		let tracks = {
			let g = lock(&project);
			graphops::track_ids(&g.graph, seq, TrackType::Video)
		};
		// The two default video tracks plus the two added here.
		assert_eq!(tracks.len(), 4, "default 2 video tracks + 2 added");

		// The whole stack sees both clips; the single-track montage sees only
		// its own track's clip.
		assert_eq!(video_montage(&project, seq, at(0)).len(), 2);
		let track0_only = single_track_video_montage(&project, seq, tracks[0], at(0));
		assert_eq!(track0_only.len(), 1, "track 0 contributes its own clip");
		assert_eq!(track0_only[0].filename, media.to_string_lossy());
		let track1_only = single_track_video_montage(&project, seq, tracks[1], at(0));
		assert_eq!(track1_only.len(), 1, "track 1 contributes its own clip");

		// A hidden video track contributes nothing (the angle's track is
		// skipped, matching the full montage's muted-track rule).
		graphops::set_track_muted(&project, tracks[0], true).expect("hide track 0");
		assert!(
			single_track_video_montage(&project, seq, tracks[0], at(0)).is_empty(),
			"a hidden angle track renders nothing"
		);
		assert_eq!(
			single_track_video_montage(&project, seq, tracks[1], at(0)).len(),
			1,
			"the other track is unaffected"
		);

		oak_undo::global::clear().unwrap();
		let _ = std::fs::remove_file(&media);
	}

	#[test]
	fn audio_montage_overlaps_the_range() {
		let _media = media_lock();
		let media = std::env::temp_dir().join(format!("oakapp_montage_a_{}.mp4", std::process::id()));
		oak_codec::testmedia::write_test_clip(&media, 64, 64, 10, 10).expect("generate test media");

		let (project, seq, footage) = project_with_clip(&media);
		graphops::add_track(&project, seq, TrackType::Audio).expect("add an audio track");
		graphops::place_footage_clip(&project, seq, footage, TrackType::Audio, 0, 0, 10, 0)
			.expect("place the audio clip");
		let tb = graphops::sequence_time_base(&lock(&project).graph, seq).unwrap();
		let at = |frame: i64| graphops::ts_to_rational(frame, tb);

		let overlapping = audio_montage(&project, seq, TimeRange::new(at(0), at(5)));
		assert_eq!(overlapping.len(), 1, "the range overlaps the audio clip");
		assert_eq!(overlapping[0].stream_index, 1, "the audio stream is selected");
		let disjoint = audio_montage(&project, seq, TimeRange::new(at(10), at(20)));
		assert!(disjoint.is_empty(), "a range past the clip overlaps nothing");
		oak_undo::global::clear().unwrap();
		let _ = std::fs::remove_file(&media);
	}

	/// Hidden video tracks and muted audio tracks (both are the track's
	/// `muted` flag) contribute nothing to the montages; undoing the flag
	/// set restores them.
	#[test]
	fn montages_skip_muted_tracks() {
		let _media = media_lock();
		let media = std::env::temp_dir().join(format!("oakapp_montage_m_{}.mp4", std::process::id()));
		oak_codec::testmedia::write_test_clip(&media, 64, 64, 10, 10).expect("generate test media");

		let (project, seq, footage) = project_with_clip(&media);
		graphops::add_track(&project, seq, TrackType::Audio).expect("add an audio track");
		graphops::place_footage_clip(&project, seq, footage, TrackType::Audio, 0, 0, 10, 0)
			.expect("place the audio clip");
		let (video_track, audio_track, tb) = {
			let g = lock(&project);
			(
				graphops::track_ids(&g.graph, seq, TrackType::Video)[0],
				graphops::track_ids(&g.graph, seq, TrackType::Audio)[0],
				graphops::sequence_time_base(&g.graph, seq).unwrap(),
			)
		};
		let at = |frame: i64| graphops::ts_to_rational(frame, tb);
		let range = TimeRange::new(at(0), at(5));
		assert_eq!(video_montage(&project, seq, at(0)).len(), 1);
		assert_eq!(audio_montage(&project, seq, range).len(), 1);

		graphops::set_track_muted(&project, video_track, true).expect("hide the video track");
		graphops::set_track_muted(&project, audio_track, true).expect("mute the audio track");
		assert!(
			video_montage(&project, seq, at(0)).is_empty(),
			"a hidden video track contributes nothing"
		);
		assert!(
			audio_montage(&project, seq, range).is_empty(),
			"a muted audio track contributes nothing"
		);

		oak_undo::global::undo().unwrap();
		oak_undo::global::undo().unwrap();
		assert_eq!(video_montage(&project, seq, at(0)).len(), 1, "undo restores the video clip");
		assert_eq!(audio_montage(&project, seq, range).len(), 1, "undo restores the audio clip");
		oak_undo::global::clear().unwrap();
		let _ = std::fs::remove_file(&media);
	}
}
