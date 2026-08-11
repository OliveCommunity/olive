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

//! oakrender C ABI imports (display textures, renderers, cancel atoms).
//!
//! The OIIO/FFmpeg decoders push frames to a `DisplayTexture` and poll a
//! `CancelAtom`; both are oakrender refcounted handles with the standard
//! `{ctx, addref, release, abi_version}` layout. `oakrender_video_params`
//! is a flattened POD the decoders construct to describe the frame.

use std::ffi::{c_char, c_int, c_void};

use crate::handle::CHandle;

/// `OakRenderTexture` — refcounted GPU texture handle.
pub type OakRenderTexture = CHandle;

/// `OakCancelAtom` — refcounted cancellation atom handle.
pub type OakCancelAtom = CHandle;

/// `OakRenderRenderer` — refcounted display-renderer handle.
pub type OakRenderRenderer = CHandle;

/// `OakCodecFrame` — refcounted CPU-frame handle shared with oakrender.
pub type OakCodecFrame = CHandle;

// Refcounted opaque handles; thread-safe in the C library.

/// `oakrender_video_params` — flattened POD of `olive::VideoParams`
/// passed into oakrender; see `include/render/renderer.h`.
#[repr(C)]
pub struct oakrender_video_params {
	/// Width in pixels.
	pub width: c_int,
	/// Height in pixels.
	pub height: c_int,
	/// Frame-duration numerator (e.g. 1001/30000 s).
	pub time_base_num: c_int,
	/// Frame-duration denominator.
	pub time_base_den: c_int,
	/// `olive::PixelFormat::Format`.
	pub format: c_int,
	/// Pixel-aspect numerator.
	pub pixel_aspect_num: c_int,
	/// Pixel-aspect denominator.
	pub pixel_aspect_den: c_int,
	/// `olive::VideoParams::Interlacing`.
	pub interlacing: c_int,
	/// `olive::VideoParams::ColorRange`.
	pub color_range: c_int,
	/// Preview-resolution divider (1 = full).
	pub divider: c_int,
	/// `olive::VideoParams::Type` (0 = video).
	pub video_type: c_int,
	/// 0/1 premultiplied alpha.
	pub premultiplied_alpha: c_int,
}

extern "C" {
	/// `oakrender_cancelatom_init`.
	pub fn oakrender_cancelatom_init() -> OakCancelAtom;
	/// `oakrender_cancelatom_free` (NULL/empty no-op).
	pub fn oakrender_cancelatom_free(atom: *mut OakCancelAtom);
	/// `oakrender_cancelatom_is_cancelled`.
	pub fn oakrender_cancelatom_is_cancelled(atom: OakCancelAtom) -> c_int;
	/// `oakrender_cancelatom_heard_cancel`.
	pub fn oakrender_cancelatom_heard_cancel(atom: OakCancelAtom) -> c_int;
	/// `oakrender_cancelatom_cancel`.
	pub fn oakrender_cancelatom_cancel(atom: OakCancelAtom);
	/// `oakrender_cancelatom_get_native`.
	pub fn oakrender_cancelatom_get_native(atom: OakCancelAtom) -> *mut c_void;
	/// `oakrender_display_texture_create`.
	pub fn oakrender_display_texture_create(
		renderer: OakRenderRenderer,
		params: *const oakrender_video_params,
		data: *const c_void,
		linesize: c_int,
	) -> OakRenderTexture;
	/// `oakrender_display_texture_retain`.
	pub fn oakrender_display_texture_retain(texture: OakRenderTexture) -> OakRenderTexture;
	/// `oakrender_display_texture_free` (NULL/empty no-op).
	pub fn oakrender_display_texture_free(texture: *mut OakRenderTexture);
	/// `oakrender_display_texture_upload`.
	pub fn oakrender_display_texture_upload(texture: OakRenderTexture) -> c_int;
	/// `oakrender_display_texture_download`.
	pub fn oakrender_display_texture_download(
		texture: OakRenderTexture,
		pixels: *mut c_void,
		linesize: c_int,
	) -> c_int;
	/// `oakrender_display_texture_get_params`.
	pub fn oakrender_display_texture_get_params(
		texture: OakRenderTexture,
		out: *mut oakrender_video_params,
	) -> c_int;
	/// `oakrender_display_texture_id`.
	pub fn oakrender_display_texture_id(texture: OakRenderTexture) -> c_int;
	/// `oakrender_display_texture_is_dummy`.
	pub fn oakrender_display_texture_is_dummy(texture: OakRenderTexture) -> c_int;
	/// `oakrender_display_texture_get_frame` (two-stage frame access).
	pub fn oakrender_display_texture_get_frame(
		texture: OakRenderTexture,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int;
	/// `oakrender_codec_frame_width`.
	pub fn oakrender_codec_frame_width(frame: OakCodecFrame) -> c_int;
	/// `oakrender_codec_frame_height`.
	pub fn oakrender_codec_frame_height(frame: OakCodecFrame) -> c_int;
	/// `oakrender_codec_frame_fb_format`.
	pub fn oakrender_codec_frame_fb_format(frame: OakCodecFrame) -> c_int;
	/// `oakrender_codec_frame_free` (NULL/empty no-op).
	pub fn oakrender_codec_frame_free(frame: *mut OakCodecFrame);
	/// `oakrender_codec_frame_allocate`.
	pub fn oakrender_codec_frame_allocate(frame: OakCodecFrame) -> c_int;
	/// `oakrender_codec_frame_linesize_bytes`.
	pub fn oakrender_codec_frame_linesize_bytes(frame: OakCodecFrame) -> c_int;
	/// `oakrender_codec_frame_is_allocated`.
	pub fn oakrender_codec_frame_is_allocated(frame: OakCodecFrame) -> c_int;
	/// `oakrender_display_renderer_blit_color_managed`.
	pub fn oakrender_display_renderer_blit_color_managed(
		renderer: OakRenderRenderer,
		job: *const c_void,
		dst_texture: OakRenderTexture,
		params: *const oakrender_video_params,
	) -> c_int;
}
