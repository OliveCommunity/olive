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

//! `olive::UndoCommand` / `olive::MultiUndoCommand` — the undoable
//! operation and its composite. Mirrors `src/undo/src/undocommand.h`
//! and `include/undo/undocommand.h`.
//!
//! The C++ base is subclassed by other modules (`redo()`/`undo()`
//! overrides). Rust has no inheritance, so the C ABI models the same
//! polymorphism with a callback table + `userdata` pointer
//! ([`OakUndoCommandVtable`]); this module's [`UndoCommand`] holds a
//! [`CommandKind`] that is either that vtable-backed command or a
//! [`MultiUndoCommand`] composite. This vtable-command pattern is the
//! centerpiece of the oakundo architecture (see README.md).
//!
//! ## Command boxes
//!
//! Commands cross the FFI boundary through a dedicated [`CommandBox`]
//! (not the generic [`crate::handle::RefBox`]): it holds the raw
//! `*mut UndoCommand`, an `owns` flag (mirroring the C++
//! `OakUndoCommandBox`) and a refcount. An **owning** box owns and
//! destroys its command at refcount zero; a **borrowed** box (a
//! reference into a `MultiUndoCommand` or an `UndoStack`) owns only its
//! shell. `take_command` moves the command value out of an owning box,
//! turning it into a non-owning shell (the C++ `mark_container_owned`).

use std::ffi::c_void;
use std::sync::atomic::{AtomicU32, Ordering};

use crate::error::{Error, Result};
use crate::handle::{CHandle, OAKUNDO_ABI_VERSION};

/// `oakundo_command_vtable` — the caller-defined redo/undo/free
/// callback table. Any entry may be `None`; a `None` redo/undo makes
/// that direction a no-op, `None` free_fn skips userdata release.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct OakUndoCommandVtable {
	/// Execute the operation.
	pub redo: Option<unsafe extern "C" fn(*mut c_void)>,
	/// Reverse the operation.
	pub undo: Option<unsafe extern "C" fn(*mut c_void)>,
	/// Release `userdata` when the command is destroyed.
	pub free_fn: Option<unsafe extern "C" fn(*mut c_void)>,
}

/// What backs an [`UndoCommand`]: a caller-defined vtable command or a
/// composite of children. The direct analog of C++ virtual dispatch.
pub enum CommandKind {
	/// Caller-defined command: callback table plus opaque userdata.
	Vtable {
		/// The copied callback table.
		vtable: OakUndoCommandVtable,
		/// Opaque caller state; owned by the command.
		userdata: *mut c_void,
	},
	/// Composite command (`olive::MultiUndoCommand`).
	Multi(MultiUndoCommand),
}

/// `olive::UndoCommand` — an undoable operation.
///
/// State mirrors the C++ base: `done_` (already executed) and
/// `modified_` (project-dirty flag snapshot). The C++ modified-state
/// *callbacks* are not part of the C ABI and are intentionally omitted
/// (see README.md decision 2); only the flag snapshot is retained.
///
/// `prepared` mirrors the C++ `prepared_` flag (the vtable has no
/// `prepare()` callback, so it starts `true` and never un-prepares);
/// `is_empty` marks the invariant bottom-of-stack command so the stack
/// can keep it out of `can_undo`.
pub struct UndoCommand {
	/// The operation (vtable or composite).
	kind: CommandKind,
	/// Whether the command has been executed (`done_`).
	done: bool,
	/// Project-dirty flag snapshot recorded at the last redo.
	///
	/// Reserved for the future modified-state callbacks; with no C ABI
	/// surface it is retained but never read.
	#[allow(dead_code)]
	modified: bool,
	/// Whether `prepare()` has run (`prepared_`).
	prepared: bool,
	/// True for the stack's invariant bottom command.
	is_empty: bool,
}

/// A command's userdata is caller-controlled; like the C++ object it
/// may be moved across threads when the owning stack is shared behind a
/// `Mutex`. Callbacks must be thread-safe with respect to the caller's
/// own locking, exactly as in the C++ implementation.
unsafe impl Send for UndoCommand {}

impl UndoCommand {
	/// New vtable-backed command; takes ownership of `userdata`.
	pub fn from_vtable(vtable: OakUndoCommandVtable, userdata: *mut c_void) -> Self {
		UndoCommand {
			kind: CommandKind::Vtable { vtable, userdata },
			done: false,
			modified: false,
			prepared: true,
			is_empty: false,
		}
	}

	/// New empty composite command (`olive::MultiUndoCommand`).
	pub fn multi() -> Self {
		UndoCommand {
			kind: CommandKind::Multi(MultiUndoCommand::new()),
			done: false,
			modified: false,
			prepared: true,
			is_empty: false,
		}
	}

	/// The invariant bottom-of-stack command ("New/Open Project"); all
	/// callbacks are no-ops and it is never undoable.
	pub(crate) fn empty() -> Self {
		UndoCommand {
			kind: CommandKind::Vtable {
				vtable: OakUndoCommandVtable {
					redo: None,
					undo: None,
					free_fn: None,
				},
				userdata: std::ptr::null_mut(),
			},
			done: false,
			modified: false,
			prepared: true,
			is_empty: true,
		}
	}

	/// Add `child` to a composite command (takes one reference).
	pub fn multi_add_child(&mut self, child: UndoCommand) {
		match &mut self.kind {
			CommandKind::Multi(m) => m.add_child(child),
			CommandKind::Vtable { .. } => {
				panic!("multi_add_child on a non-multi command")
			}
		}
	}

	/// Number of children of a composite command.
	pub fn multi_child_count(&self) -> usize {
		match &self.kind {
			CommandKind::Multi(m) => m.child_count(),
			CommandKind::Vtable { .. } => 0,
		}
	}

	/// Reference to the child at `index` of a composite command.
	pub fn multi_child(&self, index: usize) -> Result<&UndoCommand> {
		match &self.kind {
			CommandKind::Multi(m) => m.child(index),
			CommandKind::Vtable { .. } => Err(Error::Invalid),
		}
	}

	/// `redo_now` semantics: a no-op if already done.
	pub fn redo_now(&mut self) {
		if !self.done {
			if !self.prepared {
				self.set_prepared();
			}
			self.redo();
			self.done = true;
		}
	}

	/// `undo_now` semantics: a no-op if not done.
	pub fn undo_now(&mut self) {
		if self.done {
			self.undo();
			self.done = false;
		}
	}

	/// `redo_and_set_modified`: redo, then snapshot-and-force the dirty
	/// flag. Without callbacks this is just `redo_now`.
	pub fn redo_and_set_modified(&mut self) {
		self.redo_now();
	}

	/// `undo_and_set_modified`: undo, then restore the dirty flag.
	pub fn undo_and_set_modified(&mut self) {
		self.undo_now();
	}

	/// `has_prepared`: whether `prepare()` has run (vtable commands have
	/// no prepare; always true).
	pub fn has_prepared(&self) -> bool {
		self.prepared
	}

	/// `set_prepared`.
	pub fn set_prepared(&mut self) {
		self.prepared = true;
	}

	/// `set_done`: mark as already executed without running redo.
	pub fn set_done(&mut self, done: bool) {
		self.done = done;
	}

	/// Whether the command is currently done (executed, not undone).
	pub fn is_done(&self) -> bool {
		self.done
	}

	/// Whether this is the invariant bottom-of-stack command.
	pub(crate) fn is_empty(&self) -> bool {
		self.is_empty
	}

	/// Whether this is a composite (`MultiUndoCommand`) command.
	pub(crate) fn is_multi(&self) -> bool {
		matches!(self.kind, CommandKind::Multi(_))
	}

	/// Run the operation (virtual dispatch).
	fn redo(&mut self) {
		match &mut self.kind {
			CommandKind::Vtable { vtable, userdata } => {
				if let Some(f) = vtable.redo {
					// SAFETY: `userdata` is caller-supplied and outlives
					// the command (owned by it). The callback is the one
					// registered by the caller.
					unsafe { f(*userdata) }
				}
			}
			CommandKind::Multi(m) => m.redo(),
		}
	}

	/// Reverse the operation (virtual dispatch).
	fn undo(&mut self) {
		match &mut self.kind {
			CommandKind::Vtable { vtable, userdata } => {
				if let Some(f) = vtable.undo {
					// SAFETY: as in `redo`.
					unsafe { f(*userdata) }
				}
			}
			CommandKind::Multi(m) => m.undo(),
		}
	}
}

impl Drop for UndoCommand {
	/// Invokes the vtable `free_fn` on `userdata` (composites free
	/// children transitively).
	fn drop(&mut self) {
		if let CommandKind::Vtable { vtable, userdata } = &self.kind {
			if let Some(f) = vtable.free_fn {
				// SAFETY: `userdata` is owned by the command; this is the
				// final release.
				unsafe { f(*userdata) }
			}
		}
	}
}

/// `olive::MultiUndoCommand` — a composite of child commands.
///
/// `redo` runs children in order; `undo` runs them in reverse. Owns one
/// reference to each child.
pub struct MultiUndoCommand {
	/// Child commands in insertion order.
	children: Vec<UndoCommand>,
}

/// See `UndoCommand::Send`.
unsafe impl Send for MultiUndoCommand {}

impl MultiUndoCommand {
	/// New empty composite.
	pub fn new() -> Self {
		MultiUndoCommand {
			children: Vec::new(),
		}
	}

	/// Add a child (takes one reference).
	pub fn add_child(&mut self, child: UndoCommand) {
		self.children.push(child);
	}

	/// Child count.
	pub fn child_count(&self) -> usize {
		self.children.len()
	}

	/// Child at `index`.
	pub fn child(&self, index: usize) -> Result<&UndoCommand> {
		self.children.get(index).ok_or(Error::NotFound)
	}

	/// Redo all children in order.
	pub fn redo(&mut self) {
		for child in self.children.iter_mut() {
			child.redo_and_set_modified();
		}
	}

	/// Undo all children in reverse order.
	pub fn undo(&mut self) {
		for child in self.children.iter_mut().rev() {
			child.undo_and_set_modified();
		}
	}
}

impl Default for MultiUndoCommand {
	fn default() -> Self {
		Self::new()
	}
}

/// Heap box behind a command handle's `ctx`. Mirrors the C++
/// `OakUndoCommandBox` (`{command, owns, refs}`).
pub(crate) struct CommandBox {
	/// Raw pointer to the command value.
	command: *mut UndoCommand,
	/// Whether this box owns (and must destroy) `command`.
	owns: bool,
	/// Atomic reference count.
	refs: AtomicU32,
}

/// Owned handle for a fresh command value (refcount 1, owns `cmd`).
///
/// # Safety
/// The returned handle owns `cmd`; release it with `command_release`.
pub(crate) unsafe fn command_from_owned(cmd: UndoCommand) -> CHandle {
	let boxed = Box::into_raw(Box::new(CommandBox {
		command: Box::into_raw(Box::new(cmd)),
		owns: true,
		refs: AtomicU32::new(1),
	})) as *mut c_void;
	CHandle {
		ctx: boxed,
		addref: Some(command_addref),
		release: Some(command_release),
		abi_version: OAKUNDO_ABI_VERSION,
	}
}

/// Borrowed handle for a command owned elsewhere (release frees only
/// the shell).
///
/// # Safety
/// `cmd` must outlive the returned handle.
pub(crate) unsafe fn command_from_borrowed(cmd: *mut UndoCommand) -> CHandle {
	let boxed = Box::into_raw(Box::new(CommandBox {
		command: cmd,
		owns: false,
		refs: AtomicU32::new(1),
	})) as *mut c_void;
	CHandle {
		ctx: boxed,
		addref: Some(command_addref),
		release: Some(command_release),
		abi_version: OAKUNDO_ABI_VERSION,
	}
}

/// Read-only view of the command behind `ctx`; `None` for a null handle.
///
/// # Safety
/// `ctx` must come from a valid, live `CommandBox`.
pub(crate) unsafe fn command_to_ref(ctx: *mut c_void) -> Option<&'static UndoCommand> {
	unsafe {
		if ctx.is_null() {
			return None;
		}
		let boxed = ctx as *mut CommandBox;
		if (*boxed).command.is_null() {
			return None;
		}
		Some(&*(*boxed).command)
	}
}

/// Mutable view of the command behind `ctx`; `None` for a null handle.
///
/// # Safety
/// `ctx` must come from a valid, live `CommandBox`.
pub(crate) unsafe fn command_to_mut(ctx: *mut c_void) -> Option<&'static mut UndoCommand> {
	unsafe {
		if ctx.is_null() {
			return None;
		}
		let boxed = ctx as *mut CommandBox;
		if (*boxed).command.is_null() {
			return None;
		}
		Some(&mut *(*boxed).command)
	}
}

/// Move the command value out of an owning box, turning it into a
/// non-owning shell (the C++ `mark_container_owned` transfer).
///
/// # Safety
/// `ctx` must come from a valid, live `CommandBox`.
pub(crate) unsafe fn command_take(ctx: *mut c_void) -> Result<UndoCommand> {
	unsafe {
		if ctx.is_null() {
			return Err(Error::Invalid);
		}
		let boxed = ctx as *mut CommandBox;
		if !(*boxed).owns || (*boxed).command.is_null() {
			return Err(Error::State);
		}
		let value = Box::from_raw((*boxed).command);
		(*boxed).command = std::ptr::null_mut();
		(*boxed).owns = false;
		Ok(*value)
	}
}

/// Increment a command box's refcount (the `addref` function pointer).
///
/// # Safety
/// `ctx` must come from a valid `CommandBox`.
pub(crate) unsafe extern "C" fn command_addref(ctx: *mut c_void) {
	unsafe {
		if ctx.is_null() {
			return;
		}
		let boxed = ctx as *mut CommandBox;
		(&(*boxed).refs).fetch_add(1, Ordering::AcqRel);
	}
}

/// Decrement a command box's refcount; destroys at zero. Owned boxes
/// free their command first, then the shell; borrowed boxes free only
/// the shell.
///
/// # Safety
/// `ctx` must come from a valid `CommandBox`.
pub(crate) unsafe extern "C" fn command_release(ctx: *mut c_void) {
	unsafe {
		if ctx.is_null() {
			return;
		}
		let boxed = ctx as *mut CommandBox;
		if (&(*boxed).refs).fetch_sub(1, Ordering::AcqRel) == 1 {
			if (*boxed).owns && !(*boxed).command.is_null() {
				drop(Box::from_raw((*boxed).command));
			}
			drop(Box::from_raw(boxed));
		}
	}
}
