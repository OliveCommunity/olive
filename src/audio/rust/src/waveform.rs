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

//! Visual waveform store (`olive::AudioVisualWaveform`). Holds channel-
//! interleaved min/max pairs at multiple mipmap levels for efficient display
//! at any zoom scale. `draw_sample()`/`draw_waveform()` are app-layer
//! QPainter helpers and live in the facade; this module only stores and
//! summarizes data.

use std::collections::BTreeMap;
use std::ffi::{c_int, CStr};

use oakcore_rs::Rational;

use crate::bridge::codec::AudioStreamInfo;
use crate::bridge::ffmpeg::{
	fb_audio_graph_create, fb_audio_graph_free, fb_audio_graph_pull,
	fb_audio_graph_push, fb_decoder_close, fb_decoder_create, fb_decoder_free,
	fb_decoder_get_frame, fb_decoder_get_stream_info, fb_decoder_open,
	fb_frame_alloc, fb_frame_free, fb_frame_get_data, fb_frame_get_nb_samples,
	fb_packet_alloc, fb_packet_free, AudioGraph, AudioGraphConfig, Decoder,
	Frame, Packet, SampleFormat,
};
use crate::error::{Error, Result};
use crate::handle::{free_handle, make_owned, CHandle};

/// Maximum channel count accepted by [`extract`]. The C++ plane array is a
/// fixed `OAKAUDIO_EXTRACT_MAX_CHANNELS` (64) stack buffer; the Rust
/// rewrite rejects wider streams instead of overflowing.
///
/// `// CPP-PARITY: src/audio/c_api/waveform.cpp:67`.
pub const EXTRACT_MAX_CHANNELS: i32 = 64;

/// Minimum overridable sample rate. Must be a power of two.
///
/// `// CPP-PARITY: src/audio/src/audiovisualwaveform.cpp:30`
/// (`AudioVisualWaveform::k_minimum_sample_rate`).
pub fn minimum_sample_rate() -> Rational {
	Rational::new(1, 8)
}
/// Maximum overridable sample rate. Must be a power of two.
///
/// `// CPP-PARITY: src/audio/src/audiovisualwaveform.cpp:31`
/// (`AudioVisualWaveform::k_maximum_sample_rate`).
pub fn maximum_sample_rate() -> Rational {
	Rational::new(1024, 1)
}

/// One min/max pair for a single channel.
///
/// `// CPP-PARITY: src/audio/src/audiovisualwaveform.h`
/// (`AudioVisualWaveform::SamplePerChannel`).
#[derive(Debug, Clone, Copy, PartialEq, Default)]
pub struct SamplePerChannel {
	/// Minimum amplitude in the window.
	pub min: f32,
	/// Maximum amplitude in the window.
	pub max: f32,
}

/// One display sample: a `SamplePerChannel` per channel, channel-interleaved.
///
/// `// CPP-PARITY: src/audio/src/audiovisualwaveform.h`
/// (`AudioVisualWaveform::Sample`).
pub type Sample = Vec<SamplePerChannel>;

/// A visual waveform store with mipmapped min/max data.
///
/// `// CPP-PARITY: src/audio/src/audiovisualwaveform.h`
/// (`AudioVisualWaveform`).
#[derive(Debug, Clone)]
pub struct AudioVisualWaveform {
	/// Timeline time the stored data starts at (shifts on trim_in).
	virtual_start: Rational,
	channels: i32,
	length: Rational,
	// Channel-interleaved min/max samples, keyed by the mipmap sample rate.
	mipmapped_data: BTreeMap<Rational, Sample>,
}

/// `floor(time * sample_rate) * channels` — every mipmap index is
/// channel-interleaved, so time conversions scale by the channel count.
///
/// `// CPP-PARITY: src/audio/src/audiovisualwaveform.cpp:359`
/// (`AudioVisualWaveform::time_to_samples`).
fn time_to_samples(time: f64, sample_rate: f64, channels: i32) -> usize {
	let v = (time * sample_rate).floor();
	if v <= 0.0 {
		return 0;
	}
	v as usize * channels.max(0) as usize
}

impl AudioVisualWaveform {
	/// Create an empty waveform (channel count 0) with the full mipmap
	/// chain pre-allocated.
	///
	/// `// CPP-PARITY: src/audio/src/audiovisualwaveform.cpp:33`
	/// (`AudioVisualWaveform::AudioVisualWaveform`): mipmaps from 1/8 to
	/// 1024 points/second, doubling.
	pub fn new() -> AudioVisualWaveform {
		let mut w = AudioVisualWaveform {
			virtual_start: Rational::NULL,
			channels: 0,
			length: Rational::NULL,
			mipmapped_data: BTreeMap::new(),
		};
		let mut rate = minimum_sample_rate();
		while rate <= maximum_sample_rate() {
			w.mipmapped_data.insert(rate, Vec::new());
			rate = rate * Rational::new(2, 1);
		}
		w
	}

	/// Channel count.
	pub fn channel_count(&self) -> i32 {
		self.channels
	}

	/// Replace the channel count.
	pub fn set_channel_count(&mut self, channels: i32) {
		self.channels = channels;
	}

	/// Length of the waveform in seconds.
	pub fn length(&self) -> Rational {
		self.length
	}

	/// Keep `virtual_start` consistent with a new write position.
	///
	/// `// CPP-PARITY: src/audio/src/audiovisualwaveform.cpp:90`
	/// (`AudioVisualWaveform::validate_virtual_start`): writing before the
	/// current start prepends via a NEGATIVE trim_in.
	fn validate_virtual_start(&mut self, new_start: Rational) {
		if self.length.is_null() {
			self.virtual_start = new_start;
		} else if self.virtual_start > new_start {
			self.trim_in(new_start - self.virtual_start);
		}
	}

	/// `// CPP-PARITY: src/audio/src/audiovisualwaveform.cpp:40`
	/// (`AudioVisualWaveform::overwrite_samples_from_buffer`).
	#[allow(clippy::too_many_arguments)]
	fn overwrite_samples_from_buffer(
		planar: &[&[f32]],
		sample_rate: i32,
		start: Rational,
		target_rate: f64,
		channels: i32,
		data: &mut Sample,
	) -> (usize, usize) {
		let sample_count = planar.first().map_or(0, |p| p.len());
		let start_index = time_to_samples(start.to_f64(), target_rate, channels);
		let samples_length = time_to_samples(
			sample_count as f64 / f64::from(sample_rate),
			target_rate,
			channels,
		);

		let end_index = start_index + samples_length;
		if data.len() < end_index {
			data.resize(end_index, SamplePerChannel::default());
		}

		let chunk_size = f64::from(sample_rate) / target_rate;

		let mut i = 0usize;
		while i < samples_length {
			let src_start =
				((i as f64 * chunk_size).round() as usize) / channels as usize;
			let src_end = (((i + channels as usize) as f64 * chunk_size).round() as usize
				/ channels as usize)
				.min(sample_count);

			let summary = Self::sum_samples(planar, src_start, src_end - src_start);

			data[i + start_index..i + start_index + summary.len()]
				.copy_from_slice(&summary);

			i += channels as usize;
		}

		(start_index, samples_length)
	}

	/// `// CPP-PARITY: src/audio/src/audiovisualwaveform.cpp:69`
	/// (`AudioVisualWaveform::overwrite_samples_from_mipmap`): mipmaps are
	/// powers of two so the integer chunk division is exact.
	#[allow(clippy::too_many_arguments)]
	fn overwrite_samples_from_mipmap(
		input: &Sample,
		input_sample_rate: f64,
		start: Rational,
		output_rate: f64,
		channels: i32,
		output_data: &mut Sample,
		input_start: usize,
		input_length: usize,
	) -> (usize, usize) {
		let start_index = time_to_samples(start.to_f64(), output_rate, channels);
		let samples_length = time_to_samples(
			(input_length / channels as usize) as f64 / input_sample_rate,
			output_rate,
			channels,
		);

		let end_index = start_index + samples_length;
		if output_data.len() < end_index {
			output_data.resize(end_index, SamplePerChannel::default());
		}

		let chunk_size = (input_sample_rate / output_rate) as usize;

		let mut i = 0usize;
		while i < samples_length {
			let summary = Self::re_sum_samples(
				&input[input_start + (i * chunk_size)..],
				chunk_size * channels as usize,
				channels,
			);

			output_data[i + start_index..i + start_index + summary.len()]
				.copy_from_slice(&summary);

			i += channels as usize;
		}

		(start_index, samples_length)
	}

	/// Write planar samples into the waveform, expanding as needed.
	///
	/// `// CPP-PARITY: src/audio/src/audiovisualwaveform.cpp:98`
	/// (`AudioVisualWaveform::overwrite_samples`): the largest mipmap is
	/// filled from the raw samples, then each smaller mipmap from the one
	/// before it.
	pub fn overwrite_samples(&mut self, planar: &[&[f32]], sample_rate: i32, start: Rational) {
		if self.channels == 0 {
			// C++ logs "channel count is zero" and returns
			return;
		}

		self.validate_virtual_start(start);

		// Process the largest mipmap directly from the samples
		let rates: Vec<Rational> = self.mipmapped_data.keys().copied().collect();
		let channels = self.channels;
		let rel_start = start - self.virtual_start;

		let mut iter_input: Option<(Sample, f64, usize, usize)> = None;
		for rate in rates.iter().rev() {
			let out_rate = rate.to_f64();
			match iter_input.take() {
				None => {
					let data = self.mipmapped_data.get_mut(rate).unwrap();
					let (s, l) = Self::overwrite_samples_from_buffer(
						planar,
						sample_rate,
						rel_start,
						out_rate,
						channels,
						data,
					);
					iter_input = Some((data.clone(), out_rate, s, l));
				}
				Some((input, input_rate, input_start, input_length)) => {
					let data = self.mipmapped_data.get_mut(rate).unwrap();
					let (s, l) = Self::overwrite_samples_from_mipmap(
						&input, input_rate, rel_start, out_rate, channels, data,
						input_start, input_length,
					);
					iter_input = Some((data.clone(), out_rate, s, l));
				}
			}
		}

		let sample_count = planar.first().map_or(0, |p| p.len()) as i64;
		let sample_length = Rational::new(sample_count, i64::from(sample_rate));
		self.length = self.length.max(start + sample_length);
	}

	/// Copy min/max data from another waveform over a destination range.
	///
	/// `// CPP-PARITY: src/audio/src/audiovisualwaveform.cpp:137`
	/// (`AudioVisualWaveform::overwrite_sums`): source indexing uses the
	/// SOURCE's channel count; a null `length` copies everything from
	/// `offset`.
	pub fn overwrite_sums(
		&mut self,
		sums: &AudioVisualWaveform,
		dest: Rational,
		offset: Rational,
		length: Rational,
	) {
		self.validate_virtual_start(dest);

		let rates: Vec<Rational> = self.mipmapped_data.keys().copied().collect();
		for rate in rates {
			let rate_dbl = rate.to_f64();

			let their_arr = match sums.mipmapped_data.get(&rate) {
				Some(a) => a,
				None => continue,
			};

			// Get our destination sample
			let our_start_index =
				time_to_samples((dest - self.virtual_start).to_f64(), rate_dbl, self.channels);

			// Get our source sample, indexing with the SOURCE's channel count
			let their_start_index = (offset.to_f64() * rate_dbl).floor() as usize
				* sums.channel_count().max(0) as usize;
			if their_start_index >= their_arr.len() {
				continue;
			}

			// Determine how much we're copying
			let mut copy_len = their_arr.len() - their_start_index;
			if !length.is_null() {
				copy_len = copy_len.min(time_to_samples(length.to_f64(), rate_dbl, self.channels));
				if copy_len == 0 {
					continue;
				}
			}

			let their_slice = their_arr[their_start_index..their_start_index + copy_len].to_vec();
			let our_arr = self.mipmapped_data.get_mut(&rate).unwrap();

			// Determine end index of our array
			let end_index = our_start_index + copy_len;
			if our_arr.len() < end_index {
				our_arr.resize(end_index, SamplePerChannel::default());
			}

			our_arr[our_start_index..end_index].copy_from_slice(&their_slice);
		}

		self.length = self.length.max(
			dest + if length.is_null() {
				sums.length() - offset
			} else {
				length
			},
		);
	}

	/// Write silence over a range.
	///
	/// `// CPP-PARITY: src/audio/src/audiovisualwaveform.cpp:187`
	/// (`AudioVisualWaveform::overwrite_silence`).
	pub fn overwrite_silence(&mut self, start: Rational, length: Rational) {
		self.validate_virtual_start(start);

		for (rate, our_arr) in self.mipmapped_data.iter_mut() {
			let rate_dbl = rate.to_f64();

			let our_start_index = time_to_samples(
				(start - self.virtual_start).to_f64(),
				rate_dbl,
				self.channels,
			);
			let our_length_index = time_to_samples(length.to_f64(), rate_dbl, self.channels);
			let our_end_index = our_start_index + our_length_index;

			if our_arr.len() < our_end_index {
				our_arr.resize(our_end_index, SamplePerChannel::default());
			}

			for p in &mut our_arr[our_start_index..our_start_index + our_length_index] {
				*p = SamplePerChannel::default();
			}
		}

		self.length = self.length.max(start + length);
	}

	/// Trim the start of the waveform.
	///
	/// `// CPP-PARITY: src/audio/src/audiovisualwaveform.cpp:218`
	/// (`AudioVisualWaveform::trim_in`): a NEGATIVE length prepends silence
	/// and leaves `length_` unchanged (the absolute end does not move).
	pub fn trim_in(&mut self, length: Rational) {
		if length.is_null() {
			return;
		}

		self.virtual_start = self.virtual_start + length;

		let negative = length < Rational::NULL || length.to_f64() < 0.0;
		let abs_length = if negative { Rational::NULL - length } else { length };

		for (rate, data) in self.mipmapped_data.iter_mut() {
			let rate_dbl = rate.to_f64();

			let chop_length = time_to_samples(abs_length.to_f64(), rate_dbl, self.channels);
			if chop_length == 0 {
				continue;
			}

			if !negative {
				let drop = chop_length.min(data.len());
				data.drain(..drop);
			} else {
				let mut padded = vec![SamplePerChannel::default(); chop_length];
				padded.extend_from_slice(data);
				*data = padded;
			}
		}

		if !negative {
			self.length = Rational::new(0, 1).max(self.length - abs_length);
		}
		// Prepending grows the data before the existing start, so the absolute
		// end (which length_ tracks) is unchanged
	}

	/// Take a sub-waveform starting at `offset`.
	///
	/// `// CPP-PARITY: src/audio/src/audiovisualwaveform.cpp:252`
	/// (`AudioVisualWaveform::mid`).
	pub fn mid(&self, offset: Rational, length: Rational) -> AudioVisualWaveform {
		let mut mid = self.clone();
		mid.trim_range(offset - self.virtual_start, length);
		mid
	}

	/// Resize to `length`, truncating or padding with silence.
	///
	/// `// CPP-PARITY: src/audio/src/audiovisualwaveform.cpp:268`
	/// (`AudioVisualWaveform::resize`).
	pub fn resize(&mut self, length: Rational) {
		if self.length == length {
			return;
		}

		for (rate, data) in self.mipmapped_data.iter_mut() {
			let rate_dbl = rate.to_f64();
			let chop_length = time_to_samples(length.to_f64(), rate_dbl, self.channels);
			data.resize(chop_length, SamplePerChannel::default());
		}

		self.length = length;
	}

	/// Trim to a range starting at `in` with the given `length`.
	///
	/// `// CPP-PARITY: src/audio/src/audiovisualwaveform.cpp:286`
	/// (`AudioVisualWaveform::trim_range`).
	pub fn trim_range(&mut self, r#in: Rational, length: Rational) {
		self.trim_in(r#in);
		self.resize(length);
	}

	/// Pick the smallest mipmap whose rate covers `scale`.
	///
	/// `// CPP-PARITY: src/audio/src/audiovisualwaveform.cpp:365`
	/// (`AudioVisualWaveform::get_mipmap_for_scale`): falls back to the
	/// largest mipmap when none is sufficient.
	fn get_mipmap_for_scale(&self, scale: f64) -> (&Rational, &Sample) {
		for (rate, data) in self.mipmapped_data.iter() {
			if rate.to_f64() >= scale {
				return (rate, data);
			}
		}
		self.mipmapped_data.iter().next_back().unwrap()
	}

	/// Return summarized min/max pairs for a time range.
	///
	/// `// CPP-PARITY: src/audio/src/audiovisualwaveform.cpp:292`
	/// (`AudioVisualWaveform::get_summary_from_time`): a start past the end
	/// of the data returns zero pairs instead of underflowing (signed
	/// `available` comparison).
	pub fn get_summary_from_time(&self, start: Rational, length: Rational) -> Sample {
		// Find mipmap that requires
		let (rate, mipmap_data) = self.get_mipmap_for_scale(length.to_f64().recip_or_zero());

		let rate_dbl = rate.to_f64();

		let start_sample =
			time_to_samples((start - self.virtual_start).to_f64(), rate_dbl, self.channels);
		let mut sample_length = time_to_samples(length.to_f64(), rate_dbl, self.channels);

		// Determine if the array actually has this sample. Compare in signed
		// arithmetic so a start past the end of the data doesn't underflow.
		let available = mipmap_data.len() as i64 - start_sample as i64;
		if available > 0 {
			sample_length = sample_length.min(available as usize);

			if sample_length > 0 {
				return Self::re_sum_samples(
					&mipmap_data[start_sample..],
					sample_length,
					self.channels,
				);
			}
		}

		// Return null samples
		vec![
			SamplePerChannel::default();
			self.channel_count().max(0) as usize
		]
	}

	/// Reduce planar samples into min/max pairs.
	///
	/// `// CPP-PARITY: src/audio/src/audiovisualwaveform.cpp:329`
	/// (`AudioVisualWaveform::sum_samples`; scalar fallback of
	/// `expand_min_max_channel`, the SIMD path is numerically identical).
	pub fn sum_samples(planar: &[&[f32]], start_index: usize, length: usize) -> Sample {
		let mut summed = Vec::with_capacity(planar.len());
		for data in planar {
			let end = (start_index + length).min(data.len());
			let mut min_val: f32;
			let mut max_val: f32;
			if start_index < end {
				min_val = data[start_index];
				max_val = data[start_index];
				for &s in &data[start_index + 1..end] {
					if s < min_val {
						min_val = s;
					}
					if s > max_val {
						max_val = s;
					}
				}
			} else {
				min_val = 0.0;
				max_val = 0.0;
			}
			summed.push(SamplePerChannel {
				min: min_val,
				max: max_val,
			});
		}
		summed
	}

	/// Merge already-summed min/max pairs across channels.
	///
	/// `// CPP-PARITY: src/audio/src/audiovisualwaveform.cpp:353`
	/// (`AudioVisualWaveform::re_sum_samples`): initialized from the FIRST
	/// point rather than {0,0} — the engine version clamped all-positive
	/// (resp. all-negative) ranges to zero; fixed in oakaudio (see the
	/// comment in the C++ source).
	pub fn re_sum_samples(samples: &[SamplePerChannel], nb_samples: usize, nb_channels: i32) -> Sample {
		let channel_count = nb_channels.max(0) as usize;
		let mut summed = vec![SamplePerChannel::default(); channel_count];

		let nb_samples = nb_samples.min(samples.len());

		// Initialize from the first point instead of {0,0}
		if nb_samples >= channel_count {
			summed[..channel_count].copy_from_slice(&samples[..channel_count]);
		}

		let mut i = 0usize;
		while i < nb_samples {
			for j in 0..channel_count {
				if i + j >= samples.len() {
					break;
				}
				let sample = samples[i + j];

				if sample.min < summed[j].min {
					summed[j].min = sample.min;
				}
				if sample.max > summed[j].max {
					summed[j].max = sample.max;
				}
			}
			i += nb_channels.max(1) as usize;
		}

		summed
	}
}

impl Default for AudioVisualWaveform {
	fn default() -> AudioVisualWaveform {
		AudioVisualWaveform::new()
	}
}

/// f64 reciprocal that yields 0 for a zero denominator (0/0 or 0-length
/// rationals): C++ `length.flipped().to_double()` on a null rational is
/// NaN; NaN never satisfies `rate >= scale` so the largest mipmap is
/// picked. We mirror that by returning NaN for the null case.
trait RecipOrZero {
	fn recip_or_zero(self) -> f64;
}

impl RecipOrZero for f64 {
	fn recip_or_zero(self) -> f64 {
		if self == 0.0 || self.is_nan() {
			f64::NAN
		} else {
			1.0 / self
		}
	}
}

// ---- Handle plumbing (mirrors processor.rs) --------------------------------

/// Create an empty waveform behind a refcounted handle (count 1).
pub fn init() -> Result<CHandle> {
	Ok(make_owned(AudioVisualWaveform::new()))
}

/// Release one reference to a waveform (NULL/empty no-op).
pub fn free(self_: *mut CHandle) {
	unsafe { free_handle(self_) };
}

/// Borrow the waveform behind a handle; `OAKAUDIO_E_INVALID` for empty.
pub fn get(self_: &CHandle) -> Result<&AudioVisualWaveform> {
	// SAFETY: every non-empty handle returned by `init` boxes an
	// `AudioVisualWaveform`.
	unsafe { crate::handle::get::<AudioVisualWaveform>(self_) }.ok_or(Error::Invalid)
}

/// Mutable variant of [`get`].
pub fn get_mut(self_: &CHandle) -> Result<&mut AudioVisualWaveform> {
	// SAFETY: every non-empty handle returned by `init` boxes an
	// `AudioVisualWaveform`.
	unsafe { crate::handle::get_mut::<AudioVisualWaveform>(self_) }.ok_or(Error::Invalid)
}

// ---- Whole-file extraction --------------------------------------------------

/// Result of [`extract`]: channel-interleaved min/max pairs.
pub struct ExtractOutcome {
	/// `points * channels` channel-interleaved min/max pairs.
	pub points: Vec<SamplePerChannel>,
	/// Channel count of the decoded stream.
	pub channels: i32,
}

/// Append a pulled frame's per-channel planar f32 samples to `pending`.
fn append_pending(pending: &mut Vec<Vec<f32>>, frame: *mut Frame, channels: i32, nb: i32) {
	// SAFETY: `frame` is a live graph-output frame (`fltp`), `channels` was
	// validated against the stream info and `nb` comes from the same frame.
	if pending.is_empty() {
		pending.resize(channels.max(0) as usize, Vec::new());
	}
	for ch in 0..channels {
		let data = unsafe { fb_frame_get_data(frame, ch) } as *const f32;
		let slice = unsafe { std::slice::from_raw_parts(data, nb as usize) };
		pending[ch as usize].extend_from_slice(slice);
	}
}

/// Emit one channel-interleaved point per `samples_per_point` pending
/// samples; with `flush`, a trailing partial point is emitted too.
///
/// `// CPP-PARITY: src/audio/c_api/waveform.cpp:88` (`emit_points`).
fn emit_points(
	pending: &mut Vec<Vec<f32>>,
	channels: i32,
	samples_per_point: i32,
	points: &mut Vec<SamplePerChannel>,
	flush: bool,
) {
	if pending.is_empty() {
		return;
	}
	loop {
		let available = pending[0].len();
		if available == 0 || (!flush && available < samples_per_point as usize) {
			return;
		}
		let n = available.min(samples_per_point as usize);

		let point = points.len() / channels as usize;
		points.resize(points.len() + channels as usize, SamplePerChannel::default());
		for ch in 0..channels {
			let plane = &mut pending[ch as usize];
			let mut mn = plane[0];
			let mut mx = mn;
			for &v in &plane[1..n] {
				mn = mn.min(v);
				mx = mx.max(v);
			}
			points[point * channels as usize + ch as usize] = SamplePerChannel { min: mn, max: mx };
			plane.drain(..n);
		}
	}
}

/// Decode a whole audio stream to a channel-interleaved min/max summary.
///
/// The stream is probed through the oakcodec decoder C ABI and decoded via
/// ffmpeg_bridge (`fb_decoder` + `fb_audio_graph`), then reduced to one
/// point per `samples_per_point` source samples.
///
/// `// CPP-PARITY: src/audio/c_api/waveform.cpp:404`
/// (`oakaudio_waveform_extract`).
pub fn extract(filename: &CStr, stream_index: i32, samples_per_point: i32) -> Result<ExtractOutcome> {
	// Probe for the stream's native rate/layout (stateless).
	// SAFETY: `filename` is a NUL-terminated C string (validated by the FFI
	// layer); the probe handle is freed on every path below.
	let probe = unsafe { crate::bridge::codec::oakcodec_decoder_probe(filename.as_ptr()) };
	if probe.is_null() {
		return Err(Error::NotFound);
	}
	let mut info = unsafe { std::mem::zeroed::<AudioStreamInfo>() };
	let r = unsafe {
		crate::bridge::codec::oakcodec_decoder_probe_get_audio_stream(
			probe,
			stream_index,
			&mut info,
		)
	};
	// SAFETY: `probe` was created above and is no longer used.
	unsafe { crate::bridge::codec::oakcodec_decoder_free(probe) };
	if r != 0 {
		return Err(Error::NotFound);
	}
	if info.sample_rate <= 0 || info.channel_count <= 0 {
		return Err(Error::Failed("invalid audio stream".to_string()));
	}
	let channels = info.channel_count;
	if channels > EXTRACT_MAX_CHANNELS {
		return Err(Error::Failed(format!(
			"stream has {channels} channels (max {EXTRACT_MAX_CHANNELS})"
		)));
	}

	// Decode the whole stream through ffmpeg_bridge.
	let decoder = unsafe { fb_decoder_create() };
	if decoder.is_null() {
		return Err(Error::NoMem);
	}
	// SAFETY: `decoder` is live until `fb_decoder_free` below; every early
	// return releases it first.
	let open_r = unsafe { fb_decoder_open(decoder, filename.as_ptr(), info.stream_index) };
	if open_r < 0 {
		// SAFETY: `decoder` is a live ffmpeg_bridge decoder.
		unsafe { fb_decoder_free(&mut (decoder as *mut Decoder)) };
		return Err(Error::Failed(format!("failed to open decoder: {open_r}")));
	}

	let mut sinfo = unsafe { std::mem::zeroed::<crate::bridge::ffmpeg::FBStreamInfo>() };
	if unsafe { fb_decoder_get_stream_info(decoder, &mut sinfo) } < 0 || sinfo.sample_rate <= 0 {
		// SAFETY: see above.
		unsafe {
			fb_decoder_close(decoder);
			fb_decoder_free(&mut (decoder as *mut Decoder));
		}
		return Err(Error::Failed(
			"failed to query decoder stream info".to_string(),
		));
	}

	let config = AudioGraphConfig {
		in_sample_rate: sinfo.sample_rate,
		in_channel_layout_mask: sinfo.channel_layout_mask,
		in_sample_format: sinfo.sample_format,
		in_channels: channels,
		out_sample_rate: sinfo.sample_rate,
		out_channel_layout_mask: sinfo.channel_layout_mask,
		out_sample_format: SampleFormat::Fltp as c_int,
		out_channels: channels,
		out_is_planar: 1,
		tempo: 1.0,
	};

	let mut packet = unsafe { fb_packet_alloc() };
	let mut frame = unsafe { fb_frame_alloc() };
	let mut converted = unsafe { fb_frame_alloc() };
	let graph = unsafe { fb_audio_graph_create(&config) };
	if packet.is_null() || frame.is_null() || converted.is_null() {
		cleanup_extract(graph, &mut converted, &mut frame, &mut packet, decoder);
		return Err(Error::NoMem);
	}
	if graph.is_null() {
		cleanup_extract(graph, &mut converted, &mut frame, &mut packet, decoder);
		return Err(Error::Failed(
			"failed to create audio filter graph".to_string(),
		));
	}

	let mut pending: Vec<Vec<f32>> = Vec::new();
	let mut points: Vec<SamplePerChannel> = Vec::new();

	// SAFETY: all handles are live; `frame` holds the decoded frame and
	// `converted` the graph output.
	let mut result: Result<()> = Ok(());
	'decode: loop {
		let r = unsafe { fb_decoder_get_frame(decoder, packet, frame) };
		if r < 0 {
			break; // EOF or error: stop decoding (C++ breaks on < 0)
		}

		// Push the decoded frame (planar pointer array; a packed source is
		// read from plane 0 by the buffersrc).
		let nb = unsafe { fb_frame_get_nb_samples(frame) };
		let mut planes: Vec<*const u8> = Vec::with_capacity(channels as usize);
		for ch in 0..channels {
			// SAFETY: `frame` carries at least `channels` planes for the
			// decoded format (validated stream info).
			planes.push(unsafe { fb_frame_get_data(frame, ch) });
		}
		if unsafe { fb_audio_graph_push(graph, planes.as_ptr(), nb) } < 0 {
			result = Err(Error::Failed("failed to push decoded frame".to_string()));
			break 'decode;
		}

		// Drain the graph: pull converted output until no more is available.
		loop {
			let pull = unsafe { fb_audio_graph_pull(graph, converted) };
			if pull < 0 {
				result = Err(Error::Failed("failed to pull from graph".to_string()));
				break 'decode;
			}
			if pull == 0 {
				break;
			}
			let nb = unsafe { fb_frame_get_nb_samples(converted) };
			append_pending(&mut pending, converted, channels, nb);
			emit_points(&mut pending, channels, samples_per_point, &mut points, false);
		}
	}

	// Flush the resampler delay (identity in the extract path, so this only
	// emits the trailing partial window).
	if result.is_ok() {
		// SAFETY: `graph` is live; NULL channel data signals EOF.
		unsafe { fb_audio_graph_push(graph, std::ptr::null(), 0) };
		loop {
			let pull = unsafe { fb_audio_graph_pull(graph, converted) };
			if pull <= 0 {
				break;
			}
			let nb = unsafe { fb_frame_get_nb_samples(converted) };
			append_pending(&mut pending, converted, channels, nb);
		}
		emit_points(&mut pending, channels, samples_per_point, &mut points, true);
	}

	cleanup_extract(graph, &mut converted, &mut frame, &mut packet, decoder);
	result?;

	Ok(ExtractOutcome { points, channels })
}

/// Free every resource allocated by [`extract`] after the graph/decoder
/// creation succeeded.
fn cleanup_extract(
	graph: *mut AudioGraph,
	converted: &mut *mut Frame,
	frame: &mut *mut Frame,
	packet: &mut *mut Packet,
	decoder: *mut Decoder,
) {
	// SAFETY: the pointers were produced by the corresponding ffmpeg_bridge
	// allocators and are freed exactly once here.
	unsafe {
		if !graph.is_null() {
			fb_audio_graph_free(&mut (graph as *mut AudioGraph));
		}
		if !converted.is_null() {
			fb_frame_free(converted);
		}
		if !frame.is_null() {
			fb_frame_free(frame);
		}
		if !packet.is_null() {
			fb_packet_free(packet);
		}
		fb_decoder_close(decoder);
		fb_decoder_free(&mut (decoder as *mut Decoder));
	}
}
