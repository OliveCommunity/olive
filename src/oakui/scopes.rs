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

//! Scope analysis for the viewer scopes: derives the luma / chroma sample
//! streams the `gpui_widgets::scopes` widgets graph from an F32 RGBA frame
//! (the engine pipeline's pixel format).
//!
//! The analysis runs once per rendered frame, inside the same pass that
//! already touches every sample for the viewer downconvert, so a paused
//! viewer costs nothing and no frame is ever walked twice. The scope widgets
//! own the graphing math (histogram binning, waveform envelopes, vectorscope
//! projection); this module only turns pixels into their input samples.

use std::sync::Arc;

/// BT.709 luma coefficients.
const KR: f32 = 0.2126;
const KG: f32 = 0.7152;
const KB: f32 = 0.0722;

/// The scope samples of one frame: luma per pixel for the histogram /
/// waveform, `(Cb, Cr)` per pixel for the vectorscope.
///
/// Cheap to clone: both streams sit behind an [`Arc`], so handing the data
/// from the engine's frame cache to a panel copies two pointers.
#[derive(Debug, Clone, Default)]
pub struct ScopeData {
	/// Per-pixel luma in `0..=1` (BT.709), row-major.
	pub luma: Arc<Vec<f32>>,
	/// Per-pixel chroma `(Cb, Cr)` in `0..=1`, centered on `0.5`.
	pub chroma: Arc<Vec<(f32, f32)>>,
}

/// Analyzes one F32 RGBA frame into its [`ScopeData`]. `samples` must hold
/// exactly `width * height * 4` tightly packed values (the same contract as
/// [`super::frames::f32_rgba_to_bgra_image`]).
///
/// Out-of-gamut samples are clamped into `0..=1` per channel first, so the
/// scopes read the same values the viewer displays.
pub(crate) fn analyze_f32_rgba(width: u32, height: u32, samples: &[f32]) -> ScopeData {
	assert_eq!(
		samples.len(),
		(width * height * 4) as usize,
		"F32 RGBA frame must be tightly packed"
	);
	let pixels = (width * height) as usize;
	let mut luma = Vec::with_capacity(pixels);
	let mut chroma = Vec::with_capacity(pixels);
	for i in (0..samples.len()).step_by(4) {
		let r = samples[i].clamp(0.0, 1.0);
		let g = samples[i + 1].clamp(0.0, 1.0);
		let b = samples[i + 2].clamp(0.0, 1.0);
		let y = KR * r + KG * g + KB * b;
		// Cb/Cr normalized to 0..=1 (centered on 0.5) from the BT.709
		// coefficients: Cb = (B - Y) / (2(1 - Kb)) + 0.5, Cr likewise.
		let cb = 0.5 + (b - y) / (2.0 * (1.0 - KB));
		let cr = 0.5 + (r - y) / (2.0 * (1.0 - KR));
		luma.push(y);
		chroma.push((cb.clamp(0.0, 1.0), cr.clamp(0.0, 1.0)));
	}
	ScopeData {
		luma: Arc::new(luma),
		chroma: Arc::new(chroma),
	}
}

#[cfg(test)]
mod tests {
	use super::*;

	#[test]
	fn gray_pixels_have_neutral_chroma() {
		// One mid-gray pixel: luma equals the channel value, chroma is
		// neutral (0.5, 0.5).
		let samples = [0.5, 0.5, 0.5, 1.0];
		let data = analyze_f32_rgba(1, 1, &samples);
		assert!((data.luma[0] - 0.5).abs() < 1e-6);
		assert!((data.chroma[0].0 - 0.5).abs() < 1e-6);
		assert!((data.chroma[0].1 - 0.5).abs() < 1e-6);
	}

	#[test]
	fn pure_primaries_have_known_luma_and_chroma() {
		// Red, green, blue pixels in one row.
		let samples = [
			1.0, 0.0, 0.0, 1.0, // red
			0.0, 1.0, 0.0, 1.0, // green
			0.0, 0.0, 1.0, 1.0, // blue
		];
		let data = analyze_f32_rgba(3, 1, &samples);
		assert!((data.luma[0] - KR).abs() < 1e-6);
		assert!((data.luma[1] - KG).abs() < 1e-6);
		assert!((data.luma[2] - KB).abs() < 1e-6);
		// Pure red: Cb = 0.5 - Kr / (2(1 - Kb)), Cr saturates to 1.0.
		assert!((data.chroma[0].0 - (0.5 - KR / (2.0 * (1.0 - KB)))).abs() < 1e-6);
		assert!((data.chroma[0].1 - 1.0).abs() < 1e-6);
		// Pure blue is the mirror: Cb saturates to 1.0, Cr dives.
		assert!((data.chroma[2].0 - 1.0).abs() < 1e-6);
	}

	#[test]
	fn out_of_gamut_samples_clamp_like_the_viewer() {
		// A super-white and a negative channel clamp to the displayed value.
		let samples = [2.0, 2.0, 2.0, 1.0, -1.0, -1.0, -1.0, 1.0];
		let data = analyze_f32_rgba(2, 1, &samples);
		assert!((data.luma[0] - 1.0).abs() < 1e-6);
		assert!((data.luma[1] - 0.0).abs() < 1e-6);
	}

	#[test]
	fn analyzed_samples_feed_the_scope_math() {
		// A black top half and a white bottom half: the histogram puts every
		// sample into the two edge bins, and the waveform envelope (slicing
		// the row-major luma stream) rises from black to white across the
		// columns.
		let width = 8u32;
		let height = 4u32;
		let mut samples = vec![0.0f32; (width * height * 4) as usize];
		for y in 0..height {
			for x in 0..width {
				let i = ((y * width + x) * 4) as usize;
				let v = if y >= height / 2 { 1.0 } else { 0.0 };
				samples[i] = v;
				samples[i + 1] = v;
				samples[i + 2] = v;
				samples[i + 3] = 1.0;
			}
		}
		let data = analyze_f32_rgba(width, height, &samples);

		let bins = gpui_widgets::scopes::histogram_bins(&data.luma, 4);
		assert_eq!(bins, vec![16, 0, 0, 16]);

		let envelope = gpui_widgets::scopes::waveform_envelope(&data.luma, 2);
		assert_eq!(envelope.len(), 2);
		assert!((envelope[0].0 - 0.0).abs() < 1e-6 && (envelope[0].1 - 0.0).abs() < 1e-6);
		assert!((envelope[1].0 - 1.0).abs() < 1e-6 && (envelope[1].1 - 1.0).abs() < 1e-6);

		// Neutral gray chroma projects to the vectorscope's center.
		let points = gpui_widgets::scopes::vectorscope_points(&data.chroma);
		assert!(points
			.iter()
			.all(|&(u, v)| u.abs() < 1e-6 && v.abs() < 1e-6));
	}
}
