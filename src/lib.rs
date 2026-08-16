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
//! the `gpui_widgets` library, and an engine seam (`oakui`) with two
//! backends: the mock ([`oakui::MockEngine`]) feeding demo data, and the
//! real engine ([`oakui::RealEngine`]) driving the oak* module crates
//! directly (M14 R3: project open/save through the oaknode serializer,
//! timeline edits through the oaktimeline edit commands on the oakundo
//! global stack, the oakrender ticket arena for the viewers, the oaktask
//! export path, the oakcommon config store, and the oakstorage
//! write-through library — no `liboakengine` dylib, no C ABI).
//!
//! # Layout
//!
//! * [`app`] — the window shell: menu bar, dock layout, status bar, modal
//!   dialogs (file open/save-as, preferences, export), tick loop.
//! * [`dialogs`] — the preferences and export dialog content views.
//! * [`shortcuts`] — the keyboard shortcut table (keystroke → menu action).
//! * [`manager`] — the project manager window (M13 D4): the library browser
//!   with new / open / rename / duplicate / delete / import / export.
//! * [`panels`] — the dockable panels (viewers, timeline, inspector, ...).
//! * [`oakui`] — the engine gateway trait, the mock + real implementations,
//!   and the pure view-state logic (timecode, transport).
//!
//! # Running
//!
//! ```text
//! cargo run --bin oak-editor                        # the real engine, no project
//! cargo run --bin oak-editor -- path/to/project.ove    # open a project at startup
//! cargo run --bin oak-editor -- --mock              # the demo (mock) engine
//! cargo test                                         # unit tests (app + crates)
//! ```
//!
//! The engine backend is selected at startup: the real engine by default,
//! the mock with the `--mock` flag, `OAK_ENGINE=mock`, or the
//! `mock-engine` cargo feature.

pub mod app;
pub mod dialogs;
pub mod i18n;
pub mod manager;
pub mod oakui;
pub mod panels;
pub mod shortcuts;

/// The application entry point (called from `main.rs`).
pub fn run() {
	app::run();
}
