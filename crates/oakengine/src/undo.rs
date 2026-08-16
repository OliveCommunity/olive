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

//! `engine/include/oakengine/undo.h` — the process-wide undo stack,
//! undo groups and command lifecycle over the oakundo module.
//!
//! The process-wide stack, the open undo group and the "command
//! recorded" notification now live in [`oakundo::global`] (M14 R1: sunk
//! from this facade); every export here is a thin forward that only adds
//! the engine's box/unbox, buf/size and error-code conventions.
//!
//! Command creators declared in undo.h but backed by other modules
//! (`oakengine_node_*_command`, `oakengine_track_*_command`,
//! `oakengine_block_*_command`, `oakengine_timeline_*_command`) live in
//! the corresponding family modules, mirroring the C++ capi layout.

use std::ffi::{c_char, c_int, c_void};

use oakundo::undocommand::{
	command_free, command_init, command_init_multi, command_multi_add_child,
	command_multi_child_count, command_redo_now, command_undo_now,
};

use crate::error::{Error, Result};
use crate::handle::{box_handle, free_box, guard, guard_void, unbox, OakEngineClipboard};

/// Map an oakundo error onto the facade error space for the GROUP
/// functions: `State` (no group open / already open) and the allocation
/// failure map to the facade's own codes; every other oakundo code passes
/// through as a module code (the numeric value is preserved).
fn map_group_err(e: oakundo::error::Error) -> Error {
	match e {
		oakundo::error::Error::State => Error::State,
		oakundo::error::Error::NoMem => Error::NoMem,
		oakundo::error::Error::Failed(s) => Error::Failed(s),
		other => Error::Module(other.code()),
	}
}

/// Map an oakundo error onto the facade error space for the STACK
/// queries: every code passes through untranslated as a module code (the
/// facade contract says module codes cross the boundary verbatim).
fn map_stack_err(e: oakundo::error::Error) -> Error {
	Error::Module(e.code())
}

/// Push `command` onto the stack, add it to the open group, or run it
/// directly — whichever applies (module 00 analogue of the C++ capi's
/// `oakengine_undo_push_or_run`). `command_box` is consumed.
///
/// # Safety
/// `command_box` must be a live box created by a facade command creator.
pub(crate) unsafe fn push_or_run(
	command_box: *mut OakEngineClipboard,
	name: *const c_char,
) -> Result<()> {
	let cmd = unsafe { unbox(command_box)? };
	let label = unsafe { crate::handle::read_cstr(name) };
	let rc = unsafe { oakundo::global::push_or_run(cmd, &label) };
	// The stack/multi took (or rejected) the command value; release the box
	// shell either way (the command is destroyed with the stack/multi, or
	// with this shell when the push failed and nobody took it).
	unsafe { free_box(command_box) };
	if rc == 0 {
		Ok(())
	} else {
		Err(Error::Module(rc))
	}
}

/// `oakengine_undo_handle` — borrowed token of the global undo stack
/// (NULL never: the module creates the stack lazily).
#[no_mangle]
pub extern "C" fn oakengine_undo_handle() -> *mut c_void {
	crate::handle::guard_ptr(|| Ok(oakundo::global::stack_token()))
}

/// `oakengine_undo_push` — push `command` onto the stack and execute its
/// redo (or add it to the open group). Takes ownership of `command`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_undo_push(command: *mut c_void, name: *const c_char) -> c_int {
	guard(|| unsafe {
		if command.is_null() {
			return Err(Error::Invalid);
		}
		push_or_run(command.cast::<OakEngineClipboard>(), name)
	})
}

/// `oakengine_undo_group_begin` — start collecting commands into a group.
#[no_mangle]
pub extern "C" fn oakengine_undo_group_begin(name: *const c_char) -> c_int {
	guard(|| oakundo::global::group_begin(name).map_err(map_group_err))
}

/// `oakengine_undo_group_end` — close the group and push it as one entry.
/// An empty group is discarded (no undo entry).
#[no_mangle]
pub extern "C" fn oakengine_undo_group_end() -> c_int {
	guard(|| oakundo::global::group_end().map_err(map_group_err))
}

/// `oakengine_undo_group_abort` — undo all executed children and discard
/// the group.
#[no_mangle]
pub extern "C" fn oakengine_undo_group_abort() -> c_int {
	guard(|| oakundo::global::group_abort().map_err(map_group_err))
}

/// `oakengine_undo_command_redo_now` — execute the redo of `command`
/// without taking ownership.
#[no_mangle]
pub unsafe extern "C" fn oakengine_undo_command_redo_now(command: *mut c_void) -> c_int {
	guard(|| unsafe {
		let cmd = unbox(command.cast::<OakEngineClipboard>())?;
		Error::from_module(command_redo_now(cmd))
	})
}

/// `oakengine_undo_command_undo_now` — execute the undo of `command`
/// without taking ownership.
#[no_mangle]
pub unsafe extern "C" fn oakengine_undo_command_undo_now(command: *mut c_void) -> c_int {
	guard(|| unsafe {
		let cmd = unbox(command.cast::<OakEngineClipboard>())?;
		Error::from_module(command_undo_now(cmd))
	})
}

/// Engine-side callback types for app-defined undo commands
/// (`engine/include/oakengine/undo.h`).
type UndoRedoFn = unsafe extern "C" fn(userdata: *mut c_void);
type UndoFreeFn = unsafe extern "C" fn(userdata: *mut c_void);

/// `oakengine_undo_command_create` — create an app-defined undo command
/// backed by C callbacks. Takes ownership of `userdata`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_undo_command_create(
	name: *const c_char,
	redo: Option<UndoRedoFn>,
	undo: Option<UndoRedoFn>,
	free_fn: Option<UndoFreeFn>,
	userdata: *mut c_void,
) -> *mut c_void {
	crate::handle::guard_ptr(|| unsafe {
		let _ = crate::handle::read_cstr(name);
		let vtable = oakundo::undocommand::OakUndoCommandVtable {
			redo,
			undo,
			free_fn,
		};
		let cmd = command_init(&vtable, userdata);
		if cmd.is_null() {
			return Ok(std::ptr::null_mut());
		}
		Ok(box_handle::<OakEngineClipboard>(cmd).cast())
	})
}

/// `oakengine_undo_command_create_multi` — create an empty
/// MultiUndoCommand as an opaque command pointer.
#[no_mangle]
pub extern "C" fn oakengine_undo_command_create_multi() -> *mut c_void {
	crate::handle::guard_ptr(|| {
		let cmd = unsafe { command_init_multi() };
		if cmd.is_null() {
			return Ok(std::ptr::null_mut());
		}
		Ok(box_handle::<OakEngineClipboard>(cmd).cast())
	})
}

/// `oakengine_undo_command_multi_add_child` — add `child` to `multi`
/// (the multi takes one reference; `child`'s box is consumed).
#[no_mangle]
pub unsafe extern "C" fn oakengine_undo_command_multi_add_child(
	multi: *mut c_void,
	child: *mut c_void,
) -> c_int {
	guard(|| unsafe {
		if multi.is_null() || child.is_null() {
			return Err(Error::Invalid);
		}
		let m = unbox(multi.cast::<OakEngineClipboard>())?;
		let c = unbox(child.cast::<OakEngineClipboard>())?;
		let rc = command_multi_add_child(m, c);
		free_box(child.cast::<OakEngineClipboard>());
		if rc == 0 {
			Ok(())
		} else {
			Err(Error::Module(rc))
		}
	})
}

/// `oakengine_undo_command_multi_child_count` — children of `multi`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_undo_command_multi_child_count(multi: *mut c_void) -> c_int {
	crate::handle::guard_int(|| unsafe {
		let m = unbox(multi.cast::<OakEngineClipboard>())?;
		let mut count: c_int = 0;
		Error::from_module(command_multi_child_count(m, &mut count))?;
		Ok(count)
	})
}

/// `oakengine_undo_command_free` — destroy a command without pushing it.
#[no_mangle]
pub unsafe extern "C" fn oakengine_undo_command_free(command: *mut c_void) {
	guard_void(|| unsafe {
		free_box(command.cast::<OakEngineClipboard>());
	})
}

/// `oakengine_undo_count` — total number of history rows.
#[no_mangle]
pub extern "C" fn oakengine_undo_count() -> i64 {
	crate::handle::guard_i64(|| oakundo::global::count().map_err(map_stack_err))
}

/// `oakengine_undo_index` — current position in the history.
#[no_mangle]
pub extern "C" fn oakengine_undo_index() -> i64 {
	crate::handle::guard_i64(|| oakundo::global::index().map_err(map_stack_err))
}

/// `oakengine_undo_command_text` — label of the row at `row`
/// (buf/size; OAKENGINE_E_NOT_FOUND for an invalid row).
#[no_mangle]
pub unsafe extern "C" fn oakengine_undo_command_text(
	row: i64,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	// The oakundo getter is itself two-stage: it reports the required
	// size when `buf` is NULL/too small and copies otherwise, so the
	// module return value is returned verbatim (guarded against panic),
	// converted to the engine's length-excluding-NUL convention.
	crate::handle::guard_int(|| {
		let rc = oakundo::global::command_text(row, buf, buf_size);
		if rc < 0 {
			Err(Error::Module(rc))
		} else {
			Ok(crate::handle::string_result(rc))
		}
	})
}

/// `oakengine_undo_command_is_done` — 1 when the row is done, 0 when
/// undone, OAKENGINE_E_NOT_FOUND for an invalid row.
#[no_mangle]
pub extern "C" fn oakengine_undo_command_is_done(row: i64) -> c_int {
	crate::handle::guard_int(|| {
		let mut value: c_int = 0;
		oakundo::global::command_is_done(row, &mut value).map_err(map_stack_err)?;
		Ok(value)
	})
}

/// `oakengine_undo_jump` — undo/redo until the done-command count equals
/// `index`. On success the bound projects are written through (the jump
/// executed the undo/redo callbacks that mutated them) — the module's
/// command observers fire the write-through subscribers.
#[no_mangle]
pub extern "C" fn oakengine_undo_jump(index: i64) -> c_int {
	guard(|| oakundo::global::jump(index).map_err(map_stack_err))
}

/// `oakengine_undo_clear` — delete all commands and push the fresh
/// "New/Open Project" empty command.
#[no_mangle]
pub extern "C" fn oakengine_undo_clear() -> c_int {
	guard(|| oakundo::global::clear().map_err(map_stack_err))
}

/// `oakengine_undo_update_actions` — no-op: the QAction members were
/// removed in the de-Qt pass (see notes.md), the app builds its own
/// undo/redo actions from `oakengine_undo_can_undo/redo`.
#[no_mangle]
pub extern "C" fn oakengine_undo_update_actions() -> c_int {
	crate::error::OAKENGINE_OK
}

/// `oakengine_undo_can_undo` — 1/0.
#[no_mangle]
pub extern "C" fn oakengine_undo_can_undo() -> c_int {
	crate::handle::guard_int(|| {
		let mut value: c_int = 0;
		oakundo::global::can_undo(&mut value).map_err(map_stack_err)?;
		Ok(value)
	})
}

/// `oakengine_undo_can_redo` — 1/0.
#[no_mangle]
pub extern "C" fn oakengine_undo_can_redo() -> c_int {
	crate::handle::guard_int(|| {
		let mut value: c_int = 0;
		oakundo::global::can_redo(&mut value).map_err(map_stack_err)?;
		Ok(value)
	})
}

/// `oakengine_undo_undo_action` — Qt leftover: the de-Qt module world has
/// no QAction; returns NULL. The app builds its own action.
#[no_mangle]
pub extern "C" fn oakengine_undo_undo_action() -> *mut c_void {
	std::ptr::null_mut()
}

/// `oakengine_undo_redo_action` — Qt leftover; returns NULL (see
/// `oakengine_undo_undo_action`).
#[no_mangle]
pub extern "C" fn oakengine_undo_redo_action() -> *mut c_void {
	std::ptr::null_mut()
}
