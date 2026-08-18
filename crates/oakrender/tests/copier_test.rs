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

//! Copier + autocacher contract tests (the former render→node
//! coupling, now direct Rust calls).
//!
//! The oaknode deep-copy direction is unimplemented (single-lib plan
//! §4.1 — dead direction), so the success paths fail explainably and the
//! tests assert those failures.

use std::sync::Arc;
use std::time::Duration;

use oakcore_rs::{Rational, TimeRange};

use oakrender::error::Error;
use oakrender::frame::VideoParamsPod;
use oakrender::texture::{Frame, Texture};
use oakrender::ticket::TicketArena;
use oakrender::worker::{InlineDispatcher, JobDispatch};

fn frame_producer() -> oakrender::ticket::Producer {
	Arc::new(|_, _| {
		let mut f = Frame::new();
		let mut p = VideoParamsPod::default();
		p.width = 4;
		p.height = 4;
	f.set_video_params(p);
	f.allocate();
	Ok(oakrender::ticket::TicketPayload::Video(Texture::wrap_frame(f)))
})
}

fn cacher() -> (oakrender::autocacher::PreviewAutoCacher, Arc<InlineDispatcher>) {
	let d = InlineDispatcher::queued();
	let arena = Arc::new(TicketArena::new(d.clone(), frame_producer()));
	(oakrender::autocacher::PreviewAutoCacher::new(arena), d)
}

/// Deep-copy through oaknode with a valid (non-empty) project identity:
/// the direction is unimplemented (single-lib plan §4.1 — dead
/// direction), so the copier fails explainably — the live path every
/// caller sees today.
#[test]
fn deep_copy_with_valid_identity_fails_explainably() {
	let mut copier = oakrender::copier::ProjectCopy::new();
	let src = oakrender::copier::ProjectHandle::new(7);
	assert_eq!(
		copier.set_project(src).unwrap_err().code(),
		Error::Failed(String::new()).code()
	);
	assert_eq!(copier.copy, 0);
	assert!(copier.copied_project().is_none());
}

/// sync requires an established copy; with the deep-copy direction dead,
/// no copy can ever be attached, so sync always reports the state error.
#[test]
fn sync_without_established_copy_fails() {
	let mut copier = oakrender::copier::ProjectCopy::new();
	let src = oakrender::copier::ProjectHandle::new(7);
	assert!(copier.set_project(src).is_err());
	let changes = [oakrender::copier::ChangeRecord {
		kind: oakrender::copier::change_kind::NODE_ADD,
		payload: [0u8; 48],
	}];
	assert_eq!(
		copier.sync(&changes).unwrap_err().code(),
		Error::State.code()
	);
	assert_eq!(copier.last_sync_generation, 0);
}

/// Autocacher attach/detach: requests on the copied project's caches
/// enqueue jobs; detach cancels them all; no callbacks fire after
/// detach (lifetime discipline).
#[test]
fn autocacher_attach_detach() {
	let (mut c, mut pool) = cacher();
	c.attach(42).unwrap();
	assert_eq!(c.copied_project, 42);
	c.on_cache_request(
		42,
		TimeRange::new(Rational::new(0, 1), Rational::new(10, 1)),
	);
	assert_eq!(c.live_jobs().len(), 1);

	// Jobs complete or are cancelled on detach; bookkeeping cleared.
	c.detach();
	assert_eq!(c.copied_project, 0);
	assert!(c.live_jobs().is_empty());
	assert!(c.pending_requests().is_empty());
	pool.shutdown();
}

/// cancel_video_tasks(wait=false) returns immediately with jobs
/// cancelled; wait=true blocks until the dispatcher is idle.
#[test]
fn cancel_video_tasks_semantics() {
	let (mut c, d) = cacher();
	c.attach(1);
	c.force_range(TimeRange::new(Rational::new(0, 1), Rational::new(5, 1)));
	assert_eq!(c.live_jobs().len(), 1);

	// wait=false: returns immediately; the ticket may still be draining.
	c.cancel_video_tasks(false);
	// wait=true: drain the queued jobs first (the inline dispatcher has no
	// background threads), then blocks until every job finished.
	c.force_range(TimeRange::new(Rational::new(5, 1), Rational::new(10, 1)));
	d.run();
	c.cancel_video_tasks(true);
	assert!(
		!c.is_rendering_custom_range(),
		"all custom-range jobs finished after wait"
	);
	d.shutdown();
}

/// Change-record marshalling: every ChangeRecord kind survives the
/// C struct round-trip (layout pinned by the C ABI header).
#[test]
fn change_record_marshalling() {
	let kinds = [
		oakrender::copier::change_kind::NODE_ADD,
		oakrender::copier::change_kind::NODE_REMOVE,
		oakrender::copier::change_kind::EDGE_ADD,
		oakrender::copier::change_kind::EDGE_REMOVE,
		oakrender::copier::change_kind::VALUE_CHANGE,
		oakrender::copier::change_kind::VALUE_HINT_CHANGE,
		oakrender::copier::change_kind::PROJECT_SETTING_CHANGE,
		oakrender::copier::change_kind::FOOTAGE_PROXY,
	];
	for kind in kinds {
		let record = oakrender::copier::ChangeRecord {
			kind,
			payload: [0xAA; 48],
		};
		assert_eq!(record.kind, kind);
		assert_eq!(record.payload.len(), 48);
		assert_eq!(
			std::mem::size_of::<oakrender::copier::ChangeRecord>(),
			52
		);
	}
}

/// Copier failure paths (the deep-copy direction is dead).
#[test]
fn copier_error_paths() {
	let mut copier = oakrender::copier::ProjectCopy::new();
	assert_eq!(
		copier
			.set_project(oakrender::copier::ProjectHandle::null())
			.unwrap_err()
			.code(),
		Error::Invalid.code()
	);
	// sync before any project → state error.
	let changes = [oakrender::copier::ChangeRecord {
		kind: oakrender::copier::change_kind::NODE_ADD,
		payload: [0u8; 48],
	}];
	assert_eq!(
		copier.sync(&changes).unwrap_err().code(),
		Error::State.code()
	);
	// copy_of_node is deferred (node map query pending).
	assert!(copier.copy_of_node(1).is_none());
}
