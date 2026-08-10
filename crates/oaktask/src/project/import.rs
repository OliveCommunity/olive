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

//! `ProjectImportTask`, mirroring `src/task/src/project/import/import.h`.
//!
//! Imports media files into a folder of a project. Produces an undoable
//! `OakUndoCommand` (taken via `take_command()`), tracks per-file import
//! failures, and supports an optional image-sequence confirmation callback.
//!
//! CPP-PARITY: src/task/src/project/import/import.h

use std::path::Path;

use crate::bridge;
use crate::error::{Error, Result};
use crate::ffi::taskhandle::cstr;
use crate::handle::CHandle;
use crate::task::{Task, TaskBehavior};

/// Callback used to confirm whether a detected image sequence should be
/// imported as a sequence rather than individual frames, mirroring
/// `ImageSequenceConfirmFn` in import.h.
pub type ImageSequenceConfirmFn = Box<dyn FnMut(&str, &str) -> bool + Send>;

/// A project-import task. Borrows the destination folder and project.
pub struct ProjectImportTask {
	/// The shared task base.
	pub base: Task,
	/// Destination folder (borrowed `OakNodeFolder`).
	pub folder: CHandle,
	/// Destination project (borrowed `OakNodeProject`).
	pub project: CHandle,
	/// Media filenames to import.
	pub filenames: Vec<String>,
	/// The undo command produced by the task, taken via `take_command`.
	command: Option<CHandle>,
	/// Files that failed to import.
	invalid_files: Vec<String>,
	/// Imported footage, taken via `get_imported_footage`.
	imported_footage: Vec<CHandle>,
	/// Optional image-sequence confirmation callback.
	image_sequence_confirm: Option<ImageSequenceConfirmFn>,
	/// Total number of files to import (directories counted recursively).
	file_count: usize,
	/// Image-sequence files already decided not to be imported as sequences.
	image_sequence_ignore_files: Vec<String>,
}

impl Drop for ProjectImportTask {
	fn drop(&mut self) {
		if let Some(command) = &mut self.command {
			if !command.ctx.is_null() {
				unsafe {
					bridge::undo::oakundo_command_free(command);
				}
			}
		}
		// Borrowed footage handles: releasing them only frees the handle
		// boxes (the footage nodes are owned by the project).
		for footage in &self.imported_footage {
			if !footage.ctx.is_null() {
				if let Some(release) = footage.release {
					unsafe {
						release(footage.ctx);
					}
				}
			}
		}
	}
}

impl ProjectImportTask {
	/// Create an import task for the given folder/project and filenames.
	pub fn new(
		base: Task,
		folder: CHandle,
		project: CHandle,
		filenames: Vec<String>,
		image_sequence_confirm: Option<ImageSequenceConfirmFn>,
		file_count: usize,
	) -> ProjectImportTask {
		ProjectImportTask {
			base,
			folder,
			project,
			filenames,
			command: None,
			invalid_files: Vec::new(),
			imported_footage: Vec::new(),
			image_sequence_confirm,
			file_count,
			image_sequence_ignore_files: Vec::new(),
		}
	}

	/// Take ownership of the produced undo command; `Err(Error::State)` if
	/// the task has not run yet.
	pub fn take_command(&mut self) -> Result<CHandle> {
		self.command.take().ok_or(Error::State)
	}

	/// Number of files that failed to import.
	pub fn get_invalid_file_count(&self) -> usize {
		self.invalid_files.len()
	}

	/// Whether any file failed to import.
	pub fn has_invalid_files(&self) -> bool {
		!self.invalid_files.is_empty()
	}

	/// Borrowed handle to the imported footage at `index` (addref'd by the C
	/// ABI); `Err(Error::NotFound)` if out of range.
	pub fn get_imported_footage(&self, index: usize) -> Result<CHandle> {
		let footage = self.imported_footage.get(index).ok_or(Error::NotFound)?;
		if !footage.ctx.is_null() {
			if let Some(addref) = footage.addref {
				unsafe {
					addref(footage.ctx);
				}
			}
		}
		Ok(*footage)
	}

	/// Total number of imported footage entries.
	pub fn get_file_count(&self) -> usize {
		self.imported_footage.len()
	}

	/// Set the image-sequence confirmation callback.
	pub fn set_image_sequence_confirm_callback(&mut self, cb: ImageSequenceConfirmFn) {
		self.image_sequence_confirm = Some(cb);
	}

	/// The number of files counted at construction (progress denominator).
	pub fn file_count(&self) -> usize {
		self.file_count
	}

	/// The invalid filename at `index` (assumes the index is in range).
	pub(crate) fn invalid_file_at(&self, index: usize) -> &str {
		&self.invalid_files[index]
	}

	fn import(&mut self, task: &mut Task, folder: CHandle, entries: &mut Vec<String>, counter: &mut usize, parent_command: CHandle) {
		let mut i = 0;
		while i < entries.len() {
			if task.is_cancelled() {
				break;
			}

			let file_path = &entries[i];

			if Path::new(file_path).is_dir() {
				// Create a folder corresponding to the directory and recurse.
				let entry_list: Vec<String> = match std::fs::read_dir(file_path) {
					Ok(rd) => rd
						.filter_map(|e| e.ok())
						.map(|e| e.path().to_string_lossy().into_owned())
						.collect(),
					Err(_) => Vec::new(),
				};

				if !entry_list.is_empty() {
					let folder_handle = unsafe { bridge::node::oaknode_folder_create(self.project) };
					if !folder_handle.ctx.is_null() {
						unsafe {
							bridge::node::oaknode_node_set_label(
								bridge::node::oaknode_folder_as_node(folder_handle),
								cstr(&basename_of(file_path)),
							);
						}
						self.add_item_to_folder(folder, unsafe { bridge::node::oaknode_folder_as_node(folder_handle) }, parent_command);
						let mut sub_entries = entry_list;
						self.import(task, folder_handle, &mut sub_entries, counter, parent_command);
					}
				}
			} else {
				let footage = unsafe { bridge::node::oaknode_footage_create(self.project, std::ptr::null()) };
				if footage.ctx.is_null() {
					i += 1;
					continue;
				}

				unsafe {
					bridge::node::oaknode_footage_set_cancel_atom(footage, task.get_cancel_atom());
				}
				let ok = unsafe { bridge::node::oaknode_footage_set_filename(footage, cstr(file_path)) } == 0;
				unsafe {
					bridge::node::oaknode_footage_set_cancel_atom(footage, bridge::render::OakCancelAtom::null());
				}

				if ok && unsafe { bridge::node::oaknode_footage_is_valid(footage) } != 0 {
					unsafe {
						bridge::node::oaknode_node_set_label(bridge::node::oaknode_footage_as_node(footage), cstr(&basename_of(file_path)));
					}

					// See if this footage is an image sequence.
					self.validate_image_sequence(task, footage, entries, i);

					// Create the undoable command that adds the item.
					self.add_item_to_folder(folder, unsafe { bridge::node::oaknode_footage_as_node(footage) }, parent_command);

					self.imported_footage.push(footage);
				} else {
					self.invalid_files.push(file_path.clone());

					// Remove the invalid footage from the graph; the remove
					// command takes ownership on redo and deletes the node
					// when the command is destroyed.
					let mut remove = unsafe { bridge::node::oaknode_command_create_remove_node(bridge::node::oaknode_footage_as_node(footage)) };
					if !remove.ctx.is_null() {
						unsafe {
							bridge::undo::oakundo_command_redo_now(remove);
							bridge::undo::oakundo_command_free(&mut remove);
						}
					}
				}

				*counter += 1;
				task.emit_progress(*counter as f64 / self.file_count.max(1) as f64);
			}
			i += 1;
		}
	}

	fn add_item_to_folder(&self, folder: CHandle, item: CHandle, command: CHandle) {
		let child = unsafe { bridge::node::oaknode_command_create_folder_add_child(folder, item) };
		if !child.ctx.is_null() {
			unsafe {
				bridge::undo::oakundo_command_multi_add_child(command, child);
			}
		}
	}

	fn validate_image_sequence(&mut self, task: &mut Task, footage: CHandle, info_list: &mut Vec<String>, index: usize) {
		let filename = footage_filename(footage);
		if filename.is_empty() {
			return;
		}

		let digit_count = unsafe { bridge::codec::oakcodec_decoder_get_image_sequence_digit_count(cstr(&filename)) };
		if digit_count <= 0 {
			return;
		}

		if self.image_sequence_ignore_files.iter().any(|f| f == &filename) {
			return;
		}

		if !self.item_is_still_image_footage_only(footage) {
			return;
		}

		let mut video_stream = CHandle::null();
		if unsafe { bridge::node::oaknode_footage_get_video_params(footage, 0, &mut video_stream) } != 0 {
			return;
		}

		let mut width = 0;
		let mut height = 0;
		unsafe {
			bridge::common::oakcommon_videoparams_get_width(video_stream, &mut width);
			bridge::common::oakcommon_videoparams_get_height(video_stream, &mut height);
		}

		let seq_index = unsafe { bridge::codec::oakcodec_decoder_get_image_sequence_index(cstr(&filename)) };

		let prev_fn = transform_sequence_filename(&filename, seq_index - 1, digit_count);
		let next_fn = transform_sequence_filename(&filename, seq_index + 1, digit_count);

		let previous_file = unsafe { bridge::node::oaknode_footage_create(self.project, cstr(&prev_fn)) };
		let next_file = unsafe { bridge::node::oaknode_footage_create(self.project, cstr(&next_fn)) };

		let prev_matches = !previous_file.ctx.is_null()
			&& unsafe { bridge::node::oaknode_footage_is_valid(previous_file) } != 0
			&& self.compare_still_image_size(previous_file, width, height);
		let next_matches = !next_file.ctx.is_null()
			&& unsafe { bridge::node::oaknode_footage_is_valid(next_file) } != 0
			&& self.compare_still_image_size(next_file, width, height);

		if prev_matches || next_matches {
			// Ask the user whether this is really a sequence (default: no).
			let is_sequence = match self.image_sequence_confirm.as_mut() {
				Some(cb) => cb(&filename, &filename),
				None => false,
			};

			let start_index = get_image_sequence_limit(&filename, seq_index, false, digit_count);
			let end_index = get_image_sequence_limit(&filename, seq_index, true, digit_count);

			for j in start_index..=end_index {
				let entry_fn = transform_sequence_filename(&filename, j, digit_count);

				if is_sequence {
					// Remove later entries of this sequence from the import
					// list (they are imported as one footage). The C++
					// `info_list.size()` is re-evaluated after each erase.
					let mut k = index + 1;
					while k < info_list.len() {
						if info_list[k] == entry_fn {
							info_list.remove(k);
							break;
						}
						k += 1;
					}
				} else {
					self.image_sequence_ignore_files.push(entry_fn);
				}
			}

			if is_sequence {
				unsafe {
					bridge::common::oakcommon_videoparams_set_video_type(video_stream, bridge::common::OAKCOMMON_VIDEO_TYPE_IMAGE_SEQUENCE);

					let mut rate_buf = [0i8; 64];
					let needed = bridge::common::oakcommon_config_get(std::ptr::null(), cstr("DefaultSequenceFrameRate"), rate_buf.as_mut_ptr(), rate_buf.len() as i32);
					if needed > 0 {
						let rate = crate::project::load::buf_to_string(&rate_buf);
						if let Some((num, den)) = parse_rational(&rate) {
							if den != 0 {
								bridge::common::oakcommon_videoparams_set_time_base(video_stream, num, den);
								bridge::common::oakcommon_videoparams_set_frame_rate(video_stream, den, num);
							}
						}
					}

					bridge::common::oakcommon_videoparams_set_start_time(video_stream, start_index);
					bridge::common::oakcommon_videoparams_set_duration(video_stream, end_index - start_index + 1);
					bridge::node::oaknode_footage_set_video_params(footage, 0, &video_stream);
				}
			}
		}

		if !video_stream.ctx.is_null() {
			unsafe {
				bridge::common::oakcommon_videoparams_free(&mut video_stream);
			}
		}

		// The probe footage was only created for comparison; remove it.
		for probe in [previous_file, next_file] {
			if !probe.ctx.is_null() {
				let mut remove = unsafe { bridge::node::oaknode_command_create_remove_node(bridge::node::oaknode_footage_as_node(probe)) };
				if !remove.ctx.is_null() {
					unsafe {
						bridge::undo::oakundo_command_redo_now(remove);
						bridge::undo::oakundo_command_free(&mut remove);
					}
				}
			}
		}

		let _ = task;
	}

	fn item_is_still_image_footage_only(&self, footage: CHandle) -> bool {
		if unsafe { bridge::node::oaknode_footage_total_stream_count(footage) } != 1 {
			return false;
		}

		let mut vp = CHandle::null();
		if unsafe { bridge::node::oaknode_footage_get_video_params(footage, 0, &mut vp) } != 0 {
			return false;
		}

		let mut video_type = 0;
		let mut valid = 0;
		unsafe {
			bridge::common::oakcommon_videoparams_get_video_type(vp, &mut video_type);
			bridge::common::oakcommon_videoparams_get_is_valid(vp, &mut valid);
			bridge::common::oakcommon_videoparams_free(&mut vp);
		}

		valid != 0 && video_type == bridge::common::OAKCOMMON_VIDEO_TYPE_STILL
	}

	fn compare_still_image_size(&self, footage: CHandle, width: i32, height: i32) -> bool {
		if !self.item_is_still_image_footage_only(footage) {
			return false;
		}

		let mut stream = CHandle::null();
		if unsafe { bridge::node::oaknode_footage_get_video_params(footage, 0, &mut stream) } != 0 {
			return false;
		}

		let mut w = 0;
		let mut h = 0;
		unsafe {
			bridge::common::oakcommon_videoparams_get_width(stream, &mut w);
			bridge::common::oakcommon_videoparams_get_height(stream, &mut h);
			bridge::common::oakcommon_videoparams_free(&mut stream);
		}

		w == width && h == height
	}
}

impl TaskBehavior for ProjectImportTask {
	/// Import each filename via the oaknode footage/folder C ABI
	/// (`bridge::node`), collecting an undo command, imported footage, and
	/// per-file failures.
	fn run(&mut self, task: &mut Task) -> Result<()> {
		let mut command = unsafe { bridge::undo::oakundo_command_init_multi() };
		if command.ctx.is_null() {
			task.set_error("Failed to create import command");
			return Err(Error::Failed("Failed to create import command".to_string()));
		}
		self.command = Some(command);

		let mut counter = 0;
		let mut entries = self.filenames.clone();
		self.import(task, self.folder, &mut entries, &mut counter, command);

		if task.is_cancelled() {
			if !command.ctx.is_null() {
				unsafe {
					bridge::undo::oakundo_command_free(&mut command);
				}
			}
			self.command = None;
			return Err(Error::Cancelled);
		}
		Ok(())
	}
}

/// Count files recursively (directories recurse; anything else counts 1),
/// mirroring `count_files_recursive` in import.cpp.
fn count_files_recursive(paths: &[String]) -> usize {
	paths
		.iter()
		.map(|p| {
			let path = Path::new(p);
			if path.is_dir() {
				count_dir_files(path)
			} else {
				1
			}
		})
		.sum()
}

fn count_dir_files(dir: &Path) -> usize {
	match std::fs::read_dir(dir) {
		Ok(rd) => rd
			.filter_map(|e| e.ok())
			.map(|e| {
				let p = e.path();
				if p.is_dir() {
					count_dir_files(&p)
				} else {
					1
				}
			})
			.sum(),
		Err(_) => 0,
	}
}

/// `std::filesystem::path(filename).filename()`, as a String.
fn basename_of(path: &str) -> String {
	Path::new(path)
		.file_name()
		.map(|n| n.to_string_lossy().into_owned())
		.unwrap_or_default()
}

/// Two-stage read of the footage filename.
fn footage_filename(footage: CHandle) -> String {
	let needed = unsafe { bridge::node::oaknode_footage_filename(footage, std::ptr::null_mut(), 0) };
	if needed <= 0 {
		return String::new();
	}
	let mut buf = vec![0i8; needed as usize];
	unsafe {
		bridge::node::oaknode_footage_filename(footage, buf.as_mut_ptr(), needed);
	}
	crate::project::load::buf_to_string(&buf)
}

/// Substitute `number` into the trailing-digit field of an image-sequence
/// filename (mirrors `oakcodec_decoder_transform_image_sequence_file_name`).
fn transform_sequence_filename(filename: &str, number: i64, digit_count: i32) -> String {
	let width = digit_count.max(0) as usize;
	match filename.rfind('.') {
		Some(dot) => {
			// The digits are the `width` chars immediately before the
			// extension; they are replaced by `number`.
			let digits_start = dot.saturating_sub(width);
			let prefix = &filename[..digits_start];
			format!("{prefix}{:0width$}{}", number, &filename[dot..], width = width)
		}
		None => format!("{filename}{:0width$}", number, width = width),
	}
}

/// Walk the index from `start` in the given direction while the transformed
/// filename exists (mirrors `get_image_sequence_limit` in import.cpp).
fn get_image_sequence_limit(start_fn: &str, start: i64, up: bool, digit_count: i32) -> i64 {
	let mut current = start;
	loop {
		let test_index = if up { current + 1 } else { current - 1 };
		let test_filename = transform_sequence_filename(start_fn, test_index, digit_count);
		if !Path::new(&test_filename).exists() {
			break;
		}
		current = test_index;
	}
	current
}

/// Parse a "num/den" frame-rate string.
fn parse_rational(s: &str) -> Option<(i32, i32)> {
	let mut parts = s.trim().split('/');
	let num: i32 = parts.next()?.trim().parse().ok()?;
	let den: i32 = parts.next()?.trim().parse().ok()?;
	Some((num, den))
}

/// Convenience used by the C ABI factory to compute the title.
pub(crate) fn import_title(paths: &[String]) -> String {
	let count = count_files_recursive(paths).max(1);
	format!("Importing {count} file(s)")
}

/// Convenience used by the C ABI factory to compute the progress
/// denominator (mirrors the C++ `file_count_`).
pub(crate) fn import_file_count(paths: &[String]) -> usize {
	count_files_recursive(paths).max(1)
}
