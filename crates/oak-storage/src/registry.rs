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
//!
//! The process-wide registry is pre-populated with the built-in
//! backends (ove-xml, otio) at first access; foreign backends (the
//! future database backend, or a test mock) register through
//! [`Registry::register`] or the C vtable pair in `ffi.rs`.

use std::sync::{Arc, Mutex, OnceLock};

use crate::backend::StorageBackend;
use crate::error::{Error, Result};
use crate::uri::StorageUri;

/// Process-wide backend registry (built-ins pre-registered).
pub struct Registry {
	backends: Mutex<Vec<Arc<dyn StorageBackend>>>,
}

impl Registry {
	/// Global registry; built-ins are installed on first access.
	pub fn global() -> &'static Registry {
		static REGISTRY: OnceLock<Registry> = OnceLock::new();
		REGISTRY.get_or_init(|| {
			let registry = Registry {
				backends: Mutex::new(Vec::new()),
			};
			{
				let mut guard = lock(&registry.backends);
				for backend in crate::backends::builtins() {
					guard.push(backend);
				}
			}
			registry
		})
	}

	/// Register a backend (registration order = arbitration order
	/// within a scheme). Duplicate names are rejected with E_STATE.
	pub fn register(&self, backend: Arc<dyn StorageBackend>) -> Result<()> {
		let mut guard = lock(&self.backends);
		if guard.iter().any(|b| b.name() == backend.name()) {
			return Err(Error::State);
		}
		guard.push(backend);
		Ok(())
	}

	/// Unregister by name; unknown name is E_NOT_FOUND.
	pub fn unregister(&self, name: &str) -> Result<()> {
		let mut guard = lock(&self.backends);
		let before = guard.len();
		guard.retain(|b| b.name() != name);
		if guard.len() == before {
			Err(Error::NotFound)
		} else {
			Ok(())
		}
	}

	/// First backend claiming `uri` (E_NO_BACKEND when none).
	pub fn resolve(&self, uri: &StorageUri) -> Result<Arc<dyn StorageBackend>> {
		let guard = lock(&self.backends);
		guard
			.iter()
			.find(|b| b.can_handle(uri))
			.cloned()
			.ok_or(Error::NoBackend)
	}
}

/// Lock a mutex, tolerating poisoning (a panic inside this crate must
/// not cascade into every later call).
fn lock<T>(m: &Mutex<T>) -> std::sync::MutexGuard<'_, T> {
	m.lock().unwrap_or_else(|e| e.into_inner())
}

#[cfg(test)]
mod tests {
	use super::*;
	use crate::backend::{LoadResult, StorageBackend};
	use crate::handle::CHandle;

	/// A trivial in-crate backend for registry unit tests.
	struct MemBackend;
	impl StorageBackend for MemBackend {
		fn name(&self) -> &'static str {
			"mem-test"
		}
		fn uri_scheme(&self) -> &'static str {
			"mem"
		}
		fn can_handle(&self, uri: &StorageUri) -> bool {
			uri.scheme == "mem"
		}
		fn load(&self, _uri: &StorageUri) -> Result<LoadResult> {
			Ok(LoadResult::success(CHandle::null()))
		}
		fn save(&self, _project: CHandle, _uri: &StorageUri, _options: u32) -> Result<()> {
			Ok(())
		}
	}

	#[test]
	fn builtins_are_preregistered() {
		let names: Vec<String> = {
			let guard = lock(&Registry::global().backends);
			guard.iter().map(|b| b.name().to_string()).collect()
		};
		assert!(names.iter().any(|n| n == "ove-xml"), "builtins: {names:?}");
		assert!(names.iter().any(|n| n == "otio"), "builtins: {names:?}");
	}

	#[test]
	fn register_unregister_resolve() {
		let registry = Registry {
			backends: Mutex::new(Vec::new()),
		};
		let backend = Arc::new(MemBackend);
		assert!(registry.resolve(&StorageUri::parse("mem://x").unwrap()).is_err());

		registry.register(backend).unwrap();
		// Duplicate name rejected.
		assert!(registry.register(Arc::new(MemBackend)).is_err());
		assert!(
			registry
				.resolve(&StorageUri::parse("mem://x").unwrap())
				.is_ok()
		);
		// Unknown scheme still unresolvable.
		assert!(
			registry
				.resolve(&StorageUri::parse("file:///a.ove").unwrap())
				.is_err()
		);
		// Unregister flips it back to E_NO_BACKEND; unknown names E_NOT_FOUND.
		registry.unregister("mem-test").unwrap();
		assert!(registry.unregister("mem-test").is_err());
		assert!(
			registry
				.resolve(&StorageUri::parse("mem://x").unwrap())
				.is_err()
		);
	}

	#[test]
	fn resolve_is_first_registered_winner() {
		let registry = Registry {
			backends: Mutex::new(Vec::new()),
		};
		struct First;
		struct Second;
		impl StorageBackend for First {
			fn name(&self) -> &'static str {
				"first"
			}
			fn uri_scheme(&self) -> &'static str {
				"x"
			}
			fn can_handle(&self, _uri: &StorageUri) -> bool {
				true
			}
			fn load(&self, _uri: &StorageUri) -> Result<LoadResult> {
				Ok(LoadResult::success(CHandle::null()))
			}
			fn save(&self, _project: CHandle, _uri: &StorageUri, _options: u32) -> Result<()> {
				Ok(())
			}
		}
		impl StorageBackend for Second {
			fn name(&self) -> &'static str {
				"second"
			}
			fn uri_scheme(&self) -> &'static str {
				"x"
			}
			fn can_handle(&self, _uri: &StorageUri) -> bool {
				true
			}
			fn load(&self, _uri: &StorageUri) -> Result<LoadResult> {
				Ok(LoadResult::success(CHandle::null()))
			}
			fn save(&self, _project: CHandle, _uri: &StorageUri, _options: u32) -> Result<()> {
				Ok(())
			}
		}
		registry.register(Arc::new(First)).unwrap();
		registry.register(Arc::new(Second)).unwrap();
		let backend = registry
			.resolve(&StorageUri::parse("x://a").unwrap())
			.unwrap();
		assert_eq!(backend.name(), "first");
	}
}
