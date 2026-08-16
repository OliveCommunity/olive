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
//! `engine.rs` (M14 R2). The export drives `oaktask::export::ExportTask`
//! synchronously on a background thread with the module task's event
//! listener and cancel atom wired to the app's [`ExportSession`].

use std::sync::mpsc;

use oakcore_rs::{Rational, TimeRange};
use oaknode::id::NodeId;
use oaknode::track::TrackType;
use oakrender::manager::RenderManager;
use oakrender::texture::Texture;
use oakrender::ticket::{AudioTicketParams, MontageClip, TicketPayload, VideoTicketParams};

use super::engine::{ExportEvent, ExportSession};
use super::graphops::{
	clip_behavior, lock, sequence_behavior, track_behavior, track_list_behavior, ProjectRef,
};

/// The pixel format the viewers render in (`oakcore_rs::PixelFormat::F32`,
/// the pipeline's internal format; the app downconverts to BGRA itself).
pub const PIXEL_FORMAT_F32: i32 = 4;

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

/// The media `(filename, stream_index)` feeding a clip (upstream BFS +
/// footage behavior); video stream 0.
fn clip_media(g: &oaknode::graph::Graph, block_id: NodeId) -> Option<String> {
	super::graphops::clip_media_filename(g, block_id)
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
				let Some(filename) = clip_media(&g.graph, block_id) else {
					continue;
				};
				clips.push(MontageClip {
					filename,
					stream_index: 0,
					in_time: in_,
					out_time: out,
					media_in: clip.core.media_in,
					gain: 1.0,
				});
			}
		}
	}
	clips
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
				let Some(filename) = clip_media(&g.graph, block_id) else {
					continue;
				};
				clips.push(MontageClip {
					filename,
					stream_index: 1,
					in_time: in_,
					out_time: out,
					media_in: clip.core.media_in,
					gain: 1.0,
				});
			}
		}
	}
	clips
}

// ---------------------------------------------------------------------------
// Ticket rendering
// ---------------------------------------------------------------------------

/// A rendered frame's pixel payload (the module frame a video ticket
/// produces).
pub struct RenderedFrame {
	/// Width in pixels.
	pub width: i32,
	/// Height in pixels.
	pub height: i32,
	/// Pixel format (`oakcore_rs::PixelFormat` as int).
	pub format: i32,
	/// Bytes per scanline (stride).
	pub linesize: i32,
	/// Pixel data (at least `linesize * height` bytes).
	pub data: Vec<u8>,
}

/// The renderer geometry / format validation (the facade's
/// `make_renderer_box` argument checks).
fn validate_geometry(width: i32, height: i32, tb: (i64, i64)) -> Result<(), String> {
	if width <= 0 || height <= 0 || tb.0 <= 0 || tb.1 <= 0 {
		return Err("invalid render geometry or frame rate".to_string());
	}
	Ok(())
}

/// Drive one video ticket synchronously.
fn render_video(params: VideoTicketParams) -> Result<RenderedFrame, String> {
	let m = RenderManager::global().ok_or_else(|| "render manager is not initialized".to_string())?;
	let id = m.tickets.next_id();
	m.tickets.submit_video_with_id(id, params, Box::new(|_| {}));
	m.tickets.wait(id).map_err(|e| e.to_string())?;
	let result = m
		.tickets
		.result(id)
		.ok_or_else(|| "render ticket produced no result".to_string())?;
	match &result {
		Ok(TicketPayload::Video(Texture::Cpu(frame))) => Ok(RenderedFrame {
			width: frame.width,
			height: frame.height,
			format: frame.format as i32,
			linesize: frame.linesize_bytes() as i32,
			data: frame.data.clone(),
		}),
		Ok(TicketPayload::Video(_)) => Err("render produced a non-CPU frame".to_string()),
		_ => Err("render produced no video frame".to_string()),
	}
}

/// Render one frame of the sequence's montage at `frame_ts` (a timestamp
/// in the `(tb.1 / tb.0)`-per-frame timebase) into a `(width, height)`
/// F32 frame.
pub fn render_sequence_frame(
	p: &ProjectRef,
	seq: NodeId,
	frame_ts: i64,
	tb: (i64, i64),
	width: i32,
	height: i32,
) -> Result<RenderedFrame, String> {
	validate_geometry(width, height, tb)?;
	let time = Rational::new(frame_ts * tb.0, tb.1);
	let montage = video_montage(p, seq, time);
	render_video(VideoTicketParams {
		viewer: seq.identity(),
		time,
		force_size: Some((width, height)),
		force_format: None,
		cache: None,
		cache_dir: None,
		cache_id: None,
		cache_timebase: None,
		footage: None,
		montage,
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
	validate_geometry(width, height, tb)?;
	let filename = {
		let g = lock(p);
		super::graphops::footage_behavior(&g.graph, footage)
			.map(|f| f.filename.clone())
			.ok_or_else(|| "the node is not footage".to_string())?
	};
	let time = Rational::new(frame_ts * tb.0, tb.1);
	render_video(VideoTicketParams {
		viewer: footage.identity(),
		time,
		force_size: Some((width, height)),
		force_format: None,
		cache: None,
		cache_dir: None,
		cache_id: None,
		cache_timebase: None,
		footage: Some((filename, 0)),
		montage: Vec::new(),
	})
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
		_ => Err("audio render produced no samples".to_string()),
	}
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
) -> Result<oaktask::export::EncodingParams, String> {
	let container = oakcodec::exportformat::Format::from_i32(format)
		.ok_or_else(|| format!("unknown export format {format}"))?;
	let video_codec = oakcodec::exportformat::Format::get_video_codecs(container)
		.first()
		.copied()
		.ok_or_else(|| format!("format {format} has no video codec"))?;
	let audio_codec = oakcodec::exportformat::Format::get_audio_codecs(container)
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
	Ok(oaktask::export::EncodingParams {
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
	params: oaktask::export::EncodingParams,
) -> ExportSession {
	let (tx, rx) = mpsc::channel::<ExportEvent>();
	let mut driver = oaktask::task::Task::new("Exporting...", None);
	let cancel_atom = driver.get_cancel_atom();
	{
		let tx = tx.clone();
		driver.set_event_listener(Box::new(move |event| {
			let event = match event {
				oaktask::task::TaskEvent::Started => ExportEvent::Started,
				oaktask::task::TaskEvent::Progress(value) => ExportEvent::Progress(value),
				// Finished is reported by the worker below (with the error).
				oaktask::task::TaskEvent::Finished => return,
			};
			let _ = tx.send(event);
		}));
	}
	driver.set_behavior(Box::new(oaktask::export::ExportTask::new(
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
		oakcodec::testmedia::write_test_clip(&media, 64, 64, 10, 10).expect("generate test media");

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
		oakundo::global::clear().unwrap();
		let _ = std::fs::remove_file(&media);
	}

	#[test]
	fn audio_montage_overlaps_the_range() {
		let _media = media_lock();
		let media = std::env::temp_dir().join(format!("oakapp_montage_a_{}.mp4", std::process::id()));
		oakcodec::testmedia::write_test_clip(&media, 64, 64, 10, 10).expect("generate test media");

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
		oakundo::global::clear().unwrap();
		let _ = std::fs::remove_file(&media);
	}

	/// Hidden video tracks and muted audio tracks (both are the track's
	/// `muted` flag) contribute nothing to the montages; undoing the flag
	/// set restores them.
	#[test]
	fn montages_skip_muted_tracks() {
		let _media = media_lock();
		let media = std::env::temp_dir().join(format!("oakapp_montage_m_{}.mp4", std::process::id()));
		oakcodec::testmedia::write_test_clip(&media, 64, 64, 10, 10).expect("generate test media");

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

		oakundo::global::undo().unwrap();
		oakundo::global::undo().unwrap();
		assert_eq!(video_montage(&project, seq, at(0)).len(), 1, "undo restores the video clip");
		assert_eq!(audio_montage(&project, seq, range).len(), 1, "undo restores the audio clip");
		oakundo::global::clear().unwrap();
		let _ = std::fs::remove_file(&media);
	}
}
