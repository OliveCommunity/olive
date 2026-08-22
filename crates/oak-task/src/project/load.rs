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
//! Loads an `.oakproj` file into a new `oak_node::project::Project`. The
//! base task holds the filename and produces the project on
//! `take_project()`; `ProjectLoadTask` is the concrete (OTIO-less) loader.
//! The project itself is loaded through the direct Rust serializer
//! (`oak_node::serializer::load` — single-lib unification; the old oaknode
//! serializer C ABI with its per-code result mapping is gone, so the
//! version/result-code ladder collapses into the error message the XML
//! path used).
//!
//! CPP-PARITY: src/task/src/project/load/load.h

use std::sync::{Arc, Mutex};

use oak_node::project::Project;

use crate::error::{Error, Result};
use crate::task::{Task, TaskBehavior};

/// The base project-load task: owns a source filename and yields a loaded
/// project via [`ProjectLoadBaseTask::take_project`].
pub struct ProjectLoadBaseTask {
	/// The shared task base.
	pub base: Task,
	/// Absolute project filename to load.
	pub filename: String,
	/// The loaded project, produced by the task and taken via `take_project`.
	loaded_project: Option<Arc<Mutex<Project>>>,
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
	pub fn take_project(&mut self) -> Result<Arc<Mutex<Project>>> {
		self.loaded_project.take().ok_or(Error::State)
	}

	/// Store the loaded project (called by `run`).
	pub(crate) fn store_project(&mut self, project: Arc<Mutex<Project>>) {
		self.loaded_project = Some(project);
	}
}

impl TaskBehavior for ProjectLoadBaseTask {
	/// Load the project from `filename` via the direct oaknode serializer
	/// (`oak_node::serializer::load`), storing the resulting project.
	fn run(&mut self, task: &mut Task) -> Result<()> {
		let xml = match std::fs::read_to_string(&self.filename) {
			Ok(xml) => xml,
			Err(e) => {
				task.set_error(&format!(
					"Failed to read file \"{}\" for reading.",
					self.filename
				));
				let _ = e;
				return Err(Error::Failed("Failed to load project".to_string()));
			}
		};

		let project = match oak_node::serializer::load(&xml) {
			Ok(project) => project,
			Err(e) => {
				task.set_error(&format!(
					"Failed to read XML document. File may be corrupt. Error was: {e}"
				));
				return Err(Error::Failed("Failed to load project".to_string()));
			}
		};

		{
			let mut guard = project.lock().unwrap_or_else(|e| e.into_inner());
			guard.set_filename(&self.filename);
		}

		self.store_project(project);
		Ok(())
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
