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
//! The facade owns the process-wide undo stack (module 00 analogue of
//! `EngineCore::undo_stack()`): it is created lazily on first use and
//! lives for the process (mirroring the C++ EngineCore shell, which is
//! also leaked intentionally). The open undo group is facade state too:
//! while a group is open, every command a wrapped family hands to
//! [`push_or_run`] is added to the group instead of the stack.
//!
//! Command creators declared in undo.h but backed by other modules
//! (`oakengine_node_*_command`, `oakengine_track_*_command`,
//! `oakengine_block_*_command`, `oakengine_timeline_*_command`) live in
//! the corresponding family modules, mirroring the C++ capi layout.

use std::ffi::{c_char, c_int, c_void};
use std::sync::{Mutex, OnceLock};

use oakundo::undocommand::{
	command_free, command_init, command_init_multi, command_multi_add_child,
	command_multi_child, command_multi_child_count, command_redo_now, command_undo_now,
};
use oakundo::undostack::{
	undostack_can_redo, undostack_can_undo, undostack_clear, undostack_command_is_done,
	undostack_command_text, undostack_count, undostack_index, undostack_init, undostack_jump,
	undostack_push, undostack_push_pre_executed,
};
use crate::error::{Error, Result};
use crate::handle::{box_handle, free_box, guard, guard_void, unbox, CHandle, OakEngineClipboard};

/// The process-wide undo stack handle (oakundo `OakUndoStack`), created
/// lazily and kept for the process lifetime.
fn global_stack() -> &'static CHandle {
	static STACK: OnceLock<CHandle> = OnceLock::new();
	STACK.get_or_init(|| unsafe { undostack_init() })
}

/// Stable opaque token for `oakengine_undo_handle`: the module stack's
/// `ctx` pointer (never dereferenced by the facade; lives for the
/// process).
fn stack_token() -> *mut c_void {
	global_stack().ctx
}

/// The currently open undo group (a multi command handle) plus its name.
struct OpenGroup {
	/// Multi command handle; owned by this state until end/abort.
	multi: CHandle,
	/// Group label.
	#[allow(dead_code)]
	name: String,
}

static GROUP: Mutex<Option<OpenGroup>> = Mutex::new(None);

fn group_lock() -> std::sync::MutexGuard<'static, Option<OpenGroup>> {
	GROUP.lock().unwrap_or_else(|e| e.into_inner())
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
	let g = group_lock();
	if let Some(group) = g.as_ref() {
		// The module's `oakundo_command_multi_add_child` consumes the
		// child's command value (command_take), so the eager redo must
		// happen on the still-owned handle FIRST — the group takes the
		// already-done command (C++ semantics: add_child + redo_now, net
		// effect identical for the group's reverse-order undo).
		let rc = unsafe { command_redo_now(cmd) };
		if rc != 0 {
			return Err(Error::Module(rc));
		}
		let rc = unsafe { command_multi_add_child(group.multi, cmd) };
		drop(g);
		unsafe { free_box(command_box) };
		return if rc == 0 {
			Ok(())
		} else {
			Err(Error::Module(rc))
		};
	}
	let stack = *global_stack();
	// The module treats a NULL name like an empty label, but an empty Rust
	// String's `as_ptr()` is a DANGLING non-NULL pointer (0x1): the module's
	// `read_name` would strlen it and SIGSEGV. Pass a real NULL instead.
	let label_ptr = if label.is_empty() {
		std::ptr::null()
	} else {
		label.as_ptr() as *const c_char
	};
	let rc = unsafe { undostack_push(stack, cmd, label_ptr) };
	if rc == 0 {
		// Stack took a reference; release ours by freeing the box. The
		// command's redo already ran (plan M13 D2): persist the project.
		unsafe { free_box(command_box) };
		crate::storage::note_command();
		Ok(())
	} else {
		// Push failed (e.g. empty multi): the module deleted the command;
		// release the box shell without touching the (already consumed)
		// handle.
		unsafe { free_box(command_box) };
		Err(Error::Module(rc))
	}
}

/// `oakengine_undo_handle` — borrowed token of the global undo stack
/// (NULL never: the facade creates the stack lazily).
#[no_mangle]
pub extern "C" fn oakengine_undo_handle() -> *mut c_void {
	crate::handle::guard_ptr(|| Ok(stack_token()))
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
	guard(|| {
		let mut g = group_lock();
		if g.is_some() {
			return Err(Error::State);
		}
		let multi = unsafe { command_init_multi() };
		if multi.is_null() {
			return Err(Error::Failed("undo group allocation failed".into()));
		}
		*g = Some(OpenGroup {
			multi,
			name: unsafe { crate::handle::read_cstr(name) },
		});
		Ok(())
	})
}

/// `oakengine_undo_group_end` — close the group and push it as one entry.
/// An empty group is discarded (no undo entry).
#[no_mangle]
pub extern "C" fn oakengine_undo_group_end() -> c_int {
	guard(|| {
		let mut g = group_lock();
		let open = g.take().ok_or(Error::State)?;
		let multi = open.multi;
		let name = open.name;
		drop(g);
		// Same NULL-for-empty convention as `push_or_run`: the module's
		// `read_name` treats NULL like an empty label, while an empty String's
		// dangling `as_ptr()` (0x1) would be strlen'd -> SIGSEGV.
		let name_ptr = if name.is_empty() {
			std::ptr::null()
		} else {
			name.as_ptr() as *const c_char
		};
		// push_pre_executed discards an empty multi command. Either way
		// the stack took (or destroyed) the command; release our own
		// reference to the multi handle.
		let stack = *global_stack();
		let rc = unsafe { undostack_push_pre_executed(stack, multi, name_ptr) };
		let mut multi_handle = multi;
		unsafe { command_free(&mut multi_handle) };
		if rc == 0 {
			// The group's children were redo'd eagerly at push time; the
			// whole group is one command (plan §2: commit at group_end).
			crate::storage::note_command();
			Ok(())
		} else {
			Err(Error::Module(rc))
		}
	})
}

/// `oakengine_undo_group_abort` — undo all executed children and discard
/// the group.
#[no_mangle]
pub extern "C" fn oakengine_undo_group_abort() -> c_int {
	guard(|| {
		let mut g = group_lock();
		let open = g.take().ok_or(Error::State)?;
		drop(g);
		// The multi command itself is never marked done (each child was
		// redo'd eagerly at push time), so `undo_now` on it is a no-op.
		// Undo the executed children individually instead, in reverse
		// insertion order (mirroring the multi's reverse-order undo), each
		// through its own borrowed handle.
		let mut count: c_int = 0;
		let rc = unsafe { command_multi_child_count(open.multi, &mut count) };
		if rc != 0 {
			let mut multi = open.multi;
			unsafe { command_free(&mut multi) };
			return Err(Error::Module(rc));
		}
		for i in (0..count).rev() {
			let mut child = CHandle::null();
			let rc = unsafe { command_multi_child(open.multi, i, &mut child) };
			if rc != 0 {
				let mut multi = open.multi;
				unsafe { command_free(&mut multi) };
				return Err(Error::Module(rc));
			}
			let rc = unsafe { command_undo_now(child) };
			// The child handle is borrowed (owns:false): release only its
			// shell — the child value lives on in the multi until the multi
			// itself is freed below.
			unsafe { command_free(&mut child) };
			if rc != 0 {
				let mut multi = open.multi;
				unsafe { command_free(&mut multi) };
				return Err(Error::Module(rc));
			}
		}
		let mut multi = open.multi;
		unsafe { command_free(&mut multi) };
		Ok(())
	})
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
	crate::handle::guard_i64(|| unsafe {
		let mut count: i64 = 0;
		Error::from_module(undostack_count(*global_stack(), &mut count))?;
		Ok(count)
	})
}

/// `oakengine_undo_index` — current position in the history.
#[no_mangle]
pub extern "C" fn oakengine_undo_index() -> i64 {
	crate::handle::guard_i64(|| unsafe {
		let mut index: i64 = 0;
		Error::from_module(undostack_index(*global_stack(), &mut index))?;
		Ok(index)
	})
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
	crate::handle::guard_int(|| unsafe {
		let rc = undostack_command_text(*global_stack(), row, buf, buf_size);
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
	crate::handle::guard_int(|| unsafe {
		let mut value: c_int = 0;
		Error::from_module(undostack_command_is_done(
			*global_stack(),
			row,
			&mut value,
		))?;
		Ok(value)
	})
}

/// `oakengine_undo_jump` — undo/redo until the done-command count equals
/// `index`. On success the bound projects are written through (the jump
/// executed the undo/redo callbacks that mutated them).
#[no_mangle]
pub extern "C" fn oakengine_undo_jump(index: i64) -> c_int {
	let rc = guard(|| unsafe { Error::from_module(undostack_jump(*global_stack(), index)) });
	if rc == crate::error::OAKENGINE_OK {
		crate::storage::note_command();
	}
	rc
}

/// `oakengine_undo_clear` — delete all commands and push the fresh
/// "New/Open Project" empty command.
#[no_mangle]
pub extern "C" fn oakengine_undo_clear() -> c_int {
	guard(|| unsafe { Error::from_module(undostack_clear(*global_stack())) })
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
	crate::handle::guard_int(|| unsafe {
		let mut value: c_int = 0;
		Error::from_module(undostack_can_undo(*global_stack(), &mut value))?;
		Ok(value)
	})
}

/// `oakengine_undo_can_redo` — 1/0.
#[no_mangle]
pub extern "C" fn oakengine_undo_can_redo() -> c_int {
	crate::handle::guard_int(|| unsafe {
		let mut value: c_int = 0;
		Error::from_module(undostack_can_redo(*global_stack(), &mut value))?;
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
