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

//! The project session (M10 §2.2 `OakStorageProject`): wraps a loaded
//! project plus its source URI. `take` transfers the project handle
//! out, leaving an empty shell that must still be freed.

use crate::handle::CHandle;
use crate::uri::StorageUri;

/// An open project session.
pub struct Session {
	/// Source URI.
	uri: StorageUri,
	/// The project handle (None after [`Session::take`]).
	project: Option<CHandle>,
}

impl Session {
	/// Wrap a freshly loaded project.
	pub fn new(uri: StorageUri, project: CHandle) -> Self {
		Session {
			uri,
			project: Some(project),
		}
	}

	/// Source URI.
	pub fn uri(&self) -> &StorageUri {
		&self.uri
	}

	/// Borrowed project handle (None after take).
	pub fn project(&self) -> Option<&CHandle> {
		self.project.as_ref()
	}

	/// Transfer the project out (C++ take_project semantics); the
	/// session becomes an empty shell, and the caller owns the returned
	/// handle (release it with `oaknode_project_free`).
	pub fn take(&mut self) -> Option<CHandle> {
		self.project.take()
	}
}

impl Drop for Session {
	fn drop(&mut self) {
		// Release the still-held project handle (the `take` path already
		// removed it).
		if let Some(h) = self.project.take() {
			if let Some(f) = h.release {
				unsafe { f(h.ctx) };
			}
		}
	}
}
