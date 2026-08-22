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
//! project plus its source URI. `take` transfers the project out, leaving
//! an empty shell that must still be freed.
//!
//! M14 R5: the session holds the boxed project directly
//! ([`crate::nodeutil::ProjectArc`]) instead of a `CHandle` — the handle
//! form is only produced at the backend `load` boundary
//! ([`crate::backend::LoadResult`]); `open` converts it before wrapping.

use crate::nodeutil::ProjectArc;
use crate::uri::StorageUri;

/// An open project session.
pub struct Session {
	/// Source URI.
	uri: StorageUri,
	/// The boxed project (None after [`Session::take`]).
	project: Option<ProjectArc>,
}

impl Session {
	/// Wrap a freshly loaded project. `None` (the version-info path:
	/// TOO_OLD/TOO_NEW/UNKNOWN_VERSION carries no project) leaves an empty
	/// session.
	pub fn new(uri: StorageUri, project: Option<ProjectArc>) -> Self {
		Session { uri, project }
	}

	/// Source URI.
	pub fn uri(&self) -> &StorageUri {
		&self.uri
	}

	/// Borrowed project (None after take).
	pub fn project(&self) -> Option<&ProjectArc> {
		self.project.as_ref()
	}

	/// Transfer the project out (C++ take_project semantics); the session
	/// becomes an empty shell, and the caller owns the returned reference.
	pub fn take(&mut self) -> Option<ProjectArc> {
		self.project.take()
	}
}
