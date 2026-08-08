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

//! Safe-layer behavior matrix for `UndoCommand` / `UndoStack`
//! (mirrors `src/undo/src/undostack.cpp` / `undocommand.cpp`).

use std::cell::RefCell;
use std::ffi::c_void;

use oakundo::error::Error;
use oakundo::undocommand::{OakUndoCommandVtable, UndoCommand};
use oakundo::undostack::{EmptyCommand, UndoStack, K_MAX_UNDO_COMMANDS};

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

/// A vtable-backed command whose callbacks record into `probe`.
fn trace_cmd(probe: &mut Probe) -> UndoCommand {
	let vtable = OakUndoCommandVtable {
		redo: Some(probe_redo),
		undo: Some(probe_undo),
		free_fn: None,
	};
	UndoCommand::from_vtable(vtable, probe as *mut Probe as *mut c_void)
}

fn events(trace: &Trace) -> Vec<String> {
	trace.events.borrow().clone()
}

fn setup() -> (Box<Trace>, Vec<Probe>) {
	let trace = Box::new(Trace {
		events: RefCell::new(Vec::new()),
	});
	let ptr = &*trace as *const Trace;
	let mut probes = Vec::new();
	for name in ["a", "b", "c"] {
		probes.push(Probe {
			name,
			trace: ptr,
		});
	}
	(trace, probes)
}

#[test]
fn command_redo_undo_lifecycle() {
	let (trace, mut probes) = setup();
	let mut cmd = trace_cmd(&mut probes[0]);

	assert!(!cmd.is_done());
	assert!(cmd.has_prepared());
	assert_eq!(events(&trace), Vec::<String>::new());

	cmd.redo_now();
	assert!(cmd.is_done());
	assert_eq!(events(&trace), vec!["redo:a"]);

	// Idempotent: a second redo does nothing.
	cmd.redo_now();
	assert_eq!(events(&trace), vec!["redo:a"]);

	cmd.undo_now();
	assert!(!cmd.is_done());
	assert_eq!(events(&trace), vec!["redo:a", "undo:a"]);

	// Idempotent undo.
	cmd.undo_now();
	assert_eq!(events(&trace), vec!["redo:a", "undo:a"]);

	// redo_and_set_modified / undo_and_set_modified alias the *_now forms.
	cmd.redo_and_set_modified();
	cmd.undo_and_set_modified();
	assert_eq!(
		events(&trace),
		vec!["redo:a", "undo:a", "redo:a", "undo:a"]
	);

	// set_done marks executed without running anything.
	let mut probe = Probe {
		name: "x",
		trace: probes[0].trace,
	};
	let mut cmd2 = trace_cmd(&mut probe);
	cmd2.set_done(true);
	assert!(cmd2.is_done());
	cmd2.redo_now(); // no-op (already done)
	assert_eq!(events(&trace), vec!["redo:a", "undo:a", "redo:a", "undo:a"]);
}

#[test]
fn empty_command_is_noop() {
	let mut cmd = EmptyCommand::new();
	assert!(cmd.has_prepared());
	cmd.redo_now();
	assert!(cmd.is_done());
	cmd.undo_now();
	assert!(!cmd.is_done());
	// No panics, no side effects.
}

#[test]
fn multi_redo_undo_ordering() {
	let (trace, mut probes) = setup();
	let mut multi = UndoCommand::multi();
	multi.multi_add_child(trace_cmd(&mut probes[0]));
	multi.multi_add_child(trace_cmd(&mut probes[1]));
	multi.multi_add_child(trace_cmd(&mut probes[2]));

	assert_eq!(multi.multi_child_count(), 3);
	// Children are reachable and named.
	assert_eq!(multi.multi_child(0).unwrap().is_done(), false);
	assert_eq!(multi.multi_child(2).unwrap().is_done(), false);
	assert!(matches!(multi.multi_child(3), Err(Error::NotFound)));

	// Redo in insertion order; undo in reverse.
	multi.redo_now();
	assert!(multi.is_done());
	assert_eq!(events(&trace), vec!["redo:a", "redo:b", "redo:c"]);

	multi.undo_now();
	assert_eq!(
		events(&trace),
		vec!["redo:a", "redo:b", "redo:c", "undo:c", "undo:b", "undo:a"]
	);
}

#[test]
#[should_panic]
fn multi_add_child_on_vtable_panics() {
	let (_trace, mut probes) = setup();
	let mut vtable_cmd = trace_cmd(&mut probes[0]);
	vtable_cmd.multi_add_child(trace_cmd(&mut probes[1]));
}

#[test]
fn multi_child_helpers_on_non_multi() {
	let (_trace, mut probes) = setup();
	let mut vtable_cmd = trace_cmd(&mut probes[0]);
	assert_eq!(vtable_cmd.multi_child_count(), 0);
	assert!(matches!(vtable_cmd.multi_child(0), Err(Error::Invalid)));
}

#[test]
fn stack_new_has_empty_bottom() {
	let s = UndoStack::new();
	assert_eq!(s.command_count(), 1);
	assert_eq!(s.done_count(), 1);
	assert!(!s.can_undo());
	assert!(!s.can_redo());
	assert_eq!(s.command_name(0).unwrap(), "New/Open Project");
	assert_eq!(s.command_is_done(0).unwrap(), true);
}

#[test]
fn stack_push_undo_redo_branch() {
	let (trace, mut probes) = setup();
	let mut s = UndoStack::new();

	s.push(trace_cmd(&mut probes[0]), "A");
	s.push(trace_cmd(&mut probes[1]), "B");
	assert_eq!(s.command_count(), 3);
	assert!(s.can_undo());
	assert!(!s.can_redo());
	assert_eq!(events(&trace), vec!["redo:a", "redo:b"]);

	// Undo the top.
	s.undo().unwrap();
	assert_eq!(events(&trace), vec!["redo:a", "redo:b", "undo:b"]);
	assert!(s.can_redo());
	assert_eq!(s.done_count(), 2);
	assert_eq!(s.command_name(2).unwrap(), "B"); // undone row is still labeled
	assert_eq!(s.command_is_done(2).unwrap(), false);

	// Redo restores.
	s.redo().unwrap();
	assert_eq!(
		events(&trace),
		vec!["redo:a", "redo:b", "undo:b", "redo:b"]
	);
	assert_eq!(s.done_count(), 3);

	// Undo then push drops the redoable tail.
	s.undo().unwrap();
	assert!(s.can_redo());
	s.push(trace_cmd(&mut probes[2]), "C");
	assert!(!s.can_redo(), "pushing drops redoable tail");
	assert_eq!(s.command_count(), 3);
	assert_eq!(s.command_name(2).unwrap(), "C");
}

#[test]
fn stack_undo_redo_noop_when_invalid() {
	let mut s = UndoStack::new();
	// Undo on a stack with only the bottom empty command is a no-op.
	s.undo().unwrap();
	assert_eq!(s.command_count(), 1);
	assert!(!s.can_redo());
	s.redo().unwrap();
	assert_eq!(s.command_count(), 1);
}

#[test]
fn stack_jump_clamps_and_lands() {
	let (trace, mut probes) = setup();
	let mut s = UndoStack::new();
	s.push(trace_cmd(&mut probes[0]), "A");
	s.push(trace_cmd(&mut probes[1]), "B");
	s.push(trace_cmd(&mut probes[2]), "C");
	assert_eq!(s.done_count(), 4); // empty + A + B + C

	// Jump back to just the empty command (clamps negative to 0).
	s.jump(0);
	assert_eq!(s.done_count(), 1);
	assert!(!s.can_undo());
	assert!(s.can_redo());
	assert_eq!(
		events(&trace),
		vec![
			"redo:a",
			"redo:b",
			"redo:c",
			"undo:c",
			"undo:b",
			"undo:a"
		]
	);

	// Jump forward redoes in order.
	s.jump(3);
	assert_eq!(s.done_count(), 3);
	assert_eq!(
		events(&trace),
		vec![
			"redo:a",
			"redo:b",
			"redo:c",
			"undo:c",
			"undo:b",
			"undo:a",
			"redo:a",
			"redo:b"
		]
	);

	// Jump beyond the top redoes up to the top (matches the C++ `jump`:
	// the undone C is redoable, so the second loop runs).
	s.jump(100);
	assert_eq!(s.done_count(), 4);
}

#[test]
fn stack_discards_empty_multi() {
	let mut s = UndoStack::new();
	let empty_multi = UndoCommand::multi();
	s.push(empty_multi, "empty");
	assert_eq!(
		s.command_count(),
		1,
		"empty multi command is dropped on push"
	);

	s.push_pre_executed(UndoCommand::multi(), "empty2");
	assert_eq!(s.command_count(), 1);
}

#[test]
fn stack_push_pre_executed_skips_redo() {
	let (trace, mut probes) = setup();
	let mut s = UndoStack::new();
	s.push_pre_executed(trace_cmd(&mut probes[0]), "A");
	assert_eq!(
		events(&trace),
		Vec::<String>::new(),
		"push_pre_executed does not run redo"
	);
	assert_eq!(s.command_count(), 2);
	assert!(s.can_undo());
	assert!(s.command_is_done(1).unwrap());

	// Undoing still runs the undo callback.
	s.undo().unwrap();
	assert_eq!(events(&trace), vec!["undo:a"]);
}

#[test]
fn stack_clear_resets_to_bottom() {
	let (trace, mut probes) = setup();
	let mut s = UndoStack::new();
	s.push(trace_cmd(&mut probes[0]), "A");
	s.undo().unwrap();
	assert_eq!(s.command_count(), 2);

	s.clear();
	assert_eq!(s.command_count(), 1);
	assert_eq!(s.command_name(0).unwrap(), "New/Open Project");
	assert!(!s.can_undo());
	assert!(!s.can_redo());
}

#[test]
fn stack_caps_at_k_max() {
	let (_trace, mut probes) = setup();
	let mut s = UndoStack::new();
	for i in 0..(K_MAX_UNDO_COMMANDS + 20) {
		let mut probe = Probe {
			name: "x",
			trace: probes[0].trace,
		};
		let _ = &mut probe;
		s.push(trace_cmd(&mut probes[i % 3]), &format!("cmd{i}"));
	}
	assert_eq!(s.command_count() as usize, K_MAX_UNDO_COMMANDS);
}

#[test]
fn stack_query_bounds_errors() {
	let mut s = UndoStack::new();
	assert!(matches!(s.command_name(-1), Err(Error::NotFound)));
	assert!(matches!(s.command_name(1), Err(Error::NotFound)));
	assert!(matches!(s.command_is_done(-1), Err(Error::NotFound)));
	assert!(matches!(s.command_is_done(1), Err(Error::NotFound)));
}
