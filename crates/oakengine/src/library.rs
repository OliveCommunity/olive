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

//! The project-library manager C ABI (plan M13 §4): list / create / open /
//! rename / duplicate / delete / import / export over the oakstorage
//! database backend the write-through binds to ([`crate::storage`]).
//!
//! These exports are additive (D4): the app talks to the engine dylib only
//! through the frozen `oakengine_*` surface, so the manager's data source
//! crosses the boundary here instead of linking oakstorage directly (which
//! would give the app a second copy of the handle/serializer types).
//!
//! All operations address the configured default library (the same
//! `Storage/Backend` + `Storage/SqlitePath` configuration the write-through
//! uses); with storage disabled every call fails with `OAKENGINE_E_STATE`
//! except [`oakengine_library_list`], which reports an empty library
//! (`"[]"`) so the manager window can still open.

use std::ffi::{c_char, c_int};

use oakstorage::backend::StorageBackend;
use oakstorage::uri::StorageUri;

use crate::error::{Error, Result};
use crate::handle::{guard, guard_int, read_cstr, write_string, OakEngineProject};
use crate::stubs::node as n;

/// One library row as the project manager shows it: the project metadata
/// plus the stats derived from the head state (plan §4).
#[derive(serde::Serialize)]
struct LibraryRow {
	/// Library row uuid (the open/duplicate/export selector).
	uuid: String,
	/// Display name.
	name: String,
	/// Row creation time (unix seconds, UTC).
	created_at: i64,
	/// Last-write time (unix seconds, UTC; the manager sort key).
	modified_at: i64,
	/// Longest sequence duration, milliseconds.
	duration_ms: i64,
	/// Total tracks across all sequences.
	track_count: i32,
	/// Total clip blocks.
	clip_count: i32,
	/// Total footage nodes.
	footage_count: i32,
}

/// Map an oakstorage error onto the facade error space (the context string
/// is log-only per the error contract).
fn map_err(e: oakstorage::error::Error) -> Error {
	use oakstorage::error::Error as E;
	match e {
		E::Invalid => Error::Invalid,
		E::State => Error::State,
		E::NotFound => Error::NotFound,
		E::NoMem => Error::NoMem,
		other => Error::Failed(other.to_string()),
	}
}

/// The configured default library as a parsed URI; [`Error::State`] when
/// the write-through backend is disabled or the path does not resolve.
fn library() -> Result<StorageUri> {
	if !crate::storage::storage_enabled() {
		return Err(Error::State);
	}
	let uri = crate::storage::library_uri().ok_or(Error::State)?;
	StorageUri::parse(&uri).map_err(map_err)
}

/// The library URI selecting one row (`…?project=<uuid>`).
fn project_uri(uuid: &str) -> Result<StorageUri> {
	let uri = library()?;
	StorageUri::parse(&format!("{}?project={uuid}", uri.to_uri_string())).map_err(map_err)
}

/// Load one library row as an owned project handle (refcount 1).
fn load_handle(uuid: &str) -> Result<crate::handle::CHandle> {
	let uri = project_uri(uuid)?;
	let result = crate::storage::backend().load(&uri).map_err(map_err)?;
	if result.project.is_null() {
		return Err(Error::Failed(format!(
			"library load of {uuid} returned no project (info code {})",
			result.version_info
		)));
	}
	Ok(result.project)
}

/// Release an owned handle (refcount 1).
fn release(h: crate::handle::CHandle) {
	if let Some(release) = h.release {
		unsafe { release(h.ctx) };
	}
}

/// `oakengine_library_list` — the library rows as a JSON array (buf/size
/// convention), most recently modified first. Each row carries the manager
/// stats derived from the head state; a row whose stats fail to replay
/// degrades to zeros instead of failing the whole list. With storage
/// disabled the result is the empty array (`"[]"`), not an error.
#[no_mangle]
pub unsafe extern "C" fn oakengine_library_list(buf: *mut c_char, buf_size: c_int) -> c_int {
	guard_int(|| unsafe {
		if !crate::storage::storage_enabled() {
			return Ok(write_string("[]", buf, buf_size));
		}
		let uri = library()?;
		let infos = crate::storage::backend()
			.list_projects(&uri)
			.map_err(map_err)?;
		let mut rows = Vec::with_capacity(infos.len());
		for info in infos {
			let stats = crate::storage::backend()
				.project_stats(&uri, &info.uuid)
				.unwrap_or_default();
			rows.push(LibraryRow {
				uuid: info.uuid,
				name: info.name,
				created_at: info.created_at.and_utc().timestamp(),
				modified_at: info.modified_at.and_utc().timestamp(),
				duration_ms: stats.duration_ms,
				track_count: stats.track_count,
				clip_count: stats.clip_count,
				footage_count: stats.footage_count,
			});
		}
		let json = serde_json::to_string(&rows)
			.map_err(|e| Error::Failed(format!("library list encode: {e}")))?;
		Ok(write_string(&json, buf, buf_size))
	})
}

/// `oakengine_library_create` — create a blank project named `name` as a
/// new library row and report its uuid (buf/size convention on
/// `out_uuid`; the return value is the uuid length, negative on error).
/// The row lands immediately (one `kind='import'` command), so the
/// manager list shows it before the first edit.
#[no_mangle]
pub unsafe extern "C" fn oakengine_library_create(
	name: *const c_char,
	out_uuid: *mut c_char,
	out_size: c_int,
) -> c_int {
	guard_int(|| unsafe {
		if name.is_null() {
			return Err(Error::Invalid);
		}
		let name = read_cstr(name);
		if name.trim().is_empty() {
			return Err(Error::Invalid);
		}
		let uri = library()?;

		let mut h = n::oaknode_project_init();
		if h.is_null() {
			return Err(Error::NoMem);
		}
		let outcome = (|| -> Result<String> {
			Error::from_module(n::oaknode_project_initialize(h))?;
			let uuid = {
				let arc = crate::handle::domain::project_of(&h).ok_or(Error::Invalid)?;
				let mut guard = arc.lock().unwrap_or_else(|e| e.into_inner());
				guard.settings.insert("projectname".to_string(), name);
				guard.uuid.clone()
			};
			crate::storage::backend().save(h, &uri, 0).map_err(map_err)?;
			Ok(uuid)
		})();
		n::oaknode_project_free(&mut h);
		let uuid = outcome?;
		Ok(write_string(&uuid, out_uuid, out_size))
	})
}

/// `oakengine_library_delete` — delete the library row `uuid` (cascades
/// settings / snapshots / journal; `OAKENGINE_E_NOT_FOUND` when absent).
/// The manager confirms with the user before calling.
#[no_mangle]
pub unsafe extern "C" fn oakengine_library_delete(uuid: *const c_char) -> c_int {
	guard(|| unsafe {
		if uuid.is_null() {
			return Err(Error::Invalid);
		}
		let uuid = read_cstr(uuid);
		if uuid.is_empty() {
			return Err(Error::Invalid);
		}
		crate::storage::backend()
			.delete_project(&library()?, &uuid)
			.map_err(map_err)
	})
}

/// `oakengine_library_rename` — rename the library row `uuid` (the
/// manager's list name; the in-project `projectname` setting is
/// untouched).
#[no_mangle]
pub unsafe extern "C" fn oakengine_library_rename(
	uuid: *const c_char,
	name: *const c_char,
) -> c_int {
	guard(|| unsafe {
		if uuid.is_null() || name.is_null() {
			return Err(Error::Invalid);
		}
		let (uuid, name) = (read_cstr(uuid), read_cstr(name));
		if uuid.is_empty() || name.trim().is_empty() {
			return Err(Error::Invalid);
		}
		crate::storage::backend()
			.rename_project(&library()?, &uuid, name.trim())
			.map_err(map_err)
	})
}

/// `oakengine_library_duplicate` — copy the library row `uuid` (settings,
/// snapshots and the full journal history included) under a fresh uuid,
/// reporting the new row's uuid (buf/size convention on `out_uuid`; the
/// return value is the uuid length, negative on error).
/// `name` is the copy's display name; NULL/empty defaults to
/// `<name> (copy)`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_library_duplicate(
	uuid: *const c_char,
	name: *const c_char,
	out_uuid: *mut c_char,
	out_size: c_int,
) -> c_int {
	guard_int(|| unsafe {
		if uuid.is_null() {
			return Err(Error::Invalid);
		}
		let uuid = read_cstr(uuid);
		if uuid.is_empty() {
			return Err(Error::Invalid);
		}
		let name = read_cstr(name);
		let name = match name.trim() {
			"" => None,
			trimmed => Some(trimmed),
		};
		let info = crate::storage::backend()
			.duplicate_project(&library()?, &uuid, name)
			.map_err(map_err)?;
		Ok(write_string(&info.uuid, out_uuid, out_size))
	})
}

/// `oakengine_library_import` — import a `.ove` / `.otio` / `.fcpxml`
/// project file as a new library row (the file backend parses it, a fresh
/// uuid is assigned, and the first save journals the whole project as one
/// `kind='import'` command). Reports the new row's uuid (buf/size
/// convention on `out_uuid`; the return value is the uuid length,
/// negative on error).
#[no_mangle]
pub unsafe extern "C" fn oakengine_library_import(
	path: *const c_char,
	out_uuid: *mut c_char,
	out_size: c_int,
) -> c_int {
	guard_int(|| unsafe {
		if path.is_null() {
			return Err(Error::Invalid);
		}
		let path = read_cstr(path);
		if path.is_empty() {
			return Err(Error::Invalid);
		}
		let file_uri = StorageUri::parse(&path).map_err(map_err)?;
		let uuid = crate::storage::backend()
			.import_from_file(&library()?, &file_uri)
			.map_err(map_err)?;
		Ok(write_string(&uuid, out_uuid, out_size))
	})
}

/// `oakengine_library_export` — export the library row `uuid` to the file
/// `path`; the format is dispatched by extension through the oakstorage
/// registry (`.ove` / `.ovexml` → ove-xml, `.otio` / `.fcpxml` → the
/// interchange backend). Nothing is written back to the library.
#[no_mangle]
pub unsafe extern "C" fn oakengine_library_export(
	uuid: *const c_char,
	path: *const c_char,
) -> c_int {
	guard(|| unsafe {
		if uuid.is_null() || path.is_null() {
			return Err(Error::Invalid);
		}
		let (uuid, path) = (read_cstr(uuid), read_cstr(path));
		if uuid.is_empty() || path.is_empty() {
			return Err(Error::Invalid);
		}
		let file_uri = StorageUri::parse(&path).map_err(map_err)?;
		if file_uri.scheme != "file" {
			return Err(Error::Invalid);
		}
		let handle = load_handle(&uuid)?;
		let backend = oakstorage::registry::Registry::global()
			.resolve(&file_uri)
			.map_err(map_err)?;
		let result = backend.save(handle, &file_uri, 0).map_err(map_err);
		release(handle);
		result
	})
}

/// `oakengine_project_load_library` — load the library row `uuid` into a
/// fresh project shell (same contract as `oakengine_project_load`: the
/// shell must carry no content). On success the undo stack is cleared, the
/// modified flag is reset, and the project is bound to the library session
/// (the write-through continues the row's journal from its head seq).
#[no_mangle]
pub unsafe extern "C" fn oakengine_project_load_library(
	self_: *mut OakEngineProject,
	uuid: *const c_char,
	err: *mut c_char,
	err_size: c_int,
) -> c_int {
	guard(|| unsafe {
		if self_.is_null() || uuid.is_null() {
			return Err(Error::Invalid);
		}
		let h = crate::handle::unbox(self_)?;
		if !n::oaknode_project_root(h).is_null() {
			return Err(Error::State);
		}
		let uuid = read_cstr(uuid);
		if uuid.is_empty() {
			return Err(Error::Invalid);
		}
		let loaded = match load_handle(&uuid) {
			Ok(handle) => handle,
			Err(e) => {
				write_string(&e.to_string(), err, err_size);
				return Err(e);
			}
		};
		// Swap the loaded content into the caller's shell box, releasing
		// the empty shell handle the box was created with.
		let mut old = (*self_).handle;
		(*self_).handle = loaded;
		n::oaknode_project_free(&mut old);
		crate::undo::oakengine_undo_clear();
		Error::from_module(n::oaknode_project_set_modified(loaded, 0))?;
		crate::storage::bind_project(loaded);
		if !err.is_null() && err_size > 0 {
			*err = 0;
		}
		Ok(())
	})
}
