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

//! The process-wide PortAudio output/input manager (`olive::AudioManager`).
//!
//! Singleton semantics: the single instance lives behind a
//! `OnceLock<Mutex<ManagerInner>>`; handles returned to C are borrowed and
//! their addref/release are no-ops (mirrors the C++ singleton and
//! `include/audio/manager.h`). An empty handle reports `OAKAUDIO_E_STATE`.
//!
//! Recording goes through the oakcodec encoder C ABI ([`crate::bridge`]);
//! device/config lookups go through oakcommon.

use std::ffi::c_void;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Mutex, MutexGuard, OnceLock};

use crate::bridge::codec::EncodingParams;
use crate::error::{Error, Result};
use crate::handle::{make_borrowed, CHandle};
use crate::params::AudioParams;
use crate::previewdevice::PreviewAudioDevice;

/// `paNoDevice` (PortAudio "no device" sentinel; also the default when no
/// device is configured).
const PA_NO_DEVICE: i32 = -1;

/// The process-wide manager state. `OnceLock` cannot be reset, so
/// [`destroy_instance`] flips `DESTROYED` to make [`instance`] hand out empty
/// handles again (the singleton box itself is retained).
static MANAGER: OnceLock<Mutex<ManagerInner>> = OnceLock::new();
static DESTROYED: AtomicBool = AtomicBool::new(false);

/// Manager state (all device/stream fields; PortAudio itself is not bridged,
/// see [`ManagerInner::default`] for the degradations).
struct ManagerInner {
	/// Current output device index.
	output_device: i32,
	/// Current input device index.
	input_device: i32,
	/// Output params the buffer is configured for.
	output_params: Option<AudioParams>,
	/// Queued output samples feeding the (virtual) playback clock.
	output_buffer: PreviewAudioDevice,
	/// Whether the output "stream" is running (stand-in for
	/// `Pa_IsStreamActive`).
	output_started: bool,
	/// Active oakcodec recording encoder (NULL when idle).
	recording: Option<CHandle>,
}

// SAFETY: the raw encoder pointer is only touched while the manager mutex is
// held, which serializes every access; the encoder lives until `recording` is
// taken out in `stop_recording`.
unsafe impl Send for ManagerInner {}

impl Default for ManagerInner {
	fn default() -> Self {
		ManagerInner {
			// CPP-PARITY: no device is selected until `create_instance` runs
			// the config lookup (PortAudio enumeration cannot be bridged, so
			// the devices stay at paNoDevice and the C layer reports E_FAILED
			// on output/recording until a device is set explicitly).
			output_device: PA_NO_DEVICE,
			input_device: PA_NO_DEVICE,
			output_params: None,
			output_buffer: PreviewAudioDevice::new(),
			output_started: false,
			recording: None,
		}
	}
}

/// Lock the manager behind a handle; `OAKAUDIO_E_STATE` for empty handles.
fn with_instance(h: &CHandle) -> Result<MutexGuard<'static, ManagerInner>> {
	if h.is_null() {
		return Err(Error::State);
	}
	// SAFETY: `instance()` only creates borrowed handles whose ctx points at
	// the MANAGER Mutex, which lives in a static for the whole process.
	let m: &'static Mutex<ManagerInner> = unsafe { &*(h.ctx as *const Mutex<ManagerInner>) };
	Ok(m.lock().unwrap_or_else(|p| p.into_inner()))
}

/// Create the process-wide AudioManager (no-op when it exists). Returns
/// `OAKAUDIO_OK` or `OAKAUDIO_E_NOMEM`.
///
/// `// CPP-PARITY: src/audio/c_api/manager.cpp:73` (C++ allocates with `new`
/// and reports `OAKAUDIO_E_NOMEM` on exception; Rust allocation infallibly
/// panics, so the error code is never produced).
pub fn create_instance() -> Result<()> {
	DESTROYED.store(false, Ordering::SeqCst);
	let _ = MANAGER.get_or_init(|| Mutex::new(ManagerInner::default()));
	Ok(())
}

/// Destroy the process-wide AudioManager (no-op when absent).
///
/// `// CPP-PARITY: src/audio/c_api/manager.cpp:85` — the C++ singleton is
/// deleted and re-creatable; `OnceLock` cannot be reset, so a `DESTROYED`
/// flag makes [`instance`] return an empty handle (and a later
/// [`create_instance`] resurrects the existing box).
pub fn destroy_instance() {
	DESTROYED.store(true, Ordering::SeqCst);
}

/// Return a handle to the process-wide AudioManager (borrowed; empty when
/// no instance exists).
///
/// `// CPP-PARITY: src/audio/c_api/manager.cpp:90` (`wrap`; the handle is a
/// borrowed singleton whose addref/release are no-ops).
pub fn instance() -> CHandle {
	if DESTROYED.load(Ordering::SeqCst) {
		return CHandle::null();
	}
	match MANAGER.get() {
		Some(m) => {
			// SAFETY: `m` is the process-wide singleton; borrowed handles do
			// not free it, so it outlives every handle.
			unsafe { make_borrowed(m as *const _ as *mut Mutex<ManagerInner>) }
		}
		None => CHandle::null(),
	}
}

/// Release a manager handle. No-op (singleton), safe on NULL/empty.
///
/// `// CPP-PARITY: src/audio/c_api/manager.cpp:95` — releasing never
/// destroys; just clear the caller's copy.
pub fn free(self_: *mut CHandle) {
	unsafe {
		if let Some(h) = self_.as_mut() {
			h.ctx = std::ptr::null_mut();
		}
	}
}

/// Bytes between output-notify pulses (0 disables).
pub fn set_output_notify_interval(self_: &CHandle, bytes: i64) -> Result<()> {
	if bytes < 0 {
		return Err(Error::Invalid);
	}
	let mut m = with_instance(self_)?;
	m.output_buffer.set_notify_interval(bytes);
	Ok(())
}

/// Push a block of samples to the output device, opening/restarting the
/// stream when the params changed.
///
/// `// CPP-PARITY: src/audio/src/audiomanager.cpp:111` — the PortAudio
/// open/start path is not bridged; the buffer is configured and written
/// directly and the "stream" is marked running. `error_buf` is written by
/// the FFI layer from the returned error.
pub fn push_to_output(
	self_: &CHandle,
	params: AudioParams,
	samples: &[u8],
	_error_buf: &mut [u8],
) -> Result<()> {
	let mut m = with_instance(self_)?;
	if m.output_device == PA_NO_DEVICE {
		return Err(Error::Failed("No output device is set".to_string()));
	}
	if m.output_params.as_ref() != Some(&params) {
		m.output_params = Some(params);
		m.output_buffer.set_params(params);
	}
	m.output_buffer.write(samples);
	m.output_started = true;
	Ok(())
}

/// Discard buffered output.
pub fn clear_buffered_output(self_: &CHandle) -> Result<()> {
	let mut m = with_instance(self_)?;
	m.output_buffer.clear();
	Ok(())
}

/// Stop the output stream.
///
/// `// CPP-PARITY: src/audio/src/audiomanager.cpp:229` (`stop_output` aborts
/// the stream and clears the buffer).
pub fn stop_output(self_: &CHandle) -> Result<()> {
	let mut m = with_instance(self_)?;
	m.output_started = false;
	m.output_buffer.clear();
	Ok(())
}

/// Seconds of audio consumed by the output device since the last reset,
/// compensated for output latency; negative when no stream is running.
///
/// `// CPP-PARITY: src/audio/src/audiomanager.cpp:169` — PortAudio's
/// `outputLatency` is not representable without a live stream, so the buffer
/// clock is used directly (the `max(0, ...)` clamp is kept).
pub fn seconds(self_: &CHandle, out: &mut f64) -> Result<()> {
	let m = with_instance(self_)?;
	if !m.output_started {
		*out = -1.0;
		return Ok(());
	}
	let rate = m.output_params.map(|p| p.sample_rate).unwrap_or(0);
	if rate <= 0 {
		*out = -1.0;
		return Ok(());
	}
	let secs = m.output_buffer.output_frames_consumed() as f64 / f64::from(rate);
	*out = secs.max(0.0);
	Ok(())
}

/// Restart the output clock at zero.
pub fn reset_output_clock(self_: &CHandle) -> Result<()> {
	let m = with_instance(self_)?;
	m.output_buffer.reset_output_frames();
	Ok(())
}

/// Current output device index (`paNoDevice` = -1) or a negative error code.
pub fn get_output_device(self_: &CHandle) -> Result<i32> {
	let m = with_instance(self_)?;
	Ok(m.output_device)
}

/// Set the output device index.
///
/// `// CPP-PARITY: src/audio/src/audiomanager.cpp:238` (the device is
/// recorded and the stream closed; PortAudio's index validation and name
/// logging are not bridged).
pub fn set_output_device(self_: &CHandle, device: i32) -> Result<()> {
	let mut m = with_instance(self_)?;
	m.output_device = device;
	m.output_started = false;
	m.output_buffer.clear();
	Ok(())
}

/// Current input device index or a negative error code.
pub fn get_input_device(self_: &CHandle) -> Result<i32> {
	let m = with_instance(self_)?;
	Ok(m.input_device)
}

/// Set the input device index.
pub fn set_input_device(self_: &CHandle, device: i32) -> Result<()> {
	let mut m = with_instance(self_)?;
	m.input_device = device;
	Ok(())
}

/// Close the output stream and re-initialize PortAudio.
///
/// `// CPP-PARITY: src/audio/src/audiomanager.cpp:271` (PortAudio terminate/
/// initialize is not bridged; the output side is reset).
pub fn hard_reset(self_: &CHandle) -> Result<()> {
	let mut m = with_instance(self_)?;
	m.output_started = false;
	m.output_buffer.clear();
	Ok(())
}

/// Start recording the input device to a file via the oakcodec encoder C
/// ABI. The input stream is always captured as interleaved f32.
///
/// `// CPP-PARITY: src/audio/src/audiomanager.cpp:278` (encoder init/open;
/// the PortAudio input stream is not bridged). On failure the encoder's
/// last-error string is surfaced when available.
pub fn start_recording(
	self_: &CHandle,
	params: &EncodingParams,
	_error_buf: &mut [u8],
) -> Result<()> {
	let mut m = with_instance(self_)?;
	if m.input_device == PA_NO_DEVICE {
		return Err(Error::Failed("no input device".to_string()));
	}
	eprintln!(
		"MANAGER before encoder_init: audio_enabled={} codec={}",
		params.audio_enabled, params.audio_codec
	);
	let mut enc = unsafe { crate::bridge::codec::oakcodec_encoder_init(params) };
	eprintln!(
		"MANAGER encoder_init null? {} ptr={:p} size={}",
		enc.is_null(),
		params as *const EncodingParams,
		std::mem::size_of::<EncodingParams>()
	);
	let direct =
		unsafe { oakcodec::ffi::encoder::oakcodec_encoder_init(params as *const EncodingParams) };
	eprintln!("MANAGER direct init null? {}", direct.is_null());
	if !direct.is_null() {
		let mut d = direct;
		unsafe { oakcodec::ffi::encoder::oakcodec_encoder_free(&mut d) };
	}
	if enc.is_null() {
		return Err(Error::Failed(
			"failed to open encoder for recording".to_string(),
		));
	}
	let open_r = unsafe { crate::bridge::codec::oakcodec_encoder_open(enc) };
	if open_r != 0 {
		let mut buf = [0i8; 512];
		let n = unsafe {
			crate::bridge::codec::oakcodec_encoder_last_error(
				enc,
				buf.as_mut_ptr(),
				buf.len() as i32,
			)
		};
		let msg = if n > 0 {
			let s = buf.split(|c| *c == 0).next().unwrap_or(&[]);
			let bytes: Vec<u8> = s.iter().map(|&b| b as u8).collect();
			String::from_utf8_lossy(&bytes).into_owned()
		} else {
			"failed to open encoder for recording".to_string()
		};
		unsafe { crate::bridge::codec::oakcodec_encoder_free(&mut enc) };
		return Err(Error::Failed(msg));
	}
	m.recording = Some(enc);
	Ok(())
}

/// Stop recording.
///
/// `// CPP-PARITY: src/audio/src/audiomanager.cpp:328` (the PortAudio input
/// stream is not bridged; the encoder is flushed and freed).
pub fn stop_recording(self_: &CHandle) -> Result<()> {
	let mut m = with_instance(self_)?;
	if let Some(mut enc) = m.recording.take() {
		unsafe {
			crate::bridge::codec::oakcodec_encoder_flush(enc);
			crate::bridge::codec::oakcodec_encoder_free(&mut enc);
		}
	}
	Ok(())
}

/// Device index named by the configuration ("AudioOutput"/"AudioInput"), or
/// the default when unset/unmatched. Static; `paNoDevice` when PortAudio is
/// not initialized.
///
/// `// CPP-PARITY: src/audio/src/audiomanager.cpp:404`.
pub fn find_config_device_by_name_s(is_output_device: bool) -> i32 {
	let name = crate::config::device_name(is_output_device);
	find_device_by_name_s(&name, is_output_device)
}

/// Device index whose name matches `name` exactly (empty matches nothing,
/// falls through to the default device). Static.
///
/// `// CPP-PARITY: src/audio/src/audiomanager.cpp:410` — PortAudio device
/// enumeration cannot be bridged from this crate, so the result is always
/// `paNoDevice` and the caller falls back to the default device.
pub fn find_device_by_name_s(name: &std::ffi::CStr, _is_output_device: bool) -> i32 {
	let _ = name;
	PA_NO_DEVICE
}

/// Number of live oakaudio reference-counted objects (leak check).
pub fn debug_alive_count() -> i32 {
	crate::handle::alive_count()
}
