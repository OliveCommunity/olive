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

//! M12 P1: the real audio output device (PortAudio, direct crate call).
//!
//! The playback stream's callback pulls interleaved bytes from the
//! shared [`PreviewAudioDevice`] and advances the output clock; underrun
//! (empty buffer) writes silence. The stream is opened lazily on the
//! first pushed samples and re-opened when the format or device changes.
//!
//! The callback never allocates or locks anything but the preview
//! device's mutex (a short critical section; the buffer is drained in
//! whole frames).

use std::sync::Arc;

use portaudio::{
	DeviceIndex, OutputStreamCallbackArgs, OutputStreamSettings, StreamParameters, Stream,
};

use crate::previewdevice::PreviewAudioDevice;

/// The default PortAudio frames-per-buffer for the preview stream.
const FRAMES_PER_BUFFER: u32 = 512;

/// An open (or openable) PortAudio output stream.
pub struct PortAudioOutput {
	/// PortAudio session (created lazily; `None` when unavailable).
	pa: Option<portaudio::PortAudio>,
	/// The live stream (None when closed).
	stream: Option<Stream<portaudio::NonBlocking, portaudio::Output<f32>>>,
	/// The (device, rate, channels) the stream was opened with.
	opened: Option<(i32, i32, i32)>,
}

impl PortAudioOutput {
	/// Create the device (PortAudio initialized lazily on first use).
	pub fn new() -> PortAudioOutput {
		PortAudioOutput {
			pa: None,
			stream: None,
			opened: None,
		}
	}

	/// Whether an output stream is currently running.
	pub fn is_running(&self) -> bool {
		self.stream.is_some()
	}

	/// Close the stream (stop + drop).
	pub fn close(&mut self) {
		if let Some(mut s) = self.stream.take() {
			let _ = s.stop();
		}
		self.opened = None;
	}

	/// Ensure an output stream is open for `(device, rate, channels)` and
	/// pulling from `sink`. `device` < 0 selects the system default.
	/// Failures leave the output silent (samples still buffer).
	pub fn ensure_open(
		&mut self,
		device: i32,
		rate: i32,
		channels: i32,
		sink: Arc<PreviewAudioDevice>,
	) -> Result<(), String> {
		if rate <= 0 || channels <= 0 {
			return Err("invalid output format".into());
		}
		if self.opened == Some((device, rate, channels)) && self.stream.is_some() {
			return Ok(());
		}
		self.close();

		let pa = match &self.pa {
			Some(pa) => pa.clone(),
			None => {
				let pa = portaudio::PortAudio::new()
					.map_err(|e| format!("PortAudio init failed: {e:?}"))?;
				self.pa = Some(pa);
				match self.pa.as_ref() {
					Some(pa) => pa.clone(),
					None => unreachable!("just assigned"),
				}
			}
		};

		let dev = if device >= 0 {
			DeviceIndex(device as u32)
		} else {
			pa.default_output_device()
				.map_err(|e| format!("no default output device: {e:?}"))?
		};
		let info = pa
			.device_info(dev)
			.map_err(|e| format!("device info failed: {e:?}"))?;
		let latency = info.default_low_output_latency;
		let params = StreamParameters::<f32>::new(dev, channels, true, latency);
		let settings = OutputStreamSettings::<f32>::new(params, rate as f64, FRAMES_PER_BUFFER);

		// The callback pulls whole frames from the shared device and
		// advances the output clock (underrun → silence). `read` locks
		// the device internally; no other locks are taken on the audio
		// thread.
		let sink_cb = sink.clone();
		let callback = move |args: OutputStreamCallbackArgs<f32>| {
			let out = args.buffer;
			let frames = args.frames;
			let total = frames * channels.max(1) as usize;
			let mut byte_buf = vec![0u8; total * 4];
			let got = sink_cb.read(&mut byte_buf);
			let frames_got = (got as usize) / (channels.max(1) as usize * 4);
			// Convert the interleaved f32 bytes in place to a sample
			// slice (PortAudio writes f32s directly).
			let n_samples = frames_got * channels.max(1) as usize;
			for i in 0..n_samples {
				let b = &byte_buf[i * 4..i * 4 + 4];
				out[i] = f32::from_le_bytes([b[0], b[1], b[2], b[3]]);
			}
			for s in out.iter_mut().skip(n_samples) {
				*s = 0.0; // underrun: silence
			}
			sink_cb.add_output_frames(frames as i64);
			portaudio::Continue
		};

		let mut stream = pa
			.open_non_blocking_stream(settings, callback)
			.map_err(|e| format!("output stream open failed: {e:?}"))?;
		stream
			.start()
			.map_err(|e| format!("output stream start failed: {e:?}"))?;
		self.stream = Some(stream);
		self.opened = Some((device, rate, channels));
		Ok(())
	}
}
