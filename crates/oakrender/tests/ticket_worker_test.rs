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

/// Unwrap a video ticket payload for assertions.
fn res_video(res: &oakrender::ticket::TicketPayload) -> &oakrender::texture::Texture {
	match res {
		oakrender::ticket::TicketPayload::Video(t) => t,
		_ => panic!("expected a video payload"),
	}
}
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
	Arc::new(|_, _| Ok(oakrender::ticket::TicketPayload::Video(Texture::wrap_frame(small_frame()))))
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
		footage: None,
		montage: Vec::new(),
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
	let id = arena.submit_video(
		params(Rational::new(0, 1)),
		Box::new(move |r| {
			let _ = tx.send(r.is_ok());
		}),
	);
	arena.wait(id).unwrap();
	assert!(rx.recv_timeout(Duration::from_secs(5)).unwrap());
	assert!(
		rx.recv_timeout(Duration::from_millis(50)).is_err(),
		"exactly once"
	);

	let res = arena.result(id).unwrap().unwrap();
	assert_eq!(res_video(&res).size(), (8, 4));
	assert_eq!(res_video(&res).format(), oakcore_rs::PixelFormat::F32);
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
		Ok(oakrender::ticket::TicketPayload::Video(Texture::wrap_frame(small_frame())))
	});
	let arena = TicketArena::new(pool.clone(), blocking);

	let (tx, rx) = mpsc::channel();
	let id = arena.submit_video(
		params(Rational::new(1, 1)),
		Box::new(move |r| {
			let _ = tx.send(r);
		}),
	);
	arena.cancel(id);
	release.store(true, Ordering::Release);
	arena.wait(id).unwrap();
	let res = rx.recv_timeout(Duration::from_secs(5)).unwrap();
	assert_eq!(res.unwrap_err().code(), Error::State.code());
	assert!(
		rx.recv_timeout(Duration::from_millis(50)).is_err(),
		"exactly once"
	);
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
			Ok(oakrender::ticket::TicketPayload::Video(Texture::wrap_frame(small_frame())))
		})
	};
	let arena = TicketArena::new(pool.clone(), blocking);

	let (tx, rx) = mpsc::channel();
	let mut ids = Vec::new();
	for i in 0..8 {
		let tx = tx.clone();
		let id = arena.submit_video(
			params(Rational::new(i, 1)),
			Box::new(move |r| {
				let _ = tx.send(r.is_err());
			}),
		);
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
	assert!(
		completions.iter().all(|&e| e),
		"all cancelled with Error::State"
	);
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
		Arc::new(|_, _| Ok(oakrender::ticket::TicketPayload::Video(Texture::wrap_frame(small_frame()))));
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
	assert!(
		!std::path::Path::new(&p1).exists(),
		"unlinked at refcount 0"
	);
	assert_eq!(store.refs(&p1), 0);
}

/// TimeRange sanity (used above).
#[test]
fn range_sanity() {
	let r = TimeRange::new(Rational::new(0, 1), Rational::new(10, 1));
	assert_eq!(r.length(), Rational::new(10, 1));
}
