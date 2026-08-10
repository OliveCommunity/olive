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

//! The engine gateway: the narrow Rust API through which the app layer talks
//! to the engine.
//!
//! # Why a gateway trait
//!
//! The UI must never depend on *how* the engine is implemented. Today the
//! only implementation is the mock ([`super::mock::MockEngine`]) feeding demo
//! data; later a real backend will bind the `liboakengine` C ABI
//! (`src/facade/rust`, the frozen `oakengine_*` exports) behind the *same*
//! trait. Swapping backends then touches only the wiring in
//! [`crate::app`] — the panels, the widgets and the view state stay as they
//! are.
//!
//! The trait is intentionally narrow: open a project, inspect the current
//! sequence, and drive the transport (play / pause / step / seek). Timeline
//! edits arrive as widget request events and are applied by the host through
//! methods on the engine type itself (see the `MockEngine` docs for the
//! current mapping), so they do not need to be part of this seam yet.
//!
//! Everything here is plain Rust — no C ABI, no FFI. The C-ABI binding is a
//! later concern of the real backend only.

use gpui::timeline::{Frame, FrameRate};
use std::path::PathBuf;

/// A monitor the transport can address.
///
/// Oak has two independent transports: the source monitor plays the clip
/// shown in the source viewer (素材查看器), the program monitor plays the
/// sequence shown in the program viewer (序列查看器).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum Monitor {
	/// The source (footage) monitor.
	Source,
	/// The program (sequence) monitor.
	Program,
}

/// A video format: resolution plus frame rate.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct VideoFormat {
	/// Width in pixels.
	pub width: u32,
	/// Height in pixels.
	pub height: u32,
	/// The frame rate (rational, e.g. 30000/1001 for NTSC 29.97).
	pub rate: FrameRate,
}

impl VideoFormat {
	/// The classic HD television format: 1920×1080 at 25 fps.
	pub fn hd_1080p25() -> Self {
		Self {
			width: 1920,
			height: 1080,
			rate: FrameRate::new(25, 1),
		}
	}
}

/// A project open in the engine.
#[derive(Debug, Clone, PartialEq)]
pub struct Project {
	/// The project's display name.
	pub name: String,
	/// The project file on disk (`.ove`).
	pub path: PathBuf,
}

/// The sequence currently open in the project.
#[derive(Debug, Clone, PartialEq)]
pub struct Sequence {
	/// The sequence's display name.
	pub name: String,
	/// The sequence's video format.
	pub format: VideoFormat,
	/// The sequence length in frames.
	pub length: Frame,
}

/// The engine gateway.
///
/// Implementations own the "engine" side of the app: project state, the
/// current sequence, and the transport. Query methods are pure reads;
/// mutating methods take a gpui [`Context`](gpui::Context) so the backend can
/// update its observable entities (clocks, models) and notify them.
pub trait EngineGateway: Sized {
	/// The currently open project, or `None` before any project is opened.
	fn project(&self) -> Option<&Project>;

	/// The current sequence of the open project, if any.
	fn current_sequence(&self) -> Option<&Sequence>;

	/// Open a project file. The backend loads it and becomes the source of
	/// truth for [`project`](EngineGateway::project) /
	/// [`current_sequence`](EngineGateway::current_sequence).
	fn open_project(&mut self, path: PathBuf, cx: &mut gpui::Context<Self>);

	/// Seek `monitor` to `frame` (clamped to the sequence).
	fn request_frame(&mut self, monitor: Monitor, frame: Frame, cx: &mut gpui::Context<Self>);

	/// Start playback on `monitor`.
	fn play(&mut self, monitor: Monitor, cx: &mut gpui::Context<Self>);

	/// Pause playback on `monitor`, leaving the playhead where it is.
	fn pause(&mut self, monitor: Monitor, cx: &mut gpui::Context<Self>);

	/// Step `monitor`'s playhead by `delta` frames (negative steps back).
	fn step(&mut self, monitor: Monitor, delta: i64, cx: &mut gpui::Context<Self>);

	/// Advance the playback clocks by one wall-clock tick. Called on a
	/// periodic timer while any monitor is playing.
	fn tick(&mut self, cx: &mut gpui::Context<Self>);
}
