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

//! `oakapp` — the Rust application layer of Oak, built on the `gpui` UI
//! framework (the oak-gpui fork at `gpui/`).
//!
//! This is the start of the Rust rewrite of `app/` (Qt): a gpui window with
//! the main layout from the design (`design/`), dockable panels built from
//! the `gpui_widgets` library, and an engine seam (`oakui`) that currently
//! feeds demo data through [`MockEngine`](oakui::MockEngine).
//!
//! # Layout
//!
//! * [`app`] — the window shell: menu bar, dock layout, status bar, tick
//!   loop.
//! * [`panels`] — the dockable panels (viewers, timeline, inspector, ...).
//! * [`oakui`] — the engine gateway trait, the mock implementation, and the
//!   pure view-state logic (timecode, transport).
//!
//! # Running
//!
//! ```text
//! cargo run --bin oakapp        # the demo window
//! cargo test                    # unit tests (timecode, transport)
//! ```

pub mod app;
pub mod i18n;
pub mod oakui;
pub mod panels;

/// The application entry point (called from `main.rs`).
pub fn run() {
	app::run();
}
