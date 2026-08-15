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
//! `OAKCODEC_TASK_CONFORM`) and reports progress as it decodes. The
//! decoder is reached through the direct Rust API
//! (`oakcodec::decoder::{receive_list_of_all_decoders, Decoder}`) —
//! single-lib unification; the old oakcodec decoder C ABI is gone.
//!
//! CPP-PARITY: src/task/src/conform/conform.h

use oakcodec::decoder::CodecStream;
use oakcodec::task::TaskRequest;

use crate::error::{Error, Result};
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
	pub fn new(request: &TaskRequest) -> ConformTask {
		let title = format!(
			"Conforming Audio {}:{}",
			request.input_filename, request.stream_index
		);
		ConformTask {
			base: Task::new(&title, None),
			input_filename: request.input_filename.to_string(),
			output_filename: request.output_filename.to_string(),
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
	/// Run the conform via the direct oakcodec decoder API, emitting
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

		let atom = task.get_cancel_atom();

		// Pick the first registered decoder that can probe the file (the
		// C++ picks the decoder the footage was created with; probing in
		// registry order is the direct-Rust equivalent).
		let decoder = oakcodec::decoder::receive_list_of_all_decoders()
			.into_iter()
			.find(|d| d.probe(&self.input_filename, Some(&atom)).is_some());
		let Some(decoder) = decoder else {
			task.set_error("Failed to create decoder");
			return Err(Error::Failed("Failed to create decoder".to_string()));
		};

		let result = (|| {
			let stream =
				CodecStream::with_block(self.input_filename.clone(), self.stream_index, None);
			if let Err(e) = decoder.open(&stream) {
				task.set_error(&format!("Failed to open decoder for audio conform: {e}"));
				return false;
			}

			let conform_result = decoder.conform_audio(
				&self.working_names,
				self.sample_rate,
				self.channel_layout,
				self.sample_format,
				Some(&atom),
			);
			let _ = decoder.close();

			match conform_result {
				Ok(()) => {
					// Rename each working file into place; a failure aborts
					// the rest (mirroring the C++ loop).
					for i in 0..self.working_names.len() {
						if std::fs::rename(&self.working_names[i], &self.final_names[i]).is_err() {
							task.set_error("Failed to move conformed audio into place");
							return false;
						}
					}
					true
				}
				Err(_) => {
					// Clean up any partial working files.
					for name in &self.working_names {
						let _ = std::fs::remove_file(name);
					}
					if atom.is_cancelled() {
						task.set_error("Audio conform was cancelled");
					} else {
						task.set_error("Audio conform failed");
					}
					false
				}
			}
		})();

		if result {
			Ok(())
		} else {
			Err(Error::Failed("Audio conform failed".to_string()))
		}
	}
}
