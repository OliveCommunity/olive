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
}

impl FootageBehavior {
	/// Create for `filename` (unprobed).
	pub fn new(filename: &str) -> Self {
		todo!()
	}

	/// Probe the file through oakcodec (`oakcodec_decoder_probe`),
	/// filling `streams`. Error on unreadable/corrupt media.
	pub fn probe(&mut self) -> crate::error::Result<()> {
		todo!()
	}
}
