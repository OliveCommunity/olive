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
//! The library imports the other oak modules (`oakcommon`, `oakcodec`,
//! `ffmpeg_bridge`) through `extern "C"` declarations in `bridge/`. A
//! standalone `cargo test`/`cargo tarpaulin` run has no C++ objects to
//! link, so [`mod stubs`] provides minimal definitions — real enough for
//! the contract tests (a passthrough/linear filter graph, a WAV decoder,
//! a no-op config/encoder) but by no means an ffmpeg replacement. The
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
	use oakaudio::bridge::ffmpeg::{AudioGraphConfig, FBStreamInfo};

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

	// ------------------------- ffmpeg_bridge ------------------------------

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

	#[no_mangle]
	pub extern "C" fn fb_channel_layout_get_channels(mask: u64) -> c_int {
		mask.count_ones() as c_int
	}

	#[no_mangle]
	pub extern "C" fn fb_channel_layout_default(nb_channels: c_int) -> u64 {
		layout_for(nb_channels)
	}

	/// A tiny deterministic filter graph: buffers planar-f32 (or packed s16)
	/// input and emits linearly-interpolated output at `out_rate` with an
	/// atempo-style tempo factor (tempo > 1 speeds up → fewer frames).
	struct StubGraph {
		in_rate: f64,
		out_rate: f64,
		in_channels: usize,
		out_channels: usize,
		in_format: c_int,
		tempo: f64,
		input: Vec<Vec<f32>>,
		emitted: usize,
	}

	impl StubGraph {
		fn available(&self) -> usize {
			let len = self.input.first().map_or(0, |c| c.len());
			if len == 0 {
				return 0;
			}
			let ratio = self.out_rate / (self.in_rate * self.tempo);
			((len as f64 * ratio) + 1e-9).floor() as usize
		}
	}

	#[no_mangle]
	pub extern "C" fn fb_audio_graph_create(config: *const AudioGraphConfig) -> *mut c_void {
		if config.is_null() {
			return std::ptr::null_mut();
		}
		// SAFETY: the caller guarantees a valid config pointer.
		let c = unsafe { &*config };
		if c.in_sample_rate <= 0
			|| c.out_sample_rate <= 0
			|| c.in_channels <= 0
			|| c.out_channels <= 0
		{
			return std::ptr::null_mut();
		}
		let g = StubGraph {
			in_rate: f64::from(c.in_sample_rate),
			out_rate: f64::from(c.out_sample_rate),
			in_channels: c.in_channels as usize,
			out_channels: c.out_channels as usize,
			in_format: c.in_sample_format,
			tempo: c.tempo.max(0.001),
			input: vec![Vec::new(); c.in_channels as usize],
			emitted: 0,
		};
		Box::into_raw(Box::new(g)) as *mut c_void
	}

	#[no_mangle]
	pub extern "C" fn fb_audio_graph_free(graph: *mut *mut c_void) {
		if !graph.is_null() && !(unsafe { *graph }).is_null() {
			// SAFETY: the pointer was created by `fb_audio_graph_create`.
			drop(unsafe { Box::from_raw(*graph as *mut StubGraph) });
			// SAFETY: the double-pointer belongs to the caller.
			unsafe { *graph = std::ptr::null_mut() };
		}
	}

	#[no_mangle]
	pub extern "C" fn fb_audio_graph_push(
		graph: *mut c_void,
		channel_data: *const *const u8,
		nb_samples: c_int,
	) -> c_int {
		if graph.is_null() || nb_samples < 0 {
			return -1;
		}
		// SAFETY: the graph pointer was created by `fb_audio_graph_create`
		// and is still live.
		let g = unsafe { &mut *(graph as *mut StubGraph) };
		if channel_data.is_null() {
			return 0; // flush marker
		}
		for f in 0..nb_samples as usize {
			for c in 0..g.in_channels {
				let v = match g.in_format {
					8 => {
						// fltp: one f32 plane per channel.
						// SAFETY: the caller guarantees `nb_samples` floats
						// per plane.
						let p = unsafe { *channel_data.add(c) } as *const f32;
						unsafe { *p.add(f) }
					}
					1 => {
						// s16 packed: interleaved in plane 0.
						// SAFETY: the caller guarantees `nb_samples *
						// channels * 2` bytes in plane 0.
						let p = unsafe { *channel_data } as *const u8;
						let off = (f * g.in_channels + c) * 2;
						let lo = unsafe { *p.add(off) };
						let hi = unsafe { *p.add(off + 1) };
						f32::from(i16::from_le_bytes([lo, hi])) / 32768.0
					}
					_ => 0.0,
				};
				g.input[c].push(v);
			}
		}
		0
	}

	/// A frame payload: per-plane raw bytes plus sample/format metadata.
	struct StubFrame {
		nb: i32,
		channels: i32,
		format: c_int,
		rate: c_int,
		layout: u64,
		data: Vec<Vec<u8>>,
	}

	impl Default for StubFrame {
		fn default() -> Self {
			StubFrame {
				nb: 0,
				channels: 0,
				format: -1,
				rate: 0,
				layout: 0,
				data: Vec::new(),
			}
		}
	}

	#[no_mangle]
	pub extern "C" fn fb_audio_graph_pull(graph: *mut c_void, out_frame: *mut c_void) -> c_int {
		if graph.is_null() || out_frame.is_null() {
			return -1;
		}
		// SAFETY: both pointers are live (created by the allocators below).
		let g = unsafe { &mut *(graph as *mut StubGraph) };
		let out = unsafe { &mut *(out_frame as *mut StubFrame) };

		let total = g.available();
		if g.emitted >= total {
			return 0;
		}
		let nb = total - g.emitted;

		out.nb = nb as i32;
		out.channels = g.out_channels as i32;
		out.format = 8; // fltp
		out.data = vec![vec![0u8; nb * 4]; g.out_channels];

		for o in 0..nb {
			let pos = o as f64 * g.in_rate / g.out_rate * g.tempo;
			for c in 0..g.out_channels {
				let lower = (pos.floor() as usize).min(g.input[c].len() - 1);
				let upper = (lower + 1).min(g.input[c].len() - 1);
				let frac = pos - lower as f64;
				let v = f64::from(g.input[c][lower]) * (1.0 - frac)
					+ f64::from(g.input[c][upper]) * frac;
				let bytes = (v as f32).to_le_bytes();
				let off = o * 4;
				out.data[c][off..off + 4].copy_from_slice(&bytes);
			}
		}
		g.emitted = total;
		1
	}

	#[no_mangle]
	pub extern "C" fn fb_frame_alloc() -> *mut c_void {
		Box::into_raw(Box::new(StubFrame::default())) as *mut c_void
	}

	#[no_mangle]
	pub extern "C" fn fb_frame_free(frame: *mut *mut c_void) {
		if !frame.is_null() && !(unsafe { *frame }).is_null() {
			// SAFETY: the pointer was created by `fb_frame_alloc`.
			drop(unsafe { Box::from_raw(*frame as *mut StubFrame) });
			// SAFETY: the double-pointer belongs to the caller.
			unsafe { *frame = std::ptr::null_mut() };
		}
	}

	#[no_mangle]
	pub extern "C" fn fb_frame_unref(_frame: *mut c_void) {}

	#[no_mangle]
	pub extern "C" fn fb_frame_get_nb_samples(frame: *const c_void) -> c_int {
		if frame.is_null() {
			return 0;
		}
		// SAFETY: the pointer is a live `StubFrame`.
		unsafe { (*(frame as *const StubFrame)).nb }
	}

	#[no_mangle]
	pub extern "C" fn fb_frame_set_nb_samples(frame: *mut c_void, nb_samples: c_int) {
		if frame.is_null() {
			return;
		}
		// SAFETY: the pointer is a live `StubFrame`.
		unsafe { (*(frame as *mut StubFrame)).nb = nb_samples };
	}

	#[no_mangle]
	pub extern "C" fn fb_frame_get_sample_rate(frame: *const c_void) -> c_int {
		if frame.is_null() {
			return 0;
		}
		// SAFETY: the pointer is a live `StubFrame`.
		unsafe { (*(frame as *const StubFrame)).rate }
	}

	#[no_mangle]
	pub extern "C" fn fb_frame_get_format(frame: *const c_void) -> c_int {
		if frame.is_null() {
			return -1;
		}
		// SAFETY: the pointer is a live `StubFrame`.
		unsafe { (*(frame as *const StubFrame)).format }
	}

	#[no_mangle]
	pub extern "C" fn fb_frame_get_channel_layout_mask(frame: *const c_void) -> u64 {
		if frame.is_null() {
			return 0;
		}
		// SAFETY: the pointer is a live `StubFrame`.
		unsafe { (*(frame as *const StubFrame)).layout }
	}

	#[no_mangle]
	pub extern "C" fn fb_frame_get_data(frame: *mut c_void, plane: c_int) -> *mut u8 {
		if frame.is_null() {
			return std::ptr::null_mut();
		}
		// SAFETY: the pointer is a live `StubFrame`.
		let f = unsafe { &mut *(frame as *mut StubFrame) };
		match f.data.get_mut(plane as usize) {
			Some(v) => v.as_mut_ptr(),
			None => std::ptr::null_mut(),
		}
	}

	#[no_mangle]
	pub extern "C" fn fb_frame_get_data_const(frame: *const c_void, plane: c_int) -> *const u8 {
		if frame.is_null() {
			return std::ptr::null();
		}
		// SAFETY: the pointer is a live `StubFrame`.
		let f = unsafe { &*(frame as *const StubFrame) };
		match f.data.get(plane as usize) {
			Some(v) => v.as_ptr(),
			None => std::ptr::null(),
		}
	}

	#[no_mangle]
	pub extern "C" fn fb_frame_get_linesize(frame: *const c_void, _plane: c_int) -> c_int {
		if frame.is_null() {
			return 0;
		}
		// SAFETY: the pointer is a live `StubFrame`.
		unsafe { (*(frame as *const StubFrame)).nb * 4 }
	}

	/// A raw packet payload (unused by oakaudio, kept for completeness).
	struct StubPacket {
		data: Vec<u8>,
	}

	#[no_mangle]
	pub extern "C" fn fb_packet_alloc() -> *mut c_void {
		Box::into_raw(Box::new(StubPacket { data: Vec::new() })) as *mut c_void
	}

	#[no_mangle]
	pub extern "C" fn fb_packet_free(packet: *mut *mut c_void) {
		if !packet.is_null() && !(unsafe { *packet }).is_null() {
			// SAFETY: the pointer was created by `fb_packet_alloc`.
			drop(unsafe { Box::from_raw(*packet as *mut StubPacket) });
			// SAFETY: the double-pointer belongs to the caller.
			unsafe { *packet = std::ptr::null_mut() };
		}
	}

	#[no_mangle]
	pub extern "C" fn fb_packet_unref(_packet: *mut c_void) {}

	/// 16-bit PCM WAV stream state (the only format the fixture writer
	/// produces).
	struct StubDecoder {
		file: Option<std::fs::File>,
		channels: i32,
		sample_rate: i32,
		block_align: usize,
		remaining: usize,
		total_frames: i64,
		layout: u64,
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

	#[no_mangle]
	pub extern "C" fn fb_decoder_create() -> *mut c_void {
		Box::into_raw(Box::new(StubDecoder {
			file: None,
			channels: 0,
			sample_rate: 0,
			block_align: 0,
			remaining: 0,
			total_frames: 0,
			layout: 0,
		})) as *mut c_void
	}

	#[no_mangle]
	pub extern "C" fn fb_decoder_open(
		decoder: *mut c_void,
		filename: *const c_char,
		stream_index: c_int,
	) -> c_int {
		if decoder.is_null() || filename.is_null() {
			return -1;
		}
		// SAFETY: the C string is NUL-terminated (caller contract).
		let cname = unsafe { CStr::from_ptr(filename) };
		let path = Path::new(cname.to_str().unwrap_or(""));
		match parse_wav(path) {
			Some(info) if stream_index == 0 => {
				// SAFETY: the decoder pointer is a live `StubDecoder`.
				let d = unsafe { &mut *(decoder as *mut StubDecoder) };
				d.file = std::fs::File::open(path).ok();
				// The file cursor starts at 0; skip the 44-byte WAV header
				// so reads land on the data chunk (decoder_read_chunk reads
				// exactly `remaining` data bytes).
				if let Some(f) = d.file.as_mut() {
					use std::io::{Seek, SeekFrom};
					let _ = f.seek(SeekFrom::Start(44));
				}
				d.channels = info.channels;
				d.sample_rate = info.rate;
				d.block_align = info.block_align;
				d.remaining = (info.frames as usize) * info.block_align;
				d.total_frames = info.frames;
				d.layout = layout_for(info.channels);
				0
			}
			_ => -1,
		}
	}

	#[no_mangle]
	pub extern "C" fn fb_decoder_close(_decoder: *mut c_void) {}

	#[no_mangle]
	pub extern "C" fn fb_decoder_free(decoder: *mut *mut c_void) {
		if !decoder.is_null() && !(unsafe { *decoder }).is_null() {
			// SAFETY: the pointer was created by `fb_decoder_create`.
			drop(unsafe { Box::from_raw(*decoder as *mut StubDecoder) });
			// SAFETY: the double-pointer belongs to the caller.
			unsafe { *decoder = std::ptr::null_mut() };
		}
	}

	/// Read up to `max` whole frames of interleaved s16 PCM.
	fn decoder_read_chunk(d: &mut StubDecoder, max_bytes: usize) -> Vec<u8> {
		use std::io::Read;
		if d.file.is_none() || d.remaining == 0 {
			return Vec::new();
		}
		let mut buf = vec![0u8; max_bytes.min(d.remaining)];
		let f = d.file.as_mut().unwrap();
		let mut n = 0usize;
		while n < buf.len() {
			match f.read(&mut buf[n..]) {
				Ok(0) => break,
				Ok(read) => n += read,
				Err(_) => break,
			}
		}
		d.remaining = d.remaining.saturating_sub(n);
		// Keep only whole frames.
		let whole = n / d.block_align * d.block_align;
		buf.truncate(whole);
		buf
	}

	#[no_mangle]
	pub extern "C" fn fb_decoder_get_frame(
		decoder: *mut c_void,
		_packet: *mut c_void,
		frame: *mut c_void,
	) -> c_int {
		if decoder.is_null() || frame.is_null() {
			return -1;
		}
		// SAFETY: both pointers are live stubs.
		let d = unsafe { &mut *(decoder as *mut StubDecoder) };
		let f = unsafe { &mut *(frame as *mut StubFrame) };
		let chunk = decoder_read_chunk(d, 4096);
		if chunk.is_empty() {
			return -1; // EOF
		}
		f.nb = (chunk.len() / d.block_align) as i32;
		f.channels = d.channels;
		f.format = 1; // s16 packed (native WAV format)
		f.rate = d.sample_rate;
		f.layout = d.layout;
		f.data = vec![chunk];
		0
	}

	#[no_mangle]
	pub extern "C" fn fb_decoder_get_packet(decoder: *mut c_void, packet: *mut c_void) -> c_int {
		if decoder.is_null() || packet.is_null() {
			return -1;
		}
		// SAFETY: both pointers are live stubs.
		let d = unsafe { &mut *(decoder as *mut StubDecoder) };
		let p = unsafe { &mut *(packet as *mut StubPacket) };
		let chunk = decoder_read_chunk(d, 4096);
		if chunk.is_empty() {
			return -1;
		}
		p.data = chunk;
		0
	}

	#[no_mangle]
	pub extern "C" fn fb_decoder_get_stream_info(decoder: *const c_void, out: *mut FBStreamInfo) -> c_int {
		if decoder.is_null() || out.is_null() {
			return -1;
		}
		// SAFETY: the decoder pointer is a live `StubDecoder`; `out` is a
		// caller-owned info struct.
		let d = unsafe { &*(decoder as *const StubDecoder) };
		unsafe {
			(*out).index = 0;
			(*out).codec_type = 1; // audio
			(*out).codec_id = 0;
			(*out).has_decoder = 1;
			(*out).width = 0;
			(*out).height = 0;
			(*out).pixel_format = -1;
			(*out).field_order = 0;
			(*out).color_range = 0;
			(*out).color_primaries = 2;
			(*out).color_trc = 2;
			(*out).sample_rate = d.sample_rate;
			(*out).sample_format = 1; // s16
			(*out).channel_layout_mask = d.layout;
			(*out).start_time = 0;
			(*out).duration = d.total_frames;
			(*out).time_base_num = 1;
			(*out).time_base_den = d.sample_rate.max(1);
			(*out).avg_frame_rate_num = 0;
			(*out).avg_frame_rate_den = 0;
		}
		0
	}

	#[no_mangle]
	pub extern "C" fn fb_decoder_get_format_start_time(decoder: *const c_void) -> i64 {
		if decoder.is_null() {
			return 0;
		}
		0
	}

	#[no_mangle]
	pub extern "C" fn fb_decoder_get_format_duration(decoder: *const c_void) -> i64 {
		if decoder.is_null() {
			return 0;
		}
		// SAFETY: the decoder pointer is a live `StubDecoder`.
		unsafe { (*(decoder as *const StubDecoder)).total_frames }
	}

	// ------------------------- oakcodec ----------------------------------

	struct StubEncoder;

	#[no_mangle]
	pub extern "C" fn oakcodec_encoder_init(params: *const c_void) -> *mut c_void {
		if params.is_null() {
			return std::ptr::null_mut();
		}
		Box::into_raw(Box::new(StubEncoder)) as *mut c_void
	}

	#[no_mangle]
	pub extern "C" fn oakcodec_encoder_open(_encoder: *mut c_void) -> c_int {
		0
	}

	#[no_mangle]
	pub extern "C" fn oakcodec_encoder_write_audio(
		_encoder: *mut c_void,
		_samples: *const f32,
		_frame_count: c_int,
	) -> c_int {
		0
	}

	#[no_mangle]
	pub extern "C" fn oakcodec_encoder_flush(_encoder: *mut c_void) -> c_int {
		0
	}

	#[no_mangle]
	pub extern "C" fn oakcodec_encoder_last_error(
		_encoder: *mut c_void,
		_buf: *mut c_char,
		_buf_size: c_int,
	) -> c_int {
		0
	}

	#[no_mangle]
	pub extern "C" fn oakcodec_encoder_free(encoder: *mut c_void) {
		if !encoder.is_null() {
			// SAFETY: the pointer was created by `oakcodec_encoder_init`.
			drop(unsafe { Box::from_raw(encoder as *mut StubEncoder) });
		}
	}

	/// Probe result for a decodable WAV file.
	struct StubProbe {
		channels: i32,
		sample_rate: i32,
		block_align: usize,
		frames: i64,
		layout: u64,
	}

	#[no_mangle]
	pub extern "C" fn oakcodec_decoder_probe(filename: *const c_char) -> *mut c_void {
		if filename.is_null() {
			return std::ptr::null_mut();
		}
		// SAFETY: the C string is NUL-terminated (caller contract).
		let cname = unsafe { CStr::from_ptr(filename) };
		let path = Path::new(cname.to_str().unwrap_or(""));
		match parse_wav(path) {
			Some(info) => {
				Box::into_raw(Box::new(StubProbe {
					channels: info.channels,
					sample_rate: info.rate,
					block_align: info.block_align,
					frames: info.frames,
					layout: layout_for(info.channels),
				})) as *mut c_void
			}
			None => std::ptr::null_mut(),
		}
	}

	#[no_mangle]
	pub extern "C" fn oakcodec_decoder_free(probe: *mut c_void) {
		if !probe.is_null() {
			// SAFETY: the pointer was created by `oakcodec_decoder_probe`.
			drop(unsafe { Box::from_raw(probe as *mut StubProbe) });
		}
	}

	#[no_mangle]
	pub extern "C" fn oakcodec_decoder_probe_audio_stream_count(_probe: *mut c_void) -> c_int {
		1
	}

	#[no_mangle]
	pub extern "C" fn oakcodec_decoder_probe_get_audio_stream(
		probe: *mut c_void,
		index: c_int,
		out: *mut AudioStreamInfo,
	) -> c_int {
		if probe.is_null() || out.is_null() || index != 0 {
			return -1;
		}
		// SAFETY: the probe pointer is a live `StubProbe`; `out` is a
		// caller-owned info struct.
		let p = unsafe { &*(probe as *const StubProbe) };
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
		_decoder: *mut c_void,
		_filename: *const c_char,
		_stream_index: c_int,
	) -> c_int {
		0
	}

	#[no_mangle]
	pub extern "C" fn oakcodec_decoder_decode_audio(
		_decoder: *mut c_void,
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
