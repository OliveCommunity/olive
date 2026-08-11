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

//! `ProjectLoadBaseTask` / `ProjectLoadTask`, mirroring
//! `src/task/src/project/load/load.h`.
//!
//! Loads an `.oakproj` file into a new `OakNodeProject`. The base task holds
//! the filename and produces the project on `take_project()`; `ProjectLoadTask`
//! is the concrete (OTIO-less) loader.
//!
//! CPP-PARITY: src/task/src/project/load/load.h

use crate::bridge;
use crate::error::{Error, Result};
use crate::ffi::taskhandle::cstr;
use crate::handle::CHandle;
use crate::task::{Task, TaskBehavior};

/// Serializer result codes (`include/node/serializer.h`).
const OAKNODE_SERIALIZER_RESULT_SUCCESS: i32 = 0;
const OAKNODE_SERIALIZER_RESULT_PROJECT_TOO_OLD: i32 = 1;
const OAKNODE_SERIALIZER_RESULT_PROJECT_TOO_NEW: i32 = 2;
const OAKNODE_SERIALIZER_RESULT_UNKNOWN_VERSION: i32 = 3;
const OAKNODE_SERIALIZER_RESULT_FILE_ERROR: i32 = 4;
const OAKNODE_SERIALIZER_RESULT_XML_ERROR: i32 = 5;
const OAKNODE_SERIALIZER_RESULT_NO_DATA: i32 = 7;

/// The base project-load task: owns a source filename and yields a loaded
/// project via [`ProjectLoadBaseTask::take_project`].
pub struct ProjectLoadBaseTask {
	/// The shared task base.
	pub base: Task,
	/// Absolute project filename to load.
	pub filename: String,
	/// The loaded project, produced by the task and taken via `take_project`.
	loaded_project: Option<CHandle>,
}

impl Drop for ProjectLoadBaseTask {
	fn drop(&mut self) {
		// Free the loaded project if it was never taken.
		if let Some(project) = &mut self.loaded_project {
			if !project.ctx.is_null() {
				unsafe {
					bridge::node::oaknode_project_free(project);
				}
			}
		}
	}
}

impl ProjectLoadBaseTask {
	/// Create a base load task for the given filename.
	pub fn new(base: Task, filename: String) -> ProjectLoadBaseTask {
		ProjectLoadBaseTask {
			base,
			filename,
			loaded_project: None,
		}
	}

	/// Take ownership of the loaded project. Returns `Err(Error::State)` if
	/// the task has not loaded a project yet.
	pub fn take_project(&mut self) -> Result<CHandle> {
		self.loaded_project.take().ok_or(Error::State)
	}

	/// Store the loaded project (called by `run`).
	pub(crate) fn store_project(&mut self, project: CHandle) {
		self.loaded_project = Some(project);
	}
}

impl TaskBehavior for ProjectLoadBaseTask {
	/// Load the project from `filename` via the oaknode serializer
	/// (`bridge::node`), storing the resulting `OakNodeProject`.
	fn run(&mut self, task: &mut Task) -> Result<()> {
		let mut project = unsafe { bridge::node::oaknode_project_init() };
		if project.ctx.is_null() {
			task.set_error("Failed to create project");
			return Err(Error::Failed("Failed to create project".to_string()));
		}

		unsafe {
			bridge::node::oaknode_project_set_filename(project, cstr(&self.filename));
		}

		let mut code = OAKNODE_SERIALIZER_RESULT_FILE_ERROR;
		let mut details = [0i8; 512];
		let result = unsafe {
			bridge::node::oaknode_serializer_load_from_file(
				project,
				cstr(&self.filename),
				&mut code,
				details.as_mut_ptr(),
				details.len() as i32,
			)
		};

		let mut success = false;
		match code {
			OAKNODE_SERIALIZER_RESULT_SUCCESS => success = true,
			OAKNODE_SERIALIZER_RESULT_PROJECT_TOO_OLD => {
				task.set_error("This project is from a version of Oak Video Editor that is no longer supported in this version.");
			}
			OAKNODE_SERIALIZER_RESULT_PROJECT_TOO_NEW => {
				task.set_error("This project is from a newer version of Oak Video Editor and cannot be opened in this version.");
			}
			OAKNODE_SERIALIZER_RESULT_UNKNOWN_VERSION => {
				task.set_error("Failed to determine project version.");
			}
			OAKNODE_SERIALIZER_RESULT_FILE_ERROR => {
				task.set_error(&format!(
					"Failed to read file \"{}\" for reading.",
					self.filename
				));
			}
			OAKNODE_SERIALIZER_RESULT_XML_ERROR => {
				task.set_error(&format!(
					"Failed to read XML document. File may be corrupt. Error was: {}",
					buf_to_string(&details)
				));
			}
			OAKNODE_SERIALIZER_RESULT_NO_DATA => {
				task.set_error("Failed to find any data to parse.");
			}
			_ => task.set_error("Unknown error."),
		}

		if result == 0 && success {
			self.store_project(project);
			return Ok(());
		}

		if !project.ctx.is_null() {
			unsafe {
				bridge::node::oaknode_project_free(&mut project);
			}
		}
		Err(Error::Failed("Failed to load project".to_string()))
	}
}

/// The concrete native project loader (no OTIO path).
pub struct ProjectLoadTask {
	/// The shared task base.
	pub base: ProjectLoadBaseTask,
}

impl TaskBehavior for ProjectLoadTask {
	fn run(&mut self, task: &mut Task) -> Result<()> {
		<ProjectLoadBaseTask as TaskBehavior>::run(&mut self.base, task)
	}
}

/// Read a NUL-terminated char buffer into a String (lossy).
pub(crate) fn buf_to_string(buf: &[i8]) -> String {
	let len = buf.iter().position(|&c| c == 0).unwrap_or(buf.len());
	unsafe {
		String::from_utf8_lossy(std::slice::from_raw_parts(buf.as_ptr() as *const u8, len))
			.into_owned()
	}
}
