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

//! M15 S1 end-to-end: real oak-worker processes driven by the
//! main-process [`ProcessDispatcher`] — spawn, handshake, batched
//! renders into shared-memory slots, crash isolation with restart and
//! re-dispatch, and the zero-copy main-process guarantee.
//!
//! These tests spawn actual `oak-worker` child processes (located through
//! `CARGO_BIN_EXE_oak-worker`), so they double as the binary-resolution
//! regression gate for [`DispatcherConfig::worker_bin`].
//!
//! All tests in this file serialize on [`TEST_LOCK`]: they spawn worker
//! processes that inherit the process environment, and the crash test
//! sets crash-hook variables that must not leak into sibling runs.

use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};

use oakcore_rs::{PixelFormat, Rational};
use oakrender::ipc::SLOT_FORMAT_BGRA8;
use oakrender::procpool::{
	main_heap_frame_copies, reset_main_heap_frame_copies, DispatcherConfig, ProcessDispatcher,
};
use oakrender::ticket::{TicketPayload, TicketResult, VideoTicketParams};
use oakrender::worker::{Job, JobDispatch, JobSchedule};

/// Serialize every test in this file (shared process environment +
/// real child processes).
static TEST_LOCK: Mutex<()> = Mutex::new(());

fn lock_test() -> std::sync::MutexGuard<'static, ()> {
	TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner())
}

fn worker_bin() -> std::path::PathBuf {
	env!("CARGO_BIN_EXE_oak-worker").into()
}

fn config(workers: usize, slots: u32) -> DispatcherConfig {
	DispatcherConfig {
		worker_bin: Some(worker_bin()),
		workers,
		slots_per_worker: slots,
		width: 64,
		height: 64,
		slot_format: SLOT_FORMAT_BGRA8,
		batch_size: 4,
		graph_snapshot: None,
		handshake_timeout_ms: 30_000,
	}
}

fn params(time: Rational, footage: Option<(String, i32)>) -> Arc<VideoTicketParams> {
	Arc::new(VideoTicketParams {
		viewer: 1,
		time,
		force_size: Some((64, 64)),
		force_format: Some(PixelFormat::F32),
		cache: None,
		cache_dir: None,
		cache_id: None,
		cache_timebase: None,
		footage,
		montage: Vec::new(),
	})
}

/// Submit `count` generated-frame tickets; returns the shared results
/// sink (completions land there from the dispatcher's poll pump).
fn submit(
	dispatcher: &ProcessDispatcher,
	results: &Arc<Mutex<Vec<TicketResult>>>,
	count: usize,
	footage: Option<(String, i32)>,
) {
	for i in 0..count {
		let results = results.clone();
		let footage = footage.clone();
		let job = Job {
			node_identity: 1,
			time: Rational::new(i as i64, 25),
			params: params(Rational::new(i as i64, 25), footage),
			// Never invoked on the process backend (workers render from
			// the wire spec); must still be a valid producer.
			produce: Arc::new(|_, _| {
				Err(oakrender::error::Error::Failed(
					"process backend does not use the in-process producer".into(),
				))
			}),
			done: Box::new(move |result| {
				results.lock().unwrap_or_else(|e| e.into_inner()).push(result);
			}),
			schedule: JobSchedule::seek(),
		};
		assert!(dispatcher.post(job), "post accepted while alive");
	}
}

/// Pump the dispatcher until `expect` completions arrive or the deadline
/// passes.
fn pump_until(dispatcher: &ProcessDispatcher, results: &Mutex<Vec<TicketResult>>, expect: usize) {
	let deadline = Instant::now() + Duration::from_secs(60);
	loop {
		dispatcher.poll();
		if results.lock().unwrap_or_else(|e| e.into_inner()).len() >= expect {
			return;
		}
		if Instant::now() > deadline {
			let have = results.lock().unwrap_or_else(|e| e.into_inner()).len();
			panic!("timeout: {have}/{expect} completions");
		}
		std::thread::sleep(Duration::from_millis(5));
	}
}

/// Two real workers render two waves of generated frames into shm slots;
/// the main process never copies frame bytes. Slots are released as
/// frames arrive (the dispatcher's credit-based flow control then keeps
/// the remaining tickets flowing).
#[test]
fn two_workers_render_two_waves_zero_copy() {
	let _guard = lock_test();
	let dispatcher = ProcessDispatcher::new(config(2, 4)).expect("dispatcher config");
	dispatcher.start().expect("workers start + handshake");
	assert_eq!(dispatcher.worker_count(), 2);
	assert!(dispatcher.is_alive(0));
	assert!(dispatcher.is_alive(1));

	reset_main_heap_frame_copies();

	// Wave 1: more tickets than slots in one worker, so both workers and
	// the slot-recycling path are exercised.
	let results = Arc::new(Mutex::new(Vec::new()));
	submit(&dispatcher, &results, 12, None);

	let mut seen_worker = [false; 2];
	let mut completed = 0usize;
	let deadline = Instant::now() + Duration::from_secs(60);
	while completed < 12 {
		dispatcher.poll();
		let drained: Vec<TicketResult> =
			results.lock().unwrap_or_else(|e| e.into_inner()).drain(..).collect();
		for result in drained {
			let payload = result.expect("frame rendered");
			let TicketPayload::ShmFrame(frame) = payload else {
				panic!("process backend must deliver ShmFrame payloads");
			};
			assert!(frame.worker < 2);
			assert!(frame.slot < 4);
			assert_eq!(frame.meta.width, 64);
			assert_eq!(frame.meta.height, 64);
			assert_eq!(frame.meta.format, SLOT_FORMAT_BGRA8);
			assert_eq!(frame.meta.data_size, 64 * 64 * 4);
			assert_eq!(frame.meta.linesize, 64 * 4);
			// Generated frame (transparent black) converted to BGRA8: all
			// zero. Read through the mapping — never `slot_to_vec` (the
			// counted copy path).
			let pixels = frame.shm.slot_bytes(frame.slot);
			assert!(
				pixels[..frame.meta.data_size as usize]
					.iter()
					.all(|&b| b == 0),
				"generated frame is transparent black"
			);
			seen_worker[frame.worker as usize] = true;
			dispatcher.release_frame(&frame);
			completed += 1;
		}
		if Instant::now() > deadline {
			panic!("timeout: {completed}/12 completions");
		}
		if completed < 12 {
			std::thread::sleep(Duration::from_millis(5));
		}
	}
	// Zero copy: nothing bumped the main-process frame-copy counter.
	assert_eq!(main_heap_frame_copies(), 0);
	// Both workers participated (interleaved sharded claiming).
	assert!(seen_worker[0], "worker 0 rendered at least one frame");
	assert!(seen_worker[1], "worker 1 rendered at least one frame");

	// Wave 2 through the same recycled slots.
	submit(&dispatcher, &results, 8, None);
	pump_until(&dispatcher, &results, 8);
	for result in results.lock().unwrap().drain(..) {
		let payload = result.expect("second wave rendered");
		let TicketPayload::ShmFrame(frame) = payload else {
			panic!("ShmFrame payload");
		};
		dispatcher.release_frame(&frame);
	}
	assert_eq!(main_heap_frame_copies(), 0);

	dispatcher.shutdown();
	assert_eq!(main_heap_frame_copies(), 0);
}

/// A worker crashing mid-render (SIGSEGV hook) must not take down the
/// main process: the frame is re-queued, the worker restarted and the
/// ticket still completes with a rendered frame.
#[test]
fn crash_isolation_restarts_worker_and_frame_still_renders() {
	let _guard = lock_test();

	// One-shot crash hook: the worker dies with SIGSEGV while rendering
	// ticket 1; the marker file it leaves behind makes the restarted
	// worker render the re-queued frame for real.
	let marker = std::env::temp_dir().join(format!(
		"oak-procpool-crash-marker-{}",
		std::process::id()
	));
	let _ = std::fs::remove_file(&marker);
	std::env::set_var("OAK_WORKER_CRASH_ON_TICKET", "1");
	std::env::set_var("OAK_WORKER_CRASH_MARKER", &marker);
	struct EnvGuard;
	impl Drop for EnvGuard {
		fn drop(&mut self) {
			std::env::remove_var("OAK_WORKER_CRASH_ON_TICKET");
			std::env::remove_var("OAK_WORKER_CRASH_MARKER");
		}
	}
	let _env_guard = EnvGuard;

	let dispatcher = ProcessDispatcher::new(config(1, 4)).expect("dispatcher config");
	dispatcher.start().expect("worker starts");
	assert_eq!(dispatcher.worker_count(), 1);

	let results = Arc::new(Mutex::new(Vec::new()));
	// The dispatcher's first ticket id is 1 — exactly the crash ticket.
	submit(&dispatcher, &results, 4, None);
	pump_until(&dispatcher, &results, 4);

	// The crash hit the worker (it restarted at least once)...
	assert!(
		dispatcher.restarts_of(0) >= 1,
		"crashed worker must be restarted (restarts={})",
		dispatcher.restarts_of(0)
	);
	// ...and every ticket still completed with a real frame.
	let mut crashed_ticket_seen = false;
	for result in results.lock().unwrap().drain(..) {
		let payload = result.expect("frame rendered despite the worker crash");
		let TicketPayload::ShmFrame(frame) = payload else {
			panic!("ShmFrame payload");
		};
		if frame.meta.id == 1 {
			crashed_ticket_seen = true;
			assert_eq!(frame.meta.width, 64);
			assert_eq!(frame.meta.data_size, 64 * 64 * 4);
		}
		dispatcher.release_frame(&frame);
	}
	assert!(crashed_ticket_seen, "ticket 1 delivered after the restart");
	assert!(marker.exists(), "the crash hook fired exactly once");
	let _ = std::fs::remove_file(&marker);

	dispatcher.shutdown();
}

/// Footage decode inside the worker process: a real H.264 frame from
/// `tests/demo.mp4` is decoded, scaled and converted into a BGRA8 slot.
#[test]
fn worker_decodes_real_footage_into_slot() {
	let _guard = lock_test();
	let demo = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../tests/demo.mp4");
	assert!(demo.exists(), "repo fixture tests/demo.mp4 missing");

	let dispatcher = ProcessDispatcher::new(config(1, 4)).expect("dispatcher config");
	dispatcher.start().expect("worker starts");

	let results = Arc::new(Mutex::new(Vec::new()));
	submit(
		&dispatcher,
		&results,
		2,
		Some((demo.display().to_string(), 0)),
	);
	pump_until(&dispatcher, &results, 2);

	for result in results.lock().unwrap().drain(..) {
		let payload = result.expect("footage frame rendered");
		let TicketPayload::ShmFrame(frame) = payload else {
			panic!("ShmFrame payload");
		};
		assert_eq!(frame.meta.width, 64);
		assert_eq!(frame.meta.height, 64);
		assert_eq!(frame.meta.format, SLOT_FORMAT_BGRA8);
		// Decoded video is opaque: every BGRA alpha byte is 255.
		let pixels = &frame.shm.slot_bytes(frame.slot)[..frame.meta.data_size as usize];
		let alpha_ok = pixels
			.chunks_exact(4)
			.filter(|px| px[3] == 255)
			.count();
		assert!(
			alpha_ok as f64 >= 0.99 * (64 * 64) as f64,
			"decoded frame must be opaque ({alpha_ok}/4096)"
		);
		dispatcher.release_frame(&frame);
	}

	dispatcher.shutdown();
}
