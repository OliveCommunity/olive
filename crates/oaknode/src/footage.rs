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

//! Footage nodes (C++ `olive::Footage`): media file references.
//! Probing goes through the oakcodec decoder registry (direct Rust calls,
//! single-lib unification).
//! `// CPP-PARITY: src/node/src/project/footage/footage.{h,cpp}`.

use std::sync::atomic::{AtomicBool, Ordering};

use crate::input::Input;
use crate::node::{Category, NodeBehavior, NodeCore};
use crate::value::{AudioParams, NodeValue, ValueType, VideoParams};

/// One media stream inside a footage file.
#[derive(Clone, Debug)]
pub struct StreamInfo {
	/// Stream index in the container.
	pub index: i32,
	/// True for video streams.
	pub is_video: bool,
	/// Video parameters (when `is_video`).
	pub video: Option<VideoParams>,
	/// Audio parameters (when not video).
	pub audio: Option<AudioParams>,
	/// Duration in rational seconds (the longest stream wins; see
	/// [`FootageBehavior::duration`]).
	pub duration: oakcore_rs::Rational,
}

/// Footage behavior.
pub struct FootageBehavior {
	/// Absolute file path.
	pub filename: String,
	/// Probed streams (empty until [`FootageBehavior::probe`]).
	pub streams: Vec<StreamInfo>,
	/// Proxy path (C++ set_proxy; empty = none).
	pub proxy: String,
	/// Proxy playback enabled.
	pub proxy_enabled: bool,
	/// Proxy state (`ProxyManager::ProxyState`).
	pub proxy_state: i32,
	/// Proxy's video stream index (-1 when none).
	pub proxy_video_stream_index: i32,
	/// Proxy preset version.
	pub proxy_preset_version: i32,
	/// File last-modified timestamp (ms since epoch).
	pub timestamp: i64,
	/// Decoder id recorded at probe time.
	pub decoder: String,
	/// True after a successful probe (C++ `is_valid`).
	pub valid: bool,
	/// Shared cancellation flag (C++ cancel atom).
	pub cancel: std::sync::Arc<AtomicBool>,
}

impl FootageBehavior {
	/// Create for `filename` (unprobed).
	pub fn new(filename: &str) -> Self {
		FootageBehavior {
			filename: filename.to_string(),
			streams: Vec::new(),
			proxy: String::new(),
			proxy_enabled: false,
			proxy_state: 0,
			proxy_video_stream_index: -1,
			proxy_preset_version: 0,
			timestamp: 0,
			decoder: String::new(),
			valid: false,
			cancel: std::sync::Arc::new(AtomicBool::new(false)),
		}
	}

	/// Probe the file through oakcodec's decoder registry, recording the
	/// recognized decoder id, the probed stream inventory and the file's
	/// last-modified timestamp. Error on unreadable/corrupt media; the
	/// prior `streams`/`valid` state is preserved on failure (no partial
	/// state).
	pub fn probe(&mut self) -> crate::error::Result<()> {
		use crate::error::Error;
		// Direct probe through the oakcodec decoder registry (single-lib
		// unification; replaces the former `oakcodec_decoder_probe` C ABI
		// call).
		let desc = oakcodec::decoder::receive_list_of_all_decoders()
			.into_iter()
			.find_map(|d| {
				let desc = d.probe(&self.filename, None)?;
				if desc.decoder().is_empty()
					|| (desc.video_stream_count() == 0
						&& desc.audio_stream_count() == 0
						&& desc.subtitle_stream_count() == 0)
				{
					return None;
				}
				Some(desc)
			})
			.ok_or_else(|| Error::Failed("oakcodec probe returned no streams".to_string()))?;
		let streams = streams_from_description(&desc);
		// File last-modified timestamp (ms since epoch; the C++ probe
		// records it for the proxy/reprobe freshness check). Best effort:
		// an unreadable stat never fails the probe.
		let timestamp = std::fs::metadata(&self.filename)
			.and_then(|m| m.modified())
			.ok()
			.and_then(|t| t.duration_since(std::time::UNIX_EPOCH).ok())
			.map(|d| d.as_millis() as i64)
			.unwrap_or(0);
		// Commit only on full success (no partial state on failure).
		self.decoder = desc.decoder().to_string();
		self.streams = streams;
		self.timestamp = timestamp;
		self.valid = true;
		Ok(())
	}

	/// Set the cancellation flag (C++ `set_cancel_pointer`).
	pub fn set_cancel(&mut self, cancelled: bool) {
		self.cancel.store(cancelled, Ordering::Relaxed);
	}

	/// True when the cancellation atom is set.
	pub fn is_cancelled(&self) -> bool {
		self.cancel.load(Ordering::Relaxed)
	}

	/// Total stream count (C++ `get_total_stream_count`).
	pub fn total_stream_count(&self) -> usize {
		self.streams.len()
	}

	/// Video stream count.
	pub fn video_stream_count(&self) -> usize {
		self.streams.iter().filter(|s| s.is_video).count()
	}

	/// Audio stream count.
	pub fn audio_stream_count(&self) -> usize {
		self.streams.iter().filter(|s| !s.is_video).count()
	}

	/// Subtitle stream count (none without a subtitle codec).
	pub fn subtitle_stream_count(&self) -> usize {
		0
	}

	/// Duration of the longest stream (C++ `ViewerOutput::get_length`).
	pub fn duration(&self) -> oakcore_rs::Rational {
		let mut longest = oakcore_rs::Rational::new(0, 1);
		for s in &self.streams {
			if s.duration > longest {
				longest = s.duration;
			}
		}
		longest
	}

	/// Video length (duration of the longest video stream).
	pub fn video_length(&self) -> oakcore_rs::Rational {
		let mut longest = oakcore_rs::Rational::new(0, 1);
		for s in self.streams.iter().filter(|s| s.is_video) {
			if s.duration > longest {
				longest = s.duration;
			}
		}
		longest
	}

	/// Video params of the `index`th video stream.
	pub fn video_params(&self, index: usize) -> Option<VideoParams> {
		self.streams
			.iter()
			.filter(|s| s.is_video)
			.nth(index)
			.and_then(|s| s.video)
	}

	/// Audio params of the `index`th audio stream.
	pub fn audio_params(&self, index: usize) -> Option<AudioParams> {
		self.streams
			.iter()
			.filter(|s| !s.is_video)
			.nth(index)
			.and_then(|s| s.audio)
	}

	/// The stream at container order `index` (params plus duration).
	pub fn stream_at(&self, index: usize) -> Option<&StreamInfo> {
		self.streams.get(index)
	}

	/// Set all proxy fields at once (C++ `set_proxy`).
	pub fn set_proxy(
		&mut self,
		path: &str,
		state: i32,
		video_stream_index: i32,
		preset_version: i32,
		enabled: bool,
	) {
		self.proxy = path.to_string();
		self.proxy_state = state;
		self.proxy_video_stream_index = video_stream_index;
		self.proxy_preset_version = preset_version;
		self.proxy_enabled = enabled;
	}

	/// Clear all proxy fields (C++ `clear_proxy`).
	pub fn clear_proxy(&mut self) {
		self.proxy.clear();
		self.proxy_state = 0;
		self.proxy_video_stream_index = -1;
		self.proxy_preset_version = 0;
		self.proxy_enabled = false;
	}

	/// Constructor for the serializer: the C++ `Footage` input surface
	/// (`file_in` + the viewer parameter stream arrays) with an unprobed
	/// behavior (`// CPP-PARITY: footage.cpp:83`, `viewer.cpp:84`).
	pub fn create() -> (NodeCore, Box<dyn NodeBehavior>) {
		let mut core = NodeCore::new();
		let mut file = Input::new(
			"file_in",
			ValueType::Text,
			NodeValue::Text(String::new()),
		);
		file.flags |= crate::input::flags::NOT_CONNECTABLE | crate::input::flags::NOT_KEYFRAMABLE;
		core.add_input(file);
		for (id, ty) in [
			("video_param_in", ValueType::VideoParams),
			("audio_param_in", ValueType::AudioParams),
			("subtitle_param_in", ValueType::None),
		] {
			let mut input = Input::new(id, ty, NodeValue::None);
			input.flags |= crate::input::flags::NOT_CONNECTABLE
				| crate::input::flags::NOT_KEYFRAMABLE
				| crate::input::flags::ARRAY
				| crate::input::flags::HIDDEN;
			core.add_input(input);
		}
		(core, Box::new(FootageBehavior::new("")))
	}
}

impl NodeBehavior for FootageBehavior {
	fn name(&self) -> &str {
		"Footage"
	}

	fn type_id(&self) -> &str {
		"org.olivevideoeditor.Olive.footage"
	}

	fn categories(&self) -> &[Category] {
		&[Category::Input]
	}

	fn duplicate(&self, _core: &NodeCore) -> Option<Box<dyn NodeBehavior>> {
		Some(Box::new(FootageBehavior {
			filename: self.filename.clone(),
			streams: self.streams.clone(),
			proxy: self.proxy.clone(),
			proxy_enabled: self.proxy_enabled,
			proxy_state: self.proxy_state,
			proxy_video_stream_index: self.proxy_video_stream_index,
			proxy_preset_version: self.proxy_preset_version,
			timestamp: self.timestamp,
			decoder: self.decoder.clone(),
			valid: self.valid,
			cancel: self.cancel.clone(),
		}))
	}

	/// Custom project save (C++ `Footage::SaveCustom`): the file name,
	/// media timestamp, proxy state and the probed streams. `<filename>`
	/// and `<streams>` are Rust additions (C++ reads the name from the
	/// `file_in` input); old readers skip them.
	fn save_custom(&self, core: &NodeCore, writer: &mut dyn crate::serializer::XmlWrite) {
		let _ = core;
		if !self.filename.is_empty() {
			writer.text_element("filename", &self.filename);
		}
		if self.timestamp != 0 {
			writer.text_element("timestamp", &self.timestamp.to_string());
		}
		if !self.proxy.is_empty() || self.proxy_enabled {
			writer.start_element("proxy");
			writer.attribute("enabled", if self.proxy_enabled { "1" } else { "0" });
			writer.attribute("state", &self.proxy_state.to_string());
			writer.attribute("stream", &self.proxy_video_stream_index.to_string());
			writer.attribute("preset", &self.proxy_preset_version.to_string());
			writer.characters(&self.proxy);
			writer.end_element(); // proxy
		}
		if !self.streams.is_empty() {
			writer.start_element("streams");
			for s in &self.streams {
				writer.start_element("stream");
				writer.attribute("index", &s.index.to_string());
				writer.attribute("video", if s.is_video { "1" } else { "0" });
				writer.attribute("duration", &s.duration.to_display_string());
				if let Some(v) = s.video {
					writer.start_element("video");
					writer.attribute("width", &v.width.to_string());
					writer.attribute("height", &v.height.to_string());
					writer.attribute("framerate", &v.frame_rate.to_display_string());
					writer.attribute("pixelformat", &v.pixel_format.to_string());
					writer.attribute("channels", &v.channels.to_string());
					writer.end_element(); // video
				}
				if let Some(a) = s.audio {
					writer.start_element("audio");
					writer.attribute("samplerate", &a.sample_rate.to_string());
					writer.attribute("channellayout", &a.channel_layout.to_string());
					writer.attribute("format", &a.format.to_string());
					writer.end_element(); // audio
				}
				writer.end_element(); // stream
			}
			writer.end_element(); // streams
		}
	}

	/// Custom project load. C++ segments without a Rust counterpart
	/// (`sourcestarttime`, `viewer` workarea/markers) are skipped. The
	/// filename falls back to the `file_in` input when the file carries
	/// no `<filename>` element (the C++ convention; `<filename>` is a
	/// Rust addition).
	fn load_custom(
		&mut self,
		core: &mut NodeCore,
		reader: &mut dyn crate::serializer::XmlRead,
	) -> bool {
		while reader.next_start_element() {
			match reader.name() {
				"filename" => self.filename = reader.read_element_text(),
				"timestamp" => {
					self.timestamp = reader.read_element_text().trim().parse().unwrap_or(0)
				}
				"proxy" => {
					self.proxy_enabled = reader
						.attribute("enabled")
						.map(|v| v == "1")
						.unwrap_or(false);
					self.proxy_state = reader
						.attribute("state")
						.and_then(|v| v.parse().ok())
						.unwrap_or(0);
					self.proxy_video_stream_index = reader
						.attribute("stream")
						.and_then(|v| v.parse().ok())
						.unwrap_or(-1);
					self.proxy_preset_version = reader
						.attribute("preset")
						.and_then(|v| v.parse().ok())
						.unwrap_or(0);
					self.proxy = reader.read_element_text();
				}
				"streams" => {
					self.streams.clear();
					while reader.next_start_element() {
						if reader.name() == "stream" {
							let index = reader
								.attribute("index")
								.and_then(|v| v.parse().ok())
								.unwrap_or(0);
							let is_video = reader
								.attribute("video")
								.map(|v| v == "1")
								.unwrap_or(false);
							let duration = reader
								.attribute("duration")
								.map(|v| oakcore_rs::Rational::from_string(&v))
								.unwrap_or_else(|| oakcore_rs::Rational::new(0, 1));
							let mut video = None;
							let mut audio = None;
							while reader.next_start_element() {
								match reader.name() {
									"video" => {
										video = Some(VideoParams {
											width: reader
												.attribute("width")
												.and_then(|v| v.parse().ok())
												.unwrap_or(0),
											height: reader
												.attribute("height")
												.and_then(|v| v.parse().ok())
												.unwrap_or(0),
											frame_rate: reader
												.attribute("framerate")
												.map(|v| oakcore_rs::Rational::from_string(&v))
												.unwrap_or_else(|| {
													oakcore_rs::Rational::new(0, 1)
												}),
											pixel_format: reader
												.attribute("pixelformat")
												.and_then(|v| v.parse().ok())
												.unwrap_or(0),
											channels: reader
												.attribute("channels")
												.and_then(|v| v.parse().ok())
												.unwrap_or(0),
										});
										// Consume the (self-closing) element.
										let _ = reader.read_element_text();
									}
									"audio" => {
										audio = Some(AudioParams {
											sample_rate: reader
												.attribute("samplerate")
												.and_then(|v| v.parse().ok())
												.unwrap_or(0),
											channel_layout: reader
												.attribute("channellayout")
												.and_then(|v| v.parse().ok())
												.unwrap_or(0),
											format: reader
												.attribute("format")
												.and_then(|v| v.parse().ok())
												.unwrap_or(0),
										});
										// Consume the (self-closing) element.
										let _ = reader.read_element_text();
									}
									_ => reader.skip_current_element(),
								}
							}
							self.streams.push(StreamInfo {
								index,
								is_video,
								video,
								audio,
								duration,
							});
						} else {
							reader.skip_current_element();
						}
					}
				}
				_ => reader.skip_current_element(),
			}
		}
		if self.filename.is_empty() {
			let value = core.standard_value("file_in", -1);
			if let NodeValue::Text(text) = &value {
				self.filename = text.clone();
			}
		}
		true
	}

	fn as_any(&self) -> Option<&dyn std::any::Any> {
		Some(self)
	}

	fn as_any_mut(&mut self) -> Option<&mut dyn std::any::Any> {
		Some(self)
	}
}

// ---------------------------------------------------------------------------
// Probe-result conversion
// ---------------------------------------------------------------------------

/// The codec-side stream inventory as node-side [`StreamInfo`] rows.
///
/// Video and audio streams are converted field-by-field (their durations
/// become rational seconds); subtitle streams have no [`StreamInfo`]
/// representation and are skipped (the ffmpeg probe counts but never adds
/// them anyway). The rows are sorted by container index, restoring the
/// probe order the per-kind ordinal loops group away.
fn streams_from_description(
	desc: &oakcodec::footagedescription::FootageDescription,
) -> Vec<StreamInfo> {
	let mut streams = Vec::new();
	for i in 0..desc.video_stream_count() {
		let Some(vp) = desc.get_video_stream(i) else {
			continue;
		};
		let (num, den) = vp.frame_rate();
		streams.push(StreamInfo {
			index: vp.stream_index(),
			is_video: true,
			video: Some(VideoParams {
				width: vp.width(),
				height: vp.height(),
				frame_rate: oakcore_rs::Rational::new(num as i64, den as i64),
				pixel_format: vp.format().code(),
				channels: vp.channel_count(),
			}),
			audio: None,
			duration: stream_duration_seconds(vp.duration(), vp.time_base()),
		});
	}
	for i in 0..desc.audio_stream_count() {
		let Some(ap) = desc.get_audio_stream(i) else {
			continue;
		};
		streams.push(StreamInfo {
			index: ap.stream_index,
			is_video: false,
			video: None,
			audio: Some(AudioParams {
				sample_rate: ap.sample_rate,
				channel_layout: ap.channel_layout,
				format: ap.format,
			}),
			duration: stream_duration_seconds(ap.duration, ap.time_base),
		});
	}
	streams.sort_by_key(|s| s.index);
	streams
}

/// A stream duration in timebase ticks as rational seconds. `0/1` when
/// the duration or the timebase is unusable (FFmpeg reports
/// `AV_NOPTS_VALUE` for streams without a duration).
fn stream_duration_seconds(duration: i64, time_base: (i32, i32)) -> oakcore_rs::Rational {
	if duration <= 0 || time_base.0 <= 0 || time_base.1 <= 0 {
		return oakcore_rs::Rational::new(0, 1);
	}
	oakcore_rs::Rational::new(
		duration.saturating_mul(time_base.0 as i64),
		time_base.1 as i64,
	)
}

#[cfg(test)]
mod tests {
	use super::*;
	use oakcodec::footagedescription::{FootageDescription, StreamEntry};

	fn video_entry(stream_index: i32, duration: i64) -> StreamEntry {
		let mut vp = oakcommon::videoparams::VideoParams::new_basic(
			1920,
			1080,
			oakcommon::ocioutils::PixelFormat::F32,
			4,
			1,
			1,
			0,
			1,
		);
		vp.set_stream_index(stream_index);
		vp.set_frame_rate(30000, 1001);
		vp.set_time_base(1, 30000);
		vp.set_duration(duration);
		StreamEntry::Video(vp)
	}

	fn audio_entry(stream_index: i32, duration: i64) -> StreamEntry {
		StreamEntry::Audio(oakcodec::audioparams::AudioParams {
			sample_rate: 48000,
			channel_layout: 0x3,
			format: 0,
			stream_index,
			duration,
			time_base: (1, 48000),
		})
	}

	#[test]
	fn conversion_maps_fields_and_restores_probe_order() {
		let mut desc = FootageDescription::new("ffmpeg");
		// Interleaved container order: audio first, then video.
		desc.push_stream(audio_entry(0, 480_000));
		desc.push_stream(video_entry(1, 300_000));

		let streams = streams_from_description(&desc);
		assert_eq!(streams.len(), 2);

		// Sorted back into container order.
		let audio = &streams[0];
		let video = &streams[1];
		assert!(!audio.is_video);
		assert!(video.is_video);

		let a = audio.audio.expect("audio params");
		assert_eq!(a.sample_rate, 48000);
		assert_eq!(a.channel_layout, 0x3);
		// 480000 ticks at 1/48000 = 10 seconds.
		assert_eq!(audio.duration, oakcore_rs::Rational::new(10, 1));

		let v = video.video.expect("video params");
		assert_eq!(v.width, 1920);
		assert_eq!(v.height, 1080);
		assert_eq!(v.frame_rate, oakcore_rs::Rational::new(30000, 1001));
		assert_eq!(v.pixel_format, oakcommon::ocioutils::PixelFormat::F32.code());
		// 300000 ticks at 1/30000 = 10 seconds.
		assert_eq!(video.duration, oakcore_rs::Rational::new(10, 1));
	}

	#[test]
	fn conversion_skips_subtitles_and_unusable_durations() {
		let mut desc = FootageDescription::new("ffmpeg");
		desc.push_stream(video_entry(0, i64::MIN)); // AV_NOPTS_VALUE
		desc.push_stream(StreamEntry::Subtitle(oakcommon::subtitleparams::SubtitleParams::new()));

		let streams = streams_from_description(&desc);
		assert_eq!(streams.len(), 1, "the subtitle stream is skipped");
		assert_eq!(
			streams[0].duration,
			oakcore_rs::Rational::new(0, 1),
			"an unusable duration clamps to zero"
		);
	}

	#[test]
	fn failed_probe_keeps_prior_state() {
		let mut f = FootageBehavior::new("/nonexistent/file.mov");
		assert!(f.probe().is_err());
		assert!(!f.valid);
		assert!(f.streams.is_empty());
		assert_eq!(f.timestamp, 0);
	}
}
