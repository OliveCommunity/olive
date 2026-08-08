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

//! Concurrency contract of the [`RenderTask`] render loop (src/render.rs):
//! up to `max_inflight` oakrender tickets run concurrently, results reach
//! the behavior hooks in timestamp order through the reorder buffer (even
//! when tickets complete out of order), cancellation drains in-flight
//! tickets (their completions still fire exactly once), progress is
//! monotonic, and a hook error stops dispatch immediately.
//!
//! The link-time stubs in `common` simulate the oakrender ticket arena;
//! with `TICKET_DEFER = 1` tickets stay in flight until the test fires
//! them with `stub_complete`, so completion order is fully scripted. Frame
//! handles encode the submitting ticket id (0x10000 + id), so the tests
//! can assert the exact delivery order of frames.

#[path = "common/mod.rs"]
mod common;

use std::sync::atomic::Ordering;
use std::sync::{Arc, Mutex};

use common::*;
use oaktask::error::{Error, OAKTASK_E_CANCELLED, OAKTASK_E_FAILED, Result};
use oaktask::handle::CHandle;
use oaktask::render::{ForceParams, RenderTask, RenderTaskBehavior};
use oaktask::task::{Task, TaskEvent};
use oakcore_rs::{Rational, TimeRange};

/// Poison-tolerant serialization (the shared stub registry is process-wide).
fn lock() -> std::sync::MutexGuard<'static, ()> {
	MANAGER_LOCK.lock().unwrap_or_else(|e| e.into_inner())
}

/// Frame-handle ctx base (see common: STUB_FRAME_CTX_BASE).
const FRAME_CTX_BASE: usize = 0x10000;

/// A RenderTask rendering `frames` 1-second frames (timebase 1/1) from
/// 0, optionally with audio enabled for the whole range.
fn render_task(frames: i64, audio: bool) -> RenderTask {
	VIDEO_TIME_BASE_NUM.store(1, Ordering::SeqCst);
	VIDEO_TIME_BASE_DEN.store(1, Ordering::SeqCst);
	let range = TimeRange::new(Rational::new(0, 1), Rational::new(frames, 1));
	let base = Task::new("render-loop-test", fake_atom().to_chandle());
	let mut rt = RenderTask::new(
		base,
		CHandle::null(),
		CHandle::null(),
		fake_handle(),
		ForceParams::default(),
		None,
	);
	rt.set_render_inputs(fake_handle(), CHandle::null(), 0, audio, range);
	rt
}

/// Records the hook calls in delivery order; `fail_on_frame` (0-based
/// index) makes that frame's `frame_downloaded` return an error.
#[derive(Default)]
struct RecordingBehavior {
	/// ctx values of delivered frames, in delivery order (0x10000 + id).
	frames: Vec<usize>,
	/// number of `audio_downloaded` calls.
	audio_calls: usize,
	/// fail the frame at this delivery index, if set.
	fail_on_frame: Option<usize>,
}

impl RenderTaskBehavior for RecordingBehavior {
	fn frame_downloaded(&mut self, _task: &mut Task, frame: CHandle) -> Result<()> {
		if let Some(i) = self.fail_on_frame {
			if self.frames.len() == i {
				return Err(Error::Failed("stub encoder write failure".to_string()));
			}
		}
		self.frames.push(frame.ctx as usize);
		Ok(())
	}

	fn audio_downloaded(&mut self, _task: &mut Task, _buffer: CHandle) -> Result<()> {
		self.audio_calls += 1;
		Ok(())
	}

	fn encode_subtitle(&mut self, _task: &mut Task, _text: &str) -> Result<()> {
		Ok(())
	}
}

/// Drive `render` on a spawned thread. The base task lives inside the
/// render task; it is moved out first so the test can keep its cancel atom.
fn run_render(mut rt: RenderTask, behavior: RecordingBehavior) -> std::thread::JoinHandle<(Result<()>, RecordingBehavior)> {
	std::thread::spawn(move || {
		let mut base = std::mem::replace(&mut rt.base, Task::new("", CHandle::null()));
		let mut behavior = behavior;
		let result = rt.render(&mut base, &mut behavior);
		(result, behavior)
	})
}

/// Frames complete in scrambled order; the hooks still see them in
/// timestamp order, and every completion fires exactly once.
#[test]
fn render_delivers_frames_in_order_under_scrambled_completion() {
	let _g = lock();
	reset_stubs();
	TICKET_DEFER.store(1, Ordering::SeqCst);

	let mut rt = render_task(5, false);
	rt.set_max_inflight(8); // window >= frame count: all tickets submitted upfront
	let handle = run_render(rt, RecordingBehavior::default());

	stub_wait_submitted(5);
	for id in [2usize, 0, 1, 4, 3] {
		stub_complete(id);
	}

	let (result, behavior) = handle.join().unwrap();
	assert!(result.is_ok());
	assert_eq!(
		behavior.frames,
		vec![FRAME_CTX_BASE, FRAME_CTX_BASE + 1, FRAME_CTX_BASE + 2, FRAME_CTX_BASE + 3, FRAME_CTX_BASE + 4],
		"frames must be delivered in timestamp order despite scrambled completion"
	);
	assert_eq!(stub_completed_count(), 5, "each ticket completes exactly once");
}

/// The audio ticket completes last of all; it is still delivered before any
/// frame (the observable audio-first contract).
#[test]
fn render_delivers_audio_before_frames_under_scrambled_completion() {
	let _g = lock();
	reset_stubs();
	TICKET_DEFER.store(1, Ordering::SeqCst);

	let mut rt = render_task(3, true);
	rt.set_max_inflight(4); // audio + 3 frames, all submitted upfront
	let handle = run_render(rt, RecordingBehavior::default());

	stub_wait_submitted(4);
	// Frames 2,3 and 1 complete before the audio ticket (id 0).
	for id in [2usize, 3, 1, 0] {
		stub_complete(id);
	}

	let (result, behavior) = handle.join().unwrap();
	assert!(result.is_ok());
	assert_eq!(behavior.audio_calls, 1, "audio delivered exactly once");
	assert_eq!(
		behavior.frames,
		vec![FRAME_CTX_BASE + 1, FRAME_CTX_BASE + 2, FRAME_CTX_BASE + 3],
		"audio first, then frames in timestamp order"
	);
	assert_eq!(stub_completed_count(), 4);
}

/// Cancellation between frames: the loop aborts, every in-flight ticket is
/// cancelled+waited, and each of their completions still fires exactly once.
#[test]
fn render_cancel_drains_inflight_and_fires_their_completions() {
	let _g = lock();
	reset_stubs();
	TICKET_DEFER.store(1, Ordering::SeqCst);

	let mut rt = render_task(5, false);
	rt.set_max_inflight(2);
	let atom = rt.base.get_cancel_atom();
	let handle = run_render(rt, RecordingBehavior::default());

	// Frame 0 completes normally; the loop delivers it and tops up ticket 2.
	stub_wait_submitted(2);
	stub_complete(0);
	stub_wait_submitted(3);

	// Cancel through the atom. The stub atom-cancel finishes the pending
	// tickets (real cancellation propagates into in-flight renders), waking
	// the loop; the loop then sees the cancellation and drains.
	unsafe {
		oaktask::bridge::render::oakrender_cancelatom_cancel(
			oaktask::bridge::render::OakCancelAtom::from_chandle(atom),
		);
	}

	let (result, behavior) = handle.join().unwrap();
	assert_eq!(result.unwrap_err().code(), OAKTASK_E_CANCELLED);
	assert_eq!(behavior.frames, vec![FRAME_CTX_BASE], "only frame 0 delivered before cancel");
	assert_eq!(stub_completed_count(), 3, "every submitted ticket's completion fires (incl. on cancel)");
	assert_eq!(stub_submitted_count(), 3, "no new tickets are dispatched after cancellation");
}

/// Progress events are non-decreasing and end at 1.0 (5 frames, 1s each).
#[test]
fn render_progress_is_monotonic_and_reaches_one() {
	let _g = lock();
	reset_stubs();

	let mut rt = render_task(5, false);
	rt.set_max_inflight(8);
	let progress: Arc<Mutex<Vec<f64>>> = Default::default();
	{
		let progress = progress.clone();
		rt.base.set_event_listener(Box::new(move |ev: TaskEvent| {
			if let TaskEvent::Progress(v) = ev {
				progress.lock().unwrap().push(v);
			}
		}));
	}
	let handle = run_render(rt, RecordingBehavior::default());

	let (result, behavior) = handle.join().unwrap();
	assert!(result.is_ok());
	assert_eq!(behavior.frames.len(), 5);

	let seen = progress.lock().unwrap().clone();
	assert_eq!(seen.len(), 5, "one progress event per delivered frame");
	assert!(
		seen.windows(2).all(|w| w[0] <= w[1]),
		"progress must be non-decreasing, got {seen:?}"
	);
	assert!((seen[seen.len() - 1] - 1.0).abs() < 1e-9, "progress reaches 1.0");
}

/// A hook error mid-stream stops dispatching new tickets; the in-flight
/// tickets are drained and their completions fire.
#[test]
fn render_error_stops_dispatching_new_tickets() {
	let _g = lock();
	reset_stubs();
	TICKET_DEFER.store(1, Ordering::SeqCst);

	let mut behavior = RecordingBehavior::default();
	behavior.fail_on_frame = Some(2); // the third delivered frame fails
	let mut rt = render_task(10, false);
	rt.set_max_inflight(2);
	let handle = run_render(rt, behavior);

	stub_wait_submitted(2);
	stub_complete(0);
	stub_wait_submitted(3); // frame 0 delivered, ticket 2 submitted
	stub_complete(1);
	stub_wait_submitted(4); // frame 1 delivered, ticket 3 submitted
	stub_complete(2); // frame 2 fails in the hook

	let (result, behavior) = handle.join().unwrap();
	assert_eq!(result.unwrap_err().code(), OAKTASK_E_FAILED);
	assert_eq!(
		behavior.frames,
		vec![FRAME_CTX_BASE, FRAME_CTX_BASE + 1],
		"two frames delivered before the error"
	);
	assert_eq!(stub_submitted_count(), 4, "no ticket past the failing frame");
	assert_eq!(stub_completed_count(), 4, "in-flight tickets are drained, completions fire");
}

/// Default stub mode (completions fire synchronously on submit): the loop
/// still windows the submissions and delivers in timestamp order.
#[test]
fn render_succeeds_with_immediate_completion_and_windowing() {
	let _g = lock();
	reset_stubs();

	let mut rt = render_task(7, false);
	rt.set_max_inflight(3);
	let handle = run_render(rt, RecordingBehavior::default());

	let (result, behavior) = handle.join().unwrap();
	assert!(result.is_ok());
	assert_eq!(behavior.frames.len(), 7);
	assert_eq!(
		behavior.frames,
		(0..7).map(|i| FRAME_CTX_BASE + i).collect::<Vec<_>>(),
		"frames delivered in timestamp order"
	);
	assert_eq!(stub_completed_count(), 7);
}

/// An empty export range renders nothing and succeeds.
#[test]
fn render_empty_range_succeeds_without_tickets() {
	let _g = lock();
	reset_stubs();

	let mut rt = render_task(0, false);
	rt.set_max_inflight(1);
	let handle = run_render(rt, RecordingBehavior::default());

	let (result, behavior) = handle.join().unwrap();
	assert!(result.is_ok());
	assert!(behavior.frames.is_empty());
	assert_eq!(stub_submitted_count(), 0);
}
