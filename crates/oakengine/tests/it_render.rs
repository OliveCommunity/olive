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

//! Integration tests for the render family (`crates/oakengine/src/render.rs`,
//! the facade contract of `engine/include/oakengine/{renderer,color,lut}.h`).
//!
//! Covers all 60 exported `oakengine_*` functions of the family: the render
//! manager/cacher setters, the renderer lifecycle + real frame render, the
//! frame/audio-buffer accessors, the color-manager/config/processor surface
//! and the LUT library.
//!
//! Nothing here is mocked: the render manager is initialized through the real
//! oakrender C ABI (`oakrender_manager_init`), frames are produced by the
//! module's CPU eval pipeline, the color processor goes through the real
//! bundled OCIO config (`oakrender_color_manager_set_up_default_config`), and
//! the renderer's sequence handle comes from the real node/timeline families
//! (`oakengine_project_*` + `oakengine_sequence_new`).
//!
//! ## Global state
//!
//! The render manager singleton, the OCIO default config, the `$OCIO`
//! environment variable and the facade undo stack (cleared by
//! `oakengine_project_new`) are process-global, so every test that touches
//! them takes the single [`STATE_LOCK`]. The pure-stub tests (NULL-argument
//! accessors, LUT/audio stubs) run lock-free.

#[path = "common/mod.rs"]
mod common;

use std::ffi::{c_char, c_int, c_void};
use std::sync::{Mutex, MutexGuard};

use oakengine::handle::OakEngineAudioBuffer;
use oakengine::node::{
	oakengine_node_factory_create_from_id, oakengine_node_free, oakengine_project_create,
	oakengine_project_free, oakengine_project_new,
};
use oakengine::render::{
	oakengine_audio_channel_count, oakengine_audio_data, oakengine_audio_free,
	oakengine_audio_sample_count, oakengine_audio_sample_rate,
	oakengine_color_config_colorspace_at, oakengine_color_config_colorspace_count,
	oakengine_color_config_free, oakengine_color_config_load_default,
	oakengine_color_config_load_file, oakengine_color_last_error,
	oakengine_color_manager_colorspace_at, oakengine_color_manager_colorspace_count,
	oakengine_color_manager_compliant_color_space, oakengine_color_manager_compliant_transform,
	oakengine_color_manager_default_display, oakengine_color_manager_default_input_color_space,
	oakengine_color_manager_default_luma_coefs, oakengine_color_manager_default_view,
	oakengine_color_manager_display_at, oakengine_color_manager_display_count,
	oakengine_color_manager_from_project, oakengine_color_manager_get_config_filename,
	oakengine_color_manager_look_at, oakengine_color_manager_look_count,
	oakengine_color_manager_reference_color_space, oakengine_color_manager_set_config_filename,
	oakengine_color_manager_set_default_input_color_space, oakengine_color_manager_view_at,
	oakengine_color_manager_view_count, oakengine_color_processor_convert_color,
	oakengine_color_processor_create, oakengine_color_processor_free, oakengine_color_processor_id,
	oakengine_color_processor_is_valid, oakengine_color_transform_job_set_processor,
	oakengine_frame_channel_count, oakengine_frame_data, oakengine_frame_format,
	oakengine_frame_free, oakengine_frame_height, oakengine_frame_linesize_bytes,
	oakengine_frame_width, oakengine_lut_directory_at, oakengine_lut_directory_count,
	oakengine_lut_file_at, oakengine_lut_file_count, oakengine_lut_set_directories,
	oakengine_render_cache_set_display_color_processor, oakengine_render_cache_set_multicam_node,
	oakengine_render_manager_backend_to_string, oakengine_render_manager_requested_backend,
	oakengine_render_manager_set_aggressive_garbage_collection, oakengine_renderer_cancel,
	oakengine_renderer_create, oakengine_renderer_free, oakengine_renderer_last_error,
	oakengine_renderer_render_audio, oakengine_renderer_render_frame, oakengine_renderer_set_mode,
	OakColorTransformPod,
};
use oakengine::timeline::oakengine_sequence_new;

/// Serializes tests that touch process-global state: the render manager
/// singleton, the OCIO default config, the `$OCIO` env var and the facade
/// undo stack (`oakengine_project_new` clears it). Every test that takes
/// this lock leaves the manager shut down and the config/env untouched, so
/// the tests are order-independent.
static STATE_LOCK: Mutex<()> = Mutex::new(());

fn state_lock() -> MutexGuard<'static, ()> {
	STATE_LOCK.lock().unwrap_or_else(|e| e.into_inner())
}

/// Read a two-stage facade string into a Rust `String`.
unsafe fn read_buf(buf: &mut [c_char]) -> String {
	unsafe { std::ffi::CStr::from_ptr(buf.as_ptr()) }
		.to_string_lossy()
		.into_owned()
}

// ---------------------------------------------------------------------------
// Render manager / cacher (5 exports)
// ---------------------------------------------------------------------------

/// Manager state machine: STATE errors before init, legal paths after
/// `oakrender_manager_init`, STATE again after shutdown. The two cache
/// setters use their documented NULL-clears path here (real-handle paths
/// are exercised in `color_family_lifecycle`).
#[test]
fn render_manager_state_machine() {
	common::force_link();
	let _g = state_lock();

	// Clean slate: no manager may be up when this test runs.
	unsafe { oakrender::ffi::manager::oakrender_manager_shutdown() };
	assert_eq!(
		unsafe { oakrender::ffi::manager::oakrender_manager_available() },
		0
	);

	// Not initialized → the module's STATE error passes through.
	assert_eq!(
		unsafe { oakengine_render_manager_set_aggressive_garbage_collection(1) },
		-70002
	);
	assert_eq!(
		unsafe { oakengine_render_cache_set_display_color_processor(std::ptr::null_mut()) },
		-70002
	);
	assert_eq!(
		unsafe { oakengine_render_cache_set_multicam_node(std::ptr::null_mut()) },
		-70002
	);

	// Backend queries are stubs that ignore their arguments: requested
	// backend is 0 (k_open_gl); backend_to_string is always E_FAILED even
	// with garbage backend ids or NULL buffers.
	assert_eq!(unsafe { oakengine_render_manager_requested_backend() }, 0);
	let mut buf = [0 as c_char; 64];
	assert_eq!(
		unsafe { oakengine_render_manager_backend_to_string(0, std::ptr::null_mut(), 0) },
		-3
	);
	assert_eq!(
		unsafe { oakengine_render_manager_backend_to_string(2, std::ptr::null_mut(), 0) },
		-3
	);
	assert_eq!(
		unsafe { oakengine_render_manager_backend_to_string(-7, buf.as_mut_ptr(), 64) },
		-3
	);

	// Init through the real module C ABI; double init is a state error.
	assert_eq!(
		unsafe { oakrender::ffi::manager::oakrender_manager_init() },
		0
	);
	assert_eq!(
		unsafe { oakrender::ffi::manager::oakrender_manager_available() },
		1
	);
	assert_eq!(
		unsafe { oakrender::ffi::manager::oakrender_manager_init() },
		-70002
	);

	// Legal matrix for the aggressive-GC toggle: 0, 1 and any garbage
	// value are all accepted (nonzero = enabled).
	assert_eq!(
		unsafe { oakengine_render_manager_set_aggressive_garbage_collection(0) },
		0
	);
	assert_eq!(
		unsafe { oakengine_render_manager_set_aggressive_garbage_collection(1) },
		0
	);
	assert_eq!(
		unsafe { oakengine_render_manager_set_aggressive_garbage_collection(2) },
		0
	);
	assert_eq!(
		unsafe { oakengine_render_manager_set_aggressive_garbage_collection(-1) },
		0
	);

	// NULL-clears paths on the cacher.
	assert_eq!(
		unsafe { oakengine_render_cache_set_display_color_processor(std::ptr::null_mut()) },
		0
	);
	assert_eq!(
		unsafe { oakengine_render_cache_set_multicam_node(std::ptr::null_mut()) },
		0
	);

	// Shutdown → STATE again.
	unsafe { oakrender::ffi::manager::oakrender_manager_shutdown() };
	assert_eq!(
		unsafe { oakrender::ffi::manager::oakrender_manager_available() },
		0
	);
	assert_eq!(
		unsafe { oakengine_render_manager_set_aggressive_garbage_collection(1) },
		-70002
	);
}

// ---------------------------------------------------------------------------
// Renderer (7 exports)
// ---------------------------------------------------------------------------

/// Renderer lifecycle and illegal arguments. `render_frame` without a
/// render manager fails cleanly (NULL + last_error); the audio path is
/// unimplemented by the module and always fails the same way.
#[test]
fn renderer_lifecycle() {
	common::force_link();
	let _g = state_lock();
	unsafe { oakrender::ffi::manager::oakrender_manager_shutdown() }; // render_frame below must see no manager

	// NULL sequence → NULL renderer for every geometry combination.
	for (w, h, pf, num, den) in [
		(1920, 1080, 4, 30000, 1001),
		(0, 1080, 4, 30000, 1001),
		(1920, 0, 4, 30000, 1001),
		(1920, 1080, 4, 0, 1001),
		(1920, 1080, 4, 30000, 0),
		(-1, 1080, 4, 30000, 1001),
		(1920, -1, 4, 30000, 1001),
	] {
		let r = unsafe {
			oakengine_renderer_create(std::ptr::null_mut(), w, h, pf, num, den, std::ptr::null())
		};
		assert!(
			r.is_null(),
			"NULL seq (w={w} h={h} pf={pf} {num}/{den}) must give NULL"
		);
	}

	// NULL renderer calls are all safe.
	unsafe { oakengine_renderer_free(std::ptr::null_mut()) };
	unsafe { oakengine_renderer_cancel(std::ptr::null_mut()) };
	let mut err = [0 as c_char; 128];
	assert_eq!(
		unsafe { oakengine_renderer_last_error(std::ptr::null(), err.as_mut_ptr(), 128) },
		-1
	);
	assert_eq!(
		unsafe { oakengine_renderer_set_mode(std::ptr::null_mut(), 0) },
		-1
	);
	assert!(unsafe { oakengine_renderer_render_frame(std::ptr::null_mut(), 0) }.is_null());
	assert!(unsafe { oakengine_renderer_render_audio(std::ptr::null_mut(), 0, 10) }.is_null());

	// A real sequence (node family, no manager needed to create it).
	let project = unsafe { oakengine_project_create() };
	assert!(!project.is_null());
	assert_eq!(unsafe { oakengine_project_new(project) }, 0);
	let seq = unsafe { oakengine_sequence_new(project, c"it-render-seq".as_ptr()) };
	assert!(!seq.is_null());

	let r = unsafe { oakengine_renderer_create(seq, 1920, 1080, 4, 30000, 1001, std::ptr::null()) };
	assert!(!r.is_null());

	// Mode matrix: only 0 and 1 are legal; anything else is E_INVALID.
	assert_eq!(unsafe { oakengine_renderer_set_mode(r, 0) }, 0);
	assert_eq!(unsafe { oakengine_renderer_set_mode(r, 1) }, 0);
	assert_eq!(unsafe { oakengine_renderer_set_mode(r, 2) }, -1);
	assert_eq!(unsafe { oakengine_renderer_set_mode(r, -1) }, -1);
	assert_eq!(unsafe { oakengine_renderer_set_mode(r, 42) }, -1);

	// A fresh renderer reports an empty last_error.
	assert_eq!(
		unsafe { oakengine_renderer_last_error(r, err.as_mut_ptr(), 128) },
		0
	);

	// render_frame without a manager: clean NULL + last_error.
	assert!(unsafe { oakengine_renderer_render_frame(r, 0) }.is_null());
	let elen = unsafe { oakengine_renderer_last_error(r, err.as_mut_ptr(), 128) };
	assert!(elen > 0, "failed render must set last_error (got {elen})");

	// render_audio: the facade hands the module a NULL audio-params
	// pointer, so the ticket is never created (documented: the module's
	// samples path is unimplemented) → NULL + last_error.
	assert!(unsafe { oakengine_renderer_render_audio(r, 0, 10) }.is_null());
	let elen = unsafe { oakengine_renderer_last_error(r, err.as_mut_ptr(), 128) };
	assert!(
		elen > 0,
		"failed audio render must set last_error (got {elen})"
	);
	assert_eq!(
		unsafe { read_buf(&mut err) },
		"audio render ticket submission failed"
	);

	// Cancel is a documented no-op.
	unsafe { oakengine_renderer_cancel(r) };

	// Extreme timestamps must never crash: in debug builds the
	// i64::MAX * frame_rate_den overflow panics inside the facade guard and
	// yields NULL; in release it wraps and renders a frame. Either outcome
	// is acceptable — the point is that the call is robust.
	let f_extreme = unsafe { oakengine_renderer_render_frame(r, i64::MAX) };
	if !f_extreme.is_null() {
		unsafe { oakengine_frame_free(f_extreme) };
	}

	unsafe { oakengine_renderer_free(r) };
	unsafe { oakengine_project_free(project) };
}

/// End-to-end CPU render: with the render manager up and a real sequence,
/// `render_frame` produces a real F32 frame through the module's eval
/// pipeline. The renderer's geometry (width/height) is forwarded as
/// force_width/force_height, so the frame size follows the renderer, and
/// the pixel-format validation rejects codes outside the oakcore enum.
#[test]
fn renderer_render_frame_e2e() {
	common::force_link();
	let _g = state_lock();

	unsafe { oakrender::ffi::manager::oakrender_manager_shutdown() };
	assert_eq!(
		unsafe { oakrender::ffi::manager::oakrender_manager_init() },
		0
	);

	let base = unsafe { oakrender::ffi::cache::oakrender_debug_alive_count() };

	let project = unsafe { oakengine_project_create() };
	assert_eq!(unsafe { oakengine_project_new(project) }, 0);
	let seq = unsafe { oakengine_sequence_new(project, c"it-render-e2e".as_ptr()) };
	assert!(!seq.is_null());

	// 1920x1080 F32 (pixel format 4 = PixelFormat::F32).
	let r = unsafe { oakengine_renderer_create(seq, 1920, 1080, 4, 30000, 1001, std::ptr::null()) };
	assert!(!r.is_null());

	// Legal render: a real frame comes back and every accessor reads it.
	let f = unsafe { oakengine_renderer_render_frame(r, 0) };
	assert!(!f.is_null(), "render_frame must produce a frame");
	assert_eq!(unsafe { oakengine_frame_width(f) }, 1920);
	assert_eq!(unsafe { oakengine_frame_height(f) }, 1080);
	assert_eq!(unsafe { oakengine_frame_format(f) }, 4); // F32 pipeline format
	assert_eq!(unsafe { oakengine_frame_linesize_bytes(f) }, 1920 * 4 * 4);
	assert!(!unsafe { oakengine_frame_data(f) }.is_null());
	// channel_count has no crate accessor and reports 0 (documented).
	assert_eq!(unsafe { oakengine_frame_channel_count(f) }, 0);

	// The renderer's last_error is cleared after a successful render.
	assert_eq!(
		unsafe { oakengine_renderer_last_error(r, err_buf().as_mut_ptr(), 128) },
		0
	);

	// The produced frame is an oakrender-owned handle: alive count +1,
	// back to baseline after free.
	assert_eq!(
		unsafe { oakrender::ffi::cache::oakrender_debug_alive_count() },
		base + 1
	);
	unsafe { oakengine_frame_free(f) };
	assert_eq!(
		unsafe { oakrender::ffi::cache::oakrender_debug_alive_count() },
		base
	);

	// A second render at a positive timestamp works too.
	let f2 = unsafe { oakengine_renderer_render_frame(r, 30) };
	assert!(!f2.is_null());
	unsafe { oakengine_frame_free(f2) };

	// The renderer's output geometry is honored: the facade forwards the
	// boxed size as force_width/force_height, so a 640x360 renderer
	// produces a 640x360 frame.
	let r_small =
		unsafe { oakengine_renderer_create(seq, 640, 360, 0, 30000, 1001, std::ptr::null()) };
	assert!(!r_small.is_null());
	let f3 = unsafe { oakengine_renderer_render_frame(r_small, 0) };
	assert!(!f3.is_null());
	assert_eq!(unsafe { oakengine_frame_width(f3) }, 640);
	assert_eq!(unsafe { oakengine_frame_height(f3) }, 360);
	unsafe { oakengine_frame_free(f3) };
	unsafe { oakengine_renderer_free(r_small) };

	// The pixel-format validation in renderer_create rejects codes outside
	// the oakcore enum: garbage formats and Invalid (-1) yield NULL.
	let r_garbage_pf =
		unsafe { oakengine_renderer_create(seq, 64, 48, 99999, 30000, 1001, std::ptr::null()) };
	assert!(
		r_garbage_pf.is_null(),
		"renderer_create must reject pixel_format=99999"
	);
	let r_neg_pf =
		unsafe { oakengine_renderer_create(seq, 64, 48, -1, 30000, 1001, std::ptr::null()) };
	assert!(
		r_neg_pf.is_null(),
		"renderer_create must reject pixel_format=-1"
	);

	unsafe { oakengine_renderer_free(r) };
	unsafe { oakengine_project_free(project) };
	assert_eq!(
		unsafe { oakrender::ffi::cache::oakrender_debug_alive_count() },
		base
	);

	unsafe { oakrender::ffi::manager::oakrender_manager_shutdown() };
	assert_eq!(
		unsafe { oakrender::ffi::manager::oakrender_manager_available() },
		0
	);
}

/// A scratch error buffer helper.
fn err_buf() -> [c_char; 128] {
	[0 as c_char; 128]
}

// ---------------------------------------------------------------------------
// Frame accessors (7 exports)
// ---------------------------------------------------------------------------

/// All frame accessors are NULL-safe and report zero/NULL (the engine
/// contract: NULL is a no-op yielding zero results).
#[test]
fn frame_accessors_null_safe() {
	assert_eq!(unsafe { oakengine_frame_width(std::ptr::null()) }, 0);
	assert_eq!(unsafe { oakengine_frame_height(std::ptr::null()) }, 0);
	assert_eq!(unsafe { oakengine_frame_format(std::ptr::null()) }, 0);
	assert_eq!(
		unsafe { oakengine_frame_channel_count(std::ptr::null()) },
		0
	);
	assert_eq!(
		unsafe { oakengine_frame_linesize_bytes(std::ptr::null()) },
		0
	);
	assert!(unsafe { oakengine_frame_data(std::ptr::null()) }.is_null());
	unsafe { oakengine_frame_free(std::ptr::null_mut()) };
}

// ---------------------------------------------------------------------------
// Audio buffer accessors (5 exports, documented stubs)
// ---------------------------------------------------------------------------

/// The audio buffer has no crate backing: every accessor reports the
/// documented neutral value for NULL (and any) handles.
#[test]
fn audio_buffer_stubs() {
	let fake = 0x1 as *const OakEngineAudioBuffer;
	assert_eq!(unsafe { oakengine_audio_sample_rate(std::ptr::null()) }, 0);
	assert_eq!(unsafe { oakengine_audio_sample_rate(fake) }, 0);
	assert_eq!(
		unsafe { oakengine_audio_channel_count(std::ptr::null()) },
		0
	);
	assert_eq!(unsafe { oakengine_audio_channel_count(fake) }, 0);
	assert_eq!(unsafe { oakengine_audio_sample_count(std::ptr::null()) }, 0);
	assert_eq!(unsafe { oakengine_audio_sample_count(fake) }, 0);
	assert!(unsafe { oakengine_audio_data(std::ptr::null(), 0) }.is_null());
	assert!(unsafe { oakengine_audio_data(fake, 0) }.is_null());
	assert!(unsafe { oakengine_audio_data(fake, -1) }.is_null());
	assert!(unsafe { oakengine_audio_data(fake, 8) }.is_null());
	// The free is an empty no-op; NULL and garbage pointers are safe.
	unsafe { oakengine_audio_free(std::ptr::null_mut()) };
	unsafe { oakengine_audio_free(fake as *mut OakEngineAudioBuffer) };
}

// ---------------------------------------------------------------------------
// Color management (20 exports) + config handle (5) + processor (6)
// ---------------------------------------------------------------------------

/// The color family: documented stubs, the config-filename env-var paths,
/// the real OCIO-backed processor lifecycle, and the real-handle cache
/// setters. Runs under the state lock because the `$OCIO` env var, the
/// process-wide OCIO config and the render manager are global.
#[test]
fn color_family_lifecycle() {
	common::force_link();
	let _g = state_lock();
	unsafe { oakrender::ffi::manager::oakrender_manager_shutdown() }; // clean slate for the tail

	let mut buf = [0 as c_char; 256];

	// ---- last_error: empty until a processor creation fails ----------
	assert_eq!(
		unsafe { oakengine_color_last_error(buf.as_mut_ptr(), 64) },
		0
	);
	assert_eq!(
		unsafe { oakengine_color_last_error(std::ptr::null_mut(), 0) },
		0
	);

	// ---- color manager: documented stubs report their header values ---
	assert!(unsafe { oakengine_color_manager_from_project(std::ptr::null_mut()) }.is_null());
	assert_eq!(
		unsafe {
			oakengine_color_manager_set_config_filename(std::ptr::null_mut(), std::ptr::null())
		},
		-3
	);
	assert_eq!(
		unsafe {
			oakengine_color_manager_set_config_filename(std::ptr::null_mut(), c"x.ocio".as_ptr())
		},
		-3
	);
	assert_eq!(unsafe { oakengine_color_manager_colorspace_count() }, -3);
	assert_eq!(unsafe { oakengine_color_manager_display_count() }, -3);
	assert_eq!(unsafe { oakengine_color_manager_look_count() }, -3);
	assert_eq!(
		unsafe {
			oakengine_color_manager_colorspace_at(std::ptr::null(), 0, std::ptr::null_mut(), 0)
		},
		-3
	);
	assert_eq!(
		unsafe { oakengine_color_manager_display_at(std::ptr::null(), 0, std::ptr::null_mut(), 0) },
		-3
	);
	assert_eq!(
		unsafe { oakengine_color_manager_view_count(std::ptr::null(), std::ptr::null()) },
		-1
	);
	assert_eq!(
		unsafe {
			oakengine_color_manager_view_at(
				std::ptr::null(),
				std::ptr::null(),
				0,
				std::ptr::null_mut(),
				0,
			)
		},
		-3
	);
	assert_eq!(
		unsafe { oakengine_color_manager_look_at(std::ptr::null(), 0, std::ptr::null_mut(), 0) },
		-3
	);
	assert_eq!(
		unsafe {
			oakengine_color_manager_default_display(std::ptr::null(), std::ptr::null_mut(), 0)
		},
		-3
	);
	assert_eq!(
		unsafe {
			oakengine_color_manager_default_view(
				std::ptr::null(),
				std::ptr::null(),
				std::ptr::null_mut(),
				0,
			)
		},
		-3
	);
	assert_eq!(
		unsafe {
			oakengine_color_manager_default_input_color_space(
				std::ptr::null(),
				std::ptr::null_mut(),
				0,
			)
		},
		-3
	);
	assert_eq!(
		unsafe {
			oakengine_color_manager_set_default_input_color_space(
				std::ptr::null_mut(),
				std::ptr::null(),
			)
		},
		-3
	);
	assert_eq!(
		unsafe {
			oakengine_color_manager_reference_color_space(std::ptr::null(), std::ptr::null_mut(), 0)
		},
		-3
	);
	assert_eq!(
		unsafe {
			oakengine_color_manager_default_luma_coefs(std::ptr::null(), std::ptr::null_mut())
		},
		-3
	);
	let mut rgb = [0.0f64; 3];
	assert_eq!(
		unsafe { oakengine_color_manager_default_luma_coefs(std::ptr::null(), rgb.as_mut_ptr()) },
		-3
	);
	assert_eq!(
		unsafe {
			oakengine_color_manager_compliant_color_space(
				std::ptr::null(),
				std::ptr::null(),
				std::ptr::null_mut(),
				0,
			)
		},
		-3
	);
	assert_eq!(
		unsafe {
			oakengine_color_manager_compliant_transform(
				std::ptr::null(),
				std::ptr::null(),
				0,
				std::ptr::null_mut(),
				std::ptr::null_mut(),
				0,
				std::ptr::null_mut(),
				0,
				std::ptr::null_mut(),
				0,
			)
		},
		-3
	);

	// ---- standalone config handle: documented stubs -------------------
	assert!(unsafe { oakengine_color_config_load_default() }.is_null());
	assert!(unsafe { oakengine_color_config_load_file(std::ptr::null()) }.is_null());
	assert!(unsafe { oakengine_color_config_load_file(c"no/such/config.ocio".as_ptr()) }.is_null());
	unsafe { oakengine_color_config_free(std::ptr::null_mut()) };
	assert_eq!(
		unsafe { oakengine_color_config_colorspace_count(std::ptr::null()) },
		0
	);
	assert_eq!(
		unsafe {
			oakengine_color_config_colorspace_at(std::ptr::null(), 0, std::ptr::null_mut(), 0)
		},
		-3
	);

	// ---- processor id / transform-job stubs --------------------------
	assert_eq!(
		unsafe { oakengine_color_processor_id(std::ptr::null(), std::ptr::null_mut(), 0) },
		-3
	);
	assert_eq!(
		unsafe {
			oakengine_color_transform_job_set_processor(std::ptr::null_mut(), std::ptr::null())
		},
		-1
	);
	assert_eq!(
		unsafe {
			oakengine_color_transform_job_set_processor(0x1 as *mut c_void, std::ptr::null())
		},
		-3
	);

	// ---- config-filename: no $OCIO and no default config yet → STATE --
	unsafe { std::env::remove_var("OCIO") };
	assert_eq!(
		unsafe {
			oakengine_color_manager_get_config_filename(std::ptr::null(), std::ptr::null_mut(), 0)
		},
		-70002
	);

	// ---- config-filename: $OCIO set → the getter reports the path ----
	// (the file itself is never opened by the getter).
	let path = "/tmp/oakengine-it-render-ocio/config.ocio";
	unsafe { std::env::set_var("OCIO", path) };
	let rc = unsafe {
		oakengine_color_manager_get_config_filename(std::ptr::null(), std::ptr::null_mut(), 0)
	};
	assert_eq!(rc, path.len() as c_int);
	let rc = unsafe {
		oakengine_color_manager_get_config_filename(std::ptr::null(), buf.as_mut_ptr(), 256)
	};
	assert_eq!(rc, path.len() as c_int);
	assert_eq!(unsafe { read_buf(&mut buf) }, path);
	// A too-small buffer still reports the full length (two-stage).
	let mut small = [0 as c_char; 8];
	let rc = unsafe {
		oakengine_color_manager_get_config_filename(std::ptr::null(), small.as_mut_ptr(), 8)
	};
	assert_eq!(rc, path.len() as c_int);

	// ---- default config: real OCIO setup through the module C ABI ----
	unsafe { std::env::remove_var("OCIO") };
	let setup_rc =
		unsafe { oakrender::ffi::color::oakrender_color_manager_set_up_default_config() };
	assert!(
		setup_rc == 0 || setup_rc == -70003,
		"set_up_default_config rc={setup_rc} (0 = bundled OCIO, -70003 = stub build)"
	);
	let config_ok = setup_rc == 0;
	if config_ok {
		// The getter now reports the extracted config path (no $OCIO).
		let rc = unsafe {
			oakengine_color_manager_get_config_filename(std::ptr::null(), buf.as_mut_ptr(), 256)
		};
		assert!(rc > 0);
		let s = unsafe { read_buf(&mut buf) };
		assert!(s.ends_with("config.ocio"), "config path was {s:?}");
	}

	// ---- processor creation ------------------------------------------
	let dest = OakColorTransformPod {
		is_display: 0,
		output: c"ACEScg".as_ptr(),
		view: std::ptr::null(),
		look: std::ptr::null(),
	};
	// NULL input / NULL dest → NULL.
	assert!(unsafe {
		oakengine_color_processor_create(std::ptr::null(), std::ptr::null(), std::ptr::null(), 0)
	}
	.is_null());
	assert!(unsafe {
		oakengine_color_processor_create(std::ptr::null(), c"ACEScg".as_ptr(), std::ptr::null(), 0)
	}
	.is_null());
	// A NULL output name in the POD is substituted with an empty string by
	// the facade (not rejected); it follows the config-dependent path below.
	let dest_null_out = OakColorTransformPod {
		is_display: 0,
		output: std::ptr::null(),
		view: std::ptr::null(),
		look: std::ptr::null(),
	};
	// Empty input string → NULL (module rejects empty names).
	assert!(
		unsafe { oakengine_color_processor_create(std::ptr::null(), c"".as_ptr(), &dest, 0) }
			.is_null()
	);
	// Garbage directions → NULL.
	assert!(unsafe {
		oakengine_color_processor_create(std::ptr::null(), c"ACEScg".as_ptr(), &dest, 7)
	}
	.is_null());
	assert!(unsafe {
		oakengine_color_processor_create(std::ptr::null(), c"ACEScg".as_ptr(), &dest, -1)
	}
	.is_null());
	// The failed create set the thread-local color last_error.
	let elen = unsafe { oakengine_color_last_error(buf.as_mut_ptr(), 256) };
	assert!(
		elen > 0,
		"failed processor create must set last_error (got {elen})"
	);

	// NULL is invalid / convert_color on a NULL processor → E_INVALID.
	assert_eq!(
		unsafe { oakengine_color_processor_is_valid(std::ptr::null()) },
		0
	);
	let mut out_rgba = [0.0f64; 4];
	let in_rgba = [0.18f64, 0.18, 0.18, 1.0];
	assert_eq!(
		unsafe {
			oakengine_color_processor_convert_color(
				std::ptr::null(),
				in_rgba.as_ptr(),
				out_rgba.as_mut_ptr(),
			)
		},
		-1
	);
	unsafe { oakengine_color_processor_free(std::ptr::null_mut()) };

	// Real processor (only when a config exists; the module returns NULL
	// without one, which the engine contract allows). Capture the alive
	// baseline before creating the first oakrender-owned handle.
	let base = unsafe { oakrender::ffi::cache::oakrender_debug_alive_count() };
	let proc =
		unsafe { oakengine_color_processor_create(std::ptr::null(), c"ACEScg".as_ptr(), &dest, 0) };
	if config_ok {
		assert!(
			!proc.is_null(),
			"a default config must yield a processor handle"
		);
		assert_eq!(
			unsafe { oakrender::ffi::cache::oakrender_debug_alive_count() },
			base + 1
		);
		// A processor is either valid (real OCIO processor) or a
		// documented pass-through (is_valid 0); both are legal outcomes.
		let valid = unsafe { oakengine_color_processor_is_valid(proc) };
		assert!(valid == 0 || valid == 1);
		// Direction INVERSE is a legal path too.
		let proc_inv = unsafe {
			oakengine_color_processor_create(std::ptr::null(), c"ACEScg".as_ptr(), &dest, 1)
		};
		assert!(!proc_inv.is_null());
		// A NULL output name is substituted with an empty string by the
		// facade and still creates a processor.
		let proc_null_out = unsafe {
			oakengine_color_processor_create(
				std::ptr::null(),
				c"ACEScg".as_ptr(),
				&dest_null_out,
				0,
			)
		};
		assert!(!proc_null_out.is_null());
		// A display-transform destination is accepted as well.
		let dest_disp = OakColorTransformPod {
			is_display: 1,
			output: c"sRGB".as_ptr(),
			view: c"Filmic".as_ptr(),
			look: std::ptr::null(),
		};
		let proc_disp = unsafe {
			oakengine_color_processor_create(std::ptr::null(), c"ACEScg".as_ptr(), &dest_disp, 0)
		};
		assert!(!proc_disp.is_null());

		// convert_color: NULL in/out → E_INVALID; legal → OK. A
		// pass-through processor copies the input; a valid one converts
		// (outputs stay finite and alpha is preserved).
		assert_eq!(
			unsafe {
				oakengine_color_processor_convert_color(
					proc,
					std::ptr::null(),
					out_rgba.as_mut_ptr(),
				)
			},
			-1
		);
		assert_eq!(
			unsafe {
				oakengine_color_processor_convert_color(
					proc,
					in_rgba.as_ptr(),
					std::ptr::null_mut(),
				)
			},
			-1
		);
		out_rgba = [-1.0; 4];
		assert_eq!(
			unsafe {
				oakengine_color_processor_convert_color(
					proc,
					in_rgba.as_ptr(),
					out_rgba.as_mut_ptr(),
				)
			},
			0
		);
		if valid == 0 {
			assert_eq!(out_rgba, in_rgba, "pass-through processor copies the input");
		} else {
			assert!(out_rgba.iter().all(|v| v.is_finite()));
			assert!((out_rgba[3] - 1.0).abs() < 1e-6, "alpha preserved");
		}

		// The successful create cleared last_error.
		assert_eq!(
			unsafe { oakengine_color_last_error(buf.as_mut_ptr(), 256) },
			0
		);

		// Free contracts: each free releases the oakrender handle (alive
		// count back to the pre-create baseline).
		unsafe { oakengine_color_processor_free(proc_disp) };
		unsafe { oakengine_color_processor_free(proc_inv) };
		unsafe { oakengine_color_processor_free(proc_null_out) };
		unsafe { oakengine_color_processor_free(proc) };
		assert_eq!(
			unsafe { oakrender::ffi::cache::oakrender_debug_alive_count() },
			base
		);

		// The transform-job stub returns E_FAILED for a real processor too.
		assert_eq!(
			unsafe { oakengine_color_transform_job_set_processor(0x1 as *mut c_void, proc.cast()) },
			-3
		);

		// Real-handle cache setter paths (manager required; proc is dead
		// here, so create a fresh one for the cacher).
		let proc2 = unsafe {
			oakengine_color_processor_create(std::ptr::null(), c"ACEScg".as_ptr(), &dest, 0)
		};
		let node = unsafe {
			oakengine_node_factory_create_from_id(
				c"org.olivevideoeditor.Olive.solidgenerator".as_ptr(),
			)
		};
		assert!(!node.is_null());
		assert_eq!(
			unsafe { oakrender::ffi::manager::oakrender_manager_init() },
			0
		);
		assert_eq!(
			unsafe { oakengine_render_cache_set_display_color_processor(proc2.cast()) },
			0
		);
		assert_eq!(unsafe { oakengine_render_cache_set_multicam_node(node) }, 0);
		unsafe { oakrender::ffi::manager::oakrender_manager_shutdown() };
		unsafe { oakengine_color_processor_free(proc2) };
		unsafe { oakengine_node_free(node) };
	} else {
		// Stub-OCIO build: creation is a clean NULL, nothing to free.
		assert!(proc.is_null());
		assert_eq!(
			unsafe { oakrender::ffi::cache::oakrender_debug_alive_count() },
			0
		);
	}
}

// ---------------------------------------------------------------------------
// LUT library (5 exports, documented stubs)
// ---------------------------------------------------------------------------

/// The LUT directory/file library is facade-level over FileFunctions and
/// has no module backing: the documented neutral values hold for every
/// argument (garbage indices, NULL buffers, NULL/NULL directory lists).
#[test]
fn lut_library_stubs() {
	assert_eq!(unsafe { oakengine_lut_directory_count() }, 0);
	assert_eq!(
		unsafe { oakengine_lut_directory_at(0, std::ptr::null_mut(), 0) },
		-3
	);
	assert_eq!(
		unsafe { oakengine_lut_directory_at(-1, std::ptr::null_mut(), 0) },
		-3
	);
	assert_eq!(
		unsafe { oakengine_lut_directory_at(999, std::ptr::null_mut(), 0) },
		-3
	);
	assert_eq!(unsafe { oakengine_lut_file_count() }, 0);
	assert_eq!(
		unsafe { oakengine_lut_file_at(0, std::ptr::null_mut(), 0) },
		-3
	);
	assert_eq!(
		unsafe { oakengine_lut_file_at(-1, std::ptr::null_mut(), 0) },
		-3
	);
	assert_eq!(
		unsafe { oakengine_lut_set_directories(std::ptr::null(), 0) },
		-3
	);
	assert_eq!(
		unsafe { oakengine_lut_set_directories(std::ptr::null(), 1) },
		-3
	);
	assert_eq!(
		unsafe { oakengine_lut_set_directories(std::ptr::null(), -1) },
		-3
	);
	// A non-NULL list is never dereferenced by the stub.
	let fake_dirs = 0x1 as *const *const c_char;
	assert_eq!(unsafe { oakengine_lut_set_directories(fake_dirs, 1) }, -3);
}

// ---------------------------------------------------------------------------
// oakrender debug counter + free/destroy contracts
// ---------------------------------------------------------------------------

/// Module-level frame free contracts through the real oakrender C ABI:
/// free(NULL) no-op, alive count +1 on create and back to baseline on
/// free, and a double free is a safe no-op (the module nulls the handle
/// ctx after releasing). Facade box frees (`oakengine_frame_free` /
/// `oakengine_color_processor_free` / `oakengine_renderer_free`) are
/// NULL-safe and alive-balance-clean, verified in `renderer_render_frame_e2e`
/// and `color_family_lifecycle`.
#[test]
fn oakrender_debug_alive_and_double_free() {
	common::force_link();
	let _g = state_lock();

	let base = unsafe { oakrender::ffi::cache::oakrender_debug_alive_count() };

	// Create an oakrender-owned frame: +1.
	let mut f = unsafe { oakrender::ffi::renderer::oakrender_codec_frame_create() };
	assert!(!f.is_null());
	assert_eq!(
		unsafe { oakrender::ffi::cache::oakrender_debug_alive_count() },
		base + 1
	);

	// free(NULL) is a no-op.
	unsafe { oakrender::ffi::renderer::oakrender_codec_frame_free(std::ptr::null_mut()) };
	assert_eq!(
		unsafe { oakrender::ffi::cache::oakrender_debug_alive_count() },
		base + 1
	);

	// Free: back to baseline.
	unsafe { oakrender::ffi::renderer::oakrender_codec_frame_free(&mut f) };
	assert_eq!(
		unsafe { oakrender::ffi::cache::oakrender_debug_alive_count() },
		base
	);

	// Double free: the handle ctx was nulled, so it is a safe no-op.
	unsafe { oakrender::ffi::renderer::oakrender_codec_frame_free(&mut f) };
	assert_eq!(
		unsafe { oakrender::ffi::cache::oakrender_debug_alive_count() },
		base
	);
}
