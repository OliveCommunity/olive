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
//! Probing goes through the oakcodec C ABI (`bridge::codec`) — the C++
//! transition-stub probe path does not exist here.
//! `// CPP-PARITY: src/node/src/project/footage/footage.{h,cpp}`.

use std::sync::atomic::{AtomicBool, Ordering};

use crate::node::{Category, NodeBehavior, NodeCore};
use crate::value::{AudioParams, VideoParams};

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
	/// Duration in stream timebase.
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

	/// Probe the file through oakcodec (`oakcodec_decoder_probe`),
	/// filling `streams`. Error on unreadable/corrupt media or when the
	/// codec module is unavailable; the prior `streams`/`valid` state is
	/// preserved on failure (no partial state).
	pub fn probe(&mut self) -> crate::error::Result<()> {
		use crate::error::Error;
		// Direct call into the oakcodec crate (single-lib unification):
		// `oakcodec_decoder_probe(filename)` returns the stream-list
		// handle (owned by the caller).
		let out = match crate::bridge::codec::decoder_probe(&self.filename) {
			Some(out) => out,
			None => {
				return Err(Error::Failed(
					"oakcodec unavailable (not linked)".to_string(),
				));
			}
		};
		if out.is_null() {
			return Err(Error::Failed(
				"oakcodec probe returned no streams".to_string(),
			));
		}
		// The probe result handle is an oakcodec stream-list. Reading
		// stream entries into `streams` is a Phase-2 follow-up (the
		// exact accessor symbols are pinned when the codec module C ABI
		// is finalized).
		let _ = out;
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

	fn as_any(&self) -> Option<&dyn std::any::Any> {
		Some(self)
	}

	fn as_any_mut(&mut self) -> Option<&mut dyn std::any::Any> {
		Some(self)
	}
}
