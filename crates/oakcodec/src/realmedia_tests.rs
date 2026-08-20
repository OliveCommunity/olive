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

//! Real-media tests for the FFmpeg decoder/encoder (`#[cfg(test)]` module).
//!
//! These exercise the real `ffmpeg-next` implementation against
//! `tests/demo.mp4` at the repository root (H.264 1920x1080@25fps + AAC
//! 48kHz stereo) and a full H.264 encode round-trip through `/tmp`.
//!
//! They live inside the crate (not `tests/`) because the crate's
//! `#[cfg(test)]` in-memory stubs — which the `Frame`/`FootageDescription`
//! paths need — are only linked for the lib test binary (`tests/` is
//! compiled without `#[cfg(test)]` and cannot resolve those symbols; see
//! `tests/ffi_contract_test.rs`).

use oakcommon::ocioutils::PixelFormat as OakPixelFormat;
use oakcommon::videoparams::VideoParams;
use crate::decoder::{
	CodecStream, Decoder, RenderMode, RetrieveAudioStatus, RetrieveVideoParams,
	K_COLOR_RANGE_DEFAULT,
};
use crate::encoder::create_from_params;
use crate::ffmpeg::FFmpegDecoder;
use crate::frame::Frame;
use oakcore_rs::{PixelFormat, Rational, TimeRange};
use std::sync::Arc;

/// `tests/demo.mp4` at the repository root.
fn demo_path() -> std::path::PathBuf {
	std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../tests/demo.mp4")
}

fn video_params(stream: CodecStream, time: Rational) -> RetrieveVideoParams {
	RetrieveVideoParams {
		stream,
		time,
		length: TimeRange::default(),
		force_range: K_COLOR_RANGE_DEFAULT,
		is_image_sequence: false,
		image_sequence_digits: 0,
		image_sequence_number: 0,
		mode: RenderMode::Offline,
		alpha_is_premultiplied: false,
	}
}

/// H.264 encoder parameters: 64x64, 10 fps, `out` as the target file.
fn h264_params(out: &std::path::Path) -> crate::encodingparams::EncodingParams {
	let mut p = crate::encodingparams::EncodingParams::default();
	let name = out.as_os_str().as_encoded_bytes();
	p.filename[..name.len()].copy_from_slice(name);
	p.format = 2; // MPEG-4 video
	p.video_enabled = 1;
	p.video_codec = 1; // H.264
	p.video_width = 64;
	p.video_height = 64;
	p.video_time_base_num = 1;
	p.video_time_base_den = 10;
	p.video_pixel_format = PixelFormat::F32;
	p.video_interlacing = 0;
	p.video_pixel_aspect_num = 1;
	p.video_pixel_aspect_den = 1;
	p
}

/// Build an allocated F32-RGBA frame with a moving color pattern.
fn pattern_frame(i: i32) -> Frame {
	let mut vp = VideoParams::new_basic(64, 64, OakPixelFormat::from_code(0), 4, 1, 1, 0, 1);
	vp.set_format(OakPixelFormat::from_code(PixelFormat::F32 as i32));
	let mut f = Frame::with_params(vp);
	f.set_timestamp(Rational::new(i as i64, 10));
	f.allocate().unwrap();

	let linesize = f.linesize_bytes() as usize;
	let data = f.data_mut().unwrap();
	for y in 0..64usize {
		for x in 0..64usize {
			let off = y * linesize + x * 16;
			let r: f32 = if x < 32 { 0.4 + i as f32 * 0.05 } else { 0.1 };
			let g: f32 = y as f32 / 64.0;
			let b: f32 = if x >= 32 { 0.7 } else { 0.2 };
			data[off..off + 4].copy_from_slice(&r.to_le_bytes());
			data[off + 4..off + 8].copy_from_slice(&g.to_le_bytes());
			data[off + 8..off + 12].copy_from_slice(&b.to_le_bytes());
			data[off + 12..off + 16].copy_from_slice(&1.0f32.to_le_bytes());
		}
	}
	f
}

#[test]
fn probe_reports_streams_and_duration() {
	let d = FFmpegDecoder::new();
	let desc = d
		.probe(demo_path().to_str().unwrap(), None)
		.expect("demo.mp4 should probe");
	assert_eq!(desc.decoder(), "ffmpeg");
	// video + audio + data (timecode) stream.
	assert_eq!(desc.total_stream_count(), 3);
	assert_eq!(desc.video_stream_count(), 1);
	assert_eq!(desc.audio_stream_count(), 1);

	// Video stream: 1920x1080, 25fps, 17s at 1/12800 time base.
	let vp = desc.get_video_stream(0).expect("video stream");
	assert_eq!(vp.width(), 1920);
	assert_eq!(vp.height(), 1080);
	assert_eq!(vp.duration(), 17 * 12800);
	assert_eq!(vp.frame_rate(), (25, 1));
}

#[test]
fn decode_first_video_frame_has_dimensions_and_content() {
	let d = FFmpegDecoder::new();
	let s = CodecStream::with_block(demo_path().to_string_lossy().into_owned(), 0, None);
	d.open(&s).expect("open video stream");

	let f = d
		.retrieve_video_frame(&video_params(s, Rational::new(0, 1)))
		.expect("decode first frame");
	assert_eq!(f.width(), 1920);
	assert_eq!(f.height(), 1080);
	assert_eq!(f.format(), PixelFormat::F32);
	assert!(f.is_allocated());
	// Expected size: 4 channels x 4 bytes, linesize 32-byte aligned.
	assert_eq!(f.allocated_size(), (16 * 1920) * 1080);

	// The frame must contain non-zero pixels.
	let data = f.data().expect("allocated data");
	assert!(data.iter().any(|&b| b != 0), "decoded frame is all zeros");
}

#[test]
fn decode_video_frame_at_midpoint() {
	let d = FFmpegDecoder::new();
	let s = CodecStream::with_block(demo_path().to_string_lossy().into_owned(), 0, None);
	d.open(&s).expect("open video stream");

	let f = d
		.retrieve_video_frame(&video_params(s, Rational::new(8, 1)))
		.expect("decode mid frame");
	assert_eq!(f.width(), 1920);
	assert_eq!(f.height(), 1080);
	assert!(f.data().unwrap().iter().any(|&b| b != 0));
}

#[test]
fn audio_decode_is_non_empty() {
	let d = FFmpegDecoder::new();
	let s = CodecStream::with_block(demo_path().to_string_lossy().into_owned(), 1, None);
	d.open(&s).expect("open audio stream");

	// One second of stereo at 48 kHz.
	let mut dest = vec![0f32; 48000 * 2];
	let status = d
		.retrieve_audio(
			&mut dest,
			&TimeRange::new(Rational::new(0, 1), Rational::new(1, 1)),
			48000,
			0x3, // stereo mask
		)
		.expect("retrieve audio");
	assert_eq!(status, RetrieveAudioStatus::Success);

	let peak = dest.iter().fold(0.0f32, |a, &s| a.max(s.abs()));
	assert!(peak > 0.0, "decoded audio is all silence");
}

#[test]
fn encode_h264_roundtrip_to_tmp() {
	let out = std::env::temp_dir().join(format!("oakcodec_roundtrip_{}.mp4", std::process::id()));
	let params = h264_params(&out);
	let out_str = out.to_str().expect("utf8 temp path").to_string();

	let e = create_from_params(&params).expect("create ffmpeg encoder");
	assert_eq!(e.id(), "ffmpeg");
	e.configure(&params).expect("configure");
	e.open().expect("open output");

	// Encode 10 frames with a moving pattern.
	for i in 0..10 {
		let f = pattern_frame(i);
		e.write_video(&f).expect("write video frame");
	}
	e.flush().expect("flush");

	// The output exists and has a plausible size.
	assert!(out.exists(), "round-trip file was not created");
	assert!(
		out.metadata().unwrap().len() > 1000,
		"round-trip file is empty"
	);

	// Probe the result: one 64x64 video stream.
	let d = FFmpegDecoder::new();
	let desc = d.probe(&out_str, None).expect("probe round-trip output");
	assert_eq!(desc.video_stream_count(), 1);
	let vp = desc.get_video_stream(0).expect("video stream");
	assert_eq!(vp.width(), 64);
	assert_eq!(vp.height(), 64);

	// Decode the first frame of the result.
	let s = CodecStream::with_block(out_str.clone(), 0, None);
	d.open(&s).expect("open round-trip video");
	let f = d
		.retrieve_video_frame(&video_params(s, Rational::new(0, 1)))
		.expect("decode round-trip first frame");
	assert_eq!(f.width(), 64);
	assert_eq!(f.height(), 64);
	assert_eq!(f.format(), PixelFormat::F32);
	assert!(f.data().unwrap().iter().any(|&b| b != 0));

	let _ = std::fs::remove_file(&out);
}

#[test]
fn audio_conform_writes_planar_pcm() {
	let d = FFmpegDecoder::new();
	let s = CodecStream::with_block(demo_path().to_string_lossy().into_owned(), 1, None);
	d.open(&s).expect("open audio stream");

	let dir = std::env::temp_dir().join(format!("oakcodec_conform_{}", std::process::id()));
	std::fs::create_dir_all(&dir).unwrap();
	let ch0 = dir.join("0.pcm").to_string_lossy().into_owned();
	let ch1 = dir.join("1.pcm").to_string_lossy().into_owned();

	d.conform_audio(&[ch0.clone(), ch1.clone()], 48000, 0x3, 4, None)
		.expect("conform to f32 planar");

	for path in [&ch0, &ch1] {
		let meta = std::fs::metadata(path).expect("conform output exists");
		assert!(meta.len() > 0, "conform file is empty");
		// 1 second at 48kHz * 4 bytes = 192 KB minimum.
		assert!(
			meta.len() >= 192_000,
			"conform file too short: {}",
			meta.len()
		);
	}

	let _ = std::fs::remove_dir_all(&dir);
}

#[test]
fn testmedia_clip_probe_roundtrip() {
	let out = std::env::temp_dir().join(format!("oakcodec_tm3_{}.mp4", std::process::id()));
	crate::testmedia::write_test_clip(&out, 64, 64, 10, 10).expect("generate");
	let d = FFmpegDecoder::new();
	let s = CodecStream::with_block(out.to_string_lossy().into_owned(), 0, None);
	d.open(&s).expect("open");
	for t in [0i64, 3, 5, 9] {
		let f = d
			.retrieve_video_frame(&video_params(s.clone(), Rational::new(t, 10)))
			.unwrap_or_else(|e| panic!("decode t={t}: {e:?}"));
		let stride = f.linesize_bytes() as usize;
		let data = f.data().unwrap();
		let off = 32 * stride + 8 * 16;
		let px = [
			f32::from_le_bytes(data[off..off + 4].try_into().unwrap()),
			f32::from_le_bytes(data[off + 4..off + 8].try_into().unwrap()),
			f32::from_le_bytes(data[off + 8..off + 12].try_into().unwrap()),
			f32::from_le_bytes(data[off + 12..off + 16].try_into().unwrap()),
		];
		println!("t={t}/10 px(8,32): {px:?}");
	}
	let _ = std::fs::remove_file(&out);
}

#[test]
fn testmedia_audio_track_encodes() {
	let out = std::env::temp_dir().join(format!("oakcodec_tm_audio_{}.mp4", std::process::id()));
	crate::testmedia::write_test_clip(&out, 64, 64, 10, 10).expect("generate with audio");
	let d = FFmpegDecoder::new();
	let desc = d.probe(&out.to_string_lossy(), None).expect("probe");
	assert!(desc.audio_stream_count() >= 1, "audio stream present");
	let _ = std::fs::remove_file(&out);
}

/// Serialize the config-toggling hardware-decode test (the config store
/// is process-global; decode tests must not observe each other's flag).
static HW_TEST_LOCK: std::sync::Mutex<()> = std::sync::Mutex::new(());

/// Hardware decoding (the mandated default): with the switch ON the
/// session must open the platform's hardware decoder (on macOS,
/// `h264_videotoolbox` for the H.264 demo); with the switch OFF it must
/// be pure software. Both paths must decode the same frame to matching
/// pixels (small tolerance for decoder rounding).
#[test]
fn hardware_decode_matches_software_decode() {
	let _guard = HW_TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
	let config = oakcommon::configstore::ConfigStore::instance();
	let key = crate::hwdecode::CONFIG_KEY_HARDWARE_DECODING;

	let decode_at = |time: i64| -> (Option<String>, Arc<Frame>) {
		let d = FFmpegDecoder::new();
		let s = CodecStream::with_block(demo_path().to_string_lossy().into_owned(), 0, None);
		d.open(&s).expect("open video stream");
		let hw = d.hw_decoder_name();
		let f = d
			.retrieve_video_frame(&video_params(s, Rational::new(time, 1)))
			.expect("decode frame");
		(hw, f)
	};

	// Hardware path.
	config.set(None, key, "true");
	let (hw_name, hw_frame) = decode_at(5);
	#[cfg(target_os = "macos")]
	{
		let transfers =
			crate::hwdecode::HW_TRANSFERS.load(std::sync::atomic::Ordering::Relaxed);
		if hw_name.as_deref() != Some("videotoolbox") || transfers == 0 {
			// Headless/virtualized macOS (CI runners) cannot bring up
			// VideoToolbox ("hwaccel initialisation returned error"); the
			// decoder then falls back to software and the engagement
			// mandate can only be asserted where the hardware path exists.
			eprintln!("VideoToolbox unavailable on this host; skipping hw assertion");
			return;
		}
	}
	#[cfg(not(target_os = "macos"))]
	assert!(
		hw_name.is_none()
			|| hw_name.as_deref().unwrap().contains("vaapi")
			|| hw_name.as_deref().unwrap().contains("nvdec")
			|| hw_name.as_deref().unwrap().contains("d3d11va"),
		"unexpected decoder {hw_name:?}"
	);

	// Software path (the switch off).
	config.set(None, key, "false");
	let (sw_name, sw_frame) = decode_at(5);
	assert!(sw_name.is_none(), "switch off must force software decoding");
	config.set(None, key, "true");

	// Same geometry, same pixels (within decoder rounding).
	assert_eq!(
		(hw_frame.width(), hw_frame.height()),
		(sw_frame.width(), sw_frame.height())
	);
	let (hw_data, sw_data) = (hw_frame.data().unwrap(), sw_frame.data().unwrap());
	assert_eq!(hw_data.len(), sw_data.len());
	let mut max_diff = 0.0f32;
	for (a, b) in hw_data.chunks_exact(4).zip(sw_data.chunks_exact(4)) {
		let fa = f32::from_le_bytes(a.try_into().unwrap());
		let fb = f32::from_le_bytes(b.try_into().unwrap());
		max_diff = max_diff.max((fa - fb).abs());
	}
	assert!(
		max_diff < 0.05,
		"hardware and software decodes diverge (max channel diff {max_diff})"
	);
}
