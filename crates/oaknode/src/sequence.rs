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

//! Sequence: the C++ `ViewerOutput`/`Sequence` pair — a node that owns
//! tracks, markers, work area, and playback caches.
//! `// CPP-PARITY: src/node/src/output/viewer/viewer.{h,cpp}`,
//! `// CPP-PARITY: src/node/src/project/sequence/sequence.{h,cpp}`.

use crate::id::NodeId;
use crate::node::{Category, NodeBehavior, NodeCore};
use crate::value::{AudioParams, VideoParams};

/// Sequence texture/samples input ids (ViewerOutput::k_texture_input /
/// k_samples_input) and the track input id format (Sequence::
/// k_track_input_format).
pub const TEXTURE_INPUT: &str = "tex_in";
pub const SAMPLES_INPUT: &str = "samples_in";
pub const TRACK_INPUT_FORMAT: &str = "track_in_%1";

/// Sequence behavior (viewer node).
pub struct SequenceBehavior {
	/// Track list node ids (video then audio, C++ order).
	pub track_lists: Vec<NodeId>,
	/// Timeline markers handle (oaktimeline, owned).
	pub markers: crate::handle::CHandle,
	/// Work area handle (oaktimeline, owned).
	pub workarea: crate::handle::CHandle,
	/// Length cache (C++ last_length_).
	pub last_length: oakcore_rs::Rational,
	/// Autocache toggles.
	pub autocache_video: bool,
	/// Audio autocache toggle.
	pub autocache_audio: bool,
	/// Playhead position (C++ ViewerOutput::playhead_).
	pub playhead: oakcore_rs::Rational,
	/// Video parameter streams.
	pub video_params: Vec<VideoParams>,
	/// Audio parameter streams.
	pub audio_params: Vec<AudioParams>,
}

impl SequenceBehavior {
	/// Empty sequence with zero tracks and no parameters.
	pub fn new() -> Self {
		SequenceBehavior {
			track_lists: Vec::new(),
			markers: crate::handle::CHandle::null(),
			workarea: crate::handle::CHandle::null(),
			last_length: oakcore_rs::Rational::new(0, 1),
			autocache_video: false,
			autocache_audio: false,
			playhead: oakcore_rs::Rational::new(0, 1),
			video_params: Vec::new(),
			audio_params: Vec::new(),
		}
	}

	/// Apply the default video/audio parameters (C++
	/// `ViewerOutput::set_default_parameters()`; the config lookups use
	/// oakcommon's defaults when the config module is absent).
	pub fn set_default_parameters(&mut self) {
		let width = crate::bridge::common::config_get_int("DefaultSequenceWidth", "", 1920)
			.unwrap_or(1920);
		let height = crate::bridge::common::config_get_int("DefaultSequenceHeight", "", 1080)
			.unwrap_or(1080);
		let sample_rate =
			crate::bridge::common::config_get_int("DefaultSequenceAudioFrequency", "", 48000)
				.unwrap_or(48000);
		let fps_num = crate::bridge::common::config_get_int("DefaultSequenceFrameRateNum", "", 30)
			.unwrap_or(30);
		let fps_den = crate::bridge::common::config_get_int("DefaultSequenceFrameRateDen", "", 1)
			.unwrap_or(1);

		self.video_params = vec![VideoParams {
			width,
			height,
			frame_rate: oakcore_rs::Rational::new(fps_num as i64, fps_den as i64),
			pixel_format: 4, // f32
			channels: 4,
		}];
		self.audio_params = vec![AudioParams {
			sample_rate,
			channel_layout: 0x3, // stereo
			format: 4,           // f32
		}];
	}

	/// Recompute the cached lengths from the track lists (C++
	/// `ViewerOutput::verify_length()`).
	pub fn verify_length(&mut self, lengths: (oakcore_rs::Rational, oakcore_rs::Rational, oakcore_rs::Rational)) {
		let (video, audio, overall) = lengths;
		self.last_length = overall;
		let _ = (video, audio);
	}

	/// Total stream counts.
	pub fn video_stream_count(&self) -> usize {
		self.video_params.len()
	}

	/// Audio stream count.
	pub fn audio_stream_count(&self) -> usize {
		self.audio_params.len()
	}
}

impl NodeBehavior for SequenceBehavior {
	fn name(&self) -> &str {
		"Sequence"
	}

	fn type_id(&self) -> &str {
		"org.olivevideoeditor.Olive.sequence"
	}

	fn categories(&self) -> &[Category] {
		&[Category::Output]
	}

	fn duplicate(&self, _core: &NodeCore) -> Option<Box<dyn NodeBehavior>> {
		Some(Box::new(SequenceBehavior::new()))
	}

	fn as_any(&self) -> Option<&dyn std::any::Any> {
		Some(self)
	}

	fn as_any_mut(&mut self) -> Option<&mut dyn std::any::Any> {
		Some(self)
	}
}

impl Default for SequenceBehavior {
	fn default() -> Self {
		SequenceBehavior::new()
	}
}
