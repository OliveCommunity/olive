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

//! The backend registry: registration, arbitration, lookup.

use std::sync::{Arc, Mutex};

use crate::backend::StorageBackend;
use crate::uri::StorageUri;

/// Process-wide backend registry (built-ins pre-registered).
pub struct Registry {
	backends: Mutex<Vec<Arc<dyn StorageBackend>>>,
}

impl Registry {
	/// Global registry.
	pub fn global() -> &'static Registry {
		todo!()
	}

	/// Register a backend (registration order = arbitration order
	/// within a scheme). Duplicate names are rejected with E_STATE.
	pub fn register(&self, backend: Arc<dyn StorageBackend>) -> crate::error::Result<()> {
		todo!()
	}

	/// Unregister by name; unknown name is E_NOT_FOUND.
	pub fn unregister(&self, name: &str) -> crate::error::Result<()> {
		todo!()
	}

	/// First backend claiming `uri` (E_NO_BACKEND when none).
	pub fn resolve(&self, uri: &StorageUri) -> crate::error::Result<Arc<dyn StorageBackend>> {
		todo!()
	}
}
