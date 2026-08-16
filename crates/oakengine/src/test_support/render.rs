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

//! Smoke tests for the render family (`engine/include/oakengine/
//! {renderer,color,lut}.h`). The render manager is not initialized in
//! tests, so the manager/cacher families exercise the module's STATE
//! error path and the renderer/color families exercise the NULL/invalid
//! argument paths (real rendering needs the deferred node family plus an
//! initialized render manager).

use super::common;

use std::ffi::{c_char, c_double};

use crate::render::{
	oakengine_color_last_error, oakengine_color_manager_get_config_filename,
	oakengine_color_processor_convert_color, oakengine_color_processor_create,
	oakengine_color_processor_free, oakengine_color_processor_is_valid,
	oakengine_frame_channel_count, oakengine_frame_data, oakengine_frame_free,
	oakengine_frame_height, oakengine_frame_width, oakengine_lut_directory_count,
	oakengine_lut_set_directories, oakengine_render_cache_set_display_color_processor,
	oakengine_render_cache_set_multicam_node, oakengine_render_manager_requested_backend,
	oakengine_render_manager_set_aggressive_garbage_collection, oakengine_renderer_create,
	oakengine_renderer_free, oakengine_renderer_last_error, oakengine_renderer_set_mode,
	OakColorTransformPod,
};

/// Render manager state without initialization: the module reports its
/// STATE error, passed through untranslated (-70002).
#[test]
fn render_manager_not_initialized() {
	assert_eq!(
		unsafe { oakengine_render_manager_set_aggressive_garbage_collection(1) },
		-70002
	);
	// Cache setters with NULL handles → same module STATE.
	assert_eq!(
		unsafe { oakengine_render_cache_set_display_color_processor(std::ptr::null_mut()) },
		-70002
	);
	assert_eq!(
		unsafe { oakengine_render_cache_set_multicam_node(std::ptr::null_mut()) },
		-70002
	);
	// Without a manager the requested backend is -1 (no manager up).
	assert_eq!(unsafe { oakengine_render_manager_requested_backend() }, -1);
}

/// Renderer lifecycle: NULL sequence is rejected; mode validation.
#[test]
fn renderer_lifecycle() {
	// NULL seq → NULL renderer.
	let r = unsafe {
		oakengine_renderer_create(
			std::ptr::null_mut(),
			1920,
			1080,
			4,
			30000,
			1001,
			std::ptr::null(),
		)
	};
	assert!(r.is_null());

	// NULL free / last_error are safe.
	unsafe { oakengine_renderer_free(std::ptr::null_mut()) };
	let mut buf = [0 as c_char; 64];
	assert_eq!(
		unsafe { oakengine_renderer_last_error(std::ptr::null(), buf.as_mut_ptr(), 64) },
		-1
	);
	assert_eq!(
		unsafe { oakengine_renderer_set_mode(std::ptr::null_mut(), 0) },
		-1
	);
}

/// Frame accessors on NULL / empty handles report zero/NULL safely.
#[test]
fn frame_accessors_null_safe() {
	assert_eq!(unsafe { oakengine_frame_width(std::ptr::null()) }, 0);
	assert_eq!(unsafe { oakengine_frame_height(std::ptr::null()) }, 0);
	assert_eq!(
		unsafe { oakengine_frame_channel_count(std::ptr::null()) },
		0
	);
	assert!(unsafe { oakengine_frame_data(std::ptr::null()) }.is_null());
	unsafe { oakengine_frame_free(std::ptr::null_mut()) };
}

/// Color processor: NULL input is rejected; a valid-argument call either
/// returns a handle (possibly invalid — OCIO may be a stub bridge) or
/// NULL; freeing is safe either way.
#[test]
fn color_processor_lifecycle() {
	// NULL input → NULL.
	let p = unsafe {
		oakengine_color_processor_create(std::ptr::null(), std::ptr::null(), std::ptr::null(), 0)
	};
	assert!(p.is_null());

	// Valid arguments: the engine contract allows NULL (OCIO unavailable)
	// or a handle whose is_valid may be 0.
	let mut dest = OakColorTransformPod {
		is_display: 0,
		output: c"ACEScg".as_ptr(),
		view: std::ptr::null(),
		look: std::ptr::null(),
	};
	let p = unsafe {
		oakengine_color_processor_create(
			std::ptr::null(),
			c"Linear Rec.709 (sRGB)".as_ptr(),
			&dest,
			0,
		)
	};
	if !p.is_null() {
		let valid = unsafe { oakengine_color_processor_is_valid(p) };
		assert!(valid == 0 || valid == 1);
		unsafe { oakengine_color_processor_free(p) };
	}
	// NULL free is a no-op.
	unsafe { oakengine_color_processor_free(std::ptr::null_mut()) };

	// convert_color with a NULL processor → E_INVALID.
	let mut out_rgba = [0.0_f64; 4];
	let in_rgba = [0.5_f64, 0.5, 0.5, 1.0];
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
	let _ = dest;
}

/// Color manager config path: without a configured manager the module
/// reports STATE; the last-error string starts empty.
#[test]
fn color_manager_and_error() {
	let mut buf = [0 as c_char; 64];
	let rc = unsafe {
		oakengine_color_manager_get_config_filename(std::ptr::null(), buf.as_mut_ptr(), 64)
	};
	assert_eq!(rc, -70002);

	let len = unsafe { oakengine_color_last_error(buf.as_mut_ptr(), 64) };
	assert_eq!(len, 0);
}

/// LUT library stubs report the documented neutral values.
#[test]
fn lut_library_stubs() {
	assert_eq!(unsafe { oakengine_lut_directory_count() }, 0);
	assert_eq!(
		unsafe { oakengine_lut_set_directories(std::ptr::null(), 0) },
		-3
	);
}

// repro: render_audio on an empty sequence (playback tick on an empty timeline).
#[test]
fn render_audio_empty_sequence_no_crash() {
	super::common::force_link();
	unsafe {
		assert_eq!(crate::render::oakengine_render_manager_init(), 0);
		let project = crate::node::oakengine_project_create();
		assert!(!project.is_null());
		assert_eq!(crate::node::oakengine_project_new(project), 0);
		let name = std::ffi::CString::new("s").unwrap();
		let seq = crate::timeline::oakengine_sequence_new(project, name.as_ptr());
		assert!(!seq.is_null());
		assert_eq!(crate::timeline::oakengine_sequence_add_track(seq, 1), 0); // audio track, no clips
		let r = crate::render::oakengine_renderer_create(seq, 64, 64, 4, 25, 1, std::ptr::null());
		assert!(!r.is_null());
		for i in 0..5 {
			let buf = crate::render::oakengine_renderer_render_audio(r, i * 2048, 2048);
			if !buf.is_null() {
				crate::render::oakengine_audio_free(buf);
			}
		}
		crate::render::oakengine_renderer_free(r);
		crate::node::oakengine_project_free(project);
	}
}
