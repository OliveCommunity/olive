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

//! `ProjectSaveTask`, mirroring `src/task/src/project/save/save.h`.
//!
//! Serializes a borrowed `OakNodeProject` to an `.oakproj` file via the
//! oaknode serializer. Optionally writes to an override filename and/or uses
//! compression.
//!
//! CPP-PARITY: src/task/src/project/save/save.h

use crate::bridge;
use crate::error::{Error, Result};
use crate::ffi::taskhandle::cstr;
use crate::handle::CHandle;
use crate::project::load::buf_to_string;
use crate::task::{Task, TaskBehavior};

/// Serializer result codes (`include/node/serializer.h`).
const OAKNODE_SERIALIZER_RESULT_SUCCESS: i32 = 0;
const OAKNODE_SERIALIZER_RESULT_XML_ERROR: i32 = 5;
const OAKNODE_SERIALIZER_RESULT_FILE_ERROR: i32 = 4;
const OAKNODE_SERIALIZER_RESULT_OVERWRITE_ERROR: i32 = 6;

/// A project-save task. Borrows its project and never takes ownership.
pub struct ProjectSaveTask {
	/// The shared task base.
	pub base: Task,
	/// Borrowed project to save (borrowed `OakNodeProject`).
	pub project: CHandle,
	/// Optional override filename; when empty the project's own filename is
	/// used.
	pub override_filename: Option<String>,
	/// Whether to compress the serialized output.
	pub use_compression: bool,
}

impl ProjectSaveTask {
	/// Set the output filename override.
	pub fn set_override_filename(&mut self, filename: &str) {
		self.override_filename = Some(filename.to_string());
	}
}

impl TaskBehavior for ProjectSaveTask {
	/// Serialize the project via the oaknode serializer (`bridge::node`) to
	/// the resolved filename.
	fn run(&mut self, task: &mut Task) -> Result<()> {
		let using_filename = match &self.override_filename {
			Some(name) => name.clone(),
			None => project_filename(self.project),
		};

		if using_filename.is_empty() {
			task.set_error("Project has no filename to save to.");
			return Err(Error::Failed(
				"Project has no filename to save to.".to_string(),
			));
		}

		let mut code = OAKNODE_SERIALIZER_RESULT_FILE_ERROR;
		let mut details = [0i8; 512];
		unsafe {
			bridge::node::oaknode_serializer_save_to_file(
				self.project,
				cstr(&using_filename),
				if self.use_compression { 1 } else { 0 },
				&mut code,
				details.as_mut_ptr(),
				details.len() as i32,
			);
		}

		let mut success = false;
		match code {
			OAKNODE_SERIALIZER_RESULT_SUCCESS => success = true,
			OAKNODE_SERIALIZER_RESULT_XML_ERROR => {
				task.set_error("Failed to write XML data.");
			}
			OAKNODE_SERIALIZER_RESULT_FILE_ERROR => {
				task.set_error(&format!(
					"Failed to open file for writing: {}",
					buf_to_string(&details)
				));
			}
			OAKNODE_SERIALIZER_RESULT_OVERWRITE_ERROR => {
				task.set_error(&format!(
					"Failed to overwrite \"{}\". Project has been saved as \"{}\" instead.",
					using_filename,
					buf_to_string(&details)
				));
				success = true;
			}
			_ => task.set_error("Unknown error."),
		}

		if success {
			Ok(())
		} else {
			Err(Error::Failed("Failed to save project".to_string()))
		}
	}
}

/// Two-stage read of the project's own filename (empty when unset).
pub(crate) fn project_filename(project: CHandle) -> String {
	let needed =
		unsafe { bridge::node::oaknode_project_filename(project, std::ptr::null_mut(), 0) };
	if needed <= 0 {
		return String::new();
	}
	let mut buf = vec![0i8; needed as usize];
	unsafe {
		bridge::node::oaknode_project_filename(project, buf.as_mut_ptr(), needed);
	}
	buf_to_string(&buf)
}
