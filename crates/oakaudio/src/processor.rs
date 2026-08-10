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

//! The real-time resampler/format converter (`olive::AudioProcessor`).
//!
//! Wraps the ffmpeg_bridge audio filter graph (`fb_audio_graph_*`,
//! `fb_frame_*`) via [`crate::bridge::ffmpeg`]. The conversion output is
//! always planar 32-bit float
//! (`OAKAUDIO_PROCESSOR_OUTPUT_FORMAT == SampleFormat::F32Planar == 4`).

use std::ffi::c_int;
use std::ptr;
use std::sync::Mutex;

use crate::bridge::common::oakcommon_ffmpegutils_get_ffmpeg_sample_format;
use crate::bridge::ffmpeg::{
	fb_audio_graph_create, fb_audio_graph_free, fb_audio_graph_pull,
	fb_audio_graph_push, fb_channel_layout_default, fb_frame_alloc,
	fb_frame_free, fb_frame_get_data, fb_frame_get_nb_samples, AudioGraph,
	AudioGraphConfig, Frame,
};
use crate::error::{Error, Result};
use crate::handle::{free_handle, make_owned, CHandle};
use crate::params::{AudioParams, SampleFormat};

/// A closed audio processor (reference count 1; `ctx == NULL` on allocation
/// failure).
pub struct Processor {
	inner: Mutex<ProcessorInner>,
}

/// Resampler state behind the handle's mutex.
struct ProcessorInner {
	/// Live filter graph (`null` = closed).
	graph: *mut AudioGraph,
	/// Scratch output frame reused for every pull.
	out_frame: *mut Frame,
	/// Input spec recorded at `open`.
	from: AudioParams,
	/// Output spec recorded at `open`.
	to: AudioParams,
}

// SAFETY: the raw C pointers are only dereferenced through the ffmpeg_bridge
// ABI while the mutex is held, so all access is serialized; the handle's
// refcount keeps the box alive.
unsafe impl Send for ProcessorInner {}

impl Default for ProcessorInner {
	fn default() -> Self {
		ProcessorInner {
			graph: ptr::null_mut(),
			out_frame: ptr::null_mut(),
			from: AudioParams {
				sample_rate: 0,
				channel_layout: 0,
				format: SampleFormat::Invalid,
			},
			to: AudioParams {
				sample_rate: 0,
				channel_layout: 0,
				format: SampleFormat::Invalid,
			},
		}
	}
}

/// `// CPP-PARITY: src/audio/src/audioprocessor.cpp:35` — map a native
/// sample format to the bridge format via the oakcommon C ABI (`out` is
/// initialized to `-1` = none; identity in the test stub).
fn to_bridge_sample_format(fmt: SampleFormat) -> c_int {
	let mut out: c_int = -1;
	unsafe {
		oakcommon_ffmpegutils_get_ffmpeg_sample_format(fmt as i32, &mut out);
	}
	out
}

/// `// CPP-PARITY: src/audio/src/audioprocessor.cpp:50` — ensure a usable
/// channel layout mask: 0 (unknown) falls back to a default layout derived
/// from the channel count, itself defaulting to stereo.
fn fix_channel_layout(params: AudioParams) -> AudioParams {
	let mut result = params;
	if params.channel_layout == 0 {
		let mut channels = params.channel_count();
		if channels <= 0 {
			channels = 2;
		}
		result.channel_layout = unsafe { fb_channel_layout_default(channels) };
	}
	result
}

/// Borrow the processor state behind a handle.
fn get_processor(self_: &CHandle) -> Result<&Processor> {
	// SAFETY: every non-empty handle returned by `init` boxes a `Processor`.
	unsafe { crate::handle::get::<Processor>(self_) }.ok_or(Error::Invalid)
}

/// Create a closed processor.
pub fn init() -> Result<CHandle> {
	Ok(make_owned(Processor {
		inner: Mutex::new(ProcessorInner::default()),
	}))
}

/// Release one reference to a processor (NULL/empty no-op).
pub fn free(self_: *mut CHandle) {
	unsafe { free_handle(self_) };
}

/// Open the resampling/format-conversion graph. `out_format` is accepted for
/// interface completeness but the conversion output is always planar f32.
///
/// `// CPP-PARITY: src/audio/c_api/processor.cpp:43` (validation order:
/// empty handle, already-open state, invalid rates/speed, forced output
/// format) and `src/audio/src/audioprocessor.cpp:82` (graph creation).
pub fn open(
	self_: &CHandle,
	from: AudioParams,
	to: AudioParams,
	speed: f64,
) -> Result<()> {
	let p = get_processor(self_)?;
	let mut inner = p.inner.lock().unwrap();

	if !inner.graph.is_null() {
		// C++: "tried to open a processor that was already open"
		return Err(Error::State);
	}
	if from.sample_rate <= 0 || to.sample_rate <= 0 || speed <= 0.0 {
		return Err(Error::Invalid);
	}
	// The C ABI delivers planar float output only; force the output format
	// stage to f32p (OAKAUDIO_PROCESSOR_OUTPUT_FORMAT == 4).
	if to.format != SampleFormat::F32Planar {
		return Err(Error::Invalid);
	}

	let from_fixed = fix_channel_layout(from);
	let to_fixed = fix_channel_layout(to);

	let config = AudioGraphConfig {
		in_sample_rate: from_fixed.sample_rate,
		in_channel_layout_mask: from_fixed.channel_layout,
		in_sample_format: to_bridge_sample_format(from_fixed.format),
		in_channels: from_fixed.channel_count(),
		out_sample_rate: to_fixed.sample_rate,
		out_channel_layout_mask: to_fixed.channel_layout,
		out_sample_format: to_bridge_sample_format(to_fixed.format),
		out_channels: to_fixed.channel_count(),
		out_is_planar: if to_fixed.format.is_planar() { 1 } else { 0 },
		tempo: speed,
	};

	let graph = unsafe { fb_audio_graph_create(&config) };
	if graph.is_null() {
		// C++: "failed to create audio filter graph"
		return Err(Error::Failed("failed to create audio graph".to_string()));
	}
	inner.graph = graph;

	let out_frame = unsafe { fb_frame_alloc() };
	if out_frame.is_null() {
		// C++: "failed to allocate output frame"; close() unwinds the graph.
		unsafe { fb_audio_graph_free(&mut inner.graph) };
		return Err(Error::Failed(
			"failed to allocate output frame".to_string(),
		));
	}
	inner.out_frame = out_frame;

	inner.from = from_fixed;
	inner.to = to_fixed;
	Ok(())
}

/// Close the graph (safe when closed; handle must be non-empty).
pub fn close(self_: &CHandle) -> Result<()> {
	let p = get_processor(self_)?;
	let mut inner = p.inner.lock().unwrap();

	if !inner.graph.is_null() {
		unsafe { fb_audio_graph_free(&mut inner.graph) };
	}
	if !inner.out_frame.is_null() {
		unsafe { fb_frame_free(&mut inner.out_frame) };
	}
	Ok(())
}

/// 1 when open, 0 when closed; error for an empty handle.
pub fn is_open(self_: &CHandle) -> Result<bool> {
	let p = get_processor(self_)?;
	let inner = p.inner.lock().unwrap();
	Ok(!inner.graph.is_null())
}

/// Push planar float input and pull converted output. Returns the number of
/// output frames written.
///
/// `// CPP-PARITY: src/audio/c_api/processor.cpp:91` (validation, state
/// check, null `out_planar` short-circuit) and
/// `src/audio/src/audioprocessor.cpp:141` (push/pull loop, byte counting).
pub fn convert(
	self_: &CHandle,
	in_planar: *const *const f32,
	in_frame_count: i32,
	out_planar: *const *mut f32,
	out_capacity_frames: i32,
) -> Result<i32> {
	let p = get_processor(self_)?;
	let inner = p.inner.lock().unwrap();

	if inner.graph.is_null() {
		return Err(Error::State);
	}
	if in_frame_count < 0
		|| out_capacity_frames < 0
		|| (in_frame_count > 0 && in_planar.is_null())
	{
		return Err(Error::Invalid);
	}

	let channels = inner.to.channel_count();
	if channels <= 0 {
		return Err(Error::State);
	}

	if in_frame_count > 0 {
		// The FFI layer has no way to know the input plane count, so the
		// plane pointer array is walked using the input spec recorded at
		// `open` (`// CPP-PARITY: src/audio/src/audioprocessor.cpp:141`).
		let in_channels = inner.from.channel_count();
		let mut planes: Vec<*const u8> =
			Vec::with_capacity(in_channels.max(0) as usize);
		for ch in 0..in_channels {
			// SAFETY: `in_planar` is non-null here and the FFI contract
			// guarantees at least `from.channel_count()` entries.
			let p = unsafe { *in_planar.add(ch as usize) };
			planes.push(p as *const u8);
		}
		let r =
			unsafe { fb_audio_graph_push(inner.graph, planes.as_ptr(), in_frame_count) };
		if r < 0 {
			return Err(Error::Failed(format!(
				"failed to add frame to buffersrc: {r}"
			)));
		}
	}

	// C++: `out_planar ? &buf : nullptr` — with no destination, the input is
	// pushed but nothing is pulled.
	if out_planar.is_null() {
		return Ok(0);
	}

	let mut total: i64 = 0;
	loop {
		let r = unsafe { fb_audio_graph_pull(inner.graph, inner.out_frame) };
		if r <= 0 {
			if r < 0 {
				return Err(Error::Failed(format!(
					"failed to pull from buffersink: {r}"
				)));
			}
			break;
		}

		let nb = unsafe { fb_frame_get_nb_samples(inner.out_frame) };
		if nb > 0 && total < i64::from(out_capacity_frames) {
			let to_copy =
				(i64::from(out_capacity_frames) - total).min(i64::from(nb)) as i32;
			for ch in 0..channels {
				// SAFETY: the FFI contract guarantees at least `channels`
				// entries in `out_planar` (NULL entries are skipped).
				let dst = unsafe { *out_planar.add(ch as usize) };
				if dst.is_null() {
					continue;
				}
				// Output is planar f32 (enforced by open()); each plane is
				// `to_copy` float samples.
				let src = unsafe { fb_frame_get_data(inner.out_frame, ch) };
				unsafe {
					ptr::copy_nonoverlapping(
						src as *const u8,
						dst as *mut u8,
						(to_copy as usize) * 4,
					);
				}
			}
		}
		total += i64::from(nb);
	}

	Ok(total.min(i64::from(out_capacity_frames)) as i32)
}

/// Signal end-of-input to the graph (flushes internal delay).
///
/// `// CPP-PARITY: src/audio/c_api/processor.cpp:137` (empty handle, state)
/// and `src/audio/src/audioprocessor.cpp:210` (flush has no failure path; a
/// negative push return is logged only).
pub fn flush(self_: &CHandle) -> Result<()> {
	let p = get_processor(self_)?;
	let inner = p.inner.lock().unwrap();

	if inner.graph.is_null() {
		return Err(Error::State);
	}
	unsafe {
		fb_audio_graph_push(inner.graph, ptr::null(), 0);
	}
	Ok(())
}

/// Format of the conversion output (always planar f32).
pub const OUTPUT_FORMAT: SampleFormat = SampleFormat::F32Planar;
