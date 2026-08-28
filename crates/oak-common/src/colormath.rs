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

//! Colorimetry for the ACEScg + F32 pipeline: the one place the matrices
//! and transfer functions live.
//!
//! Pipeline contract (project-properties driven):
//!
//! * input nodes decode any source colorspace into the **working space**
//!   (ACEScg linear, F32) via [`decode_to_working`];
//! * everything between input and output stays ACEScg + F32;
//! * the output node converts to the project's **output colorspace**
//!   ([`acescg_to_output`]) for export, and the display path converts to
//!   the display target ([`working_to_display_target`]) for presentation.
//!
//! All matrices are derived from the published primaries/white points at
//! build time (no hand-typed composite matrices to drift), with Bradford
//! chromatic adaptation between differing white points (D65 sources → the
//! ACES D60-ish white of AP1). Unit tests pin the results against the
//! published ACES transform values.

/// A 3x3 row-major matrix.
pub type Mat3 = [[f32; 3]; 3];

/// An xy chromaticity pair.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Xy {
	/// The x chromaticity coordinate.
	pub x: f32,
	/// The y chromaticity coordinate.
	pub y: f32,
}

impl Xy {
	/// Build a chromaticity.
	pub const fn new(x: f32, y: f32) -> Self {
		Self { x, y }
	}

	/// The XYZ tristimulus at unit luminance (Y = 1).
	fn xyz_unit_luminance(self) -> [f32; 3] {
		[self.x / self.y, 1.0, (1.0 - self.x - self.y) / self.y]
	}
}

/// The RGB primaries of a colorspace.
#[derive(Clone, Copy, Debug)]
pub struct Primaries {
	/// Red primary.
	pub red: Xy,
	/// Green primary.
	pub green: Xy,
	/// Blue primary.
	pub blue: Xy,
	/// White point.
	pub white: Xy,
}

/// D65 standard illuminant.
pub const WHITE_D65: Xy = Xy::new(0.3127, 0.3290);

/// The ACES white point (≈D60; the exact ACES specification values).
pub const WHITE_ACES: Xy = Xy::new(0.32168, 0.33767);

/// sRGB / Rec.709 primaries (D65).
pub const PRIMARIES_SRGB: Primaries = Primaries {
	red: Xy::new(0.640, 0.330),
	green: Xy::new(0.300, 0.600),
	blue: Xy::new(0.150, 0.060),
	white: WHITE_D65,
};

/// Display P3 primaries (D65).
pub const PRIMARIES_DISPLAY_P3: Primaries = Primaries {
	red: Xy::new(0.680, 0.320),
	green: Xy::new(0.265, 0.690),
	blue: Xy::new(0.150, 0.060),
	white: WHITE_D65,
};

/// Rec.2020 / BT.2020 primaries (D65).
pub const PRIMARIES_BT2020: Primaries = Primaries {
	red: Xy::new(0.708, 0.292),
	green: Xy::new(0.170, 0.797),
	blue: Xy::new(0.131, 0.046),
	white: WHITE_D65,
};

/// ACES AP1 primaries (the ACEScg gamut; ACES white).
pub const PRIMARIES_AP1: Primaries = Primaries {
	red: Xy::new(0.713, 0.293),
	green: Xy::new(0.165, 0.830),
	blue: Xy::new(0.128, 0.044),
	white: WHITE_ACES,
};

// ---------------------------------------------------------------------------
// Matrix plumbing
// ---------------------------------------------------------------------------

fn mat_mul(a: Mat3, b: Mat3) -> Mat3 {
	let mut out = [[0.0f32; 3]; 3];
	for r in 0..3 {
		for c in 0..3 {
			out[r][c] = a[r][0] * b[0][c] + a[r][1] * b[1][c] + a[r][2] * b[2][c];
		}
	}
	out
}

fn mat_vec(m: Mat3, v: [f32; 3]) -> [f32; 3] {
	[
		m[0][0] * v[0] + m[0][1] * v[1] + m[0][2] * v[2],
		m[1][0] * v[0] + m[1][1] * v[1] + m[1][2] * v[2],
		m[2][0] * v[0] + m[2][1] * v[1] + m[2][2] * v[2],
	]
}

fn mat_inv(m: Mat3) -> Mat3 {
	let det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
		- m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
		+ m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
	let inv_det = 1.0 / det;
	[
		[
			(m[1][1] * m[2][2] - m[1][2] * m[2][1]) * inv_det,
			(m[0][2] * m[2][1] - m[0][1] * m[2][2]) * inv_det,
			(m[0][1] * m[1][2] - m[0][2] * m[1][1]) * inv_det,
		],
		[
			(m[1][2] * m[2][0] - m[1][0] * m[2][2]) * inv_det,
			(m[0][0] * m[2][2] - m[0][2] * m[2][0]) * inv_det,
			(m[0][2] * m[1][0] - m[0][0] * m[1][2]) * inv_det,
		],
		[
			(m[1][0] * m[2][1] - m[1][1] * m[2][0]) * inv_det,
			(m[0][1] * m[2][0] - m[0][0] * m[2][1]) * inv_det,
			(m[0][0] * m[1][1] - m[0][1] * m[1][0]) * inv_det,
		],
	]
}

fn mat_diag(d: [f32; 3]) -> Mat3 {
	[[d[0], 0.0, 0.0], [0.0, d[1], 0.0], [0.0, 0.0, d[2]]]
}

/// The Bradford cone-response matrix (the ICC-recommended adaptation used
/// by the ACES transforms as well).
const BRADFORD_CONE: Mat3 = [
	[0.8951, 0.2664, -0.1614],
	[-0.7502, 1.7135, 0.0367],
	[0.0389, -0.0685, 1.0296],
];

/// Bradford chromatic adaptation from one white point to another.
pub fn chromatic_adaptation(src_white: Xy, dst_white: Xy) -> Mat3 {
	let cone = BRADFORD_CONE;
	let cone_inv = mat_inv(cone);
	let src = mat_vec(cone, src_white.xyz_unit_luminance());
	let dst = mat_vec(cone, dst_white.xyz_unit_luminance());
	let scale = [dst[0] / src[0], dst[1] / src[1], dst[2] / src[2]];
	mat_mul(mat_mul(cone_inv, mat_diag(scale)), cone)
}

/// The RGB→CIE XYZ matrix of a colorspace (columns are the primaries'
/// tristimulus scaled so the white point maps to unit luminance).
pub fn rgb_to_xyz_matrix(primaries: Primaries) -> Mat3 {
	let raw = [
		[
			primaries.red.xyz_unit_luminance()[0],
			primaries.green.xyz_unit_luminance()[0],
			primaries.blue.xyz_unit_luminance()[0],
		],
		[
			primaries.red.xyz_unit_luminance()[1],
			primaries.green.xyz_unit_luminance()[1],
			primaries.blue.xyz_unit_luminance()[1],
		],
		[
			primaries.red.xyz_unit_luminance()[2],
			primaries.green.xyz_unit_luminance()[2],
			primaries.blue.xyz_unit_luminance()[2],
		],
	];
	let white_xyz = primaries.white.xyz_unit_luminance();
	let s = mat_vec(mat_inv(raw), white_xyz);
	mat_mul(raw, mat_diag(s))
}

/// Composite RGB(src) → RGB(dst) matrix: src → XYZ, chromatic adaptation
/// when the white points differ, XYZ → dst.
pub fn rgb_to_rgb_matrix(src: Primaries, dst: Primaries) -> Mat3 {
	let to_xyz = rgb_to_xyz_matrix(src);
	let from_xyz = mat_inv(rgb_to_xyz_matrix(dst));
	let middle = if src.white != dst.white {
		chromatic_adaptation(src.white, dst.white)
	} else {
		[[1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]]
	};
	mat_mul(mat_mul(from_xyz, middle), to_xyz)
}

/// Apply a 3x3 matrix to the RGB channels of one pixel in place.
#[inline]
fn apply_mat(m: Mat3, px: &mut [f32]) {
	let r = px[0];
	let g = px[1];
	let b = px[2];
	px[0] = m[0][0] * r + m[0][1] * g + m[0][2] * b;
	px[1] = m[1][0] * r + m[1][1] * g + m[1][2] * b;
	px[2] = m[2][0] * r + m[2][1] * g + m[2][2] * b;
}

// ---------------------------------------------------------------------------
// Working / output spaces (the project-properties vocabulary)
// ---------------------------------------------------------------------------

/// The pipeline working colorspace (project property). ACEScg is the
/// industrial scene-linear working space; `SrgbLegacy` keeps the old
/// display-referred sRGB passthrough for compatibility / debugging.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Default)]
pub enum WorkingColorSpace {
	/// ACEScg (AP1 primaries, linear transfer, F32) — the default.
	#[default]
	AcesCg,
	/// Legacy: gamma-encoded sRGB values pass through untransformed.
	SrgbLegacy,
}

impl WorkingColorSpace {
	/// Parse the persisted project-setting value.
	pub fn from_setting(value: &str) -> Self {
		match value.to_ascii_lowercase().as_str() {
			"srgb" | "srgb_legacy" | "legacy" => WorkingColorSpace::SrgbLegacy,
			_ => WorkingColorSpace::AcesCg,
		}
	}

	/// The value persisted into the project settings.
	pub fn as_setting(self) -> &'static str {
		match self {
			WorkingColorSpace::AcesCg => "acescg",
			WorkingColorSpace::SrgbLegacy => "srgb_legacy",
		}
	}
}

/// The output gamut (project property / export target).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Default)]
pub enum OutputGamut {
	/// Rec.709 / sRGB primaries (D65).
	#[default]
	Srgb,
	/// Display P3 primaries (D65).
	DisplayP3,
	/// Rec.2020 / BT.2020 primaries (D65).
	Bt2020,
}

impl OutputGamut {
	/// Parse the persisted project-setting value.
	pub fn from_setting(value: &str) -> Self {
		match value.to_ascii_lowercase().as_str() {
			"displayp3" | "p3" => OutputGamut::DisplayP3,
			"bt2020" | "rec2020" | "2020" => OutputGamut::Bt2020,
			_ => OutputGamut::Srgb,
		}
	}

	/// The value persisted into the project settings.
	pub fn as_setting(self) -> &'static str {
		match self {
			OutputGamut::Srgb => "srgb",
			OutputGamut::DisplayP3 => "displayp3",
			OutputGamut::Bt2020 => "bt2020",
		}
	}

	/// The primaries of this gamut.
	pub fn primaries(self) -> Primaries {
		match self {
			OutputGamut::Srgb => PRIMARIES_SRGB,
			OutputGamut::DisplayP3 => PRIMARIES_DISPLAY_P3,
			OutputGamut::Bt2020 => PRIMARIES_BT2020,
		}
	}

	/// The H.273 / `AVCOL_PRI_*` code point for container tagging (the
	/// mov `colr` atom / H.264-HEVC VUI).
	pub fn av_color_primaries(self) -> i32 {
		match self {
			OutputGamut::Srgb => 1,       // BT.709
			OutputGamut::DisplayP3 => 12, // SMPTE EG 432-1 (Display P3)
			OutputGamut::Bt2020 => 9,     // BT.2020
		}
	}

	/// The H.273 / `AVCOL_SPC_*` matrix-coefficients code point for the
	/// encoder's RGB→YCbCr conversion tag.
	pub fn av_color_space(self) -> i32 {
		match self {
			OutputGamut::Srgb | OutputGamut::DisplayP3 => 1, // BT.709
			OutputGamut::Bt2020 => 9,                        // BT.2020 NCL
		}
	}
}

/// The output transfer characteristic (project property / export target).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Default)]
pub enum OutputTransfer {
	/// The sRGB piecewise transfer (IEC 61966-2-1).
	#[default]
	Srgb,
	/// Pure gamma 2.2 power function.
	Gamma22,
	/// SMPTE ST 2084 (PQ), normalized so 1.0 = 10 000 nits.
	Pq,
	/// ARIB STD-B67 Hybrid Log-Gamma.
	Hlg,
}

impl OutputTransfer {
	/// Parse the persisted project-setting value.
	pub fn from_setting(value: &str) -> Self {
		match value.to_ascii_lowercase().as_str() {
			"gamma22" | "2.2" => OutputTransfer::Gamma22,
			"pq" | "st2084" => OutputTransfer::Pq,
			"hlg" => OutputTransfer::Hlg,
			_ => OutputTransfer::Srgb,
		}
	}

	/// The value persisted into the project settings.
	pub fn as_setting(self) -> &'static str {
		match self {
			OutputTransfer::Srgb => "srgb",
			OutputTransfer::Gamma22 => "gamma22",
			OutputTransfer::Pq => "pq",
			OutputTransfer::Hlg => "hlg",
		}
	}

	/// The H.273 / `AVCOL_TRC_*` code point for container tagging.
	pub fn av_color_trc(self) -> i32 {
		match self {
			OutputTransfer::Srgb => 13,   // IEC 61966-2-1 (sRGB)
			OutputTransfer::Gamma22 => 4, // pure gamma 2.2
			OutputTransfer::Pq => 16,     // SMPTE ST 2084 (PQ)
			OutputTransfer::Hlg => 18,    // ARIB STD-B67 (HLG)
		}
	}
}

/// The complete output colorspace specification (the project's delivery
/// target; sRGB by default).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Default)]
pub struct OutputColorSpec {
	/// The output gamut.
	pub gamut: OutputGamut,
	/// The output transfer characteristic.
	pub transfer: OutputTransfer,
}

impl OutputColorSpec {
	/// The spec from persisted project-setting values.
	pub fn from_settings(gamut: &str, transfer: &str) -> Self {
		Self {
			gamut: OutputGamut::from_setting(gamut),
			transfer: OutputTransfer::from_setting(transfer),
		}
	}
}

// ---------------------------------------------------------------------------
// Transfer functions (component-wise)
// ---------------------------------------------------------------------------

/// sRGB OETF: linear [0,∞) → gamma-encoded code (the piecewise IEC curve,
/// mirrored for negatives so scene values survive).
pub fn srgb_oetf(v: f32) -> f32 {
	let sign = if v < 0.0 { -1.0 } else { 1.0 };
	let v = v.abs();
	sign * if v <= 0.0031308 {
		12.92 * v
	} else {
		1.055 * v.powf(1.0 / 2.4) - 0.055
	}
}

/// sRGB EOTF: gamma-encoded code → linear.
pub fn srgb_eotf(v: f32) -> f32 {
	let sign = if v < 0.0 { -1.0 } else { 1.0 };
	let v = v.abs();
	sign * if v <= 0.04045 {
		v / 12.92
	} else {
		((v + 0.055) / 1.055).powf(2.4)
	}
}

/// Pure-power gamma OETF (linear → code).
pub fn gamma_oetf(v: f32, gamma: f32) -> f32 {
	let sign = if v < 0.0 { -1.0 } else { 1.0 };
	sign * v.abs().powf(1.0 / gamma)
}

/// Pure-power gamma EOTF (code → linear).
pub fn gamma_eotf(v: f32, gamma: f32) -> f32 {
	let sign = if v < 0.0 { -1.0 } else { 1.0 };
	sign * v.abs().powf(gamma)
}

/// SMPTE ST 2084 (PQ) constants (normalized: code 1.0 = 10 000 nits).
const PQ_M1: f32 = 2610.0 / 16384.0;
const PQ_M2: f32 = (2523.0 / 4096.0) * 128.0;
const PQ_C1: f32 = 3424.0 / 4096.0;
const PQ_C2: f32 = (2413.0 / 4096.0) * 32.0;
const PQ_C3: f32 = (2392.0 / 4096.0) * 32.0;

/// PQ EOTF: code → linear scene/display value (1.0 = 10 000 nits).
pub fn pq_eotf(v: f32) -> f32 {
	let sign = if v < 0.0 { -1.0 } else { 1.0 };
	let v = v.abs().clamp(0.0, 1.0).powf(1.0 / PQ_M2);
	let num = (v - PQ_C1).max(0.0);
	let den = PQ_C2 - PQ_C3 * v;
	sign * (num / den).powf(1.0 / PQ_M1)
}

/// PQ OETF (inverse of [`pq_eotf`]).
pub fn pq_oetf(v: f32) -> f32 {
	let sign = if v < 0.0 { -1.0 } else { 1.0 };
	let y = v.abs().powf(PQ_M1);
	let num = PQ_C1 + PQ_C2 * y;
	let den = 1.0 + PQ_C3 * y;
	sign * (num / den).powf(PQ_M2)
}

/// HLG constants (ARIB STD-B67). `HLG_C` is the precomputed value of
/// `0.5 - a·ln(4a)` (a non-const expression).
const HLG_A: f32 = 0.17883277;
const HLG_B: f32 = 1.0 - 4.0 * HLG_A; // 0.28466892
const HLG_C: f32 = 0.55992546;

/// HLG inverse OETF: code → linear (scene-referred half, display 1.0).
pub fn hlg_eotf(v: f32) -> f32 {
	let sign = if v < 0.0 { -1.0 } else { 1.0 };
	let v = v.abs();
	sign * if v <= 0.5 {
		v * v / 3.0
	} else {
		(((v - HLG_C) / HLG_A).exp() + HLG_B) / 12.0
	}
}

/// HLG OETF (inverse of [`hlg_eotf`]).
pub fn hlg_oetf(v: f32) -> f32 {
	let sign = if v < 0.0 { -1.0 } else { 1.0 };
	let v = v.abs();
	sign * if v <= 1.0 / 12.0 {
		(3.0 * v).sqrt()
	} else {
		HLG_A * (12.0 * v - HLG_B).ln() + HLG_C
	}
}

// ---------------------------------------------------------------------------
// Source decode characterization
// ---------------------------------------------------------------------------

/// The colorimetry of a decoded source, as read from the container /
/// codec metadata (H.264/HEVC VUI, FFmpeg `AVCodecContext` fields).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum SourcePrimaries {
	/// BT.709 / sRGB primaries (the SDR HDTV default).
	Bt709,
	/// BT.2020 primaries (UHD / wide-gamut sources).
	Bt2020,
	/// Display P3 primaries.
	DisplayP3,
	/// Unknown primaries — treated as BT.709 (the documented fallback).
	Unknown,
}

/// The transfer characteristic of a decoded source.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum SourceTransfer {
	/// The sRGB piecewise EOTF (IEC 61966-2-1), applied at decode.
	SdrGamma,
	/// BT.1886 display-referred EOTF (pure power 2.4) — the SDR display
	/// reference for BT.709 / BT.2020 sources.
	Gamma24,
	/// Pure power 2.2 gamma.
	Gamma22,
	/// Pure power 2.8 gamma.
	Gamma28,
	/// SMPTE ST 2084 (PQ) HDR.
	Pq,
	/// ARIB STD-B67 HLG HDR.
	Hlg,
	/// Linear-light source (EXR-style).
	Linear,
	/// Unknown transfer — treated as SDR gamma.
	Unknown,
}

/// Map FFmpeg/libav `color_primaries` values to [`SourcePrimaries`].
/// (AVCOL_PRI_* numbering, H.273 ISO codes.)
pub fn source_primaries_from_av(color_primaries: i32) -> SourcePrimaries {
	match color_primaries {
		1 => SourcePrimaries::Bt709, // BT.709
		9 => SourcePrimaries::Bt2020, // BT.2020
		11 => SourcePrimaries::DisplayP3, // SMPTE RP 431-2 (DCI-P3)
		12 => SourcePrimaries::DisplayP3, // SMPTE EG 432-1 (Display P3)
		_ => SourcePrimaries::Unknown,
	}
}

/// Map FFmpeg/libav `color_trc` values to [`SourceTransfer`].
/// (AVCOL_TRC_* numbering, H.273 ISO codes.)
pub fn source_transfer_from_av(color_trc: i32) -> SourceTransfer {
	match color_trc {
		1 => SourceTransfer::Gamma24,   // BT.709
		4 => SourceTransfer::Gamma22,   // gamma 2.2
		5 => SourceTransfer::Gamma28,   // gamma 2.8
		6 => SourceTransfer::Gamma24,   // SMPTE 170M
		13 => SourceTransfer::SdrGamma, // sRGB (its EOTF is applied at decode)
		14 => SourceTransfer::Gamma24,  // BT.2020 10-bit
		15 => SourceTransfer::Gamma24,  // BT.2020 12-bit
		16 => SourceTransfer::Pq,       // SMPTE ST 2084
		18 => SourceTransfer::Hlg,      // ARIB STD-B67
		8 => SourceTransfer::Linear,    // linear
		_ => SourceTransfer::Unknown,
	}
}

impl SourcePrimaries {
	/// The primaries struct for this source.
	pub fn primaries(self) -> Primaries {
		match self {
			SourcePrimaries::Bt709 | SourcePrimaries::Unknown => PRIMARIES_SRGB,
			SourcePrimaries::Bt2020 => PRIMARIES_BT2020,
			SourcePrimaries::DisplayP3 => PRIMARIES_DISPLAY_P3,
		}
	}
}

// ---------------------------------------------------------------------------
// The pipeline transforms (F32 RGBA, tightly packed or in LE byte buffers)
// ---------------------------------------------------------------------------

/// Decode-direction transform: gamma-encoded source RGB (in the source's
/// own primaries) → ACEScg linear. `samples` is an F32 RGBA buffer,
/// transformed in place.
pub fn decode_to_acescg(
	samples: &mut [f32],
	primaries: SourcePrimaries,
	transfer: SourceTransfer,
) {
	// Stage 1: linearize in the source's own primaries.
	match transfer {
		SourceTransfer::SdrGamma | SourceTransfer::Unknown => {
			for px in samples.chunks_exact_mut(4) {
				for c in 0..3 {
					px[c] = srgb_eotf(px[c]);
				}
			}
		}
		SourceTransfer::Gamma24 | SourceTransfer::Gamma22 | SourceTransfer::Gamma28 => {
			let gamma = match transfer {
				SourceTransfer::Gamma24 => 2.4,
				SourceTransfer::Gamma22 => 2.2,
				_ => 2.8,
			};
			for px in samples.chunks_exact_mut(4) {
				for c in 0..3 {
					px[c] = gamma_eotf(px[c], gamma);
				}
			}
		}
		SourceTransfer::Pq => {
			for px in samples.chunks_exact_mut(4) {
				for c in 0..3 {
					px[c] = pq_eotf(px[c]);
				}
			}
		}
		SourceTransfer::Hlg => {
			for px in samples.chunks_exact_mut(4) {
				for c in 0..3 {
					px[c] = hlg_eotf(px[c]);
				}
			}
		}
		SourceTransfer::Linear => {}
	}

	// Stage 2: source primaries → AP1 (with chromatic adaptation).
	let matrix = rgb_to_rgb_matrix(primaries.primaries(), PRIMARIES_AP1);
	for px in samples.chunks_exact_mut(4) {
		apply_mat(matrix, px);
	}
}

/// Decode-direction transform on a little-endian F32 RGBA byte buffer
/// (`pixels * 16` bytes). Avoids alignment requirements of the pipeline's
/// `Vec<u8>` frame storage.
pub fn decode_to_acescg_bytes(
	bytes: &mut [u8],
	pixels: usize,
	primaries: SourcePrimaries,
	transfer: SourceTransfer,
) {
	if bytes.len() < pixels * 16 {
		return;
	}
	let linearize: fn(f32) -> f32 = match transfer {
		SourceTransfer::SdrGamma | SourceTransfer::Unknown => srgb_eotf,
		SourceTransfer::Gamma24 => |v| gamma_eotf(v, 2.4),
		SourceTransfer::Gamma22 => |v| gamma_eotf(v, 2.2),
		SourceTransfer::Gamma28 => |v| gamma_eotf(v, 2.8),
		SourceTransfer::Pq => pq_eotf,
		SourceTransfer::Hlg => hlg_eotf,
		SourceTransfer::Linear => |v| v,
	};
	let matrix = rgb_to_rgb_matrix(primaries.primaries(), PRIMARIES_AP1);
	for i in 0..pixels {
		let off = i * 16;
		let mut px = [
			f32::from_le_bytes(bytes[off..off + 4].try_into().unwrap()),
			f32::from_le_bytes(bytes[off + 4..off + 8].try_into().unwrap()),
			f32::from_le_bytes(bytes[off + 8..off + 12].try_into().unwrap()),
			f32::from_le_bytes(bytes[off + 12..off + 16].try_into().unwrap()),
		];
		for c in 0..3 {
			px[c] = linearize(px[c]);
		}
		apply_mat(matrix, &mut px);
		bytes[off..off + 4].copy_from_slice(&px[0].to_le_bytes());
		bytes[off + 4..off + 8].copy_from_slice(&px[1].to_le_bytes());
		bytes[off + 8..off + 12].copy_from_slice(&px[2].to_le_bytes());
		bytes[off + 12..off + 16].copy_from_slice(&px[3].to_le_bytes());
	}
}

/// Output-direction transform: ACEScg linear → the output colorspace
/// (gamut matrix + transfer encoding). In place.
///
/// Gamut mapping is the contract's P1 semantics (simple clip): after the
/// AP1 → target-gamut matrix, the RGB channels are clamped to [0, 1] before
/// the transfer encoding, so out-of-gamut values are clipped rather than
/// wrapped. This applies to HDR targets too (PQ/HLG code 1.0 = 10 000 nits);
/// alpha is untouched.
pub fn acescg_to_output(samples: &mut [f32], spec: OutputColorSpec) {
	let matrix = rgb_to_rgb_matrix(PRIMARIES_AP1, spec.gamut.primaries());
	for px in samples.chunks_exact_mut(4) {
		apply_mat(matrix, px);
		for c in 0..3 {
			px[c] = px[c].clamp(0.0, 1.0);
		}
	}
	apply_transfer_oetf(samples, spec.transfer);
}

/// Output-direction transform on a little-endian F32 RGBA byte buffer
/// (`pixels * 16` bytes) — the worker's end-of-pipe conversion before
/// 8-bit quantization. RGB is clamped to [0, 1] after the gamut matrix and
/// before the transfer encoding, exactly as in [`acescg_to_output`].
pub fn acescg_to_output_bytes(bytes: &mut [u8], pixels: usize, spec: OutputColorSpec) {
	if bytes.len() < pixels * 16 {
		return;
	}
	let matrix = rgb_to_rgb_matrix(PRIMARIES_AP1, spec.gamut.primaries());
	let encode: fn(f32) -> f32 = match spec.transfer {
		OutputTransfer::Srgb => srgb_oetf,
		OutputTransfer::Gamma22 => |v| gamma_oetf(v, 2.2),
		OutputTransfer::Pq => pq_oetf,
		OutputTransfer::Hlg => hlg_oetf,
	};
	for i in 0..pixels {
		let off = i * 16;
		let mut px = [
			f32::from_le_bytes(bytes[off..off + 4].try_into().unwrap()),
			f32::from_le_bytes(bytes[off + 4..off + 8].try_into().unwrap()),
			f32::from_le_bytes(bytes[off + 8..off + 12].try_into().unwrap()),
			f32::from_le_bytes(bytes[off + 12..off + 16].try_into().unwrap()),
		];
		apply_mat(matrix, &mut px);
		for c in 0..3 {
			px[c] = px[c].clamp(0.0, 1.0);
		}
		for c in 0..3 {
			px[c] = encode(px[c]);
		}
		bytes[off..off + 4].copy_from_slice(&px[0].to_le_bytes());
		bytes[off + 4..off + 8].copy_from_slice(&px[1].to_le_bytes());
		bytes[off + 8..off + 12].copy_from_slice(&px[2].to_le_bytes());
		bytes[off + 12..off + 16].copy_from_slice(&px[3].to_le_bytes());
	}
}

/// Apply just the transfer encoding of `transfer` (linear → code). In place
/// on the RGB channels.
pub fn apply_transfer_oetf(samples: &mut [f32], transfer: OutputTransfer) {
	match transfer {
		OutputTransfer::Srgb => {
			for px in samples.chunks_exact_mut(4) {
				for c in 0..3 {
					px[c] = srgb_oetf(px[c]);
				}
			}
		}
		OutputTransfer::Gamma22 => {
			for px in samples.chunks_exact_mut(4) {
				for c in 0..3 {
					px[c] = gamma_oetf(px[c], 2.2);
				}
			}
		}
		OutputTransfer::Pq => {
			for px in samples.chunks_exact_mut(4) {
				for c in 0..3 {
					px[c] = pq_oetf(px[c]);
				}
			}
		}
		OutputTransfer::Hlg => {
			for px in samples.chunks_exact_mut(4) {
				for c in 0..3 {
					px[c] = hlg_oetf(px[c]);
				}
			}
		}
	}
}

/// The presentation transform: working space → the display target colorspace
/// (the project's output spec). With the ACEScg working space this is the
/// same chain as [`acescg_to_output`]; with the legacy sRGB working space it
/// is a pass-through (the content already IS display-referred sRGB).
pub fn working_to_display_target(
	samples: &mut [f32],
	working: WorkingColorSpace,
	spec: OutputColorSpec,
) {
	match working {
		WorkingColorSpace::AcesCg => acescg_to_output(samples, spec),
		WorkingColorSpace::SrgbLegacy => {}
	}
}

// ---------------------------------------------------------------------------
// Content → CIE XYZ (D65)
// ---------------------------------------------------------------------------

/// Encoded content in `spec` → CIE XYZ (D65, unit luminance 1.0), in place
/// on F32 RGBA. This feeds the self-managed display ICC path with its input
/// without depending on an OCIO named colorspace: linearize per the spec's
/// transfer (Srgb → sRGB EOTF, Gamma22 → pure power 2.2, Pq → ST 2084,
/// Hlg → HLG), then convert RGB → XYZ through the gamut's own matrix. All
/// output gamuts share the D65 white point, so no chromatic adaptation is
/// needed; `rgb_to_xyz_matrix` already normalizes the white point to unit
/// luminance.
pub fn output_spec_to_xyz_d65(samples: &mut [f32], spec: OutputColorSpec) {
	let linearize: fn(f32) -> f32 = match spec.transfer {
		OutputTransfer::Srgb => srgb_eotf,
		OutputTransfer::Gamma22 => |v| gamma_eotf(v, 2.2),
		OutputTransfer::Pq => pq_eotf,
		OutputTransfer::Hlg => hlg_eotf,
	};
	let matrix = rgb_to_xyz_matrix(spec.gamut.primaries());
	for px in samples.chunks_exact_mut(4) {
		for c in 0..3 {
			px[c] = linearize(px[c]);
		}
		apply_mat(matrix, px);
	}
}

// ---------------------------------------------------------------------------
// YUV → RGB (F32) decode fallback
// ---------------------------------------------------------------------------

/// The YUV matrix-coefficient set (H.273 / `AVCOL_SPC_*`).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum YuvMatrix {
	/// ITU-R BT.601 (SD) coefficients.
	Bt601,
	/// ITU-R BT.709 (HD) coefficients.
	Bt709,
	/// ITU-R BT.2020 (UHD) coefficients.
	Bt2020,
}

impl YuvMatrix {
	/// The (Kr, Kb) luma-coefficient pair of this matrix.
	fn kr_kb(self) -> (f32, f32) {
		match self {
			YuvMatrix::Bt601 => (0.299, 0.114),
			YuvMatrix::Bt709 => (0.2126, 0.0722),
			YuvMatrix::Bt2020 => (0.2627, 0.0593),
		}
	}
}

/// Convert a 16-bit little-endian planar YUV 4:4:4 frame to interleaved F32
/// RGBA (alpha 1.0). This is the decode fallback path: swscale cannot emit
/// RGBA64/F32 for some configurations and aborts, so the matrix conversion
/// happens here in Rust.
///
/// Each plane is a `height × stride` byte region of native-endian `u16`
/// samples (`stride ≥ width * 2`; padding between rows is skipped). Code
/// values are expanded per `full_range` before the matrix conversion:
///
/// * limited: Y ∈ [4096, 60160] (16-bit 16/235 shifted by 8), C ∈
///   [4096, 61440] with the neutral point at 32768;
/// * full: Y ∈ [0, 65535], C centered on 32768.
///
/// R/G/B follow from (Kr, Kb): R = Y' + 2(1−Kr)·Cr', B = Y' + 2(1−Kb)·Cb',
/// G = (Y' − Kr·R − Kb·B)/(1−Kr−Kb). Returns early (no panic, `out`
/// untouched) when any plane or `out` is too short for the frame.
pub fn yuv444p16_to_rgb_f32(
	y_plane: &[u8],
	y_stride: usize,
	u_plane: &[u8],
	u_stride: usize,
	v_plane: &[u8],
	v_stride: usize,
	width: usize,
	height: usize,
	matrix: YuvMatrix,
	full_range: bool,
	out: &mut [f32],
) {
	if out.len() < width.saturating_mul(height).saturating_mul(4) {
		return;
	}
	// Each plane must hold (height - 1) full strides plus the last row's
	// `width` samples.
	let row_bytes = width.saturating_mul(2);
	let rows_after = height.saturating_sub(1);
	if y_plane.len() < rows_after.saturating_mul(y_stride).saturating_add(row_bytes)
		|| u_plane.len() < rows_after.saturating_mul(u_stride).saturating_add(row_bytes)
		|| v_plane.len() < rows_after.saturating_mul(v_stride).saturating_add(row_bytes)
	{
		return;
	}
	let (kr, kb) = matrix.kr_kb();
	let kg = 1.0 - kr - kb;
	// Code → normalized: limited luma spans 219<<8 above 16<<8, chroma spans
	// 224<<8 above its 32768 center; full-range everything spans 65535.
	let (y_offset, c_offset, scale) = if full_range {
		(0.0f32, 32768.0f32, 1.0 / 65535.0)
	} else {
		(4096.0f32, 32768.0f32, 1.0 / 57344.0)
	};
	let y_scale = if full_range { 1.0 / 65535.0 } else { 1.0 / 56064.0 };
	for row in 0..height {
		let y_row = row * y_stride;
		let u_row = row * u_stride;
		let v_row = row * v_stride;
		for x in 0..width {
			let off = x * 2;
			let y = u16::from_le_bytes([y_plane[y_row + off], y_plane[y_row + off + 1]]) as f32;
			let cb = u16::from_le_bytes([u_plane[u_row + off], u_plane[u_row + off + 1]]) as f32;
			let cr = u16::from_le_bytes([v_plane[v_row + off], v_plane[v_row + off + 1]]) as f32;
			let yn = (y - y_offset) * y_scale;
			let cbn = (cb - c_offset) * scale;
			let crn = (cr - c_offset) * scale;
			let r = yn + 2.0 * (1.0 - kr) * crn;
			let b = yn + 2.0 * (1.0 - kb) * cbn;
			let g = (yn - kr * r - kb * b) / kg;
			let px = (row * width + x) * 4;
			out[px] = r;
			out[px + 1] = g;
			out[px + 2] = b;
			out[px + 3] = 1.0;
		}
	}
}

#[cfg(test)]
mod tests {
	use super::*;

	fn approx(a: f32, b: f32, eps: f32) -> bool {
		(a - b).abs() < eps
	}

	#[test]
	fn rgb_to_xyz_srgb_white_is_unit_luminance() {
		let m = rgb_to_xyz_matrix(PRIMARIES_SRGB);
		let white = mat_vec(m, [1.0, 1.0, 1.0]);
		let expected = WHITE_D65.xyz_unit_luminance();
		for i in 0..3 {
			assert!(
				approx(white[i], expected[i], 1e-4),
				"sRGB white → XYZ mismatch at {i}: {} vs {}",
				white[i],
				expected[i]
			);
		}
	}

	#[test]
	fn srgb_to_acescg_matches_published_matrix() {
		// Published sRGB → ACEScg transform (ACES transform docs):
		// 0.6131324224 0.3395230762 0.0473445014
		// 0.0701922769 0.9163536767 0.0134540464
		// 0.0206157712 0.1095697056 0.8698145232
		let m = rgb_to_rgb_matrix(PRIMARIES_SRGB, PRIMARIES_AP1);
		let expected: Mat3 = [
			[0.6131324224, 0.3395230762, 0.0473445014],
			[0.0701922769, 0.9163536767, 0.0134540464],
			[0.0206157712, 0.1095697056, 0.8698145232],
		];
		for r in 0..3 {
			for c in 0..3 {
				assert!(
					approx(m[r][c], expected[r][c], 2e-3),
					"matrix[{r}][{c}] = {} vs published {}",
					m[r][c],
					expected[r][c]
				);
			}
		}
	}

	#[test]
	fn srgb_red_maps_to_published_acescg() {
		let mut samples = [1.0f32, 0.0, 0.0, 1.0];
		decode_to_acescg(&mut samples, SourcePrimaries::Bt709, SourceTransfer::Linear);
		// sRGB red in ACEScg ≈ (0.6131, 0.0702, 0.0206).
		assert!(approx(samples[0], 0.6131, 2e-3), "R = {}", samples[0]);
		assert!(approx(samples[1], 0.0702, 2e-3), "G = {}", samples[1]);
		assert!(approx(samples[2], 0.0206, 2e-3), "B = {}", samples[2]);
	}

	#[test]
	fn round_trip_srgb_acescg_srgb() {
		// Gamma-encoded sRGB codes → (EOTF) → linear → ACEScg → (matrix)
		// → sRGB linear → (OETF) → the original codes.
		let original = [0.25f32, 0.5, 0.75, 1.0];
		let mut samples = original;
		decode_to_acescg(&mut samples, SourcePrimaries::Bt709, SourceTransfer::SdrGamma);
		acescg_to_output(
			&mut samples,
			OutputColorSpec {
				gamut: OutputGamut::Srgb,
				transfer: OutputTransfer::Srgb,
			},
		);
		for i in 0..3 {
			assert!(
				approx(samples[i], original[i], 1e-4),
				"round trip channel {i}: {} vs {}",
				samples[i],
				original[i]
			);
		}
	}

	#[test]
	fn round_trip_bt2020_acescg_bt2020() {
		let original = [0.2f32, 0.6, 0.9, 1.0];
		let mut samples = original;
		decode_to_acescg(&mut samples, SourcePrimaries::Bt2020, SourceTransfer::SdrGamma);
		acescg_to_output(
			&mut samples,
			OutputColorSpec {
				gamut: OutputGamut::Bt2020,
				transfer: OutputTransfer::Srgb,
			},
		);
		for i in 0..3 {
			assert!(
				approx(samples[i], original[i], 1e-4),
				"round trip channel {i}: {} vs {}",
				samples[i],
				original[i]
			);
		}
	}

	#[test]
	fn white_stays_white_through_acescg() {
		// Every primaries→AP1 matrix has rows summing to ~1, so the white
		// point maps to (1,1,1) in ACEScg.
		let mut samples = [1.0f32, 1.0, 1.0, 1.0];
		decode_to_acescg(&mut samples, SourcePrimaries::Bt709, SourceTransfer::Linear);
		for i in 0..3 {
			assert!(approx(samples[i], 1.0, 1e-3), "white channel {i} = {}", samples[i]);
		}
	}

	#[test]
	fn srgb_transfer_anchors() {
		// The piecewise curve's join point and endpoints.
		assert!(approx(srgb_oetf(0.0), 0.0, 1e-6), "oetf(0)");
		assert!(approx(srgb_oetf(1.0), 1.0, 1e-6), "oetf(1)");
		assert!(approx(srgb_eotf(0.0), 0.0, 1e-6), "eotf(0)");
		assert!(approx(srgb_eotf(1.0), 1.0, 1e-6), "eotf(1)");
		// Mid-grey: linear 0.18 → ~0.461 sRGB code (the classic check).
		assert!(approx(srgb_oetf(0.18), 0.4613, 1e-3), "oetf(0.18) = {}", srgb_oetf(0.18));
		// Round trip.
		for v in [0.0f32, 0.01, 0.18, 0.5, 0.99, 1.0] {
			assert!(approx(srgb_eotf(srgb_oetf(v)), v, 1e-4), "round trip {v}");
		}
	}

	#[test]
	fn pq_transfer_anchors() {
		// PQ code 1.0 = 10 000 nits = normalized 1.0.
		assert!(approx(pq_eotf(1.0), 1.0, 1e-4), "pq eotf(1) = {}", pq_eotf(1.0));
		// PQ code 0.5 ≈ 100 nits (normalized 0.01).
		assert!(approx(pq_eotf(0.5), 0.01008, 2e-3), "pq eotf(0.5) = {}", pq_eotf(0.5));
		// Round trip.
		for v in [0.0f32, 0.25, 0.5, 0.75, 1.0] {
			assert!(approx(pq_eotf(pq_oetf(v)), v, 1e-3), "pq round trip {v}");
		}
	}

	#[test]
	fn hlg_transfer_anchors() {
		assert!(approx(hlg_eotf(0.0), 0.0, 1e-6), "hlg eotf(0)");
		assert!(approx(hlg_eotf(1.0), 1.0, 1e-3), "hlg eotf(1) = {}", hlg_eotf(1.0));
		// The piecewise join: code 0.5 ↔ linear 1/12.
		assert!(approx(hlg_eotf(0.5), 1.0 / 12.0, 1e-3), "hlg eotf(0.5) = {}", hlg_eotf(0.5));
		// Round trip.
		for v in [0.0f32, 0.1, 0.5, 0.9, 1.0] {
			assert!(approx(hlg_eotf(hlg_oetf(v)), v, 1e-3), "hlg round trip {v}");
		}
	}

	#[test]
	fn av_color_code_mapping() {
		assert_eq!(source_primaries_from_av(1), SourcePrimaries::Bt709);
		assert_eq!(source_primaries_from_av(9), SourcePrimaries::Bt2020);
		assert_eq!(source_primaries_from_av(12), SourcePrimaries::DisplayP3);
		assert_eq!(source_primaries_from_av(2), SourcePrimaries::Unknown);
		assert_eq!(source_transfer_from_av(1), SourceTransfer::Gamma24);
		assert_eq!(source_transfer_from_av(4), SourceTransfer::Gamma22);
		assert_eq!(source_transfer_from_av(5), SourceTransfer::Gamma28);
		assert_eq!(source_transfer_from_av(6), SourceTransfer::Gamma24);
		assert_eq!(source_transfer_from_av(13), SourceTransfer::SdrGamma);
		assert_eq!(source_transfer_from_av(14), SourceTransfer::Gamma24);
		assert_eq!(source_transfer_from_av(15), SourceTransfer::Gamma24);
		assert_eq!(source_transfer_from_av(16), SourceTransfer::Pq);
		assert_eq!(source_transfer_from_av(18), SourceTransfer::Hlg);
		assert_eq!(source_transfer_from_av(8), SourceTransfer::Linear);
		assert_eq!(source_transfer_from_av(3), SourceTransfer::Unknown);
		assert_eq!(source_transfer_from_av(7), SourceTransfer::Unknown);
	}

	#[test]
	fn setting_round_trips() {
		assert_eq!(WorkingColorSpace::from_setting("acescg"), WorkingColorSpace::AcesCg);
		assert_eq!(
			WorkingColorSpace::from_setting("srgb_legacy"),
			WorkingColorSpace::SrgbLegacy
		);
		assert_eq!(WorkingColorSpace::from_setting("bogus"), WorkingColorSpace::AcesCg);
		assert_eq!(OutputGamut::from_setting("displayp3"), OutputGamut::DisplayP3);
		assert_eq!(OutputGamut::from_setting("bt2020"), OutputGamut::Bt2020);
		assert_eq!(OutputGamut::from_setting(""), OutputGamut::Srgb);
		assert_eq!(OutputTransfer::from_setting("pq"), OutputTransfer::Pq);
		assert_eq!(OutputTransfer::from_setting("hlg"), OutputTransfer::Hlg);
		assert_eq!(OutputTransfer::from_setting("gamma22"), OutputTransfer::Gamma22);
		assert_eq!(OutputTransfer::from_setting(""), OutputTransfer::Srgb);
		assert_eq!(
			OutputColorSpec::from_settings("displayp3", "pq"),
			OutputColorSpec {
				gamut: OutputGamut::DisplayP3,
				transfer: OutputTransfer::Pq
			}
		);
	}

	#[test]
	fn legacy_working_space_is_pass_through() {
		let mut samples = [0.25f32, 0.5, 0.75, 1.0];
		let original = samples;
		working_to_display_target(
			&mut samples,
			WorkingColorSpace::SrgbLegacy,
			OutputColorSpec::default(),
		);
		assert_eq!(samples, original);
	}

	#[test]
	fn gamma_transfer_anchors() {
		// Pure-power EOTFs at code 0.5. The 2.4 case is the BT.1886
		// mid-grey link: 0.5²·⁴ ≈ 0.189 (≈ the classic 18 % grey).
		assert!(approx(gamma_eotf(0.5, 2.4), 0.1894646, 1e-5), "eotf(0.5, 2.4) = {}", gamma_eotf(0.5, 2.4));
		assert!(approx(gamma_eotf(0.5, 2.2), 0.2176376, 1e-5), "eotf(0.5, 2.2) = {}", gamma_eotf(0.5, 2.2));
		assert!(approx(gamma_eotf(0.5, 2.8), 0.1435894, 1e-5), "eotf(0.5, 2.8) = {}", gamma_eotf(0.5, 2.8));
		// Endpoints and round trips.
		for gamma in [2.2f32, 2.4, 2.8] {
			assert!(approx(gamma_eotf(0.0, gamma), 0.0, 1e-6), "eotf(0) g{gamma}");
			assert!(approx(gamma_eotf(1.0, gamma), 1.0, 1e-6), "eotf(1) g{gamma}");
			for v in [0.0f32, 0.18, 0.5, 0.9, 1.0] {
				assert!(
					approx(gamma_eotf(gamma_oetf(v, gamma), gamma), v, 1e-4),
					"gamma round trip {v} @ {gamma}"
				);
			}
		}
	}

	#[test]
	fn decode_gamma24_dispatch() {
		// BT.1886 decode: code 0.5 linearizes via pure power 2.4 (≈ 0.1895,
		// not sRGB's piecewise ≈ 0.2140), then the BT.709 → AP1 matrix.
		let mut samples = [0.5f32, 0.5, 0.5, 1.0];
		decode_to_acescg(&mut samples, SourcePrimaries::Bt709, SourceTransfer::Gamma24);
		let m = rgb_to_rgb_matrix(PRIMARIES_SRGB, PRIMARIES_AP1);
		let expected = mat_vec(m, [gamma_eotf(0.5, 2.4); 3]);
		for i in 0..3 {
			assert!(
				approx(samples[i], expected[i], 1e-6),
				"Gamma24 decode channel {i}: {} vs {}",
				samples[i],
				expected[i]
			);
		}
		// ... and the bytes variant picks the same curve.
		let mut bytes = [0u8; 16];
		bytes[0..4].copy_from_slice(&0.5f32.to_le_bytes());
		bytes[4..8].copy_from_slice(&0.5f32.to_le_bytes());
		bytes[8..12].copy_from_slice(&0.5f32.to_le_bytes());
		decode_to_acescg_bytes(&mut bytes, 1, SourcePrimaries::Bt709, SourceTransfer::Gamma24);
		let r = f32::from_le_bytes(bytes[0..4].try_into().unwrap());
		assert!(approx(r, expected[0], 1e-6), "bytes Gamma24 R: {r}");
	}

	#[test]
	fn output_clamps_out_of_gamut_rgb() {
		// Pure AP1 red is far outside the sRGB gamut: after the AP1 → sRGB
		// matrix it has R > 1 and negative G/B. The P1 gamut-mapping contract
		// clips those channels to [0, 1] before the transfer encoding.
		let mut samples = [1.0f32, 0.0, 0.0, 0.5];
		acescg_to_output(
			&mut samples,
			OutputColorSpec {
				gamut: OutputGamut::Srgb,
				transfer: OutputTransfer::Srgb,
			},
		);
		let m = rgb_to_rgb_matrix(PRIMARIES_AP1, PRIMARIES_SRGB);
		let unclamped = mat_vec(m, [1.0, 0.0, 0.0]);
		assert!(
			unclamped[0] > 1.0 || unclamped[1] < 0.0 || unclamped[2] < 0.0,
			"AP1 red should leave the sRGB gamut, got {unclamped:?}"
		);
		let expected = [
			srgb_oetf(unclamped[0].clamp(0.0, 1.0)),
			srgb_oetf(unclamped[1].clamp(0.0, 1.0)),
			srgb_oetf(unclamped[2].clamp(0.0, 1.0)),
		];
		for c in 0..3 {
			assert!(
				approx(samples[c], expected[c], 1e-6),
				"clamped channel {c}: {} vs {}",
				samples[c],
				expected[c]
			);
			assert!(samples[c] >= 0.0 && samples[c] <= 1.0, "channel {c} escaped [0, 1]: {}", samples[c]);
		}
		assert_eq!(samples[3], 0.5, "alpha must be untouched");
	}

	#[test]
	fn output_bytes_clamps_out_of_gamut_rgb() {
		let mut bytes = [0u8; 16];
		bytes[0..4].copy_from_slice(&1.0f32.to_le_bytes()); // R = AP1 red
		acescg_to_output_bytes(
			&mut bytes,
			1,
			OutputColorSpec {
				gamut: OutputGamut::Srgb,
				transfer: OutputTransfer::Gamma22,
			},
		);
		let m = rgb_to_rgb_matrix(PRIMARIES_AP1, PRIMARIES_SRGB);
		let unclamped = mat_vec(m, [1.0, 0.0, 0.0]);
		let expected = gamma_oetf(unclamped[0].clamp(0.0, 1.0), 2.2);
		let r = f32::from_le_bytes(bytes[0..4].try_into().unwrap());
		assert!(approx(r, expected, 1e-6), "bytes R: {r} vs {expected}");
		// Alpha (still 0.0) passes through untouched.
		assert_eq!(f32::from_le_bytes(bytes[12..16].try_into().unwrap()), 0.0);
	}

	/// Build a multi-row 16-bit plane byte buffer from `u16` row samples with
	/// the given row stride (padding is left as zero).
	fn yuv_plane16(rows: &[&[u16]], width: usize, stride: usize) -> Vec<u8> {
		let mut plane = vec![0u8; rows.len().saturating_sub(1) * stride + width * 2];
		for (r, row) in rows.iter().enumerate() {
			for (x, s) in row.iter().take(width).enumerate() {
				let off = r * stride + x * 2;
				plane[off] = s.to_le_bytes()[0];
				plane[off + 1] = s.to_le_bytes()[1];
			}
		}
		plane
	}

	#[test]
	fn yuv_bt709_limited_gray_anchors() {
		// White (Y = 235<<8) → RGB 1.0, black (Y = 16<<8) → 0.0, neutral
		// chroma 128<<8, BT.709 matrix, limited range. Stride is padded
		// (4 bytes vs 2 for width 1) to exercise stride handling.
		let width = 1;
		let height = 2;
		let stride = 4;
		let y = yuv_plane16(&[&[235 << 8], &[16 << 8]], width, stride);
		let u = yuv_plane16(&[&[128 << 8], &[128 << 8]], width, stride);
		let v = u.clone();
		let mut out = [0.0f32; 8];
		yuv444p16_to_rgb_f32(&y, stride, &u, stride, &v, stride, width, height, YuvMatrix::Bt709, false, &mut out);
		for c in 0..3 {
			assert!(approx(out[c], 1.0, 1e-4), "white channel {c}: {}", out[c]);
			assert!(approx(out[4 + c], 0.0, 1e-4), "black channel {c}: {}", out[4 + c]);
		}
		assert_eq!(out[3], 1.0, "alpha");
		assert_eq!(out[7], 1.0, "alpha");
	}

	#[test]
	fn yuv_bt601_full_range_green_anchor() {
		// Pure green in BT.601 full range (chroma span 65535, center 32768):
		// Y = 0.587·65535, Cb = 32768 + (−0.587/1.772)·65535,
		// Cr = 32768 + (−0.587/1.402)·65535, rounded to 16-bit codes.
		let width = 1;
		let height = 1;
		let stride = 2;
		let y = yuv_plane16(&[&[38469]], width, stride);
		let u = yuv_plane16(&[&[11059]], width, stride);
		let v = yuv_plane16(&[&[5329]], width, stride);
		let mut out = [0.0f32; 4];
		yuv444p16_to_rgb_f32(&y, stride, &u, stride, &v, stride, width, height, YuvMatrix::Bt601, true, &mut out);
		assert!(approx(out[0], 0.0, 1e-3), "R = {}", out[0]);
		assert!(approx(out[1], 1.0, 1e-3), "G = {}", out[1]);
		assert!(approx(out[2], 0.0, 1e-3), "B = {}", out[2]);
	}

	#[test]
	fn yuv_bt709_limited_green_anchor() {
		// Pure green in BT.709 limited range: Y = 16 + 0.7152·219 (×256),
		// Cb = 32768 + (−0.7152/1.8556)·57344,
		// Cr = 32768 + (−0.7152/1.5748)·57344.
		let width = 1;
		let height = 1;
		let stride = 2;
		let y = yuv_plane16(&[&[44193]], width, stride);
		let u = yuv_plane16(&[&[10666]], width, stride);
		let v = yuv_plane16(&[&[6725]], width, stride);
		let mut out = [0.0f32; 4];
		yuv444p16_to_rgb_f32(&y, stride, &u, stride, &v, stride, width, height, YuvMatrix::Bt709, false, &mut out);
		assert!(approx(out[0], 0.0, 1e-3), "R = {}", out[0]);
		assert!(approx(out[1], 1.0, 1e-3), "G = {}", out[1]);
		assert!(approx(out[2], 0.0, 1e-3), "B = {}", out[2]);
	}

	#[test]
	fn yuv_short_planes_return_without_panic() {
		// A 2×2 frame at stride 4 needs (2−1)·4 + 2·2 = 8 bytes per plane;
		// 4 bytes is too short → early return, `out` untouched.
		let mut out = [7.0f32; 4];
		yuv444p16_to_rgb_f32(&[0u8; 4], 4, &[0u8; 4], 4, &[0u8; 4], 4, 2, 2, YuvMatrix::Bt709, false, &mut out);
		assert_eq!(out, [7.0; 4]);
		// `out` too short for the frame → early return.
		let mut out2 = [7.0f32; 3];
		yuv444p16_to_rgb_f32(&[0u8; 8], 4, &[0u8; 8], 4, &[0u8; 8], 4, 2, 2, YuvMatrix::Bt709, false, &mut out2);
		assert_eq!(out2, [7.0; 3]);
	}

	#[test]
	fn output_srgb_to_xyz_d65_white_and_red() {
		let spec = OutputColorSpec {
			gamut: OutputGamut::Srgb,
			transfer: OutputTransfer::Srgb,
		};
		// White (1,1,1) → the D65 white point at unit luminance.
		let mut white = [1.0f32, 1.0, 1.0, 1.0];
		output_spec_to_xyz_d65(&mut white, spec);
		let expected = WHITE_D65.xyz_unit_luminance();
		for i in 0..3 {
			assert!(
				approx(white[i], expected[i], 1e-4),
				"white → XYZ mismatch at {i}: {} vs {}",
				white[i],
				expected[i]
			);
		}
		// Red (1,0,0) → the published IEC 61966-2-1 sRGB→XYZ column.
		let mut red = [1.0f32, 0.0, 0.0, 1.0];
		output_spec_to_xyz_d65(&mut red, spec);
		let m = rgb_to_xyz_matrix(PRIMARIES_SRGB);
		for i in 0..3 {
			assert!(
				approx(red[i], m[i][0], 1e-6),
				"red → XYZ mismatch at {i}: {} vs {}",
				red[i],
				m[i][0]
			);
		}
		assert!(approx(red[0], 0.4124, 1e-3), "X = {}", red[0]);
		assert!(approx(red[1], 0.2126, 1e-3), "Y = {}", red[1]);
		assert!(approx(red[2], 0.0193, 1e-3), "Z = {}", red[2]);
	}

	#[test]
	fn output_spec_to_xyz_d65_honors_transfer() {
		// Gamma-2.2 transfer: code 0.5 linearizes to 0.5^2.2 before the
		// gamut matrix.
		let spec = OutputColorSpec {
			gamut: OutputGamut::Srgb,
			transfer: OutputTransfer::Gamma22,
		};
		let mut s = [0.5f32, 0.5, 0.5, 1.0];
		output_spec_to_xyz_d65(&mut s, spec);
		let m = rgb_to_xyz_matrix(PRIMARIES_SRGB);
		let expected = mat_vec(m, [gamma_eotf(0.5, 2.2); 3]);
		for i in 0..3 {
			assert!(
				approx(s[i], expected[i], 1e-6),
				"Gamma22 → XYZ channel {i}: {} vs {}",
				s[i],
				expected[i]
			);
		}
		// PQ / HLG code 1.0 linearizes to 1.0 → the D65 white point for any
		// D65 gamut (also exercises the P3 matrix and the PQ/HLG branches).
		for transfer in [OutputTransfer::Pq, OutputTransfer::Hlg] {
			let spec = OutputColorSpec {
				gamut: OutputGamut::DisplayP3,
				transfer,
			};
			let mut w = [1.0f32, 1.0, 1.0, 1.0];
			output_spec_to_xyz_d65(&mut w, spec);
			let expected = WHITE_D65.xyz_unit_luminance();
			for i in 0..3 {
				assert!(
					approx(w[i], expected[i], 1e-4),
					"{transfer:?} white → XYZ mismatch at {i}: {} vs {}",
					w[i],
					expected[i]
				);
			}
		}
	}
}
