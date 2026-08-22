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
//! observer hook.
//!
//! The retired engine facade used to own the process-wide stack (the
//! module-00 analogue of `EngineCore::undo_stack()`), the open undo group
//! and the write-through notification as facade state; all of it is
//! process state, so this module holds it directly. The observer registry
//! lets downstream modules (the oakstorage write-through session manager)
//! subscribe to "a command was recorded" notifications.
//!
//! Everything here is Rust-typed: the process-wide stack is a plain
//! `static Mutex<UndoStack>` and the open group holds an
//! [`UndoCommand`] value. The former `CHandle`-marshalling entry point
//! (`push_or_run`) and the raw-pointer out-parameter queries
//! (`can_undo(out)` / `command_text(buf, size)` / `command_is_done(out)`)
//! were deleted with the C ABI — the same operations are exposed as
//! value-typed functions below.

use std::sync::{Mutex, OnceLock};

use crate::error::Result;
use crate::undocommand::UndoCommand;
use crate::undostack::UndoStack;

/// The process-wide undo stack, created lazily on first use and kept for
/// the process lifetime.
fn global_stack() -> &'static Mutex<UndoStack> {
	static STACK: OnceLock<Mutex<UndoStack>> = OnceLock::new();
	STACK.get_or_init(|| Mutex::new(UndoStack::new()))
}

/// Run `f` on the process-wide stack; a poisoned mutex is recovered (its
/// inner value is still valid).
fn with_stack<R>(f: impl FnOnce(&mut UndoStack) -> Result<R>) -> Result<R> {
	let mut guard = global_stack().lock().unwrap_or_else(|e| e.into_inner());
	f(&mut guard)
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

/// The currently open undo group (a multi command value) plus its name.
struct OpenGroup {
	/// Multi command value; owned by this state until end/abort.
	multi: UndoCommand,
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
		return Err(crate::error::Error::State);
	}
	*g = Some(OpenGroup {
		multi: UndoCommand::multi(),
		name: name.to_string(),
	});
	Ok(())
}

/// Close the group and push it as one entry. An empty group is discarded
/// (no undo entry). On success the command observers fire. [`Error::State`]
/// when no group is open.
pub fn group_end() -> Result<()> {
	let mut g = group_lock();
	let open = g.take().ok_or(crate::error::Error::State)?;
	let multi = open.multi;
	let name = open.name;
	drop(g);
	// push_pre_executed discards an empty multi command; either way the
	// stack takes (or destroys) the command value.
	with_stack(|s| {
		s.push_pre_executed(multi, &name);
		Ok(())
	})?;
	// The group's children were redo'd eagerly at push time; the whole
	// group is one command (commit at group_end).
	notify_observers();
	Ok(())
}

/// Undo all executed children and discard the group. [`Error::State`] when
/// no group is open. No observers fire (nothing was recorded).
pub fn group_abort() -> Result<()> {
	let mut g = group_lock();
	let open = g.take().ok_or(crate::error::Error::State)?;
	let mut multi = open.multi;
	drop(g);
	// The multi command itself is never marked done (each child was
	// redo'd eagerly at push time), so `undo_now` on it is a no-op. Undo
	// the executed children individually instead, in reverse insertion
	// order (mirroring the multi's reverse-order undo).
	let count = multi.multi_child_count();
	for i in (0..count).rev() {
		multi.multi_child_mut(i)?.undo_now();
	}
	// The multi command value drops here, freeing the children.
	Ok(())
}

/// Push `command` onto the process-wide stack (redo then record), or into
/// the open group as an already-done child. On a stack record the command
/// observers fire (the oakstorage write-through persists the edit).
pub fn push(command: UndoCommand, name: &str) -> Result<()> {
	let mut g = group_lock();
	if let Some(group) = g.as_mut() {
		// The group takes the command as an already-done child: the eager
		// redo must run BEFORE the child joins the group (C++ semantics:
		// add_child + redo_now, net effect identical for the group's
		// reverse-order undo).
		let mut command = command;
		command.redo_now();
		group.multi.multi_add_child(command);
		return Ok(());
	}
	drop(g);
	let mut guard = global_stack().lock().unwrap_or_else(|e| e.into_inner());
	guard.push(command, name);
	drop(guard);
	// The stack took the command; its redo already ran: persist the
	// write-through subscribers.
	notify_observers();
	Ok(())
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
	can_undo()
}

/// Whether the process-wide stack has an entry to redo.
pub fn redoable() -> bool {
	can_redo()
}

// ---------------------------------------------------------------------------
// Stack queries and mutations
// ---------------------------------------------------------------------------

/// Total number of history rows.
pub fn count() -> Result<i64> {
	with_stack(|s| Ok(s.command_count()))
}

/// Current position in the history (done-command count).
pub fn index() -> Result<i64> {
	with_stack(|s| Ok(s.done_count()))
}

/// Whether an undo is possible.
pub fn can_undo() -> bool {
	with_stack(|s| Ok(s.can_undo())).unwrap_or(false)
}

/// Whether a redo is possible.
pub fn can_redo() -> bool {
	with_stack(|s| Ok(s.can_redo())).unwrap_or(false)
}

/// Undo/redo until the done-command count equals `index`. On success the
/// bound projects are written through (the jump executed the undo/redo
/// callbacks that mutated them) via the command observers.
pub fn jump(index: i64) -> Result<()> {
	with_stack(|s| {
		s.jump(index);
		Ok(())
	})?;
	notify_observers();
	Ok(())
}

/// Delete all commands and push the fresh "New/Open Project" empty command.
pub fn clear() -> Result<()> {
	with_stack(|s| {
		s.clear();
		Ok(())
	})
}

/// The user-visible label of the row at `row` (the safe replacement for
/// the C-ABI two-stage `command_text(buf, size)`; the history panel's row
/// query). `NotFound` for an out-of-range row.
pub fn command_name(row: i64) -> Result<String> {
	with_stack(|s| s.command_name(row).map(|n| n.to_string()))
}

/// Whether the row at `row` is done (the safe replacement for the C-ABI
/// `command_is_done(out)`; the history panel's gray-row query).
pub fn command_done(row: i64) -> Result<bool> {
	with_stack(|s| s.command_is_done(row))
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
	/// serially under this lock.
	static LOCK: Mutex<()> = Mutex::new(());

	/// A no-op command value.
	fn noop_command() -> UndoCommand {
		UndoCommand::from_closures(|| {}, || {})
	}

	#[test]
	fn stack_and_group_lifecycle() {
		let _g = LOCK.lock().unwrap_or_else(|e| e.into_inner());
		COUNT.store(0, std::sync::atomic::Ordering::SeqCst);
		add_observer(counter);

		assert!(clear().is_ok());
		assert_eq!(count().unwrap(), 1);
		assert_eq!(index().unwrap(), 1);
		assert!(!can_undo());

		// Push fires the observer once.
		push(noop_command(), "alpha").unwrap();
		assert_eq!(count().unwrap(), 2);
		assert_eq!(COUNT.load(std::sync::atomic::Ordering::SeqCst), 1);

		// Group begin/end fires the observer once at end.
		assert!(group_begin("grouped").is_ok());
		assert!(group_begin("again").is_err()); // State
		push(noop_command(), "c1").unwrap();
		push(noop_command(), "c2").unwrap();
		// Children joined the group: no observer fire yet.
		assert_eq!(COUNT.load(std::sync::atomic::Ordering::SeqCst), 1);
		assert_eq!(count().unwrap(), 2);
		assert!(group_end().is_ok());
		assert_eq!(count().unwrap(), 3);
		assert_eq!(COUNT.load(std::sync::atomic::Ordering::SeqCst), 2);

		// Abort fires nothing.
		assert!(group_begin("abort").is_ok());
		push(noop_command(), "c3").unwrap();
		assert!(group_abort().is_ok());
		assert_eq!(count().unwrap(), 3);
		assert_eq!(COUNT.load(std::sync::atomic::Ordering::SeqCst), 2);

		// End/abort with no group open: State.
		assert!(group_end().is_err());
		assert!(group_abort().is_err());

		assert!(clear().is_ok());
	}

	/// The value-typed surface: `push` redoes and records a closure
	/// command, `undo`/`redo` step the stack, and the observers fire on
	/// every recorded mutation.
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

	/// The row getters answer like the C-ABI twins: labels survive an undo
	/// (undone rows stay labeled) and `command_done` flips with the stack
	/// pointer.
	#[test]
	fn value_command_name_and_done() {
		let _g = LOCK.lock().unwrap_or_else(|e| e.into_inner());
		assert!(clear().is_ok());

		let cmd = UndoCommand::from_closures(|| {}, || {});
		push(cmd, "alpha").unwrap();
		assert_eq!(count().unwrap(), 2);
		assert_eq!(command_name(1).unwrap(), "alpha");
		assert!(command_done(1).unwrap());
		assert_eq!(index().unwrap(), 2);

		// Undo keeps the row labeled but marks it undone.
		undo().unwrap();
		assert_eq!(command_name(1).unwrap(), "alpha");
		assert!(!command_done(1).unwrap());
		assert_eq!(index().unwrap(), 1);

		// Out-of-range rows error, they never panic.
		assert!(command_name(2).is_err());
		assert!(command_done(-1).is_err());

		assert!(clear().is_ok());
	}
}
