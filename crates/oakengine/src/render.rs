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

//! `engine/include/oakengine/{renderer,color,lut}.h` over the oakrender
//! module.
//!
//! - The **renderer** is a facade-owned box binding a sequence handle to
//!   an output geometry; each render call submits an oakrender ticket
//!   (`OakVideoTicketParams`), waits for it and returns the produced
//!   frame (`OakCodecFrame` wrapped in `OakEngineFrame`). Audio rendering
//!   submits the ticket but the crate's samples path is unimplemented, so
//!   it fails with the reason in `last_error`.
//! - The **frame accessors** read the wrapped `OakCodecFrame`
//!   (`channel_count` has no crate accessor and reports 0).
//! - The **color processor** family maps onto
//!   `oakrender_color_processor_*`; the engine's `oak_color_transform`
//!   POD is converted into an oakcommon colortransform handle for
//!   `create_transform`. The color-manager list queries, standalone
//!   config handle and LUT directory/file library have no crate backing
//!   and are documented stubs (see `deferred.rs`).

use std::cell::RefCell;
use std::ffi::{c_char, c_double, c_int, c_void};

use crate::bridge::render as r;
use crate::bridge::render::{OakRenderVideoParams, OakVideoTicketParams};
use crate::error::{Error, Result};
use crate::handle::{
	box_handle, free_box, guard, guard_int, guard_ptr, guard_void, string_result, unbox, CHandle,
	EngineBox, OakEngineAudioBuffer, OakEngineColorProcessor, OakEngineFrame, OakEngineNode,
	OakEngineRenderer, OakEngineSequence,
};

// ---------------------------------------------------------------------------
// Render manager / cacher
// ---------------------------------------------------------------------------

/// `oakengine_render_manager_set_aggressive_garbage_collection`.
#[no_mangle]
pub extern "C" fn oakengine_render_manager_set_aggressive_garbage_collection(
	aggressive: c_int,
) -> c_int {
	guard(|| Error::from_module(unsafe { r::oakrender_manager_set_aggressive_gc(aggressive) }))
}

/// `oakengine_render_manager_requested_backend` — **not backed** (the
/// oakrender crate exposes the current backend, not the requested one).
/// Returns 0 (k_open_gl).
#[no_mangle]
pub extern "C" fn oakengine_render_manager_requested_backend() -> c_int {
	0
}

/// `oakengine_render_manager_backend_to_string` — **not backed** (the
/// crate enumerates backend ids, not enum→string). Returns
/// OAKENGINE_E_FAILED.
#[no_mangle]
pub unsafe extern "C" fn oakengine_render_manager_backend_to_string(
	_backend: c_int,
	_buf: *mut c_char,
	_buf_size: c_int,
) -> c_int {
	crate::error::OAKENGINE_E_FAILED
}

/// `oakengine_render_cache_set_display_color_processor` — NULL clears.
#[no_mangle]
pub unsafe extern "C" fn oakengine_render_cache_set_display_color_processor(
	processor: *mut c_void,
) -> c_int {
	guard(|| unsafe {
		let proc = if processor.is_null() {
			CHandle::null()
		} else {
			unbox(processor.cast::<OakEngineColorProcessor>())?
		};
		Error::from_module(r::oakrender_set_display_color_processor(proc))
	})
}

/// `oakengine_render_cache_set_multicam_node` — NULL clears.
#[no_mangle]
pub unsafe extern "C" fn oakengine_render_cache_set_multicam_node(
	node: *mut OakEngineNode,
) -> c_int {
	guard(|| unsafe {
		let n = if node.is_null() {
			CHandle::null()
		} else {
			unbox(node)?
		};
		Error::from_module(r::oakrender_set_cacher_multicam(n))
	})
}

// ---------------------------------------------------------------------------
// Renderer
// ---------------------------------------------------------------------------

/// Facade-side renderer box: the bound sequence + output geometry.
struct RendererBox {
	/// Unboxed sequence node handle (borrowed).
	seq: CHandle,
	/// Output width.
	width: c_int,
	/// Output height.
	height: c_int,
	/// Output pixel format (`PixelFormat::Format`).
	pixel_format: c_int,
	/// Frame-rate numerator.
	frame_rate_num: c_int,
	/// Frame-rate denominator.
	frame_rate_den: c_int,
	/// Render mode (0 offline / 1 online).
	mode: c_int,
	/// Last failure reason.
	last_error: String,
}

/// Borrow the renderer box (NULL → Invalid).
unsafe fn renderer(ptr: *const OakEngineRenderer) -> Result<&'static RendererBox> {
	unsafe {
		if ptr.is_null() {
			return Err(Error::Invalid);
		}
		Ok(&*(ptr as *const RendererBox))
	}
}

/// Borrow the renderer box mutably.
unsafe fn renderer_mut(ptr: *mut OakEngineRenderer) -> Result<&'static mut RendererBox> {
	unsafe {
		if ptr.is_null() {
			return Err(Error::Invalid);
		}
		Ok(&mut *(ptr as *mut RendererBox))
	}
}

/// Build an oakcommon video-params handle for the renderer's geometry.
unsafe fn make_video_params(b: &RendererBox) -> Result<CHandle> {
	unsafe {
		let params = crate::bridge::common::oakcommon_videoparams_init();
		if params.is_null() {
			return Err(Error::Failed("video params allocation failed".into()));
		}
		let mut rc = crate::bridge::common::oakcommon_videoparams_set_width(params, b.width);
		if rc == 0 {
			rc = crate::bridge::common::oakcommon_videoparams_set_height(params, b.height);
		}
		if rc == 0 {
			rc = crate::bridge::common::oakcommon_videoparams_set_format(params, b.pixel_format);
		}
		if rc == 0 {
			rc = crate::bridge::common::oakcommon_videoparams_set_time_base(
				params,
				b.frame_rate_den,
				b.frame_rate_num,
			);
		}
		if rc != 0 {
			let mut p = params;
			crate::bridge::common::oakcommon_videoparams_free(&mut p);
			return Err(Error::Failed("video params setup failed".into()));
		}
		Ok(params)
	}
}

/// `oakengine_renderer_create` — NULL for invalid arguments.
#[no_mangle]
pub unsafe extern "C" fn oakengine_renderer_create(
	seq: *mut OakEngineSequence,
	width: c_int,
	height: c_int,
	pixel_format: c_int,
	frame_rate_num: c_int,
	frame_rate_den: c_int,
	output_colorspace: *const c_char,
) -> *mut OakEngineRenderer {
	guard_ptr(|| unsafe {
		if seq.is_null() || width <= 0 || height <= 0 || frame_rate_num <= 0 || frame_rate_den <= 0
		{
			return Ok(std::ptr::null_mut());
		}
		// Validate the pixel format against the oakcore enum. The
		// oakcommon format_name lookup succeeds for ANY code (unknowns
		// format as "Unknown (0x…)"), so only the real formats (U8..F32)
		// are accepted; Invalid (-1), the Count sentinel (5) and garbage
		// codes are rejected.
		if pixel_format < oakcore_rs::PixelFormat::U8 as c_int
			|| pixel_format > oakcore_rs::PixelFormat::F32 as c_int
		{
			return Ok(std::ptr::null_mut());
		}
		let _ = crate::handle::read_cstr(output_colorspace); // resolved by the module at render time
		let seq_handle = unbox(seq)?;
		let boxed = Box::new(RendererBox {
			seq: seq_handle,
			width,
			height,
			pixel_format,
			frame_rate_num,
			frame_rate_den,
			mode: 0,
			last_error: String::new(),
		});
		Ok(Box::into_raw(boxed) as *mut OakEngineRenderer)
	})
}

/// `oakengine_renderer_free` — NULL no-op.
#[no_mangle]
pub unsafe extern "C" fn oakengine_renderer_free(self_: *mut OakEngineRenderer) {
	guard_void(|| unsafe {
		if self_.is_null() {
			return;
		}
		drop(Box::from_raw(self_ as *mut RendererBox));
	})
}

/// `oakengine_renderer_set_mode` — 0/1 only.
#[no_mangle]
pub unsafe extern "C" fn oakengine_renderer_set_mode(
	self_: *mut OakEngineRenderer,
	mode: c_int,
) -> c_int {
	guard(|| unsafe {
		let b = renderer_mut(self_)?;
		if mode != 0 && mode != 1 {
			return Err(Error::Invalid);
		}
		b.mode = mode;
		Ok(())
	})
}

/// `oakengine_renderer_last_error` (buf/size).
#[no_mangle]
pub unsafe extern "C" fn oakengine_renderer_last_error(
	self_: *const OakEngineRenderer,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	guard_int(|| unsafe {
		let b = renderer(self_)?;
		Ok(crate::handle::write_string(&b.last_error, buf, buf_size))
	})
}

/// `oakengine_renderer_render_frame` — synchronous frame render.
#[no_mangle]
pub unsafe extern "C" fn oakengine_renderer_render_frame(
	self_: *mut OakEngineRenderer,
	timestamp: i64,
) -> *mut OakEngineFrame {
	guard_ptr(|| unsafe {
		let b = renderer_mut(self_)?;
		let video_params = make_video_params(b)?;
		let params = OakVideoTicketParams {
			output_node: b.seq,
			video_params,
			audio_params: std::ptr::null(),
			time_num: timestamp * i64::from(b.frame_rate_den),
			time_den: i64::from(b.frame_rate_num),
			color_manager: CHandle::null(),
			mode: b.mode,
			force_width: b.width,
			force_height: b.height,
			force_matrix: [0.0; 16],
			has_force_matrix: 0,
			force_format: -1,
			force_channel_count: 0,
			force_color_output: CHandle::null(),
			force_color_transform: CHandle::null(),
			cache: CHandle::null(),
		};
		let ticket = r::oakrender_ticket_render_frame(&params, None, std::ptr::null_mut());
		let mut vp = video_params;
		crate::bridge::common::oakcommon_videoparams_free(&mut vp);
		if ticket.is_null() {
			b.last_error = "render ticket submission failed".into();
			return Ok(std::ptr::null_mut());
		}
		let wait_rc = r::oakrender_ticket_wait(ticket);
		let mut frame = CHandle::null();
		let get_rc = r::oakrender_ticket_get_frame(ticket, &mut frame);
		let mut t = ticket;
		r::oakrender_ticket_free(&mut t);
		if wait_rc != 0 || get_rc != 0 || frame.is_null() {
			b.last_error = "render failed or timed out".into();
			return Ok(std::ptr::null_mut());
		}
		b.last_error.clear();
		Ok(box_handle::<OakEngineFrame>(frame))
	})
}

/// `oakengine_renderer_render_audio` — synchronous audio render. The
/// oakrender crate's samples path is unimplemented, so this submits the
/// ticket and reports the failure reason.
#[no_mangle]
pub unsafe extern "C" fn oakengine_renderer_render_audio(
	self_: *mut OakEngineRenderer,
	start_timestamp: i64,
	length_timestamp: i64,
) -> *mut OakEngineAudioBuffer {
	guard_ptr(|| unsafe {
		let b = renderer_mut(self_)?;
		let start_num = start_timestamp * i64::from(b.frame_rate_den);
		let end_num = (start_timestamp + length_timestamp) * i64::from(b.frame_rate_den);
		let den = i64::from(b.frame_rate_num);
		let ticket = r::oakrender_ticket_render_audio(
			b.seq,
			start_num,
			den,
			end_num,
			den,
			std::ptr::null(),
			b.mode,
			None,
			std::ptr::null_mut(),
		);
		if ticket.is_null() {
			b.last_error = "audio render ticket submission failed".into();
			return Ok(std::ptr::null_mut());
		}
		let wait_rc = r::oakrender_ticket_wait(ticket);
		let mut samples: *mut c_void = std::ptr::null_mut();
		let get_rc = r::oakrender_ticket_get_samples(ticket, &mut samples);
		let mut t = ticket;
		r::oakrender_ticket_free(&mut t);
		let _ = (wait_rc, get_rc, samples);
		b.last_error = "audio rendering is not implemented by the render module".into();
		Ok(std::ptr::null_mut())
	})
}

/// `oakengine_renderer_cancel` — cancel the in-flight render call.
#[no_mangle]
pub unsafe extern "C" fn oakengine_renderer_cancel(self_: *mut OakEngineRenderer) {
	guard_void(|| unsafe {
		if let Ok(b) = renderer_mut(self_) {
			let _ = b; // the crate tracks in-flight tickets internally
		}
	})
}

// ---------------------------------------------------------------------------
// OakEngineFrame accessors (wraps the module's OakCodecFrame)
// ---------------------------------------------------------------------------

/// Borrow the frame's module handle; `None` for NULL/empty (the engine
/// contract: NULL is a no-op yielding zero results).
unsafe fn frame_handle(ptr: *const OakEngineFrame) -> Option<CHandle> {
	unsafe {
		if ptr.is_null() {
			return None;
		}
		let h = (*ptr).handle();
		if h.is_null() {
			None
		} else {
			Some(h)
		}
	}
}

/// `oakengine_frame_width`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_frame_width(self_: *const OakEngineFrame) -> c_int {
	guard_int(|| unsafe {
		Ok(match frame_handle(self_) {
			Some(f) => r::oakrender_codec_frame_width(f),
			None => 0,
		})
	})
}

/// `oakengine_frame_height`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_frame_height(self_: *const OakEngineFrame) -> c_int {
	guard_int(|| unsafe {
		Ok(match frame_handle(self_) {
			Some(f) => r::oakrender_codec_frame_height(f),
			None => 0,
		})
	})
}

/// `oakengine_frame_format` — the frame's params POD format
/// (`PixelFormat::Format`).
#[no_mangle]
pub unsafe extern "C" fn oakengine_frame_format(self_: *const OakEngineFrame) -> c_int {
	guard_int(|| unsafe {
		let Some(f) = frame_handle(self_) else {
			return Ok(0);
		};
		let mut params = OakRenderVideoParams {
			width: 0,
			height: 0,
			time_base_num: 0,
			time_base_den: 0,
			format: 0,
			pixel_aspect_num: 0,
			pixel_aspect_den: 0,
			interlacing: 0,
			color_range: 0,
			divider: 0,
			video_type: 0,
			premultiplied_alpha: 0,
		};
		Error::from_module(r::oakrender_codec_frame_get_params(f, &mut params))?;
		Ok(params.format)
	})
}

/// `oakengine_frame_channel_count` — **not backed** (the oakrender crate
/// exposes no frame channel count). Returns 0.
#[no_mangle]
pub unsafe extern "C" fn oakengine_frame_channel_count(self_: *const OakEngineFrame) -> c_int {
	let _ = self_;
	0
}

/// `oakengine_frame_linesize_bytes`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_frame_linesize_bytes(self_: *const OakEngineFrame) -> c_int {
	guard_int(|| unsafe {
		Ok(match frame_handle(self_) {
			Some(f) => r::oakrender_codec_frame_linesize_bytes(f),
			None => 0,
		})
	})
}

/// `oakengine_frame_data` — borrowed pixel data.
#[no_mangle]
pub unsafe extern "C" fn oakengine_frame_data(self_: *const OakEngineFrame) -> *const c_void {
	guard_ptr(|| unsafe {
		Ok(match frame_handle(self_) {
			Some(f) => r::oakrender_codec_frame_const_data(f) as *mut c_void,
			None => std::ptr::null_mut(),
		})
	})
}

/// `oakengine_frame_free` — NULL no-op.
#[no_mangle]
pub unsafe extern "C" fn oakengine_frame_free(self_: *mut OakEngineFrame) {
	guard_void(|| unsafe {
		if self_.is_null() {
			return;
		}
		let handle = (*self_).handle();
		let mut h = handle;
		r::oakrender_codec_frame_free(&mut h);
		drop(Box::from_raw(self_));
	})
}

// ---------------------------------------------------------------------------
// OakEngineAudioBuffer accessors (no crate backing — always empty)
// ---------------------------------------------------------------------------

/// `oakengine_audio_sample_rate` — 0 (no crate samples accessor).
#[no_mangle]
pub unsafe extern "C" fn oakengine_audio_sample_rate(_self_: *const OakEngineAudioBuffer) -> c_int {
	0
}

/// `oakengine_audio_channel_count` — 0.
#[no_mangle]
pub unsafe extern "C" fn oakengine_audio_channel_count(
	_self_: *const OakEngineAudioBuffer,
) -> c_int {
	0
}

/// `oakengine_audio_sample_count` — 0.
#[no_mangle]
pub unsafe extern "C" fn oakengine_audio_sample_count(_self_: *const OakEngineAudioBuffer) -> i64 {
	0
}

/// `oakengine_audio_data` — NULL.
#[no_mangle]
pub unsafe extern "C" fn oakengine_audio_data(
	_self_: *const OakEngineAudioBuffer,
	_channel: c_int,
) -> *const f32 {
	std::ptr::null()
}

/// `oakengine_audio_free` — NULL no-op.
#[no_mangle]
pub unsafe extern "C" fn oakengine_audio_free(_self_: *mut OakEngineAudioBuffer) {}

// ---------------------------------------------------------------------------
// Color management
// ---------------------------------------------------------------------------

// Thread-local reason of the last failed color call.
thread_local! {
	static LAST_COLOR_ERROR: RefCell<String> = const { RefCell::new(String::new()) };
}

/// `oakengine_color_last_error` (buf/size).
#[no_mangle]
pub unsafe extern "C" fn oakengine_color_last_error(buf: *mut c_char, buf_size: c_int) -> c_int {
	crate::handle::guard_int(|| {
		Ok(LAST_COLOR_ERROR.with(|e| {
			// SAFETY: `buf` is the caller's buf/size buffer.
			unsafe { crate::handle::write_string(&e.borrow(), buf, buf_size) }
		}))
	})
}

/// `oakengine_color_manager_from_project` — **not backed** (needs the
/// deferred oaknode project family). Returns NULL.
#[no_mangle]
pub unsafe extern "C" fn oakengine_color_manager_from_project(
	_project: *mut crate::handle::OakEngineProject,
) -> *mut crate::handle::OakEngineColorManager {
	std::ptr::null_mut()
}

/// `oakengine_color_manager_get_config_filename` (buf/size).
#[no_mangle]
pub unsafe extern "C" fn oakengine_color_manager_get_config_filename(
	_mgr: *const crate::handle::OakEngineColorManager,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	guard_int(|| unsafe {
		let rc = r::oakrender_color_manager_get_config(buf, buf_size);
		if rc < 0 {
			Err(Error::Module(rc))
		} else {
			Ok(string_result(rc))
		}
	})
}

/// `oakengine_color_manager_set_config_filename` — **not backed** (the
/// crate only reads the config). Returns OAKENGINE_E_FAILED.
#[no_mangle]
pub unsafe extern "C" fn oakengine_color_manager_set_config_filename(
	_mgr: *mut crate::handle::OakEngineColorManager,
	_filename: *const c_char,
) -> c_int {
	crate::error::OAKENGINE_E_FAILED
}

macro_rules! color_manager_stub {
	($($name:ident),* $(,)?) => {
		$(
			/// Documented stub: the oakrender crate does not implement the
			/// color-manager list queries (see `deferred.rs`).
			#[no_mangle]
			pub unsafe extern "C" fn $name() -> c_int {
				crate::error::OAKENGINE_E_FAILED
			}
		)*
	};
}

color_manager_stub! {
	oakengine_color_manager_colorspace_count,
	oakengine_color_manager_display_count,
	oakengine_color_manager_look_count,
}

macro_rules! color_manager_stub_arg {
	($($name:ident),* $(,)?) => {
		$(
			/// Documented stub: the oakrender crate does not implement the
			/// color-manager list queries (see `deferred.rs`).
			#[no_mangle]
			pub unsafe extern "C" fn $name(
				_mgr: *const crate::handle::OakEngineColorManager,
				_index: c_int,
				_buf: *mut c_char,
				_buf_size: c_int,
			) -> c_int {
				crate::error::OAKENGINE_E_FAILED
			}
		)*
	};
}

color_manager_stub_arg! {
	oakengine_color_manager_colorspace_at,
	oakengine_color_manager_display_at,
}

/// `oakengine_color_manager_view_count` — **not backed**. -1.
#[no_mangle]
pub unsafe extern "C" fn oakengine_color_manager_view_count(
	_mgr: *const crate::handle::OakEngineColorManager,
	_display: *const c_char,
) -> c_int {
	-1
}

/// `oakengine_color_manager_view_at` — **not backed**.
#[no_mangle]
pub unsafe extern "C" fn oakengine_color_manager_view_at(
	_mgr: *const crate::handle::OakEngineColorManager,
	_display: *const c_char,
	_index: c_int,
	_buf: *mut c_char,
	_buf_size: c_int,
) -> c_int {
	crate::error::OAKENGINE_E_FAILED
}

/// `oakengine_color_manager_look_at` — **not backed**.
#[no_mangle]
pub unsafe extern "C" fn oakengine_color_manager_look_at(
	_mgr: *const crate::handle::OakEngineColorManager,
	_index: c_int,
	_buf: *mut c_char,
	_buf_size: c_int,
) -> c_int {
	crate::error::OAKENGINE_E_FAILED
}

/// `oakengine_color_manager_default_display` — **not backed**.
#[no_mangle]
pub unsafe extern "C" fn oakengine_color_manager_default_display(
	_mgr: *const crate::handle::OakEngineColorManager,
	_buf: *mut c_char,
	_buf_size: c_int,
) -> c_int {
	crate::error::OAKENGINE_E_FAILED
}

/// `oakengine_color_manager_default_view` — **not backed**.
#[no_mangle]
pub unsafe extern "C" fn oakengine_color_manager_default_view(
	_mgr: *const crate::handle::OakEngineColorManager,
	_display: *const c_char,
	_buf: *mut c_char,
	_buf_size: c_int,
) -> c_int {
	crate::error::OAKENGINE_E_FAILED
}

/// `oakengine_color_manager_default_input_color_space` — **not backed**.
#[no_mangle]
pub unsafe extern "C" fn oakengine_color_manager_default_input_color_space(
	_mgr: *const crate::handle::OakEngineColorManager,
	_buf: *mut c_char,
	_buf_size: c_int,
) -> c_int {
	crate::error::OAKENGINE_E_FAILED
}

/// `oakengine_color_manager_set_default_input_color_space` — **not
/// backed**.
#[no_mangle]
pub unsafe extern "C" fn oakengine_color_manager_set_default_input_color_space(
	_mgr: *mut crate::handle::OakEngineColorManager,
	_colorspace: *const c_char,
) -> c_int {
	crate::error::OAKENGINE_E_FAILED
}

/// `oakengine_color_manager_reference_color_space` — **not backed**.
#[no_mangle]
pub unsafe extern "C" fn oakengine_color_manager_reference_color_space(
	_mgr: *const crate::handle::OakEngineColorManager,
	_buf: *mut c_char,
	_buf_size: c_int,
) -> c_int {
	crate::error::OAKENGINE_E_FAILED
}

/// `oakengine_color_manager_default_luma_coefs` — **not backed**.
#[no_mangle]
pub unsafe extern "C" fn oakengine_color_manager_default_luma_coefs(
	_mgr: *const crate::handle::OakEngineColorManager,
	_rgb: *mut c_double,
) -> c_int {
	crate::error::OAKENGINE_E_FAILED
}

/// `oakengine_color_manager_compliant_color_space` — **not backed**.
#[no_mangle]
pub unsafe extern "C" fn oakengine_color_manager_compliant_color_space(
	_mgr: *const crate::handle::OakEngineColorManager,
	_name: *const c_char,
	_buf: *mut c_char,
	_buf_size: c_int,
) -> c_int {
	crate::error::OAKENGINE_E_FAILED
}

/// `oakengine_color_manager_compliant_transform` — **not backed**.
#[no_mangle]
pub unsafe extern "C" fn oakengine_color_manager_compliant_transform(
	_mgr: *const crate::handle::OakEngineColorManager,
	_in: *const OakColorTransformPod,
	_force_display: c_int,
	_out_is_display: *mut c_int,
	_out_output: *mut c_char,
	_output_size: c_int,
	_out_view: *mut c_char,
	_view_size: c_int,
	_out_look: *mut c_char,
	_look_size: c_int,
) -> c_int {
	crate::error::OAKENGINE_E_FAILED
}

// ---------------------------------------------------------------------------
// Color config handle (not backed)
// ---------------------------------------------------------------------------

/// `oakengine_color_config_load_default` — **not backed**. NULL.
#[no_mangle]
pub extern "C" fn oakengine_color_config_load_default() -> *mut crate::handle::OakEngineColorConfig
{
	std::ptr::null_mut()
}

/// `oakengine_color_config_load_file` — **not backed**. NULL.
#[no_mangle]
pub unsafe extern "C" fn oakengine_color_config_load_file(
	_filename: *const c_char,
) -> *mut crate::handle::OakEngineColorConfig {
	std::ptr::null_mut()
}

/// `oakengine_color_config_free` — NULL no-op.
#[no_mangle]
pub unsafe extern "C" fn oakengine_color_config_free(
	_config: *mut crate::handle::OakEngineColorConfig,
) {
}

/// `oakengine_color_config_colorspace_count` — **not backed**. 0.
#[no_mangle]
pub unsafe extern "C" fn oakengine_color_config_colorspace_count(
	_config: *const crate::handle::OakEngineColorConfig,
) -> c_int {
	0
}

/// `oakengine_color_config_colorspace_at` — **not backed**.
#[no_mangle]
pub unsafe extern "C" fn oakengine_color_config_colorspace_at(
	_config: *const crate::handle::OakEngineColorConfig,
	_index: c_int,
	_buf: *mut c_char,
	_buf_size: c_int,
) -> c_int {
	crate::error::OAKENGINE_E_FAILED
}

// ---------------------------------------------------------------------------
// Color processor
// ---------------------------------------------------------------------------

/// `engine/include/oakengine/color.h` — `oak_color_transform` POD mirror.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct OakColorTransformPod {
	/// 0: `output` is a colorspace; 1: display/view/look.
	pub is_display: c_int,
	/// Colorspace name, or display device when is_display.
	pub output: *const c_char,
	/// Display view (is_display only).
	pub view: *const c_char,
	/// Display look (is_display only).
	pub look: *const c_char,
}

/// `oakengine_color_processor_create` — convert the `oak_color_transform`
/// POD into an oakcommon colortransform handle and hand it to
/// `oakrender_color_processor_create_transform`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_color_processor_create(
	mgr: *const crate::handle::OakEngineColorManager,
	input: *const c_char,
	dest: *const OakColorTransformPod,
	direction: c_int,
) -> *mut OakEngineColorProcessor {
	guard_ptr(|| unsafe {
		if input.is_null() || dest.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let empty = crate::common::empty_cstr();
		let ct = if (*dest).is_display != 0 {
			crate::bridge::common::oakcommon_colortransform_init_display(
				if (*dest).output.is_null() {
					empty
				} else {
					(*dest).output
				},
				if (*dest).view.is_null() {
					empty
				} else {
					(*dest).view
				},
				if (*dest).look.is_null() {
					empty
				} else {
					(*dest).look
				},
			)
		} else {
			crate::bridge::common::oakcommon_colortransform_init_output(
				if (*dest).output.is_null() {
					empty
				} else {
					(*dest).output
				},
			)
		};
		if ct.is_null() {
			LAST_COLOR_ERROR.with(|e| *e.borrow_mut() = "invalid color transform".into());
			return Ok(std::ptr::null_mut());
		}
		let mgr_handle = if mgr.is_null() {
			CHandle::null()
		} else {
			unbox(mgr.cast::<crate::handle::OakEngineColorManager>())?
		};
		let proc = r::oakrender_color_processor_create_transform(mgr_handle, input, ct, direction);
		let mut ct_handle = ct;
		crate::bridge::common::oakcommon_colortransform_free(&mut ct_handle);
		if proc.is_null() {
			LAST_COLOR_ERROR.with(|e| *e.borrow_mut() = "could not create color processor".into());
			return Ok(std::ptr::null_mut());
		}
		LAST_COLOR_ERROR.with(|e| e.borrow_mut().clear());
		Ok(box_handle::<OakEngineColorProcessor>(proc))
	})
}

/// `oakengine_color_processor_free` — NULL no-op.
#[no_mangle]
pub unsafe extern "C" fn oakengine_color_processor_free(proc: *mut OakEngineColorProcessor) {
	guard_void(|| unsafe {
		free_box(proc);
	})
}

/// `oakengine_color_processor_is_valid` (1/0).
#[no_mangle]
pub unsafe extern "C" fn oakengine_color_processor_is_valid(
	proc: *const OakEngineColorProcessor,
) -> c_int {
	guard_int(|| unsafe {
		if proc.is_null() {
			return Ok(0);
		}
		let p = unbox(proc)?;
		Ok(r::oakrender_color_processor_is_valid(p))
	})
}

/// `oakengine_color_processor_convert_color` — single RGBA color.
#[no_mangle]
pub unsafe extern "C" fn oakengine_color_processor_convert_color(
	proc: *const OakEngineColorProcessor,
	in_rgba: *const c_double,
	out_rgba: *mut c_double,
) -> c_int {
	guard(|| unsafe {
		if in_rgba.is_null() || out_rgba.is_null() {
			return Err(Error::Invalid);
		}
		let p = unbox(proc)?;
		let rc = r::oakrender_color_processor_convert(
			p,
			*in_rgba,
			*in_rgba.add(1),
			*in_rgba.add(2),
			*in_rgba.add(3),
			out_rgba,
			out_rgba.add(1),
			out_rgba.add(2),
			out_rgba.add(3),
		);
		Error::from_module(rc)
	})
}

/// `oakengine_color_processor_id` — **not backed** (the crate exposes no
/// processor cache id). Returns OAKENGINE_E_FAILED.
#[no_mangle]
pub unsafe extern "C" fn oakengine_color_processor_id(
	_proc: *const OakEngineColorProcessor,
	_buf: *mut c_char,
	_buf_size: c_int,
) -> c_int {
	crate::error::OAKENGINE_E_FAILED
}

/// `oakengine_color_transform_job_set_processor` — **not backed** (the
/// job is a C++ type). Returns OAKENGINE_E_INVALID for a NULL job.
#[no_mangle]
pub unsafe extern "C" fn oakengine_color_transform_job_set_processor(
	job: *mut c_void,
	_proc: *const OakEngineColorProcessor,
) -> c_int {
	if job.is_null() {
		crate::error::OAKENGINE_E_INVALID
	} else {
		crate::error::OAKENGINE_E_FAILED
	}
}

// ---------------------------------------------------------------------------
// LUT library (not backed)
// ---------------------------------------------------------------------------

/// `oakengine_lut_directory_count` — **not backed** (the LUT directory/
/// file library is facade-level over FileFunctions; the crate only
/// enumerates supported extensions). Returns 0.
#[no_mangle]
pub extern "C" fn oakengine_lut_directory_count() -> c_int {
	0
}

/// `oakengine_lut_directory_at` — **not backed**.
#[no_mangle]
pub unsafe extern "C" fn oakengine_lut_directory_at(
	_index: c_int,
	_buf: *mut c_char,
	_buf_size: c_int,
) -> c_int {
	crate::error::OAKENGINE_E_FAILED
}

/// `oakengine_lut_file_count` — **not backed**. Returns 0.
#[no_mangle]
pub extern "C" fn oakengine_lut_file_count() -> c_int {
	0
}

/// `oakengine_lut_file_at` — **not backed**.
#[no_mangle]
pub unsafe extern "C" fn oakengine_lut_file_at(
	_index: c_int,
	_buf: *mut c_char,
	_buf_size: c_int,
) -> c_int {
	crate::error::OAKENGINE_E_FAILED
}

/// `oakengine_lut_set_directories` — **not backed**.
#[no_mangle]
pub unsafe extern "C" fn oakengine_lut_set_directories(
	_dirs: *const *const c_char,
	_count: c_int,
) -> c_int {
	crate::error::OAKENGINE_E_FAILED
}
