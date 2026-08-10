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

/// `include/render/ticket.h` — video render ticket params mirror. All
/// handle fields are [`CHandle`] (borrowed unless noted).
#[repr(C)]
#[derive(Clone, Copy)]
pub struct OakVideoTicketParams {
	/// Connected texture output node (borrowed).
	pub output_node: CHandle,
	/// By-value oakcommon video-params handle.
	pub video_params: CHandle,
	/// Borrowed oakcore audio-params handle, may be null.
	pub audio_params: *const c_void,
	/// Frame timestamp numerator.
	pub time_num: i64,
	/// Frame timestamp denominator.
	pub time_den: i64,
	/// Borrowed color manager, empty ctx = null.
	pub color_manager: CHandle,
	/// RenderMode::Mode as int.
	pub mode: c_int,
	/// 0/0 = off.
	pub force_width: c_int,
	/// 0/0 = off.
	pub force_height: c_int,
	/// Used when has_force_matrix != 0.
	pub force_matrix: [c_double; 16],
	/// 0/1.
	pub has_force_matrix: c_int,
	/// PixelFormat as int, -1 = off.
	pub force_format: c_int,
	/// 0 = off.
	pub force_channel_count: c_int,
	/// Borrowed; empty ctx = none.
	pub force_color_output: CHandle,
	/// By value; empty ctx = default.
	pub force_color_transform: CHandle,
	/// Borrowed frame cache; empty ctx = none.
	pub cache: CHandle,
}

/// `include/render/renderer.h` — frame video-params POD returned by
/// `oakrender_codec_frame_get_params`.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct OakRenderVideoParams {
	/// Width.
	pub width: c_int,
	/// Height.
	pub height: c_int,
	/// Frame duration numerator.
	pub time_base_num: c_int,
	/// Frame duration denominator.
	pub time_base_den: c_int,
	/// PixelFormat as int.
	pub format: c_int,
	/// Pixel aspect numerator.
	pub pixel_aspect_num: c_int,
	/// Pixel aspect denominator.
	pub pixel_aspect_den: c_int,
	/// Interlacing as int.
	pub interlacing: c_int,
	/// Color range as int.
	pub color_range: c_int,
	/// Preview divider.
	pub divider: c_int,
	/// Video type.
	pub video_type: c_int,
	/// Premultiplied alpha 0/1.
	pub premultiplied_alpha: c_int,
}

extern "C" {
	// ---- display renderer --------------------------------------------------
	/// `oakrender_display_renderer_create_dynamic` — named backend
	/// ("opengl", "vulkan", "metal", "auto", ...).
	pub fn oakrender_display_renderer_create_dynamic(backend_id: *const c_char) -> CHandle;
	/// `oakrender_display_renderer_create_opengl` — direct OpenGL renderer.
	pub fn oakrender_display_renderer_create_opengl() -> CHandle;
	/// `oakrender_display_renderer_init` — NULL gl_context uses the
	/// backend's default device/context path.
	pub fn oakrender_display_renderer_init(renderer: CHandle, gl_context: *mut c_void) -> c_int;
	/// `oakrender_display_renderer_destroy` — NULL/empty no-op.
	pub fn oakrender_display_renderer_destroy(renderer: *mut CHandle);
	/// `oakrender_display_renderer_is_open_gl` — 1/0.
	pub fn oakrender_display_renderer_is_open_gl(renderer: CHandle) -> c_int;
	/// `oakrender_display_renderer_is_vulkan` — 1/0.
	pub fn oakrender_display_renderer_is_vulkan(renderer: CHandle) -> c_int;

	// ---- render manager -----------------------------------------------------
	/// `oakrender_manager_set_aggressive_gc`.
	pub fn oakrender_manager_set_aggressive_gc(enabled: c_int) -> c_int;
	/// `oakrender_set_cacher_multicam` — NULL clears.
	pub fn oakrender_set_cacher_multicam(multicam_or_null: CHandle) -> c_int;
	/// `oakrender_set_display_color_processor` — NULL clears.
	pub fn oakrender_set_display_color_processor(p_or_null: CHandle) -> c_int;

	// ---- render tickets -----------------------------------------------------
	/// `oakrender_ticket_render_frame`.
	pub fn oakrender_ticket_render_frame(
		params: *const OakVideoTicketParams,
		cb: Option<unsafe extern "C" fn(CHandle, *mut c_void)>,
		userdata: *mut c_void,
	) -> CHandle;
	/// `oakrender_ticket_render_audio`.
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
	) -> CHandle;
	/// `oakrender_ticket_wait` — block until finished.
	pub fn oakrender_ticket_wait(ticket: CHandle) -> c_int;
	/// `oakrender_ticket_cancel`.
	pub fn oakrender_ticket_cancel(ticket: CHandle) -> c_int;
	/// `oakrender_ticket_get_frame` — `*out` receives an owned copy.
	pub fn oakrender_ticket_get_frame(ticket: CHandle, out: *mut CHandle) -> c_int;
	/// `oakrender_ticket_get_samples` — audio not implemented in the crate.
	pub fn oakrender_ticket_get_samples(ticket: CHandle, out: *mut *mut c_void) -> c_int;
	/// `oakrender_ticket_free` — NULL/empty no-op.
	pub fn oakrender_ticket_free(ticket: *mut CHandle);

	// ---- codec frame --------------------------------------------------------
	/// `oakrender_codec_frame_create` — refcount 1.
	pub fn oakrender_codec_frame_create() -> CHandle;
	/// `oakrender_codec_frame_retain` — addref, return copy.
	pub fn oakrender_codec_frame_retain(frame: CHandle) -> CHandle;
	/// `oakrender_codec_frame_free` — NULL/empty no-op.
	pub fn oakrender_codec_frame_free(frame: *mut CHandle);
	/// `oakrender_codec_frame_width`.
	pub fn oakrender_codec_frame_width(frame: CHandle) -> c_int;
	/// `oakrender_codec_frame_height`.
	pub fn oakrender_codec_frame_height(frame: CHandle) -> c_int;
	/// `oakrender_codec_frame_linesize_bytes`.
	pub fn oakrender_codec_frame_linesize_bytes(frame: CHandle) -> c_int;
	/// `oakrender_codec_frame_data` — borrowed.
	pub fn oakrender_codec_frame_data(frame: CHandle) -> *mut c_void;
	/// `oakrender_codec_frame_const_data` — borrowed.
	pub fn oakrender_codec_frame_const_data(frame: CHandle) -> *const c_void;
	/// `oakrender_codec_frame_is_allocated`.
	pub fn oakrender_codec_frame_is_allocated(frame: CHandle) -> c_int;
	/// `oakrender_codec_frame_get_params` — fills the params POD.
	pub fn oakrender_codec_frame_get_params(frame: CHandle, out: *mut OakRenderVideoParams) -> c_int;

	// ---- color processor ----------------------------------------------------
	/// `oakrender_color_processor_create`.
	pub fn oakrender_color_processor_create(
		src_space: *const c_char,
		dst_transform: *const c_char,
		direction: c_int,
	) -> CHandle;
	/// `oakrender_color_processor_free` — NULL/empty no-op.
	pub fn oakrender_color_processor_free(processor: *mut CHandle);
	/// `oakrender_color_processor_is_valid`.
	pub fn oakrender_color_processor_is_valid(processor: CHandle) -> c_int;
	/// `oakrender_color_processor_create_transform` — manager + input +
	/// oakcommon colortransform handle + direction.
	pub fn oakrender_color_processor_create_transform(
		manager: CHandle,
		input: *const c_char,
		dest: CHandle,
		direction: c_int,
	) -> CHandle;
	/// `oakrender_color_processor_convert` — single RGBA color.
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
	) -> c_int;

	// ---- color manager ------------------------------------------------------
	/// `oakrender_color_manager_set_up_default_config`.
	pub fn oakrender_color_manager_set_up_default_config() -> c_int;
	/// `oakrender_color_manager_get_config` (two-stage).
	pub fn oakrender_color_manager_get_config(buf: *mut c_char, n: c_int) -> c_int;

	// ---- LUT extension enumeration ------------------------------------------
	/// `oakrender_lut_is_supported_extension`.
	pub fn oakrender_lut_is_supported_extension(extension: *const c_char) -> c_int;
	/// `oakrender_lut_supported_extensions_count`.
	pub fn oakrender_lut_supported_extensions_count() -> c_int;
	/// `oakrender_lut_supported_extension_at` (two-stage).
	pub fn oakrender_lut_supported_extension_at(i: c_int, buf: *mut c_char, n: c_int) -> c_int;
}
