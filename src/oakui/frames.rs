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

//! The synthetic CPU viewer frame both engines display.
//!
//! The real engine delivers frames through the render worker (a separate
//! process speaking the NDJSON control-plane protocol, `oakengine::worker`),
//! which is out of scope for this increment. Until that transport is wired,
//! both the mock and the real engine feed the viewers the same SMPTE-style
//! test pattern, so playback is visibly moving while the engine metadata
//! (project / sequence / tracks) comes from the real facade in real mode.

use gpui::timeline::Frame;
use gpui::RenderImage;

/// Width of the synthetic test frame (a small proxy size; the real engine
/// will deliver full-resolution frames).
pub(crate) const SYNTH_FRAME_WIDTH: u32 = 384;
/// Height of the synthetic test frame.
pub(crate) const SYNTH_FRAME_HEIGHT: u32 = 216;

/// Generates a synthetic test frame: SMPTE-style color bars with a white
/// sweep whose x position follows `frame`, so transport playback shows up as
/// motion across the picture.
///
/// Samples are computed as F32 RGBA (mirroring the real engine's pixel
/// pipeline) and downconverted to BGRA8 for the viewer's CPU-frame path.
pub(crate) fn synthetic_frame(frame: Frame) -> RenderImage {
	let width = SYNTH_FRAME_WIDTH;
	let height = SYNTH_FRAME_HEIGHT;

	// F32 RGBA samples, then quantized to BGRA8 for the sprite atlas.
	let mut samples = vec![0.0f32; (width * height * 4) as usize];
	// SMPTE bars: 75% white, yellow, cyan, green, magenta, red, blue.
	let bars: [(f32, f32, f32); 7] = [
		(1.0, 1.0, 1.0),
		(1.0, 1.0, 0.0),
		(0.0, 1.0, 1.0),
		(0.0, 1.0, 0.0),
		(1.0, 0.0, 1.0),
		(1.0, 0.0, 0.0),
		(0.0, 0.0, 1.0),
	];
	// Bottom strip: blue, magenta, 75% white, black.
	let strip: [(f32, f32, f32); 4] = [
		(0.0, 0.0, 1.0),
		(1.0, 0.0, 1.0),
		(0.75, 0.75, 0.75),
		(0.0, 0.0, 0.0),
	];
	// The sweep moves 6 px per frame and wraps around the width, so
	// transport playback shows up as motion across the picture.
	let sweep = (frame.0 as f32 * 6.0) % width as f32;
	let bars_top = height as f32 * 0.66;

	for y in 0..height {
		for x in 0..width {
			let in_sweep = (x as f32 - sweep).abs() < 6.0;
			let color = if in_sweep {
				(1.0, 1.0, 1.0)
			} else if (y as f32) < bars_top {
				bars[((x as f32 / width as f32) * 7.0) as usize]
			} else {
				strip[((x as f32 / width as f32) * 4.0) as usize]
			};
			let i = ((y * width + x) * 4) as usize;
			samples[i] = color.0;
			samples[i + 1] = color.1;
			samples[i + 2] = color.2;
			samples[i + 3] = 1.0;
		}
	}

	let mut bytes = Vec::with_capacity((width * height * 4) as usize);
	for i in (0..samples.len()).step_by(4) {
		bytes.push((samples[i + 2] * 255.0) as u8); // B
		bytes.push((samples[i + 1] * 255.0) as u8); // G
		bytes.push((samples[i] * 255.0) as u8); // R
		bytes.push((samples[i + 3] * 255.0) as u8); // A
	}
	let buffer = image::RgbaImage::from_raw(width, height, bytes).expect("synthetic frame");
	RenderImage::new(smallvec::SmallVec::from_elem(image::Frame::new(buffer), 1))
}
