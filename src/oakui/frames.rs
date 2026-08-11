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

	f32_rgba_to_bgra_image(width, height, &samples)
}

/// Downconverts an F32 RGBA frame (the engine pipeline's pixel format) to a
/// BGRA8 [`RenderImage`] for the viewers' CPU-frame path. Samples are
/// clamped to `0.0..=1.0` before quantization; `samples` must hold exactly
/// `width * height * 4` values (tightly packed rows).
///
/// Shared by the synthetic test pattern and the real engine's rendered
/// frames ([`super::real`]).
pub(crate) fn f32_rgba_to_bgra_image(width: u32, height: u32, samples: &[f32]) -> RenderImage {
	assert_eq!(
		samples.len(),
		(width * height * 4) as usize,
		"F32 RGBA frame must be tightly packed"
	);
	let mut bytes = Vec::with_capacity(samples.len());
	for i in (0..samples.len()).step_by(4) {
		bytes.push((samples[i + 2].clamp(0.0, 1.0) * 255.0) as u8); // B
		bytes.push((samples[i + 1].clamp(0.0, 1.0) * 255.0) as u8); // G
		bytes.push((samples[i].clamp(0.0, 1.0) * 255.0) as u8); // R
		bytes.push((samples[i + 3].clamp(0.0, 1.0) * 255.0) as u8); // A
	}
	let buffer = image::RgbaImage::from_raw(width, height, bytes).expect("BGRA frame");
	RenderImage::new(smallvec::SmallVec::from_elem(image::Frame::new(buffer), 1))
}

#[cfg(test)]
mod tests {
	use super::*;

	#[test]
	fn f32_rgba_converts_to_bgra_bytes() {
		// One red-ish pixel and one pixel exercising clamping.
		let samples = [1.0, 0.0, 0.5, 1.0, 2.0, -1.0, 0.25, 1.0];
		let image = f32_rgba_to_bgra_image(2, 1, &samples);
		let frame = image.as_bytes(0).expect("one frame"); // BGRA8, tightly packed
		assert_eq!(&frame[0..4], &[127, 0, 255, 255]);
		assert_eq!(&frame[4..8], &[63, 0, 255, 255]);
	}
}
