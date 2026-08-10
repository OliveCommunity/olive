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

//! Real-oakrender integration: drive oaktask's [`RenderTask`] render loop
//! against the **actual** oakrender ticket arena instead of the link-time
//! stubs of `tests/common/mod.rs`.
//!
//! The two crates' C ABIs are joined the way the app joins them: oaktask's
//! `bridge::render` `extern "C"` declarations resolve against oakrender's
//! `#[no_mangle]` exports (both rlibs linked into this one test binary),
//! the `oakrender_ticket_finished_fn` callback (the frozen 2-argument
//! `(ticket, userdata)` contract) fires on the arena's worker threads, and
//! the loop's reorder buffer delivers the frames to the hooks in timestamp
//! order. The render crate's CPU path is used (`eval::render_produced_frame`
//! generates F32 frames; no GPU is initialized).
//!
//! Only the render symbols are real; the oaknode/oakcodec/oakcommon/undo
//! sides still come from the `common` stubs (the render loop only touches
//! them for graph queries and timebase reads).
//!
//! Build / run (both rlibs in one binary — duplicate `#[no_mangle]`
//! symbols make this incompatible with the stub-based test binaries, so it
//! must be built with the feature and the `--test` filter):
//!
//! ```text
//! cargo test --features real-oakrender --test render_real_integration_test
//! ```

#![cfg(feature = "real-oakrender")]
#![allow(clippy::missing_safety_doc)]

#[path = "common/mod.rs"]
mod common;

use std::sync::atomic::Ordering;

use oaktask::error::Result;
use oaktask::handle::CHandle;
use oaktask::render::{ForceParams, RenderTask, RenderTaskBehavior};
use oaktask::task::Task;
use oakcore_rs::{Rational, TimeRange};

/// Records what the render loop delivers: each frame's timestamp (the
/// producer stamps `Frame::timestamp` with the ticket time) and size.
#[derive(Default)]
struct RecordingBehavior {
	timestamps: Vec<Rational>,
	sizes: Vec<(i32, i32)>,
}

impl RenderTaskBehavior for RecordingBehavior {
	fn frame_downloaded(&mut self, _task: &mut Task, frame: CHandle) -> Result<()> {
		assert!(!frame.is_null(), "real arena must hand out an owned frame");
		// The frame handle boxes a `oakrender::texture::Frame` (created by
		// `oakrender_ticket_get_frame`); read it through oakrender's own
		// handle API to assert the real payload.
		let rh = oakrender::handle::CHandle {
			ctx: frame.ctx,
			addref: frame.addref,
			release: frame.release,
			abi_version: frame.abi_version,
		};
		let f = unsafe { oakrender::handle::get::<oakrender::texture::Frame>(&rh) }
			.expect("frame handle must box a real oakrender Frame");
		self.timestamps.push(f.timestamp);
		self.sizes.push((f.width, f.height));
		Ok(())
	}

	fn audio_downloaded(&mut self, _task: &mut Task, _buffer: CHandle) -> Result<()> {
		panic!("audio disabled for this test")
	}

	fn encode_subtitle(&mut self, _task: &mut Task, _text: &str) -> Result<()> {
		Ok(())
	}
}

/// Serializes against anything else touching the process-wide manager.
fn manager_guard() -> std::sync::MutexGuard<'static, ()> {
	static LOCK: std::sync::Mutex<()> = std::sync::Mutex::new(());
	LOCK.lock().unwrap_or_else(|e| e.into_inner())
}

/// The real-oakrender end-to-end run: 3 one-second frames (timebase 1/1)
/// through the task render loop into the arena, delivered in timestamp
/// order with the forced 64x64 size.
#[test]
fn render_loop_submits_real_tickets_and_delivers_in_timestamp_order() {
	let _guard = manager_guard();
	common::reset_stubs();
	common::VIDEO_TIME_BASE_NUM.store(1, Ordering::SeqCst);
	common::VIDEO_TIME_BASE_DEN.store(1, Ordering::SeqCst);

	// Fresh real manager (CPU path; the ticket producer is eval-based, no
	// GPU backend is created).
	oakrender::manager::RenderManager::shutdown();
	oakrender::manager::RenderManager::init().expect("real oakrender manager init");
	let baseline = oakrender::handle::alive_count();

	{
		let range = TimeRange::new(Rational::new(0, 1), Rational::new(3, 1));
		let base = Task::new("real-render-integration", CHandle::null());
		let mut rt = RenderTask::new(
			base,
			common::fake_handle(), // borrowed video params (ctx checked only)
			CHandle::null(),       // no audio
			common::fake_handle(), // viewer node (the node stub supplies the output)
			ForceParams {
				force_width: 64,
				force_height: 64,
				..Default::default()
			},
			None,
		);
		rt.set_render_inputs(CHandle::null(), CHandle::null(), 0, false, range);
		rt.set_max_inflight(3);

		let mut behavior = RecordingBehavior::default();
		let mut task = std::mem::replace(&mut rt.base, Task::new("", CHandle::null()));
		let result = rt.render(&mut task, &mut behavior);
		drop(rt);

		assert!(result.is_ok(), "render through the real arena must succeed: {result:?}");
		assert_eq!(
			behavior.timestamps,
			vec![
				Rational::new(0, 1),
				Rational::new(1, 1),
				Rational::new(2, 1),
			],
			"frames must arrive in timestamp order (reorder buffer over the real arena)"
		);
		assert_eq!(
			behavior.sizes,
			vec![(64, 64), (64, 64), (64, 64)],
			"force params must reach the real producer"
		);
		// `task` owns the real cancel atom created by Task::new; dropping it
		// releases the atom so the handle accounting below returns to
		// baseline.
		drop(task);
	}

	oakrender::manager::RenderManager::shutdown();
	assert_eq!(
		oakrender::handle::alive_count(),
		baseline,
		"every handle created by the run (tickets, frames, cancel atom) must be released"
	);
}
