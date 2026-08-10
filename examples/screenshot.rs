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

//! Offscreen screenshot capture for the app window.
//!
//! Renders the full [`OakApp`] shell at 1600×900 (2× = 3200×1866 px) in an
//! offscreen macOS window and writes the PNG to
//! `app/rust/docs/screenshot-window.png`, using the same
//! [`VisualTestAppContext`] machinery the gpui visual tests use. The window
//! is created at `(-10000, -10000)` so nothing flickers on screen.
//!
//! Run it on the macOS main thread (examples run on the main thread, unlike
//! `#[test]` harness threads):
//!
//! ```text
//! cargo run --example screenshot               # 1600×900 → docs/screenshot-window.png
//! cargo run --example screenshot -- 1100 900   # any size (still overwrites the same file)
//! ```

use gpui::{px, size, AnyWindowHandle, AppContext, Result, VisualTestAppContext};
use gpui_platform::current_platform;
use oakapp::app::OakApp;

const DEFAULT_WIDTH: f32 = 1600.0;
const DEFAULT_HEIGHT: f32 = 900.0;
const OUT: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/docs/screenshot-window.png");

fn main() -> Result<()> {
	let args: Vec<String> = std::env::args().skip(1).collect();
	let width = args
		.first()
		.and_then(|s| s.parse().ok())
		.unwrap_or(DEFAULT_WIDTH);
	let height = args
		.get(1)
		.and_then(|s| s.parse().ok())
		.unwrap_or(DEFAULT_HEIGHT);

	let mut cx = VisualTestAppContext::new(current_platform(false));
	cx.update(|app| app.init_colors());

	let window = cx.open_offscreen_window(size(px(width), px(height)), |window, cx| {
		cx.new(|cx| OakApp::new(window, cx))
	})?;
	let handle: AnyWindowHandle = window.into();

	// Draw a few frames so the layout settles: the node editor fits its graph
	// once the canvas size is known and the viewers upload their first CPU
	// frame, both of which happen on the frame after the initial render.
	for _ in 0..4 {
		cx.run_until_parked();
		cx.update_window(handle, |_root, window, app| {
			let _ = window.draw(app);
		})?;
	}
	cx.run_until_parked();

	let image = cx.capture_screenshot(handle)?;
	std::fs::create_dir_all(std::path::Path::new(OUT).parent().unwrap())?;
	image.save(OUT)?;
	println!("wrote {OUT} ({}×{})", image.width(), image.height());
	Ok(())
}
