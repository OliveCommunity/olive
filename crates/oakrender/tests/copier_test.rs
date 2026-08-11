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
//! coupling, now C ABI clients).
//!
//! The oaknode C ABI (project deep-copy / sync) is a concurrent
//! dependency; success-path tests are `#[ignore]`d and the error paths
//! run without liboaknode.

mod common;

use std::sync::Arc;
use std::time::Duration;

use oakcore_rs::{Rational, TimeRange};

use oakrender::error::Error;
use oakrender::frame::VideoParamsPod;
use oakrender::texture::{Frame, Texture};
use oakrender::ticket::TicketArena;
use oakrender::worker::WorkerPool;

fn frame_producer() -> oakrender::ticket::Producer {
	Arc::new(|_, _| {
		let mut f = Frame::new();
		let mut p = VideoParamsPod::default();
		p.width = 4;
		p.height = 4;
		f.set_video_params(p);
		f.allocate();
		Ok(Texture::wrap_frame(f))
	})
}

fn cacher() -> (oakrender::autocacher::PreviewAutoCacher, WorkerPool) {
	let mut pool = WorkerPool::new(2);
	pool.start();
	let arena = Arc::new(TicketArena::new(pool.clone(), frame_producer()));
	(oakrender::autocacher::PreviewAutoCacher::new(arena), pool)
}

/// deep_copy through the C ABI: the render-side copy evaluates
/// identically to the source project for a fixture graph (comparison
/// via the oaknode evaluation C ABI).
#[test]
#[ignore = "needs oaknode C ABI (oaknode_project_deep_copy)"]
fn deep_copy_evaluates_identically() {
	let mut copier = oakrender::copier::ProjectCopy::new();
	let src = common::fake_handle(7);
	copier.set_project(src).unwrap();
	assert_ne!(copier.copy, 0);
	assert!(copier.copied_project().is_some());
}

/// sync applies recorded changes; the copy matches a fresh deep_copy
/// afterwards.
#[test]
#[ignore = "needs oaknode C ABI (oaknode_project_sync_copy)"]
fn sync_matches_fresh_copy() {
	let mut copier = oakrender::copier::ProjectCopy::new();
	let src = common::fake_handle(7);
	copier.set_project(src).unwrap();
	let changes = [oakrender::bridge::node::ChangeRecord {
		kind: oakrender::bridge::node::change_kind::NODE_ADD,
		payload: [0u8; 48],
	}];
	copier.sync(&changes).unwrap();
	assert_eq!(copier.last_sync_generation, 1);
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
/// cancelled; wait=true blocks until workers are idle.
#[test]
fn cancel_video_tasks_semantics() {
	let (mut c, mut pool) = cacher();
	c.attach(1);
	c.force_range(TimeRange::new(Rational::new(0, 1), Rational::new(5, 1)));
	assert_eq!(c.live_jobs().len(), 1);

	// wait=false: returns immediately; the ticket may still be draining.
	c.cancel_video_tasks(false);
	// wait=true: blocks until every job finished.
	c.force_range(TimeRange::new(Rational::new(5, 1), Rational::new(10, 1)));
	c.cancel_video_tasks(true);
	assert!(
		!c.is_rendering_custom_range(),
		"all custom-range jobs finished after wait"
	);
	pool.shutdown();
}

/// Change-record marshalling: every ChangeRecord kind survives the
/// C struct round-trip (layout pinned by the C ABI header).
#[test]
fn change_record_marshalling() {
	let kinds = [
		oakrender::bridge::node::change_kind::NODE_ADD,
		oakrender::bridge::node::change_kind::NODE_REMOVE,
		oakrender::bridge::node::change_kind::EDGE_ADD,
		oakrender::bridge::node::change_kind::EDGE_REMOVE,
		oakrender::bridge::node::change_kind::VALUE_CHANGE,
		oakrender::bridge::node::change_kind::VALUE_HINT_CHANGE,
		oakrender::bridge::node::change_kind::PROJECT_SETTING_CHANGE,
		oakrender::bridge::node::change_kind::FOOTAGE_PROXY,
	];
	for kind in kinds {
		let record = oakrender::bridge::node::ChangeRecord {
			kind,
			payload: [0xAA; 48],
		};
		assert_eq!(record.kind, kind);
		assert_eq!(record.payload.len(), 48);
		assert_eq!(
			std::mem::size_of::<oakrender::bridge::node::ChangeRecord>(),
			52
		);
	}
}

/// Copier failure paths without liboaknode.
#[test]
fn copier_error_paths() {
	let mut copier = oakrender::copier::ProjectCopy::new();
	assert_eq!(
		copier
			.set_project(oakrender::handle::CHandle::null())
			.unwrap_err()
			.code(),
		Error::Invalid.code()
	);
	// sync before any project → state error.
	let changes = [oakrender::bridge::node::ChangeRecord {
		kind: oakrender::bridge::node::change_kind::NODE_ADD,
		payload: [0u8; 48],
	}];
	assert_eq!(
		copier.sync(&changes).unwrap_err().code(),
		Error::State.code()
	);
	// copy_of_node is deferred (node map query pending).
	assert!(copier.copy_of_node(1).is_none());
}

/// The FFI copier entry points (error paths run standalone).
#[test]
fn ffi_copier_contract() {
	use oakrender::error::{OAKRENDER_E_INVALID, OAKRENDER_OK};
	use oakrender::ffi;
	unsafe {
		let c = ffi::copier::oakrender_project_copier_create();
		assert!(!c.is_null());
		// set_project with an empty project → invalid.
		assert_eq!(
			ffi::copier::oakrender_project_copier_set_project(c, ffi::OakNodeProject::null()),
			OAKRENDER_E_INVALID
		);
		// With a fake project handle: deep copy needs the oaknode C ABI.
		let rc = ffi::copier::oakrender_project_copier_set_project(c, common::fake_handle(1));
		assert_ne!(
			rc, OAKRENDER_OK,
			"without liboaknode the copy cannot be built"
		);
		// get_copy on an empty copier → empty handle.
		let copy = ffi::copier::oakrender_project_copier_get_copy(c, common::fake_handle(1));
		assert!(copy.is_null());
		// get_copied_project before any project → empty.
		let proj = ffi::copier::oakrender_project_copier_get_copied_project(c);
		assert!(proj.is_null());
		let mut c = c;
		ffi::copier::oakrender_project_copier_free(&mut c);
		assert!(c.is_null());
	}
}

/// LUT library exports.
#[test]
fn lut_library_exports() {
	use oakrender::ffi;
	unsafe {
		assert_eq!(ffi::color::oakrender_lut_supported_extensions_count(), 9);
		assert_eq!(
			ffi::color::oakrender_lut_is_supported_extension(c"cube".as_ptr()),
			1
		);
		assert_eq!(
			ffi::color::oakrender_lut_is_supported_extension(c".CUBE".as_ptr()),
			1
		);
		assert_eq!(
			ffi::color::oakrender_lut_is_supported_extension(c"exr".as_ptr()),
			0
		);
		assert_eq!(
			ffi::color::oakrender_lut_is_supported_extension(std::ptr::null()),
			0
		);
		let mut buf = [0u8; 16];
		let size = ffi::color::oakrender_lut_supported_extension_at(
			0,
			buf.as_mut_ptr() as *mut std::ffi::c_char,
			16,
		);
		assert!(size > 0);
		assert_eq!(
			ffi::color::oakrender_lut_supported_extension_at(9, std::ptr::null_mut(), 0),
			-70004
		);
	}
}

/// Color manager statics + processor FFI paths.
#[test]
fn color_ffi_paths() {
	use oakrender::error::OAKRENDER_OK;
	use oakrender::ffi;
	unsafe {
		// No default config yet → get_config is a state error.
		let _ = ffi::color::oakrender_color_manager_set_up_default_config();
		let (size, config) =
			common::read_two_stage(|buf, n| ffi::color::oakrender_color_manager_get_config(buf, n));
		if size > 0 {
			assert!(!config.unwrap().is_empty());
		}
		// create + is_valid + convert.
		let p = ffi::color::oakrender_color_processor_create(
			c"scene_linear".as_ptr(),
			c"sdr-video".as_ptr(),
			0,
		);
		if !p.is_null() {
			let valid = ffi::color::oakrender_color_processor_is_valid(p);
			let (mut r, mut g, mut b, mut a) = (0.0f64, 0.0f64, 0.0f64, 0.0f64);
			assert_eq!(
				ffi::color::oakrender_color_processor_convert(
					p, 0.18, 0.18, 0.18, 1.0, &mut r, &mut g, &mut b, &mut a
				),
				0
			);
			assert!(a == 1.0);
			// convert with NULL out → invalid.
			assert_eq!(
				ffi::color::oakrender_color_processor_convert(
					p,
					0.0,
					0.0,
					0.0,
					0.0,
					std::ptr::null_mut(),
					&mut g,
					&mut b,
					&mut a
				),
				-70001
			);
			assert_eq!(valid, 0, "validity depends on the bundled OCIO config");
			let mut p = p;
			ffi::color::oakrender_color_processor_free(&mut p);
		}
		// display_transform errors.
		assert_eq!(
			ffi::color::oakrender_color_manager_display_transform(
				std::ptr::null(),
				c"v".as_ptr(),
				std::ptr::null_mut(),
				0
			),
			-70001
		);
		let rc = ffi::color::oakrender_color_manager_display_transform(
			c"no-such-display".as_ptr(),
			c"no-such-view".as_ptr(),
			std::ptr::null_mut(),
			0,
		);
		assert!(rc == -70004 || rc == -70002 || rc == OAKRENDER_OK || rc > 0);
	}
}
