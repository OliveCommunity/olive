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

//! oakundo C ABI calls — now direct Rust calls into the oakundo crate
//! (single-lib unification, see `docs/zh/plans/riir/single-lib.md`).
//! Undo commands are created through the C ABI vtable
//! (`oakundo_command_init` with Rust closures as userdata) — no C++
//! UndoCommand subclassing exists on this side.
//!
//! ## Test stubs (`--features test-stubs`)
//!
//! With the direct dependency, the real oakundo rlib is linked into
//! every build and test; the `test-stubs` feature still compiles
//! in-crate `#[no_mangle]` implementations of the undo C ABI (see
//! [`stub`]) for environments that link without oakundo. Do not enable
//! the feature in a binary that also links oakundo (duplicate symbols).

use std::ffi::c_int;
use std::sync::atomic::{AtomicBool, Ordering};

use crate::handle::CHandle;

/// `OakUndoCommandVtable` (include/undo/undocommand.h) — the callback
/// table backing a caller-defined undo command. Single-lib unification:
/// aliases the oakundo crate's vtable POD (identical layout).
pub type Vtable = oakundo::undocommand::OakUndoCommandVtable;

/// Rust closure state behind a vtable command's `userdata` pointer.
///
/// The box is handed to [`command_init`] (which takes ownership); the
/// vtable trampolines below route `redo`/`undo`/destruction back into
/// the closures.
pub struct CommandState {
	/// Whether the command has been executed (redo_now no-ops when done).
	pub done: AtomicBool,
	/// Redo closure.
	pub redo: Box<dyn FnMut() + Send>,
	/// Undo closure.
	pub undo: Box<dyn FnMut() + Send>,
}

impl CommandState {
	/// New state with both directions.
	pub fn new(
		redo: impl FnMut() + Send + 'static,
		undo: impl FnMut() + Send + 'static,
	) -> CommandState {
		CommandState {
			done: AtomicBool::new(false),
			redo: Box::new(redo),
			undo: Box::new(undo),
		}
	}
}

/// Trampoline: run the redo closure behind `userdata`. Panics are
/// swallowed at the C boundary (`// CPP-PARITY: undocommand.cpp` — the
/// C++ side has no panic concept; a panic here must never unwind across
/// the extern "C" frame).
unsafe extern "C" fn redo_trampoline(userdata: *mut std::ffi::c_void) {
	let _ = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
		if !userdata.is_null() {
			let state = unsafe { &mut *(userdata as *mut CommandState) };
			(state.redo)();
		}
	}));
}

/// Trampoline: run the undo closure behind `userdata`.
unsafe extern "C" fn undo_trampoline(userdata: *mut std::ffi::c_void) {
	let _ = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
		if !userdata.is_null() {
			let state = unsafe { &mut *(userdata as *mut CommandState) };
			(state.undo)();
		}
	}));
}

/// Trampoline: free the `CommandState` box.
unsafe extern "C" fn free_trampoline(userdata: *mut std::ffi::c_void) {
	if !userdata.is_null() {
		unsafe { drop(Box::from_raw(userdata as *mut CommandState)) };
	}
}

/// Create a vtable-backed undo command whose redo/undo run the given
/// closures (`oakundo_command_init`). The returned handle is owned by
/// the caller; `None` when oakundo is unavailable (or the stub returns
/// an empty handle).
pub fn command_from_closures(
	redo: impl FnMut() + Send + 'static,
	undo: impl FnMut() + Send + 'static,
) -> Option<CHandle> {
	let state = Box::new(CommandState::new(redo, undo));
	let vtable = Vtable {
		redo: Some(redo_trampoline),
		undo: Some(undo_trampoline),
		free_fn: Some(free_trampoline),
	};
	command_init(&vtable, Box::into_raw(state) as *mut std::ffi::c_void)
}

/// `oakundo_command_init` (vtable command).
#[cfg(feature = "test-stubs")]
pub fn command_init(vtable: &Vtable, userdata: *mut std::ffi::c_void) -> Option<CHandle> {
	Some(unsafe { stub::oakundo_command_init(vtable as *const Vtable, userdata) })
}

/// `oakundo_command_init` (vtable command).
#[cfg(not(feature = "test-stubs"))]
pub fn command_init(vtable: &Vtable, userdata: *mut std::ffi::c_void) -> Option<CHandle> {
	// Direct call into the oakundo crate (single-lib unification).
	let h = unsafe { oakundo::ffi::command::oakundo_command_init(vtable, userdata) };
	if h.is_null() {
		None
	} else {
		Some(h)
	}
}

/// `oakundo_command_init_multi`.
#[cfg(feature = "test-stubs")]
pub fn command_init_multi() -> Option<CHandle> {
	Some(unsafe { stub::oakundo_command_init_multi() })
}

/// `oakundo_command_init_multi`.
#[cfg(not(feature = "test-stubs"))]
pub fn command_init_multi() -> Option<CHandle> {
	// Direct call into the oakundo crate (single-lib unification).
	let h = unsafe { oakundo::ffi::command::oakundo_command_init_multi() };
	if h.is_null() {
		None
	} else {
		Some(h)
	}
}

/// `oakundo_command_multi_add_child`.
#[cfg(feature = "test-stubs")]
pub fn command_multi_add_child(multi: CHandle, child: CHandle) -> Option<c_int> {
	Some(unsafe { stub::oakundo_command_multi_add_child(multi, child) })
}

/// `oakundo_command_multi_add_child`.
#[cfg(not(feature = "test-stubs"))]
pub fn command_multi_add_child(multi: CHandle, child: CHandle) -> Option<c_int> {
	// Direct call into the oakundo crate (single-lib unification).
	Some(unsafe { oakundo::ffi::command::oakundo_command_multi_add_child(multi, child) })
}

/// `oakundo_command_redo_now`.
#[cfg(feature = "test-stubs")]
pub fn command_redo_now(command: CHandle) -> Option<c_int> {
	Some(unsafe { stub::oakundo_command_redo_now(command) })
}

/// `oakundo_command_redo_now`.
#[cfg(not(feature = "test-stubs"))]
pub fn command_redo_now(command: CHandle) -> Option<c_int> {
	// Direct call into the oakundo crate (single-lib unification).
	Some(unsafe { oakundo::ffi::command::oakundo_command_redo_now(command) })
}

/// `oakundo_command_undo_now`.
#[cfg(feature = "test-stubs")]
pub fn command_undo_now(command: CHandle) -> Option<c_int> {
	Some(unsafe { stub::oakundo_command_undo_now(command) })
}

/// `oakundo_command_undo_now`.
#[cfg(not(feature = "test-stubs"))]
pub fn command_undo_now(command: CHandle) -> Option<c_int> {
	// Direct call into the oakundo crate (single-lib unification).
	Some(unsafe { oakundo::ffi::command::oakundo_command_undo_now(command) })
}

/// `oakundo_command_free`.
#[cfg(feature = "test-stubs")]
pub fn command_free(command: *mut CHandle) {
	unsafe { stub::oakundo_command_free(command) };
}

/// `oakundo_command_free`.
#[cfg(not(feature = "test-stubs"))]
pub fn command_free(command: *mut CHandle) {
	// Direct call into the oakundo crate (single-lib unification).
	unsafe { oakundo::ffi::command::oakundo_command_free(command) }
}

/// `oakundo_stack_push` (facade-owned stack).
pub fn stack_push(
	stack: CHandle,
	command: CHandle,
	text: *const std::ffi::c_char,
) -> Option<c_int> {
	// Direct call into the oakundo crate (single-lib unification).
	Some(unsafe { oakundo::ffi::undostack::oakundo_undostack_push(stack, command, text) })
}

/// In-crate implementations of the undo C ABI for `cargo test`
/// (`--features test-stubs`). Mirrors the C++ `CallbackUndoCommand`
/// (`src/undo/c_api/undocommand.cpp`) semantics: the command holds the
/// vtable + userdata, calls `free_fn` on destruction, and `redo_now`/
/// `undo_now` are no-ops when already executed. Multi commands hold one
/// reference per child.
#[cfg(feature = "test-stubs")]
pub(crate) mod stub {
	use super::*;

	/// A command box behind an OakUndoCommand handle's `ctx`.
	pub(crate) enum StubCommand {
		/// Vtable command.
		Callback {
			/// Executed state (redo_now no-ops when true).
			done: AtomicBool,
			/// Redo callback.
			redo: Option<unsafe extern "C" fn(*mut std::ffi::c_void)>,
			/// Undo callback.
			undo: Option<unsafe extern "C" fn(*mut std::ffi::c_void)>,
			/// userdata release.
			free_fn: Option<unsafe extern "C" fn(*mut std::ffi::c_void)>,
			/// Opaque userdata (owned by the command).
			userdata: *mut std::ffi::c_void,
		},
		/// Multi command.
		Multi {
			/// Executed state.
			done: AtomicBool,
			/// Child commands (each holds one reference).
			children: Vec<CHandle>,
		},
	}

	impl Drop for StubCommand {
		fn drop(&mut self) {
			match self {
				StubCommand::Callback {
					free_fn, userdata, ..
				} => {
					if let Some(f) = free_fn {
						if !userdata.is_null() {
							unsafe { f(*userdata) };
						}
					}
				}
				StubCommand::Multi { children, .. } => {
					for child in children {
						if let Some(f) = child.release {
							unsafe { f(child.ctx) };
						}
					}
				}
			}
		}
	}

	/// `oakundo_command_init`.
	#[no_mangle]
	pub unsafe extern "C" fn oakundo_command_init(
		vtable: *const Vtable,
		userdata: *mut std::ffi::c_void,
	) -> CHandle {
		if vtable.is_null() {
			return CHandle::null();
		}
		let vt = unsafe { &*vtable };
		let cmd = StubCommand::Callback {
			done: AtomicBool::new(false),
			redo: vt.redo,
			undo: vt.undo,
			free_fn: vt.free_fn,
			userdata,
		};
		crate::handle::make_owned(SendStub(cmd))
	}

	/// `oakundo_command_init_multi`.
	#[no_mangle]
	pub unsafe extern "C" fn oakundo_command_init_multi() -> CHandle {
		crate::handle::make_owned(SendStub(StubCommand::Multi {
			done: AtomicBool::new(false),
			children: Vec::new(),
		}))
	}

	/// `oakundo_command_multi_add_child`.
	#[no_mangle]
	pub unsafe extern "C" fn oakundo_command_multi_add_child(
		multi: CHandle,
		child: CHandle,
	) -> c_int {
		if multi.ctx.is_null() || child.ctx.is_null() {
			return crate::error::OAKNODE_E_INVALID;
		}
		let boxed = multi.ctx as *mut crate::handle::RefBox<SendStub>;
		// Take one reference for the multi.
		if let Some(f) = child.addref {
			unsafe { f(child.ctx) };
		}
		let state = unsafe { &mut (*boxed).value };
		match &mut state.0 {
			StubCommand::Multi { children, .. } => {
				children.push(child);
				crate::error::OAKNODE_OK
			}
			_ => crate::error::OAKNODE_E_INVALID,
		}
	}

	/// `oakundo_command_redo_now`.
	#[no_mangle]
	pub unsafe extern "C" fn oakundo_command_redo_now(command: CHandle) -> c_int {
		if command.ctx.is_null() {
			return crate::error::OAKNODE_E_INVALID;
		}
		let boxed = command.ctx as *mut crate::handle::RefBox<SendStub>;
		let state = unsafe { &mut (*boxed).value };
		match &mut state.0 {
			StubCommand::Callback {
				done,
				redo,
				userdata,
				..
			} => {
				if !done.swap(true, Ordering::AcqRel) {
					if let Some(f) = redo {
						unsafe { f(*userdata) };
					}
				}
				crate::error::OAKNODE_OK
			}
			StubCommand::Multi { done, children } => {
				if !done.swap(true, Ordering::AcqRel) {
					for child in children.iter() {
						let _ = unsafe { oakundo_command_redo_now(child.clone()) };
					}
				}
				crate::error::OAKNODE_OK
			}
		}
	}

	/// `oakundo_command_undo_now`.
	#[no_mangle]
	pub unsafe extern "C" fn oakundo_command_undo_now(command: CHandle) -> c_int {
		if command.ctx.is_null() {
			return crate::error::OAKNODE_E_INVALID;
		}
		let boxed = command.ctx as *mut crate::handle::RefBox<SendStub>;
		let state = unsafe { &mut (*boxed).value };
		match &mut state.0 {
			StubCommand::Callback {
				done,
				undo,
				userdata,
				..
			} => {
				if done.swap(false, Ordering::AcqRel) {
					if let Some(f) = undo {
						unsafe { f(*userdata) };
					}
				}
				crate::error::OAKNODE_OK
			}
			StubCommand::Multi { done, children } => {
				if done.swap(false, Ordering::AcqRel) {
					for child in children.iter().rev() {
						let _ = unsafe { oakundo_command_undo_now(child.clone()) };
					}
				}
				crate::error::OAKNODE_OK
			}
		}
	}

	/// `oakundo_command_free`.
	#[no_mangle]
	pub unsafe extern "C" fn oakundo_command_free(command: *mut CHandle) {
		if command.is_null() || unsafe { (*command).ctx.is_null() } {
			return;
		}
		let h = unsafe { (*command).clone() };
		if let Some(f) = h.release {
			unsafe { f(h.ctx) };
		}
		unsafe { (*command).ctx = std::ptr::null_mut() };
	}

	/// Send-marker for the command box (the raw `userdata` pointer is
	/// only dereferenced on the thread that created the command — the
	/// test thread — so the box never actually crosses threads).
	struct SendStub(StubCommand);
	unsafe impl Send for SendStub {}
}
