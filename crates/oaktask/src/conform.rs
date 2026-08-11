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

//! `ConformTask`, mirroring `src/task/src/conform/conform.h`.
//!
//! Transcodes an audio stream to a PCM cache file (oakcodec kind
//! `OAKCODEC_TASK_CONFORM`) and reports progress as it decodes.
//!
//! CPP-PARITY: src/task/src/conform/conform.h

use crate::bridge;
use crate::error::{Error, Result};
use crate::ffi::taskhandle::cstr;
use crate::handle::CHandle;
use crate::task::{Task, TaskBehavior};

/// A conform task: copies/channels of one audio stream into per-channel
/// working PCM cache files, then renames them to the final names.
pub struct ConformTask {
	/// The shared task base.
	pub base: Task,
	/// Absolute input media filename.
	pub input_filename: String,
	/// Final destination path (replaces the source path extension).
	pub output_filename: String,
	/// Audio stream index inside the source media.
	pub stream_index: i32,
	/// Target sample rate (Hz).
	pub sample_rate: i32,
	/// Target channel layout mask (ffmpeg-style).
	pub channel_layout: u64,
	/// Target sample format (enum as int; `oakcore_rs::SampleFormat`).
	pub sample_format: i32,
	/// Resolved final per-channel filenames (from [`ConformTask::derive_filenames`]).
	final_names: Vec<String>,
	/// Resolved working per-channel filenames.
	working_names: Vec<String>,
}

impl ConformTask {
	/// Build a conform task from an oakcodec request, mirroring the C++
	/// constructor.
	pub fn new(request: &bridge::codec::OakCodecTaskRequest) -> ConformTask {
		let input = unsafe { crate::ffi::taskhandle::cstr_to_string(request.input_filename) };
		let output = unsafe { crate::ffi::taskhandle::cstr_to_string(request.output_filename) };
		let title = format!("Conforming Audio {}:{}", input, request.stream_index);
		ConformTask {
			base: Task::new(&title, CHandle::null()),
			input_filename: input,
			output_filename: output,
			stream_index: request.stream_index,
			sample_rate: request.sample_rate,
			channel_layout: request.channel_layout,
			sample_format: request.sample_format,
			final_names: Vec::new(),
			working_names: Vec::new(),
		}
	}

	/// Derive the final and working PCM filenames for the given first-channel
	/// final filename and channel count.
	///
	/// C++ contract: the first-channel final name must end in `.0.pcm`; the
	/// final name for channel `i` replaces the trailing index, and the
	/// working name appends `.working`. See `derive_filenames` in
	/// conform.cpp — this is a parity/golden candidate (see
	/// tests/parity_test.rs).
	///
	/// CPP-PARITY: src/task/src/conform/conform.cpp (derive_filenames)
	pub fn derive_filenames(
		first_channel_final: &str,
		channel_count: usize,
	) -> Result<(Vec<String>, Vec<String>)> {
		const SUFFIX: &str = ".0.pcm";

		if channel_count < 1
			|| first_channel_final.len() <= SUFFIX.len()
			|| !first_channel_final.ends_with(SUFFIX)
		{
			return Err(Error::Failed("Invalid conform output filename".to_string()));
		}

		let base = &first_channel_final[..first_channel_final.len() - SUFFIX.len()];
		let mut final_names = Vec::with_capacity(channel_count);
		let mut working_names = Vec::with_capacity(channel_count);
		for i in 0..channel_count {
			let final_name = format!("{base}.{i}.pcm");
			final_names.push(final_name.clone());
			working_names.push(format!("{final_name}.working"));
		}
		Ok((final_names, working_names))
	}
}

impl TaskBehavior for ConformTask {
	/// Run the conform via the oakcodec decoder (`bridge::codec`), emitting
	/// progress as audio is decoded and written to the working files, then
	/// rename working → final.
	fn run(&mut self, task: &mut Task) -> Result<()> {
		let channel_count = self.channel_layout.count_ones() as usize;
		let (final_names, working_names) =
			Self::derive_filenames(&self.output_filename, channel_count).map_err(|_| {
				task.set_error("Invalid conform output filename");
				Error::Failed("Invalid conform output filename".to_string())
			})?;
		self.final_names = final_names;
		self.working_names = working_names;

		let decoder = unsafe { bridge::codec::oakcodec_decoder_init() };
		if decoder.ctx.is_null() {
			task.set_error("Failed to create decoder");
			return Err(Error::Failed("Failed to create decoder".to_string()));
		}
		let mut decoder = decoder;

		let result = (|| {
			let open_result = unsafe {
				bridge::codec::oakcodec_decoder_open(
					decoder,
					cstr(&self.input_filename),
					self.stream_index,
				)
			};
			if open_result != 0 {
				let err = decoder_error(decoder);
				task.set_error(&format!("Failed to open decoder for audio conform: {err}"));
				return false;
			}

			let working_ptrs: Vec<*const std::ffi::c_char> =
				self.working_names.iter().map(|n| cstr(n)).collect();
			let conform_result = unsafe {
				bridge::codec::oakcodec_decoder_conform_audio(
					decoder,
					working_ptrs.as_ptr(),
					working_ptrs.len() as i32,
					self.sample_rate,
					self.channel_layout,
					self.sample_format,
					task.get_cancel_atom(),
				)
			};
			unsafe {
				bridge::codec::oakcodec_decoder_close(decoder);
			}

			if conform_result == 0 {
				// Rename each working file into place; a failure aborts the
				// rest (mirroring the C++ loop).
				for i in 0..self.working_names.len() {
					if std::fs::rename(&self.working_names[i], &self.final_names[i]).is_err() {
						task.set_error("Failed to move conformed audio into place");
						return false;
					}
				}
				true
			} else {
				// Clean up any partial working files.
				for name in &self.working_names {
					let _ = std::fs::remove_file(name);
				}
				if conform_result == bridge::codec::OAKCODEC_E_CANCELLED {
					task.set_error("Audio conform was cancelled");
				} else {
					task.set_error("Audio conform failed");
				}
				false
			}
		})();

		unsafe {
			bridge::codec::oakcodec_decoder_free(&mut decoder);
		}

		if result {
			Ok(())
		} else {
			Err(Error::Failed("Audio conform failed".to_string()))
		}
	}
}

/// Two-stage read of the decoder's last error string.
fn decoder_error(decoder: CHandle) -> String {
	let mut buf = [0i8; 256];
	let needed = unsafe {
		bridge::codec::oakcodec_decoder_last_error(decoder, buf.as_mut_ptr(), buf.len() as i32)
	};
	if needed <= 0 {
		return String::new();
	}
	let len = buf.iter().position(|&c| c == 0).unwrap_or(buf.len());
	unsafe {
		String::from_utf8_lossy(std::slice::from_raw_parts(buf.as_ptr() as *const u8, len))
			.into_owned()
	}
}
