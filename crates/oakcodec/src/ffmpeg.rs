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

//! `FFmpegDecoder` / `FFmpegEncoder` — real FFmpeg-backed implementations.
//!
//! Mirrors `src/codec/src/ffmpeg/{ffmpegdecoder,ffmpegencoder}.{h,cpp}`.
//! Unlike the C++ side — which routes everything through the `ffmpeg_bridge`
//! C library — this module calls the [`ffmpeg_next`] crate directly. The
//! bridge existed only to absorb FFmpeg API churn (the `liboakffmpeg`
//! dylib) and is deliberately not replicated. `// CPP-PARITY:` comments
//! point at the oracle lines each behavior mirrors.
//!
//! ## Delivery formats
//!
//! Video decode delivers **F32 RGBA** frames ([`PixelFormat::F32`], the
//! primary oakcore pipeline format): the decoded frame is run through
//! swscale into a float-RGBA target and copied into the crate's [`Frame`]
//! (4 channels, linesize rounded to 32 bytes — see [`Frame`]). Audio is
//! delivered as interleaved `f32`, matching the C ABI.
//!
//! ## Deviations from the C++ oracle (all trait-shaped)
//!
//! * [`Decoder::retrieve_audio`] decodes directly (the C++ path goes
//!   through the ConformManager cache; the Rust trait carries no cache
//!   path or loop mode).
//! * [`RetrieveVideoParams`] drops `renderer`, `divider` and
//!   `maximum_format` (the Rust trait surface), so [`Decoder::retrieve_video`]
//!   can only produce textures through an empty renderer handle and there
//!   is no preview-divider scaling.
//! * Probing uses stream parameters (no second decode pass), so `is_still`
//!   is always false and interlacing always progressive.
//! * Subtitle streams are counted but not added as subtitle entries.

use std::collections::VecDeque;
use std::path::PathBuf;
use std::sync::{Arc, Mutex, OnceLock};

use ffmpeg::ffi as sys;
use ffmpeg::format::sample::Type as SampleType;
use ffmpeg::format::{Pixel, Sample};
use ffmpeg::media::Type as MediaType;
use ffmpeg::software::{resampling, scaling};
use ffmpeg::{ChannelLayout, Dictionary, Error as FfmpegError, Rational as FfRational};
use ffmpeg_next as ffmpeg;

use oakcore_rs::{PixelFormat, Rational, SampleFormat, TimeRange};

use crate::bridge::common::{
	oakcommon_videoparams_init_basic, oakcommon_videoparams_set_channel_count,
	oakcommon_videoparams_set_duration, oakcommon_videoparams_set_format,
	oakcommon_videoparams_set_frame_rate, oakcommon_videoparams_set_height,
	oakcommon_videoparams_set_interlacing, oakcommon_videoparams_set_pixel_aspect_ratio,
	oakcommon_videoparams_set_premultiplied_alpha, oakcommon_videoparams_set_start_time,
	oakcommon_videoparams_set_stream_index, oakcommon_videoparams_set_time_base,
	oakcommon_videoparams_set_video_type, oakcommon_videoparams_set_width,
	oakcore_audioparams_create, oakcore_audioparams_set_duration,
	oakcore_audioparams_set_stream_index, oakcore_audioparams_set_time_base, OakAudioParams,
};
use crate::bridge::render::{oakrender_cancelatom_is_cancelled, OakCancelAtom, OakRenderTexture};
use crate::decoder::{CodecStream, Decoder, RetrieveAudioStatus, RetrieveVideoParams};
use crate::encoder::Encoder;
use crate::encodingparams::EncodingParams;
use crate::footagedescription::{FootageDescription, StreamEntry};
use crate::frame::Frame;

/// `OAKCOMMON_VIDEO_INTERLACE_NONE` (oakcommon `common/videoparams.h`).
const OAKCOMMON_VIDEO_INTERLACE_NONE: i32 = 0;
/// `OAKCOMMON_COLOR_RANGE_FULL`.
const OAKCOMMON_COLOR_RANGE_FULL: i32 = 1;
/// `OAKCOMMON_VIDEO_TYPE_VIDEO`.
const OAKCOMMON_VIDEO_TYPE_VIDEO: i32 = 0;
/// The format-level time base (microseconds), `FB_TIME_BASE` in the bridge.
const FB_TIME_BASE: i64 = 1_000_000;
/// `AV_NOPTS_VALUE`.
const AV_NOPTS_VALUE: i64 = i64::MIN;
/// Channel count of the internal RGBA pipeline layout.
const VIDEO_CHANNELS: i32 = 4;
/// Number of f32 samples in one RGBA pixel (R, G, B, A).
const PIXEL_F32_BYTES: usize = 16;

/// Lazily initialize the FFmpeg libraries (idempotent, at most once).
fn ffmpeg_init() -> crate::error::Result<()> {
	static INIT: OnceLock<Result<(), String>> = OnceLock::new();
	if let Err(e) = INIT
		.get_or_init(|| ffmpeg::init().map_err(|e| format!("ffmpeg initialization failed: {e}")))
	{
		return Err(crate::error::Error::Failed(e.clone()));
	}
	Ok(())
}

/// Wrap an FFmpeg error in the crate error type.
fn ffmpeg_err(e: FfmpegError) -> crate::error::Error {
	crate::error::Error::Failed(format!("ffmpeg error: {e}"))
}

/// Build a generic failure.
fn fail(msg: impl Into<String>) -> crate::error::Error {
	crate::error::Error::Failed(msg.into())
}

/// `cancel_atom_is_cancelled` — NULL/empty-handle-safe check of an
/// oakrender cancel atom (borrowed pointer).
///
/// # CPP-PARITY
/// `src/codec/src/ffmpeg/ffmpegdecoder.cpp` (anonymous namespace helper).
fn cancel_atom_is_cancelled(cancelled: Option<&OakCancelAtom>) -> bool {
	match cancelled {
		Some(atom) if !atom.ctx.is_null() => unsafe {
			oakrender_cancelatom_is_cancelled(atom.clone()) != 0
		},
		_ => false,
	}
}

/// Whether an FFmpeg error means "end of stream" or "try again".
fn is_eof_or_eagain(e: &FfmpegError) -> bool {
	matches!(e, FfmpegError::Eof)
		|| matches!(e, FfmpegError::Other { errno } if *errno == ffmpeg::error::EAGAIN)
}

/// Map a "JPEG" (full-range) pixel format to its regular counterpart,
/// mirroring the bridge `convert_jpeg_space_to_regular_space`.
fn convert_jpeg_space_to_regular_space(p: Pixel) -> Pixel {
	match p {
		Pixel::YUVJ420P => Pixel::YUV420P,
		Pixel::YUVJ422P => Pixel::YUV422P,
		Pixel::YUVJ444P => Pixel::YUV444P,
		Pixel::YUVJ440P => Pixel::YUV440P,
		Pixel::YUVJ411P => Pixel::YUV411P,
		other => other,
	}
}

/// Native `OakPixelFormat` for a decoded pixel format (probe reporting).
/// Mirrors `FFmpegDecoder::get_native_pixel_format`: 8-bit sources map to
/// [`PixelFormat::U8`], 16-bit to [`PixelFormat::U16`], float to
/// [`PixelFormat::F32`].
fn native_pixel_format(p: Pixel) -> PixelFormat {
	match p {
		Pixel::RGBF32BE
		| Pixel::RGBF32LE
		| Pixel::RGBAF32BE
		| Pixel::RGBAF32LE
		| Pixel::GBRPF32BE
		| Pixel::GBRPF32LE => PixelFormat::F32,
		Pixel::RGB48BE
		| Pixel::RGB48LE
		| Pixel::RGBA64BE
		| Pixel::RGBA64LE
		| Pixel::GBRP9BE
		| Pixel::GBRP9LE
		| Pixel::GBRP10BE
		| Pixel::GBRP10LE
		| Pixel::GBRP12BE
		| Pixel::GBRP12LE
		| Pixel::GBRP14BE
		| Pixel::GBRP14LE
		| Pixel::GBRP16BE
		| Pixel::GBRP16LE => PixelFormat::U16,
		_ => PixelFormat::U8,
	}
}

/// Build an ffmpeg channel layout from a legacy ffmpeg channel mask.
///
/// Dipped into `ffmpeg-sys-next`: `ffmpeg-next` 9 exposes `ChannelLayout`
/// as a wrapper around `AVChannelLayout` (no mask constructor).
fn channel_layout_from_mask(mask: u64) -> ChannelLayout {
	if mask == 0 {
		return ChannelLayout::default(2);
	}
	let channels = mask.count_ones() as i32;
	ChannelLayout(sys::AVChannelLayout {
		order: sys::AVChannelOrder::AV_CHANNEL_ORDER_NATIVE,
		nb_channels: channels,
		u: sys::AVChannelLayout__bindgen_ty_1 { mask },
		opaque: std::ptr::null_mut(),
	})
}

/// Convert a raw `AVSampleFormat` discriminant (as stored in
/// `AVCodecParameters.format`) into an ffmpeg [`Sample`].
///
/// Dipped into `ffmpeg-sys-next`: bindgen emits the C enum as a `#[repr(i32)]`
/// Rust enum, which cannot be built from an `i32` with an `as` cast.
fn sample_from_raw(v: i32) -> Sample {
	// SAFETY: `v` is always a discriminant FFmpeg itself produced; the
	// `repr(i32)` C enum has the same size and layout as `i32`.
	Sample::from(unsafe { std::mem::transmute::<i32, sys::AVSampleFormat>(v) })
}

/// Convert a raw `AVPixelFormat` discriminant (as stored in
/// `AVCodecParameters.format`) into an ffmpeg [`Pixel`].
///
/// Dipped into `ffmpeg-sys-next` (see [`sample_from_raw`]).
fn pixel_from_raw(v: i32) -> Pixel {
	// SAFETY: see `sample_from_raw`.
	Pixel::from(unsafe { std::mem::transmute::<i32, sys::AVPixelFormat>(v) })
}

/// The oakcore timebase equivalent of an ffmpeg rational.
fn oak_rational(r: FfRational) -> Rational {
	Rational::new(r.0 as i64, r.1 as i64)
}

// ---------------------------------------------------------------------------
// FFmpegDecoder
// ---------------------------------------------------------------------------

/// `olive::FFmpegDecoder` — FFmpeg-backed media decoder.
pub struct FFmpegDecoder {
	/// Opened stream (locked).
	stream: std::sync::Mutex<Option<CodecStream>>,
	/// The open FFmpeg decode session, if any.
	state: Mutex<Option<DecoderState>>,
}

impl FFmpegDecoder {
	/// New, closed decoder.
	pub fn new() -> Self {
		FFmpegDecoder {
			stream: std::sync::Mutex::new(None),
			state: Mutex::new(None),
		}
	}
}

impl Default for FFmpegDecoder {
	fn default() -> Self {
		Self::new()
	}
}

impl Decoder for FFmpegDecoder {
	fn id(&self) -> String {
		"ffmpeg".to_string()
	}

	fn supports_video(&self) -> bool {
		true
	}

	fn supports_audio(&self) -> bool {
		true
	}

	fn probe(
		&self,
		filename: &str,
		cancelled: Option<&OakCancelAtom>,
	) -> Option<FootageDescription> {
		ffmpeg_init().ok()?;
		probe_file(filename, cancelled)
	}

	fn open(&self, stream: &CodecStream) -> crate::error::Result<()> {
		ffmpeg_init()?;

		// # CPP-PARITY
		// `src/codec/src/decoder.cpp` `Decoder::open`: already-open decoders
		// accept the same stream again, reject a different one; on failure the
		// stream is unset again.
		let mut stream_guard = self.stream.lock().unwrap_or_else(|e| e.into_inner());
		if let Some(cur) = stream_guard.as_ref() {
			if cur == stream {
				return Ok(());
			}
			return Err(crate::error::Error::State);
		}
		if !stream.is_valid() {
			return Err(crate::error::Error::Invalid);
		}
		if !stream.exists() {
			return Err(crate::error::Error::NotFound);
		}

		let opened = DecoderState::open(stream)?;
		*self.state.lock().unwrap_or_else(|e| e.into_inner()) = Some(opened);
		*stream_guard = Some(stream.clone());
		Ok(())
	}

	fn close(&self) -> crate::error::Result<()> {
		// C++ `Decoder::close` is void and safe when closed.
		*self.stream.lock().unwrap_or_else(|e| e.into_inner()) = None;
		*self.state.lock().unwrap_or_else(|e| e.into_inner()) = None;
		Ok(())
	}

	fn stream(&self) -> CodecStream {
		self.stream
			.lock()
			.unwrap_or_else(|e| e.into_inner())
			.clone()
			.unwrap_or_else(CodecStream::new)
	}

	fn retrieve_video_frame(&self, p: &RetrieveVideoParams) -> crate::error::Result<Arc<Frame>> {
		ffmpeg_init()?;
		let mut state = self.state.lock().unwrap_or_else(|e| e.into_inner());
		let state = state.as_mut().ok_or(crate::error::Error::State)?;
		if !matches!(state.inner, DecoderInner::Video(_)) {
			return Err(fail("decoder is not open on a video stream"));
		}

		// # CPP-PARITY
		// `retrieve_video_frame_internal` (ffmpegdecoder.cpp): the decoded
		// frame is color-range-forced, scaled and copied into a `Frame` at
		// the requested timestamp.
		// Note: the Rust `RetrieveVideoParams` carries no cancellation atom
		// (dropped from the C++ struct), so decoding is not cancellable here.
		let f = state
			.retrieve_frame(&p.time, p.time == crate::decoder::k_any_timecode(), None)?
			.ok_or_else(|| fail("no video frame available at the requested time"))?;

		let (w, h, bytes) = state.scale_video_to_f32(f, p.force_range)?;
		let frame = copy_rgba_f32_to_frame(w, h, &bytes, p.time)?;
		Ok(Arc::new(frame))
	}

	fn retrieve_video(&self, p: &RetrieveVideoParams) -> crate::error::Result<OakRenderTexture> {
		// The Rust `RetrieveVideoParams` carries no `OakRenderRenderer`, so
		// texture creation cannot be performed — the C++ failure path returns
		// an empty texture (ffmpegdecoder.cpp `process_frame_into_texture`).
		ffmpeg_init()?;
		let mut state = self.state.lock().unwrap_or_else(|e| e.into_inner());
		let state = state.as_mut().ok_or(crate::error::Error::State)?;
		if !matches!(state.inner, DecoderInner::Video(_)) {
			return Err(fail("decoder is not open on a video stream"));
		}
		let _ = state.retrieve_frame(&p.time, p.time == crate::decoder::k_any_timecode(), None)?;
		Ok(OakRenderTexture {
			ctx: std::ptr::null_mut(),
			addref: None,
			release: None,
			abi_version: crate::handle::OAKCODEC_ABI_VERSION,
		})
	}

	fn retrieve_audio(
		&self,
		dest: &mut [f32],
		range: &TimeRange,
		sample_rate: i32,
		channel_layout: u64,
	) -> crate::error::Result<RetrieveAudioStatus> {
		ffmpeg_init()?;
		let mut state = self.state.lock().unwrap_or_else(|e| e.into_inner());
		let state = state.as_mut().ok_or(crate::error::Error::State)?;
		if !matches!(state.inner, DecoderInner::Audio(_)) {
			// The stream does not support audio.
			return Ok(RetrieveAudioStatus::Unsupported);
		}
		if sample_rate <= 0 || channel_layout == 0 {
			return Ok(RetrieveAudioStatus::Unsupported);
		}
		state.retrieve_audio_to(dest, range, sample_rate, channel_layout)
	}

	fn conform_audio(
		&self,
		output_filenames: &[String],
		sample_rate: i32,
		channel_layout: u64,
		sample_format: i32,
		cancelled: Option<&OakCancelAtom>,
	) -> crate::error::Result<()> {
		ffmpeg_init()?;
		let mut state = self.state.lock().unwrap_or_else(|e| e.into_inner());
		let state = state.as_mut().ok_or(crate::error::Error::State)?;
		if !matches!(state.inner, DecoderInner::Audio(_)) {
			return Err(fail("decoder is not open on an audio stream"));
		}
		state.conform_audio_to(
			output_filenames,
			sample_rate,
			channel_layout,
			sample_format,
			cancelled,
		)
	}

	fn get_audio_start_offset(&self) -> Rational {
		// # CPP-PARITY
		// `FFmpegDecoder::get_audio_start_offset`: format start minus stream
		// start, in seconds.
		let state = self.state.lock().unwrap_or_else(|e| e.into_inner());
		match state.as_ref() {
			Some(s) if s.format_start_time != AV_NOPTS_VALUE => {
				let fmt_start = Rational::new(s.format_start_time, FB_TIME_BASE);
				let str_start =
					oak_rational(s.stream_time_base).timestamp_to_time(s.stream_start_time);
				fmt_start - str_start
			}
			_ => Rational::new(0, 1),
		}
	}
}

/// A decoded frame plus its media type.
enum DecodedFrame {
	Video(ffmpeg::frame::Video),
	Audio(ffmpeg::frame::Audio),
}

/// Outcome of a single decoder receive attempt.
enum Pull {
	Frame(DecodedFrame),
	Eof,
	Eagain,
}

/// Whether feeding one packet advanced the decode.
enum PacketFeed {
	Sent,
	InputEof,
}

/// One opened (filename, stream) decode session.
struct DecoderState {
	input: ffmpeg::format::context::Input,
	stream_index: usize,
	inner: DecoderInner,
	stream_time_base: FfRational,
	stream_start_time: i64,
	/// Format start time in microseconds (`AV_TIME_BASE`).
	format_start_time: i64,
	input_sample_format: Sample,
	input_sample_rate: u32,
	input_channel_layout_mask: u64,
	/// True once the decoder has been drained.
	eof: bool,
	video: Option<VideoDecodeState>,
	audio: Option<AudioDecodeState>,
}

enum DecoderInner {
	Video(ffmpeg::codec::decoder::Video),
	Audio(ffmpeg::codec::decoder::Audio),
}

// SAFETY: `DecoderState` owns raw FFmpeg contexts that are not thread-safe
// themselves, but every access goes through the `FFmpegDecoder::state`
// `Mutex` (the C++ decoder uses the same lock-discipline around its bridge
// instance), so concurrent access never happens.
unsafe impl Send for DecoderState {}
unsafe impl Sync for DecoderState {}

/// Video decode state: the frame cache plus a cached swscale context.
struct VideoDecodeState {
	scaler: Option<ScalingCache>,
	cache: VecDeque<ffmpeg::frame::Video>,
	cache_at_zero: bool,
	cache_at_eof: bool,
	/// One second in the stream's time base.
	second_ts: i64,
}

/// Cached swscale context (recreated when any dimension/format changes).
struct ScalingCache {
	ctx: scaling::Context,
	src_format: Pixel,
	src_width: u32,
	src_height: u32,
	dst_format: Pixel,
	dst_width: u32,
	dst_height: u32,
}

/// Audio decode state: a resampler keyed by (rate, layout).
struct AudioDecodeState {
	resampler: Option<(u32, u64, AudioResampler)>,
}

/// A swresample conversion context.
struct AudioResampler {
	ctx: resampling::Context,
	dst_format: Sample,
	dst_layout: ChannelLayout,
	dst_rate: u32,
	dst_channels: usize,
}

impl DecoderState {
	/// Open `(filename, stream_index)` for decoding.
	fn open(stream: &CodecStream) -> crate::error::Result<DecoderState> {
		let mut dict = Dictionary::new();
		dict.set("analyzeduration", "5000000");
		dict.set("probesize", "20000000");
		let input =
			ffmpeg::format::input_with_dictionary(&stream.filename(), dict).map_err(ffmpeg_err)?;

		let stream_index = stream.stream() as usize;
		let fstream = input
			.stream(stream_index)
			.ok_or_else(|| fail(format!("no stream {}", stream.stream())))?;
		let params = fstream.parameters();
		let medium = params.medium();
		let codec_id = params.id();
		let codec = ffmpeg::decoder::find(codec_id)
			.ok_or_else(|| fail(format!("no decoder for codec {codec_id:?}")))?;

		// Per-medium stream parameters (mirroring the bridge stream info);
		// read before `params` is moved into the codec context below.
		let mut input_sample_format = Sample::None;
		let mut input_sample_rate = 0;
		let mut input_channel_layout_mask = 0u64;
		if matches!(medium, MediaType::Audio) {
			let raw = unsafe { params.as_ptr() };
			input_sample_format = sample_from_raw(unsafe { (*raw).format });
			input_sample_rate = unsafe { (*raw).sample_rate }.max(0) as u32;
			input_channel_layout_mask = unsafe { ChannelLayout::from((*raw).ch_layout) }.bits();
		}

		let mut open_opts = Dictionary::new();
		open_opts.set("threads", "auto");
		let opened = ffmpeg::codec::Context::from_parameters(params)
			.map_err(ffmpeg_err)?
			.decoder()
			.open_as_with(codec, open_opts)
			.map_err(ffmpeg_err)?;

		let inner = match medium {
			MediaType::Video => DecoderInner::Video(ffmpeg::codec::decoder::Video(opened)),
			MediaType::Audio => DecoderInner::Audio(ffmpeg::codec::decoder::Audio(opened)),
			other => {
				return Err(fail(format!(
					"stream {} is {:?}, expected video or audio",
					stream.stream(),
					other
				)))
			}
		};

		let stream_time_base = fstream.time_base();
		let stream_start_time = fstream.start_time();
		let format_start_time = unsafe { (*input.as_ptr()).start_time };

		// One second in the stream's time base (round of den/num).
		let second_ts = (stream_time_base.1 as f64 / stream_time_base.0 as f64).round() as i64;
		let video = if matches!(medium, MediaType::Video) {
			Some(VideoDecodeState {
				scaler: None,
				cache: VecDeque::new(),
				cache_at_zero: false,
				cache_at_eof: false,
				second_ts,
			})
		} else {
			None
		};
		let audio = if matches!(medium, MediaType::Audio) {
			Some(AudioDecodeState { resampler: None })
		} else {
			None
		};

		Ok(DecoderState {
			input,
			stream_index,
			inner,
			stream_time_base,
			stream_start_time,
			format_start_time,
			input_sample_format,
			input_sample_rate,
			input_channel_layout_mask,
			eof: false,
			video,
			audio,
		})
	}

	fn send_packet(&mut self, packet: &ffmpeg::packet::Packet) -> crate::error::Result<()> {
		match &mut self.inner {
			DecoderInner::Video(d) => d.send_packet(packet).map_err(ffmpeg_err),
			DecoderInner::Audio(d) => d.send_packet(packet).map_err(ffmpeg_err),
		}
	}

	fn send_eof(&mut self) -> crate::error::Result<()> {
		match &mut self.inner {
			DecoderInner::Video(d) => d.send_eof().map_err(ffmpeg_err),
			DecoderInner::Audio(d) => d.send_eof().map_err(ffmpeg_err),
		}
	}

	/// One receive attempt on the decoder.
	fn pull(&mut self) -> crate::error::Result<Pull> {
		let result = match &mut self.inner {
			DecoderInner::Video(d) => {
				let mut scratch = ffmpeg::frame::Video::empty();
				d.receive_frame(&mut scratch).map(|_| {
					Pull::Frame(DecodedFrame::Video(std::mem::replace(
						&mut scratch,
						ffmpeg::frame::Video::empty(),
					)))
				})
			}
			DecoderInner::Audio(d) => {
				let mut scratch = ffmpeg::frame::Audio::empty();
				d.receive_frame(&mut scratch).map(|_| {
					Pull::Frame(DecodedFrame::Audio(std::mem::replace(
						&mut scratch,
						ffmpeg::frame::Audio::empty(),
					)))
				})
			}
		};
		match result {
			Ok(p) => Ok(p),
			Err(FfmpegError::Eof) => Ok(Pull::Eof),
			Err(FfmpegError::Other { errno }) if errno == ffmpeg::error::EAGAIN => Ok(Pull::Eagain),
			Err(e) => Err(ffmpeg_err(e)),
		}
	}

	/// Feed the next packet of the opened stream into the decoder.
	fn feed_packet(&mut self) -> crate::error::Result<PacketFeed> {
		let mut pkt = ffmpeg::packet::Packet::empty();
		match pkt.read(&mut self.input) {
			Ok(()) => {
				if pkt.stream() == self.stream_index {
					self.send_packet(&pkt)?;
				}
				Ok(PacketFeed::Sent)
			}
			Err(FfmpegError::Eof) => {
				self.send_eof()?;
				Ok(PacketFeed::InputEof)
			}
			Err(e) => Err(ffmpeg_err(e)),
		}
	}

	/// Pull the next decoded frame, feeding packets as needed. Returns
	/// `Ok(None)` once the decoder is drained.
	fn next_frame(&mut self) -> crate::error::Result<Option<DecodedFrame>> {
		if self.eof {
			return Ok(None);
		}
		loop {
			match self.pull()? {
				Pull::Frame(f) => return Ok(Some(f)),
				Pull::Eof => {
					self.eof = true;
					return Ok(None);
				}
				Pull::Eagain => {
					self.feed_packet()?;
				}
			}
		}
	}

	/// Seek to `timestamp` (in the stream's time base) and flush the decoder.
	///
	/// # CPP-PARITY
	/// `fb_decoder_seek` semantics (backward seek on the stream, decoder
	/// flushed afterwards).
	fn seek(&mut self, timestamp: i64) -> crate::error::Result<()> {
		let stream_tb = self
			.input
			.stream(self.stream_index)
			.map(|s| s.time_base())
			.unwrap_or(FfRational(1, 1));
		let target =
			unsafe { sys::av_rescale_q(timestamp, self.stream_time_base.into(), stream_tb.into()) };
		let ret = unsafe {
			sys::av_seek_frame(
				self.input.as_mut_ptr(),
				self.stream_index as i32,
				target,
				sys::AVSEEK_FLAG_BACKWARD,
			)
		};
		if ret < 0 {
			return Err(ffmpeg_err(FfmpegError::from(ret)));
		}
		match &mut self.inner {
			DecoderInner::Video(d) => d.flush(),
			DecoderInner::Audio(d) => d.flush(),
		}
		self.eof = false;
		Ok(())
	}

	/// Retrieve the video frame at (or before) `time`, mirroring the C++
	/// `retrieve_frame` seek/cache logic.
	fn retrieve_frame(
		&mut self,
		time: &Rational,
		any_timecode: bool,
		cancelled: Option<&OakCancelAtom>,
	) -> crate::error::Result<Option<ffmpeg::frame::Video>> {
		// Move the video state out so `self.seek` / `self.pull` (which touch
		// other fields) can be called without conflicting borrows.
		let mut video = self
			.video
			.take()
			.expect("retrieve_frame requires a video session");

		let mut target_ts = oak_rational(self.stream_time_base).time_to_timestamp(*time);
		if self.format_start_time != AV_NOPTS_VALUE {
			target_ts += unsafe {
				sys::av_rescale_q(
					self.format_start_time,
					FfRational(1, FB_TIME_BASE as i32).into(),
					self.stream_time_base.into(),
				)
			};
		}

		const MIN_SEEK: i64 = 0;
		let mut seek_ts = (target_ts - maximum_queue_size() as i64).max(MIN_SEEK);
		let mut still_seeking = false;

		if !any_timecode {
			// If the frame wasn't in the frame cache, see if this cache is
			// too old to use (CPP-PARITY ffmpegdecoder.cpp:975).
			if video.cache.is_empty()
				|| target_ts < pts_of(video.cache.front()).unwrap_or(AV_NOPTS_VALUE)
				|| target_ts
					> pts_of(video.cache.back()).unwrap_or(AV_NOPTS_VALUE) + 2 * video.second_ts
			{
				video.cache.clear();
				video.cache_at_eof = false;

				self.seek(seek_ts)?;
				if seek_ts == MIN_SEEK {
					video.cache_at_zero = true;
				}

				still_seeking = true;
			} else if let Some(cached) = get_frame_from_cache(&video, target_ts) {
				self.video = Some(video);
				return Ok(Some(cached));
			}
		}

		let mut retried_after_eof = false;
		let mut return_frame: Option<ffmpeg::frame::Video> = None;

		loop {
			if cancel_atom_is_cancelled(cancelled) {
				break;
			}

			let frame = match self.pull()? {
				Pull::Frame(DecodedFrame::Video(f)) => f,
				Pull::Frame(_) => unreachable!("video session yields only video frames"),
				Pull::Eof => {
					// Handle an "expected" EOF by using the last cached frame
					// (CPP-PARITY ffmpegdecoder.cpp:1043).
					video.cache_at_eof = true;
					if video.cache.is_empty() {
						if !retried_after_eof {
							retried_after_eof = true;
							video.cache.clear();
							self.seek(MIN_SEEK)?;
							video.cache_at_zero = true;
							still_seeking = true;
							continue;
						}
						self.video = Some(video);
						return Err(fail("unexpected codec EOF - unable to retrieve frame"));
					}
					return_frame = video.cache.back().cloned();
					break;
				}
				Pull::Eagain => {
					self.feed_packet()?;
					continue;
				}
			};

			if cancel_atom_is_cancelled(cancelled) {
				break;
			}

			let frame_pts = frame.pts().unwrap_or(AV_NOPTS_VALUE);

			if still_seeking {
				// Handle a failed seek: step back by one second and retry
				// (CPP-PARITY ffmpegdecoder.cpp:1028).
				if !video.cache_at_zero && frame_pts > target_ts {
					seek_ts = (seek_ts - video.second_ts).max(MIN_SEEK);
					self.seek(seek_ts)?;
					if seek_ts == MIN_SEEK {
						video.cache_at_zero = true;
					}
					continue;
				}
				still_seeking = false;
			}

			// Cut the cache down to `maximum_queue_size` before pushing
			// (CPP-PARITY ffmpegdecoder.cpp:1065).
			if video.cache.len() > maximum_queue_size() {
				video.cache.pop_front();
				video.cache_at_zero = false;
			}

			let previous = video.cache.back().cloned();
			video.cache.push_back(frame);

			if frame_pts == target_ts || any_timecode {
				return_frame = video.cache.back().cloned();
				break;
			} else if frame_pts > target_ts {
				if previous.is_none() && video.cache_at_zero {
					return_frame = video.cache.back().cloned();
				} else {
					return_frame = previous;
				}
				break;
			}
		}

		self.video = Some(video);
		Ok(return_frame)
	}

	/// Scale a decoded frame to float RGBA (F32, 4 channels), returning the
	/// raw pixel bytes plus dimensions. Mirrors `pre_process_frame` +
	/// `retrieve_video_frame_internal` scaling with the color-range forcing.
	fn scale_video_to_f32(
		&mut self,
		f: ffmpeg::frame::Video,
		force_range: i32,
	) -> crate::error::Result<(u32, u32, Vec<u8>)> {
		let video = self
			.video
			.as_mut()
			.expect("scale_video_to_f32 requires a video session");

		// # CPP-PARITY ffmpegdecoder.cpp:376: disregard "JPEG" pixel formats
		// and force the color range to whatever the caller requested.
		let src_format = convert_jpeg_space_to_regular_space(f.format());
		let mut f = f;
		f.set_format(src_format);
		f.set_color_range(if force_range == OAKCOMMON_COLOR_RANGE_FULL {
			ffmpeg::color::Range::JPEG
		} else {
			ffmpeg::color::Range::MPEG
		});

		let (w, h) = (f.width(), f.height());

		// swscale cannot reliably output float RGBA on every build; prefer
		// RGBAF32LE and fall back to RGBA64 (converted to f32 below).
		let bytes =
			match get_or_create_scaler(&mut video.scaler, src_format, w, h, Pixel::RGBAF32LE, w, h)
			{
				Ok(ctx) => {
					let mut out = ffmpeg::frame::Video::empty();
					ctx.run(&f, &mut out).map_err(ffmpeg_err)?;
					let stride = out.stride(0);
					convert_rgba_f32_le(&out.data(0), w, h, stride)
				}
				Err(_) => {
					let ctx = get_or_create_scaler(
						&mut video.scaler,
						src_format,
						w,
						h,
						Pixel::RGBA64LE,
						w,
						h,
					)?;
					let mut out = ffmpeg::frame::Video::empty();
					ctx.run(&f, &mut out).map_err(ffmpeg_err)?;
					let stride = out.stride(0);
					convert_rgba64_to_f32(&out.data(0), w, h, stride)
				}
			};
		Ok((w, h, bytes))
	}

	/// Fill `dest` (interleaved f32) with the decoded audio covering
	/// `range`, resampled to `sample_rate` / `channel_layout`. Samples
	/// outside the decoded media are left as silence.
	fn retrieve_audio_to(
		&mut self,
		dest: &mut [f32],
		range: &TimeRange,
		sample_rate: i32,
		channel_layout: u64,
	) -> crate::error::Result<RetrieveAudioStatus> {
		let dst_layout = channel_layout_from_mask(channel_layout);
		let dst_channels = dst_layout.channels().max(0) as usize;
		if dst_channels == 0 {
			return Ok(RetrieveAudioStatus::Unsupported);
		}

		let start_sec = range.in_().to_f64();
		let end_sec = range.out().to_f64();
		let start_sample = (start_sec * sample_rate as f64).round() as i64;
		let end_sample = (end_sec * sample_rate as f64).round() as i64;
		if end_sample <= start_sample {
			return Ok(RetrieveAudioStatus::Success);
		}

		dest.fill(0.0);

		// Seek to just before the range start.
		let start_ts =
			oak_rational(self.stream_time_base).time_to_timestamp(Rational::from_double(start_sec));
		self.seek(start_ts)?;

		// Take the cached resampler out (or create one) so the decode loop
		// below can borrow `self` freely; it is put back before returning.
		let src_layout = channel_layout_from_mask(self.input_channel_layout_mask);
		let mut resampler = match self.audio.as_mut().expect("audio session").resampler.take() {
			Some((rate, layout, rs)) if rate == sample_rate as u32 && layout == channel_layout => {
				rs
			}
			_ => AudioResampler::get(
				self.input_sample_format,
				src_layout,
				self.input_sample_rate,
				Sample::F32(SampleType::Packed),
				dst_layout,
				sample_rate as u32,
			)?,
		};
		let stream_time_base = self.stream_time_base;

		let mut next_sample: Option<i64> = None;
		let dest_frames = (dest.len() / dst_channels) as i64;

		while let Some(frame) = self.next_frame()? {
			let DecodedFrame::Audio(audio) = frame else {
				continue;
			};
			let converted = resample_to_interleaved_f32(&mut resampler, &audio)?;
			if converted.is_empty() {
				continue;
			}
			let chunk_samples = (converted.len() / dst_channels) as i64;
			let frame_start = match audio.pts() {
				Some(pts) => {
					let secs = oak_rational(stream_time_base)
						.timestamp_to_time(pts)
						.to_f64();
					(secs * sample_rate as f64).round() as i64
				}
				None => next_sample.unwrap_or(start_sample),
			};
			let offset = frame_start - start_sample;
			if offset < dest_frames && offset + chunk_samples > 0 {
				let copy_start = offset.max(0) as usize;
				let copy_end = (offset + chunk_samples).min(dest_frames).max(0) as usize;
				if copy_end > copy_start {
					let src_off = (copy_start as i64 - offset) as usize * dst_channels;
					let n = (copy_end - copy_start) * dst_channels;
					let dst_off = copy_start * dst_channels;
					if dst_off + n <= dest.len() {
						dest[dst_off..dst_off + n]
							.copy_from_slice(&converted[src_off..src_off + n]);
					}
				}
			}
			next_sample = Some(frame_start + chunk_samples);
			if offset + chunk_samples >= dest_frames {
				break;
			}
		}

		// Put the resampler back into the cache for the next call.
		self.audio.as_mut().expect("audio session").resampler =
			Some((sample_rate as u32, channel_layout, resampler));

		// Flush any samples still buffered in the resampler (rate conversion
		// tail), appending after the last decoded sample.
		let resampler = self
			.audio
			.as_mut()
			.expect("audio session")
			.resampler
			.as_mut()
			.map(|r| &mut r.2)
			.unwrap();
		if let Some(flush) = flush_resampler_interleaved_f32(resampler)? {
			if !flush.is_empty() {
				let chunk_samples = (flush.len() / dst_channels) as i64;
				let frame_start = next_sample.unwrap_or(start_sample);
				let offset = frame_start - start_sample;
				if offset < dest_frames && offset + chunk_samples > 0 {
					let copy_start = offset.max(0) as usize;
					let copy_end = (offset + chunk_samples).min(dest_frames).max(0) as usize;
					if copy_end > copy_start {
						let src_off = (copy_start as i64 - offset) as usize * dst_channels;
						let n = (copy_end - copy_start) * dst_channels;
						let dst_off = copy_start * dst_channels;
						if dst_off + n <= dest.len() {
							dest[dst_off..dst_off + n]
								.copy_from_slice(&flush[src_off..src_off + n]);
						}
					}
				}
			}
		}

		Ok(RetrieveAudioStatus::Success)
	}

	/// Conform the open stream's audio into per-channel PCM files, mirroring
	/// `FFmpegDecoder::conform_audio_internal`.
	fn conform_audio_to(
		&mut self,
		output_filenames: &[String],
		sample_rate: i32,
		channel_layout: u64,
		sample_format: i32,
		cancelled: Option<&OakCancelAtom>,
	) -> crate::error::Result<()> {
		if self.input_channel_layout_mask == 0 {
			return Err(fail(
				"could not determine the channel layout of the audio file",
			));
		}

		let target_fmt = crate::encodingparams::sample_format_from_i32(sample_format);
		let dst_sample = sample_format_to_ffmpeg(target_fmt)
			.ok_or_else(|| fail(format!("unsupported conform sample format {sample_format}")))?;
		let dst_layout = channel_layout_from_mask(channel_layout);
		let planar = dst_sample.is_planar();
		let planes = if planar {
			dst_layout.channels().max(0) as usize
		} else {
			1
		};
		if planes == 0 {
			return Err(fail("invalid conform channel layout"));
		}

		// Seek to the start (CPP-PARITY ffmpegdecoder.cpp:694).
		self.seek(0)?;

		let mut resampler = AudioResampler::get(
			self.input_sample_format,
			channel_layout_from_mask(self.input_channel_layout_mask),
			self.input_sample_rate,
			dst_sample,
			dst_layout,
			sample_rate as u32,
		)?;

		let filenames: Vec<PathBuf> = output_filenames.iter().map(PathBuf::from).collect();
		let mut wave_out = crate::planarfiledevice::PlanarFileDevice::new();
		if !wave_out.open(&filenames, crate::planarfiledevice::OpenMode::WriteOnly) {
			return Err(fail("failed to open conform output files"));
		}

		let bytes_per_sample = dst_sample.bytes();
		while let Some(frame) = self.next_frame()? {
			if cancel_atom_is_cancelled(cancelled) {
				break;
			}
			let DecodedFrame::Audio(audio) = frame else {
				continue;
			};
			let planes_buf = resampler.convert_to_planes(&audio)?;
			let written = planes_buf.first().map_or(0, Vec::len) / bytes_per_sample;
			if written > 0 {
				let refs: Vec<&[u8]> = planes_buf.iter().map(|p| p.as_slice()).collect();
				wave_out.write(&refs, written as i64 * bytes_per_sample as i64, 0);
			}
		}
		// Flush the resampler tail.
		for planes_buf in resampler.flush_planes()? {
			let written = planes_buf.first().map_or(0, Vec::len) / bytes_per_sample;
			if written > 0 {
				let refs: Vec<&[u8]> = planes_buf.iter().map(|p| p.as_slice()).collect();
				wave_out.write(&refs, written as i64 * bytes_per_sample as i64, 0);
			}
		}
		wave_out.close();

		if cancel_atom_is_cancelled(cancelled) {
			return Err(crate::error::Error::Cancelled);
		}
		Ok(())
	}
}

impl AudioResampler {
	/// Create a resampler converting from the source definition to the
	/// destination definition.
	fn get(
		src_format: Sample,
		src_layout: ChannelLayout,
		src_rate: u32,
		dst_format: Sample,
		dst_layout: ChannelLayout,
		dst_rate: u32,
	) -> crate::error::Result<AudioResampler> {
		let ctx = resampling::Context::get(
			src_format, src_layout, src_rate, dst_format, dst_layout, dst_rate,
		)
		.map_err(ffmpeg_err)?;
		Ok(AudioResampler {
			ctx,
			dst_format,
			dst_layout,
			dst_rate,
			dst_channels: dst_layout.channels().max(0) as usize,
		})
	}

	/// Convert one input frame, returning one byte buffer per output plane
	/// (plane 0 for packed destinations).
	fn convert_to_planes(
		&mut self,
		input: &ffmpeg::frame::Audio,
	) -> crate::error::Result<Vec<Vec<u8>>> {
		let (written, bufs) = self.swr_convert_buffers(input)?;
		let _ = written;
		Ok(bufs)
	}

	/// Convert one input frame into a freshly allocated output frame in the
	/// destination format, layout and rate.
	fn convert_to_frame(
		&mut self,
		input: &ffmpeg::frame::Audio,
	) -> crate::error::Result<ffmpeg::frame::Audio> {
		let (written, bufs) = self.swr_convert_buffers(input)?;
		let mut out = ffmpeg::frame::Audio::new(self.dst_format, written, self.dst_layout);
		out.set_rate(self.dst_rate);
		for (plane, buf) in bufs.iter().enumerate() {
			let len = buf.len().min(out.data_mut(plane).len());
			out.data_mut(plane)[..len].copy_from_slice(&buf[..len]);
		}
		Ok(out)
	}

	/// Flush any samples still buffered inside the resampler, as planes.
	fn flush_planes(&mut self) -> crate::error::Result<Vec<Vec<Vec<u8>>>> {
		let mut all: Vec<Vec<Vec<u8>>> = Vec::new();
		for _ in 0..32 {
			let out_samples = unsafe { sys::swr_get_out_samples(self.ctx.as_mut_ptr(), 0) };
			if out_samples <= 0 {
				break;
			}
			let (bufs, written) = self.swr_convert(out_samples as usize, None, 0)?;
			if written == 0 {
				break;
			}
			all.push(bufs);
		}
		Ok(all)
	}

	/// Run the swr conversion for `input`, returning the number of written
	/// samples and one byte buffer per output plane.
	fn swr_convert_buffers(
		&mut self,
		input: &ffmpeg::frame::Audio,
	) -> crate::error::Result<(usize, Vec<Vec<u8>>)> {
		let in_samples = input.samples() as i32;
		let out_samples = unsafe { sys::swr_get_out_samples(self.ctx.as_mut_ptr(), in_samples) };
		if out_samples < 0 {
			return Err(ffmpeg_err(FfmpegError::from(out_samples)));
		}
		if out_samples == 0 {
			return Ok((0, Vec::new()));
		}
		let in_ptrs: Vec<*const u8> = (0..input.planes())
			.map(|i| input.data(i).as_ptr())
			.collect();
		let (bufs, written) = self.swr_convert(out_samples as usize, Some(&in_ptrs), in_samples)?;
		Ok((written, bufs))
	}

	/// Allocate output buffers for `out_samples` and run `swr_convert`
	/// (with the given input pointers, or a drain when `None`).
	fn swr_convert(
		&mut self,
		out_samples: usize,
		in_ptrs: Option<&[*const u8]>,
		in_samples: i32,
	) -> crate::error::Result<(Vec<Vec<u8>>, usize)> {
		let planar = self.dst_format.is_planar();
		let planes = if planar { self.dst_channels.max(1) } else { 1 };
		let plane_len =
			out_samples * self.dst_format.bytes() * if planar { 1 } else { self.dst_channels };
		let mut bufs: Vec<Vec<u8>> = (0..planes).map(|_| vec![0u8; plane_len]).collect();
		let mut out_ptrs: Vec<*mut u8> = bufs.iter_mut().map(|b| b.as_mut_ptr()).collect();
		let written = unsafe {
			sys::swr_convert(
				self.ctx.as_mut_ptr(),
				out_ptrs.as_mut_ptr(),
				out_samples as i32,
				in_ptrs.map_or(std::ptr::null(), |p| p.as_ptr()),
				in_samples,
			)
		};
		if written < 0 {
			return Err(ffmpeg_err(FfmpegError::from(written)));
		}
		for buf in bufs.iter_mut() {
			let keep = written as usize
				* self.dst_format.bytes()
				* if planar { 1 } else { self.dst_channels };
			buf.truncate(keep);
		}
		Ok((bufs, written as usize))
	}
}

/// Convert a decoded audio frame to interleaved f32 at the resampler's
/// destination definition.
fn resample_to_interleaved_f32(
	resampler: &mut AudioResampler,
	input: &ffmpeg::frame::Audio,
) -> crate::error::Result<Vec<f32>> {
	let in_samples = input.samples() as i32;
	let out_samples = unsafe { sys::swr_get_out_samples(resampler.ctx.as_mut_ptr(), in_samples) };
	if out_samples < 0 {
		return Err(ffmpeg_err(FfmpegError::from(out_samples)));
	}
	if out_samples == 0 {
		return Ok(Vec::new());
	}
	let out_samples = out_samples as usize;
	let channels = resampler.dst_channels;
	let mut buf = vec![0f32; out_samples * channels];
	let out_ptrs = [buf.as_mut_ptr() as *mut u8];
	let in_ptrs: Vec<*const u8> = (0..input.planes())
		.map(|i| input.data(i).as_ptr())
		.collect();
	let written = unsafe {
		sys::swr_convert(
			resampler.ctx.as_mut_ptr(),
			out_ptrs.as_ptr(),
			out_samples as i32,
			in_ptrs.as_ptr(),
			in_samples,
		)
	};
	if written < 0 {
		return Err(ffmpeg_err(FfmpegError::from(written)));
	}
	buf.truncate(written as usize * channels);
	Ok(buf)
}

/// Flush any samples buffered in the resampler as interleaved f32.
fn flush_resampler_interleaved_f32(
	resampler: &mut AudioResampler,
) -> crate::error::Result<Option<Vec<f32>>> {
	let channels = resampler.dst_channels;
	let mut out = Vec::new();
	for _ in 0..32 {
		let out_samples = unsafe { sys::swr_get_out_samples(resampler.ctx.as_mut_ptr(), 0) };
		if out_samples <= 0 {
			break;
		}
		let out_samples = out_samples as usize;
		let mut buf = vec![0f32; out_samples * channels];
		let out_ptrs = [buf.as_mut_ptr() as *mut u8];
		let written = unsafe {
			sys::swr_convert(
				resampler.ctx.as_mut_ptr(),
				out_ptrs.as_ptr(),
				out_samples as i32,
				std::ptr::null(),
				0,
			)
		};
		if written < 0 {
			return Err(ffmpeg_err(FfmpegError::from(written)));
		}
		if written == 0 {
			break;
		}
		buf.truncate(written as usize * channels);
		out.extend_from_slice(&buf);
	}
	if out.is_empty() {
		Ok(None)
	} else {
		Ok(Some(out))
	}
}

/// Get the cached scaler for a (src → dst) pair, recreating it when the
/// parameters change.
fn get_or_create_scaler(
	cache: &mut Option<ScalingCache>,
	src_format: Pixel,
	src_width: u32,
	src_height: u32,
	dst_format: Pixel,
	dst_width: u32,
	dst_height: u32,
) -> crate::error::Result<&mut scaling::Context> {
	let recreate = match cache.as_ref() {
		Some(s) => {
			s.src_format != src_format
				|| s.src_width != src_width
				|| s.src_height != src_height
				|| s.dst_format != dst_format
				|| s.dst_width != dst_width
				|| s.dst_height != dst_height
		}
		None => true,
	};
	if recreate {
		let ctx = scaling::Context::get(
			src_format,
			src_width,
			src_height,
			dst_format,
			dst_width,
			dst_height,
			scaling::Flags::POINT,
		)
		.map_err(ffmpeg_err)?;
		*cache = Some(ScalingCache {
			ctx,
			src_format,
			src_width,
			src_height,
			dst_format,
			dst_width,
			dst_height,
		});
	}
	Ok(&mut cache.as_mut().expect("set above").ctx)
}

/// The frame's presentation timestamp (NOPTS when unset).
fn pts_of(f: Option<&ffmpeg::frame::Video>) -> Option<i64> {
	f.and_then(|f| f.pts())
}

/// Search the frame cache for the frame at (or closest to) `t`.
///
/// # CPP-PARITY
/// `FFmpegDecoder::get_frame_from_cache`.
fn get_frame_from_cache(video: &VideoDecodeState, t: i64) -> Option<ffmpeg::frame::Video> {
	let front = pts_of(video.cache.front()).unwrap_or(AV_NOPTS_VALUE);
	let back = pts_of(video.cache.back()).unwrap_or(AV_NOPTS_VALUE);
	if t < front {
		if video.cache_at_zero {
			return video.cache.front().cloned();
		}
	} else if t > back {
		if video.cache_at_eof {
			return video.cache.back().cloned();
		}
	} else {
		for (i, frame) in video.cache.iter().enumerate() {
			let this_pts = frame.pts().unwrap_or(AV_NOPTS_VALUE);
			let next_pts = video
				.cache
				.get(i + 1)
				.and_then(|f| f.pts())
				.unwrap_or(AV_NOPTS_VALUE);
			if this_pts == t || next_pts > t {
				return Some(frame.clone());
			}
		}
	}
	None
}

/// Decoder frame-cache size (C++ `maximum_queue_size`).
fn maximum_queue_size() -> usize {
	// # CPP-PARITY ffmpegdecoder.cpp:1184 — "Fairly arbitrary size... This
	// value may be tweaked over time."
	2
}

/// Copy a packed F32-RGBA byte buffer (as produced by swscale RGBAF32LE)
/// into an owned buffer, forcing alpha opaque (1.0) per pixel
/// (CPP-PARITY ffmpegdecoder.cpp:428).
fn convert_rgba_f32_le(data: &[u8], w: u32, h: u32, stride: usize) -> Vec<u8> {
	let mut out = vec![0u8; (w as usize) * (h as usize) * PIXEL_F32_BYTES];
	for y in 0..h as usize {
		let row = &data[y * stride..y * stride + (w as usize) * PIXEL_F32_BYTES];
		let dst =
			&mut out[y * (w as usize) * PIXEL_F32_BYTES..(y + 1) * (w as usize) * PIXEL_F32_BYTES];
		dst.copy_from_slice(row);
		for px in dst.chunks_exact_mut(PIXEL_F32_BYTES) {
			px[12..16].copy_from_slice(&1.0f32.to_le_bytes());
		}
	}
	out
}

/// Copy a packed u16-RGBA buffer (RGBA64LE) into F32 RGBA bytes.
fn convert_rgba64_to_f32(data: &[u8], w: u32, h: u32, stride: usize) -> Vec<u8> {
	let mut out = vec![0u8; (w as usize) * (h as usize) * PIXEL_F32_BYTES];
	for y in 0..h as usize {
		let row = &data[y * stride..y * stride + (w as usize) * 8];
		let dst =
			&mut out[y * (w as usize) * PIXEL_F32_BYTES..(y + 1) * (w as usize) * PIXEL_F32_BYTES];
		for (px, src_px) in dst
			.chunks_exact_mut(PIXEL_F32_BYTES)
			.zip(row.chunks_exact(8))
		{
			for c in 0..3 {
				let v = u16::from_le_bytes([src_px[c * 2], src_px[c * 2 + 1]]);
				px[c * 4..c * 4 + 4].copy_from_slice(&(v as f32 / 65535.0).to_le_bytes());
			}
			px[12..16].copy_from_slice(&1.0f32.to_le_bytes());
		}
	}
	out
}

/// Build an allocated [`Frame`] (F32, RGBA) from raw pixel bytes.
///
/// # CPP-PARITY
/// `copy_packed_av_frame_to_frame` (ffmpegdecoder.cpp:116): params are built
/// from the decoded dimensions, the buffer is allocated and each row is
/// copied with the destination linesize.
fn copy_rgba_f32_to_frame(
	width: u32,
	height: u32,
	bytes: &[u8],
	timestamp: Rational,
) -> crate::error::Result<Frame> {
	let params = unsafe { oakcommon_videoparams_init_basic(width as i32, height as i32) };
	unsafe {
		oakcommon_videoparams_set_format(params.clone(), PixelFormat::F32 as i32);
		oakcommon_videoparams_set_channel_count(params.clone(), VIDEO_CHANNELS);
	}
	let mut frame = Frame::with_params(params);
	frame.set_timestamp(timestamp);
	frame.allocate()?;

	let row_bytes = (width as usize) * PIXEL_F32_BYTES;
	let linesize = frame.linesize_bytes() as usize;
	let out = frame.data_mut().ok_or(crate::error::Error::State)?;
	if out.len() < linesize * (height as usize) {
		return Err(crate::error::Error::State);
	}
	for y in 0..(height as usize) {
		let src = &bytes[y * row_bytes..y * row_bytes + row_bytes];
		let dst = &mut out[y * linesize..y * linesize + row_bytes];
		dst.copy_from_slice(src);
	}
	Ok(frame)
}

// ---------------------------------------------------------------------------
// Probe
// ---------------------------------------------------------------------------

/// Probe `filename`, building a [`FootageDescription`].
///
/// # CPP-PARITY
/// `FFmpegDecoder::probe` (ffmpegdecoder.cpp:484). Differences: video
/// stream details are taken from stream parameters (no second decode pass),
/// so `is_still` is always false and interlacing always progressive;
/// subtitle streams are counted but not added.
fn probe_file(filename: &str, cancelled: Option<&OakCancelAtom>) -> Option<FootageDescription> {
	let mut dict = Dictionary::new();
	dict.set("analyzeduration", "5000000");
	dict.set("probesize", "20000000");
	let input = ffmpeg::format::input_with_dictionary(filename, dict).ok()?;

	let mut desc = FootageDescription::new("ffmpeg");
	let footage_duration = input.duration();

	let file_meta: Vec<(String, String)> = input
		.metadata()
		.iter()
		.map(|(k, v)| (k.to_string(), v.to_string()))
		.collect();
	let mut source_start_time =
		extract_source_start_time(&file_meta, FfRational(1, FB_TIME_BASE as i32), 0);

	let stream_count = input.nb_streams();
	for i in 0..stream_count {
		if cancel_atom_is_cancelled(cancelled) {
			return None;
		}
		let Some(stream) = input.stream(i as usize) else {
			continue;
		};
		let params = stream.parameters();
		let medium = params.medium();

		if !source_start_time.valid {
			let stream_meta: Vec<(String, String)> = stream
				.metadata()
				.iter()
				.map(|(k, v)| (k.to_string(), v.to_string()))
				.collect();
			let raw = unsafe { params.as_ptr() };
			source_start_time =
				extract_source_start_time(&stream_meta, stream.time_base(), unsafe {
					(*raw).sample_rate
				});
		}

		// Only proceed if a decoder exists for this stream
		// (CPP-PARITY ffmpegdecoder.cpp:530).
		if ffmpeg::decoder::find(params.id()).is_none() {
			continue;
		}

		let raw = unsafe { params.as_ptr() };
		match medium {
			MediaType::Video => {
				let pixel = pixel_from_raw(unsafe { (*raw).format });
				let native = native_pixel_format(pixel);
				let frame_rate = stream.avg_frame_rate();
				let tb = stream.time_base();

				let vp = unsafe { oakcommon_videoparams_init_basic(1, 1) };
				unsafe {
					oakcommon_videoparams_set_stream_index(vp.clone(), i as i32);
					oakcommon_videoparams_set_width(vp.clone(), (*raw).width);
					oakcommon_videoparams_set_height(vp.clone(), (*raw).height);
					oakcommon_videoparams_set_video_type(vp.clone(), OAKCOMMON_VIDEO_TYPE_VIDEO);
					oakcommon_videoparams_set_format(vp.clone(), native as i32);
					oakcommon_videoparams_set_channel_count(vp.clone(), VIDEO_CHANNELS);
					oakcommon_videoparams_set_interlacing(
						vp.clone(),
						OAKCOMMON_VIDEO_INTERLACE_NONE,
					);
					oakcommon_videoparams_set_pixel_aspect_ratio(vp.clone(), 1, 1);
					oakcommon_videoparams_set_frame_rate(
						vp.clone(),
						frame_rate.0 as i64,
						frame_rate.1 as i64,
					);
					oakcommon_videoparams_set_start_time(vp.clone(), stream.start_time());
					oakcommon_videoparams_set_time_base(vp.clone(), tb.0 as i64, tb.1 as i64);
					oakcommon_videoparams_set_duration(vp.clone(), stream.duration());
					oakcommon_videoparams_set_premultiplied_alpha(vp.clone(), 0);
				}
				desc.push_stream(StreamEntry::Video(vp));
			}
			MediaType::Audio => {
				let mut stream_duration = stream.duration();
				if stream_duration == AV_NOPTS_VALUE && footage_duration != AV_NOPTS_VALUE {
					// Fall back to the container duration rescaled into the
					// stream time base (CPP-PARITY ffmpegdecoder.cpp:621).
					stream_duration = unsafe {
						sys::av_rescale_q(
							footage_duration,
							FfRational(1, FB_TIME_BASE as i32).into(),
							stream.time_base().into(),
						)
					};
				}
				let sample_rate = unsafe { (*raw).sample_rate };
				let layout_mask = unsafe { ChannelLayout::from((*raw).ch_layout) }.bits();
				let ap = unsafe { oakcore_audioparams_create(sample_rate, layout_mask, 0) };
				if !ap.is_null() {
					let tb = stream.time_base();
					unsafe {
						oakcore_audioparams_set_stream_index(ap, i as i32);
						oakcore_audioparams_set_duration(ap, stream_duration);
						oakcore_audioparams_set_time_base(ap, tb.0 as i32, tb.1 as i32);
					}
					desc.push_stream(StreamEntry::Audio(OakAudioParams {
						ctx: ap as *mut std::ffi::c_void,
						addref: None,
						release: None,
						abi_version: crate::handle::OAKCODEC_ABI_VERSION,
					}));
				}
			}
			_ => {}
		}
	}

	desc.set_stream_count(stream_count as usize);
	if source_start_time.valid {
		desc.set_source_start_time(source_start_time.time, 0);
	}
	Some(desc)
}

/// Parsed source start time.
struct SourceTime {
	valid: bool,
	time: Rational,
}

/// Extract a source start time from `timecode` / `time_reference` metadata,
/// mirroring `extract_source_start_time` (ffmpegdecoder.cpp:176).
fn extract_source_start_time(
	metadata: &[(String, String)],
	timebase: FfRational,
	sample_rate: i32,
) -> SourceTime {
	let mut out = SourceTime {
		valid: false,
		time: Rational::new(0, 1),
	};
	for (key, value) in metadata {
		if key == "timecode" {
			let parsed = crate::timecodemetadata::SourceTime::from_timecode_string(
				value,
				&oak_rational(timebase),
			);
			if parsed.valid {
				out.valid = true;
				out.time = parsed.time;
				return out;
			}
		} else if key == "time_reference" {
			let parsed =
				crate::timecodemetadata::SourceTime::from_bwf_time_reference(value, sample_rate);
			if parsed.valid {
				out.valid = true;
				out.time = parsed.time;
				return out;
			}
		}
	}
	out
}

// ---------------------------------------------------------------------------
// FFmpegEncoder
// ---------------------------------------------------------------------------

/// `olive::FFmpegEncoder` — FFmpeg-backed media encoder.
pub struct FFmpegEncoder {
	/// The encoding parameters this encoder was configured with.
	pub params: EncodingParams,
	/// Encoder session state.
	state: Mutex<EncoderState>,
}

/// Encoder session state.
struct EncoderState {
	/// Parameters from [`Encoder::configure`] (overrides `params`).
	configured: Option<EncodingParams>,
	/// The open output, if any.
	output: Option<OutputState>,
	/// Last error detail.
	last_error: String,
}

/// One opened output file.
struct OutputState {
	output: ffmpeg::format::context::Output,
	video: Option<VideoEncoderState>,
	audio: Option<AudioEncoderState>,
	/// Audio presentation timestamp in 1/sample_rate time base units.
	audio_pts: i64,
	flushed: bool,
}

// SAFETY: `EncoderState` owns raw FFmpeg contexts (output + encoders) that
// are not thread-safe themselves, but every access goes through the
// `FFmpegEncoder::state` `Mutex`, so concurrent access never happens.
unsafe impl Send for EncoderState {}
unsafe impl Sync for EncoderState {}

/// Opened video encoder + its conversion scaler.
struct VideoEncoderState {
	encoder: ffmpeg::codec::encoder::video::Encoder,
	stream_index: usize,
	/// swscale from the incoming F32-RGBA bytes to the encoder pixel format.
	scaler: scaling::Context,
	width: u32,
	height: u32,
}

/// Opened audio encoder + its conversion resampler.
struct AudioEncoderState {
	encoder: ffmpeg::codec::encoder::audio::Encoder,
	stream_index: usize,
	/// swr from interleaved f32 to the encoder sample format.
	resampler: AudioResampler,
}

impl FFmpegEncoder {
	/// New encoder configured with `params`.
	pub(crate) fn with_params(params: EncodingParams) -> Self {
		FFmpegEncoder {
			params,
			state: Mutex::new(EncoderState {
				configured: None,
				output: None,
				last_error: String::new(),
			}),
		}
	}

	/// The effective encoding parameters (configured overrides construction).
	fn effective_params(&self) -> EncodingParams {
		let state = self.state.lock().unwrap_or_else(|e| e.into_inner());
		state
			.configured
			.clone()
			.unwrap_or_else(|| self.params.clone())
	}
}

impl Encoder for FFmpegEncoder {
	fn id(&self) -> String {
		"ffmpeg".to_string()
	}

	fn supports_video(&self) -> bool {
		true
	}

	fn supports_audio(&self) -> bool {
		true
	}

	fn supports_subtitles(&self) -> bool {
		true
	}

	fn supports_image_sequences(&self) -> bool {
		false
	}

	fn is_configurable(&self) -> bool {
		true
	}

	fn configure(&self, params: &EncodingParams) -> crate::error::Result<()> {
		if !is_ffmpeg_format(params.format) {
			return Err(fail(format!("unknown export format {}", params.format)));
		}
		// `configure` cannot write the public `params` field through `&self`;
		// the configured copy is stored in the session state and used by
		// `open` (CPP-PARITY encoder.cpp `configure` stores the params).
		let mut state = self.state.lock().unwrap_or_else(|e| e.into_inner());
		state.configured = Some(params.clone());
		state.last_error.clear();
		Ok(())
	}

	fn open(&self) -> crate::error::Result<()> {
		let params = self.effective_params();
		let mut state = self.state.lock().unwrap_or_else(|e| e.into_inner());
		state.open(&params)
	}

	fn close(&self) -> crate::error::Result<()> {
		// `Encoder::close` is documented idempotent; flush if needed.
		let mut state = self.state.lock().unwrap_or_else(|e| e.into_inner());
		state.close()
	}

	fn write_video(&self, frame: &Frame) -> crate::error::Result<()> {
		let params = self.effective_params();
		let mut state = self.state.lock().unwrap_or_else(|e| e.into_inner());
		state.write_video(frame, &params)
	}

	fn write_audio(&self, samples: &[f32], frame_count: i32) -> crate::error::Result<()> {
		let params = self.effective_params();
		let mut state = self.state.lock().unwrap_or_else(|e| e.into_inner());
		state.write_audio(samples, frame_count, &params)
	}

	fn write_subtitle(
		&self,
		_text: &str,
		_in_seconds: f64,
		_out_seconds: f64,
	) -> crate::error::Result<()> {
		// Subtitle muxing is not exposed by the crate's encoder trait flow
		// (the C++ writes through the bridge's SRT encoder); report the same
		// unsupported state the stub did.
		Err(fail(
			"subtitle encoding is not supported by the ffmpeg encoder",
		))
	}

	fn flush(&self) -> crate::error::Result<()> {
		let mut state = self.state.lock().unwrap_or_else(|e| e.into_inner());
		state.close()
	}

	fn desired_pixel_format(&self) -> Option<PixelFormat> {
		// The encoder accepts (and converts) F32 RGBA frames.
		Some(PixelFormat::F32)
	}

	fn desired_sample_format(&self) -> Option<SampleFormat> {
		// The encoder accepts interleaved f32 audio.
		Some(SampleFormat::F32)
	}

	fn filename(&self) -> String {
		c_string_1024(&self.params.filename)
	}

	fn get_error(&self) -> String {
		self.state
			.lock()
			.unwrap_or_else(|e| e.into_inner())
			.last_error
			.clone()
	}
}

impl EncoderState {
	/// Open the output file, create the streams and encoders and write the
	/// header.
	fn open(&mut self, params: &EncodingParams) -> crate::error::Result<()> {
		if self.output.is_some() {
			return Ok(());
		}
		let filename = c_string_1024(&params.filename);
		if filename.is_empty() {
			return Err(fail("no output filename"));
		}
		if params.video_enabled == 0 && params.audio_enabled == 0 {
			return Err(fail("no output tracks enabled"));
		}

		let mut output = ffmpeg::format::output(&filename)
			.map_err(|e| fail(format!("failed to create output '{filename}': {e}")))?;

		let mut video = None;
		if params.video_enabled != 0 {
			let codec_id = export_codec_to_id(params.video_codec)
				.ok_or_else(|| fail(format!("unknown video codec {}", params.video_codec)))?;
			let codec = ffmpeg::encoder::find(codec_id)
				.ok_or_else(|| fail(format!("no encoder for codec {:?}", codec_id)))?;

			let width = params.video_width.max(1) as u32;
			let height = params.video_height.max(1) as u32;
			let time_base = FfRational(params.video_time_base_num, params.video_time_base_den);
			let frame_rate = FfRational(
				params.video_time_base_den,
				params.video_time_base_num.max(1),
			);

			let mut stream = output.add_stream(codec).map_err(ffmpeg_err)?;
			let stream_index = stream.index();

			let mut encoder = ffmpeg::codec::Context::new_with_codec(codec)
				.encoder()
				.video()
				.map_err(ffmpeg_err)?;
			stream.set_parameters(&encoder);
			stream.set_time_base(time_base);

			encoder.set_width(width);
			encoder.set_height(height);
			encoder.set_aspect_ratio(FfRational(
				params.video_pixel_aspect_num.max(1),
				params.video_pixel_aspect_den.max(1),
			));
			encoder.set_frame_rate(Some(frame_rate));
			encoder.set_time_base(time_base);
			if params.video_bit_rate > 0 {
				encoder.set_bit_rate(params.video_bit_rate as usize);
			}
			if params.video_max_bit_rate > 0 {
				encoder.set_max_bit_rate(params.video_max_bit_rate as usize);
			}

			// Choose the target pixel format: explicit `video_pix_fmt`, else
			// a codec-appropriate default.
			let pix_fmt = pixel_format_from_name(&params.video_pix_fmt)
				.unwrap_or_else(|| default_pixel_format_for_codec(codec_id));
			encoder.set_format(pix_fmt);

			let opened = encoder.open().map_err(ffmpeg_err)?;
			stream.set_parameters(&opened);

			let scaler = scaling::Context::get(
				Pixel::RGBA,
				width,
				height,
				pix_fmt,
				width,
				height,
				scaling::Flags::BILINEAR,
			)
			.map_err(ffmpeg_err)?;

			video = Some(VideoEncoderState {
				encoder: opened,
				stream_index,
				scaler,
				width,
				height,
			});
		}

		let mut audio = None;
		if params.audio_enabled != 0 {
			let codec_id = export_codec_to_id(params.audio_codec)
				.ok_or_else(|| fail(format!("unknown audio codec {}", params.audio_codec)))?;
			let codec = ffmpeg::encoder::find(codec_id)
				.ok_or_else(|| fail(format!("no encoder for codec {:?}", codec_id)))?;

			let sample_rate = params.audio_sample_rate.max(1) as u32;
			let layout = channel_layout_from_mask(params.audio_channel_layout);

			let mut stream = output.add_stream(codec).map_err(ffmpeg_err)?;
			let stream_index = stream.index();
			stream.set_time_base(FfRational(1, sample_rate as i32));

			let mut encoder = ffmpeg::codec::Context::new_with_codec(codec)
				.encoder()
				.audio()
				.map_err(ffmpeg_err)?;
			stream.set_parameters(&encoder);

			encoder.set_rate(sample_rate as i32);
			encoder.set_channel_layout(layout);
			if params.audio_bit_rate > 0 {
				encoder.set_bit_rate(params.audio_bit_rate as usize);
			}
			let sample_fmt = sample_format_to_ffmpeg(params.audio_sample_format)
				.unwrap_or_else(|| default_sample_format_for_codec(codec_id));
			encoder.set_format(sample_fmt);

			let opened = encoder.open().map_err(ffmpeg_err)?;
			stream.set_parameters(&opened);

			let resampler = AudioResampler::get(
				Sample::F32(SampleType::Packed),
				layout,
				sample_rate,
				sample_fmt,
				layout,
				sample_rate,
			)?;

			audio = Some(AudioEncoderState {
				encoder: opened,
				stream_index,
				resampler,
			});
		}

		output.write_header().map_err(ffmpeg_err)?;

		self.output = Some(OutputState {
			output,
			video,
			audio,
			audio_pts: 0,
			flushed: false,
		});
		Ok(())
	}

	/// Encode one frame (F32 RGBA) into the open output.
	fn write_video(&mut self, frame: &Frame, params: &EncodingParams) -> crate::error::Result<()> {
		let output = self
			.output
			.as_mut()
			.ok_or_else(|| fail("encoder is not open"))?;
		if output.flushed {
			return Err(fail("encoder is already flushed"));
		}
		let video = output
			.video
			.as_mut()
			.ok_or_else(|| fail("encoder has no video track"))?;

		let data = frame.data().ok_or(crate::error::Error::State)?;
		let (w, h) = (frame.width() as u32, frame.height() as u32);
		if w != video.width || h != video.height {
			return Err(fail(format!(
				"frame size {w}x{h} does not match the encoder size {}x{}",
				video.width, video.height
			)));
		}

		// Convert the F32 RGBA buffer to 8-bit RGBA (swscale cannot take
		// float input on every build) and scale into the encoder format.
		let mut rgba = vec![0u8; (w as usize) * (h as usize) * 4];
		let linesize = frame.linesize_bytes() as usize;
		for y in 0..(h as usize) {
			let row = &data[y * linesize..y * linesize + (w as usize) * PIXEL_F32_BYTES];
			let dst = &mut rgba[y * (w as usize) * 4..(y + 1) * (w as usize) * 4];
			for (out_px, in_px) in dst
				.chunks_exact_mut(4)
				.zip(row.chunks_exact(PIXEL_F32_BYTES))
			{
				for c in 0..4 {
					let v = f32::from_le_bytes([
						in_px[c * 4],
						in_px[c * 4 + 1],
						in_px[c * 4 + 2],
						in_px[c * 4 + 3],
					]);
					out_px[c] = (v * 255.0).clamp(0.0, 1.0) as u8;
				}
			}
		}

		let mut src = ffmpeg::frame::Video::new(Pixel::RGBA, w, h);
		src.data_mut(0).copy_from_slice(&rgba);

		let mut scaled = ffmpeg::frame::Video::empty();
		video.scaler.run(&src, &mut scaled).map_err(ffmpeg_err)?;

		// # CPP-PARITY
		// `FFmpegEncoder::write_frame` passes the frame time in seconds; the
		// Rust `Frame` carries the timestamp as a rational.
		let secs = frame.timestamp().to_f64();
		let tb = FfRational(
			params.video_time_base_num.max(1),
			params.video_time_base_den.max(1),
		);
		let pts = (secs * tb.1 as f64 / tb.0 as f64).round() as i64;
		scaled.set_pts(Some(pts));

		video.encoder.send_frame(&scaled).map_err(ffmpeg_err)?;
		drain_video_packets(&mut output.output, video)
	}

	/// Encode interleaved f32 audio into the open output.
	fn write_audio(
		&mut self,
		samples: &[f32],
		_frame_count: i32,
		params: &EncodingParams,
	) -> crate::error::Result<()> {
		let output = self
			.output
			.as_mut()
			.ok_or_else(|| fail("encoder is not open"))?;
		if output.flushed {
			return Err(fail("encoder is already flushed"));
		}
		let audio = output
			.audio
			.as_mut()
			.ok_or_else(|| fail("encoder has no audio track"))?;

		if samples.is_empty() {
			return Ok(());
		}
		let channels = audio.resampler.dst_channels.max(1);
		let in_frames = samples.len() / channels;

		// Presentation timestamp in the output stream time base (1/sample_rate).
		let pts = output.audio_pts;

		let layout = channel_layout_from_mask(params.audio_channel_layout);
		let mut input =
			ffmpeg::frame::Audio::new(Sample::F32(SampleType::Packed), in_frames, layout);
		let bytes =
			unsafe { std::slice::from_raw_parts(samples.as_ptr() as *const u8, samples.len() * 4) };
		input.data_mut(0)[..bytes.len()].copy_from_slice(bytes);

		let mut converted = audio.resampler.convert_to_frame(&input)?;
		converted.set_pts(Some(pts));
		output.audio_pts += converted.samples() as i64;

		audio.encoder.send_frame(&converted).map_err(ffmpeg_err)?;
		drain_audio_packets(&mut output.output, audio)
	}

	/// Flush encoders, write the trailer and close the output (idempotent).
	fn close(&mut self) -> crate::error::Result<()> {
		let Some(output) = self.output.as_mut() else {
			return Ok(());
		};
		if !output.flushed {
			if let Some(v) = output.video.as_mut() {
				drain_video_encoder(&mut output.output, v)?;
			}
			if let Some(a) = output.audio.as_mut() {
				drain_audio_encoder(&mut output.output, a)?;
			}
			output.output.write_trailer().map_err(ffmpeg_err)?;
			output.flushed = true;
		}
		// Drop the output (closes the file).
		self.output = None;
		Ok(())
	}
}

/// Drain a video encoder after EOF, writing any packets.
fn drain_video_encoder(
	output: &mut ffmpeg::format::context::Output,
	video: &mut VideoEncoderState,
) -> crate::error::Result<()> {
	video.encoder.send_eof().map_err(ffmpeg_err)?;
	let mut pkt = ffmpeg::packet::Packet::empty();
	loop {
		match video.encoder.receive_packet(&mut pkt) {
			Ok(()) => {
				pkt.set_stream(video.stream_index);
				pkt.write_interleaved(output).map_err(ffmpeg_err)?;
			}
			Err(e) if is_eof_or_eagain(&e) => break,
			Err(e) => return Err(ffmpeg_err(e)),
		}
	}
	Ok(())
}

/// Drain an audio encoder after EOF, writing any packets.
fn drain_audio_encoder(
	output: &mut ffmpeg::format::context::Output,
	audio: &mut AudioEncoderState,
) -> crate::error::Result<()> {
	audio.encoder.send_eof().map_err(ffmpeg_err)?;
	let mut pkt = ffmpeg::packet::Packet::empty();
	loop {
		match audio.encoder.receive_packet(&mut pkt) {
			Ok(()) => {
				pkt.set_stream(audio.stream_index);
				pkt.write_interleaved(output).map_err(ffmpeg_err)?;
			}
			Err(e) if is_eof_or_eagain(&e) => break,
			Err(e) => return Err(ffmpeg_err(e)),
		}
	}
	Ok(())
}

/// Drain ready packets from a video encoder into the muxer.
fn drain_video_packets(
	output: &mut ffmpeg::format::context::Output,
	video: &mut VideoEncoderState,
) -> crate::error::Result<()> {
	let mut pkt = ffmpeg::packet::Packet::empty();
	loop {
		match video.encoder.receive_packet(&mut pkt) {
			Ok(()) => {
				pkt.set_stream(video.stream_index);
				pkt.write_interleaved(output).map_err(ffmpeg_err)?;
			}
			Err(e) if is_eof_or_eagain(&e) => break,
			Err(e) => return Err(ffmpeg_err(e)),
		}
	}
	Ok(())
}

/// Drain ready packets from an audio encoder into the muxer.
fn drain_audio_packets(
	output: &mut ffmpeg::format::context::Output,
	audio: &mut AudioEncoderState,
) -> crate::error::Result<()> {
	let mut pkt = ffmpeg::packet::Packet::empty();
	loop {
		match audio.encoder.receive_packet(&mut pkt) {
			Ok(()) => {
				pkt.set_stream(audio.stream_index);
				pkt.write_interleaved(output).map_err(ffmpeg_err)?;
			}
			Err(e) if is_eof_or_eagain(&e) => break,
			Err(e) => return Err(ffmpeg_err(e)),
		}
	}
	Ok(())
}

/// Map an `ExportCodec::Codec` raw value to an FFmpeg codec id
/// (CPP-PARITY `FFmpegEncoder::export_codec_to_bridge`). Values are the
/// documented `ExportCodec::Codec` discriminants (see `exportcodec.rs`).
fn export_codec_to_id(codec: i32) -> Option<ffmpeg::codec::Id> {
	match codec {
		0 => Some(ffmpeg::codec::Id::DNXHD),       // DNxHD
		1 | 2 => Some(ffmpeg::codec::Id::H264),    // H264 / H264 RGB
		3 => Some(ffmpeg::codec::Id::HEVC),        // H265
		6 => Some(ffmpeg::codec::Id::PRORES),      // ProRes
		7 => Some(ffmpeg::codec::Id::CFHD),        // CineForm
		10 => Some(ffmpeg::codec::Id::MPEG2VIDEO), // MP2
		11 => Some(ffmpeg::codec::Id::MP3),        // MP3
		12 => Some(ffmpeg::codec::Id::AAC),        // AAC
		13 => Some(ffmpeg::codec::Id::PCM_S16LE),  // PCM
		14 => Some(ffmpeg::codec::Id::OPUS),       // Opus
		15 => Some(ffmpeg::codec::Id::VORBIS),     // Vorbis
		16 => Some(ffmpeg::codec::Id::FLAC),       // FLAC
		17 => Some(ffmpeg::codec::Id::SUBRIP),     // SRT
		18 => Some(ffmpeg::codec::Id::AV1),        // AV1
		_ => None,
	}
}

/// Whether `format` is a container the FFmpeg encoder handles
/// (CPP-PARITY `Encoder::create_from_params` format mapping).
fn is_ffmpeg_format(format: i32) -> bool {
	matches!(format, 0 | 1 | 2 | 4 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14)
}

/// Default encoder pixel format for a codec.
fn default_pixel_format_for_codec(codec: ffmpeg::codec::Id) -> Pixel {
	match codec {
		ffmpeg::codec::Id::PRORES | ffmpeg::codec::Id::DNXHD => Pixel::YUV422P10LE,
		_ => Pixel::YUV420P,
	}
}

/// Default encoder sample format for a codec.
fn default_sample_format_for_codec(codec: ffmpeg::codec::Id) -> Sample {
	match codec {
		ffmpeg::codec::Id::PCM_S16LE => Sample::I16(SampleType::Packed),
		ffmpeg::codec::Id::FLAC => Sample::I16(SampleType::Planar),
		ffmpeg::codec::Id::MP3 => Sample::F32(SampleType::Packed),
		_ => Sample::F32(SampleType::Planar),
	}
}

/// Parse a NUL-terminated pixel-format name (e.g. "yuv420p").
fn pixel_format_from_name(buf: &[u8; 64]) -> Option<Pixel> {
	let end = buf.iter().position(|&b| b == 0).unwrap_or(buf.len());
	let name = std::str::from_utf8(&buf[..end]).ok()?;
	if name.is_empty() {
		return None;
	}
	name.parse::<Pixel>().ok()
}

/// Map an oakcore sample format to an FFmpeg sample format.
fn sample_format_to_ffmpeg(f: SampleFormat) -> Option<Sample> {
	match f {
		SampleFormat::U8Planar => Some(Sample::U8(SampleType::Planar)),
		SampleFormat::S16Planar => Some(Sample::I16(SampleType::Planar)),
		SampleFormat::S32Planar => Some(Sample::I32(SampleType::Planar)),
		SampleFormat::S64Planar => Some(Sample::I64(SampleType::Planar)),
		SampleFormat::F32Planar => Some(Sample::F32(SampleType::Planar)),
		SampleFormat::F64Planar => Some(Sample::F64(SampleType::Planar)),
		SampleFormat::U8 => Some(Sample::U8(SampleType::Packed)),
		SampleFormat::S16 => Some(Sample::I16(SampleType::Packed)),
		SampleFormat::S32 => Some(Sample::I32(SampleType::Packed)),
		SampleFormat::S64 => Some(Sample::I64(SampleType::Packed)),
		SampleFormat::F32 => Some(Sample::F32(SampleType::Packed)),
		SampleFormat::F64 => Some(Sample::F64(SampleType::Packed)),
		SampleFormat::Invalid => None,
	}
}

/// Read a NUL-terminated C string out of the `EncodingParams.filename` byte
/// buffer, stopping at the first NUL (empty string when unset).
fn c_string_1024(buf: &[u8; 1024]) -> String {
	let end = buf.iter().position(|&b| b == 0).unwrap_or(buf.len());
	String::from_utf8_lossy(&buf[..end]).into_owned()
}

#[cfg(test)]
mod tests {
	use super::*;
	use crate::decoder::RetrieveVideoParams;
	use oakcore_rs::Rational;

	fn video_params() -> RetrieveVideoParams {
		RetrieveVideoParams {
			stream: CodecStream::new(),
			time: Rational::new(1, 30),
			length: TimeRange::default(),
			force_range: crate::decoder::K_COLOR_RANGE_DEFAULT,
			is_image_sequence: false,
			image_sequence_digits: 0,
			image_sequence_number: 0,
			mode: crate::decoder::RenderMode::Offline,
			alpha_is_premultiplied: false,
		}
	}

	#[test]
	fn ffmpeg_decoder_identity_and_capabilities() {
		let d = FFmpegDecoder::new();
		assert_eq!(d.id(), "ffmpeg");
		assert!(d.supports_video());
		assert!(d.supports_audio());
		assert!(d.probe("any.mp4", None).is_none());
	}

	#[test]
	fn ffmpeg_decoder_closed_state_errors() {
		let d = FFmpegDecoder::new();
		// Open on a missing file fails and leaves the decoder closed
		// (C++ parity: the stream is unset on failure).
		let s = CodecStream::with_block("in.mp4".to_string(), 0, None);
		assert!(d.open(&s).is_err());
		assert_eq!(d.stream().filename(), "");
		assert!(d.close().is_ok());

		// All media operations on a closed decoder fail.
		assert!(d.retrieve_video_frame(&video_params()).is_err());
		assert!(d.retrieve_video(&video_params()).is_err());
		let mut dest = [0f32; 8];
		assert!(d
			.retrieve_audio(
				&mut dest,
				&TimeRange::new(Rational::new(0, 1), Rational::new(1, 1)),
				48000,
				0x3
			)
			.is_err());
		assert!(d
			.conform_audio(&["a.pcm".to_string()], 48000, 0x3, 10, None)
			.is_err());
		assert_eq!(d.get_audio_start_offset(), Rational::new(0, 1));
	}

	#[test]
	fn ffmpeg_encoder_identity_and_config_validation() {
		let mut params = EncodingParams::default();
		let name = b"out/ffmpeg.mp4";
		params.filename[..name.len()].copy_from_slice(name);
		params.format = 2; // mp4
		params.video_enabled = 1;
		params.video_codec = 1; // H264
		params.video_width = 64;
		params.video_height = 64;
		params.video_time_base_num = 1;
		params.video_time_base_den = 10;
		let e = FFmpegEncoder::with_params(params);

		assert_eq!(e.id(), "ffmpeg");
		assert!(e.supports_video());
		assert!(e.supports_audio());
		assert!(e.supports_subtitles());
		assert!(!e.supports_image_sequences());
		assert!(e.is_configurable());
		assert_eq!(e.filename(), "out/ffmpeg.mp4");
		assert_eq!(e.desired_pixel_format(), Some(PixelFormat::F32));
		assert_eq!(e.desired_sample_format(), Some(SampleFormat::F32));
		assert_eq!(e.get_error(), "");

		// configure rejects unknown export formats.
		let mut bad = EncodingParams::default();
		bad.format = 99;
		assert!(e.configure(&bad).is_err());

		// A valid configures succeeds but nothing is open yet.
		let mut good = EncodingParams::default();
		good.format = 2;
		good.video_enabled = 1;
		good.video_codec = 1;
		good.video_width = 64;
		good.video_height = 64;
		good.video_time_base_num = 1;
		good.video_time_base_den = 10;
		assert!(e.configure(&good).is_ok());

		// Operations before open fail cleanly; close/flush are safe.
		let frame = Frame::new();
		assert!(e.write_video(&frame).is_err());
		assert!(e.write_audio(&[0f32; 4], 1).is_err());
		assert!(e.write_subtitle("hi", 0.0, 1.0).is_err());
		assert!(e.close().is_ok());
		assert!(e.flush().is_ok());
	}

	#[test]
	fn ffmpeg_encoder_open_rejects_invalid_config() {
		// No filename.
		let e = FFmpegEncoder::with_params(EncodingParams::default());
		assert!(e.open().is_err());

		// No enabled tracks.
		let mut p = EncodingParams::default();
		let name = b"out/x.mp4";
		p.filename[..name.len()].copy_from_slice(name);
		p.format = 2;
		let e = FFmpegEncoder::with_params(p);
		assert!(e.open().is_err());
	}
}
