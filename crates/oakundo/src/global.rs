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

//! The process-wide undo stack, undo groups and the command-success
//! observer hook (M14 R1: sunk from the engine facade's `undo.rs`).
//!
//! The facade used to own the process-wide stack (the module-00 analogue
//! of `EngineCore::undo_stack()`), the open undo group and the
//! write-through notification as facade state; all of it is process state,
//! so this module holds it and the facade forwards. The observer registry
//! lets downstream modules (the oakstorage write-through session manager)
//! subscribe to "a command was recorded" notifications without a facade
//! round-trip.

use std::ffi::{c_char, c_int, c_void};
use std::sync::{Mutex, OnceLock};

use crate::error::{Error, Result};
use crate::handle::CHandle;
use crate::undocommand::{
	command_free, command_from_owned, command_init_multi, command_multi_add_child,
	command_multi_child, command_multi_child_count, command_redo_now, command_undo_now,
	UndoCommand,
};
use crate::undostack::{
	undostack_can_redo, undostack_can_undo, undostack_clear, undostack_command_is_done,
	undostack_command_text, undostack_count, undostack_index, undostack_init, undostack_jump,
	undostack_push, undostack_push_pre_executed,
};

/// The process-wide undo stack handle (`OakUndoStack`), created lazily
/// on first use and kept for the process lifetime.
fn global_stack() -> &'static CHandle {
	static STACK: OnceLock<CHandle> = OnceLock::new();
	STACK.get_or_init(|| undostack_init())
}

/// Stable opaque token for the engine's `oakengine_undo_handle` export:
/// the stack handle's `ctx` pointer (never dereferenced by callers; lives
/// for the process).
pub fn stack_token() -> *mut c_void {
	global_stack().ctx
}

/// Borrowed copy of the process-wide stack handle (for the module-level
/// queries below).
fn stack() -> CHandle {
	*global_stack()
}

// ---------------------------------------------------------------------------
// Command-success observers
// ---------------------------------------------------------------------------

/// A callback invoked after a command is successfully recorded on the
/// process-wide stack (a stack push, a group end or a jump).
pub type CommandObserver = fn();

static OBSERVERS: OnceLock<Mutex<Vec<CommandObserver>>> = OnceLock::new();

fn observers() -> &'static Mutex<Vec<CommandObserver>> {
	OBSERVERS.get_or_init(|| Mutex::new(Vec::new()))
}

/// Register a command-success observer. The callback runs after the stack
/// mutation is complete, outside the stack/group locks; multiple observers
/// are supported and run in registration order. There is no un-registration
/// API — observers are process-lifetime, mirroring the global stack itself.
pub fn add_observer(f: CommandObserver) {
	observers().lock().unwrap_or_else(|e| e.into_inner()).push(f);
}

/// Invoke every registered observer (called on the stack-path push, the
/// group end and the jump success).
fn notify_observers() {
	let callbacks: Vec<CommandObserver> = observers()
		.lock()
		.unwrap_or_else(|e| e.into_inner())
		.clone();
	for cb in callbacks {
		cb();
	}
}

// ---------------------------------------------------------------------------
// Undo group
// ---------------------------------------------------------------------------

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

/// Start collecting commands into a group. [`Error::State`] when a group
/// is already open.
pub fn group_begin(name: &str) -> Result<()> {
	let mut g = group_lock();
	if g.is_some() {
		return Err(Error::State);
	}
	let multi = command_init_multi();
	if multi.is_null() {
		return Err(Error::NoMem);
	}
	*g = Some(OpenGroup {
		multi,
		name: name.to_string(),
	});
	Ok(())
}

/// Close the group and push it as one entry. An empty group is discarded
/// (no undo entry). On success the command observers fire. [`Error::State`]
/// when no group is open.
pub fn group_end() -> Result<()> {
	let mut g = group_lock();
	let open = g.take().ok_or(Error::State)?;
	let multi = open.multi;
	let name = open.name;
	drop(g);
	// Same NULL-for-empty convention as [`push_or_run`]: the module's
	// `read_name` treats NULL like an empty label, while an empty String's
	// dangling `as_ptr()` (0x1) would be strlen'd -> SIGSEGV.
	let name_ptr = if name.is_empty() {
		std::ptr::null()
	} else {
		name.as_ptr() as *const c_char
	};
	// push_pre_executed discards an empty multi command. Either way the
	// stack took (or destroyed) the command; release our own reference to
	// the multi handle.
	let rc = undostack_push_pre_executed(stack(), multi, name_ptr);
	let mut multi_handle = multi;
	command_free(&mut multi_handle);
	if rc == 0 {
		// The group's children were redo'd eagerly at push time; the whole
		// group is one command (commit at group_end).
		notify_observers();
		Ok(())
	} else {
		Err(Error::from_code(rc))
	}
}

/// Undo all executed children and discard the group. [`Error::State`] when
/// no group is open. No observers fire (nothing was recorded).
pub fn group_abort() -> Result<()> {
	let mut g = group_lock();
	let open = g.take().ok_or(Error::State)?;
	drop(g);
	// The multi command itself is never marked done (each child was
	// redo'd eagerly at push time), so `undo_now` on it is a no-op. Undo
	// the executed children individually instead, in reverse insertion
	// order (mirroring the multi's reverse-order undo), each through its
	// own borrowed handle.
	let mut count: c_int = 0;
	let rc = command_multi_child_count(open.multi, &mut count);
	if rc != 0 {
		let mut multi = open.multi;
		command_free(&mut multi);
		return Err(Error::from_code(rc));
	}
	for i in (0..count).rev() {
		let mut child = CHandle::null();
		let rc = command_multi_child(open.multi, i, &mut child);
		if rc != 0 {
			let mut multi = open.multi;
			command_free(&mut multi);
			return Err(Error::from_code(rc));
		}
		let rc = command_undo_now(child);
		// The child handle is borrowed (owns:false): release only its shell
		// — the child value lives on in the multi until the multi itself is
		// freed below.
		command_free(&mut child);
		if rc != 0 {
			let mut multi = open.multi;
			command_free(&mut multi);
			return Err(Error::from_code(rc));
		}
	}
	let mut multi = open.multi;
	command_free(&mut multi);
	Ok(())
}

/// Push `command` onto the stack and execute its redo (or add it to the
/// open group). `command` is consumed: the stack/multi takes the command
/// value, leaving the caller's handle as a non-owning shell that still
/// needs its own release. Returns 0 on success, a module error code
/// otherwise. Command observers fire only when the command was recorded on
/// the STACK — a group child joins the group, and the group itself
/// notifies at [`group_end`].
pub fn push_or_run(command: CHandle, name: &str) -> c_int {
	let g = group_lock();
	if let Some(group) = g.as_ref() {
		// The module's `command_multi_add_child` consumes the child's
		// command value (command_take), so the eager redo must happen on the
		// still-owned handle FIRST — the group takes the already-done
		// command (C++ semantics: add_child + redo_now, net effect identical
		// for the group's reverse-order undo).
		let rc = command_redo_now(command);
		if rc != 0 {
			return rc;
		}
		let rc = command_multi_add_child(group.multi, command);
		drop(g);
		return rc;
	}
	let stack = stack();
	// The module treats a NULL name like an empty label, but an empty Rust
	// String's `as_ptr()` is a DANGLING non-NULL pointer (0x1): the module's
	// `read_name` would strlen it and SIGSEGV. Pass a real NULL instead.
	let label_ptr = if name.is_empty() {
		std::ptr::null()
	} else {
		name.as_ptr() as *const c_char
	};
	let rc = undostack_push(stack, command, label_ptr);
	if rc == 0 {
		// The stack took a reference; the command's redo already ran (plan
		// M13 D2): persist the write-through subscribers.
		notify_observers();
	}
	rc
}

// ---------------------------------------------------------------------------
// Safe value-typed surface (M14 R3)
//
// The direct-rlib frontends (the app) hold module [`UndoCommand`] values,
// not CHandles. The functions below are the safe twins of the handle-based
// API above; the handle marshalling they perform stays inside this crate.
// ---------------------------------------------------------------------------

/// Push `command` onto the process-wide stack (redo then record), or into
/// the open group. On a stack record the command observers fire (the
/// oakstorage write-through persists the edit).
pub fn push(command: UndoCommand, name: &str) -> Result<()> {
	// SAFETY: `command_from_owned` boxes the value; `push_or_run` takes the
	// value out of the handle (stack push or group child), and
	// `command_free` releases the remaining non-owning shell (dropping the
	// command itself when nobody took it).
	let mut handle = unsafe { command_from_owned(command) };
	if handle.is_null() {
		return Err(Error::NoMem);
	}
	let rc = push_or_run(handle, name);
	command_free(&mut handle);
	if rc == 0 {
		Ok(())
	} else {
		Err(Error::from_code(rc))
	}
}

/// Step the process-wide stack back one entry (no-op at the bottom). On
/// success the command observers fire, persisting the reverted state.
pub fn undo() -> Result<()> {
	let i = index()?;
	jump(i - 1)
}

/// Step the process-wide stack forward one entry (no-op at the top). On
/// success the command observers fire.
pub fn redo() -> Result<()> {
	let i = index()?;
	jump(i + 1)
}

/// Whether the process-wide stack has an entry to undo.
pub fn undoable() -> bool {
	let mut v: c_int = 0;
	can_undo(&mut v).is_ok() && v != 0
}

/// Whether the process-wide stack has an entry to redo.
pub fn redoable() -> bool {
	let mut v: c_int = 0;
	can_redo(&mut v).is_ok() && v != 0
}

// ---------------------------------------------------------------------------
// Stack queries and mutations
// ---------------------------------------------------------------------------

/// Total number of history rows.
pub fn count() -> Result<i64> {
	let mut c: i64 = 0;
	let rc = undostack_count(stack(), &mut c);
	if rc == 0 {
		Ok(c)
	} else {
		Err(Error::from_code(rc))
	}
}

/// Current position in the history (done-command count).
pub fn index() -> Result<i64> {
	let mut i: i64 = 0;
	let rc = undostack_index(stack(), &mut i);
	if rc == 0 {
		Ok(i)
	} else {
		Err(Error::from_code(rc))
	}
}

/// Whether an undo is possible (1/0 via `out_value`; a module error code
/// otherwise).
pub fn can_undo(out_value: *mut c_int) -> Result<()> {
	let rc = undostack_can_undo(stack(), out_value);
	if rc == 0 {
		Ok(())
	} else {
		Err(Error::from_code(rc))
	}
}

/// Whether a redo is possible (1/0 via `out_value`; a module error code
/// otherwise).
pub fn can_redo(out_value: *mut c_int) -> Result<()> {
	let rc = undostack_can_redo(stack(), out_value);
	if rc == 0 {
		Ok(())
	} else {
		Err(Error::from_code(rc))
	}
}

/// Undo/redo until the done-command count equals `index`. On success the
/// bound projects are written through (the jump executed the undo/redo
/// callbacks that mutated them) via the command observers.
pub fn jump(index: i64) -> Result<()> {
	let rc = undostack_jump(stack(), index);
	if rc == 0 {
		notify_observers();
		Ok(())
	} else {
		Err(Error::from_code(rc))
	}
}

/// Delete all commands and push the fresh "New/Open Project" empty command.
pub fn clear() -> Result<()> {
	let rc = undostack_clear(stack());
	if rc == 0 {
		Ok(())
	} else {
		Err(Error::from_code(rc))
	}
}

/// Two-stage label getter for the row at `row` (see
/// [`crate::undostack::undostack_command_text`]): returns the required
/// size including the NUL, or a module error code.
pub fn command_text(row: i64, buf: *mut c_char, buf_size: c_int) -> c_int {
	undostack_command_text(stack(), row, buf, buf_size)
}

/// Whether the row at `row` is done (1/0 via `out_value`; a module error
/// code otherwise — `-20004` for an out-of-range row).
pub fn command_is_done(row: i64, out_value: *mut c_int) -> Result<()> {
	let rc = undostack_command_is_done(stack(), row, out_value);
	if rc == 0 {
		Ok(())
	} else {
		Err(Error::from_code(rc))
	}
}

#[cfg(test)]
mod tests {
	use super::*;

	/// A no-op observer that counts its invocations.
	static COUNT: std::sync::atomic::AtomicI32 = std::sync::atomic::AtomicI32::new(0);
	fn counter() {
		COUNT.fetch_add(1, std::sync::atomic::Ordering::SeqCst);
	}

	/// A second counting observer (the observers are process-lifetime, so a
	/// test must not share `COUNT` with the lifecycle test above).
	static COUNT2: std::sync::atomic::AtomicI32 = std::sync::atomic::AtomicI32::new(0);
	fn counter2() {
		COUNT2.fetch_add(1, std::sync::atomic::Ordering::SeqCst);
	}

	/// The stack/group/observer state is process-wide: every test here runs
	/// serially under this lock, mirroring the facade's GLOBAL_STACK_LOCK
	/// pattern.
	static LOCK: Mutex<()> = Mutex::new(());

	fn vtable_command() -> CHandle {
		use crate::undocommand::{command_init, OakUndoCommandVtable};
		command_init(
			&OakUndoCommandVtable {
				redo: None,
				undo: None,
				free_fn: None,
			},
			std::ptr::null_mut(),
		)
	}

	#[test]
	fn stack_and_group_lifecycle() {
		let _g = LOCK.lock().unwrap_or_else(|e| e.into_inner());
		COUNT.store(0, std::sync::atomic::Ordering::SeqCst);
		add_observer(counter);

		assert!(clear().is_ok());
		assert_eq!(count().unwrap(), 1);
		assert_eq!(index().unwrap(), 1);
		let mut v: c_int = 1;
		assert!(can_undo(&mut v).is_ok());
		assert_eq!(v, 0);

		// Push fires the observer once.
		let cmd = vtable_command();
		assert_eq!(push_or_run(cmd, "alpha"), 0);
		assert_eq!(count().unwrap(), 2);
		assert_eq!(COUNT.load(std::sync::atomic::Ordering::SeqCst), 1);

		// Group begin/end fires the observer once at end.
		assert!(group_begin("grouped").is_ok());
		assert!(group_begin("again").is_err()); // State
		let c1 = vtable_command();
		let c2 = vtable_command();
		assert_eq!(push_or_run(c1, "c1"), 0);
		assert_eq!(push_or_run(c2, "c2"), 0);
		// Children joined the group: no observer fire yet.
		assert_eq!(COUNT.load(std::sync::atomic::Ordering::SeqCst), 1);
		assert_eq!(count().unwrap(), 2);
		assert!(group_end().is_ok());
		assert_eq!(count().unwrap(), 3);
		assert_eq!(COUNT.load(std::sync::atomic::Ordering::SeqCst), 2);

		// Abort fires nothing.
		assert!(group_begin("abort").is_ok());
		let c3 = vtable_command();
		assert_eq!(push_or_run(c3, "c3"), 0);
		assert!(group_abort().is_ok());
		assert_eq!(count().unwrap(), 3);
		assert_eq!(COUNT.load(std::sync::atomic::Ordering::SeqCst), 2);

		// End/abort with no group open: State.
		assert!(group_end().is_err());
		assert!(group_abort().is_err());

		assert!(clear().is_ok());
	}

	/// The safe value-typed surface (M14 R3): `push` redoes and records a
	/// closure command, `undo`/`redo` step the stack, and the observers
	/// fire on every recorded mutation.
	#[test]
	fn value_push_and_undo_redo() {
		let _g = LOCK.lock().unwrap_or_else(|e| e.into_inner());
		COUNT2.store(0, std::sync::atomic::Ordering::SeqCst);
		add_observer(counter2);

		assert!(clear().is_ok());
		assert!(!undoable());
		assert!(!redoable());

		let state = std::sync::Arc::new(std::sync::atomic::AtomicI32::new(0));
		let (r, u) = (state.clone(), state.clone());
		let cmd = UndoCommand::from_closures(
			move || {
				r.fetch_add(1, std::sync::atomic::Ordering::SeqCst);
			},
			move || {
				u.fetch_sub(1, std::sync::atomic::Ordering::SeqCst);
			},
		);
		push(cmd, "bump").unwrap();
		assert_eq!(state.load(std::sync::atomic::Ordering::SeqCst), 1);
		assert!(undoable());
		assert_eq!(COUNT2.load(std::sync::atomic::Ordering::SeqCst), 1);

		undo().unwrap();
		assert_eq!(state.load(std::sync::atomic::Ordering::SeqCst), 0);
		assert!(!undoable());
		assert!(redoable());

		redo().unwrap();
		assert_eq!(state.load(std::sync::atomic::Ordering::SeqCst), 1);
		assert!(undoable());
		assert!(!redoable());

		assert!(clear().is_ok());
	}
}
