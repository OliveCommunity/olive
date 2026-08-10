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

//! oakrender C ABI bridge: direct Rust calls into the `oakrender` crate.
//!
//! Single-lib unification (see `docs/zh/plans/riir/single-lib.md`): every
//! call below is a compile-time Rust call into `oakrender`'s `ffi` (the
//! `#[no_mangle]` exports stay in the dylib for the external C ABI;
//! internal callers bypass them). Handles cross as the shared
//! [`crate::handle::CHandle`]. Exceptions that keep an `extern "C"`
//! declaration (resolved at link time against the sibling crate in the
//! same dylib) are the host `oakcore_*` symbols and the encoding-params
//! C ABI POD crossings (the facade keeps its own POD mirrors there).

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

//! oakrender C ABI imports, mirroring the oakrender crate's exports
//! (`src/render/rust/src/ffi.rs`; headers `include/render/*.h`).

use std::ffi::{c_char, c_double, c_int, c_void};

use crate::handle::CHandle;

/// `include/render/ticket.h` — video render ticket params. Single-lib
/// unification: aliases the oakrender crate's POD (same `repr(C)`
/// layout; all handle fields are the shared [`CHandle`]).
pub type OakVideoTicketParams = oakrender::ffi::OakVideoTicketParams;


/// `include/render/renderer.h` — frame video-params POD returned by
/// `oakrender_codec_frame_get_params`. Single-lib unification: aliases
/// the oakrender crate's POD.
pub type OakRenderVideoParams = oakrender::ffi::OakRenderVideoParams;

/// Direct call into the `oakrender` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakrender_display_renderer_create_dynamic(backend_id: *const c_char) -> CHandle {
	unsafe { oakrender::ffi::renderer::oakrender_display_renderer_create_dynamic(backend_id) }
}

/// Direct call into the `oakrender` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakrender_display_renderer_create_opengl() -> CHandle {
	unsafe { oakrender::ffi::renderer::oakrender_display_renderer_create_opengl() }
}

/// Direct call into the `oakrender` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakrender_display_renderer_init(renderer: CHandle, gl_context: *mut c_void) -> c_int {
	unsafe { oakrender::ffi::renderer::oakrender_display_renderer_init(renderer, gl_context) }
}

/// Direct call into the `oakrender` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakrender_display_renderer_destroy(renderer: *mut CHandle) {
	unsafe { oakrender::ffi::renderer::oakrender_display_renderer_destroy(renderer) }
}

/// Direct call into the `oakrender` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakrender_display_renderer_is_open_gl(renderer: CHandle) -> c_int {
	unsafe { oakrender::ffi::renderer::oakrender_display_renderer_is_open_gl(renderer) }
}

/// Direct call into the `oakrender` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakrender_display_renderer_is_vulkan(renderer: CHandle) -> c_int {
	unsafe { oakrender::ffi::renderer::oakrender_display_renderer_is_vulkan(renderer) }
}

/// Direct call into the `oakrender` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakrender_manager_set_aggressive_gc(enabled: c_int) -> c_int {
	unsafe { oakrender::ffi::ticket::oakrender_manager_set_aggressive_gc(enabled) }
}

/// Direct call into the `oakrender` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakrender_set_cacher_multicam(multicam_or_null: CHandle) -> c_int {
	unsafe { oakrender::ffi::manager::oakrender_set_cacher_multicam(multicam_or_null) }
}

/// Direct call into the `oakrender` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakrender_set_display_color_processor(p_or_null: CHandle) -> c_int {
	unsafe { oakrender::ffi::manager::oakrender_set_display_color_processor(p_or_null) }
}

/// Direct call into the `oakrender` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakrender_ticket_render_frame(
		params: *const OakVideoTicketParams,
		cb: Option<unsafe extern "C" fn(CHandle, *mut c_void)>,
		userdata: *mut c_void,
	) -> CHandle {
	unsafe { oakrender::ffi::ticket::oakrender_ticket_render_frame(params, cb, userdata) }
}

/// Direct call into the `oakrender` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakrender_ticket_render_audio(
		output_node: CHandle,
		in_num: i64,
		in_den: i64,
		out_num: i64,
		out_den: i64,
		params: *const c_void,
		mode: c_int,
		cb: Option<unsafe extern "C" fn(CHandle, *mut c_void)>,
		userdata: *mut c_void,
	) -> CHandle {
	unsafe {
		oakrender::ffi::ticket::oakrender_ticket_render_audio(
			output_node, in_num, in_den, out_num, out_den, params as *const CHandle, mode, cb,
			userdata,
		)
	}
}

/// Direct call into the `oakrender` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakrender_ticket_wait(ticket: CHandle) -> c_int {
	unsafe { oakrender::ffi::ticket::oakrender_ticket_wait(ticket) }
}

/// Direct call into the `oakrender` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakrender_ticket_cancel(ticket: CHandle) -> c_int {
	unsafe { oakrender::ffi::ticket::oakrender_ticket_cancel(ticket) }
}

/// Direct call into the `oakrender` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakrender_ticket_get_frame(ticket: CHandle, out: *mut CHandle) -> c_int {
	unsafe { oakrender::ffi::ticket::oakrender_ticket_get_frame(ticket, out) }
}

/// Direct call into the `oakrender` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakrender_ticket_get_samples(ticket: CHandle, out: *mut *mut c_void) -> c_int {
	unsafe { oakrender::ffi::ticket::oakrender_ticket_get_samples(ticket, out) }
}

/// Direct call into the `oakrender` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakrender_ticket_free(ticket: *mut CHandle) {
	unsafe { oakrender::ffi::ticket::oakrender_ticket_free(ticket) }
}

/// Direct call into the `oakrender` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakrender_codec_frame_create() -> CHandle {
	unsafe { oakrender::ffi::renderer::oakrender_codec_frame_create() }
}

/// Direct call into the `oakrender` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakrender_codec_frame_retain(frame: CHandle) -> CHandle {
	unsafe { oakrender::ffi::renderer::oakrender_codec_frame_retain(frame) }
}

/// Direct call into the `oakrender` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakrender_codec_frame_free(frame: *mut CHandle) {
	unsafe { oakrender::ffi::renderer::oakrender_codec_frame_free(frame) }
}

/// Direct call into the `oakrender` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakrender_codec_frame_width(frame: CHandle) -> c_int {
	unsafe { oakrender::ffi::renderer::oakrender_codec_frame_width(frame) }
}

/// Direct call into the `oakrender` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakrender_codec_frame_height(frame: CHandle) -> c_int {
	unsafe { oakrender::ffi::renderer::oakrender_codec_frame_height(frame) }
}

/// Direct call into the `oakrender` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakrender_codec_frame_linesize_bytes(frame: CHandle) -> c_int {
	unsafe { oakrender::ffi::renderer::oakrender_codec_frame_linesize_bytes(frame) }
}

/// Direct call into the `oakrender` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakrender_codec_frame_data(frame: CHandle) -> *mut c_void {
	unsafe { oakrender::ffi::renderer::oakrender_codec_frame_data(frame) }
}

/// Direct call into the `oakrender` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakrender_codec_frame_const_data(frame: CHandle) -> *const c_void {
	unsafe { oakrender::ffi::renderer::oakrender_codec_frame_const_data(frame) }
}

/// Direct call into the `oakrender` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakrender_codec_frame_is_allocated(frame: CHandle) -> c_int {
	unsafe { oakrender::ffi::renderer::oakrender_codec_frame_is_allocated(frame) }
}

/// Direct call into the `oakrender` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakrender_codec_frame_get_params(frame: CHandle, out: *mut OakRenderVideoParams) -> c_int {
	unsafe { oakrender::ffi::renderer::oakrender_codec_frame_get_params(frame, out) }
}

/// Direct call into the `oakrender` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakrender_color_processor_create(
		src_space: *const c_char,
		dst_transform: *const c_char,
		direction: c_int,
	) -> CHandle {
	unsafe { oakrender::ffi::color::oakrender_color_processor_create(src_space, dst_transform, direction) }
}

/// Direct call into the `oakrender` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakrender_color_processor_free(processor: *mut CHandle) {
	unsafe { oakrender::ffi::color::oakrender_color_processor_free(processor) }
}

/// Direct call into the `oakrender` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakrender_color_processor_is_valid(processor: CHandle) -> c_int {
	unsafe { oakrender::ffi::color::oakrender_color_processor_is_valid(processor) }
}

/// Direct call into the `oakrender` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakrender_color_processor_create_transform(
		manager: CHandle,
		input: *const c_char,
		dest: CHandle,
		direction: c_int,
	) -> CHandle {
	unsafe { oakrender::ffi::color::oakrender_color_processor_create_transform(manager, input, dest, direction) }
}

/// Direct call into the `oakrender` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakrender_color_processor_convert(
		processor: CHandle,
		ir: c_double,
		ig: c_double,
		ib: c_double,
		ia: c_double,
		out_r: *mut c_double,
		out_g: *mut c_double,
		out_b: *mut c_double,
		out_a: *mut c_double,
	) -> c_int {
	unsafe { oakrender::ffi::color::oakrender_color_processor_convert(processor, ir, ig, ib, ia, out_r, out_g, out_b, out_a) }
}

/// Direct call into the `oakrender` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakrender_color_manager_set_up_default_config() -> c_int {
	unsafe { oakrender::ffi::color::oakrender_color_manager_set_up_default_config() }
}

/// Direct call into the `oakrender` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakrender_color_manager_get_config(buf: *mut c_char, n: c_int) -> c_int {
	unsafe { oakrender::ffi::color::oakrender_color_manager_get_config(buf, n) }
}

/// Direct call into the `oakrender` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakrender_lut_is_supported_extension(extension: *const c_char) -> c_int {
	unsafe { oakrender::ffi::color::oakrender_lut_is_supported_extension(extension) }
}

/// Direct call into the `oakrender` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakrender_lut_supported_extensions_count() -> c_int {
	unsafe { oakrender::ffi::color::oakrender_lut_supported_extensions_count() }
}

/// Direct call into the `oakrender` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakrender_lut_supported_extension_at(i: c_int, buf: *mut c_char, n: c_int) -> c_int {
	unsafe { oakrender::ffi::color::oakrender_lut_supported_extension_at(i, buf, n) }
}

