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

//! `include/codec/decoder.h` exports.
//!
//! Complete inventory: probe / probe_last_error / probe_decoder_name /
//! probe_video_stream_count / probe_audio_stream_count /
//! probe_subtitle_stream_count / probe_get_video_stream /
//! probe_get_audio_stream / init / free / open / close / is_open /
//! decode_video / decode_audio / conform_audio /
//! get_image_sequence_digit_count / get_image_sequence_index /
//! transform_image_sequence_file_name / last_error.
//!
//! Two handle box types share the `OakDecoder` handle, mirroring the C++
//! `ProbeBox` / `DecoderBox` split in `c_api/decoder.cpp`: probe exports
//! read a plain [`ProbeBox`], session exports read a `Mutex<DecoderBox>`.

#[cfg(test)]
use std::ffi::c_void;
use std::ffi::{c_char, c_int};
use std::path::Path;
use std::sync::{Arc, Mutex};

use oakcore_rs::{Rational, TimeRange};

use crate::bridge::common::{
	oakcommon_videoparams_get_channel_count, oakcommon_videoparams_get_color_primaries,
	oakcommon_videoparams_get_color_transfer, oakcommon_videoparams_get_duration,
	oakcommon_videoparams_get_format, oakcommon_videoparams_get_frame_rate,
	oakcommon_videoparams_get_height, oakcommon_videoparams_get_interlacing,
	oakcommon_videoparams_get_stream_index, oakcommon_videoparams_get_time_base,
	oakcommon_videoparams_get_width, oakcore_audioparams_channel_count,
	oakcore_audioparams_channel_layout, oakcore_audioparams_duration,
	oakcore_audioparams_sample_rate, oakcore_audioparams_stream_index,
	oakcore_audioparams_time_base, oakcore_rational_denominator, oakcore_rational_free,
	oakcore_rational_numerator, OakAudioParams, OakVideoParams,
};
use crate::bridge::render::{oakrender_cancelatom_heard_cancel, OakCancelAtom};
use crate::decoder::{
	CodecStream, Decoder, OakCodecAudioStreamInfo, OakCodecVideoStreamInfo, RenderMode,
	RetrieveAudioStatus, RetrieveVideoParams, K_COLOR_RANGE_DEFAULT,
};
use crate::footagedescription::FootageDescription;
#[cfg(test)]
use crate::frame::Frame;
use crate::handle::{self, CHandle};

/// Box behind a probe handle: the recognized decoder name plus the
/// probed stream inventory (`ProbeBox` in `c_api/decoder.cpp`).
struct ProbeBox {
	decoder_name: String,
	desc: FootageDescription,
}

/// Box behind a decode-session handle (`DecoderBox` in `c_api/decoder.cpp`).
struct DecoderBox {
	decoder: Option<Arc<dyn Decoder>>,
	last_error: String,
	open_filename: String,
	open_stream: i32,
	open: bool,
}

/// Last probe failure reason. The C++ uses a `thread_local`; the crate
/// serializes through a global mutex instead (tests are single-threaded
/// per binary, so behavior is equivalent).
static PROBE_ERROR: Mutex<String> = Mutex::new(String::new());

/// `OAKCOMMON_VIDEO_INTERLACE_NONE` (oakcommon `common/videoparams.h`).
const OAKCOMMON_VIDEO_INTERLACE_NONE: c_int = 0;

/// Probe with every available decoder, returning the first valid
/// description (mirrors `probe_with_any_decoder` in `c_api/decoder.cpp`).
///
/// `is_valid` follows the C++ `FootageDescription::is_valid`: a non-empty
/// decoder name and at least one stream.
fn probe_with_any_decoder(filename: &str) -> Option<(String, FootageDescription)> {
	for d in crate::decoder::receive_list_of_all_decoders() {
		let desc = match d.probe(filename, None) {
			Some(desc) => desc,
			None => continue,
		};
		if !desc.decoder().is_empty()
			&& (desc.video_stream_count() > 0
				|| desc.audio_stream_count() > 0
				|| desc.subtitle_stream_count() > 0)
		{
			return Some((desc.decoder().to_string(), desc));
		}
	}
	None
}

/// Fill an `oakcodec_video_stream_info` from a probed video params handle
/// (mirrors `fill_video_info` in `c_api/decoder.cpp`).
fn fill_video_info(vp: &OakVideoParams, out: &mut OakCodecVideoStreamInfo) {
	*out = OakCodecVideoStreamInfo {
		stream_index: 0,
		width: 0,
		height: 0,
		frame_rate_num: 0,
		frame_rate_den: 0,
		duration_ts: 0,
		time_base_num: 0,
		time_base_den: 0,
		format: 0,
		channel_count: 0,
		color_primaries: 0,
		color_trc: 0,
		interlaced: 0,
	};
	out.stream_index = unsafe { oakcommon_videoparams_get_stream_index(vp.clone()) };
	out.width = unsafe { oakcommon_videoparams_get_width(vp.clone()) };
	out.height = unsafe { oakcommon_videoparams_get_height(vp.clone()) };

	let mut fr_num: c_int = 0;
	let mut fr_den: c_int = 0;
	unsafe { oakcommon_videoparams_get_frame_rate(vp.clone(), &mut fr_num, &mut fr_den) };
	out.frame_rate_num = fr_num;
	out.frame_rate_den = fr_den;

	let mut tb_num: i64 = 0;
	let mut tb_den: i64 = 0;
	unsafe { oakcommon_videoparams_get_time_base(vp.clone(), &mut tb_num, &mut tb_den) };
	out.time_base_num = tb_num as c_int;
	out.time_base_den = tb_den as c_int;

	out.duration_ts = unsafe { oakcommon_videoparams_get_duration(vp.clone()) };
	out.format = unsafe { oakcommon_videoparams_get_format(vp.clone()) };
	out.channel_count = unsafe { oakcommon_videoparams_get_channel_count(vp.clone()) };
	out.color_primaries = unsafe { oakcommon_videoparams_get_color_primaries(vp.clone()) };
	out.color_trc = unsafe { oakcommon_videoparams_get_color_transfer(vp.clone()) };

	let interlacing = unsafe { oakcommon_videoparams_get_interlacing(vp.clone()) };
	out.interlaced = (interlacing != OAKCOMMON_VIDEO_INTERLACE_NONE) as c_int;
}

/// Fill an `oakcodec_audio_stream_info` from a probed audio params handle
/// (mirrors `fill_audio_info` in `c_api/decoder.cpp`).
fn fill_audio_info(ap: &OakAudioParams, out: &mut OakCodecAudioStreamInfo) {
	*out = OakCodecAudioStreamInfo {
		stream_index: 0,
		sample_rate: 0,
		channel_layout: 0,
		channel_count: 0,
		duration_ts: 0,
		time_base_num: 0,
		time_base_den: 0,
	};
	let ctx = ap.ctx as *const OakAudioParams;
	out.stream_index = unsafe { oakcore_audioparams_stream_index(ctx) };
	out.sample_rate = unsafe { oakcore_audioparams_sample_rate(ctx) };
	out.channel_layout = unsafe { oakcore_audioparams_channel_layout(ctx) };
	out.channel_count = unsafe { oakcore_audioparams_channel_count(ctx) };
	out.duration_ts = unsafe { oakcore_audioparams_duration(ctx) };

	// `time_base` returns a newly allocated rational the caller owns.
	let tb = unsafe { oakcore_audioparams_time_base(ctx) };
	if !tb.is_null() {
		out.time_base_num = unsafe { oakcore_rational_numerator(tb) };
		out.time_base_den = unsafe { oakcore_rational_denominator(tb) };
		unsafe { oakcore_rational_free(tb) };
	}
}

/// Whether a media file exists on disk (mirrors the C++ `file_exists`).
fn file_exists(filename: &str) -> bool {
	Path::new(filename).exists()
}

/* ---- Probe ---------------------------------------------------------------- */

/// `oakcodec_decoder_probe`: probe a media file with every available
/// decoder; empty handle plus `oakcodec_probe_last_error` on failure.
#[no_mangle]
pub unsafe extern "C" fn oakcodec_decoder_probe(filename: *const c_char) -> CHandle {
	handle::guard_handle(|| {
		let filename = match crate::ffi::c_str(filename) {
			Some(f) if !f.is_empty() => f,
			_ => {
				*PROBE_ERROR.lock().unwrap() = "no filename given".to_string();
				return Ok(CHandle::null());
			}
		};
		if !file_exists(&filename) {
			*PROBE_ERROR.lock().unwrap() = format!("file not found: {}", filename);
			return Ok(CHandle::null());
		}
		match probe_with_any_decoder(&filename) {
			Some((decoder_name, desc)) => {
				PROBE_ERROR.lock().unwrap().clear();
				Ok(handle::make_owned(ProbeBox { decoder_name, desc }))
			}
			None => {
				*PROBE_ERROR.lock().unwrap() =
					format!("no decoder recognizes this file: {}", filename);
				Ok(CHandle::null())
			}
		}
	})
}

/// `oakcodec_probe_last_error`: last probe failure reason (two-stage).
#[no_mangle]
pub unsafe extern "C" fn oakcodec_probe_last_error(buf: *mut c_char, buf_size: c_int) -> c_int {
	handle::guard_raw(|| {
		let s = PROBE_ERROR.lock().unwrap().clone();
		super::string_out(&s, buf, buf_size)
	})
}

/// `oakcodec_decoder_probe_decoder_name`: recognized decoder id
/// (two-stage).
#[no_mangle]
pub unsafe extern "C" fn oakcodec_decoder_probe_decoder_name(
	probe: CHandle,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	handle::guard_raw(|| match super::get_box::<ProbeBox>(&probe) {
		Some(b) => super::string_out(&b.decoder_name, buf, buf_size),
		None => crate::error::OAKCODEC_E_INVALID,
	})
}

/// `oakcodec_decoder_probe_video_stream_count`.
#[no_mangle]
pub unsafe extern "C" fn oakcodec_decoder_probe_video_stream_count(probe: CHandle) -> c_int {
	handle::guard_raw(|| match super::get_box::<ProbeBox>(&probe) {
		Some(b) => b.desc.video_stream_count() as c_int,
		None => 0,
	})
}

/// `oakcodec_decoder_probe_audio_stream_count`.
#[no_mangle]
pub unsafe extern "C" fn oakcodec_decoder_probe_audio_stream_count(probe: CHandle) -> c_int {
	handle::guard_raw(|| match super::get_box::<ProbeBox>(&probe) {
		Some(b) => b.desc.audio_stream_count() as c_int,
		None => 0,
	})
}

/// `oakcodec_decoder_probe_subtitle_stream_count`.
#[no_mangle]
pub unsafe extern "C" fn oakcodec_decoder_probe_subtitle_stream_count(probe: CHandle) -> c_int {
	handle::guard_raw(|| match super::get_box::<ProbeBox>(&probe) {
		Some(b) => b.desc.subtitle_stream_count() as c_int,
		None => 0,
	})
}

/// `oakcodec_decoder_probe_get_video_stream`: fill `out` with the video
/// stream at `index` (video ordinal).
#[no_mangle]
pub unsafe extern "C" fn oakcodec_decoder_probe_get_video_stream(
	probe: CHandle,
	index: c_int,
	out: *mut OakCodecVideoStreamInfo,
) -> c_int {
	handle::guard(|| {
		if out.is_null() {
			return Err(crate::error::Error::Invalid);
		}
		let b = super::get_box::<ProbeBox>(&probe).ok_or(crate::error::Error::Invalid)?;
		let vp = b
			.desc
			.get_video_stream(index as usize)
			.ok_or(crate::error::Error::NotFound)?;
		fill_video_info(vp, unsafe { &mut *out });
		Ok(())
	})
}

/// `oakcodec_decoder_probe_get_audio_stream`: fill `out` with the audio
/// stream at `index` (audio ordinal).
#[no_mangle]
pub unsafe extern "C" fn oakcodec_decoder_probe_get_audio_stream(
	probe: CHandle,
	index: c_int,
	out: *mut OakCodecAudioStreamInfo,
) -> c_int {
	handle::guard(|| {
		if out.is_null() {
			return Err(crate::error::Error::Invalid);
		}
		let b = super::get_box::<ProbeBox>(&probe).ok_or(crate::error::Error::Invalid)?;
		let ap = b
			.desc
			.get_audio_stream(index as usize)
			.ok_or(crate::error::Error::NotFound)?;
		fill_audio_info(ap, unsafe { &mut *out });
		Ok(())
	})
}

/* ---- Decode session -------------------------------------------------------- */

/// `oakcodec_decoder_init`: new closed decoder handle, count 1.
#[no_mangle]
pub unsafe extern "C" fn oakcodec_decoder_init() -> CHandle {
	handle::guard_handle(|| {
		Ok(handle::make_owned(Mutex::new(DecoderBox {
			decoder: None,
			last_error: String::new(),
			open_filename: String::new(),
			open_stream: -1,
			open: false,
		})))
	})
}

/// `oakcodec_decoder_free`: NULL/empty no-op; nulls `ctx` afterwards.
#[no_mangle]
pub unsafe extern "C" fn oakcodec_decoder_free(decoder: *mut CHandle) {
	handle::guard_void(|| super::free_handle(decoder));
}

/// `oakcodec_decoder_open`: open `filename`'s stream `stream_index` for
/// decoding; opening the same stream again is a successful no-op.
#[no_mangle]
pub unsafe extern "C" fn oakcodec_decoder_open(
	decoder: CHandle,
	filename: *const c_char,
	stream_index: c_int,
) -> c_int {
	handle::guard(|| {
		let b =
			super::get_box::<Mutex<DecoderBox>>(&decoder).ok_or(crate::error::Error::Invalid)?;
		let filename = match crate::ffi::c_str(filename) {
			Some(f) => f,
			None => return Err(crate::error::Error::Invalid),
		};
		if stream_index < 0 {
			return Err(crate::error::Error::Invalid);
		}
		let mut b = b.lock().unwrap();

		if b.open && b.decoder.is_some() {
			if b.open_filename == filename && b.open_stream == stream_index {
				return Ok(()); // already open on this stream
			}
			let _ = b.decoder.as_ref().unwrap().close();
			b.open = false;
		}

		if !file_exists(&filename) {
			b.last_error = format!("file not found: {}", filename);
			return Err(crate::error::Error::NotFound);
		}

		let (decoder_name, _desc) = match probe_with_any_decoder(&filename) {
			Some(x) => x,
			None => {
				b.last_error = format!("no decoder recognizes this file: {}", filename);
				return Err(crate::error::Error::Failed(
					"no decoder recognizes this file".to_string(),
				));
			}
		};

		let decoder = match crate::decoder::create_from_id(&decoder_name) {
			Some(d) => d,
			None => {
				b.last_error = format!("failed to create decoder: {}", decoder_name);
				return Err(crate::error::Error::Failed(
					"failed to create decoder".to_string(),
				));
			}
		};

		let stream = CodecStream::with_block(filename.clone(), stream_index, None);
		if decoder.open(&stream).is_err() {
			b.last_error = "failed to open stream".to_string();
			return Err(crate::error::Error::Failed(
				"failed to open stream".to_string(),
			));
		}

		b.last_error.clear();
		b.decoder = Some(decoder);
		b.open_filename = filename;
		b.open_stream = stream_index;
		b.open = true;
		Ok(())
	})
}

/// `oakcodec_decoder_close`: close the current stream (safe when closed).
#[no_mangle]
pub unsafe extern "C" fn oakcodec_decoder_close(decoder: CHandle) -> c_int {
	handle::guard(|| {
		let b =
			super::get_box::<Mutex<DecoderBox>>(&decoder).ok_or(crate::error::Error::Invalid)?;
		let mut b = b.lock().unwrap();
		if b.open {
			if let Some(d) = &b.decoder {
				let _ = d.close();
			}
		}
		b.open = false;
		Ok(())
	})
}

/// `oakcodec_decoder_is_open`: 1 when a stream is open.
#[no_mangle]
pub unsafe extern "C" fn oakcodec_decoder_is_open(decoder: CHandle) -> c_int {
	handle::guard_raw(|| match super::get_box::<Mutex<DecoderBox>>(&decoder) {
		Some(b) => {
			let b = b.lock().unwrap();
			if b.open {
				1
			} else {
				0
			}
		}
		None => 0,
	})
}

/// `oakcodec_decoder_decode_video`: decode the frame at
/// `numerator/denominator` seconds into a new frame handle (count 1).
///
/// # CPP-PARITY
/// The C++ boxes an `olive::FramePtr` and happily aliases a frame the
/// decoder still holds. The Rust interim boxes `Mutex<Frame>` (see
/// `ffi::frame`), so a decode that returns a still-shared `Arc<Frame>`
/// cannot be aliased and reports an empty handle plus `last_error`.
#[no_mangle]
pub unsafe extern "C" fn oakcodec_decoder_decode_video(
	decoder: CHandle,
	numerator: c_int,
	denominator: c_int,
) -> CHandle {
	handle::guard_handle(|| {
		let b =
			super::get_box::<Mutex<DecoderBox>>(&decoder).ok_or(crate::error::Error::Invalid)?;
		let d = {
			let b = b.lock().unwrap();
			if !b.open || b.decoder.is_none() {
				// C++ returns an empty frame without touching last_error.
				return Ok(CHandle::null());
			}
			b.decoder.as_ref().unwrap().clone()
		};
		let p = RetrieveVideoParams {
			// C++ default-constructs the params and only sets `time`.
			stream: CodecStream::new(),
			time: Rational::new(numerator as i64, denominator as i64),
			length: TimeRange::default(),
			force_range: K_COLOR_RANGE_DEFAULT,
			is_image_sequence: false,
			image_sequence_digits: 0,
			image_sequence_number: 0,
			mode: RenderMode::Offline,
			alpha_is_premultiplied: false,
		};
		match d.retrieve_video_frame(&p) {
			Ok(arc) => match Arc::try_unwrap(arc) {
				Ok(frame) => Ok(handle::make_owned(Mutex::new(frame))),
				Err(_) => {
					b.lock().unwrap().last_error = "failed to decode video frame".to_string();
					Ok(CHandle::null())
				}
			},
			Err(_) => {
				b.lock().unwrap().last_error = "failed to decode video frame".to_string();
				Ok(CHandle::null())
			}
		}
	})
}

/// `oakcodec_decoder_decode_audio`: decode interleaved float audio
/// covering [in, out) into `buf` (at least `buf_frames` frames).
///
/// Returns the number of frames written, or a negative `OAKCODEC_E_*`.
/// # CPP-PARITY
/// The C++ retrieves into an allocated `SampleBuffer` and copies
/// `min(available, buf_frames)` frames. The Rust trait fills a caller-size
/// destination, so `Success` is treated as "`buf_frames` were written".
#[no_mangle]
pub unsafe extern "C" fn oakcodec_decoder_decode_audio(
	decoder: CHandle,
	in_num: c_int,
	in_den: c_int,
	out_num: c_int,
	out_den: c_int,
	sample_rate: c_int,
	channel_layout: u64,
	buf: *mut f32,
	buf_frames: c_int,
) -> c_int {
	handle::guard_raw(|| unsafe {
		decode_audio_inner(
			decoder,
			in_num,
			in_den,
			out_num,
			out_den,
			sample_rate,
			channel_layout,
			buf,
			buf_frames,
		)
	})
}

unsafe fn decode_audio_inner(
	decoder: CHandle,
	in_num: c_int,
	in_den: c_int,
	out_num: c_int,
	out_den: c_int,
	sample_rate: c_int,
	channel_layout: u64,
	buf: *mut f32,
	buf_frames: c_int,
) -> c_int {
	let b = match super::get_box::<Mutex<DecoderBox>>(&decoder) {
		Some(b) => b,
		None => return crate::error::OAKCODEC_E_INVALID,
	};
	if (buf.is_null() && buf_frames > 0) || buf_frames < 0 {
		return crate::error::OAKCODEC_E_INVALID;
	}

	let d = {
		let b = b.lock().unwrap();
		if !b.open || b.decoder.is_none() {
			return crate::error::OAKCODEC_E_STATE;
		}
		b.decoder.as_ref().unwrap().clone()
	};

	let range = TimeRange::new(
		Rational::new(in_num as i64, in_den as i64),
		Rational::new(out_num as i64, out_den as i64),
	);
	let channels = channel_layout.count_ones() as usize;
	let mut dest = vec![0f32; buf_frames as usize * channels];

	let status = match d.retrieve_audio(&mut dest, &range, sample_rate, channel_layout) {
		Ok(s) => s,
		Err(_) => {
			b.lock().unwrap().last_error = "failed to decode audio".to_string();
			return crate::error::OAKCODEC_E_FAILED;
		}
	};

	match status {
		RetrieveAudioStatus::Success => {
			// SAFETY: the caller guarantees `buf` holds at least `buf_frames`
			// interleaved floats; `dest` is exactly that size.
			unsafe { std::ptr::copy_nonoverlapping(dest.as_ptr(), buf, dest.len()) };
			buf_frames
		}
		RetrieveAudioStatus::ConformNeeded => {
			b.lock().unwrap().last_error =
				"audio requires a conform, but no task submit callback is registered (see oakcodec_set_task_submit_cb)"
					.to_string();
			crate::error::OAKCODEC_E_STATE
		}
		_ => {
			b.lock().unwrap().last_error = "failed to decode audio".to_string();
			crate::error::OAKCODEC_E_FAILED
		}
	}
}

/// `oakcodec_decoder_conform_audio`: conform the open stream's audio into
/// the given per-channel pcm files.
#[no_mangle]
pub unsafe extern "C" fn oakcodec_decoder_conform_audio(
	decoder: CHandle,
	output_filenames: *const *const c_char,
	filename_count: c_int,
	sample_rate: c_int,
	channel_layout: u64,
	sample_format: c_int,
	cancelled: OakCancelAtom,
) -> c_int {
	handle::guard(|| unsafe {
		conform_audio_inner(
			decoder,
			output_filenames,
			filename_count,
			sample_rate,
			channel_layout,
			sample_format,
			cancelled,
		)
	})
}

unsafe fn conform_audio_inner(
	decoder: CHandle,
	output_filenames: *const *const c_char,
	filename_count: c_int,
	sample_rate: c_int,
	channel_layout: u64,
	sample_format: c_int,
	cancelled: OakCancelAtom,
) -> crate::error::Result<()> {
	let b = super::get_box::<Mutex<DecoderBox>>(&decoder).ok_or(crate::error::Error::Invalid)?;
	if (output_filenames.is_null() && filename_count > 0) || filename_count < 0 {
		return Err(crate::error::Error::Invalid);
	}

	let d = {
		let b = b.lock().unwrap();
		if !b.open || b.decoder.is_none() {
			return Err(crate::error::Error::State);
		}
		b.decoder.as_ref().unwrap().clone()
	};

	let mut filenames: Vec<String> = Vec::with_capacity(filename_count as usize);
	for i in 0..filename_count {
		let p = unsafe { *output_filenames.add(i as usize) };
		let s = crate::ffi::c_str(p).ok_or(crate::error::Error::Invalid)?;
		filenames.push(s);
	}

	// The trait takes the target audio params flattened; no handle
	// construction is needed.
	let cancelled_ref = if cancelled.ctx.is_null() {
		None
	} else {
		Some(&cancelled)
	};
	let ret = d.conform_audio(
		&filenames,
		sample_rate,
		channel_layout,
		sample_format,
		cancelled_ref,
	);

	match ret {
		Ok(()) => Ok(()),
		Err(_) => {
			let heard = if cancelled.ctx.is_null() {
				0
			} else {
				unsafe { oakrender_cancelatom_heard_cancel(cancelled.clone()) }
			};
			if heard != 0 {
				Err(crate::error::Error::Cancelled)
			} else {
				Err(crate::error::Error::Failed("conform failed".to_string()))
			}
		}
	}
}

/// `oakcodec_decoder_get_image_sequence_digit_count`.
#[no_mangle]
pub unsafe extern "C" fn oakcodec_decoder_get_image_sequence_digit_count(
	filename: *const c_char,
) -> c_int {
	handle::guard_raw(|| match crate::ffi::c_str(filename) {
		Some(f) => crate::decoder::get_image_sequence_digit_count(&f),
		None => crate::error::OAKCODEC_E_INVALID,
	})
}

/// `oakcodec_decoder_get_image_sequence_index`.
#[no_mangle]
pub unsafe extern "C" fn oakcodec_decoder_get_image_sequence_index(filename: *const c_char) -> i64 {
	handle::guard_i64(|| match crate::ffi::c_str(filename) {
		Some(f) => crate::decoder::get_image_sequence_index(&f),
		None => crate::error::OAKCODEC_E_INVALID as i64,
	})
}

/// `oakcodec_decoder_transform_image_sequence_file_name` (two-stage).
#[no_mangle]
pub unsafe extern "C" fn oakcodec_decoder_transform_image_sequence_file_name(
	filename: *const c_char,
	number: i64,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	handle::guard_raw(|| match crate::ffi::c_str(filename) {
		Some(f) => {
			let s = crate::decoder::transform_image_sequence_file_name(&f, number);
			super::string_out(&s, buf, buf_size)
		}
		None => crate::error::OAKCODEC_E_INVALID,
	})
}

/// `oakcodec_decoder_last_error` (two-stage).
#[no_mangle]
pub unsafe extern "C" fn oakcodec_decoder_last_error(
	decoder: CHandle,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	handle::guard_raw(|| match super::get_box::<Mutex<DecoderBox>>(&decoder) {
		Some(b) => {
			let b = b.lock().unwrap();
			super::string_out(&b.last_error, buf, buf_size)
		}
		None => super::string_out("", buf, buf_size),
	})
}

#[cfg(test)]
mod tests {
	use super::*;
	use crate::bridge::common::{
		oakcommon_videoparams_init_basic, oakcommon_videoparams_init_with_time_base,
		oakcommon_videoparams_set_stream_index, oakcore_audioparams_create,
	};
	use crate::bridge::render::{oakrender_cancelatom_cancel, oakrender_cancelatom_init};
	use crate::decoder::set_test_decoders;
	use crate::error::{OAKCODEC_E_CANCELLED, OAKCODEC_E_INVALID, OAKCODEC_E_STATE};
	use crate::footagedescription::StreamEntry;

	/// The crate-wide ffi test lock (`crate::ffi::lock_tests`) serializes
	/// every test in this module (they share the global probe error, the
	/// injected decoder registry and the fake's state).

	/// Fake decoder driving the ffi export tests.
	struct FakeDecoder {
		retain_frame: bool,
		retained: Mutex<Option<Arc<Frame>>>,
		conform_fails: Mutex<bool>,
		audio_conform_needed: Mutex<bool>,
	}

	impl FakeDecoder {
		fn new(retain_frame: bool) -> Arc<dyn Decoder> {
			Arc::new(FakeDecoder {
				retain_frame,
				retained: Mutex::new(None),
				conform_fails: Mutex::new(false),
				audio_conform_needed: Mutex::new(false),
			})
		}

		fn with_audio_conform_needed() -> Arc<FakeDecoder> {
			Arc::new(FakeDecoder {
				retain_frame: false,
				retained: Mutex::new(None),
				conform_fails: Mutex::new(false),
				audio_conform_needed: Mutex::new(true),
			})
		}

		fn with_conform_failing() -> Arc<FakeDecoder> {
			Arc::new(FakeDecoder {
				retain_frame: false,
				retained: Mutex::new(None),
				conform_fails: Mutex::new(false),
				audio_conform_needed: Mutex::new(false),
			})
		}
	}

	impl Decoder for FakeDecoder {
		fn id(&self) -> String {
			"fake".to_string()
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
			_cancelled: Option<&OakCancelAtom>,
		) -> Option<FootageDescription> {
			if filename.ends_with("test_video.mp4") {
				let mut desc = FootageDescription::new("fake");
				let vp =
					unsafe { oakcommon_videoparams_init_with_time_base(1920, 1080, 1001, 30000) };
				unsafe { oakcommon_videoparams_set_stream_index(vp.clone(), 0) };
				desc.push_stream(StreamEntry::Video(vp));
				Some(desc)
			} else if filename.ends_with("test_audio.mp4") {
				let mut desc = FootageDescription::new("fake");
				let ap = unsafe { oakcore_audioparams_create(48000, 0x3, 10) };
				desc.push_stream(StreamEntry::Audio(OakAudioParams {
					ctx: ap as *mut c_void,
					addref: None,
					release: None,
					abi_version: crate::handle::OAKCODEC_ABI_VERSION,
				}));
				Some(desc)
			} else {
				None
			}
		}

		fn open(&self, _stream: &CodecStream) -> crate::error::Result<()> {
			Ok(())
		}

		fn close(&self) -> crate::error::Result<()> {
			Ok(())
		}

		fn stream(&self) -> CodecStream {
			CodecStream::new()
		}

		fn retrieve_video_frame(
			&self,
			_p: &RetrieveVideoParams,
		) -> crate::error::Result<Arc<Frame>> {
			let params = unsafe { oakcommon_videoparams_init_basic(100, 50) };
			let frame = Arc::new(Frame::with_params(params));
			if self.retain_frame {
				// Keep an alias alive so the ffi decode sees a shared Arc.
				*self.retained.lock().unwrap() = Some(frame.clone());
			}
			Ok(frame)
		}

		fn retrieve_video(
			&self,
			_p: &RetrieveVideoParams,
		) -> crate::error::Result<crate::bridge::render::OakRenderTexture> {
			Err(crate::error::Error::Failed("no texture".to_string()))
		}

		fn retrieve_audio(
			&self,
			dest: &mut [f32],
			_range: &TimeRange,
			_sample_rate: i32,
			_channel_layout: u64,
		) -> crate::error::Result<RetrieveAudioStatus> {
			if *self.audio_conform_needed.lock().unwrap() {
				return Ok(RetrieveAudioStatus::ConformNeeded);
			}
			for s in dest.iter_mut() {
				*s = 1.0;
			}
			Ok(RetrieveAudioStatus::Success)
		}

		fn conform_audio(
			&self,
			_output_filenames: &[String],
			_sample_rate: i32,
			_channel_layout: u64,
			_sample_format: i32,
			_cancelled: Option<&OakCancelAtom>,
		) -> crate::error::Result<()> {
			if *self.conform_fails.lock().unwrap() {
				Err(crate::error::Error::Failed("conform failed".to_string()))
			} else {
				Ok(())
			}
		}
	}

	fn inject(fake: Arc<dyn Decoder>) {
		set_test_decoders(vec![fake]);
	}

	fn restore() {
		set_test_decoders(Vec::new());
	}

	fn cstr(s: &str) -> std::ffi::CString {
		std::ffi::CString::new(s).unwrap()
	}

	fn empty_cancel() -> OakCancelAtom {
		OakCancelAtom {
			ctx: std::ptr::null_mut(),
			addref: None,
			release: None,
			abi_version: 0,
		}
	}

	fn media_file(name: &str) -> String {
		let dir =
			std::env::temp_dir().join(format!("oakcodec_ffi_dec_{}_{}", name, std::process::id()));
		let _ = std::fs::create_dir_all(&dir);
		let path = dir.join(name);
		let _ = std::fs::write(&path, b"media");
		path.to_string_lossy().into_owned()
	}

	#[test]
	fn probe_golden_video() {
		let _g = crate::ffi::lock_tests();
		inject(FakeDecoder::new(false));

		let v = media_file("test_video.mp4");
		let f = cstr(&v);
		let before = handle::alive_count();
		let mut h = unsafe { oakcodec_decoder_probe(f.as_ptr()) };
		assert!(!h.is_null());
		assert_eq!(handle::alive_count(), before + 1);

		let mut name = [0i8; 64];
		let rc = unsafe { oakcodec_decoder_probe_decoder_name(h, name.as_mut_ptr(), 64) };
		assert_eq!(rc, 5); // "fake" + NUL
		assert_eq!(crate::ffi::c_str(name.as_ptr()).as_deref(), Some("fake"));

		assert_eq!(unsafe { oakcodec_decoder_probe_video_stream_count(h) }, 1);
		assert_eq!(unsafe { oakcodec_decoder_probe_audio_stream_count(h) }, 0);
		assert_eq!(
			unsafe { oakcodec_decoder_probe_subtitle_stream_count(h) },
			0
		);

		let mut info: OakCodecVideoStreamInfo = unsafe { std::mem::zeroed() };
		let rc = unsafe { oakcodec_decoder_probe_get_video_stream(h, 0, &mut info) };
		assert_eq!(rc, crate::error::OAKCODEC_OK);
		assert_eq!(info.stream_index, 0);
		assert_eq!(info.width, 1920);
		assert_eq!(info.height, 1080);
		assert_eq!(info.time_base_num, 1001);
		assert_eq!(info.time_base_den, 30000);
		assert_eq!(info.channel_count, 4);
		assert_eq!(info.interlaced, 0);
		// frame_rate/duration/colors default to 0 in the stub.
		assert_eq!(info.frame_rate_num, 0);
		assert_eq!(info.duration_ts, 0);

		// index out of range / null out.
		let rc = unsafe { oakcodec_decoder_probe_get_video_stream(h, 3, &mut info) };
		assert_eq!(rc, crate::error::OAKCODEC_E_NOT_FOUND);
		let rc = unsafe { oakcodec_decoder_probe_get_video_stream(h, 0, std::ptr::null_mut()) };
		assert_eq!(rc, OAKCODEC_E_INVALID);

		// probe_last_error is cleared on success.
		let mut err = [0i8; 256];
		let rc = unsafe { oakcodec_probe_last_error(err.as_mut_ptr(), 256) };
		assert_eq!(rc, 1); // just the NUL
		assert_eq!(crate::ffi::c_str(err.as_ptr()).as_deref(), Some(""));

		unsafe { oakcodec_decoder_free(&mut h) };
		assert!(h.is_null());
		assert_eq!(handle::alive_count(), before);
		restore();
	}

	#[test]
	fn probe_golden_audio() {
		let _g = crate::ffi::lock_tests();
		inject(FakeDecoder::new(false));

		let v = media_file("test_audio.mp4");
		let f = cstr(&v);
		let mut h = unsafe { oakcodec_decoder_probe(f.as_ptr()) };
		assert!(!h.is_null());
		assert_eq!(unsafe { oakcodec_decoder_probe_video_stream_count(h) }, 0);
		assert_eq!(unsafe { oakcodec_decoder_probe_audio_stream_count(h) }, 1);

		let mut info: OakCodecAudioStreamInfo = unsafe { std::mem::zeroed() };
		let rc = unsafe { oakcodec_decoder_probe_get_audio_stream(h, 0, &mut info) };
		assert_eq!(rc, crate::error::OAKCODEC_OK);
		assert_eq!(info.stream_index, 0);
		assert_eq!(info.sample_rate, 48000);
		assert_eq!(info.channel_layout, 0x3);
		assert_eq!(info.channel_count, 2);
		assert_eq!(info.time_base_num, 1);
		assert_eq!(info.time_base_den, 48000);
		assert_eq!(info.duration_ts, 0);

		unsafe { oakcodec_decoder_free(&mut h) };
		restore();
	}

	#[test]
	fn probe_failures() {
		let _g = crate::ffi::lock_tests();
		inject(FakeDecoder::new(false));

		// NULL filename.
		let mut h = unsafe { oakcodec_decoder_probe(std::ptr::null()) };
		assert!(h.is_null());
		let mut err = [0i8; 256];
		unsafe { oakcodec_probe_last_error(err.as_mut_ptr(), 256) };
		assert_eq!(
			crate::ffi::c_str(err.as_ptr()).as_deref(),
			Some("no filename given")
		);

		// Empty filename.
		let f = cstr("");
		let mut h = unsafe { oakcodec_decoder_probe(f.as_ptr()) };
		assert!(h.is_null());

		// Nonexistent file.
		let f = cstr("/definitely/not/here.mp4");
		let mut h = unsafe { oakcodec_decoder_probe(f.as_ptr()) };
		assert!(h.is_null());
		unsafe { oakcodec_probe_last_error(err.as_mut_ptr(), 256) };
		assert_eq!(
			crate::ffi::c_str(err.as_ptr()).as_deref(),
			Some("file not found: /definitely/not/here.mp4")
		);

		// A file no decoder recognizes (a real file: the test harness).
		let me = std::env::current_exe().unwrap();
		let me = me.to_string_lossy().into_owned();
		let f = cstr(&me);
		let mut h = unsafe { oakcodec_decoder_probe(f.as_ptr()) };
		assert!(h.is_null());
		unsafe { oakcodec_probe_last_error(err.as_mut_ptr(), 256) };
		assert!(crate::ffi::c_str(err.as_ptr())
			.as_deref()
			.unwrap()
			.contains("no decoder recognizes"));

		// Empty handle on probe exports.
		let empty = CHandle::null();
		assert_eq!(
			unsafe { oakcodec_decoder_probe_decoder_name(empty, err.as_mut_ptr(), 256) },
			OAKCODEC_E_INVALID
		);
		assert_eq!(
			unsafe { oakcodec_decoder_probe_video_stream_count(empty) },
			0
		);
		restore();
	}

	#[test]
	fn open_decode_video_golden() {
		let _g = crate::ffi::lock_tests();
		inject(FakeDecoder::new(false));

		let mut h = unsafe { oakcodec_decoder_init() };
		assert!(!h.is_null());
		assert_eq!(unsafe { oakcodec_decoder_is_open(h) }, 0);

		let v = media_file("test_video.mp4");
		let f = cstr(&v);
		let rc = unsafe { oakcodec_decoder_open(h, f.as_ptr(), 0) };
		assert_eq!(rc, crate::error::OAKCODEC_OK);
		assert_eq!(unsafe { oakcodec_decoder_is_open(h) }, 1);

		// Reopening the same stream is a successful no-op.
		let rc = unsafe { oakcodec_decoder_open(h, f.as_ptr(), 0) };
		assert_eq!(rc, crate::error::OAKCODEC_OK);

		let mut frame = unsafe { oakcodec_decoder_decode_video(h, 1, 30) };
		assert!(!frame.is_null());
		assert_eq!(
			unsafe { crate::ffi::frame::oakcodec_frame_width(frame) },
			100
		);
		assert_eq!(
			unsafe { crate::ffi::frame::oakcodec_frame_height(frame) },
			50
		);

		unsafe { crate::ffi::frame::oakcodec_frame_free(&mut frame) };
		let rc = unsafe { oakcodec_decoder_close(h) };
		assert_eq!(rc, crate::error::OAKCODEC_OK);
		assert_eq!(unsafe { oakcodec_decoder_is_open(h) }, 0);
		unsafe { oakcodec_decoder_free(&mut h) };
		restore();
	}

	#[test]
	fn decode_video_shared_frame_reports_error() {
		let _g = crate::ffi::lock_tests();
		inject(FakeDecoder::new(true)); // decoder retains the frame

		let mut h = unsafe { oakcodec_decoder_init() };
		let v = media_file("test_video.mp4");
		let f = cstr(&v);
		unsafe { oakcodec_decoder_open(h, f.as_ptr(), 0) };
		let mut frame = unsafe { oakcodec_decoder_decode_video(h, 0, 1) };
		assert!(frame.is_null());
		let mut err = [0i8; 128];
		unsafe { oakcodec_decoder_last_error(h, err.as_mut_ptr(), 128) };
		assert_eq!(
			crate::ffi::c_str(err.as_ptr()).as_deref(),
			Some("failed to decode video frame")
		);
		unsafe { oakcodec_decoder_free(&mut h) };
		restore();
	}

	#[test]
	fn decode_audio_golden_and_errors() {
		let _g = crate::ffi::lock_tests();
		inject(FakeDecoder::new(false));

		let mut h = unsafe { oakcodec_decoder_init() };
		let v = media_file("test_audio.mp4");
		let f = cstr(&v);
		let rc = unsafe { oakcodec_decoder_open(h, f.as_ptr(), 0) };
		assert_eq!(rc, crate::error::OAKCODEC_OK);

		let mut buf = [0f32; 64];
		let frames = unsafe {
			oakcodec_decoder_decode_audio(h, 0, 1, 1, 1, 48000, 0x3, buf.as_mut_ptr(), 16)
		};
		assert_eq!(frames, 16);
		// Interleaved stereo filled by the fake.
		assert_eq!(buf[0], 1.0);
		assert_eq!(buf[31], 1.0);

		// Invalid args.
		let rc = unsafe {
			oakcodec_decoder_decode_audio(h, 0, 1, 1, 1, 48000, 0x3, std::ptr::null_mut(), 16)
		};
		assert_eq!(rc, OAKCODEC_E_INVALID);
		let rc = unsafe {
			oakcodec_decoder_decode_audio(h, 0, 1, 1, 1, 48000, 0x3, buf.as_mut_ptr(), -1)
		};
		assert_eq!(rc, OAKCODEC_E_INVALID);

		// Not open -> E_STATE.
		let mut h2 = unsafe { oakcodec_decoder_init() };
		let rc = unsafe {
			oakcodec_decoder_decode_audio(h2, 0, 1, 1, 1, 48000, 0x3, buf.as_mut_ptr(), 16)
		};
		assert_eq!(rc, OAKCODEC_E_STATE);
		unsafe { oakcodec_decoder_free(&mut h2) };

		// Empty handle -> E_INVALID.
		let empty = CHandle::null();
		let rc = unsafe {
			oakcodec_decoder_decode_audio(empty, 0, 1, 1, 1, 48000, 0x3, buf.as_mut_ptr(), 16)
		};
		assert_eq!(rc, OAKCODEC_E_INVALID);

		unsafe { oakcodec_decoder_free(&mut h) };
		restore();
	}

	#[test]
	fn decode_audio_conform_needed_is_state() {
		let _g = crate::ffi::lock_tests();
		set_test_decoders(vec![FakeDecoder::with_audio_conform_needed()]);

		let mut h = unsafe { oakcodec_decoder_init() };
		let v = media_file("test_audio.mp4");
		let f = cstr(&v);
		unsafe { oakcodec_decoder_open(h, f.as_ptr(), 0) };
		let mut buf = [0f32; 64];
		let rc = unsafe {
			oakcodec_decoder_decode_audio(h, 0, 1, 1, 1, 48000, 0x3, buf.as_mut_ptr(), 16)
		};
		assert_eq!(rc, OAKCODEC_E_STATE);
		let mut err = [0i8; 256];
		unsafe { oakcodec_decoder_last_error(h, err.as_mut_ptr(), 256) };
		assert!(crate::ffi::c_str(err.as_ptr())
			.as_deref()
			.unwrap()
			.contains("conform"));
		unsafe { oakcodec_decoder_free(&mut h) };
		restore();
	}

	#[test]
	fn conform_audio_golden_cancelled_and_errors() {
		let _g = crate::ffi::lock_tests();
		let fake = FakeDecoder::with_conform_failing();
		set_test_decoders(vec![fake.clone()]);

		let mut h = unsafe { oakcodec_decoder_init() };
		let v = media_file("test_audio.mp4");
		let f = cstr(&v);
		unsafe { oakcodec_decoder_open(h, f.as_ptr(), 0) };

		let a = cstr("a.pcm");
		let b = cstr("b.pcm");
		let files = [a.as_ptr(), b.as_ptr()];
		let rc = unsafe {
			oakcodec_decoder_conform_audio(h, files.as_ptr(), 2, 48000, 0x3, 10, empty_cancel())
		};
		assert_eq!(rc, crate::error::OAKCODEC_OK);

		// Not open -> E_STATE.
		let mut h2 = unsafe { oakcodec_decoder_init() };
		let rc = unsafe {
			oakcodec_decoder_conform_audio(h2, files.as_ptr(), 2, 48000, 0x3, 10, empty_cancel())
		};
		assert_eq!(rc, OAKCODEC_E_STATE);

		// Invalid args.
		let rc = unsafe {
			oakcodec_decoder_conform_audio(h, std::ptr::null(), 1, 48000, 0x3, 10, empty_cancel())
		};
		assert_eq!(rc, OAKCODEC_E_INVALID);
		let rc = unsafe {
			oakcodec_decoder_conform_audio(h, files.as_ptr(), -1, 48000, 0x3, 10, empty_cancel())
		};
		assert_eq!(rc, OAKCODEC_E_INVALID);

		// Failure without cancellation -> E_FAILED.
		*fake.conform_fails.lock().unwrap() = true;
		let rc = unsafe {
			oakcodec_decoder_conform_audio(h, files.as_ptr(), 2, 48000, 0x3, 10, empty_cancel())
		};
		assert_eq!(rc, crate::error::OAKCODEC_E_FAILED);

		// Failure with a cancelled atom -> E_CANCELLED.
		let atom = unsafe { oakrender_cancelatom_init() };
		unsafe { oakrender_cancelatom_cancel(atom.clone()) };
		let rc =
			unsafe { oakcodec_decoder_conform_audio(h, files.as_ptr(), 2, 48000, 0x3, 10, atom) };
		assert_eq!(rc, OAKCODEC_E_CANCELLED);

		unsafe { oakcodec_decoder_free(&mut h) };
		unsafe { oakcodec_decoder_free(&mut h2) };
		restore();
	}

	#[test]
	fn image_sequence_exports() {
		let _g = crate::ffi::lock_tests();

		let f = cstr("frame_0001.png");
		assert_eq!(
			unsafe { oakcodec_decoder_get_image_sequence_digit_count(f.as_ptr()) },
			4
		);
		assert_eq!(
			unsafe { oakcodec_decoder_get_image_sequence_index(f.as_ptr()) },
			1
		);

		let mut buf = [0i8; 128];
		let rc = unsafe {
			oakcodec_decoder_transform_image_sequence_file_name(
				f.as_ptr(),
				7,
				buf.as_mut_ptr(),
				128,
			)
		};
		assert!(rc > 0);
		assert_eq!(
			crate::ffi::c_str(buf.as_ptr()).as_deref(),
			Some("frame_0007.png")
		);

		// NULL filename -> E_INVALID.
		assert_eq!(
			unsafe { oakcodec_decoder_get_image_sequence_digit_count(std::ptr::null()) },
			OAKCODEC_E_INVALID
		);
		assert_eq!(
			unsafe { oakcodec_decoder_get_image_sequence_index(std::ptr::null()) },
			OAKCODEC_E_INVALID as i64
		);
		assert_eq!(
			unsafe {
				oakcodec_decoder_transform_image_sequence_file_name(
					std::ptr::null(),
					1,
					buf.as_mut_ptr(),
					128,
				)
			},
			OAKCODEC_E_INVALID
		);
	}

	#[test]
	fn open_errors_and_last_error_empty_handle() {
		let _g = crate::ffi::lock_tests();
		inject(FakeDecoder::new(false));

		let mut h = unsafe { oakcodec_decoder_init() };

		// NULL filename -> E_INVALID.
		let rc = unsafe { oakcodec_decoder_open(h, std::ptr::null(), 0) };
		assert_eq!(rc, OAKCODEC_E_INVALID);

		// Negative stream index -> E_INVALID.
		let v = media_file("test_video.mp4");
		let f = cstr(&v);
		let rc = unsafe { oakcodec_decoder_open(h, f.as_ptr(), -1) };
		assert_eq!(rc, OAKCODEC_E_INVALID);

		// Missing file -> E_NOT_FOUND.
		let f = cstr("/missing/file.mp4");
		let rc = unsafe { oakcodec_decoder_open(h, f.as_ptr(), 0) };
		assert_eq!(rc, crate::error::OAKCODEC_E_NOT_FOUND);
		let mut err = [0i8; 256];
		unsafe { oakcodec_decoder_last_error(h, err.as_mut_ptr(), 256) };
		assert_eq!(
			crate::ffi::c_str(err.as_ptr()).as_deref(),
			Some("file not found: /missing/file.mp4")
		);

		// Empty handle -> E_INVALID and empty last_error.
		let empty = CHandle::null();
		let rc = unsafe { oakcodec_decoder_open(empty, f.as_ptr(), 0) };
		assert_eq!(rc, OAKCODEC_E_INVALID);
		let rc = unsafe { oakcodec_decoder_close(empty) };
		assert_eq!(rc, OAKCODEC_E_INVALID);
		assert_eq!(unsafe { oakcodec_decoder_is_open(empty) }, 0);
		let rc = unsafe { oakcodec_decoder_last_error(empty, err.as_mut_ptr(), 256) };
		assert_eq!(rc, 1);
		assert_eq!(crate::ffi::c_str(err.as_ptr()).as_deref(), Some(""));

		unsafe { oakcodec_decoder_free(&mut h) };
		restore();
	}
}
