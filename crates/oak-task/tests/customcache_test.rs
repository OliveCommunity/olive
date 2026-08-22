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

//! CustomCacheTask lifecycle tests: the task parks until `finish()` (success)
//! or a real cancellation (`Err(Cancelled)`); the cancelled callback fires
//! only for real cancellations.

use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;

use oak_task::customcache::CustomCacheTask;
use oak_task::task::Task;

fn spawn_parked(
	cc: CustomCacheTask,
) -> (
	std::thread::JoinHandle<oak_task::error::Result<()>>,
	Arc<oak_task::customcache::CustomCacheState>,
) {
	let state = cc.state();
	let atom = cc.base.get_cancel_atom();
	let title = cc.base.title().to_string();
	let mut outer = Task::new(&title, Some(atom));
	outer.set_behavior(Box::new(cc));
	let handle = std::thread::spawn(move || outer.start());
	// Let the worker park on the condvar.
	std::thread::sleep(std::time::Duration::from_millis(50));
	(handle, state)
}

/// The task title mirrors the C++ constructor; finishing wakes the parked
/// task which then reports success.
#[test]
fn finish_completes_parked_task() {
	let cc = CustomCacheTask::new("my-sequence");
	assert_eq!(cc.base.title(), "Caching custom range for \"my-sequence\"");
	let (handle, state) = spawn_parked(cc);

	state.finish();
	let result = handle.join().expect("worker panicked");
	assert!(
		result.is_ok(),
		"finish should complete the task successfully"
	);

	// Re-run: a real cancel (no finish) reports Err(Cancelled).
	let cc = CustomCacheTask::new("cancel-seq");
	let (handle, state) = spawn_parked(cc);
	state.cancel();
	let result = handle.join().expect("worker panicked");
	assert!(result.is_err(), "real cancellation should fail the task");
}

/// The cancelled callback fires only for a real cancellation, not for
/// `finish()`.
#[test]
fn cancelled_callback_fires_only_on_real_cancel() {
	// finish(): the callback must NOT fire.
	let fired_on_finish = Arc::new(AtomicBool::new(false));
	let mut cc = CustomCacheTask::new("finish-seq");
	cc.set_cancelled_callback(Box::new({
		let flag = fired_on_finish.clone();
		move || flag.store(true, Ordering::SeqCst)
	}));
	let (handle, state) = spawn_parked(cc);
	state.finish();
	assert!(handle.join().expect("worker panicked").is_ok());
	assert!(
		!fired_on_finish.load(Ordering::SeqCst),
		"finish must not fire the cancelled callback"
	);

	// real cancel: the callback fires.
	let fired_on_cancel = Arc::new(AtomicBool::new(false));
	let mut cc = CustomCacheTask::new("cancel-seq");
	cc.set_cancelled_callback(Box::new({
		let flag = fired_on_cancel.clone();
		move || flag.store(true, Ordering::SeqCst)
	}));
	let (handle, state) = spawn_parked(cc);
	state.cancel();
	assert!(handle.join().expect("worker panicked").is_err());
	assert!(
		fired_on_cancel.load(Ordering::SeqCst),
		"real cancel must fire the cancelled callback"
	);
}

/// The cancel hook wakes the parked task even without finish().
#[test]
fn cancel_hook_wakes_parked_task() {
	let cc = CustomCacheTask::new("wake-seq");
	let (handle, state) = spawn_parked(cc);
	// Simulate the manager's cancel path (base.cancel -> atom + hook).
	state.cancel();
	let result = handle.join().expect("worker panicked");
	assert!(result.is_err());
}
