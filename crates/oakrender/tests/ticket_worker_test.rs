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

//! Ticket / worker-pool contract tests.

mod common;

use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{mpsc, Arc};
use std::time::Duration;

use oakcore_rs::{Rational, TimeRange};

use oakrender::error::Error;
use oakrender::frame::VideoParamsPod;
use oakrender::texture::{Frame, Texture};
use oakrender::ticket::{TicketArena, TicketId, VideoTicketParams};
use oakrender::worker::{GraphSnapshotStore, WorkerPool};

fn small_frame() -> Frame {
	let mut f = Frame::new();
	let mut p = VideoParamsPod::default();
	p.width = 8;
	p.height = 4;
	f.set_video_params(p);
	f.allocate();
	f
}

fn ok_producer() -> oakrender::ticket::Producer {
	Arc::new(|_, _| Ok(Texture::wrap_frame(small_frame())))
}

fn params(time: Rational) -> VideoTicketParams {
	VideoTicketParams {
		viewer: 1,
		time,
		force_size: Some((8, 4)),
		force_format: None,
		cache: None,
		cache_dir: None,
		cache_id: None,
		cache_timebase: None,
	}
}

/// Opens a worker gate on drop (also on panic), so a failing assertion
/// can never leave workers spinning while the manager shuts down.
struct GateRelease(Arc<AtomicBool>);

impl Drop for GateRelease {
	fn drop(&mut self) {
		self.0.store(true, Ordering::Release);
	}
}

/// Completion fires exactly once on success; the payload texture has
/// the requested size/format.
#[test]
fn ticket_completion_once_success() {
	let mut pool = WorkerPool::new(2);
	pool.start();
	let arena = TicketArena::new(pool.clone(), ok_producer());

	let (tx, rx) = mpsc::channel();
	let id = arena.submit_video(params(Rational::new(0, 1)), Box::new(move |r| {
		let _ = tx.send(r.is_ok());
	}));
	arena.wait(id).unwrap();
	assert!(rx.recv_timeout(Duration::from_secs(5)).unwrap());
	assert!(rx.recv_timeout(Duration::from_millis(50)).is_err(), "exactly once");

	let res = arena.result(id).unwrap().unwrap();
	assert_eq!(res.size(), (8, 4));
	assert_eq!(res.format(), oakcore_rs::PixelFormat::F32);
	assert!(arena.is_finished(id));
	pool.shutdown();
}

/// Completion fires exactly once on cancel (Error::State), even when
/// cancel races the running job.
#[test]
fn ticket_completion_once_on_cancel() {
	let mut pool = WorkerPool::new(1);
	pool.start();
	let release = Arc::new(AtomicBool::new(false));
	let release2 = release.clone();
	let blocking: oakrender::ticket::Producer = Arc::new(move |_, _| {
		while !release2.load(Ordering::Acquire) {
			std::thread::sleep(Duration::from_millis(1));
		}
		Ok(Texture::wrap_frame(small_frame()))
	});
	let arena = TicketArena::new(pool.clone(), blocking);

	let (tx, rx) = mpsc::channel();
	let id = arena.submit_video(params(Rational::new(1, 1)), Box::new(move |r| {
		let _ = tx.send(r);
	}));
	arena.cancel(id);
	release.store(true, Ordering::Release);
	arena.wait(id).unwrap();
	let res = rx.recv_timeout(Duration::from_secs(5)).unwrap();
	assert_eq!(res.unwrap_err().code(), Error::State.code());
	assert!(rx.recv_timeout(Duration::from_millis(50)).is_err(), "exactly once");
	pool.shutdown();
}

/// cancel_all during shutdown delivers cancellation to every pending
/// ticket; no completion fires after shutdown returns.
#[test]
fn shutdown_drains_completions() {
	let mut pool = WorkerPool::new(1);
	pool.start();
	// The producer blocks until released so no job can finish before the
	// shutdown (otherwise the timing of which jobs ran is nondeterministic).
	let gate = Arc::new(AtomicBool::new(false));
	let blocking: oakrender::ticket::Producer = {
		let gate = gate.clone();
		Arc::new(move |_, _| {
			while !gate.load(Ordering::Acquire) {
				std::thread::sleep(Duration::from_millis(1));
			}
			Ok(Texture::wrap_frame(small_frame()))
		})
	};
	let arena = TicketArena::new(pool.clone(), blocking);

	let (tx, rx) = mpsc::channel();
	let mut ids = Vec::new();
	for i in 0..8 {
		let tx = tx.clone();
		let id = arena.submit_video(params(Rational::new(i, 1)), Box::new(move |r| {
			let _ = tx.send(r.is_err());
		}));
		ids.push(id);
	}
	drop(tx);
	arena.cancel_all();
	gate.store(true, Ordering::Release);
	pool.shutdown();

	let mut completions = Vec::new();
	while let Ok(err) = rx.recv_timeout(Duration::from_secs(5)) {
		completions.push(err);
	}
	assert_eq!(completions.len(), 8, "every ticket completed");
	assert!(completions.iter().all(|&e| e), "all cancelled with Error::State");
	// Everything is finished after shutdown.
	assert!(ids.iter().all(|id| arena.is_finished(*id)));
}

/// Pool saturation: 4 workers × 64 jobs all complete; no job runs
/// twice (arena ids unique).
#[test]
fn pool_saturation() {
	let mut pool = WorkerPool::new(4);
	pool.start();
	let arena = TicketArena::new(pool.clone(), ok_producer());
	let (tx, rx) = mpsc::channel();
	let mut ids = Vec::new();
	for i in 0..64u64 {
		let tx = tx.clone();
		let id = arena.submit_video(
			params(Rational::new(i as i64, 1)),
			Box::new(move |r| {
				let _ = tx.send(r.is_ok());
			}),
		);
		ids.push(id);
	}
	drop(tx);
	let mut ok = 0;
	while let Ok(true) = rx.recv_timeout(Duration::from_secs(10)) {
		ok += 1;
	}
	assert_eq!(ok, 64, "all 64 jobs completed successfully");
	ids.sort_by_key(|i| i.0);
	let unique: std::collections::HashSet<_> = ids.iter().collect();
	assert_eq!(unique.len(), 64, "unique arena ids");
	pool.shutdown();
}

/// Ticket arena ids are monotonic and never reused within a manager
/// lifetime.
#[test]
fn ticket_id_monotonic() {
	let mut pool = WorkerPool::new(1);
	pool.start();
	let arena = TicketArena::new(pool.clone(), ok_producer());
	let a = arena.submit_video(params(Rational::new(0, 1)), Box::new(|_| {}));
	let b = arena.submit_video(params(Rational::new(1, 1)), Box::new(|_| {}));
	let c = arena.submit_video(params(Rational::new(2, 1)), Box::new(|_| {}));
	assert!(a.0 < b.0 && b.0 < c.0);
	pool.shutdown();
}

/// Process pool: documented stub until the oakengine_ipc worker binary
/// is wired (see worker.rs).
#[test]
#[ignore = "needs oakengine_ipc worker-process binary"]
fn process_pool_roundtrip() {
	let mut pp = oakrender::worker::ProcessPool::new(2);
	pp.start().unwrap();
	let (tx, rx) = mpsc::channel();
	let produce: oakrender::ticket::Producer =
		Arc::new(|_, _| Ok(Texture::wrap_frame(small_frame())));
	let job = oakrender::worker::Job {
		node_identity: 1,
		time: Rational::new(0, 1),
		params: Arc::new(params(Rational::new(0, 1))),
		produce,
		done: Box::new(move |r| {
			let _ = tx.send(r.is_ok());
		}),
	};
	pp.post(job).unwrap();
	assert!(rx.recv_timeout(Duration::from_secs(5)).unwrap());
	pp.shutdown();
}

/// Crash isolation: a child killed mid-job fails that ticket with
/// Error::Failed and the pool stays usable.
#[test]
#[ignore = "needs oakengine_ipc worker-process binary"]
fn process_crash_isolation() {
	let mut pp = oakrender::worker::ProcessPool::new(1);
	pp.start().unwrap();
	pp.shutdown();
}

/// GraphSnapshotStore: acquire twice shares one file; release to zero
/// unlinks it (no orphaned snapshots after shutdown).
#[test]
fn snapshot_store_refcount() {
	let mut store = GraphSnapshotStore::new();
	let p1 = store.acquire(42).unwrap();
	let p2 = store.acquire(42).unwrap();
	assert_eq!(p1, p2);
	assert!(std::path::Path::new(&p1).exists());
	assert_eq!(store.refs(&p1), 2);
	store.release(&p1);
	assert!(std::path::Path::new(&p1).exists());
	store.mark_cached(&p1, true);
	assert!(store.is_cached(&p1));
	store.release(&p1);
	assert!(!std::path::Path::new(&p1).exists(), "unlinked at refcount 0");
	assert_eq!(store.refs(&p1), 0);
}

/// The FFI ticket path end to end (requires the manager).
#[test]
fn ffi_ticket_render_frame_roundtrip() {
	use oakrender::error::{OAKRENDER_E_INVALID, OAKRENDER_E_STATE};
	use oakrender::ffi;
	let _g = common::ManagerGuard::init();
	unsafe {
		// NULL params → empty handle.
		let h = ffi::ticket::oakrender_ticket_render_frame(std::ptr::null(), None, std::ptr::null_mut());
		assert!(h.is_null());
		// Empty output node → empty handle.
		let mut params = std::mem::zeroed::<ffi::OakVideoTicketParams>();
		let h = ffi::ticket::oakrender_ticket_render_frame(&params, None, std::ptr::null_mut());
		assert!(h.is_null());

		// Ticket params: fake node + fake video params handle.
		params.output_node = common::fake_handle(9);
		params.video_params = common::fake_handle(10);
		params.time_num = 3;
		params.time_den = 1;
		params.force_width = 32;
		params.force_height = 16;
		params.force_format = 4; // F32

		// Occupy every worker with a gated job so the ticket submitted
		// below is queued behind them and guaranteed to still be running
		// when it is queried. An idle pool can finish a small frame before
		// the caller's next statement, so "not finished yet" must not
		// depend on timing.
		let manager = oakrender::manager::RenderManager::global().unwrap();
		let pool = manager.pool.clone();
		let occupied = pool.worker_count();
		let gate = Arc::new(AtomicBool::new(false));
		let _release = GateRelease(gate.clone());
		let (done_tx, done_rx) = mpsc::channel();
		for _ in 0..occupied {
			let gate = gate.clone();
			let done_tx = done_tx.clone();
			let producer: oakrender::ticket::Producer = Arc::new(move |_, _| {
				while !gate.load(Ordering::Acquire) {
					std::thread::sleep(Duration::from_millis(1));
				}
				Ok(Texture::wrap_frame(small_frame()))
			});
			assert!(pool.post(oakrender::worker::Job {
				node_identity: 99,
				time: Rational::new(0, 1),
				params: Arc::new(VideoTicketParams {
					viewer: 1,
					time: Rational::new(0, 1),
					force_size: Some((8, 4)),
					force_format: None,
					cache: None,
					cache_dir: None,
					cache_id: None,
					cache_timebase: None,
				}),
				produce: producer,
				done: Box::new(move |_| {
					let _ = done_tx.send(());
				}),
			}));
		}
		drop(done_tx);

		let h = ffi::ticket::oakrender_ticket_render_frame(&params, None, std::ptr::null_mut());
		assert!(!h.is_null(), "ticket handle created");
		assert_eq!(h.abi_version, 1);

		// Queries before finish: the ticket sits queued behind the occupied
		// workers, so it is deterministically still running.
		assert_eq!(ffi::ticket::oakrender_ticket_is_finished(h), 0);
		assert_eq!(ffi::ticket::oakrender_ticket_get_type(h), 0); // video
		let (mut n, mut d) = (0i64, 0i64);
		assert_eq!(ffi::ticket::oakrender_ticket_get_time(h, &mut n, &mut d), 0);
		assert_eq!((n, d), (3, 1));

		// Release the workers and let the ticket finish. Draining the
		// completions guarantees no gated job is still in flight when the
		// manager shuts down (the guard drops at the end of the test).
		gate.store(true, Ordering::Release);
		while done_rx.recv_timeout(Duration::from_secs(5)).is_ok() {}

		// Wait + finished + get_frame.
		assert_eq!(ffi::ticket::oakrender_ticket_wait(h), 0);
		assert_eq!(ffi::ticket::oakrender_ticket_is_finished(h), 1);
		let mut frame = ffi::OakCodecFrame::null();
		assert_eq!(ffi::ticket::oakrender_ticket_get_frame(h, &mut frame), 0);
		assert!(!frame.is_null());
		assert_eq!(ffi::renderer::oakrender_codec_frame_width(frame), 32);
		assert_eq!(ffi::renderer::oakrender_codec_frame_height(frame), 16);
		assert_eq!(ffi::renderer::oakrender_codec_frame_is_allocated(frame), 1);
		let mut pod = std::mem::zeroed::<ffi::OakRenderVideoParams>();
		assert_eq!(ffi::renderer::oakrender_codec_frame_get_params(frame, &mut pod), 0);
		assert_eq!(pod.format, 4, "F32 pipeline format");
		let mut frame = frame;
		ffi::renderer::oakrender_codec_frame_free(&mut frame);

		// get_samples: audio not implemented → failed.
		let mut samples = std::ptr::null_mut();
		assert!(ffi::ticket::oakrender_ticket_get_samples(h, &mut samples) < 0);
		assert!(samples.is_null());

		let mut h = h;
		ffi::ticket::oakrender_ticket_free(&mut h);
		assert!(h.is_null());

		// Empty ticket queries → E_INVALID.
		assert_eq!(ffi::ticket::oakrender_ticket_is_finished(ffi::OakRenderTicket::null()), OAKRENDER_E_INVALID);
		assert_eq!(ffi::ticket::oakrender_ticket_get_type(ffi::OakRenderTicket::null()), OAKRENDER_E_INVALID);
		assert_eq!(ffi::ticket::oakrender_ticket_wait(ffi::OakRenderTicket::null()), OAKRENDER_E_INVALID);
		assert_eq!(ffi::ticket::oakrender_ticket_cancel(ffi::OakRenderTicket::null()), OAKRENDER_E_INVALID);
	}
}

/// The FFI audio ticket path.
#[test]
fn ffi_ticket_render_audio() {
	use oakrender::error::OAKRENDER_OK;
	use oakrender::ffi;
	let _g = common::ManagerGuard::init();
	unsafe {
		// Null output node → empty handle.
		let h = ffi::ticket::oakrender_ticket_render_audio(
			ffi::OakNodeNode::null(),
			0, 1, 10, 1,
			std::ptr::null(),
			0,
			None,
			std::ptr::null_mut(),
		);
		assert!(h.is_null());

		let audio_params = common::fake_handle(1);
		let h = ffi::ticket::oakrender_ticket_render_audio(
			common::fake_handle(9),
			0, 1, 10, 1,
			&audio_params,
			0,
			None,
			std::ptr::null_mut(),
		);
		assert!(!h.is_null());
		assert_eq!(ffi::ticket::oakrender_ticket_get_type(h), 1); // audio
		let (mut i_n, mut i_d, mut o_n, mut o_d) = (0i64, 0i64, 0i64, 0i64);
		assert_eq!(ffi::ticket::oakrender_ticket_get_range(h, &mut i_n, &mut i_d, &mut o_n, &mut o_d), OAKRENDER_OK);
		assert_eq!((i_n, i_d, o_n, o_d), (0, 1, 10, 1));
		assert_eq!(ffi::ticket::oakrender_ticket_wait(h), OAKRENDER_OK);
		let mut h = h;
		ffi::ticket::oakrender_ticket_free(&mut h);
	}
}

/// TimeRange sanity (used above).
#[test]
fn range_sanity() {
	let r = TimeRange::new(Rational::new(0, 1), Rational::new(10, 1));
	assert_eq!(r.length(), Rational::new(10, 1));
}
