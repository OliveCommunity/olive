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

//! Edge-path coverage: error-code mapping, borrowed handles, refcount
//! symmetry, shell handles after `command_take`, `Default` impls, and the
//! `push_pre_executed` redo-tail/cap paths. Everything here goes through
//! the crate's public Rust API (the handle-level functions sunk from the
//! former C ABI layer).

use oakundo::error::{
	Error, OAKUNDO_E_FAILED, OAKUNDO_E_INVALID, OAKUNDO_E_NOMEM, OAKUNDO_E_NOT_FOUND,
	OAKUNDO_E_STATE,
};
use oakundo::handle::{make_borrowed, make_owned};
use oakundo::undocommand::{
	command_free, command_init, command_init_multi, command_multi_child_count, command_redo_now,
	MultiUndoCommand, OakUndoCommandVtable, UndoCommand,
};
use oakundo::undostack::{
	undostack_can_redo, undostack_free, undostack_index, undostack_init, undostack_push,
	undostack_push_pre_executed, undostack_undo, EmptyCommand, UndoStack, K_MAX_UNDO_COMMANDS,
};

/// Every `Error` variant maps to its documented public code.
#[test]
fn error_code_mapping_is_complete() {
	assert_eq!(Error::Invalid.code(), OAKUNDO_E_INVALID);
	assert_eq!(Error::State.code(), OAKUNDO_E_STATE);
	assert_eq!(Error::Failed("ctx".to_string()).code(), OAKUNDO_E_FAILED);
	assert_eq!(Error::NotFound.code(), OAKUNDO_E_NOT_FOUND);
	assert_eq!(Error::NoMem.code(), OAKUNDO_E_NOMEM);
}

/// Borrowed handles: addref/release only touch the shell, never the
/// pointee; NULL ctx is a no-op for both.
#[test]
fn borrowed_handle_refcounting() {
	let mut value: i32 = 7;
	let h = unsafe { make_borrowed(&mut value as *mut i32) };
	assert!(!h.is_null());

	// NULL ctx is a no-op.
	unsafe { h.addref.unwrap()(std::ptr::null_mut()) };
	unsafe { h.release.unwrap()(std::ptr::null_mut()) };

	// addref then two releases: the pointee survives (still readable).
	unsafe { h.addref.unwrap()(h.ctx) };
	unsafe { h.release.unwrap()(h.ctx) };
	assert_eq!(value, 7);
	unsafe { h.release.unwrap()(h.ctx) };
	assert_eq!(value, 7);
}

/// Owned handles: addref requires a matching extra release; releasing to
/// zero destroys the box exactly once.
#[test]
fn owned_handle_refcounting() {
	let h = make_owned(String::from("owned"));
	assert!(!h.is_null());
	unsafe { h.addref.unwrap()(h.ctx) };
	unsafe { h.release.unwrap()(h.ctx) };
	// Still alive (one reference left) and readable.
	let view = unsafe { oakundo::handle::get::<String>(&h) };
	assert_eq!(view.map(String::as_str), Some("owned"));
	unsafe { h.release.unwrap()(h.ctx) };
}

/// `Default` impls mirror `new()`.
#[test]
fn default_impls_match_new() {
	let _empty = EmptyCommand::default();
	let stack = UndoStack::default();
	assert_eq!(
		stack.done_count(),
		1,
		"fresh stack holds the bottom command"
	);
	assert!(!stack.can_undo());
	let multi = MultiUndoCommand::default();
	assert_eq!(multi.child_count(), 0);
}

/// `set_prepared` is idempotent and `has_prepared` reflects it.
#[test]
fn prepared_flag_roundtrip() {
	let vtable = OakUndoCommandVtable {
		redo: None,
		undo: None,
		free_fn: None,
	};
	let mut cmd = UndoCommand::from_vtable(vtable, std::ptr::null_mut());
	assert!(cmd.has_prepared());
	cmd.set_prepared();
	assert!(cmd.has_prepared());
}

/// A command handle whose value was taken by a stack push becomes a
/// non-owning shell: redo/undo on it are `E_INVALID`, and pushing it a
/// second time is `E_STATE`.
#[test]
fn taken_command_shell_is_inert() {
	let vtable = OakUndoCommandVtable {
		redo: None,
		undo: None,
		free_fn: None,
	};
	let mut stack = undostack_init();
	let cmd = command_init(&vtable, std::ptr::null_mut());
	assert!(!cmd.ctx.is_null());

	let name = c"once";
	assert_eq!(undostack_push(stack, cmd, name.as_ptr()), 0);

	// The shell no longer holds a command value.
	assert_eq!(command_redo_now(cmd), OAKUNDO_E_INVALID);
	// Taking the same box twice is a state error.
	assert_eq!(undostack_push(stack, cmd, name.as_ptr()), OAKUNDO_E_STATE);

	let mut release = cmd;
	command_free(&mut release);
	undostack_free(&mut stack);
}

/// `push_pre_executed` also drops the redoable tail and evicts the oldest
/// row past the cap (mirrors `push`).
#[test]
fn push_pre_executed_clears_redo_tail_and_caps() {
	let vtable = OakUndoCommandVtable {
		redo: None,
		undo: None,
		free_fn: None,
	};
	let mut stack = undostack_init();
	let name = c"row";

	// Push two, undo one, then push_pre_executed: redo tail is dropped.
	for _ in 0..2 {
		let cmd = command_init(&vtable, std::ptr::null_mut());
		assert_eq!(undostack_push(stack, cmd, name.as_ptr()), 0);
		let mut shell = cmd;
		command_free(&mut shell);
	}
	assert_eq!(undostack_undo(stack), 0);
	let cmd = command_init(&vtable, std::ptr::null_mut());
	assert_eq!(undostack_push_pre_executed(stack, cmd, name.as_ptr()), 0);
	let mut can_redo: i32 = 1;
	assert_eq!(undostack_can_redo(stack, &mut can_redo), 0);
	assert_eq!(can_redo, 0, "push_pre_executed drops the redoable tail");

	// Fill past the cap with pre-executed commands: the oldest rows are
	// evicted and the count stays at K_MAX_UNDO_COMMANDS.
	for _ in 0..(K_MAX_UNDO_COMMANDS + 10) {
		let cmd = command_init(&vtable, std::ptr::null_mut());
		assert_eq!(undostack_push_pre_executed(stack, cmd, name.as_ptr()), 0);
	}
	let mut index: i64 = 0;
	assert_eq!(undostack_index(stack, &mut index), 0);
	assert_eq!(
		index, K_MAX_UNDO_COMMANDS as i64,
		"pre-executed rows evict at the cap"
	);

	undostack_free(&mut stack);
}

/// `make_owned` on a stack mutex is what `undostack_init` uses; the
/// `CHandle` accessors tolerate empty handles.
#[test]
fn empty_chandle_accessors() {
	let h = oakundo::handle::CHandle::null();
	assert!(h.is_null());
	assert!(h.addref.is_none() && h.release.is_none());
	assert_eq!(h.abi_version, 0);
	let view = unsafe { oakundo::handle::get::<i32>(&h) };
	assert!(view.is_none());
}

/// Command-box refcounting: addref/release tolerate NULL ctx, a bumped
/// refcount needs a matching release, and a taken multi shell reports
/// `E_INVALID` instead of dereferencing a null command pointer.
#[test]
fn command_box_refcounting_and_taken_multi_shell() {
	let vtable = OakUndoCommandVtable {
		redo: None,
		undo: None,
		free_fn: None,
	};

	let mut cmd = command_init(&vtable, std::ptr::null_mut());
	assert!(!cmd.ctx.is_null());

	// NULL ctx is a no-op for both refcount callbacks.
	unsafe { cmd.addref.unwrap()(std::ptr::null_mut()) };
	unsafe { cmd.release.unwrap()(std::ptr::null_mut()) };

	// addref then two releases: destroyed exactly once at zero.
	unsafe { cmd.addref.unwrap()(cmd.ctx) };
	unsafe { cmd.release.unwrap()(cmd.ctx) };
	unsafe { cmd.release.unwrap()(cmd.ctx) };
	cmd.ctx = std::ptr::null_mut();

	// A multi command pushed into a stack is taken; the remaining shell
	// must fail cleanly on child access.
	let mut stack = undostack_init();
	let mut multi = command_init_multi();
	let name = c"m";
	assert_eq!(undostack_push(stack, multi, name.as_ptr()), 0);
	let mut out: i32 = -1;
	assert_eq!(command_multi_child_count(multi, &mut out), OAKUNDO_E_INVALID);
	command_free(&mut multi);
	undostack_free(&mut stack);
}
