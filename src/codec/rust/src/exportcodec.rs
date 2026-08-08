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

//! `olive::ExportCodec` — the export codec enum and name table.
//!
//! Mirrors `src/codec/src/exportcodec.h`. The enum is the raw int contract
//! the C ABI documents (oakengine/encoding.h); values must NOT be
//! reordered (they are used in serialized files).

/// `olive::ExportCodec::Codec`. Only append (never insert/reorder); the
/// integer values are part of the file format.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(i32)]
pub enum Codec {
	/// Apple ProRes.
	DNxHD = 0,
	/// H.264.
	H264 = 1,
	/// H.264 with RGB color.
	H264RGB = 2,
	/// H.265/HEVC.
	H265 = 3,
	/// OpenEXR.
	OpenEXR = 4,
	/// PNG.
	PNG = 5,
	/// Apple ProRes.
	ProRes = 6,
	/// CineForm.
	CineForm = 7,
	/// TIFF.
	TIFF = 8,
	/// VP9.
	VP9 = 9,
	/// MPEG-2.
	MP2 = 10,
	/// MPEG-3.
	MP3 = 11,
	/// AAC.
	AAC = 12,
	/// Uncompressed PCM.
	PCM = 13,
	/// Opus.
	Opus = 14,
	/// Vorbis.
	Vorbis = 15,
	/// FLAC.
	FLAC = 16,
	/// SubRip subtitles.
	SRT = 17,
	/// AV1.
	AV1 = 18,
	/// Sentinel count (not a real codec).
	Count = 19,
}

impl Codec {
	/// Convert an `i32` code to a [`Codec`]; `None` outside the valid
	/// `0..=18` range (incl. `Count`).
	pub fn from_i32(v: i32) -> Option<Codec> {
		match v {
			0 => Some(Codec::DNxHD),
			1 => Some(Codec::H264),
			2 => Some(Codec::H264RGB),
			3 => Some(Codec::H265),
			4 => Some(Codec::OpenEXR),
			5 => Some(Codec::PNG),
			6 => Some(Codec::ProRes),
			7 => Some(Codec::CineForm),
			8 => Some(Codec::TIFF),
			9 => Some(Codec::VP9),
			10 => Some(Codec::MP2),
			11 => Some(Codec::MP3),
			12 => Some(Codec::AAC),
			13 => Some(Codec::PCM),
			14 => Some(Codec::Opus),
			15 => Some(Codec::Vorbis),
			16 => Some(Codec::FLAC),
			17 => Some(Codec::SRT),
			18 => Some(Codec::AV1),
			_ => None,
		}
	}

	/// Human-readable name for a codec.
	///
	/// CPP-PARITY: `src/codec/src/exportcodec.cpp` `get_codec_name`; unknown /
	/// `Count` returns "Unknown".
	pub fn get_codec_name(c: Codec) -> String {
		match c {
			Codec::DNxHD => "DNxHD".to_string(),
			Codec::H264 => "H.264".to_string(),
			Codec::H264RGB => "H.264 RGB".to_string(),
			Codec::H265 => "H.265".to_string(),
			Codec::OpenEXR => "OpenEXR".to_string(),
			Codec::PNG => "PNG".to_string(),
			Codec::ProRes => "ProRes".to_string(),
			Codec::CineForm => "Cineform".to_string(),
			Codec::TIFF => "TIFF".to_string(),
			Codec::VP9 => "VP9".to_string(),
			Codec::MP2 => "MP2".to_string(),
			Codec::MP3 => "MP3".to_string(),
			Codec::AAC => "AAC".to_string(),
			Codec::PCM => "PCM (Uncompressed)".to_string(),
			Codec::Opus => "Opus".to_string(),
			Codec::Vorbis => "Vorbis".to_string(),
			Codec::FLAC => "FLAC".to_string(),
			Codec::SRT => "SubRip SRT".to_string(),
			Codec::AV1 => "AV1".to_string(),
			Codec::Count => "Unknown".to_string(),
		}
	}

	/// Whether the codec produces a still image.
	///
	/// CPP-PARITY: `src/codec/src/exportcodec.cpp` `is_codec_a_still_image`;
	/// OpenEXR / PNG / TIFF are still images, everything else is not.
	pub fn is_codec_a_still_image(c: Codec) -> bool {
		match c {
			Codec::OpenEXR | Codec::PNG | Codec::TIFF => true,
			_ => false,
		}
	}

	/// Whether the codec is lossless.
	///
	/// CPP-PARITY: `src/codec/src/exportcodec.cpp` `is_codec_lossless`;
	/// only PCM and FLAC are lossless.
	pub fn is_codec_lossless(c: Codec) -> bool {
		match c {
			Codec::PCM | Codec::FLAC => true,
			_ => false,
		}
	}
}

#[cfg(test)]
mod tests {
	use super::*;

	#[test]
	fn from_i32_maps_valid_and_rejects_others() {
		for v in 0..=18 {
			assert!(Codec::from_i32(v).is_some(), "codec {v}");
		}
		assert!(Codec::from_i32(19).is_none()); // Count
		assert!(Codec::from_i32(-1).is_none());
		assert!(Codec::from_i32(99).is_none());
	}

	#[test]
	fn codec_name_mapping() {
		assert_eq!(Codec::get_codec_name(Codec::DNxHD), "DNxHD");
		assert_eq!(Codec::get_codec_name(Codec::H264), "H.264");
		assert_eq!(Codec::get_codec_name(Codec::H264RGB), "H.264 RGB");
		assert_eq!(Codec::get_codec_name(Codec::H265), "H.265");
		assert_eq!(Codec::get_codec_name(Codec::OpenEXR), "OpenEXR");
		assert_eq!(Codec::get_codec_name(Codec::PNG), "PNG");
		assert_eq!(Codec::get_codec_name(Codec::ProRes), "ProRes");
		assert_eq!(Codec::get_codec_name(Codec::CineForm), "Cineform");
		assert_eq!(Codec::get_codec_name(Codec::TIFF), "TIFF");
		assert_eq!(Codec::get_codec_name(Codec::VP9), "VP9");
		assert_eq!(Codec::get_codec_name(Codec::MP2), "MP2");
		assert_eq!(Codec::get_codec_name(Codec::MP3), "MP3");
		assert_eq!(Codec::get_codec_name(Codec::AAC), "AAC");
		assert_eq!(Codec::get_codec_name(Codec::PCM), "PCM (Uncompressed)");
		assert_eq!(Codec::get_codec_name(Codec::Opus), "Opus");
		assert_eq!(Codec::get_codec_name(Codec::Vorbis), "Vorbis");
		assert_eq!(Codec::get_codec_name(Codec::FLAC), "FLAC");
		assert_eq!(Codec::get_codec_name(Codec::SRT), "SubRip SRT");
		assert_eq!(Codec::get_codec_name(Codec::AV1), "AV1");
		assert_eq!(Codec::get_codec_name(Codec::Count), "Unknown");
	}

	#[test]
	fn still_image_and_lossless_flags() {
		assert!(Codec::is_codec_a_still_image(Codec::OpenEXR));
		assert!(Codec::is_codec_a_still_image(Codec::PNG));
		assert!(Codec::is_codec_a_still_image(Codec::TIFF));
		assert!(!Codec::is_codec_a_still_image(Codec::H264));
		assert!(!Codec::is_codec_a_still_image(Codec::PCM));

		assert!(Codec::is_codec_lossless(Codec::PCM));
		assert!(Codec::is_codec_lossless(Codec::FLAC));
		assert!(!Codec::is_codec_lossless(Codec::MP3));
		assert!(!Codec::is_codec_lossless(Codec::H264));
	}
}
