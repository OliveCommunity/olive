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

//! C ABI contract tests (`ffi.rs`). Each exported function gets at least
//! one success path and one failure path; complex multi-command and stack
//! behavior is exercised as a matrix. The expected semantics are pinned by
//! the C++ module (`src/undo/src`, unchanged).

use std::cell::RefCell;
use std::ffi::{c_char, c_int, c_void, CString};

use oakundo::error::{OAKUNDO_E_INVALID, OAKUNDO_E_NOT_FOUND, OAKUNDO_OK};
use oakundo::ffi::command::{
	oakundo_command_free, oakundo_command_init, oakundo_command_init_multi,
	oakundo_command_multi_add_child, oakundo_command_multi_child,
	oakundo_command_multi_child_count, oakundo_command_redo_now, oakundo_command_undo_now,
};
use oakundo::ffi::undostack::{
	oakundo_undostack_can_redo, oakundo_undostack_can_undo, oakundo_undostack_clear,
	oakundo_undostack_command_is_done, oakundo_undostack_command_text, oakundo_undostack_count,
	oakundo_undostack_free, oakundo_undostack_index, oakundo_undostack_init,
	oakundo_undostack_jump, oakundo_undostack_push, oakundo_undostack_push_pre_executed,
	oakundo_undostack_redo, oakundo_undostack_undo,
};
use oakundo::ffi::{OakUndoCommand, OakUndoStack};
use oakundo::handle::CHandle;
use oakundo::handle::OAKUNDO_ABI_VERSION;
use oakundo::undocommand::OakUndoCommandVtable;

/// Shared event recorder driven through vtable callbacks.
struct Trace {
	events: RefCell<Vec<String>>,
}

/// Per-command callback payload: a name and a shared trace.
struct Probe {
	name: &'static str,
	trace: *const Trace,
}

unsafe extern "C" fn probe_redo(u: *mut c_void) {
	let p = unsafe { &mut *(u as *mut Probe) };
	let trace = unsafe { &*p.trace };
	trace.events.borrow_mut().push(format!("redo:{}", p.name));
}

unsafe extern "C" fn probe_undo(u: *mut c_void) {
	let p = unsafe { &mut *(u as *mut Probe) };
	let trace = unsafe { &*p.trace };
	trace.events.borrow_mut().push(format!("undo:{}", p.name));
}

unsafe extern "C" fn probe_free(u: *mut c_void) {
	let p = unsafe { &mut *(u as *mut Probe) };
	let trace = unsafe { &*p.trace };
	trace.events.borrow_mut().push(format!("free:{}", p.name));
}

/// Snapshot of the recorded events, in order.
fn events(trace: &Trace) -> Vec<String> {
	trace.events.borrow().clone()
}

/// A fresh trace plus three named probes (`a`, `b`, `c`) pointing at it.
fn setup() -> (Box<Trace>, Vec<Probe>) {
	let trace = Box::new(Trace {
		events: RefCell::new(Vec::new()),
	});
	let ptr = &*trace as *const Trace;
	let mut probes = Vec::new();
	for name in ["a", "b", "c"] {
		probes.push(Probe { name, trace: ptr });
	}
	(trace, probes)
}

/// A vtable-backed command handle whose callbacks record into `probe`.
unsafe fn make_cmd(probe: *mut Probe) -> OakUndoCommand {
	let vtable = OakUndoCommandVtable {
		redo: Some(probe_redo),
		undo: Some(probe_undo),
		free_fn: Some(probe_free),
	};
	oakundo_command_init(&vtable, probe as *mut c_void)
}

/// An empty (all-zero) command handle.
fn empty_cmd() -> OakUndoCommand {
	CHandle {
		ctx: std::ptr::null_mut(),
		addref: None,
		release: None,
		abi_version: 0,
	}
}

/// An empty (all-zero) stack handle.
fn empty_stack() -> OakUndoStack {
	CHandle {
		ctx: std::ptr::null_mut(),
		addref: None,
		release: None,
		abi_version: 0,
	}
}

/// A fresh stack handle (refcount 1) for the calling test.
unsafe fn new_stack() -> OakUndoStack {
	oakundo_undostack_init()
}

// ---------------------------------------------------------------------------
// Skeleton contract tests (ffi_contract_test.rs)
// ---------------------------------------------------------------------------

/// Command lifecycle: `init` returns a refcounted handle, `redo_now` marks
/// it done (a second `redo_now` is a no-op), `undo_now` un-done it, and
/// `free` runs `free_fn` once and clears `ctx`.
#[test]
fn command_lifecycle() {
	let (trace, mut probes) = setup();
	let mut cmd = unsafe { make_cmd(&mut probes[0] as *mut Probe) };

	assert!(!cmd.ctx.is_null());
	assert_eq!(cmd.abi_version, OAKUNDO_ABI_VERSION);
	assert!(cmd.addref.is_some() && cmd.release.is_some());

	assert_eq!(unsafe { oakundo_command_redo_now(cmd) }, OAKUNDO_OK);
	assert_eq!(events(&trace), vec!["redo:a"]);

	// Idempotent redo.
	assert_eq!(unsafe { oakundo_command_redo_now(cmd) }, OAKUNDO_OK);
	assert_eq!(events(&trace), vec!["redo:a"]);

	assert_eq!(unsafe { oakundo_command_undo_now(cmd) }, OAKUNDO_OK);
	assert_eq!(events(&trace), vec!["redo:a", "undo:a"]);

	// Idempotent undo.
	assert_eq!(unsafe { oakundo_command_undo_now(cmd) }, OAKUNDO_OK);
	assert_eq!(events(&trace), vec!["redo:a", "undo:a"]);

	// free destroys once: free_fn runs exactly once and ctx is cleared.
	unsafe { oakundo_command_free(&mut cmd) };
	assert_eq!(events(&trace), vec!["redo:a", "undo:a", "free:a"]);
	assert!(cmd.ctx.is_null());
}

/// Multi command: `add_child` → `child_count` reflects it; redo runs
/// children in order, undo in reverse order.
#[test]
fn multi_redo_undo_ordering() {
	let (trace, mut probes) = setup();
	let mut multi = unsafe { oakundo_command_init_multi() };

	// add children a, b, c.
	let ca = unsafe { make_cmd(&mut probes[0] as *mut Probe) };
	let cb = unsafe { make_cmd(&mut probes[1] as *mut Probe) };
	let cc = unsafe { make_cmd(&mut probes[2] as *mut Probe) };
	assert_eq!(
		unsafe { oakundo_command_multi_add_child(multi, ca) },
		OAKUNDO_OK
	);
	assert_eq!(
		unsafe { oakundo_command_multi_add_child(multi, cb) },
		OAKUNDO_OK
	);
	assert_eq!(
		unsafe { oakundo_command_multi_add_child(multi, cc) },
		OAKUNDO_OK
	);

	// child_count reflects three.
	let mut count: c_int = 0;
	assert_eq!(
		unsafe { oakundo_command_multi_child_count(multi, &mut count) },
		OAKUNDO_OK
	);
	assert_eq!(count, 3);

	// redo fires in insertion order.
	assert_eq!(unsafe { oakundo_command_redo_now(multi) }, OAKUNDO_OK);
	assert_eq!(events(&trace), vec!["redo:a", "redo:b", "redo:c"]);

	// undo fires in reverse order.
	assert_eq!(unsafe { oakundo_command_undo_now(multi) }, OAKUNDO_OK);
	assert_eq!(
		events(&trace),
		vec!["redo:a", "redo:b", "redo:c", "undo:c", "undo:b", "undo:a"]
	);

	// Borrowed child handles are released harmlessly before the multi dies.
	let mut child0 = empty_cmd();
	assert_eq!(
		unsafe { oakundo_command_multi_child(multi, 0, &mut child0) },
		OAKUNDO_OK
	);
	assert!(!child0.ctx.is_null());
	unsafe { oakundo_command_free(&mut child0) };

	// free the multi: children are freed (shell only) without double-free.
	let mut multi_owned = multi;
	unsafe { oakundo_command_free(&mut multi_owned) };
	assert_eq!(
		events(&trace)
			.iter()
			.filter(|e| e.starts_with("free:"))
			.count(),
		3
	);
}

/// Stack: fresh stack has one empty "New/Open Project" command, so
/// `can_undo` is 0 and `count` is 1; pushing redoable commands grows
/// `count` and makes `can_undo`/`can_redo` track the position.
#[test]
fn stack_push_undo_redo_queries() {
	let (trace, mut probes) = setup();
	let mut stack = unsafe { new_stack() };

	let mut count: i64 = 0;
	let mut value: c_int = 0;
	assert_eq!(
		unsafe { oakundo_undostack_count(stack, &mut count) },
		OAKUNDO_OK
	);
	assert_eq!(count, 1);
	assert_eq!(
		unsafe { oakundo_undostack_can_undo(stack, &mut value) },
		OAKUNDO_OK
	);
	assert_eq!(value, 0);
	assert_eq!(
		unsafe { oakundo_undostack_can_redo(stack, &mut value) },
		OAKUNDO_OK
	);
	assert_eq!(value, 0);

	// Push two commands.
	let name_a = CString::new("A").unwrap();
	let mut ca = unsafe { make_cmd(&mut probes[0] as *mut Probe) };
	assert_eq!(
		unsafe { oakundo_undostack_push(stack, ca, name_a.as_ptr()) },
		OAKUNDO_OK
	);
	unsafe { oakundo_command_free(&mut ca) }; // non-owning shell now
	let name_b = CString::new("B").unwrap();
	let mut cb = unsafe { make_cmd(&mut probes[1] as *mut Probe) };
	assert_eq!(
		unsafe { oakundo_undostack_push(stack, cb, name_b.as_ptr()) },
		OAKUNDO_OK
	);
	unsafe { oakundo_command_free(&mut cb) };

	assert_eq!(events(&trace), vec!["redo:a", "redo:b"]);
	assert_eq!(
		unsafe { oakundo_undostack_count(stack, &mut count) },
		OAKUNDO_OK
	);
	assert_eq!(count, 3);
	assert_eq!(
		unsafe { oakundo_undostack_can_undo(stack, &mut value) },
		OAKUNDO_OK
	);
	assert_eq!(value, 1);
	assert_eq!(
		unsafe { oakundo_undostack_can_redo(stack, &mut value) },
		OAKUNDO_OK
	);
	assert_eq!(value, 0);

	// Undo moves B into the redoable tail.
	assert_eq!(unsafe { oakundo_undostack_undo(stack) }, OAKUNDO_OK);
	assert_eq!(events(&trace), vec!["redo:a", "redo:b", "undo:b"]);
	assert_eq!(
		unsafe { oakundo_undostack_can_undo(stack, &mut value) },
		OAKUNDO_OK
	);
	assert_eq!(value, 1);
	assert_eq!(
		unsafe { oakundo_undostack_can_redo(stack, &mut value) },
		OAKUNDO_OK
	);
	assert_eq!(value, 1);

	// Redo restores.
	assert_eq!(unsafe { oakundo_undostack_redo(stack) }, OAKUNDO_OK);
	assert_eq!(events(&trace), vec!["redo:a", "redo:b", "undo:b", "redo:b"]);
	assert_eq!(
		unsafe { oakundo_undostack_can_redo(stack, &mut value) },
		OAKUNDO_OK
	);
	assert_eq!(value, 0);

	unsafe { oakundo_undostack_free(&mut stack) };
}

/// `can_redo`/`index` after undo and redo; `jump(0)` clamps to the
/// bottom empty command without spinning; `jump` beyond the top is a no-op.
#[test]
fn stack_jump_clamps() {
	let (trace, mut probes) = setup();
	let mut stack = unsafe { new_stack() };

	let na = CString::new("A").unwrap();
	let nb = CString::new("B").unwrap();
	let nc = CString::new("C").unwrap();
	let mut ca = unsafe { make_cmd(&mut probes[0] as *mut Probe) };
	unsafe { oakundo_undostack_push(stack, ca, na.as_ptr()) };
	let mut cb = unsafe { make_cmd(&mut probes[1] as *mut Probe) };
	unsafe { oakundo_undostack_push(stack, cb, nb.as_ptr()) };
	let mut cc = unsafe { make_cmd(&mut probes[2] as *mut Probe) };
	unsafe { oakundo_undostack_push(stack, cc, nc.as_ptr()) };

	let mut index: i64 = 0;
	assert_eq!(
		unsafe { oakundo_undostack_index(stack, &mut index) },
		OAKUNDO_OK
	);
	assert_eq!(index, 4);

	// jump to the bottom (0): undo all three; negative clamps to 0 too.
	assert_eq!(unsafe { oakundo_undostack_jump(stack, 0) }, OAKUNDO_OK);
	assert_eq!(
		unsafe { oakundo_undostack_index(stack, &mut index) },
		OAKUNDO_OK
	);
	assert_eq!(index, 1);
	assert_eq!(
		events(&trace),
		vec!["redo:a", "redo:b", "redo:c", "undo:c", "undo:b", "undo:a"]
	);

	// jump back up to 3: redo a and b (c was already undone to reach 1).
	assert_eq!(unsafe { oakundo_undostack_jump(stack, 3) }, OAKUNDO_OK);
	assert_eq!(
		unsafe { oakundo_undostack_index(stack, &mut index) },
		OAKUNDO_OK
	);
	assert_eq!(index, 3);

	// jump beyond the top redoes up to the top (index 4), matching the C++
	// `jump` (undone commands are redoable, so the second loop runs).
	assert_eq!(unsafe { oakundo_undostack_jump(stack, 100) }, OAKUNDO_OK);
	assert_eq!(
		unsafe { oakundo_undostack_index(stack, &mut index) },
		OAKUNDO_OK
	);
	assert_eq!(index, 4);

	// Negative index clamps to the bottom.
	assert_eq!(unsafe { oakundo_undostack_jump(stack, -5) }, OAKUNDO_OK);
	assert_eq!(
		unsafe { oakundo_undostack_index(stack, &mut index) },
		OAKUNDO_OK
	);
	assert_eq!(index, 1);

	unsafe { oakundo_undostack_free(&mut stack) };
}

/// `push_pre_executed` records without redoing (stays undoable); empty
/// multi commands are discarded on push.
#[test]
fn stack_pre_executed_and_empty_multi() {
	let (trace, mut probes) = setup();
	let mut stack = unsafe { new_stack() };

	// push_pre_executed: no redo callback.
	let name = CString::new("Pre").unwrap();
	let mut cp = unsafe { make_cmd(&mut probes[0] as *mut Probe) };
	assert_eq!(
		unsafe { oakundo_undostack_push_pre_executed(stack, cp, name.as_ptr()) },
		OAKUNDO_OK
	);
	unsafe { oakundo_command_free(&mut cp) };
	assert_eq!(events(&trace), Vec::<String>::new());

	// The pre-executed command is recorded as done (undoable).
	let mut done: c_int = 0;
	assert_eq!(
		unsafe { oakundo_undostack_command_is_done(stack, 1, &mut done) },
		OAKUNDO_OK
	);
	assert_eq!(done, 1);

	let mut count: i64 = 0;
	assert_eq!(
		unsafe { oakundo_undostack_count(stack, &mut count) },
		OAKUNDO_OK
	);
	assert_eq!(count, 2);

	// Undoing the pre-executed command still runs its undo callback.
	assert_eq!(unsafe { oakundo_undostack_undo(stack) }, OAKUNDO_OK);
	assert_eq!(events(&trace), vec!["undo:a"]);

	// Empty multi command is discarded on push: count stays 2 (bottom
	// "New/Open Project" + the undone pre-executed command), not 3.
	let mut multi = unsafe { oakundo_command_init_multi() };
	let name2 = CString::new("Empty").unwrap();
	assert_eq!(
		unsafe { oakundo_undostack_push(stack, multi, name2.as_ptr()) },
		OAKUNDO_OK
	);
	unsafe { oakundo_command_free(&mut multi) };
	assert_eq!(
		unsafe { oakundo_undostack_count(stack, &mut count) },
		OAKUNDO_OK
	);
	assert_eq!(count, 2, "empty multi is discarded on push");

	unsafe { oakundo_undostack_free(&mut stack) };
}

/// Every handle-returning export returns `ctx == NULL` on failure and a
/// valid handle (`abi_version` stamped) on success; `free(NULL)` /
/// `free(empty)` are no-ops across command and stack families.
#[test]
fn handle_and_free_contract() {
	let (_trace, mut probes) = setup();

	// init with a NULL vtable → empty handle.
	let empty = unsafe { oakundo_command_init(std::ptr::null(), std::ptr::null_mut()) };
	assert!(empty.ctx.is_null());
	assert_eq!(empty.abi_version, 0);
	assert!(empty.addref.is_none() && empty.release.is_none());

	// init with a valid vtable → stamped handle.
	let mut cmd = unsafe { make_cmd(&mut probes[0] as *mut Probe) };
	assert!(!cmd.ctx.is_null());
	assert_eq!(cmd.abi_version, OAKUNDO_ABI_VERSION);

	// init_multi → stamped handle.
	let mut multi = unsafe { oakundo_command_init_multi() };
	assert!(!multi.ctx.is_null());
	assert_eq!(multi.abi_version, OAKUNDO_ABI_VERSION);

	// init stack → stamped handle.
	let mut stack = unsafe { new_stack() };
	assert!(!stack.ctx.is_null());
	assert_eq!(stack.abi_version, OAKUNDO_ABI_VERSION);

	// free(NULL) is a no-op for both families.
	unsafe { oakundo_command_free(std::ptr::null_mut()) };
	unsafe { oakundo_undostack_free(std::ptr::null_mut()) };

	// free(empty handle value) is a no-op.
	let mut ecmd = empty_cmd();
	unsafe { oakundo_command_free(&mut ecmd) };
	assert!(ecmd.ctx.is_null());
	let mut estack = empty_stack();
	unsafe { oakundo_undostack_free(&mut estack) };
	assert!(estack.ctx.is_null());

	// free(valid) clears ctx.
	unsafe { oakundo_command_free(&mut multi) };
	assert!(multi.ctx.is_null());
	unsafe { oakundo_command_free(&mut cmd) };
	assert!(cmd.ctx.is_null());
	unsafe { oakundo_undostack_free(&mut stack) };
	assert!(stack.ctx.is_null());
}

// ---------------------------------------------------------------------------
// Exhaustive per-export success/failure coverage
// ---------------------------------------------------------------------------

#[test]
fn command_redo_undo_now_null_is_invalid() {
	assert_eq!(
		unsafe { oakundo_command_redo_now(empty_cmd()) },
		OAKUNDO_E_INVALID
	);
	assert_eq!(
		unsafe { oakundo_command_undo_now(empty_cmd()) },
		OAKUNDO_E_INVALID
	);
}

#[test]
fn command_multi_add_child_errors() {
	let (_trace, mut probes) = setup();
	let mut multi = unsafe { oakundo_command_init_multi() };
	let mut child = unsafe { make_cmd(&mut probes[0] as *mut Probe) };
	let mut vtable_cmd = unsafe { make_cmd(&mut probes[1] as *mut Probe) };

	// multi is null → E_INVALID.
	assert_eq!(
		unsafe { oakundo_command_multi_add_child(empty_cmd(), child) },
		OAKUNDO_E_INVALID
	);
	// child is null → E_INVALID (nothing taken).
	assert_eq!(
		unsafe { oakundo_command_multi_add_child(multi, empty_cmd()) },
		OAKUNDO_E_INVALID
	);
	// target is a vtable command, not a multi → E_INVALID.
	assert_eq!(
		unsafe { oakundo_command_multi_add_child(vtable_cmd, child) },
		OAKUNDO_E_INVALID
	);

	// The child handle still owns its value (never taken), so free it.
	unsafe { oakundo_command_free(&mut child) };
	unsafe { oakundo_command_free(&mut vtable_cmd) };
	unsafe { oakundo_command_free(&mut multi) };
}

#[test]
fn command_multi_child_count_errors() {
	let (_trace, mut probes) = setup();
	let mut multi = unsafe { oakundo_command_init_multi() };
	let mut vtable_cmd = unsafe { make_cmd(&mut probes[0] as *mut Probe) };
	let mut out: c_int = -1;

	// null out pointer → E_INVALID.
	assert_eq!(
		unsafe { oakundo_command_multi_child_count(multi, std::ptr::null_mut()) },
		OAKUNDO_E_INVALID
	);
	// null multi → E_INVALID.
	assert_eq!(
		unsafe { oakundo_command_multi_child_count(empty_cmd(), &mut out) },
		OAKUNDO_E_INVALID
	);
	// non-multi command → E_INVALID.
	assert_eq!(
		unsafe { oakundo_command_multi_child_count(vtable_cmd, &mut out) },
		OAKUNDO_E_INVALID
	);

	unsafe { oakundo_command_free(&mut vtable_cmd) };
	unsafe { oakundo_command_free(&mut multi) };
}

#[test]
fn command_multi_child_errors() {
	let (_trace, mut probes) = setup();
	let mut multi = unsafe { oakundo_command_init_multi() };
	let mut vtable_cmd = unsafe { make_cmd(&mut probes[0] as *mut Probe) };
	let mut out = empty_cmd();

	// null out pointer → E_INVALID.
	assert_eq!(
		unsafe { oakundo_command_multi_child(multi, 0, std::ptr::null_mut()) },
		OAKUNDO_E_INVALID
	);
	// null multi → E_INVALID.
	assert_eq!(
		unsafe { oakundo_command_multi_child(empty_cmd(), 0, &mut out) },
		OAKUNDO_E_INVALID
	);
	// non-multi command → E_INVALID.
	assert_eq!(
		unsafe { oakundo_command_multi_child(vtable_cmd, 0, &mut out) },
		OAKUNDO_E_INVALID
	);
	// empty multi, negative index → E_NOT_FOUND.
	assert_eq!(
		unsafe { oakundo_command_multi_child(multi, -1, &mut out) },
		OAKUNDO_E_NOT_FOUND
	);
	// empty multi, positive OOB → E_NOT_FOUND.
	assert_eq!(
		unsafe { oakundo_command_multi_child(multi, 5, &mut out) },
		OAKUNDO_E_NOT_FOUND
	);

	unsafe { oakundo_command_free(&mut vtable_cmd) };
	unsafe { oakundo_command_free(&mut multi) };
}

#[test]
fn command_multi_child_success_borrowed() {
	let (trace, mut probes) = setup();
	let mut multi = unsafe { oakundo_command_init_multi() };
	let mut ca = unsafe { make_cmd(&mut probes[0] as *mut Probe) };
	unsafe { oakundo_command_multi_add_child(multi, ca) };

	let mut child = empty_cmd();
	assert_eq!(
		unsafe { oakundo_command_multi_child(multi, 0, &mut child) },
		OAKUNDO_OK
	);
	assert!(!child.ctx.is_null());
	// A borrowed child is not independently owned: freeing it frees only the
	// shell and must not free the child the multi still owns.
	unsafe { oakundo_command_free(&mut child) };
	assert!(child.ctx.is_null());

	// Redo through the multi still works and no child was freed.
	assert_eq!(unsafe { oakundo_command_redo_now(multi) }, OAKUNDO_OK);
	assert_eq!(events(&trace), vec!["redo:a"]);

	unsafe { oakundo_command_free(&mut multi) };
	assert_eq!(events(&trace), vec!["redo:a", "free:a"]);
}

#[test]
fn command_free_fires_exactly_once() {
	let (trace, mut probes) = setup();
	let mut cmd = unsafe { make_cmd(&mut probes[0] as *mut Probe) };

	// free → one free callback.
	unsafe { oakundo_command_free(&mut cmd) };
	assert_eq!(events(&trace), vec!["free:a"]);

	// free again on a cleared handle → no-op.
	unsafe { oakundo_command_free(&mut cmd) };
	assert_eq!(events(&trace), vec!["free:a"]);
}

#[test]
fn undostack_free_null_and_empty() {
	let mut stack = empty_stack();
	unsafe { oakundo_undostack_free(std::ptr::null_mut()) };
	unsafe { oakundo_undostack_free(&mut stack) };
	assert!(stack.ctx.is_null());
}

#[test]
fn undostack_push_errors() {
	let (trace, mut probes) = setup();
	let mut stack = unsafe { new_stack() };
	let mut cmd = unsafe { make_cmd(&mut probes[0] as *mut Probe) };
	let name = CString::new("A").unwrap();

	// null command → E_INVALID; stack untouched, command still owned.
	assert_eq!(
		unsafe { oakundo_undostack_push(stack, empty_cmd(), name.as_ptr()) },
		OAKUNDO_E_INVALID
	);
	// empty stack → E_INVALID; command NOT drained (still owns its value).
	assert_eq!(
		unsafe { oakundo_undostack_push(empty_stack(), cmd, name.as_ptr()) },
		OAKUNDO_E_INVALID
	);
	assert_eq!(
		events(&trace),
		Vec::<String>::new(),
		"no callbacks ran on failed push"
	);

	// NULL name is accepted as an empty label → OK.
	assert_eq!(
		unsafe { oakundo_undostack_push(stack, cmd, std::ptr::null()) },
		OAKUNDO_OK
	);

	unsafe { oakundo_command_free(&mut cmd) }; // now a non-owning shell
	unsafe { oakundo_undostack_free(&mut stack) };
}

#[test]
fn undostack_push_pre_executed_errors() {
	let (_trace, mut probes) = setup();
	let mut stack = unsafe { new_stack() };
	let mut cmd = unsafe { make_cmd(&mut probes[0] as *mut Probe) };
	let name = CString::new("A").unwrap();

	assert_eq!(
		unsafe { oakundo_undostack_push_pre_executed(empty_stack(), cmd, name.as_ptr()) },
		OAKUNDO_E_INVALID
	);
	// command was not drained; still owned, so free it.
	unsafe { oakundo_command_free(&mut cmd) };
	assert_eq!(
		unsafe { oakundo_undostack_push_pre_executed(stack, empty_cmd(), name.as_ptr()) },
		OAKUNDO_E_INVALID
	);

	unsafe { oakundo_undostack_free(&mut stack) };
}

#[test]
fn undostack_undo_redo_errors() {
	let mut stack = unsafe { new_stack() };
	assert_eq!(
		unsafe { oakundo_undostack_undo(empty_stack()) },
		OAKUNDO_E_INVALID
	);
	assert_eq!(
		unsafe { oakundo_undostack_redo(empty_stack()) },
		OAKUNDO_E_INVALID
	);
	// Valid stack, but nothing to undo: still OK (no-op).
	assert_eq!(unsafe { oakundo_undostack_undo(stack) }, OAKUNDO_OK);
	assert_eq!(unsafe { oakundo_undostack_redo(stack) }, OAKUNDO_OK);
	unsafe { oakundo_undostack_free(&mut stack) };
}

#[test]
fn undostack_jump_clear_errors() {
	let mut stack = unsafe { new_stack() };
	assert_eq!(
		unsafe { oakundo_undostack_jump(empty_stack(), 3) },
		OAKUNDO_E_INVALID
	);
	assert_eq!(
		unsafe { oakundo_undostack_clear(empty_stack()) },
		OAKUNDO_E_INVALID
	);

	// Clear on a valid stack resets to the empty bottom command.
	let mut count: i64 = 0;
	unsafe { oakundo_undostack_clear(stack) };
	assert_eq!(
		unsafe { oakundo_undostack_count(stack, &mut count) },
		OAKUNDO_OK
	);
	assert_eq!(count, 1);
	unsafe { oakundo_undostack_free(&mut stack) };
}

#[test]
fn undostack_can_undo_redo_errors() {
	let mut stack = unsafe { new_stack() };
	let mut value: c_int = 0;
	assert_eq!(
		unsafe { oakundo_undostack_can_undo(empty_stack(), &mut value) },
		OAKUNDO_E_INVALID
	);
	assert_eq!(
		unsafe { oakundo_undostack_can_redo(empty_stack(), &mut value) },
		OAKUNDO_E_INVALID
	);
	assert_eq!(
		unsafe { oakundo_undostack_can_undo(stack, std::ptr::null_mut()) },
		OAKUNDO_E_INVALID
	);
	assert_eq!(
		unsafe { oakundo_undostack_can_redo(stack, std::ptr::null_mut()) },
		OAKUNDO_E_INVALID
	);
	unsafe { oakundo_undostack_free(&mut stack) };
}

#[test]
fn undostack_count_index_errors() {
	let mut stack = unsafe { new_stack() };
	let mut out: i64 = 0;
	assert_eq!(
		unsafe { oakundo_undostack_count(empty_stack(), &mut out) },
		OAKUNDO_E_INVALID
	);
	assert_eq!(
		unsafe { oakundo_undostack_index(empty_stack(), &mut out) },
		OAKUNDO_E_INVALID
	);
	assert_eq!(
		unsafe { oakundo_undostack_count(stack, std::ptr::null_mut()) },
		OAKUNDO_E_INVALID
	);
	assert_eq!(
		unsafe { oakundo_undostack_index(stack, std::ptr::null_mut()) },
		OAKUNDO_E_INVALID
	);
	unsafe { oakundo_undostack_free(&mut stack) };
}

#[test]
fn undostack_command_text_two_stage_and_errors() {
	let (_trace, mut probes) = setup();
	let mut stack = unsafe { new_stack() };
	let name = CString::new("MyAction").unwrap();
	let mut cmd = unsafe { make_cmd(&mut probes[0] as *mut Probe) };
	unsafe { oakundo_undostack_push(stack, cmd, name.as_ptr()) };

	// Failure: empty stack → E_INVALID.
	assert_eq!(
		unsafe { oakundo_undostack_command_text(empty_stack(), 1, std::ptr::null_mut(), 0) },
		OAKUNDO_E_INVALID
	);
	// Failure: OOB row (positive) → E_NOT_FOUND.
	assert_eq!(
		unsafe { oakundo_undostack_command_text(stack, 5, std::ptr::null_mut(), 0) },
		OAKUNDO_E_NOT_FOUND
	);
	// Failure: negative row → E_NOT_FOUND.
	assert_eq!(
		unsafe { oakundo_undostack_command_text(stack, -1, std::ptr::null_mut(), 0) },
		OAKUNDO_E_NOT_FOUND
	);

	// Stage one: null buffer returns the required size ("MyAction" + NUL).
	let required = unsafe { oakundo_undostack_command_text(stack, 1, std::ptr::null_mut(), 0) };
	assert_eq!(required, "MyAction".len() as c_int + 1);

	// Stage two: a buffer of that size is populated with a NUL-terminated
	// string and the required size is returned again.
	let mut buf = vec![0u8; required as usize];
	let ret = unsafe {
		oakundo_undostack_command_text(stack, 1, buf.as_mut_ptr() as *mut c_char, required)
	};
	assert_eq!(ret, required);
	let actual = unsafe { std::ffi::CStr::from_ptr(buf.as_ptr() as *const c_char) };
	assert_eq!(actual.to_bytes(), b"MyAction");

	// A too-small buffer is safely truncated (NUL-terminated).
	let mut small = vec![0xffu8; 3];
	unsafe { oakundo_undostack_command_text(stack, 1, small.as_mut_ptr() as *mut c_char, 3) };
	assert_eq!(small, [b'M', b'y', 0]);

	unsafe { oakundo_command_free(&mut cmd) };
	unsafe { oakundo_undostack_free(&mut stack) };
}

#[test]
fn undostack_command_is_done_errors() {
	let mut stack = unsafe { new_stack() };
	let mut value: c_int = 0;
	assert_eq!(
		unsafe { oakundo_undostack_command_is_done(empty_stack(), 0, &mut value) },
		OAKUNDO_E_INVALID
	);
	assert_eq!(
		unsafe { oakundo_undostack_command_is_done(stack, 0, std::ptr::null_mut()) },
		OAKUNDO_E_INVALID
	);
	assert_eq!(
		unsafe { oakundo_undostack_command_is_done(stack, 5, &mut value) },
		OAKUNDO_E_NOT_FOUND
	);
	assert_eq!(
		unsafe { oakundo_undostack_command_is_done(stack, -1, &mut value) },
		OAKUNDO_E_NOT_FOUND
	);
	unsafe { oakundo_undostack_free(&mut stack) };
}

#[test]
fn undostack_push_drops_redoable_tail_via_ffi() {
	let (trace, mut probes) = setup();
	let mut stack = unsafe { new_stack() };
	let na = CString::new("A").unwrap();
	let nb = CString::new("B").unwrap();
	let mut ca = unsafe { make_cmd(&mut probes[0] as *mut Probe) };
	unsafe { oakundo_undostack_push(stack, ca, na.as_ptr()) };
	let mut cb = unsafe { make_cmd(&mut probes[1] as *mut Probe) };
	unsafe { oakundo_undostack_push(stack, cb, nb.as_ptr()) };

	// Undo B, then push C → the redoable tail is dropped.
	assert_eq!(unsafe { oakundo_undostack_undo(stack) }, OAKUNDO_OK);
	let mut value: c_int = 0;
	assert_eq!(
		unsafe { oakundo_undostack_can_redo(stack, &mut value) },
		OAKUNDO_OK
	);
	assert_eq!(value, 1);

	let nc = CString::new("C").unwrap();
	let mut cc = unsafe { make_cmd(&mut probes[2] as *mut Probe) };
	assert_eq!(
		unsafe { oakundo_undostack_push(stack, cc, nc.as_ptr()) },
		OAKUNDO_OK
	);

	assert_eq!(
		unsafe { oakundo_undostack_can_redo(stack, &mut value) },
		OAKUNDO_OK
	);
	assert_eq!(value, 0, "pushing drops the redoable tail");
	assert_eq!(
		events(&trace),
		vec!["redo:a", "redo:b", "undo:b", "free:b", "redo:c"]
	);

	unsafe { oakundo_command_free(&mut ca) };
	unsafe { oakundo_command_free(&mut cb) };
	unsafe { oakundo_command_free(&mut cc) };
	unsafe { oakundo_undostack_free(&mut stack) };
}
