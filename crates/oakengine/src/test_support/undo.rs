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

//! Smoke tests for the undo family (`engine/include/oakengine/undo.h`).
//!
//! The facade owns a process-wide undo stack and one open undo group, so
//! the stack-mutating tests are serialized inside a single test
//! function; the command-lifecycle tests (no stack access) can run in
//! parallel.

use super::common;

use std::ffi::{c_char, c_int, c_void};
use std::sync::atomic::{AtomicI32, Ordering};

use crate::undo::{
	oakengine_undo_can_redo, oakengine_undo_can_undo, oakengine_undo_clear,
	oakengine_undo_command_create, oakengine_undo_command_create_multi,
	oakengine_undo_command_free, oakengine_undo_command_is_done,
	oakengine_undo_command_multi_add_child, oakengine_undo_command_multi_child_count,
	oakengine_undo_command_redo_now, oakengine_undo_command_text, oakengine_undo_command_undo_now,
	oakengine_undo_count, oakengine_undo_group_abort, oakengine_undo_group_begin,
	oakengine_undo_group_end, oakengine_undo_handle, oakengine_undo_index, oakengine_undo_jump,
	oakengine_undo_push,
};

// ---------------------------------------------------------------------------
// Command lifecycle (no global-stack state)
// ---------------------------------------------------------------------------

/// Callback counters for the app-defined command test (own set so it can
/// run in parallel with the serialized stack test).
static CMD_REDO_COUNT: AtomicI32 = AtomicI32::new(0);
static CMD_UNDO_COUNT: AtomicI32 = AtomicI32::new(0);
static CMD_FREE_COUNT: AtomicI32 = AtomicI32::new(0);

/// Callback counters for the serialized global-stack test.
static STK_REDO_COUNT: AtomicI32 = AtomicI32::new(0);
static STK_UNDO_COUNT: AtomicI32 = AtomicI32::new(0);

/// Stack-test callbacks: bump only the `STK_*` counters. They must not
/// touch the `CMD_*` counters — the command-lifecycle tests reset and
/// assert those in parallel threads, so a stray bump here would race.
unsafe extern "C" fn redo_cb(_userdata: *mut c_void) {
	STK_REDO_COUNT.fetch_add(1, Ordering::SeqCst);
}

unsafe extern "C" fn undo_cb(_userdata: *mut c_void) {
	STK_UNDO_COUNT.fetch_add(1, Ordering::SeqCst);
}

/// Command-lifecycle-only callbacks: bump only the `CMD_*` counters. The
/// serialized stack test runs in a parallel thread and must not flip
/// these.
unsafe extern "C" fn cmd_redo_cb(_userdata: *mut c_void) {
	CMD_REDO_COUNT.fetch_add(1, Ordering::SeqCst);
}

unsafe extern "C" fn cmd_undo_cb(_userdata: *mut c_void) {
	CMD_UNDO_COUNT.fetch_add(1, Ordering::SeqCst);
}

unsafe extern "C" fn free_cb(_userdata: *mut c_void) {
	CMD_FREE_COUNT.fetch_add(1, Ordering::SeqCst);
}

/// Lifecycle: create a callback command, run redo/undo, free it.
#[test]
fn command_create_redo_undo_free() {
	CMD_REDO_COUNT.store(0, Ordering::SeqCst);
	CMD_UNDO_COUNT.store(0, Ordering::SeqCst);
	CMD_FREE_COUNT.store(0, Ordering::SeqCst);

	let cmd = unsafe {
		oakengine_undo_command_create(
			c"custom".as_ptr(),
			Some(cmd_redo_cb),
			Some(cmd_undo_cb),
			Some(free_cb),
			std::ptr::null_mut(),
		)
	};
	assert!(!cmd.is_null());

	assert_eq!(unsafe { oakengine_undo_command_redo_now(cmd) }, 0);
	assert_eq!(CMD_REDO_COUNT.load(Ordering::SeqCst), 1);

	assert_eq!(unsafe { oakengine_undo_command_undo_now(cmd) }, 0);
	assert_eq!(CMD_UNDO_COUNT.load(Ordering::SeqCst), 1);

	// The free callback must fire exactly once when freed directly.
	unsafe { oakengine_undo_command_free(cmd) };
	assert_eq!(CMD_FREE_COUNT.load(Ordering::SeqCst), 1);

	// Freeing a NULL pointer is a no-op.
	unsafe { oakengine_undo_command_free(std::ptr::null_mut()) };
}

/// Multi command: add children, count them, redo the whole multi.
#[test]
fn multi_command_add_child_count_redo() {
	let multi = unsafe { oakengine_undo_command_create_multi() };
	assert!(!multi.is_null());

	let child = unsafe {
		oakengine_undo_command_create(
			c"child".as_ptr(),
			Some(cmd_redo_cb),
			Some(cmd_undo_cb),
			None,
			std::ptr::null_mut(),
		)
	};
	assert_eq!(
		unsafe { oakengine_undo_command_multi_add_child(multi, child) },
		0
	);
	assert_eq!(
		unsafe { oakengine_undo_command_multi_child_count(multi) },
		1
	);

	// Adding a NULL child fails with E_INVALID (-1).
	assert_eq!(
		unsafe { oakengine_undo_command_multi_add_child(multi, std::ptr::null_mut()) },
		-1
	);

	unsafe { oakengine_undo_command_free(multi) };
}

// ---------------------------------------------------------------------------
// Global stack (serialized: the facade's stack is process-wide)
// ---------------------------------------------------------------------------

/// Push/undo/redo/jump/text round-trip on the global stack, undo-group
/// begin/end/abort, and NULL-push rejection — all serialized in ONE test
/// because the facade owns a process-wide stack and a single open undo
/// group (the C++ capi's `g_undo_group` analogue), which cannot be
/// exercised from parallel test threads.
#[test]
fn undo_stack_lifecycle() {
	// Serialized on the SAME lock as the it_undo stack tests and the
	// write-through tests (the facade's stack is process-wide): without it
	// a concurrent test's pushes break the exact-count assertions below.
	let _stack = super::it_undo::GLOBAL_STACK_LOCK
		.lock();

	// Reset to a clean "New/Open Project" base row.
	assert_eq!(unsafe { oakengine_undo_clear() }, 0);
	assert_eq!(unsafe { oakengine_undo_count() }, 1);
	assert_eq!(unsafe { oakengine_undo_index() }, 1);

	// The borrowed stack handle is stable and non-NULL.
	assert!(!unsafe { oakengine_undo_handle() }.is_null());

	// Push a callback command.
	let cmd = unsafe {
		oakengine_undo_command_create(
			c"op".as_ptr(),
			Some(redo_cb),
			Some(undo_cb),
			None,
			std::ptr::null_mut(),
		)
	};
	STK_REDO_COUNT.store(0, Ordering::SeqCst);
	STK_UNDO_COUNT.store(0, Ordering::SeqCst);
	assert_eq!(
		unsafe { oakengine_undo_push(cmd, c"operation".as_ptr()) },
		0
	);
	assert_eq!(unsafe { oakengine_undo_count() }, 2);
	assert_eq!(unsafe { oakengine_undo_index() }, 2);
	assert_eq!(STK_REDO_COUNT.load(Ordering::SeqCst), 1);

	// Row label (two-stage: query, then copy).
	let mut buf = [0 as c_char; 64];
	let len = unsafe { oakengine_undo_command_text(1, buf.as_mut_ptr(), 64) };
	assert!(len > 0);
	assert_eq!(
		unsafe { std::ffi::CStr::from_ptr(buf.as_ptr()) }
			.to_str()
			.unwrap(),
		"operation"
	);
	assert_eq!(unsafe { oakengine_undo_command_is_done(1) }, 1);
	// Invalid row → module NOT_FOUND (-20004) passes through.
	assert_eq!(
		unsafe { oakengine_undo_command_text(99, buf.as_mut_ptr(), 64) },
		-20004
	);

	// Undo restores index 1 and flips the done flag.
	assert_eq!(unsafe { oakengine_undo_jump(1) }, 0);
	assert_eq!(unsafe { oakengine_undo_index() }, 1);
	assert_eq!(unsafe { oakengine_undo_command_is_done(1) }, 0);
	assert_eq!(unsafe { oakengine_undo_can_undo() }, 0);
	assert_eq!(unsafe { oakengine_undo_can_redo() }, 1);

	// Redo back to 2.
	assert_eq!(unsafe { oakengine_undo_jump(2) }, 0);
	assert_eq!(unsafe { oakengine_undo_index() }, 2);

	unsafe { oakengine_undo_clear() };

	// --- Undo group: begin → push children → end pushes ONE entry; abort
	// undoes and discards. (continues the same serialized test)
	assert_eq!(unsafe { oakengine_undo_clear() }, 0);

	// Group begin/end with two children → one history row.
	assert_eq!(
		unsafe { oakengine_undo_group_begin(c"grouped".as_ptr()) },
		0
	);
	// A second begin while open fails with E_STATE (-2).
	assert_eq!(unsafe { oakengine_undo_group_begin(c"again".as_ptr()) }, -2);

	let c1 = unsafe {
		oakengine_undo_command_create(
			c"c1".as_ptr(),
			Some(redo_cb),
			Some(undo_cb),
			None,
			std::ptr::null_mut(),
		)
	};
	let c2 = unsafe {
		oakengine_undo_command_create(
			c"c2".as_ptr(),
			Some(redo_cb),
			Some(undo_cb),
			None,
			std::ptr::null_mut(),
		)
	};
	// While a group is open, push adds to the group (child redo'd
	// eagerly) instead of the stack.
	assert_eq!(unsafe { oakengine_undo_push(c1, c"c1".as_ptr()) }, 0);
	assert_eq!(unsafe { oakengine_undo_push(c2, c"c2".as_ptr()) }, 0);
	assert_eq!(unsafe { oakengine_undo_count() }, 1); // nothing on the stack yet

	assert_eq!(unsafe { oakengine_undo_group_end() }, 0);
	assert_eq!(unsafe { oakengine_undo_count() }, 2); // one grouped row

	// Abort path: group with a child is undone and discarded.
	STK_UNDO_COUNT.store(0, Ordering::SeqCst);
	assert_eq!(unsafe { oakengine_undo_group_begin(c"abort".as_ptr()) }, 0);
	let c3 = unsafe {
		oakengine_undo_command_create(
			c"c3".as_ptr(),
			Some(redo_cb),
			Some(undo_cb),
			None,
			std::ptr::null_mut(),
		)
	};
	assert_eq!(unsafe { oakengine_undo_push(c3, c"c3".as_ptr()) }, 0);
	assert_eq!(unsafe { oakengine_undo_group_abort() }, 0);
	assert_eq!(unsafe { oakengine_undo_count() }, 2); // unchanged
	assert_eq!(STK_UNDO_COUNT.load(Ordering::SeqCst), 1); // c3's undo ran

	// End with no open group fails with E_STATE.
	assert_eq!(unsafe { oakengine_undo_group_end() }, -2);

	unsafe { oakengine_undo_clear() };

	// Push NULL fails with E_INVALID.
	assert_eq!(
		unsafe { oakengine_undo_push(std::ptr::null_mut(), c"x".as_ptr()) },
		-1
	);
}
