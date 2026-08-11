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

//! `ProxyTask`, mirroring `src/task/src/proxy/proxy.h`.
//!
//! Transcodes a video stream to a proxy via an external `ffmpeg` process
//! (oakcodec kind `OAKCODEC_TASK_PROXY`). The two pure functions
//! [`ProxyTask::build_arguments`] and [`ProxyTask::parse_progress`] are
//! parity/golden candidates (see tests/parity_test.rs).
//!
//! CPP-PARITY: src/task/src/proxy/proxy.h

use std::io::{BufRead, BufReader};
use std::process::{Command, Stdio};

use crate::bridge;
use crate::error::{Error, Result};
use crate::handle::CHandle;
use crate::task::{Task, TaskBehavior};

/// The proxy parameters, mirroring `oakcodec_proxy_params` in
/// `include/codec/proxy.h` (kept as plain fields so no oakcodec handle is
/// needed to build arguments).
pub struct ProxyParams {
	/// Target width (0 = unspecified/divider-based).
	pub width: i32,
	/// Target height (0 = unspecified/divider-based).
	pub height: i32,
	/// Resolution divider.
	pub divider: i32,
	/// Proxy format version.
	pub version: i32,
	/// CRF for the proxy encode.
	pub crf: i32,
	/// Whether audio is included in the proxy.
	pub include_audio: bool,
	/// Output container extension.
	pub extension: String,
	/// ffmpeg preset name.
	pub preset: String,
}

/// A proxy transcode task: runs `ffmpeg` with arguments from
/// [`ProxyTask::build_arguments`], parsing progress lines via
/// [`ProxyTask::parse_progress`].
pub struct ProxyTask {
	/// The shared task base.
	pub base: Task,
	/// Source media filename.
	pub source_filename: String,
	/// Target stream index.
	pub stream_index: i32,
	/// Proxy parameters.
	pub params: ProxyParams,
	/// Output proxy filename.
	pub output_filename: String,
	/// Total media duration in seconds (for progress scaling).
	duration_seconds: f64,
}

impl ProxyTask {
	/// Build a proxy task from an oakcodec request and proxy params,
	/// mirroring the C++ constructor (divider-based requests take the source
	/// fraction).
	pub fn new(request: &bridge::codec::OakCodecTaskRequest, params: ProxyParams) -> ProxyTask {
		let source = unsafe { crate::ffi::taskhandle::cstr_to_string(request.input_filename) };
		let output = unsafe { crate::ffi::taskhandle::cstr_to_string(request.output_filename) };
		let mut params = params;
		if request.proxy_width > 0 && request.proxy_height > 0 {
			params.width = request.proxy_width;
			params.height = request.proxy_height;
			params.divider = 1;
		}
		let title = format!("Generating Proxy {}:{}", source, request.stream_index);
		ProxyTask {
			base: Task::new(&title, CHandle::null()),
			source_filename: source,
			stream_index: request.stream_index,
			params,
			output_filename: output,
			duration_seconds: 0.0,
		}
	}

	/// Build the `ffmpeg` command-line argument vector for the proxy
	/// transcode. Mirrors `build_arguments` in proxy.cpp; the exact ordering
	/// and flag spelling are parity-locked (golden test).
	///
	/// CPP-PARITY: src/task/src/proxy/proxy.cpp (build_arguments)
	pub fn build_arguments(
		source_filename: &str,
		stream_index: i32,
		params: &ProxyParams,
		output_filename: &str,
	) -> Vec<String> {
		let scale_filter = if params.divider > 1 {
			format!(
				"scale=w=trunc(iw/{}/2)*2:h=trunc(ih/{}/2)*2",
				params.divider, params.divider
			)
		} else {
			format!(
				"scale=w={}:h={}:force_original_aspect_ratio=decrease",
				params.width, params.height
			)
		};

		let container_format = if params.extension.is_empty() {
			"mp4".to_string()
		} else {
			params.extension.clone()
		};

		let mut args: Vec<String> = vec![
			"-y".to_string(),
			"-nostats".to_string(),
			"-progress".to_string(),
			"pipe:1".to_string(),
			"-i".to_string(),
			source_filename.to_string(),
			"-map".to_string(),
			format!("0:{stream_index}"),
		];

		if params.include_audio {
			args.extend([
				"-map".to_string(),
				"0:a?".to_string(),
				"-c:a".to_string(),
				"aac".to_string(),
				"-b:a".to_string(),
				"128k".to_string(),
			]);
		} else {
			args.push("-an".to_string());
		}

		args.extend([
			"-vf".to_string(),
			scale_filter,
			"-c:v".to_string(),
			"libx264".to_string(),
			"-preset".to_string(),
			params.preset.clone(),
			"-crf".to_string(),
			params.crf.to_string(),
			"-pix_fmt".to_string(),
			"yuv420p".to_string(),
			"-movflags".to_string(),
			"+faststart".to_string(),
			"-f".to_string(),
			container_format,
			output_filename.to_string(),
		]);

		args
	}

	/// Parse a single `ffmpeg -progress` output line into a progress value in
	/// 0.0..=1.0 given the total duration. Mirrors `parse_progress` in
	/// proxy.cpp (golden test).
	///
	/// CPP-PARITY: src/task/src/proxy/proxy.cpp (parse_progress)
	pub fn parse_progress(line: &str, duration_seconds: f64) -> Option<f64> {
		if duration_seconds <= 0.0 {
			return None;
		}

		let out_time_us: i64 = if let Some(rest) = line.strip_prefix("out_time_us=") {
			rest.trim().parse().unwrap_or(-1)
		} else if let Some(rest) = line.strip_prefix("out_time_ms=") {
			// Despite the name, ffmpeg reports this value in microseconds.
			rest.trim().parse().unwrap_or(-1)
		} else {
			-1
		};

		if out_time_us < 0 {
			return None;
		}

		let progress = out_time_us as f64 / 1_000_000.0 / duration_seconds;
		Some(if progress < 0.0 {
			0.0
		} else if progress > 1.0 {
			1.0
		} else {
			progress
		})
	}
}

impl TaskBehavior for ProxyTask {
	/// Spawn `ffmpeg` with the built arguments, feed `-progress` lines to
	/// [`ProxyTask::parse_progress`] and emit them as task progress.
	fn run(&mut self, task: &mut Task) -> Result<()> {
		let mut ffmpeg_buf = [0i8; 1024];
		let found = unsafe {
			bridge::codec::oakcodec_proxy_find_ffmpeg(
				std::ptr::null(),
				ffmpeg_buf.as_mut_ptr(),
				ffmpeg_buf.len() as i32,
			)
		};
		if found <= 0 {
			task.set_error(
				"Failed to generate proxy: ffmpeg executable was not found. Set the ffmpeg path in Preferences > Disk > Proxy Settings.",
			);
			return Err(Error::Failed("ffmpeg executable was not found".to_string()));
		}
		let ffmpeg_path = buf_to_string(&ffmpeg_buf);

		// Create the output directory if needed.
		if let Some(parent) = std::path::Path::new(&self.output_filename).parent() {
			if !parent.as_os_str().is_empty() && !parent.exists() {
				if std::fs::create_dir_all(parent).is_err() {
					task.set_error("Failed to create proxy output directory");
					return Err(Error::Failed(
						"Failed to create proxy output directory".to_string(),
					));
				}
			}
		}

		let _ = std::fs::remove_file(&self.output_filename);
		let working_filename = format!("{}.working.mp4", self.output_filename);
		let _ = std::fs::remove_file(&working_filename);

		let args = Self::build_arguments(
			&self.source_filename,
			self.stream_index,
			&self.params,
			&working_filename,
		);

		// Probe the source duration for progress scaling (0 when unknown).
		self.duration_seconds = probe_source_duration_seconds(&ffmpeg_path, &self.source_filename);

		let mut command = Command::new(&ffmpeg_path);
		command
			.args(&args)
			.stdout(Stdio::piped())
			.stderr(Stdio::piped());
		let mut child = match command.spawn() {
			Ok(child) => child,
			Err(_) => {
				task.set_error("Failed to start ffmpeg for proxy generation");
				return Err(Error::Failed("Failed to start ffmpeg".to_string()));
			}
		};

		// Drain stderr in a thread so a chatty ffmpeg cannot block us.
		let stderr = child.stderr.take();
		if let Some(stderr) = stderr {
			std::thread::spawn(move || {
				let _ = BufReader::new(stderr).lines().count();
			});
		}

		let stdout = child.stdout.take();
		let mut last_progress = 0.0_f64;
		if let Some(stdout) = stdout {
			let reader = BufReader::new(stdout);
			for line in reader.lines() {
				match line {
					Ok(line) => {
						if let Some(progress) = Self::parse_progress(&line, self.duration_seconds) {
							if progress >= 0.0 && progress - last_progress > 0.001 {
								last_progress = progress;
								task.emit_progress(progress);
							}
						}
					}
					Err(_) => break,
				}
				if task.is_cancelled() {
					let _ = child.kill();
					let _ = child.wait();
					let _ = std::fs::remove_file(&working_filename);
					task.set_error("Proxy generation was cancelled");
					return Err(Error::Cancelled);
				}
			}
		}

		let exit_status = child.wait();
		if !matches!(exit_status, Ok(status) if status.success()) {
			let _ = std::fs::remove_file(&working_filename);
			task.set_error("ffmpeg failed to generate proxy");
			return Err(Error::Failed("ffmpeg failed to generate proxy".to_string()));
		}

		if !std::path::Path::new(&working_filename).exists() {
			task.set_error("ffmpeg finished but proxy file was not created");
			return Err(Error::Failed(
				"ffmpeg finished but proxy file was not created".to_string(),
			));
		}

		if std::fs::rename(&working_filename, &self.output_filename).is_err() {
			task.set_error("Failed to move proxy into place");
			return Err(Error::Failed("Failed to move proxy into place".to_string()));
		}

		task.emit_progress(1.0);
		Ok(())
	}
}

/// Read a NUL-terminated C char buffer into a String.
fn buf_to_string(buf: &[i8]) -> String {
	let len = buf.iter().position(|&c| c == 0).unwrap_or(buf.len());
	unsafe {
		String::from_utf8_lossy(std::slice::from_raw_parts(buf.as_ptr() as *const u8, len))
			.into_owned()
	}
}

/// Probe the source duration via `ffprobe` next to ffmpeg; 0.0 when
/// unavailable (in which case no intermediate progress is reported).
fn probe_source_duration_seconds(ffmpeg_path: &str, source_filename: &str) -> f64 {
	let ffprobe = std::path::Path::new(ffmpeg_path).with_file_name("ffprobe");
	if !ffprobe.exists() {
		return 0.0;
	}
	let output = Command::new(&ffprobe)
		.args([
			"-v",
			"error",
			"-show_entries",
			"format=duration",
			"-of",
			"default=noprint_wrappers=1:nokey=1",
			source_filename,
		])
		.output();
	match output {
		Ok(out) if out.status.success() => {
			let text = String::from_utf8_lossy(&out.stdout);
			text.trim().parse().unwrap_or(0.0)
		}
		_ => 0.0,
	}
}
