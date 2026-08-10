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

//! `oakui` — the app layer's engine seam and view-state logic.
//!
//! This module is what the UI talks to when it needs something from "the
//! engine", plus the pure view-state logic the panels build on:
//!
//! * [`engine`] — the [`EngineGateway`](engine::EngineGateway) trait and the
//!   project/sequence model. The seam itself: panels and the shell hold an
//!   `Entity` whose type implements this trait and never care about the
//!   backend.
//! * [`mock`] — [`MockEngine`](mock::MockEngine) and
//!   [`MockClock`](mock::MockClock), the demo implementation feeding every
//!   widget's data-source trait.
//! * [`transport`] — the play/pause/step/seek state machine (pure, unit
//!   tested).
//! * [`timecode`] — timecode / duration / fps / resolution formatting (pure,
//!   unit tested).
//!
//! The real engine binding (the `liboakengine` C ABI through
//! `src/facade/rust`) will implement [`EngineGateway`](engine::EngineGateway)
//! later; nothing else in the crate needs to change for the swap.

pub mod engine;
pub mod mock;
pub mod timecode;
pub mod transport;

pub use engine::{EngineGateway, Monitor, Project, Sequence, VideoFormat};
pub use mock::{MockClock, MockEngine};
pub use transport::{PlayState, TransportState};
