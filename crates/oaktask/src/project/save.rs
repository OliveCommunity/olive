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
//! Serializes a borrowed `oaknode::project::Project` to an `.oakproj` file
//! via the direct Rust serializer (`oaknode::serializer::save` —
//! single-lib unification; the old oaknode serializer C ABI with its
//! overwrite/compression result ladder is gone, so the overwrite-as-else
//! path collapses into a plain write). Optionally writes to an override
//! filename.
//!
//! CPP-PARITY: src/task/src/project/save/save.h

use std::sync::{Arc, Mutex};

use oaknode::project::Project;

use crate::error::{Error, Result};
use crate::task::{Task, TaskBehavior};

/// A project-save task. Borrows its project and never takes ownership.
pub struct ProjectSaveTask {
	/// The shared task base.
	pub base: Task,
	/// Borrowed project to save (`Arc<Mutex<Project>>`).
	pub project: Arc<Mutex<Project>>,
	/// Optional override filename; when empty the project's own filename is
	/// used.
	pub override_filename: Option<String>,
	/// Whether to compress the serialized output. The direct serializer has
	/// no compression mode (the old C ABI compression path is gone), so
	/// this flag is retained for API parity but ignored.
	pub use_compression: bool,
}

impl ProjectSaveTask {
	/// Set the output filename override.
	pub fn set_override_filename(&mut self, filename: &str) {
		self.override_filename = Some(filename.to_string());
	}
}

impl TaskBehavior for ProjectSaveTask {
	/// Serialize the project via the direct oaknode serializer
	/// (`oaknode::serializer::save`) to the resolved filename.
	fn run(&mut self, task: &mut Task) -> Result<()> {
		let using_filename = match &self.override_filename {
			Some(name) => name.clone(),
			None => self
				.project
				.lock()
				.unwrap_or_else(|e| e.into_inner())
				.filename()
				.to_string(),
		};

		if using_filename.is_empty() {
			task.set_error("Project has no filename to save to.");
			return Err(Error::Failed(
				"Project has no filename to save to.".to_string(),
			));
		}

		let xml = match oaknode::serializer::save(
			&self.project.lock().unwrap_or_else(|e| e.into_inner()),
		) {
			Ok(xml) => xml,
			Err(e) => {
				let _ = e;
				task.set_error("Failed to write XML data.");
				return Err(Error::Failed("Failed to save project".to_string()));
			}
		};

		if let Err(e) = std::fs::write(&using_filename, xml) {
			task.set_error(&format!("Failed to open file for writing: {e}"));
			return Err(Error::Failed("Failed to save project".to_string()));
		}

		Ok(())
	}
}
