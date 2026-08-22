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
//! operation and its composite. Mirrors `src/undo/src/undocommand.h`.
//!
//! The C++ base is subclassed by other modules (`redo()`/`undo()`
//! overrides); the Rust equivalent of that virtual dispatch is a
//! trait-object command. [`UndoCommand`] boxes a [`Command`] (implemented
//! by closure-backed commands and by the [`MultiUndoCommand`] composite)
//! plus the command's state flags. This replaces the former
//! `OakUndoCommandVtable` + `userdata` callback-table model, which only
//! existed to cross the frozen C ABI (see README.md). With every consumer
//! in-process there is no callback table, no `extern "C"` trampoline and
//! no refcounted `CHandle` — commands are owned values.

use crate::error::{Error, Result};

/// A single undoable operation (the C++ virtual `redo()`/`undo()` pair).
///
/// Implementations must be `Send`: the owning [`UndoCommand`] lives in
/// stacks shared behind a `Mutex`, so it may move across threads (exactly
/// as the C++ commands did when the owning stack was shared).
pub trait Command: Send {
	/// Apply the change.
	fn redo(&mut self);
	/// Revert the change.
	fn undo(&mut self);

	/// `Some` for composite commands; used by [`UndoCommand::multi_*`].
	/// Defaults to `None` (a plain command is never a composite).
	fn as_multi(&self) -> Option<&MultiUndoCommand> {
		None
	}

	/// `Some` for composite commands; used by [`UndoCommand::multi_*`].
	/// Defaults to `None` (a plain command is never a composite).
	fn as_multi_mut(&mut self) -> Option<&mut MultiUndoCommand> {
		None
	}
}

/// `olive::UndoCommand` — an undoable operation.
///
/// State mirrors the C++ base: `done_` (already executed) and
/// `modified_` (project-dirty flag snapshot). The C++ modified-state
/// *callbacks* are not part of the C ABI and are intentionally omitted
/// (see README.md decision 2); only the flag snapshot is retained.
///
/// `prepared` mirrors the C++ `prepared_` flag (the command trait has no
/// `prepare()` callback, so it starts `true` and never un-prepares);
/// `is_empty` marks the invariant bottom-of-stack command so the stack
/// can keep it out of `can_undo`.
pub struct UndoCommand {
	/// The operation (closure-backed or composite).
	inner: Box<dyn Command>,
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

impl UndoCommand {
	/// Wrap any [`Command`] as an undoable command value. This is the
	/// construction path for whole-struct commands (the app's timeline
	/// and task commands); one-off edits use [`UndoCommand::from_closures`].
	pub fn new(command: impl Command + 'static) -> Self {
		UndoCommand {
			inner: Box::new(command),
			done: false,
			modified: false,
			prepared: true,
			is_empty: false,
		}
	}

	/// New closure-backed command: `redo` runs on redo, `undo` on undo.
	/// The safe way for direct-rlib consumers (the app) to build edit
	/// commands without writing any callback machinery.
	pub fn from_closures(
		redo: impl FnMut() + Send + 'static,
		undo: impl FnMut() + Send + 'static,
	) -> Self {
		UndoCommand::new(ClosureCommand {
			redo: Box::new(redo),
			undo: Box::new(undo),
		})
	}

	/// New empty composite command (`olive::MultiUndoCommand`).
	pub fn multi() -> Self {
		UndoCommand::new(MultiUndoCommand::new())
	}

	/// The invariant bottom-of-stack command ("New/Open Project"); a
	/// no-op in both directions and never undoable.
	pub(crate) fn empty() -> Self {
		let mut command = UndoCommand::from_closures(|| {}, || {});
		command.is_empty = true;
		command
	}

	/// Add `child` to a composite command (takes one reference).
	pub fn multi_add_child(&mut self, child: UndoCommand) {
		match self.inner.as_multi_mut() {
			Some(m) => m.add_child(child),
			None => panic!("multi_add_child on a non-multi command"),
		}
	}

	/// Number of children of a composite command (0 for plain commands).
	pub fn multi_child_count(&self) -> usize {
		self.inner.as_multi().map_or(0, |m| m.child_count())
	}

	/// Reference to the child at `index` of a composite command.
	pub fn multi_child(&self, index: usize) -> Result<&UndoCommand> {
		self.inner
			.as_multi()
			.map_or(Err(Error::Invalid), |m| m.child(index))
	}

	/// Mutable reference to the child at `index` of a composite command.
	pub fn multi_child_mut(&mut self, index: usize) -> Result<&mut UndoCommand> {
		match self.inner.as_multi_mut() {
			Some(m) => m.child_mut(index),
			None => Err(Error::Invalid),
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

	/// `has_prepared`: whether `prepare()` has run (commands have no
	/// prepare; always true).
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
		self.inner.as_multi().is_some()
	}

	/// Run the operation (virtual dispatch).
	fn redo(&mut self) {
		self.inner.redo();
	}

	/// Reverse the operation (virtual dispatch).
	fn undo(&mut self) {
		self.inner.undo();
	}
}

/// Closure-backed command state (see [`UndoCommand::from_closures`]).
struct ClosureCommand {
	/// The redo closure.
	redo: Box<dyn FnMut() + Send>,
	/// The undo closure.
	undo: Box<dyn FnMut() + Send>,
}

impl Command for ClosureCommand {
	fn redo(&mut self) {
		(self.redo)();
	}

	fn undo(&mut self) {
		(self.undo)();
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

	/// Mutable child at `index`.
	pub fn child_mut(&mut self, index: usize) -> Result<&mut UndoCommand> {
		self.children.get_mut(index).ok_or(Error::NotFound)
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

impl Command for MultiUndoCommand {
	fn redo(&mut self) {
		MultiUndoCommand::redo(self);
	}

	fn undo(&mut self) {
		MultiUndoCommand::undo(self);
	}

	fn as_multi(&self) -> Option<&MultiUndoCommand> {
		Some(self)
	}

	fn as_multi_mut(&mut self) -> Option<&mut MultiUndoCommand> {
		Some(self)
	}
}
