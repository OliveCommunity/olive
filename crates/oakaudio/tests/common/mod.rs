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

//! Shared test helpers and bridge stubs for the oakaudio contract suite.
//!
//! The library imports the other oak modules (`oakcommon`, `oakcodec`)
//! through the `bridge/` wrappers. A standalone `cargo test` run has no
//! host dylibs to link, so [`mod stubs`] provides minimal definitions — a
//! no-op config/encoder and a WAV-header probe (real decoding in
//! `waveform::extract` goes through oakcodec's in-process FFmpeg decoder;
//! the processor drives a real FFmpeg filter graph via ffmpeg-next). The
//! exhaustive behavior matrix is pinned by the unchanged C++ gtest suite
//! (`src/audio/tests`).

use std::path::Path;
use std::sync::Mutex;

/// Serializes tests that mutate process-wide state (the manager singleton,
/// the alive ledger) within one test binary.
pub static MANAGER_LOCK: Mutex<()> = Mutex::new(());

/// Build a planar f32 buffer with `channel_count` channels of `frame_count`
/// frames from a single-channel `source` (replicated per channel).
pub fn planar_from(source: &[f32], channel_count: usize) -> Vec<Vec<f32>> {
	(0..channel_count).map(|_| source.to_vec()).collect()
}

/// A deterministic pseudo-random planar buffer (fixed seed) for stable
/// golden vectors. Samples land in `[-1, 1)`.
pub fn noisy_planar(channel_count: usize, frame_count: usize, seed: u64) -> Vec<Vec<f32>> {
	let mut state = seed | 1;
	let mut next = move || {
		state = state
			.wrapping_mul(6364136223846793005)
			.wrapping_add(1442695040888963407);
		((state >> 33) as u64 & 0xFFFF) as f32 / 65535.0 * 2.0 - 1.0
	};
	let mut data = Vec::with_capacity(channel_count);
	for _ in 0..channel_count {
		data.push((0..frame_count).map(|_| next()).collect());
	}
	data
}

/// A silence buffer: every sample is exactly `0.0`.
pub fn silence_planar(channel_count: usize, frame_count: usize) -> Vec<Vec<f32>> {
	vec![vec![0.0; frame_count]; channel_count]
}

/// Total number of `SamplePerChannel` entries in a channel-interleaved
/// waveform sample for `points` points across `channels` channels.
pub fn interleaved_len(points: usize, channels: usize) -> usize {
	points * channels
}

/// Convert an `oakaudio`-style `min_max` pair layout into a plain tuple for
/// comparison in tests.
pub fn pair(min: f32, max: f32) -> (f32, f32) {
	(min, max)
}

// ---- Minimal WAV fixture helpers -------------------------------------------

/// Write a 16-bit PCM WAV file (`fmt` chunk + `data` chunk, standard 44-byte
/// header). The test stubs decode exactly this layout.
pub fn write_wav(path: &Path, channels: u16, rate: u32, samples: &[i16]) -> std::io::Result<()> {
	let block_align = channels * 2;
	let byte_rate = rate * u32::from(block_align);
	let data_size = (samples.len() * 2) as u32;

	let mut out = Vec::with_capacity(44 + data_size as usize);
	out.extend_from_slice(b"RIFF");
	out.extend_from_slice(&(36 + data_size).to_le_bytes());
	out.extend_from_slice(b"WAVE");
	out.extend_from_slice(b"fmt ");
	out.extend_from_slice(&16u32.to_le_bytes());
	out.extend_from_slice(&1u16.to_le_bytes()); // PCM
	out.extend_from_slice(&channels.to_le_bytes());
	out.extend_from_slice(&rate.to_le_bytes());
	out.extend_from_slice(&byte_rate.to_le_bytes());
	out.extend_from_slice(&block_align.to_le_bytes());
	out.extend_from_slice(&16u16.to_le_bytes()); // bits per sample
	out.extend_from_slice(b"data");
	out.extend_from_slice(&data_size.to_le_bytes());
	for s in samples {
		out.extend_from_slice(&s.to_le_bytes());
	}
	std::fs::write(path, out)
}

/// Write a WAV header only (no `data` payload) with arbitrary channel and
/// rate claims — used to exercise extraction validation (e.g. the channel
/// cap) without decoding real audio.
pub fn write_wav_header_only(path: &Path, channels: u16, rate: u32) -> std::io::Result<()> {
	let block_align = channels * 2;
	let byte_rate = rate * u32::from(block_align);

	let mut out = Vec::with_capacity(44);
	out.extend_from_slice(b"RIFF");
	out.extend_from_slice(&36u32.to_le_bytes());
	out.extend_from_slice(b"WAVE");
	out.extend_from_slice(b"fmt ");
	out.extend_from_slice(&16u32.to_le_bytes());
	out.extend_from_slice(&1u16.to_le_bytes());
	out.extend_from_slice(&channels.to_le_bytes());
	out.extend_from_slice(&rate.to_le_bytes());
	out.extend_from_slice(&byte_rate.to_le_bytes());
	out.extend_from_slice(&block_align.to_le_bytes());
	out.extend_from_slice(&16u16.to_le_bytes());
	out.extend_from_slice(b"data");
	out.extend_from_slice(&0u32.to_le_bytes());
	std::fs::write(path, out)
}

// ---- Bridge stubs -----------------------------------------------------------

#[allow(dead_code)]
pub mod stubs {
	use std::ffi::{c_char, c_int, c_void, CStr};
	use std::path::Path;

	use oakaudio::bridge::codec::AudioStreamInfo;
	use oakaudio::handle::CHandle;

	// ------------------------- oakcommon ---------------------------------

	/// Not-found for every key: `config::device_name`'s two-stage query
	/// treats `size <= 1` as absent and returns the empty string, so the
	/// config-driven device lookup degrades to `paNoDevice` (the documented
	/// bridge degradation).
	#[no_mangle]
	pub extern "C" fn oakcommon_config_get(
		_group: *const c_char,
		_key: *const c_char,
		_buf: *mut c_char,
		_buf_size: c_int,
	) -> c_int {
		-1
	}

	/// Every integer config reads its default.
	#[no_mangle]
	pub extern "C" fn oakcommon_config_get_int(
		_group: *const c_char,
		_key: *const c_char,
		default: c_int,
	) -> c_int {
		default
	}

	/// Core `SampleFormat` (planar-first) → `FBSampleFormat` (AVSampleFormat
	/// order), mirroring `src/common/src/ffmpegutils.cpp:83`.
	///
	/// `// CPP-PARITY: src/common/src/ffmpegutils.cpp:83`
	/// (`FFmpegUtils::get_ffmpeg_sample_format`).
	#[no_mangle]
	pub extern "C" fn oakcommon_ffmpegutils_get_ffmpeg_sample_format(
		smp_fmt: c_int,
		out: *mut c_int,
	) -> c_int {
		let mapped = match smp_fmt {
			0 => 5,  // u8_p  -> fb_sample_fmt_u8_p
			1 => 6,  // s16_p -> fb_sample_fmt_s16_p
			2 => 7,  // s32_p -> fb_sample_fmt_s32_p
			3 => 11, // s64_p -> fb_sample_fmt_s64_p
			4 => 8,  // f32_p -> fb_sample_fmt_fltp
			5 => 9,  // f64_p -> fb_sample_fmt_dblp
			6 => 0,  // u8    -> fb_sample_fmt_u8
			7 => 1,  // s16   -> fb_sample_fmt_s16
			8 => 2,  // s32   -> fb_sample_fmt_s32
			9 => 10, // s64   -> fb_sample_fmt_s64
			10 => 3, // f32   -> fb_sample_fmt_flt
			11 => 4, // f64   -> fb_sample_fmt_dbl
			_ => -1, // invalid/count -> fb_sample_fmt_none
		};
		if out.is_null() {
			return -1;
		}
		// SAFETY: the caller guarantees a writable int.
		unsafe { *out = mapped };
		0
	}

	// ------------------------- oakcodec ----------------------------------

	/// ffmpeg-style default channel layout mask for `nb_channels` (only the
	/// popcount is load-bearing for oakaudio). Masks above 63 channels
	/// cannot be represented in a u64 and yield 0 (the extract cap check
	/// rejects such streams before any layout use).
	fn layout_for(channels: i32) -> u64 {
		match channels {
			1 => 0x4,
			2 => 0x3,
			n if n > 0 && n < 64 => (1u64 << n) - 1,
			_ => 0,
		}
	}

	/// WAV header facts parsed by `parse_wav`.
	struct WavInfo {
		channels: i32,
		rate: i32,
		block_align: usize,
		frames: i64,
	}

	/// Parse the standard 44-byte PCM WAV header the fixture writer emits.
	fn parse_wav(path: &Path) -> Option<WavInfo> {
		let bytes = std::fs::read(path).ok()?;
		if bytes.len() < 44 || &bytes[0..4] != b"RIFF" || &bytes[8..12] != b"WAVE" {
			return None;
		}
		if &bytes[12..16] != b"fmt " || &bytes[36..40] != b"data" {
			return None;
		}
		let fmt_size = u32::from_le_bytes(bytes[16..20].try_into().ok()?);
		if fmt_size < 16 {
			return None;
		}
		let audio_format = u16::from_le_bytes(bytes[20..22].try_into().ok()?);
		let channels = u16::from_le_bytes(bytes[22..24].try_into().ok()?);
		let rate = u32::from_le_bytes(bytes[24..28].try_into().ok()?);
		let block_align = u16::from_le_bytes(bytes[32..34].try_into().ok()?);
		let bits = u16::from_le_bytes(bytes[34..36].try_into().ok()?);
		let data_size = u32::from_le_bytes(bytes[40..44].try_into().ok()?);
		if audio_format != 1 || bits != 16 || channels == 0 || rate == 0 || block_align == 0 {
			return None;
		}
		let frames = (data_size as usize / block_align as usize) as i64;
		Some(WavInfo {
			channels: i32::from(channels),
			rate: rate as i32,
			block_align: block_align as usize,
			frames,
		})
	}

	// All handles below use the shared `CHandle` ABI (single-lib
	// unification): oakaudio's `bridge::codec` wrappers call the oakcodec
	// crate's `#[no_mangle]` ffi functions directly, so these stubs must
	// match the real oakcodec ffi signatures exactly.

	struct StubEncoder;

	#[no_mangle]
	pub extern "C" fn oakcodec_encoder_init(params: *const c_void) -> CHandle {
		if params.is_null() {
			return CHandle::null();
		}
		CHandle {
			ctx: Box::into_raw(Box::new(StubEncoder)) as *mut c_void,
			addref: None,
			release: None,
			abi_version: 0,
		}
	}

	#[no_mangle]
	pub extern "C" fn oakcodec_encoder_open(_encoder: CHandle) -> c_int {
		0
	}

	#[no_mangle]
	pub extern "C" fn oakcodec_encoder_write_audio(
		_encoder: CHandle,
		_samples: *const f32,
		_frame_count: c_int,
	) -> c_int {
		0
	}

	#[no_mangle]
	pub extern "C" fn oakcodec_encoder_flush(_encoder: CHandle) -> c_int {
		0
	}

	#[no_mangle]
	pub extern "C" fn oakcodec_encoder_last_error(
		_encoder: CHandle,
		_buf: *mut c_char,
		_buf_size: c_int,
	) -> c_int {
		0
	}

	#[no_mangle]
	pub extern "C" fn oakcodec_encoder_free(encoder: *mut CHandle) {
		if !encoder.is_null() {
			// SAFETY: the pointer was created by `oakcodec_encoder_init`;
			// the caller owns `encoder` and expects it cleared.
			let e = unsafe { &mut *encoder };
			if !e.ctx.is_null() {
				drop(unsafe { Box::from_raw(e.ctx as *mut StubEncoder) });
				e.ctx = std::ptr::null_mut();
			}
		}
	}

	/// Probe result for a decodable WAV file.
	struct StubProbe {
		channels: i32,
		sample_rate: i32,
		frames: i64,
		layout: u64,
	}

	#[no_mangle]
	pub extern "C" fn oakcodec_decoder_probe(filename: *const c_char) -> CHandle {
		if filename.is_null() {
			return CHandle::null();
		}
		// SAFETY: the C string is NUL-terminated (caller contract).
		let cname = unsafe { CStr::from_ptr(filename) };
		let path = Path::new(cname.to_str().unwrap_or(""));
		match parse_wav(path) {
			Some(info) => CHandle {
				ctx: Box::into_raw(Box::new(StubProbe {
					channels: info.channels,
					sample_rate: info.rate,
					frames: info.frames,
					layout: layout_for(info.channels),
				})) as *mut c_void,
				addref: None,
				release: None,
				abi_version: 0,
			},
			None => CHandle::null(),
		}
	}

	#[no_mangle]
	pub extern "C" fn oakcodec_decoder_free(probe: *mut CHandle) {
		if !probe.is_null() {
			// SAFETY: the pointer was created by `oakcodec_decoder_probe`;
			// the caller owns `probe` and expects it cleared.
			let p = unsafe { &mut *probe };
			if !p.ctx.is_null() {
				drop(unsafe { Box::from_raw(p.ctx as *mut StubProbe) });
				p.ctx = std::ptr::null_mut();
			}
		}
	}

	#[no_mangle]
	pub extern "C" fn oakcodec_decoder_probe_audio_stream_count(_probe: CHandle) -> c_int {
		1
	}

	#[no_mangle]
	pub extern "C" fn oakcodec_decoder_probe_get_audio_stream(
		probe: CHandle,
		index: c_int,
		out: *mut AudioStreamInfo,
	) -> c_int {
		if probe.ctx.is_null() || out.is_null() || index != 0 {
			return -1;
		}
		// SAFETY: the probe ctx is a live `StubProbe`; `out` is a
		// caller-owned info struct.
		let p = unsafe { &*(probe.ctx as *const StubProbe) };
		unsafe {
			(*out).stream_index = 0;
			(*out).sample_rate = p.sample_rate;
			(*out).channel_layout = p.layout;
			(*out).channel_count = p.channels;
			(*out).duration_ts = p.frames;
			(*out).time_base_num = 1;
			(*out).time_base_den = p.sample_rate;
		}
		0
	}

	#[no_mangle]
	pub extern "C" fn oakcodec_decoder_open(
		_decoder: CHandle,
		_filename: *const c_char,
		_stream_index: c_int,
	) -> c_int {
		0
	}

	#[no_mangle]
	pub extern "C" fn oakcodec_decoder_decode_audio(
		_decoder: CHandle,
		_in_num: c_int,
		_in_den: c_int,
		_out_num: c_int,
		_out_den: c_int,
		_sample_rate: c_int,
		_channel_layout: u64,
		_buf: *mut f32,
		_buf_frames: c_int,
	) -> c_int {
		0
	}
}
