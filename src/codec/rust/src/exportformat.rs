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

//! `olive::ExportFormat` — the export container-format enum and tables.
//!
//! Mirrors `src/codec/src/exportformat.h`. As with `ExportCodec`, the enum
//! values are the serialized-file contract and must never be reordered.

use oakcore_rs::SampleFormat;

use crate::exportcodec::Codec;

/// `olive::ExportFormat::Format`. Only append (never insert/reorder).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(i32)]
pub enum Format {
	/// QuickTime DNxHD.
	DNxHD = 0,
	/// Matroska.
	Matroska = 1,
	/// MPEG-4 video.
	MPEG4Video = 2,
	/// OpenEXR.
	OpenEXR = 3,
	/// QuickTime.
	QuickTime = 4,
	/// PNG.
	PNG = 5,
	/// TIFF.
	TIFF = 6,
	/// WAV.
	WAV = 7,
	/// AIFF.
	AIFF = 8,
	/// MP3.
	MP3 = 9,
	/// FLAC.
	FLAC = 10,
	/// Ogg.
	Ogg = 11,
	/// WebM.
	WebM = 12,
	/// SubRip.
	SRT = 13,
	/// MPEG-4 audio.
	MPEG4Audio = 14,
	/// Sentinel count (not a real format).
	Count = 15,
}

impl Format {
	/// Convert an `i32` code to a [`Format`]; `None` outside the valid
	/// `0..=14` range (incl. `Count`).
	pub fn from_i32(v: i32) -> Option<Format> {
		match v {
			0 => Some(Format::DNxHD),
			1 => Some(Format::Matroska),
			2 => Some(Format::MPEG4Video),
			3 => Some(Format::OpenEXR),
			4 => Some(Format::QuickTime),
			5 => Some(Format::PNG),
			6 => Some(Format::TIFF),
			7 => Some(Format::WAV),
			8 => Some(Format::AIFF),
			9 => Some(Format::MP3),
			10 => Some(Format::FLAC),
			11 => Some(Format::Ogg),
			12 => Some(Format::WebM),
			13 => Some(Format::SRT),
			14 => Some(Format::MPEG4Audio),
			_ => None,
		}
	}

	/// Human-readable name for a format.
	///
	/// CPP-PARITY: `src/codec/src/exportformat.cpp` `get_name`; unknown /
	/// `Count` returns "Unknown".
	pub fn get_name(f: Format) -> String {
		match f {
			Format::DNxHD => "DNxHD".to_string(),
			Format::Matroska => "Matroska Video".to_string(),
			Format::MPEG4Video => "MPEG-4 Video".to_string(),
			Format::MPEG4Audio => "MPEG-4 Audio".to_string(),
			Format::OpenEXR => "OpenEXR".to_string(),
			Format::PNG => "PNG".to_string(),
			Format::TIFF => "TIFF".to_string(),
			Format::QuickTime => "QuickTime".to_string(),
			Format::WAV => "Wave Audio".to_string(),
			Format::AIFF => "AIFF".to_string(),
			Format::MP3 => "MP3".to_string(),
			Format::FLAC => "FLAC".to_string(),
			Format::Ogg => "Ogg".to_string(),
			Format::WebM => "WebM".to_string(),
			Format::SRT => "SubRip SRT".to_string(),
			Format::Count => "Unknown".to_string(),
		}
	}

	/// File extension for a format (e.g. "mp4").
	///
	/// CPP-PARITY: `src/codec/src/exportformat.cpp` `get_extension`;
	/// `Count` returns an empty string.
	pub fn get_extension(f: Format) -> String {
		match f {
			Format::DNxHD => "mxf".to_string(),
			Format::Matroska => "mkv".to_string(),
			Format::MPEG4Video => "mp4".to_string(),
			Format::MPEG4Audio => "m4a".to_string(),
			Format::OpenEXR => "exr".to_string(),
			Format::PNG => "png".to_string(),
			Format::TIFF => "tiff".to_string(),
			Format::QuickTime => "mov".to_string(),
			Format::WAV => "wav".to_string(),
			Format::AIFF => "aiff".to_string(),
			Format::MP3 => "mp3".to_string(),
			Format::FLAC => "flac".to_string(),
			Format::Ogg => "ogg".to_string(),
			Format::WebM => "webm".to_string(),
			Format::SRT => "srt".to_string(),
			Format::Count => String::new(),
		}
	}

	/// Codecs that can carry video in this format.
	///
	/// CPP-PARITY: `src/codec/src/exportformat.cpp` `get_video_codecs`.
	pub fn get_video_codecs(f: Format) -> Vec<Codec> {
		match f {
			Format::DNxHD => vec![Codec::DNxHD],
			Format::Matroska => vec![Codec::H264, Codec::H264RGB, Codec::H265, Codec::VP9],
			Format::MPEG4Video => vec![Codec::H264, Codec::H264RGB, Codec::H265],
			Format::OpenEXR => vec![Codec::OpenEXR],
			Format::PNG => vec![Codec::PNG],
			Format::TIFF => vec![Codec::TIFF],
			Format::QuickTime => vec![
				Codec::H264,
				Codec::H264RGB,
				Codec::H265,
				Codec::ProRes,
				Codec::CineForm,
			],
			Format::WebM => vec![Codec::AV1, Codec::VP9],
			_ => Vec::new(),
		}
	}

	/// Codecs that can carry audio in this format.
	///
	/// CPP-PARITY: `src/codec/src/exportformat.cpp` `get_audio_codecs`.
	pub fn get_audio_codecs(f: Format) -> Vec<Codec> {
		match f {
			// Video/audio formats.
			Format::DNxHD => vec![Codec::PCM],
			Format::Matroska => vec![
				Codec::AAC,
				Codec::MP2,
				Codec::MP3,
				Codec::PCM,
				Codec::Vorbis,
				Codec::Opus,
				Codec::FLAC,
			],
			Format::MPEG4Video | Format::MPEG4Audio => {
				vec![Codec::AAC, Codec::MP2, Codec::MP3]
			}
			Format::QuickTime => vec![Codec::AAC, Codec::MP2, Codec::MP3, Codec::PCM],
			Format::WebM => vec![
				Codec::Opus,
				Codec::AAC,
				Codec::MP2,
				Codec::MP3,
				Codec::PCM,
				Codec::Vorbis,
			],
			// Audio-only formats.
			Format::WAV => vec![Codec::PCM],
			Format::AIFF => vec![Codec::PCM],
			Format::MP3 => vec![Codec::MP3],
			Format::FLAC => vec![Codec::FLAC],
			Format::Ogg => vec![Codec::Opus, Codec::Vorbis, Codec::PCM],
			_ => Vec::new(),
		}
	}

	/// Codecs that can carry subtitles in this format.
	///
	/// CPP-PARITY: `src/codec/src/exportformat.cpp` `get_subtitle_codecs`;
	/// only Matroska and SRT support the SRT subtitle codec.
	pub fn get_subtitle_codecs(f: Format) -> Vec<Codec> {
		match f {
			Format::Matroska | Format::SRT => vec![Codec::SRT],
			_ => Vec::new(),
		}
	}

	/// Pixel formats the given codec supports in this format.
	///
	/// The C++ side computes this by instantiating the format's encoder and
	/// asking the FFmpeg bridge (`fb_encoder_codec_get_pixel_formats`); the
	/// base `Encoder::get_pixel_formats_for_codec` returns an empty list.
	/// There is no pure-Rust table, so this returns an empty list (matching
	/// the C++ base default). CPP-PARITY: `exportformat.cpp:222`.
	pub fn get_pixel_formats_for_codec(_f: Format, _c: Codec) -> Vec<i32> {
		Vec::new()
	}

	/// Sample formats the given codec supports in this format.
	///
	/// CPP-PARITY: `ffmpegencoder.cpp:175`. PCM is pure (signed-16 first so
	/// the export dialog's default matches FFmpeg's); all other codecs query
	/// the FFmpeg bridge and return empty here (see
	/// `get_pixel_formats_for_codec`).
	pub fn get_sample_formats_for_codec(_f: Format, c: Codec) -> Vec<SampleFormat> {
		if c == Codec::PCM {
			vec![
				SampleFormat::S16,
				SampleFormat::U8,
				SampleFormat::S32,
				SampleFormat::S64,
				SampleFormat::F32,
				SampleFormat::F64,
			]
		} else {
			Vec::new()
		}
	}
}

#[cfg(test)]
mod tests {
	use super::*;

	#[test]
	fn format_name_mapping() {
		assert_eq!(Format::get_name(Format::DNxHD), "DNxHD");
		assert_eq!(Format::get_name(Format::Matroska), "Matroska Video");
		assert_eq!(Format::get_name(Format::MPEG4Video), "MPEG-4 Video");
		assert_eq!(Format::get_name(Format::MPEG4Audio), "MPEG-4 Audio");
		assert_eq!(Format::get_name(Format::OpenEXR), "OpenEXR");
		assert_eq!(Format::get_name(Format::PNG), "PNG");
		assert_eq!(Format::get_name(Format::TIFF), "TIFF");
		assert_eq!(Format::get_name(Format::QuickTime), "QuickTime");
		assert_eq!(Format::get_name(Format::WAV), "Wave Audio");
		assert_eq!(Format::get_name(Format::AIFF), "AIFF");
		assert_eq!(Format::get_name(Format::MP3), "MP3");
		assert_eq!(Format::get_name(Format::FLAC), "FLAC");
		assert_eq!(Format::get_name(Format::Ogg), "Ogg");
		assert_eq!(Format::get_name(Format::WebM), "WebM");
		assert_eq!(Format::get_name(Format::SRT), "SubRip SRT");
		assert_eq!(Format::get_name(Format::Count), "Unknown");
	}

	#[test]
	fn format_extension_mapping() {
		let cases = [
			(Format::DNxHD, "mxf"),
			(Format::Matroska, "mkv"),
			(Format::MPEG4Video, "mp4"),
			(Format::MPEG4Audio, "m4a"),
			(Format::OpenEXR, "exr"),
			(Format::PNG, "png"),
			(Format::TIFF, "tiff"),
			(Format::QuickTime, "mov"),
			(Format::WAV, "wav"),
			(Format::AIFF, "aiff"),
			(Format::MP3, "mp3"),
			(Format::FLAC, "flac"),
			(Format::Ogg, "ogg"),
			(Format::WebM, "webm"),
			(Format::SRT, "srt"),
		];
		for (f, ext) in cases {
			assert_eq!(Format::get_extension(f), ext, "{:?}", f);
		}
		assert_eq!(Format::get_extension(Format::Count), "");
	}

	#[test]
	fn from_i32_maps_valid_and_rejects_others() {
		for v in 0..=14 {
			assert!(Format::from_i32(v).is_some(), "format {v}");
		}
		assert!(Format::from_i32(15).is_none()); // Count
		assert!(Format::from_i32(-1).is_none());
		assert!(Format::from_i32(99).is_none());
	}

	#[test]
	fn video_codec_tables() {
		assert_eq!(Format::get_video_codecs(Format::DNxHD), vec![Codec::DNxHD]);
		assert_eq!(
			Format::get_video_codecs(Format::Matroska),
			vec![Codec::H264, Codec::H264RGB, Codec::H265, Codec::VP9]
		);
		assert_eq!(
			Format::get_video_codecs(Format::MPEG4Video),
			vec![Codec::H264, Codec::H264RGB, Codec::H265]
		);
		assert_eq!(Format::get_video_codecs(Format::OpenEXR), vec![Codec::OpenEXR]);
		assert_eq!(Format::get_video_codecs(Format::PNG), vec![Codec::PNG]);
		assert_eq!(Format::get_video_codecs(Format::TIFF), vec![Codec::TIFF]);
		assert_eq!(
			Format::get_video_codecs(Format::QuickTime),
			vec![Codec::H264, Codec::H264RGB, Codec::H265, Codec::ProRes, Codec::CineForm]
		);
		assert_eq!(Format::get_video_codecs(Format::WebM), vec![Codec::AV1, Codec::VP9]);
		// Formats without video codecs.
		assert!(Format::get_video_codecs(Format::WAV).is_empty());
		assert!(Format::get_video_codecs(Format::MP3).is_empty());
		assert!(Format::get_video_codecs(Format::SRT).is_empty());
	}

	#[test]
	fn audio_codec_tables() {
		assert_eq!(Format::get_audio_codecs(Format::WAV), vec![Codec::PCM]);
		assert_eq!(Format::get_audio_codecs(Format::AIFF), vec![Codec::PCM]);
		assert_eq!(Format::get_audio_codecs(Format::MP3), vec![Codec::MP3]);
		assert_eq!(Format::get_audio_codecs(Format::FLAC), vec![Codec::FLAC]);
		assert_eq!(
			Format::get_audio_codecs(Format::Ogg),
			vec![Codec::Opus, Codec::Vorbis, Codec::PCM]
		);
		assert_eq!(Format::get_audio_codecs(Format::DNxHD), vec![Codec::PCM]);
		assert!(Format::get_audio_codecs(Format::PNG).is_empty());
		assert!(Format::get_audio_codecs(Format::SRT).is_empty());
	}

	#[test]
	fn subtitle_and_codec_capability_tables() {
		assert_eq!(Format::get_subtitle_codecs(Format::Matroska), vec![Codec::SRT]);
		assert_eq!(Format::get_subtitle_codecs(Format::SRT), vec![Codec::SRT]);
		assert!(Format::get_subtitle_codecs(Format::MPEG4Video).is_empty());

		// Pixel-format query is a bridge-dependent default (empty list).
		assert!(Format::get_pixel_formats_for_codec(Format::MPEG4Video, Codec::H264).is_empty());

		// PCM exposes its native sample formats; other codecs query the
		// FFmpeg bridge and get an empty list here.
		assert_eq!(
			Format::get_sample_formats_for_codec(Format::WAV, Codec::PCM),
			vec![
				SampleFormat::S16,
				SampleFormat::U8,
				SampleFormat::S32,
				SampleFormat::S64,
				SampleFormat::F32,
				SampleFormat::F64,
			]
		);
		assert!(Format::get_sample_formats_for_codec(Format::MPEG4Video, Codec::AAC).is_empty());
	}
}
