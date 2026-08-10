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

//! Integration tests for the undo family (`engine/include/oakengine/undo.h`,
//! implemented by `src/undo.rs` on top of the real oakundo module crate) —
//! the "real behavior, end to end" complement to the smoke tests in
//! `tests/undo.rs`. No mocks: every call goes through the facade exports
//! into the real oakundo crate.
//!
//! The facade owns a process-wide undo stack and a single open undo group
//! (the module 00 analogue of `EngineCore::undo_stack()` / `g_undo_group`),
//! so every stack- and group-mutating assertion lives in ONE serialized
//! test function ([`undo_stack_integration`]). The command-lifecycle tests
//! only touch local state and run in parallel.
//!
//! Coverage: all 23 `oakengine_undo_*` exports are called on a legal path
//! with asserted results, plus the illegal-input matrix (NULL pointers,
//! empty `CHandle::null()` boxes, out-of-range rows, zero/negative buffer
//! sizes) and the free/destroy contracts. No function in this family needs
//! GPU/app state. The only `#[ignore]`d tests are the real-bug repros at
//! the bottom ([`null_name_push_repro`], [`null_name_group_repro`],
//! [`group_abort_undoes_children_repro`]) — a NULL/empty label to
//! `oakengine_undo_push` / the group-end path crashes the process, and
//! `oakengine_undo_group_abort` does not undo its children (see the
//! report).

#[path = "common/mod.rs"]
mod common;


use std::ffi::{c_char, c_void};
use std::sync::atomic::{AtomicI32, AtomicUsize, Ordering};
use std::sync::Mutex;

use oakengine::handle::{CHandle, OakEngineClipboard};
use oakengine::undo::{
	oakengine_undo_can_redo, oakengine_undo_can_undo, oakengine_undo_clear,
	oakengine_undo_command_create, oakengine_undo_command_create_multi,
	oakengine_undo_command_free, oakengine_undo_command_is_done,
	oakengine_undo_command_multi_add_child, oakengine_undo_command_multi_child_count,
	oakengine_undo_command_redo_now, oakengine_undo_command_text,
	oakengine_undo_command_undo_now, oakengine_undo_count, oakengine_undo_group_abort,
	oakengine_undo_group_begin, oakengine_undo_group_end, oakengine_undo_handle,
	oakengine_undo_index, oakengine_undo_jump, oakengine_undo_push, oakengine_undo_redo_action,
	oakengine_undo_undo_action, oakengine_undo_update_actions,
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Box a `CHandle::null()` inside an `OakEngineClipboard` — a VALID box
/// whose module handle is empty (what a plugin would hold after its own
/// handle object went away). The facade must reject it with a clean error
/// code, never crash.
fn empty_engine_ptr() -> *mut c_void {
	Box::into_raw(Box::new(OakEngineClipboard {
		handle: CHandle::null(),
	}))
	.cast()
}

/// Read back the NUL-terminated string the facade wrote into `buf`.
unsafe fn read_str(buf: *const c_char) -> String {
	unsafe { std::ffi::CStr::from_ptr(buf) }
		.to_str()
		.unwrap()
		.to_string()
}

// ---------------------------------------------------------------------------
// Command-lifecycle callbacks (parallel tests only; the serialized stack
// test uses the STK_* counters below and never touches these).
// ---------------------------------------------------------------------------

static LIFECYCLE_REDO: AtomicI32 = AtomicI32::new(0);
static LIFECYCLE_UNDO: AtomicI32 = AtomicI32::new(0);
static LIFECYCLE_FREE: AtomicI32 = AtomicI32::new(0);
static LIFECYCLE_FREED_PTR: AtomicUsize = AtomicUsize::new(0);

/// Own counter set for `command_create_variants` (the lifecycle tests run
/// in parallel, so they must not share atomics).
static VARIANTS_FREE: AtomicI32 = AtomicI32::new(0);

unsafe extern "C" fn lifecycle_redo(_ud: *mut c_void) {
	LIFECYCLE_REDO.fetch_add(1, Ordering::SeqCst);
}

unsafe extern "C" fn lifecycle_undo(_ud: *mut c_void) {
	LIFECYCLE_UNDO.fetch_add(1, Ordering::SeqCst);
}

unsafe extern "C" fn lifecycle_free(_ud: *mut c_void) {
	LIFECYCLE_FREE.fetch_add(1, Ordering::SeqCst);
}

/// No-op callback for `command_create_variants` (avoids touching the
/// lifecycle counters, which run in a parallel test).
unsafe extern "C" fn variants_noop(_ud: *mut c_void) {}

unsafe extern "C" fn variants_free(_ud: *mut c_void) {
	VARIANTS_FREE.fetch_add(1, Ordering::SeqCst);
}

/// free_fn that records the pointer and drops the boxed `u64` userdata
/// (round-trip ownership check).
unsafe extern "C" fn lifecycle_free_userdata(ud: *mut c_void) {
	LIFECYCLE_FREED_PTR.store(ud as usize, Ordering::SeqCst);
	LIFECYCLE_FREE.fetch_add(1, Ordering::SeqCst);
	drop(Box::from_raw(ud as *mut u64));
}

/// Child redo/undo callbacks that log their id (encoded in userdata) — used
/// to verify multi redo order (insertion) and undo order (reverse).
static MULTI_LOG: Mutex<Vec<i32>> = Mutex::new(Vec::new());

unsafe extern "C" fn multi_redo(ud: *mut c_void) {
	MULTI_LOG.lock().unwrap().push(ud as usize as i32);
}

unsafe extern "C" fn multi_undo(ud: *mut c_void) {
	MULTI_LOG.lock().unwrap().push(ud as usize as i32);
}

static MULTI_FREE: AtomicI32 = AtomicI32::new(0);

unsafe extern "C" fn multi_free(_ud: *mut c_void) {
	MULTI_FREE.fetch_add(1, Ordering::SeqCst);
}

/// free_fn for the module-level destroy-contract test.
static MOD_FREE: AtomicI32 = AtomicI32::new(0);

unsafe extern "C" fn mod_free_cb(_ud: *mut c_void) {
	MOD_FREE.fetch_add(1, Ordering::SeqCst);
}

// ---------------------------------------------------------------------------
// Serialized stack-test callbacks (own counters; the parallel command
// tests never touch these).
// ---------------------------------------------------------------------------

static STK_REDO: AtomicI32 = AtomicI32::new(0);
static STK_UNDO: AtomicI32 = AtomicI32::new(0);

unsafe extern "C" fn stk_redo(_ud: *mut c_void) {
	STK_REDO.fetch_add(1, Ordering::SeqCst);
}

unsafe extern "C" fn stk_undo(_ud: *mut c_void) {
	STK_UNDO.fetch_add(1, Ordering::SeqCst);
}

// ---------------------------------------------------------------------------
// Command lifecycle (parallel-safe: no global-stack state)
// ---------------------------------------------------------------------------

/// Full legal lifecycle of an app-defined command: create with name +
/// callbacks + owned userdata, redo/undo (idempotent), destroy via free —
/// the free_fn fires exactly once, with the same userdata pointer.
#[test]
fn command_lifecycle_roundtrip() {
	common::force_link();
	LIFECYCLE_REDO.store(0, Ordering::SeqCst);
	LIFECYCLE_UNDO.store(0, Ordering::SeqCst);
	LIFECYCLE_FREE.store(0, Ordering::SeqCst);
	LIFECYCLE_FREED_PTR.store(0, Ordering::SeqCst);

	let ud = Box::into_raw(Box::new(42u64)) as *mut c_void;
	let cmd = unsafe {
		oakengine_undo_command_create(
			c"roundtrip".as_ptr(),
			Some(lifecycle_redo),
			Some(lifecycle_undo),
			Some(lifecycle_free_userdata),
			ud,
		)
	};
	assert!(!cmd.is_null());

	assert_eq!(unsafe { oakengine_undo_command_redo_now(cmd) }, 0);
	assert_eq!(LIFECYCLE_REDO.load(Ordering::SeqCst), 1);
	// redo on a done command is a no-op (olive semantics).
	assert_eq!(unsafe { oakengine_undo_command_redo_now(cmd) }, 0);
	assert_eq!(LIFECYCLE_REDO.load(Ordering::SeqCst), 1);

	assert_eq!(unsafe { oakengine_undo_command_undo_now(cmd) }, 0);
	assert_eq!(LIFECYCLE_UNDO.load(Ordering::SeqCst), 1);
	// undo on an undone command is a no-op.
	assert_eq!(unsafe { oakengine_undo_command_undo_now(cmd) }, 0);
	assert_eq!(LIFECYCLE_UNDO.load(Ordering::SeqCst), 1);

	assert_eq!(unsafe { oakengine_undo_command_redo_now(cmd) }, 0);
	assert_eq!(LIFECYCLE_REDO.load(Ordering::SeqCst), 2);

	unsafe { oakengine_undo_command_free(cmd) };
	assert_eq!(LIFECYCLE_FREE.load(Ordering::SeqCst), 1);
	assert_eq!(LIFECYCLE_FREED_PTR.load(Ordering::SeqCst), ud as usize);
}

/// create() legal variants: NULL name, all-None callback table, free-only
/// table. All must produce a usable command.
#[test]
fn command_create_variants() {
	common::force_link();
	VARIANTS_FREE.store(0, Ordering::SeqCst);

	// NULL name is legal (the label is read as empty).
	let c1 = unsafe {
		oakengine_undo_command_create(
			std::ptr::null(),
			Some(variants_noop),
			Some(variants_noop),
			None,
			std::ptr::null_mut(),
		)
	};
	assert!(!c1.is_null());
	assert_eq!(unsafe { oakengine_undo_command_redo_now(c1) }, 0);
	assert_eq!(unsafe { oakengine_undo_command_undo_now(c1) }, 0);
	unsafe { oakengine_undo_command_free(c1) };

	// All-None callbacks: a no-op command, still usable.
	let c2 = unsafe {
		oakengine_undo_command_create(
			c"noop".as_ptr(),
			None,
			None,
			None,
			std::ptr::null_mut(),
		)
	};
	assert!(!c2.is_null());
	assert_eq!(unsafe { oakengine_undo_command_redo_now(c2) }, 0);
	assert_eq!(unsafe { oakengine_undo_command_undo_now(c2) }, 0);
	unsafe { oakengine_undo_command_free(c2) };

	// free-only table: destroy still invokes free_fn exactly once.
	let c3 = unsafe {
		oakengine_undo_command_create(
			c"freeonly".as_ptr(),
			None,
			None,
			Some(variants_free),
			std::ptr::null_mut(),
		)
	};
	assert!(!c3.is_null());
	unsafe { oakengine_undo_command_free(c3) };
	assert_eq!(VARIANTS_FREE.load(Ordering::SeqCst), 1);
}

/// Illegal-input robustness for the command surface: NULL pointers and
/// empty (`CHandle::null`) handles must produce clean negative codes
/// (the facade's -1 or the oakundo -20001 pass-through), never a crash.
#[test]
fn command_illegal_handle_inputs() {
	common::force_link();

	// NULL command pointers.
	assert_eq!(
		unsafe { oakengine_undo_command_redo_now(std::ptr::null_mut()) },
		-1
	);
	assert_eq!(
		unsafe { oakengine_undo_command_undo_now(std::ptr::null_mut()) },
		-1
	);
	assert_eq!(
		unsafe { oakengine_undo_command_multi_child_count(std::ptr::null_mut()) },
		-1
	);
	assert_eq!(
		unsafe {
			oakengine_undo_command_multi_add_child(std::ptr::null_mut(), std::ptr::null_mut())
		},
		-1
	);

	// Empty (CHandle::null) handles inside a valid box.
	let eb = empty_engine_ptr();
	assert_eq!(unsafe { oakengine_undo_command_redo_now(eb) }, -1);
	unsafe { oakengine_undo_command_free(eb) };

	let eb = empty_engine_ptr();
	assert_eq!(unsafe { oakengine_undo_command_undo_now(eb) }, -1);
	unsafe { oakengine_undo_command_free(eb) };

	let eb = empty_engine_ptr();
	assert_eq!(unsafe { oakengine_undo_command_multi_child_count(eb) }, -1);
	unsafe { oakengine_undo_command_free(eb) };

	// multi_add_child with an empty parent (the facade errors before
	// consuming the child, so the child box must be freed by us).
	let eb = empty_engine_ptr();
	let child = unsafe {
		oakengine_undo_command_create(
			c"child".as_ptr(),
			None,
			None,
			None,
			std::ptr::null_mut(),
		)
	};
	assert_eq!(unsafe { oakengine_undo_command_multi_add_child(eb, child) }, -1);
	unsafe { oakengine_undo_command_free(eb) };
	unsafe { oakengine_undo_command_free(child) };

	// multi_add_child with an empty child (parent untouched).
	let multi = unsafe { oakengine_undo_command_create_multi() };
	let eb = empty_engine_ptr();
	assert_eq!(unsafe { oakengine_undo_command_multi_add_child(multi, eb) }, -1);
	unsafe { oakengine_undo_command_free(eb) };
	assert_eq!(unsafe { oakengine_undo_command_multi_child_count(multi) }, 0);
	unsafe { oakengine_undo_command_free(multi) };

	// A plain (non-multi) command as the "multi" parent: the module rejects
	// with -20001 and the facade still consumes the child's box.
	let parent = unsafe {
		oakengine_undo_command_create(
			c"parent".as_ptr(),
			None,
			None,
			None,
			std::ptr::null_mut(),
		)
	};
	let child = unsafe {
		oakengine_undo_command_create(
			c"child".as_ptr(),
			None,
			None,
			None,
			std::ptr::null_mut(),
		)
	};
	assert_eq!(
		unsafe { oakengine_undo_command_multi_add_child(parent, child) },
		-20001
	);
	assert_eq!(unsafe { oakengine_undo_command_multi_child_count(parent) }, -20001);
	unsafe { oakengine_undo_command_free(parent) };
}

/// Legal-input matrix for multi commands: child counts 0→N, redo in
/// insertion order, undo in reverse order, nested multis, idempotent
/// redo/undo.
#[test]
fn multi_command_lifecycle() {
	common::force_link();
	let multi = unsafe { oakengine_undo_command_create_multi() };
	assert!(!multi.is_null());
	assert_eq!(unsafe { oakengine_undo_command_multi_child_count(multi) }, 0);

	for id in [1, 2, 3] {
		let child = unsafe {
			oakengine_undo_command_create(
				c"child".as_ptr(),
				Some(multi_redo),
				Some(multi_undo),
				None,
				id as *mut c_void,
			)
		};
		assert_eq!(unsafe { oakengine_undo_command_multi_add_child(multi, child) }, 0);
	}
	assert_eq!(unsafe { oakengine_undo_command_multi_child_count(multi) }, 3);

	*MULTI_LOG.lock().unwrap() = Vec::new();
	assert_eq!(unsafe { oakengine_undo_command_redo_now(multi) }, 0);
	assert_eq!(*MULTI_LOG.lock().unwrap(), [1, 2, 3]);
	// redo of a done multi is a no-op.
	assert_eq!(unsafe { oakengine_undo_command_redo_now(multi) }, 0);
	assert_eq!(*MULTI_LOG.lock().unwrap(), [1, 2, 3]);

	assert_eq!(unsafe { oakengine_undo_command_undo_now(multi) }, 0);
	assert_eq!(*MULTI_LOG.lock().unwrap(), [1, 2, 3, 3, 2, 1]);
	unsafe { oakengine_undo_command_free(multi) };

	// Nested multi: outer = [c10, inner([c21])]; undo runs children in
	// reverse order, inner included.
	let outer = unsafe { oakengine_undo_command_create_multi() };
	let inner = unsafe { oakengine_undo_command_create_multi() };
	let c10 = unsafe {
		oakengine_undo_command_create(
			c"c10".as_ptr(),
			Some(multi_redo),
			Some(multi_undo),
			None,
			10 as *mut c_void,
		)
	};
	let c21 = unsafe {
		oakengine_undo_command_create(
			c"c21".as_ptr(),
			Some(multi_redo),
			Some(multi_undo),
			None,
			21 as *mut c_void,
		)
	};
	assert_eq!(unsafe { oakengine_undo_command_multi_add_child(inner, c21) }, 0);
	assert_eq!(unsafe { oakengine_undo_command_multi_child_count(inner) }, 1);
	assert_eq!(unsafe { oakengine_undo_command_multi_add_child(outer, c10) }, 0);
	assert_eq!(unsafe { oakengine_undo_command_multi_add_child(outer, inner) }, 0);
	assert_eq!(unsafe { oakengine_undo_command_multi_child_count(outer) }, 2);

	*MULTI_LOG.lock().unwrap() = Vec::new();
	assert_eq!(unsafe { oakengine_undo_command_redo_now(outer) }, 0);
	assert_eq!(*MULTI_LOG.lock().unwrap(), [10, 21]);
	assert_eq!(unsafe { oakengine_undo_command_undo_now(outer) }, 0);
	assert_eq!(*MULTI_LOG.lock().unwrap(), [10, 21, 21, 10]);

	// c10 / inner / c21 were consumed by multi_add_child (their boxes are
	// freed by the facade), so only outer is freed here — the child command
	// values die with it.
	unsafe { oakengine_undo_command_free(outer) };
}

/// Destroying a multi command releases its children transitively: each
/// child's free_fn fires exactly once when the multi is freed.
#[test]
fn multi_command_free_frees_children() {
	common::force_link();
	MULTI_FREE.store(0, Ordering::SeqCst);

	let multi = unsafe { oakengine_undo_command_create_multi() };
	let inner = unsafe { oakengine_undo_command_create_multi() };
	let a = unsafe {
		oakengine_undo_command_create(
			c"a".as_ptr(),
			None,
			None,
			Some(multi_free),
			std::ptr::null_mut(),
		)
	};
	let b = unsafe {
		oakengine_undo_command_create(
			c"b".as_ptr(),
			None,
			None,
			Some(multi_free),
			std::ptr::null_mut(),
		)
	};
	let c = unsafe {
		oakengine_undo_command_create(
			c"c".as_ptr(),
			None,
			None,
			Some(multi_free),
			std::ptr::null_mut(),
		)
	};
	assert_eq!(unsafe { oakengine_undo_command_multi_add_child(inner, c) }, 0);
	assert_eq!(unsafe { oakengine_undo_command_multi_add_child(multi, a) }, 0);
	assert_eq!(unsafe { oakengine_undo_command_multi_add_child(multi, b) }, 0);
	assert_eq!(unsafe { oakengine_undo_command_multi_add_child(multi, inner) }, 0);
	assert_eq!(MULTI_FREE.load(Ordering::SeqCst), 0);

	unsafe { oakengine_undo_command_free(multi) };
	// a, b and c (via inner) are all destroyed exactly once.
	assert_eq!(MULTI_FREE.load(Ordering::SeqCst), 3);
}

/// Destroy contracts end to end: free(NULL), free(empty), and the
/// module-level double-free safety of the command/stack handles the facade
/// delegates to (`oakundo_command_free` / `oakundo_undostack_free` clear
/// `ctx` after releasing, so a second free is a no-op).
///
/// NOTE: the facade's own `oakengine_undo_command_free` frees the wrapper
/// box and is documented as "must not be freed twice"; the double-free-safe
/// contract lives on the module handle level, exercised here through the
/// real oakundo C ABI. The oakundo family has no debug alive counter, so
/// there is no alive-count-baseline to assert.
#[test]
fn free_contracts() {
	common::force_link();

	// Facade free: NULL and empty are no-ops.
	unsafe { oakengine_undo_command_free(std::ptr::null_mut()) };
	let eb = empty_engine_ptr();
	unsafe { oakengine_undo_command_free(eb) };

	// Module command handle: the first free releases (free_fn fires once)
	// and clears ctx; the second free is a no-op.
	MOD_FREE.store(0, Ordering::SeqCst);
	let vtable = oakundo::undocommand::OakUndoCommandVtable {
		redo: None,
		undo: None,
		free_fn: Some(mod_free_cb),
	};
	let mut h =
		unsafe { oakundo::ffi::command::oakundo_command_init(&vtable, std::ptr::null_mut()) };
	assert!(!h.ctx.is_null());
	unsafe { oakundo::ffi::command::oakundo_command_free(&mut h) };
	assert_eq!(MOD_FREE.load(Ordering::SeqCst), 1);
	assert!(h.ctx.is_null());
	unsafe { oakundo::ffi::command::oakundo_command_free(&mut h) };
	assert_eq!(MOD_FREE.load(Ordering::SeqCst), 1);

	// Module stack handle: double free is a no-op; NULL value and NULL
	// pointer are no-ops too.
	let mut s = unsafe { oakundo::ffi::undostack::oakundo_undostack_init() };
	assert!(!s.ctx.is_null());
	unsafe { oakundo::ffi::undostack::oakundo_undostack_free(&mut s) };
	assert!(s.ctx.is_null());
	unsafe { oakundo::ffi::undostack::oakundo_undostack_free(&mut s) };

	let mut null_h = CHandle::null();
	unsafe { oakundo::ffi::command::oakundo_command_free(&mut null_h) };
	unsafe { oakundo::ffi::command::oakundo_command_free(std::ptr::null_mut()) };
	unsafe { oakundo::ffi::undostack::oakundo_undostack_free(std::ptr::null_mut()) };
}

// ---------------------------------------------------------------------------
// Global stack + undo group (serialized: the facade's stack and open group
// are process-wide)
// ---------------------------------------------------------------------------

/// The full global-stack and undo-group matrix, serialized in one test
/// because the facade owns the process-wide stack and a single open undo
/// group. Covers every stack-scoped export: handle, clear, count, index,
/// jump, command_text, command_is_done, can_undo/can_redo, push, and the
/// group begin/end/abort lifecycle.
#[test]
fn undo_stack_integration() {
	common::force_link();

	// --- Baseline: clear() resets to the single "New/Open Project" row.
	assert_eq!(unsafe { oakengine_undo_clear() }, 0);
	assert_eq!(unsafe { oakengine_undo_count() }, 1);
	assert_eq!(unsafe { oakengine_undo_index() }, 1);
	assert_eq!(unsafe { oakengine_undo_can_undo() }, 0);
	assert_eq!(unsafe { oakengine_undo_can_redo() }, 0);

	// --- Borrowed handle + Qt-leftover actions.
	let h1 = unsafe { oakengine_undo_handle() };
	let h2 = unsafe { oakengine_undo_handle() };
	assert!(!h1.is_null());
	assert_eq!(h1, h2); // stable token
	assert_eq!(unsafe { oakengine_undo_update_actions() }, 0);
	assert!(unsafe { oakengine_undo_undo_action() }.is_null());
	assert!(unsafe { oakengine_undo_redo_action() }.is_null());

	// --- command_text / command_is_done on the base row. The two-stage
	// getter reports the length WITHOUT the trailing NUL.
	let mut buf = [0 as c_char; 64];
	assert_eq!(unsafe { oakengine_undo_command_text(0, buf.as_mut_ptr(), 64) }, 16);
	assert_eq!(unsafe { read_str(buf.as_ptr()) }, "New/Open Project");
	// NULL buf / zero / negative sizes only report the length.
	assert_eq!(unsafe { oakengine_undo_command_text(0, std::ptr::null_mut(), 64) }, 16);
	assert_eq!(unsafe { oakengine_undo_command_text(0, buf.as_mut_ptr(), 0) }, 16);
	assert_eq!(unsafe { oakengine_undo_command_text(0, buf.as_mut_ptr(), -1) }, 16);
	// Out-of-range rows → oakundo NOT_FOUND (-20004) passes through.
	assert_eq!(unsafe { oakengine_undo_command_text(-1, buf.as_mut_ptr(), 64) }, -20004);
	assert_eq!(unsafe { oakengine_undo_command_text(1, buf.as_mut_ptr(), 64) }, -20004);
	assert_eq!(unsafe { oakengine_undo_command_text(i64::MAX, buf.as_mut_ptr(), 64) }, -20004);
	assert_eq!(unsafe { oakengine_undo_command_text(i64::MIN, buf.as_mut_ptr(), 64) }, -20004);
	assert_eq!(unsafe { oakengine_undo_command_is_done(0) }, 1);
	assert_eq!(unsafe { oakengine_undo_command_is_done(-1) }, -20004);
	assert_eq!(unsafe { oakengine_undo_command_is_done(1) }, -20004);
	assert_eq!(unsafe { oakengine_undo_command_is_done(i64::MAX) }, -20004);

	// --- Push a named command; the redo runs eagerly.
	STK_REDO.store(0, Ordering::SeqCst);
	STK_UNDO.store(0, Ordering::SeqCst);
	let a = unsafe {
		oakengine_undo_command_create(
			c"alpha".as_ptr(),
			Some(stk_redo),
			Some(stk_undo),
			None,
			std::ptr::null_mut(),
		)
	};
	assert_eq!(unsafe { oakengine_undo_push(a, c"alpha".as_ptr()) }, 0);
	assert_eq!(unsafe { oakengine_undo_count() }, 2);
	assert_eq!(unsafe { oakengine_undo_index() }, 2);
	assert_eq!(STK_REDO.load(Ordering::SeqCst), 1);
	assert_eq!(unsafe { oakengine_undo_can_undo() }, 1);
	assert_eq!(unsafe { oakengine_undo_command_text(1, buf.as_mut_ptr(), 64) }, 5);
	assert_eq!(unsafe { read_str(buf.as_ptr()) }, "alpha");
	assert_eq!(unsafe { oakengine_undo_command_is_done(1) }, 1);

	// --- Push a second named command.
	let b = unsafe {
		oakengine_undo_command_create(
			c"beta".as_ptr(),
			Some(stk_redo),
			Some(stk_undo),
			None,
			std::ptr::null_mut(),
		)
	};
	assert_eq!(unsafe { oakengine_undo_push(b, c"beta".as_ptr()) }, 0);
	assert_eq!(unsafe { oakengine_undo_count() }, 3);
	assert_eq!(unsafe { oakengine_undo_index() }, 3);
	assert_eq!(STK_REDO.load(Ordering::SeqCst), 2);
	assert_eq!(unsafe { oakengine_undo_command_text(2, buf.as_mut_ptr(), 64) }, 4);
	assert_eq!(unsafe { read_str(buf.as_ptr()) }, "beta");
	// Tiny buffer: truncated copy, full length still reported.
	let mut small = [0 as c_char; 2];
	assert_eq!(unsafe { oakengine_undo_command_text(1, small.as_mut_ptr(), 2) }, 5);
	assert_eq!(unsafe { read_str(small.as_ptr()) }, "a");

	// --- jump() legal matrix. jump(1) from index 3 undoes BOTH beta and
	// alpha (the stack undoes back-to-front until the done-count is 1).
	assert_eq!(unsafe { oakengine_undo_jump(1) }, 0);
	assert_eq!(unsafe { oakengine_undo_index() }, 1);
	assert_eq!(STK_UNDO.load(Ordering::SeqCst), 2);
	assert_eq!(unsafe { oakengine_undo_can_undo() }, 0);
	assert_eq!(unsafe { oakengine_undo_can_redo() }, 1);
	assert_eq!(unsafe { oakengine_undo_command_is_done(1) }, 0);
	assert_eq!(unsafe { oakengine_undo_command_is_done(2) }, 0);

	assert_eq!(unsafe { oakengine_undo_jump(0) }, 0);
	// The base "New/Open Project" row is never undoable, so the index
	// bottoms out at 1 rather than 0.
	assert_eq!(unsafe { oakengine_undo_index() }, 1);
	assert_eq!(STK_UNDO.load(Ordering::SeqCst), 2);

	// Negative index is clamped to 0 (olive jump semantics) — still no
	// undo past the base row.
	assert_eq!(unsafe { oakengine_undo_jump(-5) }, 0);
	assert_eq!(unsafe { oakengine_undo_index() }, 1);

	// Oversized index is clamped to the done-command count.
	assert_eq!(unsafe { oakengine_undo_jump(999) }, 0);
	assert_eq!(unsafe { oakengine_undo_index() }, 3);
	assert_eq!(STK_REDO.load(Ordering::SeqCst), 4);
	assert_eq!(unsafe { oakengine_undo_can_undo() }, 1);
	assert_eq!(unsafe { oakengine_undo_can_redo() }, 0);

	// --- Undo groups.
	// A second begin while a group is open fails with E_STATE (-2);
	// ending an empty group discards it (no new row).
	assert_eq!(unsafe { oakengine_undo_group_begin(c"anon".as_ptr()) }, 0);
	assert_eq!(unsafe { oakengine_undo_group_begin(c"again".as_ptr()) }, -2);
	assert_eq!(unsafe { oakengine_undo_group_end() }, 0);
	assert_eq!(unsafe { oakengine_undo_count() }, 3);
	assert_eq!(unsafe { oakengine_undo_index() }, 3);

	// begin → push children → end pushes ONE grouped row.
	assert_eq!(unsafe { oakengine_undo_group_begin(c"grouped".as_ptr()) }, 0);
	let c1 = unsafe {
		oakengine_undo_command_create(
			c"c1".as_ptr(),
			Some(stk_redo),
			Some(stk_undo),
			None,
			std::ptr::null_mut(),
		)
	};
	let c2 = unsafe {
		oakengine_undo_command_create(
			c"c2".as_ptr(),
			Some(stk_redo),
			Some(stk_undo),
			None,
			std::ptr::null_mut(),
		)
	};
	assert_eq!(unsafe { oakengine_undo_push(c1, c"c1".as_ptr()) }, 0);
	assert_eq!(unsafe { oakengine_undo_push(c2, c"c2".as_ptr()) }, 0);
	// Both children were redo'd eagerly into the group, not the stack.
	assert_eq!(STK_REDO.load(Ordering::SeqCst), 6);
	assert_eq!(unsafe { oakengine_undo_count() }, 3);
	assert_eq!(unsafe { oakengine_undo_group_end() }, 0);
	assert_eq!(unsafe { oakengine_undo_count() }, 4);
	assert_eq!(unsafe { oakengine_undo_index() }, 4);
	assert_eq!(unsafe { oakengine_undo_command_text(3, buf.as_mut_ptr(), 64) }, 7);
	assert_eq!(unsafe { read_str(buf.as_ptr()) }, "grouped");
	assert_eq!(unsafe { oakengine_undo_command_is_done(3) }, 1);

	// Undo the group: children undo in REVERSE order.
	assert_eq!(unsafe { oakengine_undo_jump(3) }, 0);
	assert_eq!(unsafe { oakengine_undo_index() }, 3);
	assert_eq!(STK_UNDO.load(Ordering::SeqCst), 4);
	assert_eq!(unsafe { oakengine_undo_command_is_done(3) }, 0);
	assert_eq!(unsafe { oakengine_undo_can_redo() }, 1);
	// Redo the group: children redo in INSERTION order.
	assert_eq!(unsafe { oakengine_undo_jump(4) }, 0);
	assert_eq!(unsafe { oakengine_undo_index() }, 4);
	assert_eq!(STK_REDO.load(Ordering::SeqCst), 8);
	// Undo again for the abort phase.
	assert_eq!(unsafe { oakengine_undo_jump(3) }, 0);
	assert_eq!(STK_UNDO.load(Ordering::SeqCst), 6);

	// begin → push → abort discards the group (the child's undo does NOT
	// run — see the NOTE below and `group_abort_undoes_children_repro`).
	assert_eq!(unsafe { oakengine_undo_group_begin(c"abort".as_ptr()) }, 0);
	let c3 = unsafe {
		oakengine_undo_command_create(
			c"c3".as_ptr(),
			Some(stk_redo),
			Some(stk_undo),
			None,
			std::ptr::null_mut(),
		)
	};
	assert_eq!(unsafe { oakengine_undo_push(c3, c"c3".as_ptr()) }, 0);
	assert_eq!(STK_REDO.load(Ordering::SeqCst), 9);
	assert_eq!(unsafe { oakengine_undo_group_abort() }, 0);
	// NOTE: the abort does NOT run the child's undo — `undo_now` is a
	// no-op on the never-done multi command (see
	// `group_abort_undoes_children_repro`, ignored, for the full repro),
	// so c3's side effect is not rolled back. Only the side-effect-free
	// assertions follow.
	assert_eq!(unsafe { oakengine_undo_count() }, 4); // unchanged
	assert_eq!(unsafe { oakengine_undo_index() }, 3);

	// End/abort with no open group fail with E_STATE.
	assert_eq!(unsafe { oakengine_undo_group_end() }, -2);
	assert_eq!(unsafe { oakengine_undo_group_abort() }, -2);

	// --- Illegal push inputs (rejected before any stack access).
	assert_eq!(
		unsafe { oakengine_undo_push(std::ptr::null_mut(), c"x".as_ptr()) },
		-1
	);
	let eb = empty_engine_ptr();
	assert_eq!(unsafe { oakengine_undo_push(eb, c"x".as_ptr()) }, -1);
	unsafe { oakengine_undo_command_free(eb) };

	// Cleanup: back to baseline.
	assert_eq!(unsafe { oakengine_undo_clear() }, 0);
	assert_eq!(unsafe { oakengine_undo_count() }, 1);
	assert_eq!(unsafe { oakengine_undo_index() }, 1);
}

// ---------------------------------------------------------------------------
// Real-bug repros (ignored: they crash the process; see the report)
// ---------------------------------------------------------------------------

/// REAL BUG REPRO — `oakengine_undo_push(cmd, NULL)` segfaults the process.
///
/// The facade's `push_or_run` (src/undo.rs) turns a NULL name into
/// `String::new()` and passes its DANGLING `as_ptr()` (address 0x1 — Rust
/// empty-string pointers are never NULL) to the oakundo module's
/// `oakundo_undostack_push`, whose `read_name` treats any non-NULL pointer
/// as a valid C string and runs `CStr::from_ptr` (strlen) on it, faulting
/// on the unmapped page. `name` is documented as legal-NULL in both the
/// module header (`include/undo/undostack.h`: "NULL behaves like an empty
/// label") and the facade docs, and the crash is NOT caught by the
/// catch_unwind guards (it is a hard SIGSEGV, not a panic).
///
/// Verified: `cargo test -p oakengine --test it_undo null_name_push_repro -- --ignored`
/// dies with signal 11 inside `oakundo::ffi::read_name`.
#[test]
#[ignore = "crashes the process: src/undo.rs push_or_run passes String::new().as_ptr() (0x1) to oakundo's read_name, which strlen's it -> SIGSEGV; needs the engine fix"]
fn null_name_push_repro() {
	common::force_link();
	assert_eq!(unsafe { oakengine_undo_clear() }, 0);

	let cmd = unsafe {
		oakengine_undo_command_create(
			c"x".as_ptr(),
			None,
			None,
			None,
			std::ptr::null_mut(),
		)
	};
	// NULL name is a documented-legal label; this must not crash.
	assert_eq!(unsafe { oakengine_undo_push(cmd, std::ptr::null()) }, 0);

	assert_eq!(unsafe { oakengine_undo_clear() }, 0);
}

/// REAL BUG REPRO — `oakengine_undo_group_begin(NULL)` +
/// `oakengine_undo_group_end()` segfaults the process.
///
/// Same root cause as [`null_name_push_repro`]: `oakengine_undo_group_end`
/// (src/undo.rs) stores the group name as a Rust `String` and passes its
/// `as_ptr()` to `oakundo_undostack_push_pre_executed`; a NULL (or empty)
/// name is a dangling 0x1 pointer there, and the module's `read_name`
/// crashes on it. The group-abort path never crosses the name and is safe.
#[test]
#[ignore = "crashes the process: src/undo.rs group_end passes String::new().as_ptr() (0x1) to oakundo's read_name, which strlen's it -> SIGSEGV; needs the engine fix"]
fn null_name_group_repro() {
	common::force_link();
	assert_eq!(unsafe { oakengine_undo_clear() }, 0);

	assert_eq!(unsafe { oakengine_undo_group_begin(std::ptr::null()) }, 0);
	// End of a NULL-named (empty) group must not crash.
	assert_eq!(unsafe { oakengine_undo_group_end() }, 0);

	assert_eq!(unsafe { oakengine_undo_clear() }, 0);
}

/// Counter for the abort repro (own set: this test runs only under
/// `--ignored`, but keep it isolated anyway).
static ABORT_UNDO: AtomicI32 = AtomicI32::new(0);

unsafe extern "C" fn abort_undo_cb(_ud: *mut c_void) {
	ABORT_UNDO.fetch_add(1, Ordering::SeqCst);
}

/// REAL BUG REPRO — `oakengine_undo_group_abort()` does not undo the
/// group's executed children.
///
/// The facade (src/undo.rs) closes the abort with
/// `oakundo_command_undo_now(open.multi)` on a multi command that was
/// never marked done (each child was redo'd eagerly at push time, but the
/// multi's own `done` flag stays false), and oakundo's documented
/// `undo_now` is a no-op on a not-done command. Net effect: the child's
/// undo callback never fires, so the group's side effects are NOT rolled
/// back — contradicting the documented "undo all executed children and
/// discard the group". (The smoke test in tests/undo.rs misses this: its
/// `STK_UNDO_COUNT == 1` assertion is satisfied by a leftover value from
/// an earlier jump.)
#[test]
#[ignore = "fails: group_abort leaves children done (undo_now is a no-op on the never-done multi); needs the engine fix"]
fn group_abort_undoes_children_repro() {
	common::force_link();
	assert_eq!(unsafe { oakengine_undo_clear() }, 0);

	ABORT_UNDO.store(0, Ordering::SeqCst);
	assert_eq!(unsafe { oakengine_undo_group_begin(c"abort".as_ptr()) }, 0);
	let c = unsafe {
		oakengine_undo_command_create(
			c"c".as_ptr(),
			None,
			Some(abort_undo_cb),
			None,
			std::ptr::null_mut(),
		)
	};
	assert_eq!(unsafe { oakengine_undo_push(c, c"c".as_ptr()) }, 0);
	assert_eq!(unsafe { oakengine_undo_group_abort() }, 0);
	// Documented behavior: the executed child's undo must run.
	assert_eq!(ABORT_UNDO.load(Ordering::SeqCst), 1);
}
