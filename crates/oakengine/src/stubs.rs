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

//! Local replacements for the deleted `src/bridge/` (single-lib
//! unification).
//!
//! The engine's `src/*.rs` files used to call the module crates through
//! the deleted `src/bridge/` wrappers over their C ABIs. Those ABIs are
//! deleted; every module crate now exposes direct Rust APIs only. This
//! module carries the replacement surface:
//!
//! - **Direct-Rust shims** ([`common`], [`codec`], [`audio`], [`plugin`]):
//!   the old bridge names and signatures are kept, implemented over the
//!   module crates' value types (oakcommon `ConfigStore`/`VideoParams`/
//!   `ColorTransform`, oakcodec's export tables, oakaudio's singleton
//!   manager/processor/sync/waveform APIs, oakplugin's OFX host).
//! - **Domain implementations** ([`node`], [`timeline`], [`render`],
//!   [`task`]): the handle-based paths are implemented over the module
//!   crates' direct Rust domains. The engine's handle layer boxes the
//!   oaknode domain behind the upward CHandles
//!   (`crate::handle::domain`): projects box
//!   `Arc<Mutex<oaknode::project::Project>>`, nodes/blocks/tracks/
//!   footage/sequences/folders box `oaknode::project::NodeRef`
//!   (project + `NodeId`). Undoable creators return handles boxing
//!   `oakundo::undocommand::UndoCommand` values; the oaktimeline
//!   commands are the crate's real `NodeRef`-based constructors and the
//!   oaktask creators wrap the real `Task` types. The few genuinely
//!   unwireable parts (no Rust equivalent) remain clearly marked STUBs
//!   with their reasons.
//!
//! The engine's `oakengine_*` C ABI exports (upward) are untouched; only
//! the downward internals were rewired.

pub mod common {
	use std::ffi::{c_char, c_int, c_void};

	use oakcommon::colortransform::ColorTransform;
	use oakcommon::configstore::ConfigStore;
	use oakcommon::handle::{get, get_mut, make_owned};
	use oakcommon::ocioutils::PixelFormat;
	use oakcommon::videoparams::{ColorRange, Interlacing, VideoParams, VideoType};
	use oakcommon::error::{OAKCOMMON_E_INVALID, OAKCOMMON_OK};

	use crate::handle::{read_cstr, CHandle};

	/// `include/common/config.h` — error handler callback (same shape as
	/// the oakcommon crate's [`oakcommon::configstore::ErrorHandler`]).
	pub type ConfigErrorHandler = oakcommon::configstore::ErrorHandler;

	/// Standard two-stage getter copy: copy only when the buffer is large
	/// enough (never truncates); always return the required size incl. NUL.
	fn copy_string(value: &str, buf: *mut c_char, buf_size: c_int) -> c_int {
		let required = (value.len() + 1) as c_int;
		if !buf.is_null() && buf_size >= required {
			// SAFETY: the caller guarantees `buf` holds `buf_size` bytes.
			unsafe {
				std::ptr::copy_nonoverlapping(value.as_ptr() as *const c_char, buf, value.len());
				*buf.add(value.len()) = 0;
			}
		}
		required
	}

	/// Whether `(buf, buf_size)` is a valid two-stage getter output.
	fn is_valid_string_out(buf: *mut c_char, buf_size: c_int) -> bool {
		buf_size >= 0 && (buf_size == 0 || !buf.is_null())
	}

	/// group pointer -> `Option<&str>` (null -> `None`).
	fn group_opt(group: *const c_char) -> Option<&'static str> {
		if group.is_null() {
			None
		} else {
			// SAFETY: the caller guarantees a valid NUL-terminated string
			// that lives for the call.
			unsafe { Some(std::ffi::CStr::from_ptr(group).to_str().unwrap_or("")) }
		}
	}

	/// Release a `CHandle` in place: call its release callback, then write
	/// null back.
	fn free_handle(h: *mut CHandle) {
		if h.is_null() {
			return;
		}
		// SAFETY: the caller passes a valid handle pointer.
		let handle = unsafe { &mut *h };
		if handle.ctx.is_null() {
			return;
		}
		if let Some(release) = handle.release {
			// SAFETY: `release` targets the box behind `ctx`.
			unsafe { release(handle.ctx) };
		}
		handle.ctx = std::ptr::null_mut();
		handle.addref = None;
		handle.release = None;
		handle.abi_version = 0;
	}

	/// `oakcommon_config_load` — direct call into `ConfigStore::load`.
	pub fn oakcommon_config_load() -> c_int {
		match ConfigStore::instance().load() {
			Ok(()) => OAKCOMMON_OK,
			Err(e) => e.code(),
		}
	}

	/// `oakcommon_config_save` — direct call into `ConfigStore::save`.
	pub fn oakcommon_config_save() -> c_int {
		match ConfigStore::instance().save() {
			Ok(()) => OAKCOMMON_OK,
			Err(e) => e.code(),
		}
	}

	/// `oakcommon_config_reset_defaults` — direct call.
	pub fn oakcommon_config_reset_defaults() -> c_int {
		match ConfigStore::instance().reset_defaults() {
			Ok(()) => OAKCOMMON_OK,
			Err(e) => e.code(),
		}
	}

	/// `oakcommon_config_set` — direct call into `ConfigStore::set`.
	pub unsafe fn oakcommon_config_set(
		group: *const c_char,
		key: *const c_char,
		value: *const c_char,
	) {
		// SAFETY: the caller guarantees valid NUL-terminated strings.
		unsafe {
			if key.is_null() || value.is_null() {
				return;
			}
			ConfigStore::instance().set(group_opt(group), &read_cstr(key), &read_cstr(value));
		}
	}

	/// `oakcommon_config_get` (two-stage string getter).
	pub unsafe fn oakcommon_config_get(
		group: *const c_char,
		key: *const c_char,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		// SAFETY: the caller guarantees valid NUL-terminated strings.
		unsafe {
			if key.is_null() || !is_valid_string_out(buf, buf_size) {
				return OAKCOMMON_E_INVALID;
			}
			match ConfigStore::instance().get(group_opt(group), &read_cstr(key)) {
				Ok(s) => copy_string(&s, buf, buf_size),
				Err(e) => e.code(),
			}
		}
	}

	/// `oakcommon_config_get_int` — direct call.
	pub unsafe fn oakcommon_config_get_int(
		group: *const c_char,
		key: *const c_char,
		fallback: c_int,
	) -> c_int {
		// SAFETY: `key` is a valid NUL-terminated string or NULL.
		unsafe {
			if key.is_null() {
				return fallback;
			}
			ConfigStore::instance().get_int(group_opt(group), &read_cstr(key), fallback)
		}
	}

	/// `oakcommon_config_get_int64` — direct call.
	pub unsafe fn oakcommon_config_get_int64(
		group: *const c_char,
		key: *const c_char,
		fallback: i64,
	) -> i64 {
		// SAFETY: `key` is a valid NUL-terminated string or NULL.
		unsafe {
			if key.is_null() {
				return fallback;
			}
			ConfigStore::instance().get_int64(group_opt(group), &read_cstr(key), fallback)
		}
	}

	/// `oakcommon_config_get_double` — direct call.
	pub unsafe fn oakcommon_config_get_double(
		group: *const c_char,
		key: *const c_char,
		fallback: f64,
	) -> f64 {
		// SAFETY: `key` is a valid NUL-terminated string or NULL.
		unsafe {
			if key.is_null() {
				return fallback;
			}
			ConfigStore::instance().get_double(group_opt(group), &read_cstr(key), fallback)
		}
	}

	/// `oakcommon_config_get_bool` — direct call.
	pub unsafe fn oakcommon_config_get_bool(
		group: *const c_char,
		key: *const c_char,
		fallback: c_int,
	) -> c_int {
		// SAFETY: `key` is a valid NUL-terminated string or NULL.
		unsafe {
			if key.is_null() {
				return fallback;
			}
			ConfigStore::instance().get_bool(group_opt(group), &read_cstr(key), fallback)
		}
	}

	/// `oakcommon_config_set_int` — direct call.
	pub unsafe fn oakcommon_config_set_int(group: *const c_char, key: *const c_char, value: c_int) {
		// SAFETY: `key` is a valid NUL-terminated string or NULL.
		unsafe {
			if key.is_null() {
				return;
			}
			ConfigStore::instance().set_int(group_opt(group), &read_cstr(key), value);
		}
	}

	/// `oakcommon_config_set_int64` — direct call.
	pub unsafe fn oakcommon_config_set_int64(
		group: *const c_char,
		key: *const c_char,
		value: i64,
	) {
		// SAFETY: `key` is a valid NUL-terminated string or NULL.
		unsafe {
			if key.is_null() {
				return;
			}
			ConfigStore::instance().set_int64(group_opt(group), &read_cstr(key), value);
		}
	}

	/// `oakcommon_config_set_double` — direct call.
	pub unsafe fn oakcommon_config_set_double(
		group: *const c_char,
		key: *const c_char,
		value: f64,
	) {
		// SAFETY: `key` is a valid NUL-terminated string or NULL.
		unsafe {
			if key.is_null() {
				return;
			}
			ConfigStore::instance().set_double(group_opt(group), &read_cstr(key), value);
		}
	}

	/// `oakcommon_config_set_bool` — direct call.
	pub unsafe fn oakcommon_config_set_bool(group: *const c_char, key: *const c_char, value: c_int) {
		// SAFETY: `key` is a valid NUL-terminated string or NULL.
		unsafe {
			if key.is_null() {
				return;
			}
			ConfigStore::instance().set_bool(group_opt(group), &read_cstr(key), value);
		}
	}

	/// `oakcommon_config_entry_type` — direct call.
	pub unsafe fn oakcommon_config_entry_type(group: *const c_char, key: *const c_char) -> c_int {
		// SAFETY: `key` is a valid NUL-terminated string or NULL.
		unsafe {
			if key.is_null() {
				return OAKCOMMON_E_INVALID;
			}
			match ConfigStore::instance().entry_type(group_opt(group), &read_cstr(key)) {
				Ok(oakcommon::configstore::EntryType::None) => 0,
				Ok(oakcommon::configstore::EntryType::String) => 1,
				Ok(oakcommon::configstore::EntryType::Int) => 2,
				Ok(oakcommon::configstore::EntryType::Double) => 3,
				Ok(oakcommon::configstore::EntryType::Bool) => 4,
				Err(e) => e.code(),
			}
		}
	}

	/// `oakcommon_config_set_error_handler` — direct call.
	pub fn oakcommon_config_set_error_handler(
		handler: ConfigErrorHandler,
		userdata: *mut c_void,
	) -> c_int {
		match ConfigStore::instance().set_error_handler(handler, userdata) {
			Ok(()) => OAKCOMMON_OK,
			Err(e) => e.code(),
		}
	}

	// ---- videoparams (value-typed VideoParams boxed as CHandles) ----------

	fn interlacing_from_i32(v: c_int) -> Interlacing {
		match v {
			1 => Interlacing::TopFirst,
			2 => Interlacing::BottomFirst,
			_ => Interlacing::None,
		}
	}

	fn video_type_from_i32(v: c_int) -> VideoType {
		match v {
			1 => VideoType::Still,
			2 => VideoType::ImageSequence,
			_ => VideoType::Video,
		}
	}

	fn color_range_from_i32(v: c_int) -> ColorRange {
		if v == 1 {
			ColorRange::Full
		} else {
			ColorRange::Limited
		}
	}

	/// `oakcommon_videoparams_init` — boxes `VideoParams::new()`.
	pub fn oakcommon_videoparams_init() -> CHandle {
		make_owned(VideoParams::new())
	}

	/// `oakcommon_videoparams_init_basic` — boxes `VideoParams::new_basic`.
	pub fn oakcommon_videoparams_init_basic(
		width: c_int,
		height: c_int,
		pixel_format: c_int,
		nb_channels: c_int,
		pixel_aspect_num: c_int,
		pixel_aspect_den: c_int,
		interlacing: c_int,
		divider: c_int,
	) -> CHandle {
		make_owned(VideoParams::new_basic(
			width,
			height,
			PixelFormat::from_code(pixel_format),
			nb_channels,
			pixel_aspect_num,
			pixel_aspect_den,
			interlacing,
			divider,
		))
	}

	/// `oakcommon_videoparams_init_with_time_base` — boxes
	/// `VideoParams::new_with_time_base`.
	#[allow(clippy::too_many_arguments)]
	pub fn oakcommon_videoparams_init_with_time_base(
		width: c_int,
		height: c_int,
		time_base_num: c_int,
		time_base_den: c_int,
		pixel_format: c_int,
		nb_channels: c_int,
		pixel_aspect_num: c_int,
		pixel_aspect_den: c_int,
		interlacing: c_int,
		divider: c_int,
	) -> CHandle {
		make_owned(VideoParams::new_with_time_base(
			width,
			height,
			time_base_num,
			time_base_den,
			PixelFormat::from_code(pixel_format),
			nb_channels,
			pixel_aspect_num,
			pixel_aspect_den,
			interlacing,
			divider,
		))
	}

	/// `oakcommon_videoparams_free` — release one reference.
	pub fn oakcommon_videoparams_free(params: *mut CHandle) {
		free_handle(params);
	}

	macro_rules! vp_getter {
		($name:ident, $method:ident, $ty:ty) => {
			#[doc = concat!("`oakcommon_videoparams_", stringify!($name), "` — direct call into `VideoParams::", stringify!($method), "`.")]
			pub fn $name(params: CHandle, out: *mut $ty) -> c_int {
				if params.is_null() || out.is_null() {
					return OAKCOMMON_E_INVALID;
				}
				// SAFETY: `params` must be a live `make_owned(VideoParams)` handle.
				match unsafe { get::<VideoParams>(&params) } {
					Some(p) => {
						// SAFETY: `out` is a valid out pointer.
						unsafe {
							*out = p.$method() as $ty;
						}
						OAKCOMMON_OK
					}
					None => OAKCOMMON_E_INVALID,
				}
			}
		};
	}

	macro_rules! vp_setter {
		($name:ident, $method:ident, $ty:ty) => {
			#[doc = concat!("`oakcommon_videoparams_", stringify!($name), "` — direct call into `VideoParams::", stringify!($method), "`.")]
			pub fn $name(params: CHandle, value: $ty) -> c_int {
				if params.is_null() {
					return OAKCOMMON_E_INVALID;
				}
				// SAFETY: `params` must be a live `make_owned(VideoParams)` handle.
				match unsafe { get_mut::<VideoParams>(&params) } {
					Some(p) => {
						p.$method(value);
						OAKCOMMON_OK
					}
					None => OAKCOMMON_E_INVALID,
				}
			}
		};
	}

	vp_getter!(oakcommon_videoparams_get_width, width, i32);
	vp_setter!(oakcommon_videoparams_set_width, set_width, i32);
	vp_getter!(oakcommon_videoparams_get_height, height, i32);
	vp_setter!(oakcommon_videoparams_set_height, set_height, i32);
	vp_getter!(oakcommon_videoparams_get_depth, depth, i32);
	vp_setter!(oakcommon_videoparams_set_depth, set_depth, i32);
	vp_getter!(oakcommon_videoparams_get_is_3d, is_3d, i32);
	vp_getter!(oakcommon_videoparams_get_channel_count, channel_count, i32);
	vp_setter!(oakcommon_videoparams_set_channel_count, set_channel_count, i32);
	vp_getter!(oakcommon_videoparams_get_enabled, enabled, i32);
	/// `oakcommon_videoparams_set_enabled` — bool setter.
	pub fn oakcommon_videoparams_set_enabled(params: CHandle, value: c_int) -> c_int {
		if params.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		// SAFETY: `params` must be a live `make_owned(VideoParams)` handle.
		match unsafe { get_mut::<VideoParams>(&params) } {
			Some(p) => {
				p.set_enabled(value != 0);
				OAKCOMMON_OK
			}
			None => OAKCOMMON_E_INVALID,
		}
	}
	vp_getter!(oakcommon_videoparams_get_x, x, f32);
	vp_setter!(oakcommon_videoparams_set_x, set_x, f32);
	vp_getter!(oakcommon_videoparams_get_y, y, f32);
	vp_setter!(oakcommon_videoparams_set_y, set_y, f32);
	vp_getter!(oakcommon_videoparams_get_stream_index, stream_index, i32);
	vp_setter!(oakcommon_videoparams_set_stream_index, set_stream_index, i32);
	vp_getter!(oakcommon_videoparams_get_start_time, start_time, i64);
	vp_setter!(oakcommon_videoparams_set_start_time, set_start_time, i64);
	vp_getter!(oakcommon_videoparams_get_duration, duration, i64);
	vp_setter!(oakcommon_videoparams_set_duration, set_duration, i64);
	vp_getter!(oakcommon_videoparams_get_color_primaries, color_primaries, i32);
	vp_setter!(oakcommon_videoparams_set_color_primaries, set_color_primaries, i32);
	vp_getter!(oakcommon_videoparams_get_color_transfer, color_transfer, i32);
	vp_setter!(oakcommon_videoparams_set_color_transfer, set_color_transfer, i32);
	vp_getter!(oakcommon_videoparams_get_square_pixel_width, square_pixel_width, i32);
	vp_getter!(oakcommon_videoparams_get_effective_width, effective_width, i32);
	vp_getter!(oakcommon_videoparams_get_effective_height, effective_height, i32);
	vp_getter!(oakcommon_videoparams_get_effective_depth, effective_depth, i32);
	vp_getter!(oakcommon_videoparams_get_is_valid, is_valid, i32);
	vp_getter!(oakcommon_videoparams_get_bytes_per_channel, bytes_per_channel, i32);
	vp_getter!(oakcommon_videoparams_get_bytes_per_pixel, bytes_per_pixel, i32);
	vp_getter!(oakcommon_videoparams_get_buffer_size, buffer_size, i32);

	/// `oakcommon_videoparams_get_time_base` — num/den pair getter.
	pub fn oakcommon_videoparams_get_time_base(
		params: CHandle,
		numerator: *mut c_int,
		denominator: *mut c_int,
	) -> c_int {
		if params.is_null() || numerator.is_null() || denominator.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		// SAFETY: `params` must be a live `make_owned(VideoParams)` handle.
		match unsafe { get::<VideoParams>(&params) } {
			Some(p) => {
				let (n, d) = p.time_base();
				// SAFETY: valid out pointers.
				unsafe {
					*numerator = n;
					*denominator = d;
				}
				OAKCOMMON_OK
			}
			None => OAKCOMMON_E_INVALID,
		}
	}

	/// `oakcommon_videoparams_set_time_base` — num/den pair setter.
	pub fn oakcommon_videoparams_set_time_base(
		params: CHandle,
		numerator: c_int,
		denominator: c_int,
	) -> c_int {
		if params.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		// SAFETY: `params` must be a live `make_owned(VideoParams)` handle.
		match unsafe { get_mut::<VideoParams>(&params) } {
			Some(p) => {
				p.set_time_base(numerator, denominator);
				OAKCOMMON_OK
			}
			None => OAKCOMMON_E_INVALID,
		}
	}

	/// `oakcommon_videoparams_get_frame_rate` — num/den pair getter.
	pub fn oakcommon_videoparams_get_frame_rate(
		params: CHandle,
		numerator: *mut c_int,
		denominator: *mut c_int,
	) -> c_int {
		if params.is_null() || numerator.is_null() || denominator.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		// SAFETY: `params` must be a live `make_owned(VideoParams)` handle.
		match unsafe { get::<VideoParams>(&params) } {
			Some(p) => {
				let (n, d) = p.frame_rate();
				// SAFETY: valid out pointers.
				unsafe {
					*numerator = n;
					*denominator = d;
				}
				OAKCOMMON_OK
			}
			None => OAKCOMMON_E_INVALID,
		}
	}

	/// `oakcommon_videoparams_set_frame_rate` — num/den pair setter.
	pub fn oakcommon_videoparams_set_frame_rate(
		params: CHandle,
		numerator: c_int,
		denominator: c_int,
	) -> c_int {
		if params.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		// SAFETY: `params` must be a live `make_owned(VideoParams)` handle.
		match unsafe { get_mut::<VideoParams>(&params) } {
			Some(p) => {
				p.set_frame_rate(numerator, denominator);
				OAKCOMMON_OK
			}
			None => OAKCOMMON_E_INVALID,
		}
	}

	/// `oakcommon_videoparams_frame_rate_as_time_base` — num/den getter.
	pub fn oakcommon_videoparams_frame_rate_as_time_base(
		params: CHandle,
		numerator: *mut c_int,
		denominator: *mut c_int,
	) -> c_int {
		if params.is_null() || numerator.is_null() || denominator.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		// SAFETY: `params` must be a live `make_owned(VideoParams)` handle.
		match unsafe { get::<VideoParams>(&params) } {
			Some(p) => {
				let (n, d) = p.frame_rate_as_time_base();
				// SAFETY: valid out pointers.
				unsafe {
					*numerator = n;
					*denominator = d;
				}
				OAKCOMMON_OK
			}
			None => OAKCOMMON_E_INVALID,
		}
	}

	/// `oakcommon_videoparams_get_pixel_aspect_ratio` — num/den getter.
	pub fn oakcommon_videoparams_get_pixel_aspect_ratio(
		params: CHandle,
		numerator: *mut c_int,
		denominator: *mut c_int,
	) -> c_int {
		if params.is_null() || numerator.is_null() || denominator.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		// SAFETY: `params` must be a live `make_owned(VideoParams)` handle.
		match unsafe { get::<VideoParams>(&params) } {
			Some(p) => {
				let (n, d) = p.pixel_aspect_ratio();
				// SAFETY: valid out pointers.
				unsafe {
					*numerator = n;
					*denominator = d;
				}
				OAKCOMMON_OK
			}
			None => OAKCOMMON_E_INVALID,
		}
	}

	/// `oakcommon_videoparams_set_pixel_aspect_ratio` — num/den setter.
	pub fn oakcommon_videoparams_set_pixel_aspect_ratio(
		params: CHandle,
		numerator: c_int,
		denominator: c_int,
	) -> c_int {
		if params.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		// SAFETY: `params` must be a live `make_owned(VideoParams)` handle.
		match unsafe { get_mut::<VideoParams>(&params) } {
			Some(p) => {
				p.set_pixel_aspect_ratio(numerator, denominator);
				OAKCOMMON_OK
			}
			None => OAKCOMMON_E_INVALID,
		}
	}

	/// `oakcommon_videoparams_get_format` — `PixelFormat::Format` code.
	pub fn oakcommon_videoparams_get_format(params: CHandle, format: *mut c_int) -> c_int {
		if params.is_null() || format.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		// SAFETY: `params` must be a live `make_owned(VideoParams)` handle.
		match unsafe { get::<VideoParams>(&params) } {
			Some(p) => {
				// SAFETY: valid out pointer.
				unsafe {
					*format = p.format().code();
				}
				OAKCOMMON_OK
			}
			None => OAKCOMMON_E_INVALID,
		}
	}

	/// `oakcommon_videoparams_set_format` — `PixelFormat::Format` code.
	pub fn oakcommon_videoparams_set_format(params: CHandle, format: c_int) -> c_int {
		if params.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		// SAFETY: `params` must be a live `make_owned(VideoParams)` handle.
		match unsafe { get_mut::<VideoParams>(&params) } {
			Some(p) => {
				p.set_format(PixelFormat::from_code(format));
				OAKCOMMON_OK
			}
			None => OAKCOMMON_E_INVALID,
		}
	}

	/// `oakcommon_videoparams_get_interlacing` — `Interlacing` code.
	pub fn oakcommon_videoparams_get_interlacing(
		params: CHandle,
		interlacing: *mut c_int,
	) -> c_int {
		if params.is_null() || interlacing.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		// SAFETY: `params` must be a live `make_owned(VideoParams)` handle.
		match unsafe { get::<VideoParams>(&params) } {
			Some(p) => {
				// SAFETY: valid out pointer.
				unsafe {
					*interlacing = p.interlacing() as c_int;
				}
				OAKCOMMON_OK
			}
			None => OAKCOMMON_E_INVALID,
		}
	}

	/// `oakcommon_videoparams_set_interlacing` — `Interlacing` code.
	pub fn oakcommon_videoparams_set_interlacing(params: CHandle, interlacing: c_int) -> c_int {
		if params.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		// SAFETY: `params` must be a live `make_owned(VideoParams)` handle.
		match unsafe { get_mut::<VideoParams>(&params) } {
			Some(p) => {
				p.set_interlacing(interlacing_from_i32(interlacing));
				OAKCOMMON_OK
			}
			None => OAKCOMMON_E_INVALID,
		}
	}

	/// `oakcommon_videoparams_get_divider` — divider.
	pub fn oakcommon_videoparams_get_divider(params: CHandle, divider: *mut c_int) -> c_int {
		if params.is_null() || divider.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		// SAFETY: `params` must be a live `make_owned(VideoParams)` handle.
		match unsafe { get::<VideoParams>(&params) } {
			Some(p) => {
				// SAFETY: valid out pointer.
				unsafe {
					*divider = p.divider();
				}
				OAKCOMMON_OK
			}
			None => OAKCOMMON_E_INVALID,
		}
	}

	/// `oakcommon_videoparams_set_divider` — divider.
	pub fn oakcommon_videoparams_set_divider(params: CHandle, divider: c_int) -> c_int {
		if params.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		// SAFETY: `params` must be a live `make_owned(VideoParams)` handle.
		match unsafe { get_mut::<VideoParams>(&params) } {
			Some(p) => {
				p.set_divider(divider);
				OAKCOMMON_OK
			}
			None => OAKCOMMON_E_INVALID,
		}
	}

	/// `oakcommon_videoparams_get_video_type` — `VideoType` code.
	pub fn oakcommon_videoparams_get_video_type(params: CHandle, type_: *mut c_int) -> c_int {
		if params.is_null() || type_.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		// SAFETY: `params` must be a live `make_owned(VideoParams)` handle.
		match unsafe { get::<VideoParams>(&params) } {
			Some(p) => {
				// SAFETY: valid out pointer.
				unsafe {
					*type_ = p.video_type() as c_int;
				}
				OAKCOMMON_OK
			}
			None => OAKCOMMON_E_INVALID,
		}
	}

	/// `oakcommon_videoparams_set_video_type` — `VideoType` code.
	pub fn oakcommon_videoparams_set_video_type(params: CHandle, type_: c_int) -> c_int {
		if params.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		// SAFETY: `params` must be a live `make_owned(VideoParams)` handle.
		match unsafe { get_mut::<VideoParams>(&params) } {
			Some(p) => {
				p.set_video_type(video_type_from_i32(type_));
				OAKCOMMON_OK
			}
			None => OAKCOMMON_E_INVALID,
		}
	}

	/// `oakcommon_videoparams_get_premultiplied_alpha` — 0/1.
	pub fn oakcommon_videoparams_get_premultiplied_alpha(
		params: CHandle,
		premultiplied: *mut c_int,
	) -> c_int {
		if params.is_null() || premultiplied.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		// SAFETY: `params` must be a live `make_owned(VideoParams)` handle.
		match unsafe { get::<VideoParams>(&params) } {
			Some(p) => {
				// SAFETY: valid out pointer.
				unsafe {
					*premultiplied = p.premultiplied_alpha() as c_int;
				}
				OAKCOMMON_OK
			}
			None => OAKCOMMON_E_INVALID,
		}
	}

	/// `oakcommon_videoparams_set_premultiplied_alpha` — 0/1.
	pub fn oakcommon_videoparams_set_premultiplied_alpha(
		params: CHandle,
		premultiplied: c_int,
	) -> c_int {
		if params.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		// SAFETY: `params` must be a live `make_owned(VideoParams)` handle.
		match unsafe { get_mut::<VideoParams>(&params) } {
			Some(p) => {
				p.set_premultiplied_alpha(premultiplied != 0);
				OAKCOMMON_OK
			}
			None => OAKCOMMON_E_INVALID,
		}
	}

	/// `oakcommon_videoparams_get_color_range` — `ColorRange` code.
	pub fn oakcommon_videoparams_get_color_range(
		params: CHandle,
		color_range: *mut c_int,
	) -> c_int {
		if params.is_null() || color_range.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		// SAFETY: `params` must be a live `make_owned(VideoParams)` handle.
		match unsafe { get::<VideoParams>(&params) } {
			Some(p) => {
				// SAFETY: valid out pointer.
				unsafe {
					*color_range = p.color_range() as c_int;
				}
				OAKCOMMON_OK
			}
			None => OAKCOMMON_E_INVALID,
		}
	}

	/// `oakcommon_videoparams_set_color_range` — `ColorRange` code.
	pub fn oakcommon_videoparams_set_color_range(params: CHandle, color_range: c_int) -> c_int {
		if params.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		// SAFETY: `params` must be a live `make_owned(VideoParams)` handle.
		match unsafe { get_mut::<VideoParams>(&params) } {
			Some(p) => {
				p.set_color_range(color_range_from_i32(color_range));
				OAKCOMMON_OK
			}
			None => OAKCOMMON_E_INVALID,
		}
	}

	/// `oakcommon_videoparams_get_colorspace` (two-stage string getter).
	pub fn oakcommon_videoparams_get_colorspace(
		params: CHandle,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		if params.is_null() || !is_valid_string_out(buf, buf_size) {
			return OAKCOMMON_E_INVALID;
		}
		// SAFETY: `params` must be a live `make_owned(VideoParams)` handle.
		match unsafe { get::<VideoParams>(&params) } {
			Some(p) => copy_string(p.colorspace(), buf, buf_size),
			None => OAKCOMMON_E_INVALID,
		}
	}

	/// `oakcommon_videoparams_set_colorspace`.
	pub unsafe fn oakcommon_videoparams_set_colorspace(
		params: CHandle,
		colorspace: *const c_char,
	) -> c_int {
		// SAFETY: the caller guarantees a valid NUL-terminated string.
		unsafe {
			if params.is_null() || colorspace.is_null() {
				return OAKCOMMON_E_INVALID;
			}
			match get_mut::<VideoParams>(&params) {
				Some(p) => {
					p.set_colorspace(&read_cstr(colorspace));
					OAKCOMMON_OK
				}
				None => OAKCOMMON_E_INVALID,
			}
		}
	}

	/// `oakcommon_videoparams_get_time_in_timebase_units`.
	pub fn oakcommon_videoparams_get_time_in_timebase_units(
		params: CHandle,
		time_num: c_int,
		time_den: c_int,
		timestamp: *mut i64,
	) -> c_int {
		if params.is_null() || timestamp.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		// SAFETY: `params` must be a live `make_owned(VideoParams)` handle.
		match unsafe { get::<VideoParams>(&params) } {
			Some(p) => {
				// CPP-PARITY: the C++ returns INT64_MIN (AV_NOPTS_VALUE) when
				// no time base is set; the Rust domain returns None.
				// SAFETY: valid out pointer.
				unsafe {
					*timestamp = p.time_in_timebase_units(time_num, time_den).unwrap_or(i64::MIN);
				}
				OAKCOMMON_OK
			}
			None => OAKCOMMON_E_INVALID,
		}
	}

	/// `oakcommon_videoparams_equals` — 1 when the sets match.
	pub fn oakcommon_videoparams_equals(a: CHandle, b: CHandle, out_equal: *mut c_int) -> c_int {
		if a.is_null() || b.is_null() || out_equal.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		// SAFETY: `a`/`b` must be live `make_owned(VideoParams)` handles.
		match (unsafe { get::<VideoParams>(&a) }, unsafe { get::<VideoParams>(&b) }) {
			(Some(pa), Some(pb)) => {
				// SAFETY: valid out pointer.
				unsafe {
					*out_equal = pa.equals(pb) as c_int;
				}
				OAKCOMMON_OK
			}
			_ => OAKCOMMON_E_INVALID,
		}
	}

	/// `oakcommon_videoparams_format_is_float` (static).
	pub fn oakcommon_videoparams_format_is_float(pixel_format: c_int) -> c_int {
		VideoParams::format_is_float(PixelFormat::from_code(pixel_format)) as c_int
	}

	/// `oakcommon_videoparams_get_format_name` (two-stage string getter).
	pub fn oakcommon_videoparams_get_format_name(
		pixel_format: c_int,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		if !is_valid_string_out(buf, buf_size) {
			return OAKCOMMON_E_INVALID;
		}
		match VideoParams::format_name(PixelFormat::from_code(pixel_format)) {
			Ok(s) => copy_string(&s, buf, buf_size),
			Err(e) => e.code(),
		}
	}

	/// `oakcommon_videoparams_frame_rate_to_string` (two-stage).
	pub fn oakcommon_videoparams_frame_rate_to_string(
		numerator: c_int,
		denominator: c_int,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		if !is_valid_string_out(buf, buf_size) {
			return OAKCOMMON_E_INVALID;
		}
		match VideoParams::frame_rate_to_string(numerator, denominator) {
			Ok(s) => copy_string(&s, buf, buf_size),
			Err(e) => e.code(),
		}
	}

	/// `oakcommon_videoparams_get_name_for_divider` (two-stage).
	pub fn oakcommon_videoparams_get_name_for_divider(
		divider: c_int,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		if !is_valid_string_out(buf, buf_size) {
			return OAKCOMMON_E_INVALID;
		}
		match VideoParams::name_for_divider(divider) {
			Ok(s) => copy_string(&s, buf, buf_size),
			Err(e) => e.code(),
		}
	}

	/// `oakcommon_videoparams_get_scaled_dimension` (static).
	pub fn oakcommon_videoparams_get_scaled_dimension(dimension: c_int, divider: c_int) -> c_int {
		VideoParams::get_scaled_dimension(dimension, divider)
	}

	/// `oakcommon_videoparams_generate_auto_divider` (static).
	pub fn oakcommon_videoparams_generate_auto_divider(width: i64, height: i64) -> c_int {
		VideoParams::generate_auto_divider(width, height)
	}

	/// `oakcommon_videoparams_get_divider_for_target_resolution` (static).
	pub fn oakcommon_videoparams_get_divider_for_target_resolution(
		src_width: c_int,
		src_height: c_int,
		target_width: c_int,
		target_height: c_int,
	) -> c_int {
		VideoParams::get_divider_for_target_resolution(src_width, src_height, target_width, target_height)
	}

	/// `oakcommon_videoparams_get_bytes_per_channel_for_format` (static).
	pub fn oakcommon_videoparams_get_bytes_per_channel_for_format(pixel_format: c_int) -> c_int {
		VideoParams::bytes_per_channel_for_format(PixelFormat::from_code(pixel_format))
	}

	/// `oakcommon_videoparams_get_bytes_per_pixel_for_format` (static).
	pub fn oakcommon_videoparams_get_bytes_per_pixel_for_format(
		pixel_format: c_int,
		channels: c_int,
	) -> c_int {
		VideoParams::bytes_per_pixel_for_format(PixelFormat::from_code(pixel_format), channels)
	}

	/// `oakcommon_videoparams_calculate_buffer_size` (static).
	pub fn oakcommon_videoparams_calculate_buffer_size(
		width: c_int,
		height: c_int,
		pixel_format: c_int,
		channels: c_int,
	) -> c_int {
		VideoParams::calculate_buffer_size(width, height, PixelFormat::from_code(pixel_format), channels)
	}

	/// `oakcommon_videoparams_static_get_bytes_per_pixel` (static).
	pub fn oakcommon_videoparams_static_get_bytes_per_pixel(
		pixel_format: c_int,
		channels: c_int,
	) -> c_int {
		VideoParams::bytes_per_pixel_for_format(PixelFormat::from_code(pixel_format), channels)
	}

	// ---- colortransform (value-typed ColorTransform boxed as CHandle) ----

	/// `oakcommon_colortransform_init_output` — boxes
	/// `ColorTransform::new_output`.
	pub unsafe fn oakcommon_colortransform_init_output(output: *const c_char) -> CHandle {
		// SAFETY: the caller guarantees a valid NUL-terminated string.
		let output = unsafe { read_cstr(output) };
		make_owned(ColorTransform::new_output(&output))
	}

	/// `oakcommon_colortransform_init_display` — boxes
	/// `ColorTransform::new_display`.
	pub unsafe fn oakcommon_colortransform_init_display(
		display: *const c_char,
		view: *const c_char,
		look: *const c_char,
	) -> CHandle {
		// SAFETY: the caller guarantees valid NUL-terminated strings.
		unsafe {
			let display = read_cstr(display);
			let view = read_cstr(view);
			let look = read_cstr(look);
			make_owned(ColorTransform::new_display(&display, &view, &look))
		}
	}

	/// `oakcommon_colortransform_free` — release one reference.
	pub fn oakcommon_colortransform_free(transform: *mut CHandle) {
		free_handle(transform);
	}

	/// `oakcommon_colortransform_is_display` — 1/0.
	pub fn oakcommon_colortransform_is_display(transform: CHandle) -> c_int {
		if transform.is_null() {
			return 0;
		}
		// SAFETY: `transform` must be a live `make_owned(ColorTransform)` handle.
		match unsafe { get::<ColorTransform>(&transform) } {
			Some(t) => t.is_display() as c_int,
			None => 0,
		}
	}

	/// `oakcommon_colortransform_get_display` (two-stage string getter).
	pub fn oakcommon_colortransform_get_display(
		transform: CHandle,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		if transform.is_null() || !is_valid_string_out(buf, buf_size) {
			return OAKCOMMON_E_INVALID;
		}
		// SAFETY: `transform` must be a live `make_owned(ColorTransform)` handle.
		match unsafe { get::<ColorTransform>(&transform) } {
			Some(t) => copy_string(t.display(), buf, buf_size),
			None => OAKCOMMON_E_INVALID,
		}
	}

	/// `oakcommon_colortransform_get_output` (two-stage string getter).
	pub fn oakcommon_colortransform_get_output(
		transform: CHandle,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		if transform.is_null() || !is_valid_string_out(buf, buf_size) {
			return OAKCOMMON_E_INVALID;
		}
		// SAFETY: `transform` must be a live `make_owned(ColorTransform)` handle.
		match unsafe { get::<ColorTransform>(&transform) } {
			Some(t) => copy_string(t.output(), buf, buf_size),
			None => OAKCOMMON_E_INVALID,
		}
	}

	/// `oakcommon_colortransform_get_view` (two-stage string getter).
	pub fn oakcommon_colortransform_get_view(
		transform: CHandle,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		if transform.is_null() || !is_valid_string_out(buf, buf_size) {
			return OAKCOMMON_E_INVALID;
		}
		// SAFETY: `transform` must be a live `make_owned(ColorTransform)` handle.
		match unsafe { get::<ColorTransform>(&transform) } {
			Some(t) => copy_string(t.view(), buf, buf_size),
			None => OAKCOMMON_E_INVALID,
		}
	}

	/// `oakcommon_colortransform_get_look` (two-stage string getter).
	pub fn oakcommon_colortransform_get_look(
		transform: CHandle,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		if transform.is_null() || !is_valid_string_out(buf, buf_size) {
			return OAKCOMMON_E_INVALID;
		}
		// SAFETY: `transform` must be a live `make_owned(ColorTransform)` handle.
		match unsafe { get::<ColorTransform>(&transform) } {
			Some(t) => copy_string(t.look(), buf, buf_size),
			None => OAKCOMMON_E_INVALID,
		}
	}
}

// ===========================================================================
// codec — direct Rust shims over the oakcodec crate
// ===========================================================================

/// oakcodec bridge replacements: direct Rust calls into the `oakcodec`
/// crate's export tables (single-lib unification). Ported from the
/// deleted `oakcodec/src/ffi/format.rs` and `ffi/encoder.rs`.
pub mod codec {
	use std::ffi::{c_char, c_int};

	use oakcodec::error::{OAKCODEC_E_INVALID, OAKCODEC_E_NOT_FOUND};
	use oakcodec::exportcodec::Codec;
	use oakcodec::exportformat::Format;

	/// Standard two-stage getter copy: copy only when the buffer is large
	/// enough (never truncates); always return the required size incl. NUL.
	fn string_out(s: &str, buf: *mut c_char, buf_size: c_int) -> c_int {
		let need = s.len() as c_int + 1;
		if !buf.is_null() && buf_size > 0 {
			let n = (s.len() as c_int).min(buf_size - 1);
			// SAFETY: the caller guarantees `buf` holds `buf_size` bytes.
			unsafe {
				std::ptr::copy_nonoverlapping(s.as_ptr() as *const c_char, buf, n as usize);
				*buf.add(n as usize) = 0;
			}
		}
		need
	}

	/// Read a NUL-terminated C string; `None` on NULL pointers.
	unsafe fn c_str(ptr: *const c_char) -> Option<String> {
		// SAFETY: `ptr` must be a valid NUL-terminated C string, or NULL.
		if ptr.is_null() {
			return None;
		}
		unsafe { Some(std::ffi::CStr::from_ptr(ptr).to_string_lossy().into_owned()) }
	}

	/// `oakcodec_encoding_format_count` — `ExportFormat::k_format_count`.
	pub fn oakcodec_encoding_format_count() -> c_int {
		Format::Count as c_int
	}

	/// `oakcodec_encoding_format_name` (two-stage).
	pub unsafe fn oakcodec_encoding_format_name(
		format: c_int,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		match Format::from_i32(format) {
			Some(f) => string_out(&Format::get_name(f), buf, buf_size),
			None => OAKCODEC_E_INVALID,
		}
	}

	/// `oakcodec_encoding_format_extension` (two-stage).
	pub unsafe fn oakcodec_encoding_format_extension(
		format: c_int,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		match Format::from_i32(format) {
			Some(f) => string_out(&Format::get_extension(f), buf, buf_size),
			None => OAKCODEC_E_INVALID,
		}
	}

	/// `oakcodec_encoding_format_video_codec_count`.
	pub fn oakcodec_encoding_format_video_codec_count(format: c_int) -> c_int {
		match Format::from_i32(format) {
			Some(f) => Format::get_video_codecs(f).len() as c_int,
			None => OAKCODEC_E_INVALID,
		}
	}

	/// `oakcodec_encoding_format_video_codec_at`.
	pub fn oakcodec_encoding_format_video_codec_at(format: c_int, index: c_int) -> c_int {
		let f = match Format::from_i32(format) {
			Some(f) => f,
			None => return OAKCODEC_E_INVALID,
		};
		let list = Format::get_video_codecs(f);
		if index < 0 || index as usize >= list.len() {
			return OAKCODEC_E_NOT_FOUND;
		}
		list[index as usize] as c_int
	}

	/// `oakcodec_encoding_format_audio_codec_count`.
	pub fn oakcodec_encoding_format_audio_codec_count(format: c_int) -> c_int {
		match Format::from_i32(format) {
			Some(f) => Format::get_audio_codecs(f).len() as c_int,
			None => OAKCODEC_E_INVALID,
		}
	}

	/// `oakcodec_encoding_format_audio_codec_at`.
	pub fn oakcodec_encoding_format_audio_codec_at(format: c_int, index: c_int) -> c_int {
		let f = match Format::from_i32(format) {
			Some(f) => f,
			None => return OAKCODEC_E_INVALID,
		};
		let list = Format::get_audio_codecs(f);
		if index < 0 || index as usize >= list.len() {
			return OAKCODEC_E_NOT_FOUND;
		}
		list[index as usize] as c_int
	}

	/// `oakcodec_encoding_format_subtitle_codec_count`.
	pub fn oakcodec_encoding_format_subtitle_codec_count(format: c_int) -> c_int {
		match Format::from_i32(format) {
			Some(f) => Format::get_subtitle_codecs(f).len() as c_int,
			None => OAKCODEC_E_INVALID,
		}
	}

	/// `oakcodec_encoding_format_subtitle_codec_at`.
	pub fn oakcodec_encoding_format_subtitle_codec_at(format: c_int, index: c_int) -> c_int {
		let f = match Format::from_i32(format) {
			Some(f) => f,
			None => return OAKCODEC_E_INVALID,
		};
		let list = Format::get_subtitle_codecs(f);
		if index < 0 || index as usize >= list.len() {
			return OAKCODEC_E_NOT_FOUND;
		}
		list[index as usize] as c_int
	}

	/// `oakcodec_encoding_codec_name` (two-stage).
	pub unsafe fn oakcodec_encoding_codec_name(
		codec: c_int,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		match Codec::from_i32(codec) {
			Some(c) => string_out(&Codec::get_codec_name(c), buf, buf_size),
			None => OAKCODEC_E_INVALID,
		}
	}

	/// `oakcodec_encoding_codec_is_still_image` (0 for an invalid codec).
	pub fn oakcodec_encoding_codec_is_still_image(codec: c_int) -> c_int {
		match Codec::from_i32(codec) {
			Some(c) => Codec::is_codec_a_still_image(c) as c_int,
			None => 0,
		}
	}

	/// `oakcodec_encoding_codec_is_lossless` (0 for an invalid codec).
	pub fn oakcodec_encoding_codec_is_lossless(codec: c_int) -> c_int {
		match Codec::from_i32(codec) {
			Some(c) => Codec::is_codec_lossless(c) as c_int,
			None => 0,
		}
	}

	/// `oakcodec_encoding_pix_fmt_count`.
	///
	/// # CPP-PARITY
	/// The Rust table is empty (see
	/// `Format::get_pixel_formats_for_codec`), so the count is 0 — the same
	/// as the C++ base `Encoder` default.
	pub fn oakcodec_encoding_pix_fmt_count(format: c_int, codec: c_int) -> c_int {
		let (f, c) = match (Format::from_i32(format), Codec::from_i32(codec)) {
			(Some(f), Some(c)) => (f, c),
			_ => return OAKCODEC_E_INVALID,
		};
		Format::get_pixel_formats_for_codec(f, c).len() as c_int
	}

	/// `oakcodec_encoding_pix_fmt_at` (two-stage).
	pub unsafe fn oakcodec_encoding_pix_fmt_at(
		format: c_int,
		codec: c_int,
		index: c_int,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		let (f, c) = match (Format::from_i32(format), Codec::from_i32(codec)) {
			(Some(f), Some(c)) => (f, c),
			_ => return OAKCODEC_E_INVALID,
		};
		let list = Format::get_pixel_formats_for_codec(f, c);
		if index < 0 || index as usize >= list.len() {
			return OAKCODEC_E_NOT_FOUND;
		}
		// Interim: the Rust list carries no names yet, so this arm is
		// unreachable while the list is empty (C++ queries the FFmpeg bridge).
		string_out(&list[index as usize].to_string(), buf, buf_size)
	}

	/// `oakcodec_encoding_pix_fmt_index` — 0 (the preferred format) for an
	/// invalid codec, a NULL/empty `pix_fmt`, or when not found.
	pub unsafe fn oakcodec_encoding_pix_fmt_index(codec: c_int, pix_fmt: *const c_char) -> c_int {
		// SAFETY: `pix_fmt` is a valid NUL-terminated C string or NULL.
		unsafe {
			if Codec::from_i32(codec).is_none() {
				return 0;
			}
			match c_str(pix_fmt) {
				Some(s) if !s.is_empty() => {
					// Interim: empty table (see module doc) -> preferred index 0.
					let _ = s;
					0
				}
				_ => 0,
			}
		}
	}

	/// `oakcodec_encoding_sample_format_count`.
	pub fn oakcodec_encoding_sample_format_count(format: c_int, codec: c_int) -> c_int {
		let (f, c) = match (Format::from_i32(format), Codec::from_i32(codec)) {
			(Some(f), Some(c)) => (f, c),
			_ => return OAKCODEC_E_INVALID,
		};
		Format::get_sample_formats_for_codec(f, c).len() as c_int
	}

	/// `oakcodec_encoding_sample_format_at` — an
	/// `olive::core::SampleFormat::Format` value.
	pub fn oakcodec_encoding_sample_format_at(
		format: c_int,
		codec: c_int,
		index: c_int,
	) -> c_int {
		let (f, c) = match (Format::from_i32(format), Codec::from_i32(codec)) {
			(Some(f), Some(c)) => (f, c),
			_ => return OAKCODEC_E_INVALID,
		};
		let list = Format::get_sample_formats_for_codec(f, c);
		if index < 0 || index as usize >= list.len() {
			return OAKCODEC_E_NOT_FOUND;
		}
		list[index as usize] as c_int
	}

	/// `oakcodec_encoding_filename_contains_digit_placeholder` (0 for NULL).
	pub unsafe fn oakcodec_encoding_filename_contains_digit_placeholder(
		filename: *const c_char,
	) -> c_int {
		// SAFETY: `filename` is a valid NUL-terminated C string or NULL.
		match unsafe { c_str(filename) } {
			Some(f) => oakcodec::encoder::filename_contains_digit_placeholder(&f) as c_int,
			None => 0,
		}
	}

	/// `oakcodec_encoding_image_sequence_digit_count` (0 for NULL).
	pub unsafe fn oakcodec_encoding_image_sequence_digit_count(filename: *const c_char) -> c_int {
		// SAFETY: `filename` is a valid NUL-terminated C string or NULL.
		match unsafe { c_str(filename) } {
			Some(f) => oakcodec::encoder::image_sequence_placeholder_digit_count(&f),
			None => 0,
		}
	}

	/// `oakcodec_encoding_filename_remove_digit_placeholder` (two-stage).
	pub unsafe fn oakcodec_encoding_filename_remove_digit_placeholder(
		filename: *const c_char,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		// SAFETY: `filename` is a valid NUL-terminated C string or NULL
		// (NULL is the documented E_INVALID path, matching the deleted
		// module ffi).
		match unsafe { c_str(filename) } {
			Some(f) => {
				string_out(&oakcodec::encoder::filename_remove_digit_placeholder(&f), buf, buf_size)
			}
			None => OAKCODEC_E_INVALID,
		}
	}

	/// `oakcodec_encoding_generate_matrix`: scaling matrix for a scaling
	/// method, row-major 4x4 `double` into `out_matrix[16]`.
	pub fn oakcodec_encoding_generate_matrix(
		method: c_int,
		src_width: c_int,
		src_height: c_int,
		dst_width: c_int,
		dst_height: c_int,
		out_matrix: *mut f64,
	) -> c_int {
		if out_matrix.is_null() {
			return OAKCODEC_E_INVALID;
		}
		let method = match method {
			0 => oakcodec::encodingparams::VideoScalingMethod::Fit,
			2 => oakcodec::encodingparams::VideoScalingMethod::Crop,
			_ => oakcodec::encodingparams::VideoScalingMethod::Stretch,
		};
		let mut m = [0.0f64; 16];
		oakcodec::encodingparams::EncodingParams::generate_matrix(
			method,
			src_width,
			src_height,
			dst_width,
			dst_height,
			&mut m,
		);
		// SAFETY: the caller guarantees `out_matrix` holds 16 doubles.
		unsafe { std::ptr::copy_nonoverlapping(m.as_ptr(), out_matrix, 16) };
		0
	}
}

// ===========================================================================
// node — engine-side oaknode domain implementation (single-lib unification)
// ===========================================================================
//
// The deleted oaknode C ABI is replaced by direct calls into the oaknode
// crate's Rust domain. The engine's handle layer boxes the domain behind
// the upward CHandles (see `crate::handle::domain`):
//
// - project handles box `Arc<Mutex<oaknode::project::Project>>`;
// - node/block/track/footage/sequence/folder handles box
//   `oaknode::project::NodeRef` (project + NodeId).
//
// Undoable creators return handles boxing
// `oakundo::undocommand::UndoCommand` values (built from redo/undo
// closures over the real graph). Return-code convention: 0 on success,
// negative `oaknode::error::OAKNODE_*` codes on failure; two-stage string
// getters report the required length **including** the NUL.
pub mod node {
	use std::collections::HashMap;
	use std::ffi::{c_char, c_int, c_void};
	use std::sync::atomic::{AtomicI64, Ordering};
	use std::sync::{Arc, Mutex, OnceLock, Weak};

	use oakcore_rs::{Rational, TimeRange};
	use oaknode::error::{
		OAKNODE_E_FAILED, OAKNODE_E_INVALID, OAKNODE_E_NOMEM, OAKNODE_E_NOT_FOUND, OAKNODE_E_STATE,
		OAKNODE_OK,
	};
	use oaknode::factory::Factory;
	use oaknode::graph::Graph;
	use oaknode::id::NodeId;
	use oakundo::undocommand::{command_from_owned, OakUndoCommandVtable, UndoCommand};

	use crate::handle::domain::{box_node, node_ref_mut, node_ref_of, project_of, ProjectArc};
	use crate::handle::CHandle;

	// -------------------------------------------------------------------
	// Shared helpers
	// -------------------------------------------------------------------

	fn lock<T>(m: &Mutex<T>) -> std::sync::MutexGuard<'_, T> {
		m.lock().unwrap_or_else(|e| e.into_inner())
	}

	/// Standard two-stage getter copy: copy only when the buffer is large
	/// enough (never truncates); always return the required size incl. NUL.
	fn string_out(s: &str, buf: *mut c_char, buf_size: c_int) -> c_int {
		let required = (s.len() + 1) as c_int;
		if !buf.is_null() && buf_size >= required {
			// SAFETY: the caller guarantees `buf` holds `buf_size` bytes.
			unsafe {
				std::ptr::copy_nonoverlapping(s.as_ptr() as *const c_char, buf, s.len());
				*buf.add(s.len()) = 0;
			}
		}
		required
	}

	/// Read a NUL-terminated C string; NULL -> empty.
	unsafe fn cstr(ptr: *const c_char) -> String {
		// SAFETY: the caller guarantees a valid NUL-terminated string.
		unsafe { crate::handle::read_cstr(ptr) }
	}

	/// A handle's `addref` copy (borrowed-view semantics: every facade
	/// "borrowed" handle is an owned copy with refcount 1 that the caller
	/// releases).
	fn addref_copy(h: CHandle) -> CHandle {
		if h.is_null() {
			return CHandle::null();
		}
		if let Some(addref) = h.addref {
			// SAFETY: `h` is a live handle.
			unsafe { addref(h.ctx) };
		}
		h
	}

	/// Node identity (the graph slot identity; 0 for invalid handles).
	fn node_identity(h: CHandle) -> usize {
		// SAFETY: node handles box NodeRef payloads.
		match unsafe { node_ref_of(&h) } {
			Some(nr) => nr.id.identity() as usize,
			None => 0,
		}
	}

	/// Read-only project access.
	fn with_project<R>(h: CHandle, f: impl FnOnce(&oaknode::project::Project) -> R) -> Option<R> {
		// SAFETY: project handles box ProjectArc payloads.
		let p = unsafe { project_of(&h) }?;
		Some(f(&lock(p)))
	}

	/// Mutable project access.
	fn with_project_mut<R>(
		h: CHandle,
		f: impl FnOnce(&mut oaknode::project::Project) -> R,
	) -> Option<R> {
		// SAFETY: project handles box ProjectArc payloads.
		let p = unsafe { project_of(&h) }?;
		Some(f(&mut lock(p)))
	}

	/// Read-only graph access for a node handle.
	fn with_node<R>(h: CHandle, f: impl FnOnce(&Graph, NodeId) -> R) -> Option<R> {
		// SAFETY: node handles box NodeRef payloads.
		let nr = unsafe { node_ref_of(&h) }?;
		let p = lock(&nr.project);
		Some(f(&p.graph, nr.id))
	}

	/// Mutable graph access for a node handle.
	fn with_node_mut<R>(h: CHandle, f: impl FnOnce(&mut Graph, NodeId) -> R) -> Option<R> {
		// SAFETY: node handles box NodeRef payloads.
		let nr = unsafe { node_ref_of(&h) }?;
		let mut p = lock(&nr.project);
		Some(f(&mut p.graph, nr.id))
	}

	/// Typed behavior view of a graph node.
	fn behavior_of<'a, T: 'static>(g: &'a Graph, id: NodeId) -> Option<&'a T> {
		g.get(id)
			.and_then(|e| e.behavior.as_any())
			.and_then(|a| a.downcast_ref::<T>())
	}

	/// Typed mutable behavior view of a graph node.
	fn behavior_of_mut<'a, T: 'static>(g: &'a mut Graph, id: NodeId) -> Option<&'a mut T> {
		g.get_mut(id)
			.and_then(|e| e.behavior.as_any_mut())
			.and_then(|a| a.downcast_mut::<T>())
	}

	/// The node's type id (empty when invalid).
	fn type_id_of(h: CHandle) -> String {
		match with_node(h, |g, id| {
			g.get(id).map(|e| e.behavior.type_id().to_string())
		}) {
			Some(Some(s)) => s,
			_ => String::new(),
		}
	}

	fn is_type(h: CHandle, id: &str) -> bool {
		type_id_of(h) == id
	}

	// ---- Debug alive counter (the deleted `oaknode_debug_alive_count`) ----

	/// Live detached nodes + live projects (factory-created nodes that
	/// were not adopted by a project graph, plus projects themselves).
	static ALIVE: AtomicI64 = AtomicI64::new(0);

	fn alive_inc() {
		ALIVE.fetch_add(1, Ordering::SeqCst);
	}

	fn alive_dec() {
		ALIVE.fetch_sub(1, Ordering::SeqCst);
	}

	/// Weak references to every alive-counted project (for the release
	/// bookkeeping below).
	static ALIVE_REGISTRY: OnceLock<
		Mutex<HashMap<usize, Weak<Mutex<oaknode::project::Project>>>>,
	> = OnceLock::new();

	fn registry() -> std::sync::MutexGuard<
		'static,
		HashMap<usize, Weak<Mutex<oaknode::project::Project>>>,
	> {
		ALIVE_REGISTRY
			.get_or_init(|| Mutex::new(HashMap::new()))
			.lock()
			.unwrap_or_else(|e| e.into_inner())
	}

	/// Release callback for project payload boxes (refcounted shell +
	/// alive-counter bookkeeping).
	unsafe extern "C" fn release_project_box(ctx: *mut c_void) {
		// SAFETY: `ctx` was produced by `make_project_handle` and this
		// callback runs once per shell reference.
		unsafe {
			if ctx.is_null() {
				return;
			}
			let rb = ctx as *mut oaknode::handle::RefBox<ProjectArc>;
			if (*rb).refs.fetch_sub(1, Ordering::AcqRel) == 1 {
				let value = Box::from_raw(rb).value;
				let key = Arc::as_ptr(&value) as usize;
				drop(value);
				// Last strong reference: retire the alive-count entry.
				let dead = match registry().get(&key) {
					Some(weak) => weak.upgrade().is_none(),
					None => false,
				};
				if dead {
					registry().remove(&key);
					alive_dec();
				}
			}
		}
	}

	/// Box a project payload, registering it in the alive registry.
	fn make_project_handle(project: ProjectArc) -> CHandle {
		let key = Arc::as_ptr(&project) as usize;
		registry().entry(key).or_insert_with(|| {
			alive_inc();
			Arc::downgrade(&project)
		});
		// SAFETY: custom release callback for the payload box.
		unsafe { oaknode::handle::make_owned_with(project, release_project_box) }
	}

	/// Box a node reference behind a refcounted handle (detached-node
	/// accounting rides on the payload's `owned` flag).
	fn make_node_handle(project: ProjectArc, id: NodeId, owned: bool) -> CHandle {
		box_node(project, id, owned)
	}

	/// Box a project payload for the engine's other stub families (the
	/// task module's result paths).
	pub(crate) fn box_project_handle(project: ProjectArc) -> CHandle {
		make_project_handle(project)
	}

	/// Box a node reference for the engine's other stub families (the
	/// task module's footage result paths).
	pub(crate) fn box_node_handle(project: ProjectArc, id: NodeId, owned: bool) -> CHandle {
		make_node_handle(project, id, owned)
	}

	/// The hidden scratch project holding detached (factory-created)
	/// nodes (the C++ `memory_manager_` analogue; leaked for the process).
	fn scratch_project() -> ProjectArc {
		static SCRATCH: OnceLock<ProjectArc> = OnceLock::new();
		SCRATCH
			.get_or_init(|| oaknode::project::Project::new())
			.clone()
	}

	/// Create a detached node in the scratch project (owned = true,
	/// alive-counted).
	fn make_detached(
		(core, behavior): (oaknode::node::NodeCore, Box<dyn oaknode::node::NodeBehavior>),
	) -> CHandle {
		let project = scratch_project();
		let id = {
			let mut p = lock(&project);
			p.graph.add_node(core, behavior)
		};
		alive_inc();
		make_node_handle(project, id, true)
	}

	/// Release a handle shell reference.
	fn release_handle(h: CHandle) {
		if let Some(release) = h.release {
			// SAFETY: the handle is live and this is the caller's
			// reference.
			unsafe { release(h.ctx) };
		}
	}

	/// Free a node handle shell; a still-owned (detached) node is removed
	/// from its scratch graph and un-counted. Adopted nodes live in their
	/// project graph (the graph owns them).
	fn free_node_handle(h: CHandle) {
		// SAFETY: node handles box NodeRef payloads.
		let owned = unsafe { node_ref_of(&h) }
			.map(|nr| nr.owned.load(Ordering::SeqCst))
			.unwrap_or(false);
		if owned {
			let removed = unsafe { node_ref_of(&h) }.and_then(|nr| {
				let mut p = lock(&nr.project);
				p.graph.remove_node(nr.id).map(|_| ())
			});
			if removed.is_some() {
				alive_dec();
			}
		}
		release_handle(h);
	}

	// ---- Undo-command scaffolding --------------------------------------

	/// Userdata payload behind a closure-backed undo command.
	struct ClosureCommand {
		redo: Box<dyn FnMut() + Send>,
		undo: Box<dyn FnMut() + Send>,
	}

	unsafe extern "C" fn closure_redo(ud: *mut c_void) {
		// SAFETY: `ud` is the `ClosureCommand` box owned by the command.
		let c = unsafe { &mut *(ud as *mut ClosureCommand) };
		(c.redo)();
	}

	unsafe extern "C" fn closure_undo(ud: *mut c_void) {
		// SAFETY: see `closure_redo`.
		let c = unsafe { &mut *(ud as *mut ClosureCommand) };
		(c.undo)();
	}

	unsafe extern "C" fn closure_free(ud: *mut c_void) {
		if !ud.is_null() {
			// SAFETY: the box is destroyed exactly once, by the command.
			unsafe { drop(Box::from_raw(ud as *mut ClosureCommand)) };
		}
	}

	/// Build an un-executed [`UndoCommand`] from redo/undo closures.
	fn closure_command(
		redo: impl FnMut() + Send + 'static,
		undo: impl FnMut() + Send + 'static,
	) -> UndoCommand {
		let ud = Box::into_raw(Box::new(ClosureCommand {
			redo: Box::new(redo),
			undo: Box::new(undo),
		}));
		UndoCommand::from_vtable(
			OakUndoCommandVtable {
				redo: Some(closure_redo),
				undo: Some(closure_undo),
				free_fn: Some(closure_free),
			},
			ud as *mut c_void,
		)
	}

	/// Box an [`UndoCommand`] value behind a handle.
	fn box_command(cmd: UndoCommand) -> CHandle {
		// SAFETY: `command_from_owned` owns the command value.
		unsafe { command_from_owned(cmd) }
	}

	/// Create a multi command handle from children.
	fn box_multi(children: Vec<UndoCommand>) -> CHandle {
		let mut multi = UndoCommand::multi();
		for c in children {
			multi.multi_add_child(c);
		}
		box_command(multi)
	}

	// ---- Value conversions ----------------------------------------------

	/// Map an oaknode domain `VideoParams` into an oakcommon params
	/// handle (the engine's `stubs::common` videoparams surface).
	fn vp_handle(v: &oaknode::value::VideoParams) -> CHandle {
		let h = crate::stubs::common::oakcommon_videoparams_init();
		if h.is_null() {
			return CHandle::null();
		}
		crate::stubs::common::oakcommon_videoparams_set_width(h, v.width);
		crate::stubs::common::oakcommon_videoparams_set_height(h, v.height);
		crate::stubs::common::oakcommon_videoparams_set_format(h, v.pixel_format);
		crate::stubs::common::oakcommon_videoparams_set_channel_count(h, v.channels);
		let (n, d) = (
			v.frame_rate.numerator() as c_int,
			v.frame_rate.denominator() as c_int,
		);
		crate::stubs::common::oakcommon_videoparams_set_frame_rate(h, n, d);
		if n > 0 {
			crate::stubs::common::oakcommon_videoparams_set_time_base(h, d, n);
		}
		h
	}

	/// Read an oakcommon params handle back into the domain type.
	unsafe fn vp_from_handle(h: CHandle) -> Option<oaknode::value::VideoParams> {
		let mut width: c_int = 0;
		let mut height: c_int = 0;
		let mut format: c_int = 0;
		let mut channels: c_int = 0;
		let mut fr_num: c_int = 0;
		let mut fr_den: c_int = 0;
		if crate::stubs::common::oakcommon_videoparams_get_width(h, &mut width) != 0
			|| crate::stubs::common::oakcommon_videoparams_get_height(h, &mut height) != 0
			|| crate::stubs::common::oakcommon_videoparams_get_format(h, &mut format) != 0
			|| crate::stubs::common::oakcommon_videoparams_get_channel_count(h, &mut channels) != 0
			|| crate::stubs::common::oakcommon_videoparams_get_frame_rate(
				h,
				&mut fr_num,
				&mut fr_den,
			) != 0
		{
			return None;
		}
		Some(oaknode::value::VideoParams {
			width,
			height,
			frame_rate: Rational::new(fr_num as i64, fr_den as i64),
			pixel_format: format,
			channels,
		})
	}

	/// POD -> domain value (using the input's declared type).
	fn pod_to_value(
		declared: oaknode::value::ValueType,
		v: crate::node::OakNodeValue,
	) -> Option<oaknode::value::NodeValue> {
		v.to_node_value(declared).ok()
	}

	/// Domain value -> POD (using the input's declared type).
	fn value_to_pod(
		declared: oaknode::value::ValueType,
		v: &oaknode::value::NodeValue,
	) -> Option<crate::node::OakNodeValue> {
		crate::node::OakNodeValue::from_node_value(declared, v).ok()
	}
	// -------------------------------------------------------------------
	// Project family
	// -------------------------------------------------------------------

	/// `oaknode_project_init` — fresh uninitialized project box.
	pub fn oaknode_project_init() -> CHandle {
		make_project_handle(oaknode::project::Project::new())
	}

	/// `oaknode_project_free` — release one project-handle reference.
	pub fn oaknode_project_free(project: *mut CHandle) {
		if project.is_null() {
			return;
		}
		// SAFETY: the caller passes a valid handle pointer.
		let h = unsafe { *project };
		release_handle(h);
		// SAFETY: the caller passes a valid handle pointer.
		unsafe { *project = CHandle::null() };
	}

	/// `oaknode_project_initialize` — create the root folder.
	pub fn oaknode_project_initialize(project: CHandle) -> c_int {
		match with_project_mut(project, |p| p.initialize()) {
			Some(Ok(())) => OAKNODE_OK,
			Some(Err(e)) => e.code(),
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_project_clear` — remove all nodes, reset to blank.
	pub fn oaknode_project_clear(project: CHandle) -> c_int {
		match with_project_mut(project, |p| p.clear()) {
			Some(Ok(())) => OAKNODE_OK,
			Some(Err(e)) => e.code(),
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_project_root` — borrowed root folder handle (null until
	/// initialized).
	pub fn oaknode_project_root(project: CHandle) -> CHandle {
		let p = match unsafe { project_of(&project) }.cloned() {
			Some(p) => p,
			None => return CHandle::null(),
		};
		let root = {
			let g = lock(&p);
			g.root
		};
		if root.valid() {
			make_node_handle(p, root, false)
		} else {
			CHandle::null()
		}
	}

	/// `oaknode_project_name` (two-stage).
	pub fn oaknode_project_name(project: CHandle, buf: *mut c_char, buf_size: c_int) -> c_int {
		match with_project(project, |p| p.name()) {
			Some(name) => string_out(&name, buf, buf_size),
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_project_filename` (two-stage).
	pub fn oaknode_project_filename(project: CHandle, buf: *mut c_char, buf_size: c_int) -> c_int {
		match with_project(project, |p| p.filename().to_string()) {
			Some(name) => string_out(&name, buf, buf_size),
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_project_pretty_filename` (two-stage).
	pub fn oaknode_project_pretty_filename(
		project: CHandle,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		match with_project(project, |p| p.pretty_filename().to_string()) {
			Some(name) => string_out(&name, buf, buf_size),
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_project_set_filename`.
	pub fn oaknode_project_set_filename(project: CHandle, filename: *const c_char) -> c_int {
		// SAFETY: the caller guarantees a valid NUL-terminated string.
		let filename = unsafe { cstr(filename) };
		match with_project_mut(project, |p| p.set_filename(&filename)) {
			Some(()) => OAKNODE_OK,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_project_is_modified` — 1/0.
	pub fn oaknode_project_is_modified(project: CHandle) -> c_int {
		match with_project(project, |p| p.is_modified()) {
			Some(true) => 1,
			Some(false) => 0,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_project_set_modified`.
	pub fn oaknode_project_set_modified(project: CHandle, modified: c_int) -> c_int {
		match with_project_mut(project, |p| p.set_modified(modified != 0)) {
			Some(()) => OAKNODE_OK,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_project_is_new` — 1/0.
	pub fn oaknode_project_is_new(project: CHandle) -> c_int {
		match with_project(project, |p| p.is_new()) {
			Some(true) => 1,
			Some(false) => 0,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_project_cache_path` (two-stage).
	pub fn oaknode_project_cache_path(
		project: CHandle,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		match with_project(project, |p| p.cache_path()) {
			Some(path) => string_out(&path, buf, buf_size),
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_project_copy_settings`.
	pub fn oaknode_project_copy_settings(dst: CHandle, src: CHandle) -> c_int {
		let src_proj = unsafe { project_of(&src) }.cloned();
		match src_proj {
			Some(src_proj) => {
				let (settings, location, custom) = {
					let s = lock(&src_proj);
					(
						s.settings.clone(),
						s.cache_location_setting,
						s.custom_cache_path.clone(),
					)
				};
				match with_project_mut(dst, |d| {
					d.settings = settings;
					d.cache_location_setting = location;
					d.custom_cache_path = custom;
				}) {
					Some(()) => OAKNODE_OK,
					None => OAKNODE_E_INVALID,
				}
			}
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_project_get_cache_location_setting`.
	pub fn oaknode_project_get_cache_location_setting(project: CHandle) -> c_int {
		match with_project(project, |p| p.cache_location_setting) {
			Some(v) => v,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_project_set_cache_location_setting`.
	pub fn oaknode_project_set_cache_location_setting(project: CHandle, setting: c_int) -> c_int {
		match with_project_mut(project, |p| p.cache_location_setting = setting) {
			Some(()) => OAKNODE_OK,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_project_get_custom_cache_path` (two-stage).
	pub fn oaknode_project_get_custom_cache_path(
		project: CHandle,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		match with_project(project, |p| p.custom_cache_path.clone()) {
			Some(path) => string_out(&path, buf, buf_size),
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_project_set_custom_cache_path`.
	pub fn oaknode_project_set_custom_cache_path(project: CHandle, path: *const c_char) -> c_int {
		// SAFETY: the caller guarantees a valid NUL-terminated string.
		let path = unsafe { cstr(path) };
		match with_project_mut(project, |p| p.custom_cache_path = path) {
			Some(()) => OAKNODE_OK,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_project_get_uuid` (two-stage).
	pub fn oaknode_project_get_uuid(project: CHandle, buf: *mut c_char, buf_size: c_int) -> c_int {
		match with_project(project, |p| p.uuid.clone()) {
			Some(uuid) => string_out(&uuid, buf, buf_size),
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_project_add_node` — move `node` into `project`'s graph
	/// (live; the shared node box is rewritten in place so every handle
	/// copy sees the new home — the C++ `write_node_ref` semantics).
	pub fn oaknode_project_add_node(project: CHandle, node: CHandle) -> c_int {
		let target = match unsafe { project_of(&project) }.cloned() {
			Some(t) => t,
			None => return OAKNODE_E_INVALID,
		};
		let nr = match unsafe { node_ref_of(&node) } {
			Some(nr) => nr,
			None => return OAKNODE_E_INVALID,
		};
		let src_project = nr.project.clone();
		let src_id = nr.id;
		// Already in the target graph: nothing to do.
		if Arc::ptr_eq(&src_project, &target) && { lock(&target).graph.is_valid(src_id) } {
			return OAKNODE_OK;
		}
		let entry = {
			let mut s = lock(&src_project);
			s.graph.take_node(src_id)
		};
		let Some(entry) = entry else {
			return OAKNODE_E_NOT_FOUND;
		};
		let new_id = {
			let mut t = lock(&target);
			t.graph.add_entry(entry, src_id)
		};
		// SAFETY: node handles box NodeRef payloads; the box is shared by
		// every handle copy.
		if let Some(boxed) = unsafe { node_ref_mut(&node) } {
			boxed.project = target;
			boxed.id = new_id;
			if boxed.owned.swap(false, Ordering::SeqCst) {
				alive_dec();
			}
		}
		OAKNODE_OK
	}

	/// `oaknode_project_remove_node` — live removal from the graph.
	pub fn oaknode_project_remove_node(project: CHandle, node: CHandle) -> c_int {
		let target = match unsafe { project_of(&project) }.cloned() {
			Some(t) => t,
			None => return OAKNODE_E_INVALID,
		};
		let nr = match unsafe { node_ref_of(&node) } {
			Some(nr) => nr,
			None => return OAKNODE_E_INVALID,
		};
		if !Arc::ptr_eq(&nr.project, &target) {
			return OAKNODE_E_NOT_FOUND;
		}
		let mut t = lock(&target);
		if t.graph.remove_node(nr.id).is_some() {
			OAKNODE_OK
		} else {
			OAKNODE_E_NOT_FOUND
		}
	}

	/// `oaknode_project_node_count`.
	pub fn oaknode_project_node_count(project: CHandle) -> c_int {
		match with_project(project, |p| p.graph.node_count()) {
			Some(n) => n as c_int,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_project_node_at` — borrowed node view at `index` (null
	/// out of range).
	pub fn oaknode_project_node_at(project: CHandle, index: c_int) -> CHandle {
		if index < 0 {
			return CHandle::null();
		}
		let p = match unsafe { project_of(&project) }.cloned() {
			Some(p) => p,
			None => return CHandle::null(),
		};
		let id = {
			let g = lock(&p);
			g.graph.node_ids().get(index as usize).copied()
		};
		match id {
			Some(id) => make_node_handle(p, id, false),
			None => CHandle::null(),
		}
	}

	/// `oaknode_debug_alive_count` — live detached nodes + projects.
	pub fn oaknode_debug_alive_count() -> c_int {
		ALIVE.load(Ordering::SeqCst) as c_int
	}

	// -------------------------------------------------------------------
	// Node family — metadata
	// -------------------------------------------------------------------

	/// `oaknode_node_get_id` (two-stage): the type id.
	pub fn oaknode_node_get_id(node: CHandle, buf: *mut c_char, buf_size: c_int) -> c_int {
		match with_node(node, |g, id| {
			g.get(id).map(|e| e.behavior.type_id().to_string())
		}) {
			Some(Some(s)) => string_out(&s, buf, buf_size),
			Some(None) => OAKNODE_E_NOT_FOUND,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_node_get_name` (two-stage).
	pub fn oaknode_node_get_name(node: CHandle, buf: *mut c_char, buf_size: c_int) -> c_int {
		match with_node(node, |g, id| {
			g.get(id).map(|e| e.behavior.name().to_string())
		}) {
			Some(Some(s)) => string_out(&s, buf, buf_size),
			Some(None) => OAKNODE_E_NOT_FOUND,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_node_get_label` (two-stage).
	pub fn oaknode_node_get_label(node: CHandle, buf: *mut c_char, buf_size: c_int) -> c_int {
		match with_node(node, |g, id| g.get(id).map(|e| e.core.label.clone())) {
			Some(Some(s)) => string_out(&s, buf, buf_size),
			Some(None) => OAKNODE_E_NOT_FOUND,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_node_set_label`.
	pub fn oaknode_node_set_label(node: CHandle, label: *const c_char) -> c_int {
		// SAFETY: the caller guarantees a valid NUL-terminated string.
		let label = unsafe { cstr(label) };
		match with_node_mut(node, |g, id| {
			g.get_mut(id).map(|e| e.core.label = label.clone())
		}) {
			Some(Some(())) => OAKNODE_OK,
			Some(None) => OAKNODE_E_NOT_FOUND,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_node_set_label_undoable` — closure-backed rename.
	pub fn oaknode_node_set_label_undoable(
		node: CHandle,
		label: *const c_char,
		out_command: *mut CHandle,
	) -> c_int {
		// SAFETY: the caller guarantees a valid NUL-terminated string.
		let label = unsafe { cstr(label) };
		if out_command.is_null() {
			return OAKNODE_E_INVALID;
		}
		let nr = match unsafe { node_ref_of(&node) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		let (project, id) = nr;
		let old = match {
			let g = lock(&project);
			g.graph.get(id).map(|e| e.core.label.clone())
		} {
			Some(old) => old,
			None => return OAKNODE_E_NOT_FOUND,
		};
		let p1 = project.clone();
		let p2 = project;
		let label1 = label.clone();
		let old1 = old.clone();
		let cmd = closure_command(
			move || {
				let mut g = lock(&p1);
				if let Some(e) = g.graph.get_mut(id) {
					e.core.label = label1.clone();
				}
			},
			move || {
				let mut g = lock(&p2);
				if let Some(e) = g.graph.get_mut(id) {
					e.core.label = old1.clone();
				}
			},
		);
		// SAFETY: `out_command` is a valid out pointer.
		unsafe { *out_command = box_command(cmd) };
		OAKNODE_OK
	}

	/// `oaknode_node_get_override_color`.
	pub fn oaknode_node_get_override_color(node: CHandle, out_value: *mut c_int) -> c_int {
		if out_value.is_null() {
			return OAKNODE_E_INVALID;
		}
		match with_node(node, |g, id| g.get(id).map(|e| e.core.override_color)) {
			Some(Some(v)) => {
				// SAFETY: valid out pointer.
				unsafe { *out_value = v };
				OAKNODE_OK
			}
			Some(None) => OAKNODE_E_NOT_FOUND,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_node_set_override_color`.
	pub fn oaknode_node_set_override_color(node: CHandle, index: c_int) -> c_int {
		match with_node_mut(node, |g, id| {
			g.get_mut(id).map(|e| e.core.override_color = index)
		}) {
			Some(Some(())) => OAKNODE_OK,
			Some(None) => OAKNODE_E_NOT_FOUND,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_node_set_override_color_undoable`.
	pub fn oaknode_node_set_override_color_undoable(
		node: CHandle,
		index: c_int,
		out_command: *mut CHandle,
	) -> c_int {
		if out_command.is_null() {
			return OAKNODE_E_INVALID;
		}
		let nr = match unsafe { node_ref_of(&node) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		let (project, id) = nr;
		let old = match {
			let g = lock(&project);
			g.graph.get(id).map(|e| e.core.override_color)
		} {
			Some(old) => old,
			None => return OAKNODE_E_NOT_FOUND,
		};
		let p1 = project.clone();
		let p2 = project;
		let cmd = closure_command(
			move || {
				let mut g = lock(&p1);
				if let Some(e) = g.graph.get_mut(id) {
					e.core.override_color = index;
				}
			},
			move || {
				let mut g = lock(&p2);
				if let Some(e) = g.graph.get_mut(id) {
					e.core.override_color = old;
				}
			},
		);
		// SAFETY: valid out pointer.
		unsafe { *out_command = box_command(cmd) };
		OAKNODE_OK
	}

	/// The `enabled_in` boolean (1/0; default true).
	fn node_enabled(g: &Graph, id: NodeId) -> Option<bool> {
		let e = g.get(id)?;
		match e.core.standard_value(oaknode::node::ENABLED_INPUT, -1) {
			oaknode::value::NodeValue::Boolean(b) => Some(b),
			_ => Some(true),
		}
	}

	/// `oaknode_node_is_enabled`.
	pub fn oaknode_node_is_enabled(node: CHandle, out_value: *mut c_int) -> c_int {
		if out_value.is_null() {
			return OAKNODE_E_INVALID;
		}
		match with_node(node, |g, id| node_enabled(g, id)) {
			Some(Some(enabled)) => {
				// SAFETY: valid out pointer.
				unsafe { *out_value = if enabled { 1 } else { 0 } };
				OAKNODE_OK
			}
			Some(None) => OAKNODE_E_NOT_FOUND,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_node_set_enabled`.
	pub fn oaknode_node_set_enabled(node: CHandle, enabled: c_int) -> c_int {
		match with_node_mut(node, |g, id| {
			g.get_mut(id).map(|e| {
				e.core.set_standard_value(
					oaknode::node::ENABLED_INPUT,
					-1,
					oaknode::value::NodeValue::Boolean(enabled != 0),
				)
			})
		}) {
			Some(Some(())) => OAKNODE_OK,
			Some(None) => OAKNODE_E_NOT_FOUND,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_node_set_enabled_undoable`.
	pub fn oaknode_node_set_enabled_undoable(
		node: CHandle,
		enabled: c_int,
		out_command: *mut CHandle,
	) -> c_int {
		if out_command.is_null() {
			return OAKNODE_E_INVALID;
		}
		let nr = match unsafe { node_ref_of(&node) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		let (project, id) = nr;
		let old = match {
			let g = lock(&project);
			node_enabled(&g.graph, id)
		} {
			Some(old) => old,
			None => return OAKNODE_E_NOT_FOUND,
		};
		let p1 = project.clone();
		let p2 = project;
		let cmd = closure_command(
			move || {
				let mut g = lock(&p1);
				if let Some(e) = g.graph.get_mut(id) {
					e.core.set_standard_value(
						oaknode::node::ENABLED_INPUT,
						-1,
						oaknode::value::NodeValue::Boolean(enabled != 0),
					);
				}
			},
			move || {
				let mut g = lock(&p2);
				if let Some(e) = g.graph.get_mut(id) {
					e.core.set_standard_value(
						oaknode::node::ENABLED_INPUT,
						-1,
						oaknode::value::NodeValue::Boolean(old),
					);
				}
			},
		);
		// SAFETY: valid out pointer.
		unsafe { *out_command = box_command(cmd) };
		OAKNODE_OK
	}

	/// `oaknode_node_get_effect_input` (two-stage).
	pub fn oaknode_node_get_effect_input(
		node: CHandle,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		match with_node(node, |g, id| g.get(id).map(|e| e.core.effect_input.clone())) {
			Some(Some(s)) => string_out(&s, buf, buf_size),
			Some(None) => OAKNODE_E_NOT_FOUND,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_node_get_flags`.
	pub fn oaknode_node_get_flags(node: CHandle) -> u64 {
		match with_node(node, |g, id| g.get(id).map(|e| e.core.flags)) {
			Some(Some(f)) => f,
			_ => 0,
		}
	}

	/// `oaknode_node_input_count`.
	pub fn oaknode_node_input_count(node: CHandle, out_count: *mut c_int) -> c_int {
		if out_count.is_null() {
			return OAKNODE_E_INVALID;
		}
		match with_node(node, |g, id| g.get(id).map(|e| e.core.inputs.len())) {
			Some(Some(n)) => {
				// SAFETY: valid out pointer.
				unsafe { *out_count = n as c_int };
				OAKNODE_OK
			}
			Some(None) => OAKNODE_E_NOT_FOUND,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_node_input_id` (two-stage).
	pub fn oaknode_node_input_id(
		node: CHandle,
		index: c_int,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		if index < 0 {
			return OAKNODE_E_NOT_FOUND;
		}
		match with_node(node, |g, id| {
			g.get(id)
				.and_then(|e| e.core.inputs.get(index as usize))
				.map(|i| i.id.clone())
		}) {
			Some(Some(s)) => string_out(&s, buf, buf_size),
			Some(None) => OAKNODE_E_NOT_FOUND,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_node_input_get_type` — `oak_node_value_type` code.
	pub fn oaknode_node_input_get_type(
		node: CHandle,
		input_id: *const c_char,
		out_type: *mut c_int,
	) -> c_int {
		if out_type.is_null() {
			return OAKNODE_E_INVALID;
		}
		// SAFETY: the caller guarantees a valid NUL-terminated string.
		let input_id = unsafe { cstr(input_id) };
		match with_node(node, |g, id| {
			g.get(id)
				.and_then(|e| e.core.input_data_type(&input_id))
				.map(|t| t.to_oak())
		}) {
			Some(Some(t)) => {
				// SAFETY: valid out pointer.
				unsafe { *out_type = t };
				OAKNODE_OK
			}
			Some(None) => OAKNODE_E_NOT_FOUND,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_node_input_is_connected`.
	pub fn oaknode_node_input_is_connected(
		node: CHandle,
		input_id: *const c_char,
		out_value: *mut c_int,
	) -> c_int {
		if out_value.is_null() {
			return OAKNODE_E_INVALID;
		}
		// SAFETY: the caller guarantees a valid NUL-terminated string.
		let input_id = unsafe { cstr(input_id) };
		match with_node(node, |g, id| {
			g.get(id)
				.map(|e| e.core.has_input(&input_id))
				.unwrap_or(false)
				&& g.is_input_connected(id, &input_id, -1)
		}) {
			Some(v) => {
				// SAFETY: valid out pointer.
				unsafe { *out_value = if v { 1 } else { 0 } };
				OAKNODE_OK
			}
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_node_input_is_connectable`.
	pub fn oaknode_node_input_is_connectable(
		node: CHandle,
		input_id: *const c_char,
		out_value: *mut c_int,
	) -> c_int {
		if out_value.is_null() {
			return OAKNODE_E_INVALID;
		}
		// SAFETY: the caller guarantees a valid NUL-terminated string.
		let input_id = unsafe { cstr(input_id) };
		match with_node(node, |g, id| {
			g.get(id)
				.and_then(|e| e.core.get_input(&input_id))
				.map(|i| i.is_connectable())
		}) {
			Some(Some(v)) => {
				// SAFETY: valid out pointer.
				unsafe { *out_value = if v { 1 } else { 0 } };
				OAKNODE_OK
			}
			Some(None) => OAKNODE_E_NOT_FOUND,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_node_get_input_name` (two-stage).
	pub fn oaknode_node_get_input_name(
		node: CHandle,
		input_id: *const c_char,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		// SAFETY: the caller guarantees a valid NUL-terminated string.
		let input_id = unsafe { cstr(input_id) };
		match with_node(node, |g, id| {
			g.get(id).map(|e| e.behavior.input_name(&input_id).to_string())
		}) {
			Some(Some(s)) => string_out(&s, buf, buf_size),
			Some(None) => OAKNODE_E_NOT_FOUND,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_node_input_get_connected_node` — null when unconnected.
	pub fn oaknode_node_input_get_connected_node(
		node: CHandle,
		input_id: *const c_char,
		out_node: *mut CHandle,
	) -> c_int {
		if out_node.is_null() {
			return OAKNODE_E_INVALID;
		}
		// SAFETY: the caller guarantees a valid NUL-terminated string.
		let input_id = unsafe { cstr(input_id) };
		let nr = match unsafe { node_ref_of(&node) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		let (project, id) = nr;
		let from = {
			let g = lock(&project);
			if !g
				.graph
				.get(id)
				.map(|e| e.core.has_input(&input_id))
				.unwrap_or(false)
			{
				return OAKNODE_E_NOT_FOUND;
			}
			g.graph.connected_output(id, &input_id, -1)
		};
		// SAFETY: valid out pointer.
		unsafe {
			*out_node = match from {
				Some(f) => make_node_handle(project, f, false),
				None => CHandle::null(),
			};
		}
		OAKNODE_OK
	}
	/// `oaknode_node_get_input` — standard value into the POD.
	pub fn oaknode_node_get_input(
		node: CHandle,
		input_id: *const c_char,
		out: *mut crate::node::OakNodeValue,
	) -> c_int {
		if out.is_null() {
			return OAKNODE_E_INVALID;
		}
		// SAFETY: the caller guarantees a valid NUL-terminated string.
		let input_id = unsafe { cstr(input_id) };
		match with_node(node, |g, id| {
			let e = g.get(id)?;
			let declared = e.core.input_data_type(&input_id)?;
			let v = e.core.standard_value(&input_id, -1);
			value_to_pod(declared, &v)
		}) {
			Some(Some(pod)) => {
				// SAFETY: valid out pointer.
				unsafe { *out = pod };
				OAKNODE_OK
			}
			Some(None) => OAKNODE_E_NOT_FOUND,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_node_set_input`.
	pub fn oaknode_node_set_input(
		node: CHandle,
		input_id: *const c_char,
		v: *const crate::node::OakNodeValue,
	) -> c_int {
		if v.is_null() {
			return OAKNODE_E_INVALID;
		}
		// SAFETY: the caller guarantees a valid NUL-terminated string and
		// a live POD.
		let (input_id, v) = unsafe { (cstr(input_id), *v) };
		match with_node_mut(node, |g, id| {
			let e = g.get_mut(id)?;
			let declared = e.core.input_data_type(&input_id)?;
			let value = pod_to_value(declared, v)?;
			e.core.set_standard_value(&input_id, -1, value);
			Some(())
		}) {
			Some(Some(())) => OAKNODE_OK,
			Some(None) => OAKNODE_E_NOT_FOUND,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_node_set_input_undoable`.
	pub fn oaknode_node_set_input_undoable(
		node: CHandle,
		input_id: *const c_char,
		v: *const crate::node::OakNodeValue,
		out_command: *mut CHandle,
	) -> c_int {
		if v.is_null() || out_command.is_null() {
			return OAKNODE_E_INVALID;
		}
		// SAFETY: the caller guarantees a valid NUL-terminated string and
		// a live POD.
		let (input_id, v) = unsafe { (cstr(input_id), *v) };
		let nr = match unsafe { node_ref_of(&node) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		let (project, id) = nr;
		let (new_value, old_value) = {
			let g = lock(&project);
			let e = match g.graph.get(id) {
				Some(e) => e,
				None => return OAKNODE_E_NOT_FOUND,
			};
			let declared = match e.core.input_data_type(&input_id) {
				Some(d) => d,
				None => return OAKNODE_E_NOT_FOUND,
			};
			let new_value = match pod_to_value(declared, v) {
				Some(nv) => nv,
				None => return OAKNODE_E_INVALID,
			};
			let old_value = e.core.standard_value(&input_id, -1);
			(new_value, old_value)
		};
		let p1 = project.clone();
		let p2 = project;
		let input1 = input_id.clone();
		let input2 = input_id;
		let cmd = closure_command(
			move || {
				let mut g = lock(&p1);
				if let Some(e) = g.graph.get_mut(id) {
					e.core.set_standard_value(&input1, -1, new_value.clone());
				}
			},
			move || {
				let mut g = lock(&p2);
				if let Some(e) = g.graph.get_mut(id) {
					e.core.set_standard_value(&input2, -1, old_value.clone());
				}
			},
		);
		// SAFETY: valid out pointer.
		unsafe { *out_command = box_command(cmd) };
		OAKNODE_OK
	}

	/// `oaknode_node_get_input_string` (two-stage).
	pub fn oaknode_node_get_input_string(
		node: CHandle,
		input_id: *const c_char,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		// SAFETY: the caller guarantees a valid NUL-terminated string.
		let input_id = unsafe { cstr(input_id) };
		match with_node(node, |g, id| {
			let e = g.get(id)?;
			if !e.core.has_input(&input_id) {
				return None;
			}
			let v = e.core.standard_value(&input_id, -1);
			match &v {
				oaknode::value::NodeValue::Text(s) => Some(s.clone()),
				_ => None,
			}
		}) {
			Some(Some(s)) => string_out(&s, buf, buf_size),
			Some(None) => OAKNODE_E_FAILED,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_node_set_input_string`.
	pub fn oaknode_node_set_input_string(
		node: CHandle,
		input_id: *const c_char,
		value: *const c_char,
	) -> c_int {
		// SAFETY: the caller guarantees valid NUL-terminated strings.
		let (input_id, value) = unsafe { (cstr(input_id), cstr(value)) };
		match with_node_mut(node, |g, id| {
			let e = g.get_mut(id)?;
			if !e.core.has_input(&input_id) {
				return None;
			}
			e.core
				.set_standard_value(&input_id, -1, oaknode::value::NodeValue::Text(value));
			Some(())
		}) {
			Some(Some(())) => OAKNODE_OK,
			Some(None) => OAKNODE_E_NOT_FOUND,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_node_set_input_string_undoable`.
	pub fn oaknode_node_set_input_string_undoable(
		node: CHandle,
		input_id: *const c_char,
		value: *const c_char,
		out_command: *mut CHandle,
	) -> c_int {
		if out_command.is_null() {
			return OAKNODE_E_INVALID;
		}
		// SAFETY: the caller guarantees valid NUL-terminated strings.
		let (input_id, value) = unsafe { (cstr(input_id), cstr(value)) };
		let nr = match unsafe { node_ref_of(&node) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		let (project, id) = nr;
		let old = {
			let g = lock(&project);
			let e = match g.graph.get(id) {
				Some(e) => e,
				None => return OAKNODE_E_NOT_FOUND,
			};
			if !e.core.has_input(&input_id) {
				return OAKNODE_E_NOT_FOUND;
			}
			let v = e.core.standard_value(&input_id, -1);
			match &v {
				oaknode::value::NodeValue::Text(s) => s.clone(),
				_ => String::new(),
			}
		};
		let p1 = project.clone();
		let p2 = project;
		let input1 = input_id.clone();
		let input2 = input_id;
		let value1 = value.clone();
		let cmd = closure_command(
			move || {
				let mut g = lock(&p1);
				if let Some(e) = g.graph.get_mut(id) {
					e.core.set_standard_value(
						&input1,
						-1,
						oaknode::value::NodeValue::Text(value1.clone()),
					);
				}
			},
			move || {
				let mut g = lock(&p2);
				if let Some(e) = g.graph.get_mut(id) {
					e.core.set_standard_value(
						&input2,
						-1,
						oaknode::value::NodeValue::Text(old.clone()),
					);
				}
			},
		);
		// SAFETY: valid out pointer.
		unsafe { *out_command = box_command(cmd) };
		OAKNODE_OK
	}

	/// `oaknode_node_connect` — live edge add.
	pub fn oaknode_node_connect(
		output_node: CHandle,
		input_node: CHandle,
		input_id: *const c_char,
	) -> c_int {
		// SAFETY: the caller guarantees a valid NUL-terminated string.
		let input_id = unsafe { cstr(input_id) };
		let out_nr = match unsafe { node_ref_of(&output_node) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		let in_nr = match unsafe { node_ref_of(&input_node) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		if !Arc::ptr_eq(&out_nr.0, &in_nr.0) {
			// Edges live in the input node's graph; both endpoints must
			// share it (cross-project connects are rejected).
			return OAKNODE_E_NOT_FOUND;
		}
		let mut g = lock(&out_nr.0);
		match g.graph.connect(out_nr.1, in_nr.1, &input_id, -1) {
			Ok(()) => OAKNODE_OK,
			Err(e) => e.code(),
		}
	}

	/// `oaknode_node_connect_undoable`.
	pub fn oaknode_node_connect_undoable(
		output_node: CHandle,
		input_node: CHandle,
		input_id: *const c_char,
		out_command: *mut CHandle,
	) -> c_int {
		if out_command.is_null() {
			return OAKNODE_E_INVALID;
		}
		// SAFETY: the caller guarantees a valid NUL-terminated string.
		let input_id = unsafe { cstr(input_id) };
		let out_nr = match unsafe { node_ref_of(&output_node) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		let in_nr = match unsafe { node_ref_of(&input_node) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		if !Arc::ptr_eq(&out_nr.0, &in_nr.0) {
			return OAKNODE_E_NOT_FOUND;
		}
		let (project, from, to) = (out_nr.0, out_nr.1, in_nr.1);
		// Pre-validate like the live connect (existence, connectability,
		// already-connected -> STATE).
		{
			let g = lock(&project);
			if !g.graph.is_valid(from) || !g.graph.is_valid(to) {
				return OAKNODE_E_NOT_FOUND;
			}
			let e = match g.graph.get(to) {
				Some(e) => e,
				None => return OAKNODE_E_NOT_FOUND,
			};
			let input = match e.core.get_input(&input_id) {
				Some(i) => i,
				None => return OAKNODE_E_NOT_FOUND,
			};
			if !input.is_connectable() {
				return OAKNODE_E_INVALID;
			}
			if g.graph.connected_output(to, &input_id, -1).is_some() {
				return OAKNODE_E_STATE;
			}
		}
		let p1 = project.clone();
		let p2 = project;
		let input1 = input_id.clone();
		let input2 = input_id;
		let cmd = closure_command(
			move || {
				let mut g = lock(&p1);
				let _ = g.graph.connect(from, to, &input1, -1);
			},
			move || {
				let mut g = lock(&p2);
				g.graph.disconnect_input(to, &input2, -1);
			},
		);
		// SAFETY: valid out pointer.
		unsafe { *out_command = box_command(cmd) };
		OAKNODE_OK
	}

	/// `oaknode_node_disconnect` — live edge remove.
	pub fn oaknode_node_disconnect(input_node: CHandle, input_id: *const c_char) -> c_int {
		// SAFETY: the caller guarantees a valid NUL-terminated string.
		let input_id = unsafe { cstr(input_id) };
		let nr = match unsafe { node_ref_of(&input_node) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		let mut g = lock(&nr.0);
		if g
			.graph
			.get(nr.1)
			.map(|e| e.core.has_input(&input_id))
			.unwrap_or(false)
		{
			g.graph.disconnect_input(nr.1, &input_id, -1);
			OAKNODE_OK
		} else {
			OAKNODE_E_NOT_FOUND
		}
	}

	/// `oaknode_node_disconnect_undoable` — succeeds even when nothing is
	/// connected (the redo is then a no-op, mirroring the C++ command's
	/// redo swallowing). The undo re-connect is not modelled (the source
	/// node id is not retained) — documented deviation.
	pub fn oaknode_node_disconnect_undoable(
		input_node: CHandle,
		input_id: *const c_char,
		out_command: *mut CHandle,
	) -> c_int {
		if out_command.is_null() {
			return OAKNODE_E_INVALID;
		}
		// SAFETY: the caller guarantees a valid NUL-terminated string.
		let input_id = unsafe { cstr(input_id) };
		let nr = match unsafe { node_ref_of(&input_node) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		let (project, to) = nr;
		{
			let g = lock(&project);
			if !g
				.graph
				.get(to)
				.map(|e| e.core.has_input(&input_id))
				.unwrap_or(false)
			{
				return OAKNODE_E_NOT_FOUND;
			}
		}
		let p1 = project.clone();
		let input1 = input_id.clone();
		let cmd = closure_command(
			move || {
				let mut g = lock(&p1);
				g.graph.disconnect_input(to, &input1, -1);
			},
			|| {},
		);
		// SAFETY: valid out pointer.
		unsafe { *out_command = box_command(cmd) };
		OAKNODE_OK
	}

	/// `oaknode_node_output_connection_count`.
	pub fn oaknode_node_output_connection_count(node: CHandle, out_count: *mut c_int) -> c_int {
		if out_count.is_null() {
			return OAKNODE_E_INVALID;
		}
		match with_node(node, |g, id| {
			if !g.is_valid(id) {
				return None;
			}
			Some(g.output_connections(id).len())
		}) {
			Some(Some(n)) => {
				// SAFETY: valid out pointer.
				unsafe { *out_count = n as c_int };
				OAKNODE_OK
			}
			Some(None) => OAKNODE_E_NOT_FOUND,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_node_output_connection_node_at`.
	pub fn oaknode_node_output_connection_node_at(
		node: CHandle,
		index: c_int,
		out_node: *mut CHandle,
	) -> c_int {
		if out_node.is_null() {
			return OAKNODE_E_INVALID;
		}
		if index < 0 {
			return OAKNODE_E_NOT_FOUND;
		}
		let nr = match unsafe { node_ref_of(&node) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		let (project, id) = nr;
		let target = {
			let g = lock(&project);
			if !g.graph.is_valid(id) {
				return OAKNODE_E_NOT_FOUND;
			}
			g.graph
				.output_connections(id)
				.get(index as usize)
				.map(|(t, _, _)| *t)
		};
		// SAFETY: valid out pointer.
		unsafe {
			*out_node = match target {
				Some(t) => make_node_handle(project, t, false),
				None => CHandle::null(),
			};
		}
		if target.is_some() {
			OAKNODE_OK
		} else {
			OAKNODE_E_NOT_FOUND
		}
	}

	/// `oaknode_node_output_connection_input_id_at` (two-stage).
	pub fn oaknode_node_output_connection_input_id_at(
		node: CHandle,
		index: c_int,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		if index < 0 {
			return OAKNODE_E_NOT_FOUND;
		}
		match with_node(node, |g, id| {
			if !g.is_valid(id) {
				return None;
			}
			g.output_connections(id)
				.get(index as usize)
				.map(|(_, input, _)| input.clone())
		}) {
			Some(Some(s)) => string_out(&s, buf, buf_size),
			Some(None) => OAKNODE_E_NOT_FOUND,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_node_output_connection_element_at`.
	pub fn oaknode_node_output_connection_element_at(
		node: CHandle,
		index: c_int,
		out_element: *mut c_int,
	) -> c_int {
		if out_element.is_null() || index < 0 {
			return OAKNODE_E_INVALID;
		}
		match with_node(node, |g, id| {
			if !g.is_valid(id) {
				return None;
			}
			g.output_connections(id)
				.get(index as usize)
				.map(|(_, _, e)| *e)
		}) {
			Some(Some(e)) => {
				// SAFETY: valid out pointer.
				unsafe { *out_element = e };
				OAKNODE_OK
			}
			Some(None) => OAKNODE_E_NOT_FOUND,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_node_link` — live link.
	pub fn oaknode_node_link(a: CHandle, b: CHandle, out_linked: *mut c_int) -> c_int {
		if out_linked.is_null() {
			return OAKNODE_E_INVALID;
		}
		let a_nr = match unsafe { node_ref_of(&a) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		let b_nr = match unsafe { node_ref_of(&b) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		if !Arc::ptr_eq(&a_nr.0, &b_nr.0) {
			return OAKNODE_E_NOT_FOUND;
		}
		let mut g = lock(&a_nr.0);
		let linked = g.graph.link(a_nr.1, b_nr.1);
		// SAFETY: valid out pointer.
		unsafe { *out_linked = if linked { 1 } else { 0 } };
		OAKNODE_OK
	}

	/// `oaknode_node_unlink` — live unlink.
	pub fn oaknode_node_unlink(a: CHandle, b: CHandle, out_unlinked: *mut c_int) -> c_int {
		if out_unlinked.is_null() {
			return OAKNODE_E_INVALID;
		}
		let a_nr = match unsafe { node_ref_of(&a) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		let b_nr = match unsafe { node_ref_of(&b) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		if !Arc::ptr_eq(&a_nr.0, &b_nr.0) {
			return OAKNODE_E_NOT_FOUND;
		}
		let mut g = lock(&a_nr.0);
		let unlinked = g.graph.unlink(a_nr.1, b_nr.1);
		// SAFETY: valid out pointer.
		unsafe { *out_unlinked = if unlinked { 1 } else { 0 } };
		OAKNODE_OK
	}

	/// `oaknode_node_link_undoable`.
	pub fn oaknode_node_link_undoable(
		a: CHandle,
		b: CHandle,
		link: c_int,
		out_command: *mut CHandle,
	) -> c_int {
		if out_command.is_null() {
			return OAKNODE_E_INVALID;
		}
		let a_nr = match unsafe { node_ref_of(&a) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		let b_nr = match unsafe { node_ref_of(&b) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		if !Arc::ptr_eq(&a_nr.0, &b_nr.0) {
			return OAKNODE_E_NOT_FOUND;
		}
		let (project, id_a, id_b) = (a_nr.0, a_nr.1, b_nr.1);
		let p1 = project.clone();
		let p2 = project;
		let cmd = closure_command(
			move || {
				let mut g = lock(&p1);
				if link != 0 {
					g.graph.link(id_a, id_b);
				} else {
					g.graph.unlink(id_a, id_b);
				}
			},
			move || {
				let mut g = lock(&p2);
				if link != 0 {
					g.graph.unlink(id_a, id_b);
				} else {
					g.graph.link(id_a, id_b);
				}
			},
		);
		// SAFETY: valid out pointer.
		unsafe { *out_command = box_command(cmd) };
		OAKNODE_OK
	}

	/// `oaknode_node_are_linked`.
	pub fn oaknode_node_are_linked(a: CHandle, b: CHandle, out_value: *mut c_int) -> c_int {
		if out_value.is_null() {
			return OAKNODE_E_INVALID;
		}
		let a_nr = match unsafe { node_ref_of(&a) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		let b_nr = match unsafe { node_ref_of(&b) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		let value = if Arc::ptr_eq(&a_nr.0, &b_nr.0) {
			let g = lock(&a_nr.0);
			g.graph.are_linked(a_nr.1, b_nr.1)
		} else {
			false
		};
		// SAFETY: valid out pointer.
		unsafe { *out_value = if value { 1 } else { 0 } };
		OAKNODE_OK
	}

	/// `oaknode_node_link_count`.
	pub fn oaknode_node_link_count(node: CHandle, out_count: *mut c_int) -> c_int {
		if out_count.is_null() {
			return OAKNODE_E_INVALID;
		}
		match with_node(node, |g, id| {
			if !g.is_valid(id) {
				return None;
			}
			Some(g.links_of(id).len())
		}) {
			Some(Some(n)) => {
				// SAFETY: valid out pointer.
				unsafe { *out_count = n as c_int };
				OAKNODE_OK
			}
			Some(None) => OAKNODE_E_NOT_FOUND,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_node_link_at`.
	pub fn oaknode_node_link_at(node: CHandle, index: c_int, out_node: *mut CHandle) -> c_int {
		if out_node.is_null() || index < 0 {
			return OAKNODE_E_INVALID;
		}
		let nr = match unsafe { node_ref_of(&node) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		let (project, id) = nr;
		let target = {
			let g = lock(&project);
			if !g.graph.is_valid(id) {
				return OAKNODE_E_NOT_FOUND;
			}
			g.graph.links_of(id).get(index as usize).copied()
		};
		// SAFETY: valid out pointer.
		unsafe {
			*out_node = match target {
				Some(t) => make_node_handle(project, t, false),
				None => CHandle::null(),
			};
		}
		if target.is_some() {
			OAKNODE_OK
		} else {
			OAKNODE_E_NOT_FOUND
		}
	}

	/// `oaknode_node_context_count`.
	pub fn oaknode_node_context_count(node: CHandle, out_count: *mut c_int) -> c_int {
		if out_count.is_null() {
			return OAKNODE_E_INVALID;
		}
		match with_node(node, |g, id| {
			g.get(id).map(|e| e.core.context_positions.len())
		}) {
			Some(Some(n)) => {
				// SAFETY: valid out pointer.
				unsafe { *out_count = n as c_int };
				OAKNODE_OK
			}
			Some(None) => OAKNODE_E_NOT_FOUND,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_node_context_node_at`.
	pub fn oaknode_node_context_node_at(
		node: CHandle,
		index: c_int,
		out_node: *mut CHandle,
	) -> c_int {
		if out_node.is_null() || index < 0 {
			return OAKNODE_E_INVALID;
		}
		let nr = match unsafe { node_ref_of(&node) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		let (project, id) = nr;
		let target = {
			let g = lock(&project);
			let e = match g.graph.get(id) {
				Some(e) => e,
				None => return OAKNODE_E_NOT_FOUND,
			};
			e.core.context_positions.get(index as usize).map(|(c, _, _)| *c)
		};
		// SAFETY: valid out pointer.
		unsafe {
			*out_node = match target {
				Some(t) => make_node_handle(project, t, false),
				None => CHandle::null(),
			};
		}
		if target.is_some() {
			OAKNODE_OK
		} else {
			OAKNODE_E_NOT_FOUND
		}
	}

	/// `oaknode_node_get_context_position`.
	pub fn oaknode_node_get_context_position(
		node: CHandle,
		context: CHandle,
		out_x: *mut f64,
		out_y: *mut f64,
		out_expanded: *mut c_int,
	) -> c_int {
		if out_x.is_null() || out_y.is_null() || out_expanded.is_null() {
			return OAKNODE_E_INVALID;
		}
		let ctx_id = match unsafe { node_ref_of(&context) } {
			Some(nr) => nr.id,
			None => return OAKNODE_E_INVALID,
		};
		match with_node(node, |g, id| {
			g.get(id).and_then(|e| {
				e.core
					.context_positions
					.iter()
					.find(|(c, _, _)| *c == ctx_id)
					.map(|(_, pos, expanded)| (*pos, *expanded))
			})
		}) {
			Some(Some(((x, y), expanded))) => {
				// SAFETY: valid out pointers.
				unsafe {
					*out_x = x;
					*out_y = y;
					*out_expanded = if expanded { 1 } else { 0 };
				}
				OAKNODE_OK
			}
			Some(None) => OAKNODE_E_NOT_FOUND,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_node_set_context_position`.
	pub fn oaknode_node_set_context_position(
		node: CHandle,
		context: CHandle,
		x: f64,
		y: f64,
		expanded: c_int,
	) -> c_int {
		let ctx_id = match unsafe { node_ref_of(&context) } {
			Some(nr) => nr.id,
			None => return OAKNODE_E_INVALID,
		};
		match with_node_mut(node, |g, id| {
			g.get_mut(id)
				.map(|e| e.core.set_context_position(ctx_id, x, y, expanded != 0))
		}) {
			Some(Some(_)) => OAKNODE_OK,
			Some(None) => OAKNODE_E_NOT_FOUND,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_node_set_context_position_undoable`.
	pub fn oaknode_node_set_context_position_undoable(
		node: CHandle,
		context: CHandle,
		x: f64,
		y: f64,
		expanded: c_int,
		out_command: *mut CHandle,
	) -> c_int {
		if out_command.is_null() {
			return OAKNODE_E_INVALID;
		}
		let nr = match unsafe { node_ref_of(&node) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		let ctx_id = match unsafe { node_ref_of(&context) } {
			Some(nr) => nr.id,
			None => return OAKNODE_E_INVALID,
		};
		let (project, id) = nr;
		let old = {
			let g = lock(&project);
			let e = match g.graph.get(id) {
				Some(e) => e,
				None => return OAKNODE_E_NOT_FOUND,
			};
			e.core
				.context_positions
				.iter()
				.find(|(c, _, _)| *c == ctx_id)
				.map(|(_, pos, expanded)| (*pos, *expanded))
		};
		let p1 = project.clone();
		let p2 = project;
		let cmd = closure_command(
			move || {
				let mut g = lock(&p1);
				if let Some(e) = g.graph.get_mut(id) {
					e.core.set_context_position(ctx_id, x, y, expanded != 0);
				}
			},
			move || {
				let mut g = lock(&p2);
				if let Some(e) = g.graph.get_mut(id) {
					match old {
						Some(((ox, oy), oe)) => {
							e.core.set_context_position(ctx_id, ox, oy, oe);
						}
						None => {
							e.core.remove_from_context(ctx_id);
						}
					}
				}
			},
		);
		// SAFETY: valid out pointer.
		unsafe { *out_command = box_command(cmd) };
		OAKNODE_OK
	}

	/// `oaknode_node_remove_from_context`.
	pub fn oaknode_node_remove_from_context(node: CHandle, context: CHandle) -> c_int {
		let ctx_id = match unsafe { node_ref_of(&context) } {
			Some(nr) => nr.id,
			None => return OAKNODE_E_INVALID,
		};
		match with_node_mut(node, |g, id| {
			g.get_mut(id).map(|e| e.core.remove_from_context(ctx_id))
		}) {
			Some(Some(_)) => OAKNODE_OK,
			Some(None) => OAKNODE_E_NOT_FOUND,
			None => OAKNODE_E_INVALID,
		}
	}
	/// `oaknode_node_create_copy` — standalone duplicate (owned).
	pub fn oaknode_node_create_copy(node: CHandle) -> CHandle {
		let nr = match unsafe { node_ref_of(&node) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return CHandle::null(),
		};
		let (project, id) = nr;
		let (core, behavior) = {
			let g = lock(&project);
			let e = match g.graph.get(id) {
				Some(e) => e,
				None => return CHandle::null(),
			};
			let core = e.core.clone();
			let behavior = match e.behavior.duplicate(&core) {
				Some(b) => b,
				None => return CHandle::null(),
			};
			(core, behavior)
		};
		make_detached((core, behavior))
	}

	/// `oaknode_node_copy_in_graph` — duplicate into the source project
	/// plus an undo command removing the copy.
	pub fn oaknode_node_copy_in_graph(node: CHandle, out_command: *mut CHandle) -> CHandle {
		if out_command.is_null() {
			return CHandle::null();
		}
		let nr = match unsafe { node_ref_of(&node) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return CHandle::null(),
		};
		let (project, id) = nr;
		let (core, behavior) = {
			let g = lock(&project);
			let e = match g.graph.get(id) {
				Some(e) => e,
				None => return CHandle::null(),
			};
			let core = e.core.clone();
			let behavior = match e.behavior.duplicate(&core) {
				Some(b) => b,
				None => return CHandle::null(),
			};
			(core, behavior)
		};
		let new_id = {
			let mut g = lock(&project);
			g.graph.add_node(core, behavior)
		};
		let p1 = project.clone();
		let p2 = project.clone();
		let cmd = closure_command(
			move || {
				// The copy already lives in the graph (created above).
				let _ = &mut lock(&p1).graph;
			},
			move || {
				let mut g = lock(&p2);
				g.graph.remove_node(new_id);
			},
		);
		// SAFETY: valid out pointer.
		unsafe { *out_command = box_command(cmd) };
		make_node_handle(project, new_id, false)
	}

	/// `oaknode_node_get_project` — borrowed project handle.
	pub fn oaknode_node_get_project(node: CHandle, out: *mut CHandle) -> c_int {
		if out.is_null() {
			return OAKNODE_E_INVALID;
		}
		let project = unsafe { node_ref_of(&node) }.map(|nr| nr.project.clone());
		match project {
			Some(p) => {
				// SAFETY: valid out pointer.
				unsafe { *out = make_project_handle(p) };
				OAKNODE_OK
			}
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_node_input_array_insert`.
	pub fn oaknode_node_input_array_insert(
		node: CHandle,
		input_id: *const c_char,
		index: c_int,
	) -> c_int {
		if index < 0 {
			return OAKNODE_E_INVALID;
		}
		// SAFETY: the caller guarantees a valid NUL-terminated string.
		let input_id = unsafe { cstr(input_id) };
		match with_node_mut(node, |g, id| {
			let e = g.get_mut(id)?;
			if !e.core.has_input(&input_id) {
				return None;
			}
			e.core.input_array_insert(&input_id, index as usize);
			Some(())
		}) {
			Some(Some(())) => OAKNODE_OK,
			Some(None) => OAKNODE_E_NOT_FOUND,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_node_input_array_remove`.
	pub fn oaknode_node_input_array_remove(
		node: CHandle,
		input_id: *const c_char,
		index: c_int,
	) -> c_int {
		if index < 0 {
			return OAKNODE_E_INVALID;
		}
		// SAFETY: the caller guarantees a valid NUL-terminated string.
		let input_id = unsafe { cstr(input_id) };
		match with_node_mut(node, |g, id| {
			let e = g.get_mut(id)?;
			if !e.core.has_input(&input_id) {
				return None;
			}
			e.core.input_array_remove(&input_id, index as usize);
			Some(())
		}) {
			Some(Some(())) => OAKNODE_OK,
			Some(None) => OAKNODE_E_NOT_FOUND,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_node_connect_element`.
	pub fn oaknode_node_connect_element(
		output_node: CHandle,
		input_node: CHandle,
		input_id: *const c_char,
		element: c_int,
	) -> c_int {
		// SAFETY: the caller guarantees a valid NUL-terminated string.
		let input_id = unsafe { cstr(input_id) };
		let out_nr = match unsafe { node_ref_of(&output_node) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		let in_nr = match unsafe { node_ref_of(&input_node) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		if !Arc::ptr_eq(&out_nr.0, &in_nr.0) {
			return OAKNODE_E_NOT_FOUND;
		}
		let mut g = lock(&out_nr.0);
		match g.graph.connect(out_nr.1, in_nr.1, &input_id, element) {
			Ok(()) => OAKNODE_OK,
			Err(e) => e.code(),
		}
	}

	/// `oaknode_node_disconnect_element`.
	pub fn oaknode_node_disconnect_element(
		input_node: CHandle,
		input_id: *const c_char,
		element: c_int,
	) -> c_int {
		// SAFETY: the caller guarantees a valid NUL-terminated string.
		let input_id = unsafe { cstr(input_id) };
		let nr = match unsafe { node_ref_of(&input_node) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		let mut g = lock(&nr.0);
		if g
			.graph
			.get(nr.1)
			.map(|e| e.core.has_input(&input_id))
			.unwrap_or(false)
		{
			g.graph.disconnect_input(nr.1, &input_id, element);
			OAKNODE_OK
		} else {
			OAKNODE_E_NOT_FOUND
		}
	}

	/// `oaknode_command_create_add_node` — move `node` into `graph`'s
	/// project (undo moves it back).
	pub fn oaknode_command_create_add_node(graph: CHandle, node: CHandle) -> CHandle {
		let target = match unsafe { project_of(&graph) }.cloned() {
			Some(t) => t,
			None => return CHandle::null(),
		};
		let nr = match unsafe { node_ref_of(&node) } {
			Some(nr) => nr,
			None => return CHandle::null(),
		};
		let src_project = nr.project.clone();
		let src_id = nr.id;
		if Arc::ptr_eq(&src_project, &target) && { lock(&target).graph.is_valid(src_id) } {
			return CHandle::null();
		}
		let entry = {
			let mut s = lock(&src_project);
			match s.graph.take_node(src_id) {
				Some(e) => e,
				None => return CHandle::null(),
			}
		};
		let mut entry = Some(entry);
		let target_redo = target.clone();
		let src_undo = src_project;
		let target_undo = target;
		let cmd = closure_command(
			move || {
				let e = match entry.take() {
					Some(e) => e,
					None => return,
				};
				let new_id = {
					let mut t = lock(&target_redo);
					t.graph.add_entry(e, src_id)
				};
				// SAFETY: the shared node box is rewritten in place.
				if let Some(boxed) = unsafe { node_ref_mut(&node) } {
					boxed.project = target_redo.clone();
					boxed.id = new_id;
					if boxed.owned.swap(false, Ordering::SeqCst) {
						alive_dec();
					}
				}
			},
			move || {
				let current_id = unsafe { node_ref_of(&node) }
					.map(|n| n.id)
					.unwrap_or(src_id);
				let e = {
					let mut t = lock(&target_undo);
					match t.graph.take_node(current_id) {
						Some(e) => e,
						None => return,
					}
				};
				{
					let mut s = lock(&src_undo);
					s.graph.add_entry(e, src_id);
				}
				// SAFETY: the shared node box is rewritten back.
				if let Some(boxed) = unsafe { node_ref_mut(&node) } {
					boxed.project = src_undo.clone();
					boxed.id = src_id;
					if !boxed.owned.swap(true, Ordering::SeqCst) {
						alive_inc();
					}
				}
			},
		);
		box_command(cmd)
	}

	/// `oaknode_command_create_set_position_recursive` — best-effort: set
	/// the node's own context position (undo restores the old value).
	pub fn oaknode_command_create_set_position_recursive(
		node: CHandle,
		context: CHandle,
		x: f64,
		y: f64,
	) -> CHandle {
		let nr = match unsafe { node_ref_of(&node) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return CHandle::null(),
		};
		let ctx_id = match unsafe { node_ref_of(&context) } {
			Some(nr) => nr.id,
			None => return CHandle::null(),
		};
		let (project, id) = nr;
		let old = {
			let g = lock(&project);
			match g.graph.get(id) {
				Some(e) => e
					.core
					.context_positions
					.iter()
					.find(|(c, _, _)| *c == ctx_id)
					.map(|(_, pos, expanded)| (*pos, *expanded)),
				None => return CHandle::null(),
			}
		};
		let p1 = project.clone();
		let p2 = project;
		let cmd = closure_command(
			move || {
				let mut g = lock(&p1);
				if let Some(e) = g.graph.get_mut(id) {
					e.core.set_context_position(ctx_id, x, y, false);
				}
			},
			move || {
				let mut g = lock(&p2);
				if let Some(e) = g.graph.get_mut(id) {
					match old {
						Some(((ox, oy), oe)) => {
							e.core.set_context_position(ctx_id, ox, oy, oe);
						}
						None => {
							e.core.remove_from_context(ctx_id);
						}
					}
				}
			},
		);
		box_command(cmd)
	}

	/// `oaknode_node_get_markers` — the sequence's marker list (created
	/// lazily; addref'd copy).
	pub fn oaknode_node_get_markers(node: CHandle, out: *mut CHandle) -> c_int {
		if out.is_null() {
			return OAKNODE_E_INVALID;
		}
		let markers = with_node_mut(node, |g, id| {
			let seq = behavior_of_mut::<oaknode::sequence::SequenceBehavior>(g, id)?;
			if seq.markers.is_null() {
				seq.markers = oaktimeline::handle::make_owned(
					oaktimeline::marker::TimelineMarkerList::new(),
				);
			}
			Some(seq.markers)
		});
		match markers {
			Some(Some(h)) => {
				// SAFETY: valid out pointer.
				unsafe { *out = addref_copy(h) };
				OAKNODE_OK
			}
			Some(None) => OAKNODE_E_NOT_FOUND,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_node_get_work_area` — the sequence's work area (created
	/// lazily; addref'd copy).
	pub fn oaknode_node_get_work_area(node: CHandle, out: *mut CHandle) -> c_int {
		if out.is_null() {
			return OAKNODE_E_INVALID;
		}
		let workarea = with_node_mut(node, |g, id| {
			let seq = behavior_of_mut::<oaknode::sequence::SequenceBehavior>(g, id)?;
			if seq.workarea.is_null() {
				seq.workarea =
					oaktimeline::handle::make_owned(oaktimeline::workarea::TimelineWorkArea::new());
			}
			Some(seq.workarea)
		});
		match workarea {
			Some(Some(h)) => {
				// SAFETY: valid out pointer.
				unsafe { *out = addref_copy(h) };
				OAKNODE_OK
			}
			Some(None) => OAKNODE_E_NOT_FOUND,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_node_get_video_frame_cache` — the node's video cache
	/// handle (usually empty: caches are created lazily by the render
	/// module; documented).
	pub fn oaknode_node_get_video_frame_cache(node: CHandle, out: *mut CHandle) -> c_int {
		if out.is_null() {
			return OAKNODE_E_INVALID;
		}
		match with_node(node, |g, id| g.get(id).map(|e| e.core.caches.video)) {
			Some(Some(h)) => {
				// SAFETY: valid out pointer.
				unsafe { *out = addref_copy(h) };
				OAKNODE_OK
			}
			Some(None) => OAKNODE_E_NOT_FOUND,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_node_copy_inputs`.
	pub fn oaknode_node_copy_inputs(
		dst: CHandle,
		src: CHandle,
		include_connections: c_int,
	) -> c_int {
		let dst_nr = match unsafe { node_ref_of(&dst) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		let src_nr = match unsafe { node_ref_of(&src) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		if !Arc::ptr_eq(&dst_nr.0, &src_nr.0) {
			return OAKNODE_E_NOT_FOUND;
		}
		let mut g = lock(&dst_nr.0);
		match oaknode::ops::copy_inputs(
			&mut g.graph,
			src_nr.1,
			dst_nr.1,
			include_connections != 0,
		) {
			Ok(()) => OAKNODE_OK,
			Err(e) => e.code(),
		}
	}

	/// `oaknode_node_set_value_hint_track` — best-effort value hint from a
	/// track reference (video -> Texture, audio -> Samples).
	pub fn oaknode_node_set_value_hint_track(
		node: CHandle,
		input_id: *const c_char,
		track_type: c_int,
		track_index: c_int,
	) -> c_int {
		// SAFETY: the caller guarantees a valid NUL-terminated string.
		let input_id = unsafe { cstr(input_id) };
		let types = match track_type {
			0 => vec![oaknode::value::ValueType::Texture],
			1 => vec![oaknode::value::ValueType::Samples],
			_ => Vec::new(),
		};
		match with_node_mut(node, |g, id| {
			let e = g.get_mut(id)?;
			if !e.core.has_input(&input_id) {
				return None;
			}
			e.core.set_value_hint(
				&input_id,
				-1,
				oaknode::input::ValueHint {
					types,
					index: track_index,
					tag: String::new(),
				},
			);
			Some(())
		}) {
			Some(Some(())) => OAKNODE_OK,
			Some(None) => OAKNODE_E_NOT_FOUND,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_viewer_set_video_params` — set the sequence's video
	/// parameter stream 0.
	pub fn oaknode_viewer_set_video_params(viewer: CHandle, params: *const CHandle) -> c_int {
		if params.is_null() {
			return OAKNODE_E_INVALID;
		}
		// SAFETY: the caller passes a live oakcommon videoparams handle.
		let converted = unsafe { vp_from_handle(*params) };
		let Some(converted) = converted else {
			return OAKNODE_E_INVALID;
		};
		match with_node_mut(viewer, |g, id| {
			let seq = behavior_of_mut::<oaknode::sequence::SequenceBehavior>(g, id)?;
			if seq.video_params.is_empty() {
				seq.video_params.push(converted);
			} else {
				seq.video_params[0] = converted;
			}
			Some(())
		}) {
			Some(Some(())) => OAKNODE_OK,
			Some(None) => OAKNODE_E_NOT_FOUND,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_viewer_set_audio_params` — set the sequence's audio
	/// parameter stream 0 (oakcore audioparams pointer).
	pub fn oaknode_viewer_set_audio_params(viewer: CHandle, params: *const c_void) -> c_int {
		if params.is_null() {
			return OAKNODE_E_INVALID;
		}
		// SAFETY: the oakcore audioparams contract.
		let (sample_rate, channel_layout, format) = unsafe {
			(
				crate::stubs::audio::oakcore_audioparams_sample_rate(params),
				crate::stubs::audio::oakcore_audioparams_channel_layout(params),
				crate::stubs::audio::oakcore_audioparams_format(params),
			)
		};
		let converted = oaknode::value::AudioParams {
			sample_rate,
			channel_layout,
			format,
		};
		match with_node_mut(viewer, |g, id| {
			let seq = behavior_of_mut::<oaknode::sequence::SequenceBehavior>(g, id)?;
			if seq.audio_params.is_empty() {
				seq.audio_params.push(converted);
			} else {
				seq.audio_params[0] = converted;
			}
			Some(())
		}) {
			Some(Some(())) => OAKNODE_OK,
			Some(None) => OAKNODE_E_NOT_FOUND,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_node_find_input_footage` — the first footage node feeding
	/// this node (upstream walk).
	pub fn oaknode_node_find_input_footage(node: CHandle, out: *mut CHandle) -> c_int {
		if out.is_null() {
			return OAKNODE_E_INVALID;
		}
		let nr = match unsafe { node_ref_of(&node) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		let (project, id) = nr;
		let found = {
			let g = lock(&project);
			if !g.graph.is_valid(id) {
				return OAKNODE_E_NOT_FOUND;
			}
			let mut frontier = vec![id];
			let mut visited: Vec<NodeId> = Vec::new();
			let mut found = None;
			while !frontier.is_empty() && found.is_none() {
				let mut next = Vec::new();
				for cur in frontier {
					if visited.contains(&cur) {
						continue;
					}
					visited.push(cur);
					let Some(e) = g.graph.get(cur) else { continue };
					if e.behavior.type_id() == "org.olivevideoeditor.Olive.footage" && cur != id {
						found = Some(cur);
						break;
					}
					for (src, _, _) in g.graph.input_connections(cur) {
						next.push(src);
					}
				}
				frontier = next;
			}
			found
		};
		// SAFETY: valid out pointer.
		unsafe {
			*out = match found {
				Some(f) => make_node_handle(project, f, false),
				None => CHandle::null(),
			};
		}
		OAKNODE_OK
	}

	/// `oaknode_node_get_input_at_time` — value at a rational time.
	pub fn oaknode_node_get_input_at_time(
		node: CHandle,
		input_id: *const c_char,
		time_num: i64,
		time_den: i64,
		out: *mut crate::node::OakNodeValue,
	) -> c_int {
		if out.is_null() || time_den == 0 {
			return OAKNODE_E_INVALID;
		}
		// SAFETY: the caller guarantees a valid NUL-terminated string.
		let input_id = unsafe { cstr(input_id) };
		match with_node(node, |g, id| {
			let e = g.get(id)?;
			let declared = e.core.input_data_type(&input_id)?;
			let v = e
				.core
				.value_at_time(&input_id, -1, Rational::new(time_num, time_den));
			value_to_pod(declared, &v)
		}) {
			Some(Some(pod)) => {
				// SAFETY: valid out pointer.
				unsafe { *out = pod };
				OAKNODE_OK
			}
			Some(None) => OAKNODE_E_NOT_FOUND,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_node_set_input_at_time_undoable` — the real
	/// `set_value_at_time` domain op (keyframed tracks get a key at the
	/// time; static inputs get the standard value).
	pub fn oaknode_node_set_input_at_time_undoable(
		node: CHandle,
		input_id: *const c_char,
		time_num: i64,
		time_den: i64,
		v: *const crate::node::OakNodeValue,
		track: c_int,
		out_command: *mut CHandle,
	) -> c_int {
		if v.is_null() || out_command.is_null() || time_den == 0 {
			return OAKNODE_E_INVALID;
		}
		// SAFETY: the caller guarantees a valid NUL-terminated string and
		// a live POD.
		let (input_id, v) = unsafe { (cstr(input_id), *v) };
		let _ = track;
		let nr = match unsafe { node_ref_of(&node) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		let (project, id) = nr;
		let declared = {
			let g = lock(&project);
			let e = match g.graph.get(id) {
				Some(e) => e,
				None => return OAKNODE_E_NOT_FOUND,
			};
			match e.core.input_data_type(&input_id) {
				Some(d) => d,
				None => return OAKNODE_E_NOT_FOUND,
			}
		};
		let value = match pod_to_value(declared, v) {
			Some(nv) => nv,
			None => return OAKNODE_E_INVALID,
		};
		let guard = lock(&project);
		let cmd = match oaknode::ops::set_value_at_time_command(
			&project,
			&guard.graph,
			id,
			&input_id,
			-1,
			Rational::new(time_num, time_den),
			&value,
		) {
			Ok(c) => c,
			Err(e) => return e.code(),
		};
		// SAFETY: valid out pointer.
		unsafe { *out_command = box_command(cmd) };
		OAKNODE_OK
	}

	/// `oaknode_node_identity` — the graph slot identity (0 for invalid).
	pub fn oaknode_node_identity(node: CHandle) -> usize {
		node_identity(node)
	}

	/// `oaknode_node_set_input_at_time_into` — create the command and add
	/// it to a multi command (the multi takes ownership).
	pub fn oaknode_node_set_input_at_time_into(
		node: CHandle,
		input_id: *const c_char,
		time_num: i64,
		time_den: i64,
		v: *const crate::node::OakNodeValue,
		track: c_int,
		multi_command: CHandle,
	) -> c_int {
		let mut cmd = CHandle::null();
		let rc = oaknode_node_set_input_at_time_undoable(
			node, input_id, time_num, time_den, v, track, &mut cmd,
		);
		if rc != OAKNODE_OK {
			return rc;
		}
		oakundo::undocommand::command_multi_add_child(multi_command, cmd)
	}

	/// `oaknode_command_create_remove_node` — remove the node from its
	/// graph (the entry is not retained for undo — documented deviation
	/// from the C++ shared-ptr retention).
	pub fn oaknode_command_create_remove_node(node: CHandle) -> CHandle {
		let nr = match unsafe { node_ref_of(&node) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return CHandle::null(),
		};
		let (project, id) = nr;
		{
			let g = lock(&project);
			if !g.graph.is_valid(id) {
				return CHandle::null();
			}
		}
		let p1 = project.clone();
		let cmd = closure_command(
			move || {
				let mut g = lock(&p1);
				g.graph.remove_node(id);
			},
			|| {},
		);
		box_command(cmd)
	}

	/// `oaknode_node_free` — release a node handle shell; a still-owned
	/// (detached) node is dropped from its scratch graph.
	pub fn oaknode_node_free(node: *mut CHandle) {
		if node.is_null() {
			return;
		}
		// SAFETY: the caller passes a valid handle pointer.
		let h = unsafe { *node };
		free_node_handle(h);
		// SAFETY: the caller passes a valid handle pointer.
		unsafe { *node = CHandle::null() };
	}

	// -------------------------------------------------------------------
	// Factory family
	// -------------------------------------------------------------------

	/// `oaknode_factory_initialize` — the registry builds lazily; nothing
	/// to do.
	pub fn oaknode_factory_initialize() -> c_int {
		let _ = Factory::global();
		OAKNODE_OK
	}

	/// `oaknode_factory_destroy` — process-lifetime registry; nothing to
	/// do.
	pub fn oaknode_factory_destroy() {}

	/// `oaknode_factory_id_count`.
	pub fn oaknode_factory_id_count(out_count: *mut c_int) -> c_int {
		if out_count.is_null() {
			return OAKNODE_E_INVALID;
		}
		// SAFETY: valid out pointer.
		unsafe { *out_count = Factory::global().entries().len() as c_int };
		OAKNODE_OK
	}

	/// `oaknode_factory_id_at` (two-stage).
	pub fn oaknode_factory_id_at(index: c_int, buf: *mut c_char, buf_size: c_int) -> c_int {
		if index < 0 {
			return OAKNODE_E_NOT_FOUND;
		}
		match Factory::global().entries().get(index as usize) {
			Some(meta) => string_out(meta.type_id, buf, buf_size),
			None => OAKNODE_E_NOT_FOUND,
		}
	}

	/// `oaknode_factory_name_from_id` (two-stage).
	pub fn oaknode_factory_name_from_id(
		type_id: *const c_char,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		// SAFETY: the caller guarantees a valid NUL-terminated string.
		let type_id = unsafe { cstr(type_id) };
		match Factory::global().find(&type_id) {
			Some(meta) => string_out(meta.name, buf, buf_size),
			None => OAKNODE_E_NOT_FOUND,
		}
	}

	/// `oaknode_factory_create_from_id` — detached node (owned).
	pub fn oaknode_factory_create_from_id(type_id: *const c_char) -> CHandle {
		// SAFETY: the caller guarantees a valid NUL-terminated string.
		let type_id = unsafe { cstr(type_id) };
		match Factory::global().find(&type_id) {
			Some(meta) => make_detached((meta.create)()),
			None => CHandle::null(),
		}
	}

	/// Cached factory prototype nodes (one per registry entry; created
	/// lazily in the scratch project).
	static PROTOTYPES: OnceLock<Mutex<Vec<CHandle>>> = OnceLock::new();

	/// `oaknode_factory_node_at` — borrowed prototype node.
	pub fn oaknode_factory_node_at(index: c_int, out_node: *mut CHandle) -> c_int {
		if out_node.is_null() {
			return OAKNODE_E_INVALID;
		}
		if index < 0 {
			return OAKNODE_E_NOT_FOUND;
		}
		let entries = Factory::global().entries();
		if index as usize >= entries.len() {
			return OAKNODE_E_NOT_FOUND;
		}
		let mut protos = PROTOTYPES
			.get_or_init(|| Mutex::new(Vec::new()))
			.lock()
			.unwrap_or_else(|e| e.into_inner());
		if protos.len() != entries.len() {
			// Fill lazily: only the entries up to (and including) the
			// requested index are materialized.
			for i in protos.len()..=index as usize {
				let (core, behavior) = (entries[i].create)();
				let project = scratch_project();
				let id = {
					let mut p = lock(&project);
					p.graph.add_node(core, behavior)
				};
				protos.push(make_node_handle(project, id, false));
			}
		}
		// SAFETY: valid out pointer; the prototype is addref'd for the
		// caller (owned copy semantics).
		unsafe { *out_node = addref_copy(protos[index as usize]) };
		OAKNODE_OK
	}
	// -------------------------------------------------------------------
	// Folder family
	// -------------------------------------------------------------------

	/// `oaknode_folder_create` — new folder node in the project graph.
	pub fn oaknode_folder_create(project: CHandle) -> CHandle {
		let p = match unsafe { project_of(&project) }.cloned() {
			Some(p) => p,
			None => return CHandle::null(),
		};
		let (core, behavior) = oaknode::folder::create("Folder");
		let id = {
			let mut g = lock(&p);
			g.graph.add_node(core, behavior)
		};
		make_node_handle(p, id, false)
	}

	/// Folder children of a folder handle.
	fn folder_children(h: CHandle) -> Option<Vec<NodeId>> {
		with_node(h, |g, id| {
			behavior_of::<oaknode::folder::FolderBehavior>(g, id)
				.map(|f| f.children.clone())
		})?
	}

	/// `oaknode_folder_child_count`.
	pub fn oaknode_folder_child_count(folder: CHandle) -> c_int {
		match folder_children(folder) {
			Some(children) => children.len() as c_int,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_folder_child_at` — borrowed child view.
	pub fn oaknode_folder_child_at(folder: CHandle, index: c_int) -> CHandle {
		if index < 0 {
			return CHandle::null();
		}
		let nr = match unsafe { node_ref_of(&folder) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return CHandle::null(),
		};
		let (project, id) = nr;
		let child = {
			let g = lock(&project);
			behavior_of::<oaknode::folder::FolderBehavior>(&g.graph, id)
				.and_then(|f| f.children.get(index as usize).copied())
		};
		match child {
			Some(c) => make_node_handle(project, c, false),
			None => CHandle::null(),
		}
	}

	/// `oaknode_folder_add_child` — live add (+ bin membership).
	pub fn oaknode_folder_add_child(folder: CHandle, child: CHandle) -> c_int {
		let f_nr = match unsafe { node_ref_of(&folder) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		let c_id = match unsafe { node_ref_of(&child) } {
			Some(nr) => nr.id,
			None => return OAKNODE_E_INVALID,
		};
		let mut g = lock(&f_nr.0);
		let folder_b =
			match behavior_of_mut::<oaknode::folder::FolderBehavior>(&mut g.graph, f_nr.1) {
				Some(f) => f,
				None => return OAKNODE_E_NOT_FOUND,
			};
		folder_b.add_child(c_id);
		if let Some(e) = g.graph.get_mut(c_id) {
			e.core.bin_folder = Some(f_nr.1);
		}
		OAKNODE_OK
	}

	/// `oaknode_folder_as_node` — identity cast (every handle shares the
	/// same payload layout); an addref'd copy keeps the borrow discipline.
	pub fn oaknode_folder_as_node(folder: CHandle) -> CHandle {
		addref_copy(folder)
	}

	/// `oaknode_command_create_folder_add_child` — undoable add.
	pub fn oaknode_command_create_folder_add_child(folder: CHandle, child: CHandle) -> CHandle {
		let f_nr = match unsafe { node_ref_of(&folder) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return CHandle::null(),
		};
		let c_id = match unsafe { node_ref_of(&child) } {
			Some(nr) => nr.id,
			None => return CHandle::null(),
		};
		let (project, f_id) = f_nr;
		{
			let g = lock(&project);
			if behavior_of::<oaknode::folder::FolderBehavior>(&g.graph, f_id).is_none() {
				return CHandle::null();
			}
		}
		let p1 = project.clone();
		let p2 = project;
		let cmd = closure_command(
			move || {
				let mut g = lock(&p1);
				if let Some(f) =
					behavior_of_mut::<oaknode::folder::FolderBehavior>(&mut g.graph, f_id)
				{
					f.add_child(c_id);
				}
				if let Some(e) = g.graph.get_mut(c_id) {
					e.core.bin_folder = Some(f_id);
				}
			},
			move || {
				let mut g = lock(&p2);
				if let Some(f) =
					behavior_of_mut::<oaknode::folder::FolderBehavior>(&mut g.graph, f_id)
				{
					f.remove_child(c_id);
				}
				if let Some(e) = g.graph.get_mut(c_id) {
					e.core.bin_folder = None;
				}
			},
		);
		box_command(cmd)
	}

	/// `oaknode_folder_remove_child` — live remove.
	pub fn oaknode_folder_remove_child(folder: CHandle, child: CHandle) -> c_int {
		let f_nr = match unsafe { node_ref_of(&folder) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		let c_id = match unsafe { node_ref_of(&child) } {
			Some(nr) => nr.id,
			None => return OAKNODE_E_INVALID,
		};
		let mut g = lock(&f_nr.0);
		let folder_b =
			match behavior_of_mut::<oaknode::folder::FolderBehavior>(&mut g.graph, f_nr.1) {
				Some(f) => f,
				None => return OAKNODE_E_NOT_FOUND,
			};
		folder_b.remove_child(c_id);
		if let Some(e) = g.graph.get_mut(c_id) {
			e.core.bin_folder = None;
		}
		OAKNODE_OK
	}

	/// `oaknode_folder_move_children` — move nodes into `dest_folder`.
	pub fn oaknode_folder_move_children(
		nodes: *const CHandle,
		count: c_int,
		dest_folder: CHandle,
	) -> c_int {
		if nodes.is_null() || count < 0 {
			return OAKNODE_E_INVALID;
		}
		for i in 0..count as usize {
			// SAFETY: the caller guarantees `count` valid handles.
			let child = unsafe { *nodes.add(i) };
			let rc = oaknode_folder_add_child(dest_folder, child);
			if rc != OAKNODE_OK {
				return rc;
			}
		}
		OAKNODE_OK
	}

	/// `oaknode_folder_has_child_recursive`.
	pub fn oaknode_folder_has_child_recursive(folder: CHandle, child: CHandle) -> c_int {
		let f_nr = match unsafe { node_ref_of(&folder) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		let c_id = match unsafe { node_ref_of(&child) } {
			Some(nr) => nr.id,
			None => return OAKNODE_E_INVALID,
		};
		let g = lock(&f_nr.0);
		match behavior_of::<oaknode::folder::FolderBehavior>(&g.graph, f_nr.1) {
			Some(f) => {
				if f.has_child_recursive(c_id, &g.graph) {
					1
				} else {
					0
				}
			}
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_folder_index_of_child`.
	pub fn oaknode_folder_index_of_child(folder: CHandle, child: CHandle) -> c_int {
		let f_nr = match unsafe { node_ref_of(&folder) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		let c_id = match unsafe { node_ref_of(&child) } {
			Some(nr) => nr.id,
			None => return OAKNODE_E_INVALID,
		};
		let g = lock(&f_nr.0);
		match behavior_of::<oaknode::folder::FolderBehavior>(&g.graph, f_nr.1) {
			Some(f) => match f.index_of_child(c_id) {
				Some(i) => i as c_int,
				None => OAKNODE_E_NOT_FOUND,
			},
			None => OAKNODE_E_NOT_FOUND,
		}
	}

	/// `oaknode_folder_parent_of` — the folder owning this item (null when
	/// none).
	pub fn oaknode_folder_parent_of(node: CHandle) -> CHandle {
		let nr = match unsafe { node_ref_of(&node) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return CHandle::null(),
		};
		let (project, id) = nr;
		let parent = {
			let g = lock(&project);
			let entry = match g.graph.get(id) {
				Some(e) => e,
				None => return CHandle::null(),
			};
			match entry.core.bin_folder {
				Some(bin)
					if behavior_of::<oaknode::folder::FolderBehavior>(&g.graph, bin).is_some() =>
				{
					Some(bin)
				}
				_ => {
					// Fall back to a recursive walk over folder children.
					g.graph.node_ids().into_iter().find(|fid| {
						behavior_of::<oaknode::folder::FolderBehavior>(&g.graph, *fid)
							.map(|f| f.has_child_recursive(id, &g.graph))
							.unwrap_or(false)
					})
				}
			}
		};
		match parent {
			Some(p) => make_node_handle(project, p, false),
			None => CHandle::null(),
		}
	}

	// -------------------------------------------------------------------
	// Footage family
	// -------------------------------------------------------------------

	/// Borrow the footage behavior behind a footage handle.
	fn footage_behavior(h: CHandle) -> Option<&'static oaknode::footage::FootageBehavior> {
		let nr = unsafe { node_ref_of(&h) }?;
		let p = lock(&nr.project);
		let b = behavior_of::<oaknode::footage::FootageBehavior>(&p.graph, nr.id)?;
		// SAFETY: the node lives in the arena; the project outlives every
		// handle that references it (handles hold an Arc clone).
		unsafe { Some(&*(b as *const _)) }
	}

	/// Mutable footage-behavior view.
	fn footage_behavior_mut(h: CHandle) -> Option<&'static mut oaknode::footage::FootageBehavior> {
		let nr = unsafe { node_ref_of(&h) }?;
		let mut p = lock(&nr.project);
		let b = behavior_of_mut::<oaknode::footage::FootageBehavior>(&mut p.graph, nr.id)?;
		// SAFETY: see `footage_behavior`.
		unsafe { Some(&mut *(b as *mut _)) }
	}

	/// `oaknode_footage_create` — footage node registered in `project`.
	pub fn oaknode_footage_create(project: CHandle, filename: *const c_char) -> CHandle {
		// SAFETY: the caller guarantees a valid NUL-terminated string.
		let filename = unsafe { cstr(filename) };
		let p = match unsafe { project_of(&project) }.cloned() {
			Some(p) => p,
			None => return CHandle::null(),
		};
		let (mut core, behavior) = oaknode::footage::FootageBehavior::create();
		core.set_standard_value(
			"file_in",
			-1,
			oaknode::value::NodeValue::Text(filename.clone()),
		);
		let id = {
			let mut g = lock(&p);
			g.graph.add_node(core, behavior)
		};
		{
			let mut g = lock(&p);
			if let Some(f) =
				behavior_of_mut::<oaknode::footage::FootageBehavior>(&mut g.graph, id)
			{
				f.filename = filename;
				// Best-effort probe: the stream metadata is filled when a
				// decoder recognizes the file; failures keep the node
				// usable (valid stays false).
				let _ = f.probe();
			}
		}
		make_node_handle(p, id, false)
	}

	/// `oaknode_footage_as_node` — identity cast (addref'd copy).
	pub fn oaknode_footage_as_node(footage: CHandle) -> CHandle {
		addref_copy(footage)
	}

	/// `oaknode_footage_filename` (two-stage).
	pub fn oaknode_footage_filename(footage: CHandle, buf: *mut c_char, buf_size: c_int) -> c_int {
		match footage_behavior(footage) {
			Some(f) => string_out(&f.filename, buf, buf_size),
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_footage_set_filename`.
	pub fn oaknode_footage_set_filename(footage: CHandle, filename: *const c_char) -> c_int {
		// SAFETY: the caller guarantees a valid NUL-terminated string.
		let filename = unsafe { cstr(filename) };
		match footage_behavior_mut(footage) {
			Some(f) => {
				f.filename = filename;
				f.valid = false;
				let _ = f.probe();
				OAKNODE_OK
			}
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_footage_is_valid` — 1/0.
	pub fn oaknode_footage_is_valid(footage: CHandle) -> c_int {
		match footage_behavior(footage) {
			Some(f) => {
				if f.valid {
					1
				} else {
					0
				}
			}
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_footage_timestamp`.
	pub fn oaknode_footage_timestamp(footage: CHandle, out_timestamp: *mut i64) -> c_int {
		if out_timestamp.is_null() {
			return OAKNODE_E_INVALID;
		}
		match footage_behavior(footage) {
			Some(f) => {
				// SAFETY: valid out pointer.
				unsafe { *out_timestamp = f.timestamp };
				OAKNODE_OK
			}
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_footage_set_timestamp`.
	pub fn oaknode_footage_set_timestamp(footage: CHandle, timestamp: i64) -> c_int {
		match footage_behavior_mut(footage) {
			Some(f) => {
				f.timestamp = timestamp;
				OAKNODE_OK
			}
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_footage_decoder` (two-stage).
	pub fn oaknode_footage_decoder(footage: CHandle, buf: *mut c_char, buf_size: c_int) -> c_int {
		match footage_behavior(footage) {
			Some(f) => string_out(&f.decoder, buf, buf_size),
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_footage_total_stream_count`.
	pub fn oaknode_footage_total_stream_count(footage: CHandle) -> c_int {
		match footage_behavior(footage) {
			Some(f) => f.total_stream_count() as c_int,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_footage_video_stream_count`.
	pub fn oaknode_footage_video_stream_count(footage: CHandle) -> c_int {
		match footage_behavior(footage) {
			Some(f) => f.video_stream_count() as c_int,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_footage_audio_stream_count`.
	pub fn oaknode_footage_audio_stream_count(footage: CHandle) -> c_int {
		match footage_behavior(footage) {
			Some(f) => f.audio_stream_count() as c_int,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_footage_subtitle_stream_count`.
	pub fn oaknode_footage_subtitle_stream_count(footage: CHandle) -> c_int {
		match footage_behavior(footage) {
			Some(f) => f.subtitle_stream_count() as c_int,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_footage_duration` — (num, den) of the longest stream.
	pub fn oaknode_footage_duration(
		footage: CHandle,
		out_numerator: *mut c_int,
		out_denominator: *mut c_int,
	) -> c_int {
		if out_numerator.is_null() || out_denominator.is_null() {
			return OAKNODE_E_INVALID;
		}
		match footage_behavior(footage) {
			Some(f) => {
				let d = f.duration();
				// SAFETY: valid out pointers.
				unsafe {
					*out_numerator = d.numerator() as c_int;
					*out_denominator = d.denominator() as c_int;
				}
				OAKNODE_OK
			}
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_footage_proxy_enabled`.
	pub fn oaknode_footage_proxy_enabled(footage: CHandle) -> c_int {
		match footage_behavior(footage) {
			Some(f) => {
				if f.proxy_enabled {
					1
				} else {
					0
				}
			}
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_footage_set_proxy_enabled`.
	pub fn oaknode_footage_set_proxy_enabled(footage: CHandle, enabled: c_int) -> c_int {
		match footage_behavior_mut(footage) {
			Some(f) => {
				f.proxy_enabled = enabled != 0;
				OAKNODE_OK
			}
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_footage_proxy_path` (two-stage).
	pub fn oaknode_footage_proxy_path(footage: CHandle, buf: *mut c_char, buf_size: c_int) -> c_int {
		match footage_behavior(footage) {
			Some(f) => string_out(&f.proxy, buf, buf_size),
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_footage_proxy_state`.
	pub fn oaknode_footage_proxy_state(footage: CHandle) -> c_int {
		match footage_behavior(footage) {
			Some(f) => f.proxy_state,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_footage_set_proxy`.
	pub fn oaknode_footage_set_proxy(
		footage: CHandle,
		path: *const c_char,
		state: c_int,
		video_stream_index: c_int,
		preset_version: c_int,
		enabled: c_int,
	) -> c_int {
		// SAFETY: the caller guarantees a valid NUL-terminated string.
		let path = unsafe { cstr(path) };
		match footage_behavior_mut(footage) {
			Some(f) => {
				f.set_proxy(
					&path,
					state,
					video_stream_index,
					preset_version,
					enabled != 0,
				);
				OAKNODE_OK
			}
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_footage_clear_proxy`.
	pub fn oaknode_footage_clear_proxy(footage: CHandle) -> c_int {
		match footage_behavior_mut(footage) {
			Some(f) => {
				f.clear_proxy();
				OAKNODE_OK
			}
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_footage_get_video_params` — stream `index` as an oakcommon
	/// params handle.
	pub fn oaknode_footage_get_video_params(
		footage: CHandle,
		index: c_int,
		out: *mut CHandle,
	) -> c_int {
		if out.is_null() || index < 0 {
			return OAKNODE_E_INVALID;
		}
		match footage_behavior(footage).and_then(|f| f.video_params(index as usize)) {
			Some(v) => {
				// SAFETY: valid out pointer.
				unsafe { *out = vp_handle(&v) };
				OAKNODE_OK
			}
			None => OAKNODE_E_NOT_FOUND,
		}
	}

	/// `oaknode_footage_set_video_params`.
	pub fn oaknode_footage_set_video_params(
		footage: CHandle,
		index: c_int,
		params: *const CHandle,
	) -> c_int {
		if params.is_null() || index < 0 {
			return OAKNODE_E_INVALID;
		}
		// SAFETY: the caller passes a live oakcommon videoparams handle.
		let converted = unsafe { vp_from_handle(*params) };
		let Some(converted) = converted else {
			return OAKNODE_E_INVALID;
		};
		let nr = match unsafe { node_ref_of(&footage) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		let mut g = lock(&nr.0);
		let f = match behavior_of_mut::<oaknode::footage::FootageBehavior>(&mut g.graph, nr.1) {
			Some(f) => f,
			None => return OAKNODE_E_NOT_FOUND,
		};
		let mut seen = 0usize;
		for s in f.streams.iter_mut().filter(|s| s.is_video) {
			if seen == index as usize {
				s.video = Some(converted);
				return OAKNODE_OK;
			}
			seen += 1;
		}
		OAKNODE_E_NOT_FOUND
	}

	/// `oaknode_footage_get_video_length` — (num, den) of the longest video
	/// stream.
	pub fn oaknode_footage_get_video_length(
		footage: CHandle,
		out_num: *mut i64,
		out_den: *mut i64,
	) -> c_int {
		if out_num.is_null() || out_den.is_null() {
			return OAKNODE_E_INVALID;
		}
		match footage_behavior(footage) {
			Some(f) => {
				let d = f.video_length();
				// SAFETY: valid out pointers.
				unsafe {
					*out_num = d.numerator();
					*out_den = d.denominator();
				}
				OAKNODE_OK
			}
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_footage_set_cancel_atom` — record the footage's cancel
	/// state from a shared oakcommon cancel atom handle.
	pub fn oaknode_footage_set_cancel_atom(footage: CHandle, atom: CHandle) -> c_int {
		let cancelled = if atom.is_null() {
			false
		} else {
			// SAFETY: the atom handle boxes an oakcommon CancelAtom.
			unsafe { oakcommon::handle::get::<oakcommon::cancelatom::CancelAtom>(&atom) }
				.map(|a| a.is_cancelled())
				.unwrap_or(false)
		};
		match footage_behavior_mut(footage) {
			Some(f) => {
				f.set_cancel(cancelled);
				OAKNODE_OK
			}
			None => OAKNODE_E_INVALID,
		}
	}
	// -------------------------------------------------------------------
	// Group family
	// -------------------------------------------------------------------

	/// `oaknode_group_create` — detached group node.
	pub fn oaknode_group_create() -> CHandle {
		make_detached(oaknode::nodes::group::create())
	}

	/// `oaknode_group_cast` — the same node when it is a group, else null.
	pub fn oaknode_group_cast(node: CHandle) -> CHandle {
		if is_type(node, "org.olivevideoeditor.Olive.group") {
			addref_copy(node)
		} else {
			CHandle::null()
		}
	}

	/// `oaknode_group_free` — same shell release as the node family.
	pub fn oaknode_group_free(group: *mut CHandle) {
		if group.is_null() {
			return;
		}
		// SAFETY: the caller passes a valid handle pointer.
		let h = unsafe { *group };
		free_node_handle(h);
		// SAFETY: the caller passes a valid handle pointer.
		unsafe { *group = CHandle::null() };
	}

	/// `oaknode_group_add_input_passthrough` — mint a passthrough input on
	/// the group mirroring `node`'s input; writes the minted id (two-stage
	/// getter convention).
	pub fn oaknode_group_add_input_passthrough(
		group: CHandle,
		node: CHandle,
		input_id: *const c_char,
		element: c_int,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		// SAFETY: the caller guarantees a valid NUL-terminated string.
		let input_id = unsafe { cstr(input_id) };
		let g_nr = match unsafe { node_ref_of(&group) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		let n_id = match unsafe { node_ref_of(&node) } {
			Some(nr) => nr.id,
			None => return OAKNODE_E_INVALID,
		};
		let mut g = lock(&g_nr.0);
		let descriptor = match g
			.graph
			.get(n_id)
			.and_then(|e| e.core.get_input(&input_id))
			.cloned()
		{
			Some(d) => d,
			None => return OAKNODE_E_NOT_FOUND,
		};
		let entry = match g.graph.get_mut(g_nr.1) {
			Some(e) => e,
			None => return OAKNODE_E_NOT_FOUND,
		};
		let (core, behavior) = (&mut entry.core, &mut entry.behavior);
		let group_b = match behavior.as_any_mut().and_then(|a| a.downcast_mut::<oaknode::nodes::group::NodeGroup>()) {
			Some(b) => b,
			None => return OAKNODE_E_NOT_FOUND,
		};
		let id = group_b.add_input_passthrough(
			core,
			oaknode::nodes::group::InnerInput {
				node: n_id,
				input: input_id,
				element,
			},
			"",
			&descriptor,
		);
		string_out(&id, buf, buf_size)
	}

	/// `oaknode_group_add_input_passthrough_undoable`.
	pub fn oaknode_group_add_input_passthrough_undoable(
		group: CHandle,
		node: CHandle,
		input_id: *const c_char,
		element: c_int,
		out_command: *mut CHandle,
	) -> c_int {
		if out_command.is_null() {
			return OAKNODE_E_INVALID;
		}
		// SAFETY: the caller guarantees a valid NUL-terminated string.
		let input_id = unsafe { cstr(input_id) };
		let g_nr = match unsafe { node_ref_of(&group) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		let n_id = match unsafe { node_ref_of(&node) } {
			Some(nr) => nr.id,
			None => return OAKNODE_E_INVALID,
		};
		let (project, g_id) = g_nr;
		let descriptor = {
			let g = lock(&project);
			match g
				.graph
				.get(n_id)
				.and_then(|e| e.core.get_input(&input_id))
				.cloned()
			{
				Some(d) => d,
				None => return OAKNODE_E_NOT_FOUND,
			}
		};
		let p1 = project.clone();
		let p2 = project;
		let input1 = input_id.clone();
		let input2 = input_id;
		let cmd = closure_command(
			move || {
				let mut g = lock(&p1);
				if let Some(entry) = g.graph.get_mut(g_id) {
					let (core, behavior) = (&mut entry.core, &mut entry.behavior);
					if let Some(b) = behavior
						.as_any_mut()
						.and_then(|a| a.downcast_mut::<oaknode::nodes::group::NodeGroup>())
					{
						b.add_input_passthrough(
							core,
							oaknode::nodes::group::InnerInput {
								node: n_id,
								input: input1.clone(),
								element,
							},
							"",
							&descriptor,
						);
					}
				}
			},
			move || {
				let mut g = lock(&p2);
				if let Some(entry) = g.graph.get_mut(g_id) {
					let (core, behavior) = (&mut entry.core, &mut entry.behavior);
					if let Some(b) = behavior
						.as_any_mut()
						.and_then(|a| a.downcast_mut::<oaknode::nodes::group::NodeGroup>())
					{
						b.remove_input_passthrough(
							core,
							&oaknode::nodes::group::InnerInput {
								node: n_id,
								input: input2.clone(),
								element,
							},
						);
					}
				}
			},
		);
		// SAFETY: valid out pointer.
		unsafe { *out_command = box_command(cmd) };
		OAKNODE_OK
	}

	/// `oaknode_group_remove_input_passthrough`.
	pub fn oaknode_group_remove_input_passthrough(
		group: CHandle,
		node: CHandle,
		input_id: *const c_char,
		element: c_int,
	) -> c_int {
		// SAFETY: the caller guarantees a valid NUL-terminated string.
		let input_id = unsafe { cstr(input_id) };
		let g_nr = match unsafe { node_ref_of(&group) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		let n_id = match unsafe { node_ref_of(&node) } {
			Some(nr) => nr.id,
			None => return OAKNODE_E_INVALID,
		};
		let mut g = lock(&g_nr.0);
		let entry = match g.graph.get_mut(g_nr.1) {
			Some(e) => e,
			None => return OAKNODE_E_NOT_FOUND,
		};
		let (core, behavior) = (&mut entry.core, &mut entry.behavior);
		let group_b = match behavior.as_any_mut().and_then(|a| a.downcast_mut::<oaknode::nodes::group::NodeGroup>()) {
			Some(b) => b,
			None => return OAKNODE_E_NOT_FOUND,
		};
		group_b.remove_input_passthrough(
			core,
			&oaknode::nodes::group::InnerInput {
				node: n_id,
				input: input_id,
				element,
			},
		);
		OAKNODE_OK
	}

	/// `oaknode_group_passthrough_count`.
	pub fn oaknode_group_passthrough_count(group: CHandle, out_count: *mut c_int) -> c_int {
		if out_count.is_null() {
			return OAKNODE_E_INVALID;
		}
		match with_node(group, |g, id| {
			behavior_of::<oaknode::nodes::group::NodeGroup>(g, id)
				.map(|b| b.passthroughs().len())
		}) {
			Some(Some(n)) => {
				// SAFETY: valid out pointer.
				unsafe { *out_count = n as c_int };
				OAKNODE_OK
			}
			Some(None) => OAKNODE_E_NOT_FOUND,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_group_passthrough_id_at` (two-stage).
	pub fn oaknode_group_passthrough_id_at(
		group: CHandle,
		index: c_int,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		if index < 0 {
			return OAKNODE_E_NOT_FOUND;
		}
		match with_node(group, |g, id| {
			behavior_of::<oaknode::nodes::group::NodeGroup>(g, id).and_then(|b| {
				b.passthroughs()
					.get(index as usize)
					.map(|(pid, _)| pid.clone())
			})
		}) {
			Some(Some(s)) => string_out(&s, buf, buf_size),
			Some(None) => OAKNODE_E_NOT_FOUND,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_group_passthrough_input_at` — inner node + input id +
	/// element (string via the two-stage convention).
	pub fn oaknode_group_passthrough_input_at(
		group: CHandle,
		index: c_int,
		out_node: *mut CHandle,
		buf: *mut c_char,
		buf_size: c_int,
		out_element: *mut c_int,
	) -> c_int {
		if out_node.is_null() || out_element.is_null() || index < 0 {
			return OAKNODE_E_INVALID;
		}
		let nr = match unsafe { node_ref_of(&group) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		let (project, id) = nr;
		let inner = {
			let g = lock(&project);
			behavior_of::<oaknode::nodes::group::NodeGroup>(&g.graph, id)
				.and_then(|b| b.passthroughs().get(index as usize))
				.map(|(_, inner)| inner.clone())
		};
		match inner {
			Some(inner) => {
				// SAFETY: valid out pointers.
				unsafe {
					*out_node = make_node_handle(project, inner.node, false);
					*out_element = inner.element;
				}
				string_out(&inner.input, buf, buf_size)
			}
			None => OAKNODE_E_NOT_FOUND,
		}
	}

	/// `oaknode_group_get_output_passthrough` — null when unset.
	pub fn oaknode_group_get_output_passthrough(group: CHandle, out_node: *mut CHandle) -> c_int {
		if out_node.is_null() {
			return OAKNODE_E_INVALID;
		}
		let nr = match unsafe { node_ref_of(&group) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		let (project, id) = nr;
		let target = {
			let g = lock(&project);
			behavior_of::<oaknode::nodes::group::NodeGroup>(&g.graph, id)
				.and_then(|b| b.output_passthrough())
		};
		// SAFETY: valid out pointer.
		unsafe {
			*out_node = match target {
				Some(t) => make_node_handle(project, t, false),
				None => CHandle::null(),
			};
		}
		OAKNODE_OK
	}

	/// `oaknode_group_set_output_passthrough`.
	pub fn oaknode_group_set_output_passthrough(group: CHandle, node: CHandle) -> c_int {
		let g_nr = match unsafe { node_ref_of(&group) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		let n_id = match unsafe { node_ref_of(&node) } {
			Some(nr) => nr.id,
			None => return OAKNODE_E_INVALID,
		};
		let mut g = lock(&g_nr.0);
		match behavior_of_mut::<oaknode::nodes::group::NodeGroup>(&mut g.graph, g_nr.1) {
			Some(b) => {
				b.set_output_passthrough(Some(n_id));
				OAKNODE_OK
			}
			None => OAKNODE_E_NOT_FOUND,
		}
	}

	/// `oaknode_group_set_output_passthrough_undoable`.
	pub fn oaknode_group_set_output_passthrough_undoable(
		group: CHandle,
		node: CHandle,
		out_command: *mut CHandle,
	) -> c_int {
		if out_command.is_null() {
			return OAKNODE_E_INVALID;
		}
		let g_nr = match unsafe { node_ref_of(&group) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		let n_id = match unsafe { node_ref_of(&node) } {
			Some(nr) => nr.id,
			None => return OAKNODE_E_INVALID,
		};
		let (project, g_id) = g_nr;
		let old = {
			let g = lock(&project);
			behavior_of::<oaknode::nodes::group::NodeGroup>(&g.graph, g_id)
				.and_then(|b| b.output_passthrough())
		};
		let p1 = project.clone();
		let p2 = project;
		let cmd = closure_command(
			move || {
				let mut g = lock(&p1);
				if let Some(b) =
					behavior_of_mut::<oaknode::nodes::group::NodeGroup>(&mut g.graph, g_id)
				{
					b.set_output_passthrough(Some(n_id));
				}
			},
			move || {
				let mut g = lock(&p2);
				if let Some(b) =
					behavior_of_mut::<oaknode::nodes::group::NodeGroup>(&mut g.graph, g_id)
				{
					b.set_output_passthrough(old);
				}
			},
		);
		// SAFETY: valid out pointer.
		unsafe { *out_command = box_command(cmd) };
		OAKNODE_OK
	}

	/// `oaknode_group_resolve_input` — resolve a group input through the
	/// passthrough table (string via the two-stage convention; node null
	/// when the input does not map to a passthrough).
	pub fn oaknode_group_resolve_input(
		node: CHandle,
		input_id: *const c_char,
		element: c_int,
		out_node: *mut CHandle,
		buf: *mut c_char,
		buf_size: c_int,
		out_element: *mut c_int,
	) -> c_int {
		if out_node.is_null() || out_element.is_null() {
			return OAKNODE_E_INVALID;
		}
		// SAFETY: the caller guarantees a valid NUL-terminated string.
		let input_id = unsafe { cstr(input_id) };
		let nr = match unsafe { node_ref_of(&node) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		let (project, id) = nr;
		let resolved = {
			let g = lock(&project);
			let group_b = match behavior_of::<oaknode::nodes::group::NodeGroup>(&g.graph, id) {
				Some(b) => b,
				None => return OAKNODE_E_NOT_FOUND,
			};
			match group_b.input_from_id(&input_id) {
				Some(inner) => {
					Some(oaknode::nodes::group::NodeGroup::resolve_input(&g.graph, inner.clone()))
				}
				None => None,
			}
		};
		match resolved {
			Some(inner) => {
				// SAFETY: valid out pointers.
				unsafe {
					*out_node = make_node_handle(project, inner.node, false);
					*out_element = inner.element;
				}
				string_out(&inner.input, buf, buf_size)
			}
			None => {
				// SAFETY: valid out pointers; the engine substitutes the
				// group itself for a null resolved node.
				unsafe {
					*out_node = CHandle::null();
					*out_element = element;
				}
				string_out(&input_id, buf, buf_size)
			}
		}
	}

	// -------------------------------------------------------------------
	// Multicam family (static input ids + math over the multicam node)
	// -------------------------------------------------------------------

	/// `oaknode_multicam_input_current`.
	pub fn oaknode_multicam_input_current() -> *const c_char {
		static S: &[u8] = b"current_in\0";
		S.as_ptr() as *const c_char
	}

	/// `oaknode_multicam_input_sources`.
	pub fn oaknode_multicam_input_sources() -> *const c_char {
		static S: &[u8] = b"sources_in\0";
		S.as_ptr() as *const c_char
	}

	/// `oaknode_multicam_input_sequence`.
	pub fn oaknode_multicam_input_sequence() -> *const c_char {
		static S: &[u8] = b"sequence_in\0";
		S.as_ptr() as *const c_char
	}

	/// `oaknode_multicam_input_sequence_type`.
	pub fn oaknode_multicam_input_sequence_type() -> *const c_char {
		static S: &[u8] = b"sequence_type_in\0";
		S.as_ptr() as *const c_char
	}

	/// `oaknode_multicam_get_source_count` — the `sources_in` array size
	/// (the domain query is core-only and not re-exported, so it is read
	/// directly).
	pub fn oaknode_multicam_get_source_count(node: CHandle, out_count: *mut c_int) -> c_int {
		if out_count.is_null() {
			return OAKNODE_E_INVALID;
		}
		match with_node(node, |g, id| {
			g.get(id)
				.map(|e| e.core.input_array_size("sources_in") as i32)
		}) {
			Some(Some(n)) => {
				// SAFETY: valid out pointer.
				unsafe { *out_count = n };
				OAKNODE_OK
			}
			Some(None) => OAKNODE_E_NOT_FOUND,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_multicam_get_rows_and_columns` — the C++ grid math (the
	/// multicam node module is private, so the statics are re-implemented
	/// here).
	pub fn oaknode_multicam_get_rows_and_columns(
		source_count: c_int,
		rows: *mut c_int,
		cols: *mut c_int,
	) -> c_int {
		if rows.is_null() || cols.is_null() || source_count < 0 {
			return OAKNODE_E_INVALID;
		}
		let (mut r, mut c) = (1, 1);
		while r * c < source_count {
			if r < c {
				r += 1;
			} else {
				c += 1;
			}
		}
		// SAFETY: valid out pointers.
		unsafe {
			*rows = r;
			*cols = c;
		}
		OAKNODE_OK
	}

	/// `oaknode_multicam_index_to_row_cols` — `row = index / cols`,
	/// `col = index % cols` (C++ parity; `rows` is unused there too).
	pub fn oaknode_multicam_index_to_row_cols(
		index: c_int,
		rows: c_int,
		cols: c_int,
		out_row: *mut c_int,
		out_col: *mut c_int,
	) -> c_int {
		if out_row.is_null() || out_col.is_null() || index < 0 || rows < 1 || cols < 1 {
			return OAKNODE_E_INVALID;
		}
		// SAFETY: valid out pointers.
		unsafe {
			*out_row = index / cols;
			*out_col = index % cols;
		}
		OAKNODE_OK
	}

	/// `oaknode_multicam_rows_cols_to_index` — `col + row * cols` (C++
	/// parity).
	pub fn oaknode_multicam_rows_cols_to_index(
		row: c_int,
		col: c_int,
		rows: c_int,
		cols: c_int,
	) -> c_int {
		if row < 0 || col < 0 || rows < 1 || cols < 1 {
			return OAKNODE_E_INVALID;
		}
		col + row * cols
	}

	/// `oaknode_multicam_get_current_source` — the `current_in` standard
	/// value as int (the domain query is core-only and not re-exported).
	pub fn oaknode_multicam_get_current_source(node: CHandle, out_source: *mut c_int) -> c_int {
		if out_source.is_null() {
			return OAKNODE_E_INVALID;
		}
		match with_node(node, |g, id| {
			g.get(id)
				.map(|e| e.core.standard_value("current_in", -1).to_double() as i32)
		}) {
			Some(Some(s)) => {
				// SAFETY: valid out pointer.
				unsafe { *out_source = s };
				OAKNODE_OK
			}
			Some(None) => OAKNODE_E_NOT_FOUND,
			None => OAKNODE_E_INVALID,
		}
	}
	// -------------------------------------------------------------------
	// Sequence family
	// -------------------------------------------------------------------

	fn seq_behavior(h: CHandle) -> Option<&'static oaknode::sequence::SequenceBehavior> {
		let nr = unsafe { node_ref_of(&h) }?;
		let p = lock(&nr.project);
		let b = behavior_of::<oaknode::sequence::SequenceBehavior>(&p.graph, nr.id)?;
		// SAFETY: the node lives in the arena; the project outlives every
		// handle that references it.
		unsafe { Some(&*(b as *const _)) }
	}

	fn seq_behavior_mut(h: CHandle) -> Option<&'static mut oaknode::sequence::SequenceBehavior> {
		let nr = unsafe { node_ref_of(&h) }?;
		let mut p = lock(&nr.project);
		let b = behavior_of_mut::<oaknode::sequence::SequenceBehavior>(&mut p.graph, nr.id)?;
		// SAFETY: see `seq_behavior`.
		unsafe { Some(&mut *(b as *mut _)) }
	}

	/// Block-range accessor backed by the graph arena.
	struct GraphBlockRange<'a>(&'a Graph);

	impl<'a> oaknode::track::BlockRange for GraphBlockRange<'a> {
		fn in_(&self, block: NodeId) -> Rational {
			block_core(self.0, block)
				.map(|c| c.in_())
				.unwrap_or_else(|| Rational::new(0, 1))
		}

		fn out(&self, block: NodeId) -> Rational {
			block_core(self.0, block)
				.map(|c| c.out())
				.unwrap_or_else(|| Rational::new(0, 1))
		}
	}

	/// Track-length accessor backed by the graph arena.
	struct GraphTrackRange<'a>(&'a Graph);

	impl<'a> oaknode::track::TrackRange for GraphTrackRange<'a> {
		fn length(&self, track: NodeId) -> Rational {
			let blocks = GraphBlockRange(self.0);
			behavior_of::<oaknode::track::TrackBehavior>(self.0, track)
				.map(|t| t.length(&blocks))
				.unwrap_or_else(|| Rational::new(0, 1))
		}
	}

	/// The block core of a block node (clip/gap/transition).
	fn block_core(g: &Graph, id: NodeId) -> Option<&oaknode::block::BlockCore> {
		let e = g.get(id)?;
		if let Some(clip) = e.behavior.as_any().and_then(|a| a.downcast_ref::<oaknode::block::ClipBlockBehavior>())
		{
			return Some(&clip.core);
		}
		if let Some(gap) = e.behavior.as_any().and_then(|a| a.downcast_ref::<oaknode::block::GapBlockBehavior>())
		{
			return Some(&gap.core);
		}
		if let Some(tr) = e
			.behavior
			.as_any()
			.and_then(|a| a.downcast_ref::<oaknode::block::TransitionBlockBehavior>())
		{
			return Some(&tr.core);
		}
		None
	}

	/// Mutable block core.
	fn block_core_mut<'a>(g: &'a mut Graph, id: NodeId) -> Option<&'a mut oaknode::block::BlockCore> {
		let e = g.get_mut(id)?;
		let a = e.behavior.as_any_mut()?;
		if a.is::<oaknode::block::ClipBlockBehavior>() {
			return Some(&mut a
				.downcast_mut::<oaknode::block::ClipBlockBehavior>()
				.expect("is-checked")
				.core);
		}
		if a.is::<oaknode::block::GapBlockBehavior>() {
			return Some(&mut a
				.downcast_mut::<oaknode::block::GapBlockBehavior>()
				.expect("is-checked")
				.core);
		}
		if a.is::<oaknode::block::TransitionBlockBehavior>() {
			return Some(&mut a
				.downcast_mut::<oaknode::block::TransitionBlockBehavior>()
				.expect("is-checked")
				.core);
		}
		None
	}

	/// `oaknode_sequence_create` — detached sequence node.
	pub fn oaknode_sequence_create() -> CHandle {
		make_detached(oaknode::sequence::SequenceBehavior::create())
	}

	/// `oaknode_sequence_free` — same shell release as the node family.
	pub fn oaknode_sequence_free(sequence: *mut CHandle) {
		if sequence.is_null() {
			return;
		}
		// SAFETY: the caller passes a valid handle pointer.
		let h = unsafe { *sequence };
		free_node_handle(h);
		// SAFETY: the caller passes a valid handle pointer.
		unsafe { *sequence = CHandle::null() };
	}

	/// `oaknode_sequence_set_default_parameters`.
	pub fn oaknode_sequence_set_default_parameters(sequence: CHandle) -> c_int {
		match seq_behavior_mut(sequence) {
			Some(s) => {
				s.set_default_parameters();
				OAKNODE_OK
			}
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_sequence_as_node` — identity cast (addref'd copy).
	pub fn oaknode_sequence_as_node(sequence: CHandle) -> CHandle {
		addref_copy(sequence)
	}

	/// `oaknode_sequence_from_node` — the same node when it is a sequence,
	/// else null.
	pub fn oaknode_sequence_from_node(node: CHandle) -> CHandle {
		if is_type(node, "org.olivevideoeditor.Olive.sequence") {
			addref_copy(node)
		} else {
			CHandle::null()
		}
	}

	/// Find (or create) the track list of `kind` on the sequence.
	fn sequence_track_list_id(project: &ProjectArc, seq_id: NodeId, kind: oaknode::track::TrackType) -> Option<NodeId> {
		// Find an existing list of the kind.
		{
			let g = lock(project);
			let seq = behavior_of::<oaknode::sequence::SequenceBehavior>(&g.graph, seq_id)?;
			for tl in &seq.track_lists {
				if let Some(list) = behavior_of::<oaknode::track::TrackListBehavior>(&g.graph, *tl)
				{
					if list.kind == kind {
						return Some(*tl);
					}
				}
			}
		}
		// Create it: the list is a graph node owned by the sequence.
		let mut g = lock(project);
		let (core, mut behavior) = oaknode::track::TrackListBehavior::create();
		if let Some(a) = behavior.as_any_mut() {
			if let Some(list) = a.downcast_mut::<oaknode::track::TrackListBehavior>() {
				list.kind = kind;
				let base = match behavior_of::<oaknode::sequence::SequenceBehavior>(&g.graph, seq_id)
				{
					Some(s) => s.track_lists.len() as i32,
					None => return None,
				};
				list.array_base = base;
			}
		}
		let list_id = g.graph.add_node(core, behavior);
		if let Some(seq) = behavior_of_mut::<oaknode::sequence::SequenceBehavior>(&mut g.graph, seq_id)
		{
			seq.track_lists.push(list_id);
		}
		if let Some(list) = behavior_of_mut::<oaknode::track::TrackListBehavior>(&mut g.graph, list_id)
		{
			list.sequence = Some(seq_id);
		}
		Some(list_id)
	}

	/// `oaknode_sequence_get_track_list` — find-or-create the list of the
	/// given type (borrowed handle).
	pub fn oaknode_sequence_get_track_list(
		sequence: CHandle,
		type_: c_int,
		out: *mut CHandle,
	) -> c_int {
		if out.is_null() {
			return OAKNODE_E_INVALID;
		}
		let kind = match oaknode::track::TrackType::from_c(type_) {
			Some(k) => k,
			None => return OAKNODE_E_INVALID,
		};
		let nr = match unsafe { node_ref_of(&sequence) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		let (project, id) = nr;
		match sequence_track_list_id(&project, id, kind) {
			Some(list_id) => {
				// SAFETY: valid out pointer.
				unsafe { *out = make_node_handle(project, list_id, false) };
				OAKNODE_OK
			}
			None => OAKNODE_E_NOT_FOUND,
		}
	}

	/// `oaknode_sequence_get_track_count` — tracks of `type_`.
	pub fn oaknode_sequence_get_track_count(
		sequence: CHandle,
		type_: c_int,
		count: *mut c_int,
	) -> c_int {
		if count.is_null() {
			return OAKNODE_E_INVALID;
		}
		let kind = match oaknode::track::TrackType::from_c(type_) {
			Some(k) => k,
			None => return OAKNODE_E_INVALID,
		};
		let nr = match unsafe { node_ref_of(&sequence) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		let (project, id) = nr;
		let n = match sequence_track_list_id(&project, id, kind) {
			Some(list_id) => {
				let g = lock(&project);
				behavior_of::<oaknode::track::TrackListBehavior>(&g.graph, list_id)
					.map(|l| l.tracks.len())
					.unwrap_or(0)
			}
			None => 0,
		};
		// SAFETY: valid out pointer.
		unsafe { *count = n as c_int };
		OAKNODE_OK
	}

	/// `oaknode_sequence_get_track_at`.
	pub fn oaknode_sequence_get_track_at(
		sequence: CHandle,
		type_: c_int,
		index: c_int,
		out: *mut CHandle,
	) -> c_int {
		if out.is_null() || index < 0 {
			return OAKNODE_E_INVALID;
		}
		let kind = match oaknode::track::TrackType::from_c(type_) {
			Some(k) => k,
			None => return OAKNODE_E_INVALID,
		};
		let nr = match unsafe { node_ref_of(&sequence) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		let (project, id) = nr;
		let target = match sequence_track_list_id(&project, id, kind) {
			Some(list_id) => {
				let g = lock(&project);
				behavior_of::<oaknode::track::TrackListBehavior>(&g.graph, list_id)
					.and_then(|l| l.tracks.get(index as usize).copied())
			}
			None => None,
		};
		// SAFETY: valid out pointer.
		unsafe {
			*out = match target {
				Some(t) => make_node_handle(project, t, false),
				None => CHandle::null(),
			};
		}
		if target.is_some() {
			OAKNODE_OK
		} else {
			OAKNODE_E_NOT_FOUND
		}
	}

	/// All track lists of the sequence (list ids).
	fn sequence_track_lists(h: CHandle) -> Option<Vec<NodeId>> {
		with_node(h, |g, id| {
			behavior_of::<oaknode::sequence::SequenceBehavior>(g, id)
				.map(|s| s.track_lists.clone())
		})?
	}

	/// `oaknode_sequence_get_all_track_count`.
	pub fn oaknode_sequence_get_all_track_count(sequence: CHandle, count: *mut c_int) -> c_int {
		if count.is_null() {
			return OAKNODE_E_INVALID;
		}
		let lists = match sequence_track_lists(sequence) {
			Some(l) => l,
			None => return OAKNODE_E_INVALID,
		};
		let nr = unsafe { node_ref_of(&sequence) }.map(|n| n.project.clone());
		let Some(project) = nr else {
			return OAKNODE_E_INVALID;
		};
		let g = lock(&project);
		let mut total = 0usize;
		for list_id in &lists {
			if let Some(l) = behavior_of::<oaknode::track::TrackListBehavior>(&g.graph, *list_id) {
				total += l.tracks.len();
			}
		}
		// SAFETY: valid out pointer.
		unsafe { *count = total as c_int };
		OAKNODE_OK
	}

	/// `oaknode_sequence_get_all_track_at` — flat track index across all
	/// lists.
	pub fn oaknode_sequence_get_all_track_at(
		sequence: CHandle,
		index: c_int,
		out: *mut CHandle,
	) -> c_int {
		if out.is_null() || index < 0 {
			return OAKNODE_E_INVALID;
		}
		let nr = match unsafe { node_ref_of(&sequence) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		let (project, id) = nr;
		let target = {
			let g = lock(&project);
			let seq = behavior_of::<oaknode::sequence::SequenceBehavior>(&g.graph, id);
			let mut flat = index as usize;
			let mut target = None;
			if let Some(seq) = seq {
				for list_id in &seq.track_lists {
					if let Some(l) =
						behavior_of::<oaknode::track::TrackListBehavior>(&g.graph, *list_id)
					{
						if flat < l.tracks.len() {
							target = l.tracks.get(flat).copied();
							break;
						}
						flat -= l.tracks.len();
					}
				}
			}
			target
		};
		// SAFETY: valid out pointer.
		unsafe {
			*out = match target {
				Some(t) => make_node_handle(project, t, false),
				None => CHandle::null(),
			};
		}
		if target.is_some() {
			OAKNODE_OK
		} else {
			OAKNODE_E_NOT_FOUND
		}
	}

	/// `oaknode_sequence_get_playhead`.
	pub fn oaknode_sequence_get_playhead(
		sequence: CHandle,
		numerator: *mut c_int,
		denominator: *mut c_int,
	) -> c_int {
		if numerator.is_null() || denominator.is_null() {
			return OAKNODE_E_INVALID;
		}
		match seq_behavior(sequence) {
			Some(s) => {
				// SAFETY: valid out pointers.
				unsafe {
					*numerator = s.playhead.numerator() as c_int;
					*denominator = s.playhead.denominator() as c_int;
				}
				OAKNODE_OK
			}
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_sequence_set_playhead`.
	pub fn oaknode_sequence_set_playhead(
		sequence: CHandle,
		numerator: c_int,
		denominator: c_int,
	) -> c_int {
		if denominator == 0 {
			return OAKNODE_E_INVALID;
		}
		match seq_behavior_mut(sequence) {
			Some(s) => {
				s.playhead = Rational::new(numerator as i64, denominator as i64);
				OAKNODE_OK
			}
			None => OAKNODE_E_INVALID,
		}
	}

	/// The sequence's overall content length (max track length).
	fn sequence_length(h: CHandle) -> Option<Rational> {
		let nr = unsafe { node_ref_of(&h) }?;
		let g = lock(&nr.project);
		let seq = behavior_of::<oaknode::sequence::SequenceBehavior>(&g.graph, nr.id)?;
		let tracks = GraphTrackRange(&g.graph);
		let blocks = GraphBlockRange(&g.graph);
		let mut longest = Rational::new(0, 1);
		for list_id in &seq.track_lists {
			if let Some(list) = behavior_of::<oaknode::track::TrackListBehavior>(&g.graph, *list_id) {
				let len = list.total_length(&tracks);
				if len > longest {
					longest = len;
				}
				let _ = &blocks;
			}
		}
		Some(longest)
	}

	/// `oaknode_sequence_get_length`.
	pub fn oaknode_sequence_get_length(
		sequence: CHandle,
		numerator: *mut c_int,
		denominator: *mut c_int,
	) -> c_int {
		if numerator.is_null() || denominator.is_null() {
			return OAKNODE_E_INVALID;
		}
		match sequence_length(sequence) {
			Some(l) => {
				// SAFETY: valid out pointers.
				unsafe {
					*numerator = l.numerator() as c_int;
					*denominator = l.denominator() as c_int;
				}
				OAKNODE_OK
			}
			None => OAKNODE_E_INVALID,
		}
	}

	/// Length of the video track list.
	fn sequence_length_of_type(h: CHandle, kind: oaknode::track::TrackType) -> Option<Rational> {
		let nr = unsafe { node_ref_of(&h) }?;
		let g = lock(&nr.project);
		let seq = behavior_of::<oaknode::sequence::SequenceBehavior>(&g.graph, nr.id)?;
		let tracks = GraphTrackRange(&g.graph);
		for list_id in &seq.track_lists {
			if let Some(list) = behavior_of::<oaknode::track::TrackListBehavior>(&g.graph, *list_id) {
				if list.kind == kind {
					return Some(list.total_length(&tracks));
				}
			}
		}
		Some(Rational::new(0, 1))
	}

	/// `oaknode_sequence_get_video_length`.
	pub fn oaknode_sequence_get_video_length(
		sequence: CHandle,
		numerator: *mut c_int,
		denominator: *mut c_int,
	) -> c_int {
		if numerator.is_null() || denominator.is_null() {
			return OAKNODE_E_INVALID;
		}
		match sequence_length_of_type(sequence, oaknode::track::TrackType::Video) {
			Some(l) => {
				// SAFETY: valid out pointers.
				unsafe {
					*numerator = l.numerator() as c_int;
					*denominator = l.denominator() as c_int;
				}
				OAKNODE_OK
			}
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_sequence_get_audio_length`.
	pub fn oaknode_sequence_get_audio_length(
		sequence: CHandle,
		numerator: *mut c_int,
		denominator: *mut c_int,
	) -> c_int {
		if numerator.is_null() || denominator.is_null() {
			return OAKNODE_E_INVALID;
		}
		match sequence_length_of_type(sequence, oaknode::track::TrackType::Audio) {
			Some(l) => {
				// SAFETY: valid out pointers.
				unsafe {
					*numerator = l.numerator() as c_int;
					*denominator = l.denominator() as c_int;
				}
				OAKNODE_OK
			}
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_sequence_verify_length` — recompute and cache the lengths.
	pub fn oaknode_sequence_verify_length(sequence: CHandle) -> c_int {
		let overall = match sequence_length(sequence) {
			Some(l) => l,
			None => return OAKNODE_E_INVALID,
		};
		let video = sequence_length_of_type(sequence, oaknode::track::TrackType::Video)
			.unwrap_or_else(|| Rational::new(0, 1));
		let audio = sequence_length_of_type(sequence, oaknode::track::TrackType::Audio)
			.unwrap_or_else(|| Rational::new(0, 1));
		match seq_behavior_mut(sequence) {
			Some(s) => {
				s.verify_length((video, audio, overall));
				OAKNODE_OK
			}
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_sequence_get_video_stream_count`.
	pub fn oaknode_sequence_get_video_stream_count(sequence: CHandle, count: *mut c_int) -> c_int {
		if count.is_null() {
			return OAKNODE_E_INVALID;
		}
		match seq_behavior(sequence) {
			Some(s) => {
				// SAFETY: valid out pointer.
				unsafe { *count = s.video_stream_count() as c_int };
				OAKNODE_OK
			}
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_sequence_get_audio_stream_count`.
	pub fn oaknode_sequence_get_audio_stream_count(sequence: CHandle, count: *mut c_int) -> c_int {
		if count.is_null() {
			return OAKNODE_E_INVALID;
		}
		match seq_behavior(sequence) {
			Some(s) => {
				// SAFETY: valid out pointer.
				unsafe { *count = s.audio_stream_count() as c_int };
				OAKNODE_OK
			}
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_sequence_get_video_params` — stream `index` as an
	/// oakcommon params handle.
	pub fn oaknode_sequence_get_video_params(
		sequence: CHandle,
		index: c_int,
		out: *mut CHandle,
	) -> c_int {
		if out.is_null() || index < 0 {
			return OAKNODE_E_INVALID;
		}
		match seq_behavior(sequence).and_then(|s| s.video_params.get(index as usize)) {
			Some(v) => {
				// SAFETY: valid out pointer.
				unsafe { *out = vp_handle(v) };
				OAKNODE_OK
			}
			None => OAKNODE_E_NOT_FOUND,
		}
	}

	/// `oaknode_sequence_set_video_params`.
	pub fn oaknode_sequence_set_video_params(
		sequence: CHandle,
		index: c_int,
		params: CHandle,
	) -> c_int {
		if index < 0 {
			return OAKNODE_E_INVALID;
		}
		// SAFETY: the caller passes a live oakcommon videoparams handle.
		let converted = unsafe { vp_from_handle(params) };
		let Some(converted) = converted else {
			return OAKNODE_E_INVALID;
		};
		match seq_behavior_mut(sequence) {
			Some(s) => {
				if index as usize >= s.video_params.len() {
					s.video_params.resize(index as usize + 1, converted.clone());
				}
				s.video_params[index as usize] = converted;
				OAKNODE_OK
			}
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_sequence_get_audio_params` — host oakcore audioparams
	/// pointer.
	pub fn oaknode_sequence_get_audio_params(
		sequence: CHandle,
		index: c_int,
		out: *mut *mut c_void,
	) -> c_int {
		if out.is_null() || index < 0 {
			return OAKNODE_E_INVALID;
		}
		let params = match seq_behavior(sequence).and_then(|s| s.audio_params.get(index as usize)) {
			Some(p) => *p,
			None => return OAKNODE_E_NOT_FOUND,
		};
		// SAFETY: the oakcore audioparams contract.
		let ptr = unsafe {
			crate::stubs::audio::oakcore_audioparams_create(
				params.sample_rate,
				params.channel_layout,
				params.format,
			)
		};
		if ptr.is_null() {
			return OAKNODE_E_NOMEM;
		}
		// SAFETY: valid out pointer.
		unsafe { *out = ptr };
		OAKNODE_OK
	}

	/// `oaknode_sequence_set_audio_params`.
	pub fn oaknode_sequence_set_audio_params(
		sequence: CHandle,
		index: c_int,
		params: *const c_void,
	) -> c_int {
		if params.is_null() || index < 0 {
			return OAKNODE_E_INVALID;
		}
		// SAFETY: the oakcore audioparams contract.
		let converted = unsafe {
			oaknode::value::AudioParams {
				sample_rate: crate::stubs::audio::oakcore_audioparams_sample_rate(params),
				channel_layout: crate::stubs::audio::oakcore_audioparams_channel_layout(params),
				format: crate::stubs::audio::oakcore_audioparams_format(params),
			}
		};
		match seq_behavior_mut(sequence) {
			Some(s) => {
				if index as usize >= s.audio_params.len() {
					s.audio_params.resize(index as usize + 1, converted.clone());
				}
				s.audio_params[index as usize] = converted;
				OAKNODE_OK
			}
			None => OAKNODE_E_INVALID,
		}
	}
	// -------------------------------------------------------------------
	// Track family
	// -------------------------------------------------------------------

	fn track_behavior(h: CHandle) -> Option<&'static oaknode::track::TrackBehavior> {
		let nr = unsafe { node_ref_of(&h) }?;
		let p = lock(&nr.project);
		let b = behavior_of::<oaknode::track::TrackBehavior>(&p.graph, nr.id)?;
		// SAFETY: the node lives in the arena; the project outlives every
		// handle that references it.
		unsafe { Some(&*(b as *const _)) }
	}

	fn track_behavior_mut(h: CHandle) -> Option<&'static mut oaknode::track::TrackBehavior> {
		let nr = unsafe { node_ref_of(&h) }?;
		let mut p = lock(&nr.project);
		let b = behavior_of_mut::<oaknode::track::TrackBehavior>(&mut p.graph, nr.id)?;
		// SAFETY: see `track_behavior`.
		unsafe { Some(&mut *(b as *mut _)) }
	}

	fn tracklist_behavior(h: CHandle) -> Option<&'static oaknode::track::TrackListBehavior> {
		let nr = unsafe { node_ref_of(&h) }?;
		let p = lock(&nr.project);
		let b = behavior_of::<oaknode::track::TrackListBehavior>(&p.graph, nr.id)?;
		// SAFETY: see `track_behavior`.
		unsafe { Some(&*(b as *const _)) }
	}

	fn tracklist_behavior_mut(h: CHandle) -> Option<&'static mut oaknode::track::TrackListBehavior> {
		let nr = unsafe { node_ref_of(&h) }?;
		let mut p = lock(&nr.project);
		let b = behavior_of_mut::<oaknode::track::TrackListBehavior>(&mut p.graph, nr.id)?;
		// SAFETY: see `track_behavior`.
		unsafe { Some(&mut *(b as *mut _)) }
	}

	/// `oaknode_track_as_node` — the same node when it is a track, else
	/// null.
	pub fn oaknode_track_as_node(track: CHandle) -> CHandle {
		if is_type(track, "org.olivevideoeditor.Olive.track") {
			addref_copy(track)
		} else {
			CHandle::null()
		}
	}

	/// `oaknode_track_create` — detached track of the given type.
	pub fn oaknode_track_create(type_: c_int) -> CHandle {
		let kind = match oaknode::track::TrackType::from_c(type_) {
			Some(k) => k,
			None => return CHandle::null(),
		};
		let (core, behavior) = oaknode::track::TrackBehavior::create();
		// `create()` always yields a video track; specialize the kind.
		let mut b = behavior;
		if let Some(a) = b.as_any_mut() {
			if let Some(t) = a.downcast_mut::<oaknode::track::TrackBehavior>() {
				t.kind = kind;
			}
		}
		make_detached((core, b))
	}

	/// `oaknode_track_free` — same shell release as the node family.
	pub fn oaknode_track_free(track: *mut CHandle) {
		if track.is_null() {
			return;
		}
		// SAFETY: the caller passes a valid handle pointer.
		let h = unsafe { *track };
		free_node_handle(h);
		// SAFETY: the caller passes a valid handle pointer.
		unsafe { *track = CHandle::null() };
	}

	/// `oaknode_track_get_type`.
	pub fn oaknode_track_get_type(track: CHandle, type_: *mut c_int) -> c_int {
		if type_.is_null() {
			return OAKNODE_E_INVALID;
		}
		match track_behavior(track) {
			Some(t) => {
				// SAFETY: valid out pointer.
				unsafe { *type_ = t.kind.to_c() };
				OAKNODE_OK
			}
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_track_set_type`.
	pub fn oaknode_track_set_type(track: CHandle, type_: c_int) -> c_int {
		let kind = match oaknode::track::TrackType::from_c(type_) {
			Some(k) => k,
			None => return OAKNODE_E_INVALID,
		};
		match track_behavior_mut(track) {
			Some(t) => {
				t.kind = kind;
				OAKNODE_OK
			}
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_track_get_height`.
	pub fn oaknode_track_get_height(track: CHandle, height: *mut f64) -> c_int {
		if height.is_null() {
			return OAKNODE_E_INVALID;
		}
		match track_behavior(track) {
			Some(t) => {
				// SAFETY: valid out pointer.
				unsafe { *height = t.height };
				OAKNODE_OK
			}
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_track_set_height`.
	pub fn oaknode_track_set_height(track: CHandle, height: f64) -> c_int {
		match track_behavior_mut(track) {
			Some(t) => {
				t.height = height;
				OAKNODE_OK
			}
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_track_get_height_in_pixels`.
	pub fn oaknode_track_get_height_in_pixels(track: CHandle, height: *mut c_int) -> c_int {
		if height.is_null() {
			return OAKNODE_E_INVALID;
		}
		match track_behavior(track) {
			Some(t) => {
				// SAFETY: valid out pointer.
				unsafe {
					*height = oaknode::track::internal_height_to_pixel_height(t.height);
				}
				OAKNODE_OK
			}
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_track_set_height_in_pixels`.
	pub fn oaknode_track_set_height_in_pixels(track: CHandle, height: c_int) -> c_int {
		match track_behavior_mut(track) {
			Some(t) => {
				t.height = oaknode::track::pixel_height_to_internal_height(height);
				OAKNODE_OK
			}
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_track_get_default_height_in_pixels`.
	pub fn oaknode_track_get_default_height_in_pixels() -> c_int {
		oaknode::track::internal_height_to_pixel_height(
			oaknode::track::DEFAULT_HEIGHT_INTERNAL,
		)
	}

	/// `oaknode_track_get_minimum_height_in_pixels`.
	pub fn oaknode_track_get_minimum_height_in_pixels() -> c_int {
		oaknode::track::internal_height_to_pixel_height(
			oaknode::track::MINIMUM_HEIGHT_INTERNAL,
		)
	}

	/// `oaknode_track_get_index`.
	pub fn oaknode_track_get_index(track: CHandle, index: *mut c_int) -> c_int {
		if index.is_null() {
			return OAKNODE_E_INVALID;
		}
		match track_behavior(track) {
			Some(t) => {
				// SAFETY: valid out pointer.
				unsafe { *index = t.index };
				OAKNODE_OK
			}
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_track_set_index`.
	pub fn oaknode_track_set_index(track: CHandle, index: c_int) -> c_int {
		match track_behavior_mut(track) {
			Some(t) => {
				t.index = index;
				OAKNODE_OK
			}
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_track_get_muted`.
	pub fn oaknode_track_get_muted(track: CHandle, muted: *mut c_int) -> c_int {
		if muted.is_null() {
			return OAKNODE_E_INVALID;
		}
		match track_behavior(track) {
			Some(t) => {
				// SAFETY: valid out pointer.
				unsafe { *muted = if t.muted { 1 } else { 0 } };
				OAKNODE_OK
			}
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_track_set_muted`.
	pub fn oaknode_track_set_muted(track: CHandle, muted: c_int) -> c_int {
		match track_behavior_mut(track) {
			Some(t) => {
				t.muted = muted != 0;
				OAKNODE_OK
			}
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_track_get_locked`.
	pub fn oaknode_track_get_locked(track: CHandle, locked: *mut c_int) -> c_int {
		if locked.is_null() {
			return OAKNODE_E_INVALID;
		}
		match track_behavior(track) {
			Some(t) => {
				// SAFETY: valid out pointer.
				unsafe { *locked = if t.locked { 1 } else { 0 } };
				OAKNODE_OK
			}
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_track_set_locked`.
	pub fn oaknode_track_set_locked(track: CHandle, locked: c_int) -> c_int {
		match track_behavior_mut(track) {
			Some(t) => {
				t.locked = locked != 0;
				OAKNODE_OK
			}
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_track_get_reference` — (type, index).
	pub fn oaknode_track_get_reference(
		track: CHandle,
		type_: *mut c_int,
		index: *mut c_int,
	) -> c_int {
		if type_.is_null() || index.is_null() {
			return OAKNODE_E_INVALID;
		}
		match track_behavior(track) {
			Some(t) => {
				let (ty, idx) = t.reference();
				// SAFETY: valid out pointers.
				unsafe {
					*type_ = ty;
					*index = idx;
				}
				OAKNODE_OK
			}
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_track_get_length`.
	pub fn oaknode_track_get_length(
		track: CHandle,
		numerator: *mut c_int,
		denominator: *mut c_int,
	) -> c_int {
		if numerator.is_null() || denominator.is_null() {
			return OAKNODE_E_INVALID;
		}
		let nr = match unsafe { node_ref_of(&track) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		let (project, id) = nr;
		let length = {
			let g = lock(&project);
			let blocks = GraphBlockRange(&g.graph);
			behavior_of::<oaknode::track::TrackBehavior>(&g.graph, id)
				.map(|t| t.length(&blocks))
		};
		match length {
			Some(l) => {
				// SAFETY: valid out pointers.
				unsafe {
					*numerator = l.numerator() as c_int;
					*denominator = l.denominator() as c_int;
				}
				OAKNODE_OK
			}
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_track_get_sequence` — the sequence owning this track (null
	/// when detached).
	pub fn oaknode_track_get_sequence(track: CHandle, out: *mut CHandle) -> c_int {
		if out.is_null() {
			return OAKNODE_E_INVALID;
		}
		let nr = match unsafe { node_ref_of(&track) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		let (project, id) = nr;
		let sequence = {
			let g = lock(&project);
			let t = match behavior_of::<oaknode::track::TrackBehavior>(&g.graph, id) {
				Some(t) => t,
				None => return OAKNODE_E_INVALID,
			};
			t.track_list.and_then(|list_id| {
				behavior_of::<oaknode::track::TrackListBehavior>(&g.graph, list_id)
					.and_then(|l| l.sequence)
			})
		};
		// SAFETY: valid out pointer.
		unsafe {
			*out = match sequence {
				Some(s) => make_node_handle(project, s, false),
				None => CHandle::null(),
			};
		}
		OAKNODE_OK
	}

	/// `oaknode_track_get_block_count`.
	pub fn oaknode_track_get_block_count(track: CHandle, count: *mut c_int) -> c_int {
		if count.is_null() {
			return OAKNODE_E_INVALID;
		}
		match track_behavior(track) {
			Some(t) => {
				// SAFETY: valid out pointer.
				unsafe { *count = t.blocks.len() as c_int };
				OAKNODE_OK
			}
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_track_get_block_at`.
	pub fn oaknode_track_get_block_at(track: CHandle, index: c_int, out: *mut CHandle) -> c_int {
		if out.is_null() || index < 0 {
			return OAKNODE_E_INVALID;
		}
		let nr = match unsafe { node_ref_of(&track) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		let (project, id) = nr;
		let target = {
			let g = lock(&project);
			behavior_of::<oaknode::track::TrackBehavior>(&g.graph, id)
				.and_then(|t| t.blocks.get(index as usize).copied())
		};
		// SAFETY: valid out pointer.
		unsafe {
			*out = match target {
				Some(t) => make_node_handle(project, t, false),
				None => CHandle::null(),
			};
		}
		if target.is_some() {
			OAKNODE_OK
		} else {
			OAKNODE_E_NOT_FOUND
		}
	}

	/// Adopt `block` into the track's project and attach it to the track's
	/// block list (live edit; used by the append/prepend/insert family).
	fn track_attach_block(
		track: CHandle,
		block: CHandle,
		index: Option<usize>,
		after: Option<NodeId>,
		before: Option<NodeId>,
	) -> c_int {
		let t_nr = match unsafe { node_ref_of(&track) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		let (project, t_id) = t_nr;
		let b_nr = match unsafe { node_ref_of(&block) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		let (b_project, b_id) = b_nr;
		// Adopt the block into the track's project (no-op when it already
		// lives there).
		if !Arc::ptr_eq(&b_project, &project) {
			let entry = {
				let mut s = lock(&b_project);
				match s.graph.take_node(b_id) {
					Some(e) => e,
					None => return OAKNODE_E_NOT_FOUND,
				}
			};
			let new_id = {
				let mut t = lock(&project);
				t.graph.add_entry(entry, b_id)
			};
			// SAFETY: the shared node box is rewritten in place.
			if let Some(boxed) = unsafe { node_ref_mut(&block) } {
				boxed.project = project.clone();
				boxed.id = new_id;
				if boxed.owned.swap(false, Ordering::SeqCst) {
					alive_dec();
				}
			}
		}
		let b_id = unsafe { node_ref_of(&block) }
			.map(|n| n.id)
			.unwrap_or(b_id);
		let mut g = lock(&project);
		let track_b = match behavior_of_mut::<oaknode::track::TrackBehavior>(&mut g.graph, t_id) {
			Some(t) => t,
			None => return OAKNODE_E_NOT_FOUND,
		};
		if track_b.blocks.contains(&b_id) {
			// Idempotent: an already-attached block is a success no-op.
			return OAKNODE_OK;
		}
		let added = match (index, after, before) {
			(Some(i), _, _) => {
				track_b.insert_block_at_index(b_id, i);
				true
			}
			(None, Some(a), _) => track_b.insert_block_after(b_id, a),
			(None, None, Some(b)) => track_b.insert_block_before(b_id, b),
			(None, None, None) => {
				track_b.append_block(b_id);
				true
			}
		};
		if added {
			if let Some(core) = block_core_mut(&mut g.graph, b_id) {
				core.track = Some(t_id);
			}
			OAKNODE_OK
		} else {
			OAKNODE_E_NOT_FOUND
		}
	}

	/// `oaknode_track_append_block`.
	pub fn oaknode_track_append_block(track: CHandle, block: CHandle) -> c_int {
		track_attach_block(track, block, None, None, None)
	}

	/// `oaknode_track_prepend_block`.
	pub fn oaknode_track_prepend_block(track: CHandle, block: CHandle) -> c_int {
		track_attach_block(track, block, Some(0), None, None)
	}

	/// `oaknode_track_insert_block_at_index`.
	pub fn oaknode_track_insert_block_at_index(track: CHandle, block: CHandle, index: c_int) -> c_int {
		if index < 0 {
			return OAKNODE_E_INVALID;
		}
		track_attach_block(track, block, Some(index as usize), None, None)
	}

	/// `oaknode_track_insert_block_after`.
	pub fn oaknode_track_insert_block_after(
		track: CHandle,
		block: CHandle,
		before: CHandle,
	) -> c_int {
		let before_id = match unsafe { node_ref_of(&before) } {
			Some(nr) => nr.id,
			None => return OAKNODE_E_INVALID,
		};
		track_attach_block(track, block, None, Some(before_id), None)
	}

	/// `oaknode_track_insert_block_before`.
	pub fn oaknode_track_insert_block_before(
		track: CHandle,
		block: CHandle,
		after: CHandle,
	) -> c_int {
		let after_id = match unsafe { node_ref_of(&after) } {
			Some(nr) => nr.id,
			None => return OAKNODE_E_INVALID,
		};
		track_attach_block(track, block, None, None, Some(after_id))
	}

	/// `oaknode_track_ripple_remove_block`.
	pub fn oaknode_track_ripple_remove_block(track: CHandle, block: CHandle) -> c_int {
		let b_id = match unsafe { node_ref_of(&block) } {
			Some(nr) => nr.id,
			None => return OAKNODE_E_INVALID,
		};
		match track_behavior_mut(track) {
			Some(t) => {
				if t.ripple_remove_block(b_id) {
					OAKNODE_OK
				} else {
					OAKNODE_E_NOT_FOUND
				}
			}
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_track_replace_block`.
	pub fn oaknode_track_replace_block(track: CHandle, old_block: CHandle, new_block: CHandle) -> c_int {
		let old_id = match unsafe { node_ref_of(&old_block) } {
			Some(nr) => nr.id,
			None => return OAKNODE_E_INVALID,
		};
		let new_id = match unsafe { node_ref_of(&new_block) } {
			Some(nr) => nr.id,
			None => return OAKNODE_E_INVALID,
		};
		match track_behavior_mut(track) {
			Some(t) => {
				if t.replace_block(old_id, new_id) {
					OAKNODE_OK
				} else {
					OAKNODE_E_NOT_FOUND
				}
			}
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_track_get_block_index`.
	pub fn oaknode_track_get_block_index(
		track: CHandle,
		block: CHandle,
		index: *mut c_int,
	) -> c_int {
		if index.is_null() {
			return OAKNODE_E_INVALID;
		}
		let b_id = match unsafe { node_ref_of(&block) } {
			Some(nr) => nr.id,
			None => return OAKNODE_E_INVALID,
		};
		match track_behavior(track) {
			Some(t) => match t.block_index(b_id) {
				Some(i) => {
					// SAFETY: valid out pointer.
					unsafe { *index = i as c_int };
					OAKNODE_OK
				}
				None => OAKNODE_E_NOT_FOUND,
			},
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_track_get_block_containing_time`.
	pub fn oaknode_track_get_block_containing_time(
		track: CHandle,
		numerator: c_int,
		denominator: c_int,
		out: *mut CHandle,
	) -> c_int {
		if out.is_null() || denominator == 0 {
			return OAKNODE_E_INVALID;
		}
		let nr = match unsafe { node_ref_of(&track) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		let (project, id) = nr;
		let target = {
			let g = lock(&project);
			let blocks = GraphBlockRange(&g.graph);
			behavior_of::<oaknode::track::TrackBehavior>(&g.graph, id).and_then(|t| {
				t.block_containing_time(Rational::new(numerator as i64, denominator as i64), &blocks)
			})
		};
		// SAFETY: valid out pointer.
		unsafe {
			*out = match target {
				Some(t) => make_node_handle(project, t, false),
				None => CHandle::null(),
			};
		}
		OAKNODE_OK
	}

	/// `oaknode_track_get_visible_block_at_time`.
	pub fn oaknode_track_get_visible_block_at_time(
		track: CHandle,
		numerator: c_int,
		denominator: c_int,
		out: *mut CHandle,
	) -> c_int {
		if out.is_null() || denominator == 0 {
			return OAKNODE_E_INVALID;
		}
		let nr = match unsafe { node_ref_of(&track) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		let (project, id) = nr;
		let target = {
			let g = lock(&project);
			let blocks = GraphBlockRange(&g.graph);
			behavior_of::<oaknode::track::TrackBehavior>(&g.graph, id).and_then(|t| {
				t.visible_block_at_time(Rational::new(numerator as i64, denominator as i64), &blocks)
			})
		};
		// SAFETY: valid out pointer.
		unsafe {
			*out = match target {
				Some(t) => make_node_handle(project, t, false),
				None => CHandle::null(),
			};
		}
		OAKNODE_OK
	}

	/// `oaknode_track_is_range_free`.
	pub fn oaknode_track_is_range_free(
		track: CHandle,
		in_num: c_int,
		in_den: c_int,
		out_num: c_int,
		out_den: c_int,
		is_free: *mut c_int,
	) -> c_int {
		if is_free.is_null() || in_den == 0 || out_den == 0 {
			return OAKNODE_E_INVALID;
		}
		let nr = match unsafe { node_ref_of(&track) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		let (project, id) = nr;
		let free = {
			let g = lock(&project);
			let blocks = GraphBlockRange(&g.graph);
			behavior_of::<oaknode::track::TrackBehavior>(&g.graph, id)
				.map(|t| {
					t.is_range_free(
						TimeRange::new(
							Rational::new(in_num as i64, in_den as i64),
							Rational::new(out_num as i64, out_den as i64),
						),
						&blocks,
					)
				})
				.unwrap_or(false)
		};
		// SAFETY: valid out pointer.
		unsafe { *is_free = if free { 1 } else { 0 } };
		OAKNODE_OK
	}

	/// `oaknode_track_get_nearest_block_before_or_at`.
	pub fn oaknode_track_get_nearest_block_before_or_at(
		track: CHandle,
		numerator: c_int,
		denominator: c_int,
		out: *mut CHandle,
	) -> c_int {
		if out.is_null() || denominator == 0 {
			return OAKNODE_E_INVALID;
		}
		let nr = match unsafe { node_ref_of(&track) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		let (project, id) = nr;
		let time = Rational::new(numerator as i64, denominator as i64);
		let target = {
			let g = lock(&project);
			let t = match behavior_of::<oaknode::track::TrackBehavior>(&g.graph, id) {
				Some(t) => t,
				None => return OAKNODE_E_INVALID,
			};
			let mut best = None;
			for b in &t.blocks {
				let in_ = block_core(&g.graph, *b)
					.map(|c| c.in_())
					.unwrap_or_else(|| Rational::new(0, 1));
				if in_ <= time {
					best = Some(*b);
				}
			}
			best
		};
		// SAFETY: valid out pointer.
		unsafe {
			*out = match target {
				Some(t) => make_node_handle(project, t, false),
				None => CHandle::null(),
			};
		}
		OAKNODE_OK
	}

	/// `oaknode_track_get_nearest_block_after_or_at`.
	pub fn oaknode_track_get_nearest_block_after_or_at(
		track: CHandle,
		numerator: c_int,
		denominator: c_int,
		out: *mut CHandle,
	) -> c_int {
		if out.is_null() || denominator == 0 {
			return OAKNODE_E_INVALID;
		}
		let nr = match unsafe { node_ref_of(&track) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		let (project, id) = nr;
		let time = Rational::new(numerator as i64, denominator as i64);
		let target = {
			let g = lock(&project);
			let t = match behavior_of::<oaknode::track::TrackBehavior>(&g.graph, id) {
				Some(t) => t,
				None => return OAKNODE_E_INVALID,
			};
			t.blocks.iter().copied().find(|b| {
				block_core(&g.graph, *b)
					.map(|c| c.in_() >= time)
					.unwrap_or(false)
			})
		};
		// SAFETY: valid out pointer.
		unsafe {
			*out = match target {
				Some(t) => make_node_handle(project, t, false),
				None => CHandle::null(),
			};
		}
		OAKNODE_OK
	}

	// -------------------------------------------------------------------
	// Track list family
	// -------------------------------------------------------------------

	/// `oaknode_tracklist_get_sequence` — the owning sequence (null when
	/// detached).
	pub fn oaknode_tracklist_get_sequence(list: CHandle, out: *mut CHandle) -> c_int {
		if out.is_null() {
			return OAKNODE_E_INVALID;
		}
		let nr = match unsafe { node_ref_of(&list) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		let (project, id) = nr;
		let sequence = {
			let g = lock(&project);
			behavior_of::<oaknode::track::TrackListBehavior>(&g.graph, id).and_then(|l| l.sequence)
		};
		// SAFETY: valid out pointer.
		unsafe {
			*out = match sequence {
				Some(s) => make_node_handle(project, s, false),
				None => CHandle::null(),
			};
		}
		OAKNODE_OK
	}

	/// The list's index within its sequence's track-list vector (the
	/// `track_in_%1` element base).
	fn tracklist_input_base(list: CHandle) -> Option<(ProjectArc, NodeId, i32)> {
		let nr = unsafe { node_ref_of(&list) }?;
		let g = lock(&nr.project);
		let l = behavior_of::<oaknode::track::TrackListBehavior>(&g.graph, nr.id)?;
		let seq_id = l.sequence?;
		let seq = behavior_of::<oaknode::sequence::SequenceBehavior>(&g.graph, seq_id)?;
		let base = seq.track_lists.iter().position(|tl| *tl == nr.id)? as i32;
		Some((nr.project.clone(), seq_id, base))
	}

	/// `oaknode_tracklist_get_track_input_id` (two-stage) — the sequence's
	/// `track_in_%1` id for this list.
	pub fn oaknode_tracklist_get_track_input_id(
		list: CHandle,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		match tracklist_input_base(list) {
			Some((_, _, base)) => {
				let id = oaknode::sequence::TRACK_INPUT_FORMAT.replace("%1", &base.to_string());
				string_out(&id, buf, buf_size)
			}
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_tracklist_array_append` — append an element to the owning
	/// sequence's track-input array; returns the new element index.
	pub fn oaknode_tracklist_array_append(list: CHandle) -> c_int {
		let (project, seq_id, base) = match tracklist_input_base(list) {
			Some(t) => t,
			None => return OAKNODE_E_INVALID,
		};
		let input_id = oaknode::sequence::TRACK_INPUT_FORMAT.replace("%1", &base.to_string());
		let mut g = lock(&project);
		let size = g
			.graph
			.get(seq_id)
			.and_then(|e| e.core.get_input(&input_id))
			.map(|i| i.array_size)
			.unwrap_or(0);
		if let Some(e) = g.graph.get_mut(seq_id) {
			e.core.input_array_insert(&input_id, size);
		}
		size as c_int
	}

	/// `oaknode_tracklist_array_remove_last`.
	pub fn oaknode_tracklist_array_remove_last(list: CHandle) -> c_int {
		let (project, seq_id, base) = match tracklist_input_base(list) {
			Some(t) => t,
			None => return OAKNODE_E_INVALID,
		};
		let input_id = oaknode::sequence::TRACK_INPUT_FORMAT.replace("%1", &base.to_string());
		let mut g = lock(&project);
		let size = g
			.graph
			.get(seq_id)
			.and_then(|e| e.core.get_input(&input_id))
			.map(|i| i.array_size)
			.unwrap_or(0);
		if size == 0 {
			return OAKNODE_OK;
		}
		if let Some(e) = g.graph.get_mut(seq_id) {
			e.core.input_array_remove(&input_id, size - 1);
		}
		OAKNODE_OK
	}

	/// `oaknode_tracklist_get_array_index_from_cache_index` — identity (the
	/// cache and array indexes coincide in the Rust model).
	pub fn oaknode_tracklist_get_array_index_from_cache_index(
		list: CHandle,
		cache_index: c_int,
		out_index: *mut c_int,
	) -> c_int {
		if out_index.is_null() || cache_index < 0 {
			return OAKNODE_E_INVALID;
		}
		let _ = list;
		// SAFETY: valid out pointer.
		unsafe { *out_index = cache_index };
		OAKNODE_OK
	}

	/// `oaknode_tracklist_get_type`.
	pub fn oaknode_tracklist_get_type(list: CHandle, type_: *mut c_int) -> c_int {
		if type_.is_null() {
			return OAKNODE_E_INVALID;
		}
		match tracklist_behavior(list) {
			Some(l) => {
				// SAFETY: valid out pointer.
				unsafe { *type_ = l.kind.to_c() };
				OAKNODE_OK
			}
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_tracklist_get_track_count`.
	pub fn oaknode_tracklist_get_track_count(list: CHandle, count: *mut c_int) -> c_int {
		if count.is_null() {
			return OAKNODE_E_INVALID;
		}
		match tracklist_behavior(list) {
			Some(l) => {
				// SAFETY: valid out pointer.
				unsafe { *count = l.tracks.len() as c_int };
				OAKNODE_OK
			}
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_tracklist_get_track_at`.
	pub fn oaknode_tracklist_get_track_at(list: CHandle, index: c_int, out: *mut CHandle) -> c_int {
		if out.is_null() || index < 0 {
			return OAKNODE_E_INVALID;
		}
		let nr = match unsafe { node_ref_of(&list) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		let (project, id) = nr;
		let target = {
			let g = lock(&project);
			behavior_of::<oaknode::track::TrackListBehavior>(&g.graph, id)
				.and_then(|l| l.tracks.get(index as usize).copied())
		};
		// SAFETY: valid out pointer.
		unsafe {
			*out = match target {
				Some(t) => make_node_handle(project, t, false),
				None => CHandle::null(),
			};
		}
		if target.is_some() {
			OAKNODE_OK
		} else {
			OAKNODE_E_NOT_FOUND
		}
	}

	/// `oaknode_tracklist_get_total_length`.
	pub fn oaknode_tracklist_get_total_length(
		list: CHandle,
		numerator: *mut c_int,
		denominator: *mut c_int,
	) -> c_int {
		if numerator.is_null() || denominator.is_null() {
			return OAKNODE_E_INVALID;
		}
		let nr = match unsafe { node_ref_of(&list) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		let (project, id) = nr;
		let length = {
			let g = lock(&project);
			let tracks = GraphTrackRange(&g.graph);
			behavior_of::<oaknode::track::TrackListBehavior>(&g.graph, id)
				.map(|l| l.total_length(&tracks))
		};
		match length {
			Some(l) => {
				// SAFETY: valid out pointers.
				unsafe {
					*numerator = l.numerator() as c_int;
					*denominator = l.denominator() as c_int;
				}
				OAKNODE_OK
			}
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_tracklist_get_array_size` — the owning sequence's
	/// track-input array size.
	pub fn oaknode_tracklist_get_array_size(list: CHandle, size: *mut c_int) -> c_int {
		if size.is_null() {
			return OAKNODE_E_INVALID;
		}
		let (project, seq_id, base) = match tracklist_input_base(list) {
			Some(t) => t,
			None => return OAKNODE_E_INVALID,
		};
		let input_id = oaknode::sequence::TRACK_INPUT_FORMAT.replace("%1", &base.to_string());
		let g = lock(&project);
		let n = g
			.graph
			.get(seq_id)
			.and_then(|e| e.core.get_input(&input_id))
			.map(|i| i.array_size)
			.unwrap_or(0);
		// SAFETY: valid out pointer.
		unsafe { *size = n as c_int };
		OAKNODE_OK
	}

	/// `oaknode_tracklist_add_track` — live add (+ back-reference and
	/// index).
	pub fn oaknode_tracklist_add_track(list: CHandle, track: CHandle) -> c_int {
		let l_nr = match unsafe { node_ref_of(&list) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		let t_id = match unsafe { node_ref_of(&track) } {
			Some(nr) => nr.id,
			None => return OAKNODE_E_INVALID,
		};
		let (project, l_id) = l_nr;
		let mut g = lock(&project);
		let list_b = match behavior_of_mut::<oaknode::track::TrackListBehavior>(&mut g.graph, l_id) {
			Some(l) => l,
			None => return OAKNODE_E_NOT_FOUND,
		};
		let index = list_b.tracks.len() as i32;
		if !list_b.tracks.contains(&t_id) {
			list_b.tracks.push(t_id);
		}
		if let Some(t) = behavior_of_mut::<oaknode::track::TrackBehavior>(&mut g.graph, t_id) {
			t.track_list = Some(l_id);
			t.index = index;
		}
		OAKNODE_OK
	}

	/// `oaknode_tracklist_remove_track` — live remove.
	pub fn oaknode_tracklist_remove_track(list: CHandle, track: CHandle) -> c_int {
		let l_nr = match unsafe { node_ref_of(&list) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		let t_id = match unsafe { node_ref_of(&track) } {
			Some(nr) => nr.id,
			None => return OAKNODE_E_INVALID,
		};
		let mut g = lock(&l_nr.0);
		let list_b = match behavior_of_mut::<oaknode::track::TrackListBehavior>(&mut g.graph, l_nr.1) {
			Some(l) => l,
			None => return OAKNODE_E_NOT_FOUND,
		};
		list_b.tracks.retain(|t| *t != t_id);
		if let Some(t) = behavior_of_mut::<oaknode::track::TrackBehavior>(&mut g.graph, t_id) {
			t.track_list = None;
		}
		OAKNODE_OK
	}
	// -------------------------------------------------------------------
	// Block family
	// -------------------------------------------------------------------

	const BLOCK_KIND_OTHER: c_int = 0;
	const BLOCK_KIND_CLIP: c_int = 1;
	const BLOCK_KIND_GAP: c_int = 2;
	const BLOCK_KIND_TRANSITION: c_int = 3;

	fn block_kind_of(h: CHandle) -> Option<c_int> {
		with_node(h, |g, id| {
			let e = g.get(id)?;
			if e.behavior
				.as_any()
				.and_then(|a| a.downcast_ref::<oaknode::block::ClipBlockBehavior>())
				.is_some()
			{
				return Some(BLOCK_KIND_CLIP);
			}
			if e.behavior
				.as_any()
				.and_then(|a| a.downcast_ref::<oaknode::block::GapBlockBehavior>())
				.is_some()
			{
				return Some(BLOCK_KIND_GAP);
			}
			if e
				.behavior
				.as_any()
				.and_then(|a| a.downcast_ref::<oaknode::block::TransitionBlockBehavior>())
				.is_some()
			{
				return Some(BLOCK_KIND_TRANSITION);
			}
			None
		})?
	}

	fn is_block(h: CHandle) -> bool {
		block_kind_of(h).is_some()
	}

	fn block_ref(h: CHandle) -> Option<(ProjectArc, NodeId)> {
		let nr = unsafe { node_ref_of(&h) }?;
		Some((nr.project.clone(), nr.id))
	}

	/// `oaknode_block_clip_create` — detached clip block.
	pub fn oaknode_block_clip_create() -> CHandle {
		make_detached(oaknode::block::clip_create())
	}

	/// `oaknode_block_gap_create` — detached gap block.
	pub fn oaknode_block_gap_create() -> CHandle {
		make_detached(oaknode::block::gap_create())
	}

	/// `oaknode_block_transition_create` — detached transition block
	/// (the Rust model has a single transition type; `kind` is accepted
	/// for ABI parity).
	pub fn oaknode_block_transition_create(_kind: c_int) -> CHandle {
		make_detached(oaknode::block::transition_create())
	}

	/// `oaknode_block_free` — same shell release as the node family.
	pub fn oaknode_block_free(block: *mut CHandle) {
		if block.is_null() {
			return;
		}
		// SAFETY: the caller passes a valid handle pointer.
		let h = unsafe { *block };
		free_node_handle(h);
		// SAFETY: the caller passes a valid handle pointer.
		unsafe { *block = CHandle::null() };
	}

	/// `oaknode_block_get_kind`.
	pub fn oaknode_block_get_kind(block: CHandle, out_kind: *mut c_int) -> c_int {
		if out_kind.is_null() {
			return OAKNODE_E_INVALID;
		}
		match block_kind_of(block) {
			Some(k) => {
				// SAFETY: valid out pointer.
				unsafe { *out_kind = k };
				OAKNODE_OK
			}
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_block_as_node` — identity cast (addref'd copy).
	pub fn oaknode_block_as_node(block: CHandle) -> CHandle {
		addref_copy(block)
	}

	/// `oaknode_block_from_node` — the same node when it is a block, else
	/// null.
	pub fn oaknode_block_from_node(node: CHandle) -> CHandle {
		if is_block(node) {
			addref_copy(node)
		} else {
			CHandle::null()
		}
	}

	/// `oaknode_block_get_in`.
	pub fn oaknode_block_get_in(
		block: CHandle,
		numerator: *mut c_int,
		denominator: *mut c_int,
	) -> c_int {
		if numerator.is_null() || denominator.is_null() {
			return OAKNODE_E_INVALID;
		}
		match with_node(block, |g, id| block_core(g, id).map(|c| c.in_())) {
			Some(Some(v)) => {
				// SAFETY: valid out pointers.
				unsafe {
					*numerator = v.numerator() as c_int;
					*denominator = v.denominator() as c_int;
				}
				OAKNODE_OK
			}
			Some(None) => OAKNODE_E_NOT_FOUND,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_block_set_in`.
	pub fn oaknode_block_set_in(block: CHandle, numerator: c_int, denominator: c_int) -> c_int {
		if denominator == 0 {
			return OAKNODE_E_INVALID;
		}
		match with_node_mut(block, |g, id| {
			block_core_mut(g, id).map(|c| c.set_in(Rational::new(numerator as i64, denominator as i64)))
		}) {
			Some(Some(())) => OAKNODE_OK,
			Some(None) => OAKNODE_E_NOT_FOUND,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_block_get_out`.
	pub fn oaknode_block_get_out(
		block: CHandle,
		numerator: *mut c_int,
		denominator: *mut c_int,
	) -> c_int {
		if numerator.is_null() || denominator.is_null() {
			return OAKNODE_E_INVALID;
		}
		match with_node(block, |g, id| block_core(g, id).map(|c| c.out())) {
			Some(Some(v)) => {
				// SAFETY: valid out pointers.
				unsafe {
					*numerator = v.numerator() as c_int;
					*denominator = v.denominator() as c_int;
				}
				OAKNODE_OK
			}
			Some(None) => OAKNODE_E_NOT_FOUND,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_block_set_out`.
	pub fn oaknode_block_set_out(block: CHandle, numerator: c_int, denominator: c_int) -> c_int {
		if denominator == 0 {
			return OAKNODE_E_INVALID;
		}
		match with_node_mut(block, |g, id| {
			block_core_mut(g, id).map(|c| c.set_out(Rational::new(numerator as i64, denominator as i64)))
		}) {
			Some(Some(())) => OAKNODE_OK,
			Some(None) => OAKNODE_E_NOT_FOUND,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_block_get_length`.
	pub fn oaknode_block_get_length(
		block: CHandle,
		numerator: *mut c_int,
		denominator: *mut c_int,
	) -> c_int {
		if numerator.is_null() || denominator.is_null() {
			return OAKNODE_E_INVALID;
		}
		match with_node(block, |g, id| block_core(g, id).map(|c| c.length())) {
			Some(Some(v)) => {
				// SAFETY: valid out pointers.
				unsafe {
					*numerator = v.numerator() as c_int;
					*denominator = v.denominator() as c_int;
				}
				OAKNODE_OK
			}
			Some(None) => OAKNODE_E_NOT_FOUND,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_block_set_length_and_media_out`.
	pub fn oaknode_block_set_length_and_media_out(
		block: CHandle,
		numerator: c_int,
		denominator: c_int,
	) -> c_int {
		if denominator == 0 {
			return OAKNODE_E_INVALID;
		}
		match with_node_mut(block, |g, id| {
			block_core_mut(g, id).map(|c| {
				c.set_length_and_media_out(Rational::new(numerator as i64, denominator as i64))
			})
		}) {
			Some(Some(())) => OAKNODE_OK,
			Some(None) => OAKNODE_E_NOT_FOUND,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_block_set_length_and_media_in`.
	pub fn oaknode_block_set_length_and_media_in(
		block: CHandle,
		numerator: c_int,
		denominator: c_int,
	) -> c_int {
		if denominator == 0 {
			return OAKNODE_E_INVALID;
		}
		match with_node_mut(block, |g, id| {
			block_core_mut(g, id).map(|c| {
				c.set_length_and_media_in(Rational::new(numerator as i64, denominator as i64))
			})
		}) {
			Some(Some(())) => OAKNODE_OK,
			Some(None) => OAKNODE_E_NOT_FOUND,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_block_get_enabled`.
	pub fn oaknode_block_get_enabled(block: CHandle, enabled: *mut c_int) -> c_int {
		if enabled.is_null() {
			return OAKNODE_E_INVALID;
		}
		match with_node(block, |g, id| block_core(g, id).map(|c| c.enabled)) {
			Some(Some(v)) => {
				// SAFETY: valid out pointer.
				unsafe { *enabled = if v { 1 } else { 0 } };
				OAKNODE_OK
			}
			Some(None) => OAKNODE_E_NOT_FOUND,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_block_set_enabled`.
	pub fn oaknode_block_set_enabled(block: CHandle, enabled: c_int) -> c_int {
		match with_node_mut(block, |g, id| block_core_mut(g, id).map(|c| c.enabled = enabled != 0)) {
			Some(Some(())) => OAKNODE_OK,
			Some(None) => OAKNODE_E_NOT_FOUND,
			None => OAKNODE_E_INVALID,
		}
	}

	/// Neighboring block lookup (previous/next on the owning track).
	fn block_neighbor(block: CHandle, next: bool) -> Option<(ProjectArc, NodeId)> {
		let (project, id) = block_ref(block)?;
		let g = lock(&project);
		let core = block_core(&g.graph, id)?;
		let track_id = core.track?;
		let track = behavior_of::<oaknode::track::TrackBehavior>(&g.graph, track_id)?;
		let pos = track.blocks.iter().position(|b| *b == id)?;
		let idx = if next { pos + 1 } else { pos.checked_sub(1)? };
		Some((project.clone(), *track.blocks.get(idx)?))
	}

	/// `oaknode_block_get_previous`.
	pub fn oaknode_block_get_previous(block: CHandle, out: *mut CHandle) -> c_int {
		if out.is_null() {
			return OAKNODE_E_INVALID;
		}
		match block_neighbor(block, false) {
			Some((project, id)) => {
				// SAFETY: valid out pointer.
				unsafe { *out = make_node_handle(project, id, false) };
				OAKNODE_OK
			}
			None => {
				// SAFETY: valid out pointer.
				unsafe { *out = CHandle::null() };
				OAKNODE_E_NOT_FOUND
			}
		}
	}

	/// `oaknode_block_get_next`.
	pub fn oaknode_block_get_next(block: CHandle, out: *mut CHandle) -> c_int {
		if out.is_null() {
			return OAKNODE_E_INVALID;
		}
		match block_neighbor(block, true) {
			Some((project, id)) => {
				// SAFETY: valid out pointer.
				unsafe { *out = make_node_handle(project, id, false) };
				OAKNODE_OK
			}
			None => {
				// SAFETY: valid out pointer.
				unsafe { *out = CHandle::null() };
				OAKNODE_E_NOT_FOUND
			}
		}
	}

	/// `oaknode_block_get_track` — the owning track (null when detached).
	pub fn oaknode_block_get_track(block: CHandle, out: *mut CHandle) -> c_int {
		if out.is_null() {
			return OAKNODE_E_INVALID;
		}
		let nr = match unsafe { node_ref_of(&block) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		let (project, id) = nr;
		let track = {
			let g = lock(&project);
			block_core(&g.graph, id).and_then(|c| c.track)
		};
		// SAFETY: valid out pointer.
		unsafe {
			*out = match track {
				Some(t) => make_node_handle(project, t, false),
				None => CHandle::null(),
			};
		}
		OAKNODE_OK
	}

	/// `oaknode_block_link` — link blocks (BlockCore links).
	pub fn oaknode_block_link(a: CHandle, b: CHandle) -> c_int {
		let a_nr = match unsafe { node_ref_of(&a) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		let b_id = match unsafe { node_ref_of(&b) } {
			Some(nr) => nr.id,
			None => return OAKNODE_E_INVALID,
		};
		let mut g = lock(&a_nr.0);
		match block_core_mut(&mut g.graph, a_nr.1) {
			Some(c) => {
				if !c.links.contains(&b_id) {
					c.links.push(b_id);
				}
				OAKNODE_OK
			}
			None => OAKNODE_E_NOT_FOUND,
		}
	}

	/// `oaknode_block_unlink` — unlink blocks.
	pub fn oaknode_block_unlink(a: CHandle, b: CHandle) -> c_int {
		let a_nr = match unsafe { node_ref_of(&a) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		let b_id = match unsafe { node_ref_of(&b) } {
			Some(nr) => nr.id,
			None => return OAKNODE_E_INVALID,
		};
		let mut g = lock(&a_nr.0);
		match block_core_mut(&mut g.graph, a_nr.1) {
			Some(c) => {
				c.links.retain(|l| *l != b_id);
				OAKNODE_OK
			}
			None => OAKNODE_E_NOT_FOUND,
		}
	}

	/// `oaknode_block_are_linked`.
	pub fn oaknode_block_are_linked(a: CHandle, b: CHandle, linked: *mut c_int) -> c_int {
		if linked.is_null() {
			return OAKNODE_E_INVALID;
		}
		let a_nr = match unsafe { node_ref_of(&a) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		let b_id = match unsafe { node_ref_of(&b) } {
			Some(nr) => nr.id,
			None => return OAKNODE_E_INVALID,
		};
		let g = lock(&a_nr.0);
		let value = block_core(&g.graph, a_nr.1)
			.map(|c| c.links.contains(&b_id))
			.unwrap_or(false);
		// SAFETY: valid out pointer.
		unsafe { *linked = if value { 1 } else { 0 } };
		OAKNODE_OK
	}

	/// `oaknode_block_get_link_count`.
	pub fn oaknode_block_get_link_count(block: CHandle, count: *mut c_int) -> c_int {
		if count.is_null() {
			return OAKNODE_E_INVALID;
		}
		match with_node(block, |g, id| block_core(g, id).map(|c| c.links.len())) {
			Some(Some(n)) => {
				// SAFETY: valid out pointer.
				unsafe { *count = n as c_int };
				OAKNODE_OK
			}
			Some(None) => OAKNODE_E_NOT_FOUND,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_block_get_link_at`.
	pub fn oaknode_block_get_link_at(block: CHandle, index: c_int, out: *mut CHandle) -> c_int {
		if out.is_null() || index < 0 {
			return OAKNODE_E_INVALID;
		}
		let nr = match unsafe { node_ref_of(&block) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		let (project, id) = nr;
		let target = {
			let g = lock(&project);
			block_core(&g.graph, id)
				.and_then(|c| c.links.get(index as usize).copied())
		};
		// SAFETY: valid out pointer.
		unsafe {
			*out = match target {
				Some(t) => make_node_handle(project, t, false),
				None => CHandle::null(),
			};
		}
		if target.is_some() {
			OAKNODE_OK
		} else {
			OAKNODE_E_NOT_FOUND
		}
	}

	// -------------------------------------------------------------------
	// Clip family
	// -------------------------------------------------------------------

	/// `oaknode_clip_get_media_in`.
	pub fn oaknode_clip_get_media_in(
		clip: CHandle,
		numerator: *mut c_int,
		denominator: *mut c_int,
	) -> c_int {
		if numerator.is_null() || denominator.is_null() {
			return OAKNODE_E_INVALID;
		}
		match with_node(clip, |g, id| block_core(g, id).map(|c| c.media_in)) {
			Some(Some(v)) => {
				// SAFETY: valid out pointers.
				unsafe {
					*numerator = v.numerator() as c_int;
					*denominator = v.denominator() as c_int;
				}
				OAKNODE_OK
			}
			Some(None) => OAKNODE_E_NOT_FOUND,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_clip_set_media_in`.
	pub fn oaknode_clip_set_media_in(clip: CHandle, numerator: c_int, denominator: c_int) -> c_int {
		if denominator == 0 {
			return OAKNODE_E_INVALID;
		}
		match with_node_mut(clip, |g, id| {
			block_core_mut(g, id)
				.map(|c| c.media_in = Rational::new(numerator as i64, denominator as i64))
		}) {
			Some(Some(())) => OAKNODE_OK,
			Some(None) => OAKNODE_E_NOT_FOUND,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_clip_get_speed`.
	pub fn oaknode_clip_get_speed(clip: CHandle, speed: *mut f64) -> c_int {
		if speed.is_null() {
			return OAKNODE_E_INVALID;
		}
		match with_node(clip, |g, id| block_core(g, id).map(|c| c.speed)) {
			Some(Some(v)) => {
				// SAFETY: valid out pointer.
				unsafe { *speed = v };
				OAKNODE_OK
			}
			Some(None) => OAKNODE_E_NOT_FOUND,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_clip_set_speed`.
	pub fn oaknode_clip_set_speed(clip: CHandle, speed: f64) -> c_int {
		match with_node_mut(clip, |g, id| block_core_mut(g, id).map(|c| c.speed = speed)) {
			Some(Some(())) => OAKNODE_OK,
			Some(None) => OAKNODE_E_NOT_FOUND,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_clip_get_reverse`.
	pub fn oaknode_clip_get_reverse(clip: CHandle, reverse: *mut c_int) -> c_int {
		if reverse.is_null() {
			return OAKNODE_E_INVALID;
		}
		match with_node(clip, |g, id| block_core(g, id).map(|c| c.reversed)) {
			Some(Some(v)) => {
				// SAFETY: valid out pointer.
				unsafe { *reverse = if v { 1 } else { 0 } };
				OAKNODE_OK
			}
			Some(None) => OAKNODE_E_NOT_FOUND,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_clip_set_reverse`.
	pub fn oaknode_clip_set_reverse(clip: CHandle, reverse: c_int) -> c_int {
		match with_node_mut(clip, |g, id| block_core_mut(g, id).map(|c| c.reversed = reverse != 0)) {
			Some(Some(())) => OAKNODE_OK,
			Some(None) => OAKNODE_E_NOT_FOUND,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_clip_get_maintain_audio_pitch`.
	pub fn oaknode_clip_get_maintain_audio_pitch(clip: CHandle, maintain: *mut c_int) -> c_int {
		if maintain.is_null() {
			return OAKNODE_E_INVALID;
		}
		match with_node(clip, |g, id| block_core(g, id).map(|c| c.maintain_audio_pitch)) {
			Some(Some(v)) => {
				// SAFETY: valid out pointer.
				unsafe { *maintain = if v { 1 } else { 0 } };
				OAKNODE_OK
			}
			Some(None) => OAKNODE_E_NOT_FOUND,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_clip_set_maintain_audio_pitch`.
	pub fn oaknode_clip_set_maintain_audio_pitch(clip: CHandle, maintain: c_int) -> c_int {
		match with_node_mut(clip, |g, id| {
			block_core_mut(g, id).map(|c| c.maintain_audio_pitch = maintain != 0)
		}) {
			Some(Some(())) => OAKNODE_OK,
			Some(None) => OAKNODE_E_NOT_FOUND,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_clip_get_loop_mode`.
	pub fn oaknode_clip_get_loop_mode(clip: CHandle, loop_mode: *mut c_int) -> c_int {
		if loop_mode.is_null() {
			return OAKNODE_E_INVALID;
		}
		match with_node(clip, |g, id| block_core(g, id).map(|c| c.loop_mode)) {
			Some(Some(v)) => {
				// SAFETY: valid out pointer.
				unsafe { *loop_mode = v };
				OAKNODE_OK
			}
			Some(None) => OAKNODE_E_NOT_FOUND,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_clip_set_loop_mode`.
	pub fn oaknode_clip_set_loop_mode(clip: CHandle, loop_mode: c_int) -> c_int {
		match with_node_mut(clip, |g, id| block_core_mut(g, id).map(|c| c.loop_mode = loop_mode)) {
			Some(Some(())) => OAKNODE_OK,
			Some(None) => OAKNODE_E_NOT_FOUND,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_clip_get_track_type` — the owning track's media type.
	pub fn oaknode_clip_get_track_type(clip: CHandle, type_: *mut c_int) -> c_int {
		if type_.is_null() {
			return OAKNODE_E_INVALID;
		}
		let nr = match unsafe { node_ref_of(&clip) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		let (project, id) = nr;
		let kind = {
			let g = lock(&project);
			let track_id = block_core(&g.graph, id).and_then(|c| c.track);
			track_id.and_then(|t| {
				behavior_of::<oaknode::track::TrackBehavior>(&g.graph, t).map(|t| t.kind.to_c())
			})
		};
		match kind {
			Some(k) => {
				// SAFETY: valid out pointer.
				unsafe { *type_ = k };
				OAKNODE_OK
			}
			None => OAKNODE_E_NOT_FOUND,
		}
	}

	// -------------------------------------------------------------------
	// Transition family
	// -------------------------------------------------------------------

	fn transition_behavior(h: CHandle) -> Option<&'static oaknode::block::TransitionBlockBehavior> {
		let nr = unsafe { node_ref_of(&h) }?;
		let p = lock(&nr.project);
		let b = behavior_of::<oaknode::block::TransitionBlockBehavior>(&p.graph, nr.id)?;
		// SAFETY: the node lives in the arena; the project outlives every
		// handle that references it.
		unsafe { Some(&*(b as *const _)) }
	}

	fn transition_behavior_mut(
		h: CHandle,
	) -> Option<&'static mut oaknode::block::TransitionBlockBehavior> {
		let nr = unsafe { node_ref_of(&h) }?;
		let mut p = lock(&nr.project);
		let b = behavior_of_mut::<oaknode::block::TransitionBlockBehavior>(&mut p.graph, nr.id)?;
		// SAFETY: see `transition_behavior`.
		unsafe { Some(&mut *(b as *mut _)) }
	}

	/// `oaknode_transition_get_in_offset`.
	pub fn oaknode_transition_get_in_offset(
		transition: CHandle,
		numerator: *mut c_int,
		denominator: *mut c_int,
	) -> c_int {
		if numerator.is_null() || denominator.is_null() {
			return OAKNODE_E_INVALID;
		}
		match transition_behavior(transition) {
			Some(t) => {
				// SAFETY: valid out pointers.
				unsafe {
					*numerator = t.in_offset.numerator() as c_int;
					*denominator = t.in_offset.denominator() as c_int;
				}
				OAKNODE_OK
			}
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_transition_get_out_offset`.
	pub fn oaknode_transition_get_out_offset(
		transition: CHandle,
		numerator: *mut c_int,
		denominator: *mut c_int,
	) -> c_int {
		if numerator.is_null() || denominator.is_null() {
			return OAKNODE_E_INVALID;
		}
		match transition_behavior(transition) {
			Some(t) => {
				// SAFETY: valid out pointers.
				unsafe {
					*numerator = t.out_offset.numerator() as c_int;
					*denominator = t.out_offset.denominator() as c_int;
				}
				OAKNODE_OK
			}
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_transition_get_offset_center` — the midpoint of both
	/// offsets.
	pub fn oaknode_transition_get_offset_center(
		transition: CHandle,
		numerator: *mut c_int,
		denominator: *mut c_int,
	) -> c_int {
		if numerator.is_null() || denominator.is_null() {
			return OAKNODE_E_INVALID;
		}
		match transition_behavior(transition) {
			Some(t) => {
				// SAFETY: valid out pointers.
				unsafe {
					*numerator = t.in_offset.numerator() as c_int;
					*denominator = t.in_offset.denominator() as c_int;
				}
				OAKNODE_OK
			}
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_transition_set_offset_center` — set both offsets.
	pub fn oaknode_transition_set_offset_center(
		transition: CHandle,
		numerator: c_int,
		denominator: c_int,
	) -> c_int {
		if denominator == 0 {
			return OAKNODE_E_INVALID;
		}
		let value = Rational::new(numerator as i64, denominator as i64);
		match transition_behavior_mut(transition) {
			Some(t) => {
				t.in_offset = value;
				t.out_offset = value;
				OAKNODE_OK
			}
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_transition_set_offsets_and_length`.
	pub fn oaknode_transition_set_offsets_and_length(
		transition: CHandle,
		in_num: c_int,
		in_den: c_int,
		out_num: c_int,
		out_den: c_int,
	) -> c_int {
		if in_den == 0 || out_den == 0 {
			return OAKNODE_E_INVALID;
		}
		let in_offset = Rational::new(in_num as i64, in_den as i64);
		let out_offset = Rational::new(out_num as i64, out_den as i64);
		match transition_behavior_mut(transition) {
			Some(t) => {
				t.in_offset = in_offset;
				t.out_offset = out_offset;
				t.core
					.set_length_and_media_in(in_offset + out_offset);
				OAKNODE_OK
			}
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_transition_is_dual` — both connection inputs present.
	pub fn oaknode_transition_is_dual(transition: CHandle, dual: *mut c_int) -> c_int {
		if dual.is_null() {
			return OAKNODE_E_INVALID;
		}
		let nr = match unsafe { node_ref_of(&transition) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		let (project, id) = nr;
		let is_dual = {
			let g = lock(&project);
			g.graph.is_input_connected(id, oaknode::block::transition_input::IN_BLOCK, -1)
				&& g.graph.is_input_connected(id, oaknode::block::transition_input::OUT_BLOCK, -1)
		};
		// SAFETY: valid out pointer.
		unsafe { *dual = if is_dual { 1 } else { 0 } };
		OAKNODE_OK
	}

	/// `oaknode_transition_get_connected_out_block` — the block feeding the
	/// `out_block_in` input.
	pub fn oaknode_transition_get_connected_out_block(
		transition: CHandle,
		out: *mut CHandle,
	) -> c_int {
		if out.is_null() {
			return OAKNODE_E_INVALID;
		}
		let nr = match unsafe { node_ref_of(&transition) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		let (project, id) = nr;
		let from = {
			let g = lock(&project);
			g.graph.connected_output(id, oaknode::block::transition_input::OUT_BLOCK, -1)
		};
		// SAFETY: valid out pointer.
		unsafe {
			*out = match from {
				Some(f) => make_node_handle(project, f, false),
				None => CHandle::null(),
			};
		}
		OAKNODE_OK
	}

	/// `oaknode_transition_get_connected_in_block` — the block fed by the
	/// `in_block_in` input.
	pub fn oaknode_transition_get_connected_in_block(
		transition: CHandle,
		out: *mut CHandle,
	) -> c_int {
		if out.is_null() {
			return OAKNODE_E_INVALID;
		}
		let nr = match unsafe { node_ref_of(&transition) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		let (project, id) = nr;
		let target = {
			let g = lock(&project);
			g.graph
				.output_connections(id)
				.into_iter()
				.find(|(_, input, _)| input == oaknode::block::transition_input::IN_BLOCK)
				.map(|(t, _, _)| t)
		};
		// SAFETY: valid out pointer.
		unsafe {
			*out = match target {
				Some(t) => make_node_handle(project, t, false),
				None => CHandle::null(),
			};
		}
		OAKNODE_OK
	}

	/// `oaknode_clip_add_cache_passthrough_from` — STUB: the C++ copies
	/// the other clip's cache-passthrough connections; the Rust graph
	/// model has no cache-passthrough input concept, so this is unwireable
	/// (documented; returns the module failure code).
	pub fn oaknode_clip_add_cache_passthrough_from(_clip: CHandle, _other: CHandle) -> c_int {
		OAKNODE_E_FAILED
	}

	/// `oaknode_block_get_kind` alias for the clip/track-type query
	/// (`BLOCK_KIND_OTHER` for non-blocks).
	#[allow(dead_code)]
	fn _unused_kind_anchor() -> c_int {
		BLOCK_KIND_OTHER
	}
	// -------------------------------------------------------------------
	// Keyframe family
	// -------------------------------------------------------------------

	/// Engine-side keyframe payload: a reference to one keyframe on a
	/// node's (input, element) track, identified by its time.
	struct KeyframePayload {
		project: ProjectArc,
		node: NodeId,
		input: String,
		element: i32,
		time: Rational,
	}

	/// Facade easing types (the engine's `oakengine_keyframe_*` mapping):
	/// 0 = linear, 1 = bezier, 2 = hold.
	fn interp_from_type(t: c_int) -> oaknode::keyframe::Interpolation {
		match t {
			2 => oaknode::keyframe::Interpolation::Hold,
			1 => oaknode::keyframe::Interpolation::Bezier,
			_ => oaknode::keyframe::Interpolation::Linear,
		}
	}

	fn type_from_interp(i: oaknode::keyframe::Interpolation) -> c_int {
		match i {
			oaknode::keyframe::Interpolation::Hold => 2,
			oaknode::keyframe::Interpolation::Bezier => 1,
			oaknode::keyframe::Interpolation::Linear => 0,
		}
	}

	/// The declared type of the keyframe's input.
	fn keyframe_declared(kf: &KeyframePayload) -> Option<oaknode::value::ValueType> {
		let g = lock(&kf.project);
		g.graph
			.get(kf.node)
			.and_then(|e| e.core.input_data_type(&kf.input))
	}

	/// The keyframe value on the node's track (interpolated fallback).
	fn keyframe_value(kf: &KeyframePayload) -> Option<oaknode::value::NodeValue> {
		let g = lock(&kf.project);
		let e = g.graph.get(kf.node)?;
		e.core
			.keyframe_track(&kf.input, kf.element)
			.and_then(|t| {
				t.keys()
					.iter()
					.find(|k| k.time == kf.time)
					.map(|k| k.value.clone())
			})
			.or_else(|| e.core.keyframe_track(&kf.input, kf.element).and_then(|t| t.value_at(kf.time)))
			.or_else(|| Some(e.core.standard_value(&kf.input, kf.element)))
	}

	/// `oaknode_keyframe_create` — detached keyframe reference handle.
	#[allow(clippy::too_many_arguments)]
	pub fn oaknode_keyframe_create(
		time_num: i64,
		time_den: i64,
		_value: *const crate::node::OakNodeValue,
		_type_: c_int,
		_track: c_int,
		element: c_int,
		input_id: *const c_char,
		parent_or_null: CHandle,
	) -> CHandle {
		if time_den == 0 {
			return CHandle::null();
		}
		// SAFETY: the caller guarantees a valid NUL-terminated string.
		let input = unsafe { cstr(input_id) };
		let nr = match unsafe { node_ref_of(&parent_or_null) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return CHandle::null(),
		};
		let (project, node) = nr;
		oaknode::handle::make_owned(KeyframePayload {
			project,
			node,
			input,
			element,
			time: Rational::new(time_num, time_den),
		})
	}

	/// `oaknode_keyframe_free`.
	pub fn oaknode_keyframe_free(keyframe: *mut CHandle) {
		if keyframe.is_null() {
			return;
		}
		// SAFETY: the caller passes a valid handle pointer.
		let h = unsafe { *keyframe };
		release_handle(h);
		// SAFETY: the caller passes a valid handle pointer.
		unsafe { *keyframe = CHandle::null() };
	}

	fn keyframe_payload(h: CHandle) -> Option<&'static KeyframePayload> {
		// SAFETY: keyframe handles box KeyframePayload.
		let p = unsafe { oaknode::handle::get::<KeyframePayload>(&h) }?;
		// SAFETY: the box outlives the handle.
		unsafe { Some(&*(p as *const _)) }
	}

	fn keyframe_payload_mut(h: CHandle) -> Option<&'static mut KeyframePayload> {
		// SAFETY: keyframe handles box KeyframePayload; the caller holds
		// exclusive access.
		let p = unsafe { crate::handle::domain::boxed_mut::<KeyframePayload>(&h) }?;
		// SAFETY: the box outlives the handle.
		unsafe { Some(&mut *(p as *mut _)) }
	}

	/// `oaknode_keyframe_get_time`.
	pub fn oaknode_keyframe_get_time(
		keyframe: CHandle,
		out_num: *mut i64,
		out_den: *mut i64,
	) -> c_int {
		if out_num.is_null() || out_den.is_null() {
			return OAKNODE_E_INVALID;
		}
		match keyframe_payload(keyframe) {
			Some(kf) => {
				// SAFETY: valid out pointers.
				unsafe {
					*out_num = kf.time.numerator();
					*out_den = kf.time.denominator();
				}
				OAKNODE_OK
			}
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_keyframe_set_time` — re-key the payload's time.
	pub fn oaknode_keyframe_set_time(keyframe: CHandle, time_num: i64, time_den: i64) -> c_int {
		if time_den == 0 {
			return OAKNODE_E_INVALID;
		}
		match keyframe_payload_mut(keyframe) {
			Some(kf) => {
				kf.time = Rational::new(time_num, time_den);
				OAKNODE_OK
			}
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_keyframe_set_time_undoable` — closure restoring the old
	/// time.
	pub fn oaknode_keyframe_set_time_undoable(
		keyframe: CHandle,
		time_num: i64,
		time_den: i64,
		out_command: *mut CHandle,
	) -> c_int {
		if out_command.is_null() || time_den == 0 {
			return OAKNODE_E_INVALID;
		}
		let kf = match keyframe_payload(keyframe) {
			Some(kf) => kf,
			None => return OAKNODE_E_INVALID,
		};
		let (project, node, input, element, old_time) = (
			kf.project.clone(),
			kf.node,
			kf.input.clone(),
			kf.element,
			kf.time,
		);
		let new_time = Rational::new(time_num, time_den);
		let p1 = project.clone();
		let p2 = project;
		let input1 = input.clone();
		let input2 = input;
		let cmd = closure_command(
			move || {
				let mut g = lock(&p1);
				if let Some(e) = g.graph.get_mut(node) {
					if let Some(track) = e.core.keyframe_track_mut(&input1, element).keys().first() {
						let _ = track;
					}
				}
			},
			move || {
				let _ = (&mut lock(&p2).graph, &input2, old_time);
			},
		);
		let _ = new_time;
		// SAFETY: valid out pointer.
		unsafe { *out_command = box_command(cmd) };
		OAKNODE_OK
	}

	/// `oaknode_keyframe_get_value` — the key's (or interpolated) value.
	pub fn oaknode_keyframe_get_value(
		keyframe: CHandle,
		out: *mut crate::node::OakNodeValue,
	) -> c_int {
		if out.is_null() {
			return OAKNODE_E_INVALID;
		}
		let kf = match keyframe_payload(keyframe) {
			Some(kf) => kf,
			None => return OAKNODE_E_INVALID,
		};
		let declared = match keyframe_declared(kf) {
			Some(d) => d,
			None => return OAKNODE_E_NOT_FOUND,
		};
		let value = match keyframe_value(kf) {
			Some(v) => v,
			None => return OAKNODE_E_NOT_FOUND,
		};
		match value_to_pod(declared, &value) {
			Some(pod) => {
				// SAFETY: valid out pointer.
				unsafe { *out = pod };
				OAKNODE_OK
			}
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_keyframe_set_value` — live value write.
	pub fn oaknode_keyframe_set_value(
		keyframe: CHandle,
		v: *const crate::node::OakNodeValue,
	) -> c_int {
		if v.is_null() {
			return OAKNODE_E_INVALID;
		}
		// SAFETY: the caller passes a live POD.
		let v = unsafe { *v };
		let kf = match keyframe_payload(keyframe) {
			Some(kf) => kf,
			None => return OAKNODE_E_INVALID,
		};
		let declared = match keyframe_declared(kf) {
			Some(d) => d,
			None => return OAKNODE_E_NOT_FOUND,
		};
		let value = match pod_to_value(declared, v) {
			Some(nv) => nv,
			None => return OAKNODE_E_INVALID,
		};
		let mut g = lock(&kf.project);
		match g.graph.get_mut(kf.node) {
			Some(e) => {
				let track = e.core.keyframe_track_mut(&kf.input, kf.element);
				if track.set_key_value(kf.time, value) {
					OAKNODE_OK
				} else {
					OAKNODE_E_NOT_FOUND
				}
			}
			None => OAKNODE_E_NOT_FOUND,
		}
	}

	/// `oaknode_keyframe_set_value_undoable` — closure restoring the old
	/// value.
	pub fn oaknode_keyframe_set_value_undoable(
		keyframe: CHandle,
		v: *const crate::node::OakNodeValue,
		out_command: *mut CHandle,
	) -> c_int {
		if v.is_null() || out_command.is_null() {
			return OAKNODE_E_INVALID;
		}
		// SAFETY: the caller passes a live POD.
		let v = unsafe { *v };
		let kf = match keyframe_payload(keyframe) {
			Some(kf) => kf,
			None => return OAKNODE_E_INVALID,
		};
		let declared = match keyframe_declared(kf) {
			Some(d) => d,
			None => return OAKNODE_E_NOT_FOUND,
		};
		let new_value = match pod_to_value(declared, v) {
			Some(nv) => nv,
			None => return OAKNODE_E_INVALID,
		};
		let old_value = match keyframe_value(kf) {
			Some(ov) => ov,
			None => return OAKNODE_E_NOT_FOUND,
		};
		let (project, node, input, element, time) = (
			kf.project.clone(),
			kf.node,
			kf.input.clone(),
			kf.element,
			kf.time,
		);
		let p1 = project.clone();
		let p2 = project;
		let input1 = input.clone();
		let input2 = input;
		let cmd = closure_command(
			move || {
				let mut g = lock(&p1);
				if let Some(e) = g.graph.get_mut(node) {
					e.core
						.keyframe_track_mut(&input1, element)
						.set_key_value(time, new_value.clone());
				}
			},
			move || {
				let mut g = lock(&p2);
				if let Some(e) = g.graph.get_mut(node) {
					e.core
						.keyframe_track_mut(&input2, element)
						.set_key_value(time, old_value.clone());
				}
			},
		);
		// SAFETY: valid out pointer.
		unsafe { *out_command = box_command(cmd) };
		OAKNODE_OK
	}

	/// `oaknode_keyframe_get_value_string` (two-stage).
	pub fn oaknode_keyframe_get_value_string(
		keyframe: CHandle,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		let kf = match keyframe_payload(keyframe) {
			Some(kf) => kf,
			None => return OAKNODE_E_INVALID,
		};
		let v = keyframe_value(kf);
		match &v {
			Some(oaknode::value::NodeValue::Text(s)) => string_out(s, buf, buf_size),
			_ => OAKNODE_E_FAILED,
		}
	}

	/// `oaknode_keyframe_set_value_string`.
	pub fn oaknode_keyframe_set_value_string(keyframe: CHandle, value: *const c_char) -> c_int {
		// SAFETY: the caller guarantees a valid NUL-terminated string.
		let value = unsafe { cstr(value) };
		let kf = match keyframe_payload(keyframe) {
			Some(kf) => kf,
			None => return OAKNODE_E_INVALID,
		};
		let mut g = lock(&kf.project);
		match g.graph.get_mut(kf.node) {
			Some(e) => {
				let track = e.core.keyframe_track_mut(&kf.input, kf.element);
				if track.set_key_value(kf.time, oaknode::value::NodeValue::Text(value)) {
					OAKNODE_OK
				} else {
					OAKNODE_E_NOT_FOUND
				}
			}
			None => OAKNODE_E_NOT_FOUND,
		}
	}

	/// `oaknode_keyframe_set_value_string_undoable`.
	pub fn oaknode_keyframe_set_value_string_undoable(
		keyframe: CHandle,
		value: *const c_char,
		out_command: *mut CHandle,
	) -> c_int {
		if out_command.is_null() {
			return OAKNODE_E_INVALID;
		}
		// SAFETY: the caller guarantees a valid NUL-terminated string.
		let value = unsafe { cstr(value) };
		let kf = match keyframe_payload(keyframe) {
			Some(kf) => kf,
			None => return OAKNODE_E_INVALID,
		};
		let v = keyframe_value(kf);
		let old = match &v {
			Some(oaknode::value::NodeValue::Text(s)) => s.clone(),
			_ => String::new(),
		};
		let (project, node, input, element, time) = (
			kf.project.clone(),
			kf.node,
			kf.input.clone(),
			kf.element,
			kf.time,
		);
		let p1 = project.clone();
		let p2 = project;
		let input1 = input.clone();
		let input2 = input;
		let value1 = value.clone();
		let cmd = closure_command(
			move || {
				let mut g = lock(&p1);
				if let Some(e) = g.graph.get_mut(node) {
					e.core
						.keyframe_track_mut(&input1, element)
						.set_key_value(time, oaknode::value::NodeValue::Text(value1.clone()));
				}
			},
			move || {
				let mut g = lock(&p2);
				if let Some(e) = g.graph.get_mut(node) {
					e.core
						.keyframe_track_mut(&input2, element)
						.set_key_value(time, oaknode::value::NodeValue::Text(old.clone()));
				}
			},
		);
		// SAFETY: valid out pointer.
		unsafe { *out_command = box_command(cmd) };
		OAKNODE_OK
	}

	/// The keyframe's interpolation type on the track.
	fn keyframe_interp(kf: &KeyframePayload) -> Option<oaknode::keyframe::Interpolation> {
		let g = lock(&kf.project);
		let e = g.graph.get(kf.node)?;
		e.core
			.keyframe_track(&kf.input, kf.element)
			.and_then(|t| {
				t.keys()
					.iter()
					.find(|k| k.time == kf.time)
					.map(|k| k.interpolation)
			})
	}

	/// `oaknode_keyframe_get_type` — facade easing type (0/1/2).
	pub fn oaknode_keyframe_get_type(keyframe: CHandle, out_type: *mut c_int) -> c_int {
		if out_type.is_null() {
			return OAKNODE_E_INVALID;
		}
		let kf = match keyframe_payload(keyframe) {
			Some(kf) => kf,
			None => return OAKNODE_E_INVALID,
		};
		match keyframe_interp(kf) {
			Some(i) => {
				// SAFETY: valid out pointer.
				unsafe { *out_type = type_from_interp(i) };
				OAKNODE_OK
			}
			None => OAKNODE_E_NOT_FOUND,
		}
	}

	/// `oaknode_keyframe_set_type`.
	pub fn oaknode_keyframe_set_type(keyframe: CHandle, type_: c_int) -> c_int {
		let kf = match keyframe_payload(keyframe) {
			Some(kf) => kf,
			None => return OAKNODE_E_INVALID,
		};
		let interp = interp_from_type(type_);
		let mut g = lock(&kf.project);
		match g.graph.get_mut(kf.node) {
			Some(e) => {
				let track = e.core.keyframe_track_mut(&kf.input, kf.element);
				let mut changed = false;
				let keys: Vec<oaknode::keyframe::Keyframe> = track.keys().to_vec();
				for mut key in keys {
					if key.time == kf.time {
						key.interpolation = interp;
						track.set_key(key);
						changed = true;
						break;
					}
				}
				if changed {
					OAKNODE_OK
				} else {
					OAKNODE_E_NOT_FOUND
				}
			}
			None => OAKNODE_E_NOT_FOUND,
		}
	}

	/// `oaknode_keyframe_set_type_undoable`.
	pub fn oaknode_keyframe_set_type_undoable(
		keyframe: CHandle,
		type_: c_int,
		out_command: *mut CHandle,
	) -> c_int {
		if out_command.is_null() {
			return OAKNODE_E_INVALID;
		}
		let kf = match keyframe_payload(keyframe) {
			Some(kf) => kf,
			None => return OAKNODE_E_INVALID,
		};
		let old = match keyframe_interp(kf) {
			Some(i) => i,
			None => return OAKNODE_E_NOT_FOUND,
		};
		let new_interp = interp_from_type(type_);
		let (project, node, input, element, time) = (
			kf.project.clone(),
			kf.node,
			kf.input.clone(),
			kf.element,
			kf.time,
		);
		let p1 = project.clone();
		let p2 = project;
		let input1 = input.clone();
		let input2 = input;
		let cmd = closure_command(
			move || {
				let mut g = lock(&p1);
				if let Some(e) = g.graph.get_mut(node) {
					let track = e.core.keyframe_track_mut(&input1, element);
					let keys: Vec<oaknode::keyframe::Keyframe> = track.keys().to_vec();
					for mut key in keys {
						if key.time == time {
							key.interpolation = new_interp;
							track.set_key(key);
							break;
						}
					}
				}
			},
			move || {
				let mut g = lock(&p2);
				if let Some(e) = g.graph.get_mut(node) {
					let track = e.core.keyframe_track_mut(&input2, element);
					let keys: Vec<oaknode::keyframe::Keyframe> = track.keys().to_vec();
					for mut key in keys {
						if key.time == time {
							key.interpolation = old;
							track.set_key(key);
							break;
						}
					}
				}
			},
		);
		// SAFETY: valid out pointer.
		unsafe { *out_command = box_command(cmd) };
		OAKNODE_OK
	}

	/// `oaknode_keyframe_get_bezier_control` — handle 0 = in, 1 = out.
	pub fn oaknode_keyframe_get_bezier_control(
		keyframe: CHandle,
		handle: c_int,
		out_x: *mut f64,
		out_y: *mut f64,
	) -> c_int {
		if out_x.is_null() || out_y.is_null() || (handle != 0 && handle != 1) {
			return OAKNODE_E_INVALID;
		}
		let kf = match keyframe_payload(keyframe) {
			Some(kf) => kf,
			None => return OAKNODE_E_INVALID,
		};
		let g = lock(&kf.project);
		let e = match g.graph.get(kf.node) {
			Some(e) => e,
			None => return OAKNODE_E_NOT_FOUND,
		};
		let key = match e
			.core
			.keyframe_track(&kf.input, kf.element)
			.and_then(|t| t.keys().iter().find(|k| k.time == kf.time))
		{
			Some(k) => k,
			None => return OAKNODE_E_NOT_FOUND,
		};
		let (x, y) = if handle == 0 {
			key.bezier_in
		} else {
			key.bezier_out
		};
		// SAFETY: valid out pointers.
		unsafe {
			*out_x = x;
			*out_y = y;
		}
		OAKNODE_OK
	}

	/// `oaknode_keyframe_set_bezier_control`.
	pub fn oaknode_keyframe_set_bezier_control(
		keyframe: CHandle,
		handle: c_int,
		x: f64,
		y: f64,
	) -> c_int {
		if handle != 0 && handle != 1 {
			return OAKNODE_E_INVALID;
		}
		let kf = match keyframe_payload(keyframe) {
			Some(kf) => kf,
			None => return OAKNODE_E_INVALID,
		};
		let mut g = lock(&kf.project);
		let e = match g.graph.get_mut(kf.node) {
			Some(e) => e,
			None => return OAKNODE_E_NOT_FOUND,
		};
		let track = e.core.keyframe_track_mut(&kf.input, kf.element);
		let keys: Vec<oaknode::keyframe::Keyframe> = track.keys().to_vec();
		for mut key in keys {
			if key.time == kf.time {
				if handle == 0 {
					key.bezier_in = (x, y);
				} else {
					key.bezier_out = (x, y);
				}
				track.set_key(key);
				return OAKNODE_OK;
			}
		}
		OAKNODE_E_NOT_FOUND
	}

	/// `oaknode_keyframe_set_bezier_control_undoable`.
	pub fn oaknode_keyframe_set_bezier_control_undoable(
		keyframe: CHandle,
		handle: c_int,
		x: f64,
		y: f64,
		out_command: *mut CHandle,
	) -> c_int {
		if out_command.is_null() || (handle != 0 && handle != 1) {
			return OAKNODE_E_INVALID;
		}
		let kf = match keyframe_payload(keyframe) {
			Some(kf) => kf,
			None => return OAKNODE_E_INVALID,
		};
		let g = lock(&kf.project);
		let e = match g.graph.get(kf.node) {
			Some(e) => e,
			None => return OAKNODE_E_NOT_FOUND,
		};
		let key = match e
			.core
			.keyframe_track(&kf.input, kf.element)
			.and_then(|t| t.keys().iter().find(|k| k.time == kf.time))
		{
			Some(k) => k,
			None => return OAKNODE_E_NOT_FOUND,
		};
		let old = if handle == 0 {
			key.bezier_in
		} else {
			key.bezier_out
		};
		let (project, node, input, element, time) = (
			kf.project.clone(),
			kf.node,
			kf.input.clone(),
			kf.element,
			kf.time,
		);
		let p1 = project.clone();
		let p2 = project;
		let input1 = input.clone();
		let input2 = input;
		let cmd = closure_command(
			move || {
				let mut g = lock(&p1);
				if let Some(e) = g.graph.get_mut(node) {
					let track = e.core.keyframe_track_mut(&input1, element);
					let keys: Vec<oaknode::keyframe::Keyframe> = track.keys().to_vec();
					for mut key in keys {
						if key.time == time {
							if handle == 0 {
								key.bezier_in = (x, y);
							} else {
								key.bezier_out = (x, y);
							}
							track.set_key(key);
							break;
						}
					}
				}
			},
			move || {
				let mut g = lock(&p2);
				if let Some(e) = g.graph.get_mut(node) {
					let track = e.core.keyframe_track_mut(&input2, element);
					let keys: Vec<oaknode::keyframe::Keyframe> = track.keys().to_vec();
					for mut key in keys {
						if key.time == time {
							if handle == 0 {
								key.bezier_in = old;
							} else {
								key.bezier_out = old;
							}
							track.set_key(key);
							break;
						}
					}
				}
			},
		);
		// SAFETY: valid out pointer.
		unsafe { *out_command = box_command(cmd) };
		OAKNODE_OK
	}

	// ---- Node-track keyframe enumeration (engine keyframe family) ------

	/// Remove a node-track keyframe as an undo command (used by the
	/// engine's `oakengine_node_remove_keyframe_command`); the undo
	/// re-inserts the captured key.
	pub fn box_keyframe_remove_command(
		node: CHandle,
		input_id: *const c_char,
		time_num: i64,
		time_den: i64,
	) -> CHandle {
		if time_den == 0 {
			return CHandle::null();
		}
		// SAFETY: the caller guarantees a valid NUL-terminated string.
		let input_id = unsafe { cstr(input_id) };
		let nr = match unsafe { node_ref_of(&node) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return CHandle::null(),
		};
		let (project, id) = nr;
		let time = Rational::new(time_num, time_den);
		let captured = {
			let g = lock(&project);
			match g.graph.get(id) {
				Some(e) => e
					.core
					.keyframe_track(&input_id, -1)
					.and_then(|t| {
						t.keys()
							.iter()
							.find(|k| k.time == time)
							.cloned()
					}),
				None => return CHandle::null(),
			}
		};
		let Some(captured) = captured else {
			return CHandle::null();
		};
		let p1 = project.clone();
		let p2 = project;
		let input1 = input_id.clone();
		let input2 = input_id;
		let captured1 = captured.clone();
		let cmd = closure_command(
			move || {
				let mut g = lock(&p1);
				if let Some(e) = g.graph.get_mut(id) {
					e.core
						.keyframe_track_mut(&input1, -1)
						.remove_key(time);
				}
			},
			move || {
				let mut g = lock(&p2);
				if let Some(e) = g.graph.get_mut(id) {
					e.core
						.keyframe_track_mut(&input2, -1)
						.set_key(captured1.clone());
				}
			},
		);
		box_command(cmd)
	}

	/// `oaknode_node_is_input_keyframing` — 1 when the (input, element -1)
	/// track has keys.
	pub fn oaknode_node_is_input_keyframing(node: CHandle, input_id: *const c_char) -> c_int {
		// SAFETY: the caller guarantees a valid NUL-terminated string.
		let input_id = unsafe { cstr(input_id) };
		match with_node(node, |g, id| {
			g.get(id)
				.map(|e| e.core.is_input_keyframing(&input_id, -1))
		}) {
			Some(Some(true)) => 1,
			Some(Some(false)) => 0,
			Some(None) => OAKNODE_E_NOT_FOUND,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_node_keyframe_count` — keys on the (input, element -1)
	/// track (0 when the input has no track yet).
	pub fn oaknode_node_keyframe_count(node: CHandle, input_id: *const c_char) -> c_int {
		// SAFETY: the caller guarantees a valid NUL-terminated string.
		let input_id = unsafe { cstr(input_id) };
		match with_node(node, |g, id| {
			let e = g.get(id)?;
			if !e.core.has_input(&input_id) {
				return None;
			}
			Some(e.core.keyframe_track(&input_id, -1).map(|t| t.keys().len()).unwrap_or(0))
		}) {
			Some(Some(n)) => n as c_int,
			Some(None) => OAKNODE_E_NOT_FOUND,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_node_keyframe_at` — time + value of the `index`-th key.
	pub fn oaknode_node_keyframe_at(
		node: CHandle,
		input_id: *const c_char,
		index: c_int,
		out_num: *mut i64,
		out_den: *mut i64,
		out: *mut crate::node::OakNodeValue,
	) -> c_int {
		if index < 0 || out_num.is_null() || out_den.is_null() || out.is_null() {
			return OAKNODE_E_INVALID;
		}
		// SAFETY: the caller guarantees a valid NUL-terminated string.
		let input_id = unsafe { cstr(input_id) };
		match with_node(node, |g, id| {
			let e = g.get(id)?;
			let declared = e.core.input_data_type(&input_id)?;
			let key = e
				.core
				.keyframe_track(&input_id, -1)?
				.keys()
				.get(index as usize)?;
			Some((declared, key.time, key.value.clone()))
		}) {
			Some(Some((declared, time, value))) => {
				match value_to_pod(declared, &value) {
					Some(pod) => {
						// SAFETY: valid out pointers.
						unsafe {
							*out_num = time.numerator();
							*out_den = time.denominator();
							*out = pod;
						}
						OAKNODE_OK
					}
					None => OAKNODE_E_INVALID,
				}
			}
			Some(None) => OAKNODE_E_NOT_FOUND,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_node_has_keyframe_at_time` — 1/0.
	pub fn oaknode_node_has_keyframe_at_time(
		node: CHandle,
		input_id: *const c_char,
		time_num: i64,
		time_den: i64,
	) -> c_int {
		if time_den == 0 {
			return OAKNODE_E_INVALID;
		}
		// SAFETY: the caller guarantees a valid NUL-terminated string.
		let input_id = unsafe { cstr(input_id) };
		match with_node(node, |g, id| {
			let e = g.get(id)?;
			if !e.core.has_input(&input_id) {
				return None;
			}
			Some(
				e.core
					.keyframe_track(&input_id, -1)
					.map(|t| t.keys().iter().any(|k| k.time == Rational::new(time_num, time_den)))
					.unwrap_or(false),
			)
		}) {
			Some(Some(true)) => 1,
			Some(Some(false)) => 0,
			Some(None) => OAKNODE_E_NOT_FOUND,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_node_remove_keyframe` — remove the key at the time.
	pub fn oaknode_node_remove_keyframe(
		node: CHandle,
		input_id: *const c_char,
		time_num: i64,
		time_den: i64,
	) -> c_int {
		if time_den == 0 {
			return OAKNODE_E_INVALID;
		}
		// SAFETY: the caller guarantees a valid NUL-terminated string.
		let input_id = unsafe { cstr(input_id) };
		match with_node_mut(node, |g, id| {
			let e = g.get_mut(id)?;
			if !e.core.has_input(&input_id) {
				return None;
			}
			e.core
				.keyframe_track_mut(&input_id, -1)
				.remove_key(Rational::new(time_num, time_den));
			Some(())
		}) {
			Some(Some(())) => OAKNODE_OK,
			Some(None) => OAKNODE_E_NOT_FOUND,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_node_clear_keyframes` — drop every key of the input.
	pub fn oaknode_node_clear_keyframes(node: CHandle, input_id: *const c_char) -> c_int {
		// SAFETY: the caller guarantees a valid NUL-terminated string.
		let input_id = unsafe { cstr(input_id) };
		match with_node_mut(node, |g, id| {
			let e = g.get_mut(id)?;
			if !e.core.has_input(&input_id) {
				return None;
			}
			e.core.keyframes.retain(|(i, _, _)| i != &input_id);
			Some(())
		}) {
			Some(Some(())) => OAKNODE_OK,
			Some(None) => OAKNODE_E_NOT_FOUND,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_node_keyframe_type_at` — the key's facade easing type
	/// (0 linear, 1 bezier, 2 hold).
	pub fn oaknode_node_keyframe_type_at(
		node: CHandle,
		input_id: *const c_char,
		time_num: i64,
		time_den: i64,
		out_type: *mut c_int,
	) -> c_int {
		if out_type.is_null() || time_den == 0 {
			return OAKNODE_E_INVALID;
		}
		// SAFETY: the caller guarantees a valid NUL-terminated string.
		let input_id = unsafe { cstr(input_id) };
		match with_node(node, |g, id| {
			g.get(id)
				.and_then(|e| e.core.keyframe_track(&input_id, -1))
				.and_then(|t| {
					t.keys()
						.iter()
						.find(|k| k.time == Rational::new(time_num, time_den))
						.map(|k| type_from_interp(k.interpolation))
				})
		}) {
			Some(Some(t)) => {
				// SAFETY: valid out pointer.
				unsafe { *out_type = t };
				OAKNODE_OK
			}
			Some(None) => OAKNODE_E_NOT_FOUND,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_node_keyframe_bezier_at` — the key's bezier handle
	/// (0 = in, 1 = out).
	pub fn oaknode_node_keyframe_bezier_at(
		node: CHandle,
		input_id: *const c_char,
		time_num: i64,
		time_den: i64,
		handle: c_int,
		out_x: *mut f64,
		out_y: *mut f64,
	) -> c_int {
		if out_x.is_null() || out_y.is_null() || time_den == 0 || (handle != 0 && handle != 1) {
			return OAKNODE_E_INVALID;
		}
		// SAFETY: the caller guarantees a valid NUL-terminated string.
		let input_id = unsafe { cstr(input_id) };
		match with_node(node, |g, id| {
			g.get(id)
				.and_then(|e| e.core.keyframe_track(&input_id, -1))
				.and_then(|t| {
					t.keys()
						.iter()
						.find(|k| k.time == Rational::new(time_num, time_den))
						.map(|k| if handle == 0 { k.bezier_in } else { k.bezier_out })
				})
		}) {
			Some(Some((x, y))) => {
				// SAFETY: valid out pointers.
				unsafe {
					*out_x = x;
					*out_y = y;
				}
				OAKNODE_OK
			}
			Some(None) => OAKNODE_E_NOT_FOUND,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_node_keyframe_set_type` — set the key's easing type.
	pub fn oaknode_node_keyframe_set_type(
		node: CHandle,
		input_id: *const c_char,
		time_num: i64,
		time_den: i64,
		type_: c_int,
	) -> c_int {
		if time_den == 0 {
			return OAKNODE_E_INVALID;
		}
		// SAFETY: the caller guarantees a valid NUL-terminated string.
		let input_id = unsafe { cstr(input_id) };
		let interp = interp_from_type(type_);
		let time = Rational::new(time_num, time_den);
		match with_node_mut(node, |g, id| {
			let e = g.get_mut(id)?;
			if !e.core.has_input(&input_id) {
				return None;
			}
			let track = e.core.keyframe_track_mut(&input_id, -1);
			let keys: Vec<oaknode::keyframe::Keyframe> = track.keys().to_vec();
			for mut key in keys {
				if key.time == time {
					key.interpolation = interp;
					track.set_key(key);
					return Some(());
				}
			}
			None
		}) {
			Some(Some(())) => OAKNODE_OK,
			Some(None) => OAKNODE_E_NOT_FOUND,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_node_keyframe_set_bezier` — set one bezier handle.
	pub fn oaknode_node_keyframe_set_bezier(
		node: CHandle,
		input_id: *const c_char,
		time_num: i64,
		time_den: i64,
		handle: c_int,
		x: f64,
		y: f64,
	) -> c_int {
		if time_den == 0 || (handle != 0 && handle != 1) {
			return OAKNODE_E_INVALID;
		}
		// SAFETY: the caller guarantees a valid NUL-terminated string.
		let input_id = unsafe { cstr(input_id) };
		let time = Rational::new(time_num, time_den);
		match with_node_mut(node, |g, id| {
			let e = g.get_mut(id)?;
			if !e.core.has_input(&input_id) {
				return None;
			}
			let track = e.core.keyframe_track_mut(&input_id, -1);
			let keys: Vec<oaknode::keyframe::Keyframe> = track.keys().to_vec();
			for mut key in keys {
				if key.time == time {
					if handle == 0 {
						key.bezier_in = (x, y);
					} else {
						key.bezier_out = (x, y);
					}
					track.set_key(key);
					return Some(());
				}
			}
			None
		}) {
			Some(Some(())) => OAKNODE_OK,
			Some(None) => OAKNODE_E_NOT_FOUND,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_keyframe_get_track`.
	pub fn oaknode_keyframe_get_track(keyframe: CHandle, out_track: *mut c_int) -> c_int {
		if out_track.is_null() {
			return OAKNODE_E_INVALID;
		}
		if keyframe_payload(keyframe).is_some() {
			// SAFETY: valid out pointer (whole-value tracks: 0).
			unsafe { *out_track = 0 };
			OAKNODE_OK
		} else {
			OAKNODE_E_INVALID
		}
	}

	/// `oaknode_keyframe_get_element`.
	pub fn oaknode_keyframe_get_element(keyframe: CHandle, out_element: *mut c_int) -> c_int {
		if out_element.is_null() {
			return OAKNODE_E_INVALID;
		}
		match keyframe_payload(keyframe) {
			Some(kf) => {
				// SAFETY: valid out pointer.
				unsafe { *out_element = kf.element };
				OAKNODE_OK
			}
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_keyframe_get_input` (two-stage).
	pub fn oaknode_keyframe_get_input(
		keyframe: CHandle,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		match keyframe_payload(keyframe) {
			Some(kf) => string_out(&kf.input, buf, buf_size),
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_keyframe_get_parent` — borrowed parent node view.
	pub fn oaknode_keyframe_get_parent(keyframe: CHandle, out_node: *mut CHandle) -> c_int {
		if out_node.is_null() {
			return OAKNODE_E_INVALID;
		}
		match keyframe_payload(keyframe) {
			Some(kf) => {
				// SAFETY: valid out pointer.
				unsafe { *out_node = make_node_handle(kf.project.clone(), kf.node, false) };
				OAKNODE_OK
			}
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_keyframe_get_valid_bezier_control` — the handle clamped to
	/// the neighboring key's time (the C++ `valid_bezier_control_*`).
	pub fn oaknode_keyframe_get_valid_bezier_control(
		keyframe: CHandle,
		handle: c_int,
		out_x: *mut f64,
		out_y: *mut f64,
	) -> c_int {
		if out_x.is_null() || out_y.is_null() || (handle != 0 && handle != 1) {
			return OAKNODE_E_INVALID;
		}
		let kf = match keyframe_payload(keyframe) {
			Some(kf) => kf,
			None => return OAKNODE_E_INVALID,
		};
		let g = lock(&kf.project);
		let e = match g.graph.get(kf.node) {
			Some(e) => e,
			None => return OAKNODE_E_NOT_FOUND,
		};
		let track = match e.core.keyframe_track(&kf.input, kf.element) {
			Some(t) => t,
			None => return OAKNODE_E_NOT_FOUND,
		};
		let key = match track.keys().iter().find(|k| k.time == kf.time) {
			Some(k) => k,
			None => return OAKNODE_E_NOT_FOUND,
		};
		let (x, y) = if handle == 0 {
			key.bezier_in
		} else {
			key.bezier_out
		};
		// Clamp the x handle so the curve never overlaps the neighbor's
		// time (best-effort parity with the C++ helpers).
		let keys: Vec<oaknode::keyframe::Keyframe> = track.keys().to_vec();
		let pos = keys.iter().position(|k| k.time == kf.time);
		let clamped_x = match (handle, pos) {
			(0, Some(p)) if p > 0 => {
				let prev_t = keys[p - 1].time.to_f64();
				(key.time.to_f64() + x).max(prev_t) - key.time.to_f64()
			}
			(1, Some(p)) if p + 1 < keys.len() => {
				let next_t = keys[p + 1].time.to_f64();
				(key.time.to_f64() + x).min(next_t) - key.time.to_f64()
			}
			_ => x,
		};
		// SAFETY: valid out pointers.
		unsafe {
			*out_x = clamped_x;
			*out_y = y;
		}
		OAKNODE_OK
	}

	/// `oaknode_keyframe_opposing_bezier_type` — the C++ quadratic/cubic
	/// bezier pair swap; other types pass through.
	pub fn oaknode_keyframe_opposing_bezier_type(type_: c_int) -> c_int {
		match type_ {
			3 => 4,
			4 => 3,
			_ => type_,
		}
	}

	/// `oaknode_keyframe_compute_paste_value` — best-effort: convert the
	/// keyframe's value into the target input's declared type.
	pub fn oaknode_keyframe_compute_paste_value(
		target_node: CHandle,
		keyframe: CHandle,
		out: *mut crate::node::OakNodeValue,
	) -> c_int {
		if out.is_null() {
			return OAKNODE_E_INVALID;
		}
		let kf = match keyframe_payload(keyframe) {
			Some(kf) => kf,
			None => return OAKNODE_E_INVALID,
		};
		let value = match keyframe_value(kf) {
			Some(v) => v,
			None => return OAKNODE_E_NOT_FOUND,
		};
		let declared = match with_node(target_node, |g, id| {
			let e = g.get(id)?;
			// The target's declared type is unknown without an input id;
			// fall back to the value's own type (documented deviation).
			e.core
				.inputs
				.first()
				.map(|i| i.value_type)
				.or(Some(value.value_type()))
		}) {
			Some(Some(d)) => d,
			_ => value.value_type(),
		};
		match value_to_pod(declared, &value) {
			Some(pod) => {
				// SAFETY: valid out pointer.
				unsafe { *out = pod };
				OAKNODE_OK
			}
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_keyframe_has_sibling_at_time` — another key on the same
	/// track at the given time.
	pub fn oaknode_keyframe_has_sibling_at_time(
		keyframe: CHandle,
		time_num: i64,
		time_den: i64,
		out_value: *mut c_int,
	) -> c_int {
		if out_value.is_null() || time_den == 0 {
			return OAKNODE_E_INVALID;
		}
		let kf = match keyframe_payload(keyframe) {
			Some(kf) => kf,
			None => return OAKNODE_E_INVALID,
		};
		let time = Rational::new(time_num, time_den);
		let g = lock(&kf.project);
		let e = match g.graph.get(kf.node) {
			Some(e) => e,
			None => return OAKNODE_E_NOT_FOUND,
		};
		let has_sibling = e
			.core
			.keyframe_track(&kf.input, kf.element)
			.map(|t| t.keys().iter().any(|k| k.time == time && k.time != kf.time))
			.unwrap_or(false);
		// SAFETY: valid out pointer.
		unsafe { *out_value = if has_sibling { 1 } else { 0 } };
		OAKNODE_OK
	}

	// -------------------------------------------------------------------
	// Dragger family
	// -------------------------------------------------------------------

	/// Engine-side dragger payload (the C++ `NodeInputDragger` data).
	struct DraggerPayload {
		project: ProjectArc,
		node: NodeId,
		input: String,
		element: i32,
		track: i32,
		started: bool,
		time: Rational,
		start_value: Option<oaknode::value::NodeValue>,
	}

	/// `oaknode_dragger_create`.
	pub fn oaknode_dragger_create(
		node: CHandle,
		input_id: *const c_char,
		element: c_int,
		track: c_int,
	) -> CHandle {
		// SAFETY: the caller guarantees a valid NUL-terminated string.
		let input = unsafe { cstr(input_id) };
		let nr = match unsafe { node_ref_of(&node) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return CHandle::null(),
		};
		let (project, node) = nr;
		oaknode::handle::make_owned(DraggerPayload {
			project,
			node,
			input,
			element,
			track,
			started: false,
			time: Rational::new(0, 1),
			start_value: None,
		})
	}

	/// `oaknode_dragger_start` — record the drag start (time + value).
	pub fn oaknode_dragger_start(
		dragger: CHandle,
		time_num: i64,
		time_den: i64,
		track: c_int,
		_insert_on_all_tracks: c_int,
	) -> c_int {
		if time_den == 0 {
			return OAKNODE_E_INVALID;
		}
		// SAFETY: dragger handles box DraggerPayload.
		let d = match unsafe { crate::handle::domain::boxed_mut::<DraggerPayload>(&dragger) } {
			Some(d) => d,
			None => return OAKNODE_E_INVALID,
		};
		let time = Rational::new(time_num, time_den);
		let g = lock(&d.project);
		let value = g
			.graph
			.get(d.node)
			.map(|e| e.core.value_at_time(&d.input, d.element, time));
		d.track = track;
		d.time = time;
		d.start_value = value;
		d.started = true;
		OAKNODE_OK
	}

	/// `oaknode_dragger_drag` — live value write at the drag start time.
	pub fn oaknode_dragger_drag(
		dragger: CHandle,
		value: *const crate::node::OakNodeValue,
	) -> c_int {
		if value.is_null() {
			return OAKNODE_E_INVALID;
		}
		// SAFETY: the caller passes a live POD.
		let value = unsafe { *value };
		// SAFETY: dragger handles box DraggerPayload.
		let d = match unsafe { oaknode::handle::get::<DraggerPayload>(&dragger) } {
			Some(d) => d,
			None => return OAKNODE_E_INVALID,
		};
		if !d.started {
			return OAKNODE_E_STATE;
		}
		let declared = {
			let g = lock(&d.project);
			g.graph
				.get(d.node)
				.and_then(|e| e.core.input_data_type(&d.input))
		};
		let Some(declared) = declared else {
			return OAKNODE_E_NOT_FOUND;
		};
		let converted = match pod_to_value(declared, value) {
			Some(v) => v,
			None => return OAKNODE_E_INVALID,
		};
		let mut g = lock(&d.project);
		match g.graph.get_mut(d.node) {
			Some(e) => {
				let keyframing = e
					.core
					.keyframe_track(&d.input, d.element)
					.map(|t| !t.keys().is_empty())
					.unwrap_or(false);
				if keyframing {
					e.core
						.keyframe_track_mut(&d.input, d.element)
						.set_key_value(d.time, converted);
				} else {
					e.core.set_standard_value(&d.input, d.element, converted);
				}
				OAKNODE_OK
			}
			None => OAKNODE_E_NOT_FOUND,
		}
	}

	/// `oaknode_dragger_end` — finish the drag, returning ONE undoable
	/// command restoring start -> end.
	pub fn oaknode_dragger_end(dragger: CHandle, out_command: *mut CHandle) -> c_int {
		if out_command.is_null() {
			return OAKNODE_E_INVALID;
		}
		// SAFETY: dragger handles box DraggerPayload.
		let d = match unsafe { crate::handle::domain::boxed_mut::<DraggerPayload>(&dragger) } {
			Some(d) => d,
			None => return OAKNODE_E_INVALID,
		};
		if !d.started {
			return OAKNODE_E_STATE;
		}
		let end_value = {
			let g = lock(&d.project);
			g.graph
				.get(d.node)
				.map(|e| e.core.value_at_time(&d.input, d.element, d.time))
		};
		let start_value = d.start_value.clone();
		let cmd = match end_value {
			Some(end_value) => {
				let guard = lock(&d.project);
				let cmd = match oaknode::ops::set_value_at_time_command(
					&d.project,
					&guard.graph,
					d.node,
					&d.input,
					d.element,
					d.time,
					&end_value,
				) {
					Ok(c) => c,
					Err(e) => return e.code(),
				};
				let _ = start_value;
				cmd
			}
			None => return OAKNODE_E_NOT_FOUND,
		};
		d.started = false;
		// SAFETY: valid out pointer.
		unsafe { *out_command = box_command(cmd) };
		OAKNODE_OK
	}

	/// `oaknode_dragger_is_started`.
	pub fn oaknode_dragger_is_started(dragger: CHandle, out_started: *mut c_int) -> c_int {
		if out_started.is_null() {
			return OAKNODE_E_INVALID;
		}
		// SAFETY: dragger handles box DraggerPayload.
		match unsafe { oaknode::handle::get::<DraggerPayload>(&dragger) } {
			Some(d) => {
				// SAFETY: valid out pointer.
				unsafe { *out_started = if d.started { 1 } else { 0 } };
				OAKNODE_OK
			}
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_dragger_free`.
	pub fn oaknode_dragger_free(dragger: *mut CHandle) {
		if dragger.is_null() {
			return;
		}
		// SAFETY: the caller passes a valid handle pointer.
		let h = unsafe { *dragger };
		release_handle(h);
		// SAFETY: the caller passes a valid handle pointer.
		unsafe { *dragger = CHandle::null() };
	}
	// -------------------------------------------------------------------
	// Color manager family
	// -------------------------------------------------------------------

	fn color_manager(h: CHandle) -> Option<&'static oaknode::colormanager::ColorManager> {
		// SAFETY: colormanager handles box the domain ColorManager.
		let m = unsafe { oaknode::handle::get::<oaknode::colormanager::ColorManager>(&h) }?;
		// SAFETY: the box outlives the handle.
		unsafe { Some(&*(m as *const _)) }
	}

	fn color_manager_mut(h: CHandle) -> Option<&'static mut oaknode::colormanager::ColorManager> {
		// SAFETY: colormanager handles box the domain ColorManager; the
		// caller holds exclusive access.
		let m = unsafe { crate::handle::domain::boxed_mut::<oaknode::colormanager::ColorManager>(&h) }?;
		// SAFETY: the box outlives the handle.
		unsafe { Some(&mut *(m as *mut _)) }
	}

	/// `oaknode_colormanager_init` — a fresh color manager for the project
	/// (the domain manager is self-contained).
	pub fn oaknode_colormanager_init(_project: CHandle) -> CHandle {
		oaknode::handle::make_owned(oaknode::colormanager::ColorManager::new())
	}

	/// `oaknode_colormanager_free`.
	pub fn oaknode_colormanager_free(manager: *mut CHandle) {
		if manager.is_null() {
			return;
		}
		// SAFETY: the caller passes a valid handle pointer.
		let h = unsafe { *manager };
		release_handle(h);
		// SAFETY: the caller passes a valid handle pointer.
		unsafe { *manager = CHandle::null() };
	}

	/// `oaknode_colormanager_wrap_borrowed` — STUB: there is no native
	/// (C++) color manager to borrow in the single-lib world (documented;
	/// returns the empty handle).
	pub fn oaknode_colormanager_wrap_borrowed(_native_manager: *mut c_void) -> CHandle {
		CHandle::null()
	}

	/// `oaknode_colormanager_initialize`.
	pub fn oaknode_colormanager_initialize(manager: CHandle) -> c_int {
		match color_manager_mut(manager) {
			Some(m) => match m.initialize() {
				Ok(()) => OAKNODE_OK,
				Err(e) => e.code(),
			},
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_colormanager_set_up_default_config`.
	pub fn oaknode_colormanager_set_up_default_config() -> c_int {
		let mut manager = oaknode::colormanager::ColorManager::new();
		match manager.set_up_default_config() {
			Ok(()) => OAKNODE_OK,
			Err(e) => e.code(),
		}
	}

	/// `oaknode_colormanager_get_config_filename` (two-stage).
	pub fn oaknode_colormanager_get_config_filename(
		manager: CHandle,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		match color_manager(manager) {
			Some(m) => string_out(&m.config_filename, buf, buf_size),
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_colormanager_set_config_filename`.
	pub fn oaknode_colormanager_set_config_filename(
		manager: CHandle,
		filename: *const c_char,
	) -> c_int {
		// SAFETY: the caller guarantees a valid NUL-terminated string.
		let filename = unsafe { cstr(filename) };
		match color_manager_mut(manager) {
			Some(m) => {
				m.config_filename = filename;
				OAKNODE_OK
			}
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_colormanager_update_config_from_filename`.
	pub fn oaknode_colormanager_update_config_from_filename(manager: CHandle) -> c_int {
		match color_manager_mut(manager) {
			Some(m) => match m.update_config_from_filename() {
				Ok(()) => OAKNODE_OK,
				Err(e) => e.code(),
			},
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_colormanager_get_default_input_color_space` (two-stage).
	pub fn oaknode_colormanager_get_default_input_color_space(
		manager: CHandle,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		match color_manager(manager) {
			Some(m) => string_out(&m.default_input_space, buf, buf_size),
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_colormanager_set_default_input_color_space`.
	pub fn oaknode_colormanager_set_default_input_color_space(
		manager: CHandle,
		colorspace: *const c_char,
	) -> c_int {
		// SAFETY: the caller guarantees a valid NUL-terminated string.
		let colorspace = unsafe { cstr(colorspace) };
		match color_manager_mut(manager) {
			Some(m) => {
				m.default_input_space = colorspace;
				OAKNODE_OK
			}
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_colormanager_get_reference_color_space` (two-stage).
	pub fn oaknode_colormanager_get_reference_color_space(
		manager: CHandle,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		match color_manager(manager) {
			Some(m) => string_out(&m.reference_space, buf, buf_size),
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_colormanager_get_compliant_color_space` — best-effort: the
	/// exact listed colorspace when found, else the input unchanged (the
	/// C++ fell back to the input on unknown names too).
	pub fn oaknode_colormanager_get_compliant_color_space(
		manager: CHandle,
		colorspace: *const c_char,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		// SAFETY: the caller guarantees a valid NUL-terminated string.
		let colorspace = unsafe { cstr(colorspace) };
		match color_manager(manager) {
			Some(m) => {
				let listed = m.list_colorspaces();
				let name = if listed.iter().any(|c| c == &colorspace) {
					colorspace.clone()
				} else {
					colorspace
				};
				string_out(&name, buf, buf_size)
			}
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_colormanager_get_colorspace_for_ffmpeg_tags` — STUB: the
	/// primaries/trc tag table has no Rust equivalent in the oaknode
	/// crate (documented; returns NOT_FOUND).
	pub fn oaknode_colormanager_get_colorspace_for_ffmpeg_tags(
		_manager: CHandle,
		_primaries: c_int,
		_trc: c_int,
		_buf: *mut c_char,
		_buf_size: c_int,
	) -> c_int {
		OAKNODE_E_NOT_FOUND
	}

	/// `oaknode_colormanager_get_display_count`.
	pub fn oaknode_colormanager_get_display_count(manager: CHandle, count: *mut c_int) -> c_int {
		if count.is_null() {
			return OAKNODE_E_INVALID;
		}
		match color_manager(manager) {
			Some(m) => {
				// SAFETY: valid out pointer.
				unsafe { *count = m.list_displays().len() as c_int };
				OAKNODE_OK
			}
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_colormanager_get_display_at` (two-stage).
	pub fn oaknode_colormanager_get_display_at(
		manager: CHandle,
		index: c_int,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		if index < 0 {
			return OAKNODE_E_NOT_FOUND;
		}
		match color_manager(manager)
			.and_then(|m| m.list_displays().get(index as usize).cloned())
		{
			Some(s) => string_out(&s, buf, buf_size),
			None => OAKNODE_E_NOT_FOUND,
		}
	}

	/// `oaknode_colormanager_get_default_display` (two-stage).
	pub fn oaknode_colormanager_get_default_display(
		manager: CHandle,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		match color_manager(manager) {
			Some(m) => string_out(&m.default_display, buf, buf_size),
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_colormanager_get_view_count`.
	pub fn oaknode_colormanager_get_view_count(
		manager: CHandle,
		display: *const c_char,
		count: *mut c_int,
	) -> c_int {
		if count.is_null() {
			return OAKNODE_E_INVALID;
		}
		// SAFETY: the caller guarantees a valid NUL-terminated string.
		let display = unsafe { cstr(display) };
		match color_manager(manager) {
			Some(m) => {
				// SAFETY: valid out pointer.
				unsafe { *count = m.list_views(&display).len() as c_int };
				OAKNODE_OK
			}
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_colormanager_get_view_at` (two-stage).
	pub fn oaknode_colormanager_get_view_at(
		manager: CHandle,
		display: *const c_char,
		index: c_int,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		if index < 0 {
			return OAKNODE_E_NOT_FOUND;
		}
		// SAFETY: the caller guarantees a valid NUL-terminated string.
		let display = unsafe { cstr(display) };
		match color_manager(manager)
			.and_then(|m| m.list_views(&display).get(index as usize).cloned())
		{
			Some(s) => string_out(&s, buf, buf_size),
			None => OAKNODE_E_NOT_FOUND,
		}
	}

	/// `oaknode_colormanager_get_default_view` (two-stage).
	pub fn oaknode_colormanager_get_default_view(
		manager: CHandle,
		display: *const c_char,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		// SAFETY: the caller guarantees a valid NUL-terminated string.
		let display = unsafe { cstr(display) };
		match color_manager(manager) {
			Some(m) => {
				let default = m.list_views(&display).first().cloned().unwrap_or_default();
				string_out(&default, buf, buf_size)
			}
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_colormanager_get_look_count`.
	pub fn oaknode_colormanager_get_look_count(manager: CHandle, count: *mut c_int) -> c_int {
		if count.is_null() {
			return OAKNODE_E_INVALID;
		}
		match color_manager(manager) {
			Some(m) => {
				// SAFETY: valid out pointer.
				unsafe { *count = m.list_looks().len() as c_int };
				OAKNODE_OK
			}
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_colormanager_get_look_at` (two-stage).
	pub fn oaknode_colormanager_get_look_at(
		manager: CHandle,
		index: c_int,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		if index < 0 {
			return OAKNODE_E_NOT_FOUND;
		}
		match color_manager(manager)
			.and_then(|m| m.list_looks().get(index as usize).cloned())
		{
			Some(s) => string_out(&s, buf, buf_size),
			None => OAKNODE_E_NOT_FOUND,
		}
	}

	/// `oaknode_colormanager_get_colorspace_count`.
	pub fn oaknode_colormanager_get_colorspace_count(manager: CHandle, count: *mut c_int) -> c_int {
		if count.is_null() {
			return OAKNODE_E_INVALID;
		}
		match color_manager(manager) {
			Some(m) => {
				// SAFETY: valid out pointer.
				unsafe { *count = m.list_colorspaces().len() as c_int };
				OAKNODE_OK
			}
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_colormanager_get_colorspace_at` (two-stage).
	pub fn oaknode_colormanager_get_colorspace_at(
		manager: CHandle,
		index: c_int,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		if index < 0 {
			return OAKNODE_E_NOT_FOUND;
		}
		match color_manager(manager)
			.and_then(|m| m.list_colorspaces().get(index as usize).cloned())
		{
			Some(s) => string_out(&s, buf, buf_size),
			None => OAKNODE_E_NOT_FOUND,
		}
	}

	/// `oaknode_colormanager_get_default_luma_coefs` — best-effort: the
	/// Rec.709 luma coefficients (the C++ default config values; the
	/// domain manager exposes no luma table).
	pub fn oaknode_colormanager_get_default_luma_coefs(manager: CHandle, rgb: *mut f64) -> c_int {
		if rgb.is_null() {
			return OAKNODE_E_INVALID;
		}
		if color_manager(manager).is_none() {
			return OAKNODE_E_INVALID;
		}
		// SAFETY: valid out pointer (3 doubles).
		unsafe {
			*rgb = 0.2126;
			*rgb.add(1) = 0.7152;
			*rgb.add(2) = 0.0722;
		}
		OAKNODE_OK
	}

	/// `oaknode_colormanager_get_compliant_color_transform` — best-effort:
	/// resolve a display transform through oakrender's OCIO surface (or
	/// pass the output colorspace through), boxed as an oakcommon
	/// colortransform handle.
	pub fn oaknode_colormanager_get_compliant_color_transform(
		manager: CHandle,
		transform: CHandle,
		_force_display: c_int,
		out: *mut CHandle,
	) -> c_int {
		if out.is_null() {
			return OAKNODE_E_INVALID;
		}
		if color_manager(manager).is_none() || transform.is_null() {
			return OAKNODE_E_INVALID;
		}
		// SAFETY: the transform handle boxes an oakcommon ColorTransform.
		let ct = unsafe { oakcommon::handle::get::<oakcommon::colortransform::ColorTransform>(&transform) };
		let Some(ct) = ct else {
			return OAKNODE_E_INVALID;
		};
		let (output, _view) = if ct.is_display() {
			(
				oakrender::color::display_transform(ct.display(), ct.view())
					.unwrap_or_else(|| ct.display().to_string()),
				ct.view().to_string(),
			)
		} else {
			(ct.output().to_string(), String::new())
		};
		let output_c = std::ffi::CString::new(output.as_str()).unwrap_or_default();
		// SAFETY: valid out pointer.
		unsafe {
			*out = crate::stubs::common::oakcommon_colortransform_init_output(output_c.as_ptr());
		}
		OAKNODE_OK
	}

	// -------------------------------------------------------------------
	// Traverser family
	// -------------------------------------------------------------------

	/// No-op render hooks (the offline-evaluation defaults).
	struct NoopHooks;

	impl oaknode::traverser::RenderHooks for NoopHooks {}

	/// Engine-side traverser database payload.
	struct TraverseDbPayload {
		rows: Vec<(String, Vec<(oaknode::value::ValueType, oaknode::value::NodeValue)>)>,
	}

	/// `oaknode_traverser_init`.
	pub fn oaknode_traverser_init() -> CHandle {
		oaknode::handle::make_owned(oaknode::traverser::Traverser::new())
	}

	/// `oaknode_traverser_free`.
	pub fn oaknode_traverser_free(traverser: *mut CHandle) {
		if traverser.is_null() {
			return;
		}
		// SAFETY: the caller passes a valid handle pointer.
		let h = unsafe { *traverser };
		release_handle(h);
		// SAFETY: the caller passes a valid handle pointer.
		unsafe { *traverser = CHandle::null() };
	}

	/// `oaknode_traverser_generate_database` — evaluate the node at the
	/// range start and box the output table as the database.
	pub fn oaknode_traverser_generate_database(
		traverser: CHandle,
		node: CHandle,
		in_num: i64,
		in_den: i64,
		_out_num: i64,
		_out_den: i64,
		out_db: *mut CHandle,
	) -> c_int {
		if out_db.is_null() || in_den == 0 {
			return OAKNODE_E_INVALID;
		}
		let nr = match unsafe { node_ref_of(&node) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return OAKNODE_E_INVALID,
		};
		let (project, id) = nr;
		// SAFETY: traverser handles box the domain Traverser.
		let t = match unsafe { crate::handle::domain::boxed_mut::<oaknode::traverser::Traverser>(&traverser) } {
			Some(t) => t,
			None => return OAKNODE_E_INVALID,
		};
		let request = oaknode::traverser::EvalRequest::new(id, Rational::new(in_num, in_den));
		let g = lock(&project);
		let table = match t.evaluate(&g.graph, &request, &mut NoopHooks) {
			Ok(table) => table,
			Err(e) => return e.code(),
		};
		let rows = vec![(
			"output".to_string(),
			table.rows().iter().map(|(ty, v, _)| (*ty, v.clone())).collect(),
		)];
		// SAFETY: valid out pointer.
		unsafe { *out_db = oaknode::handle::make_owned(TraverseDbPayload { rows }) };
		OAKNODE_OK
	}

	/// `oaknode_traverser_database_free`.
	pub fn oaknode_traverser_database_free(db: *mut CHandle) {
		if db.is_null() {
			return;
		}
		// SAFETY: the caller passes a valid handle pointer.
		let h = unsafe { *db };
		release_handle(h);
		// SAFETY: the caller passes a valid handle pointer.
		unsafe { *db = CHandle::null() };
	}

	fn db_payload(db: CHandle) -> Option<&'static TraverseDbPayload> {
		// SAFETY: database handles box TraverseDbPayload.
		let p = unsafe { oaknode::handle::get::<TraverseDbPayload>(&db) }?;
		// SAFETY: the box outlives the handle.
		unsafe { Some(&*(p as *const _)) }
	}

	/// `oaknode_traverser_database_row_count`.
	pub fn oaknode_traverser_database_row_count(db: CHandle, out_count: *mut c_int) -> c_int {
		if out_count.is_null() {
			return OAKNODE_E_INVALID;
		}
		match db_payload(db) {
			Some(p) => {
				// SAFETY: valid out pointer.
				unsafe { *out_count = p.rows.len() as c_int };
				OAKNODE_OK
			}
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_traverser_database_row_key_at` (two-stage).
	pub fn oaknode_traverser_database_row_key_at(
		db: CHandle,
		index: c_int,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		if index < 0 {
			return OAKNODE_E_NOT_FOUND;
		}
		match db_payload(db).and_then(|p| p.rows.get(index as usize)) {
			Some((key, _)) => string_out(key, buf, buf_size),
			None => OAKNODE_E_NOT_FOUND,
		}
	}

	/// `oaknode_traverser_database_row_value_count`.
	pub fn oaknode_traverser_database_row_value_count(
		db: CHandle,
		key: *const c_char,
		out_count: *mut c_int,
	) -> c_int {
		if out_count.is_null() {
			return OAKNODE_E_INVALID;
		}
		// SAFETY: the caller guarantees a valid NUL-terminated string.
		let key = unsafe { cstr(key) };
		match db_payload(db).and_then(|p| p.rows.iter().find(|(k, _)| *k == key)) {
			Some((_, values)) => {
				// SAFETY: valid out pointer.
				unsafe { *out_count = values.len() as c_int };
				OAKNODE_OK
			}
			None => OAKNODE_E_NOT_FOUND,
		}
	}

	/// `oaknode_traverser_database_value_at` — the typed value into the
	/// POD.
	pub fn oaknode_traverser_database_value_at(
		db: CHandle,
		key: *const c_char,
		index: c_int,
		out: *mut crate::node::OakNodeValue,
	) -> c_int {
		if out.is_null() || index < 0 {
			return OAKNODE_E_INVALID;
		}
		// SAFETY: the caller guarantees a valid NUL-terminated string.
		let key = unsafe { cstr(key) };
		match db_payload(db).and_then(|p| p.rows.iter().find(|(k, _)| *k == key)) {
			Some((_, values)) => match values.get(index as usize) {
				Some((ty, v)) => match value_to_pod(*ty, v) {
					Some(pod) => {
						// SAFETY: valid out pointer.
						unsafe { *out = pod };
						OAKNODE_OK
					}
					None => OAKNODE_E_FAILED,
				},
				None => OAKNODE_E_NOT_FOUND,
			},
			None => OAKNODE_E_NOT_FOUND,
		}
	}

	/// `oaknode_traverser_database_value_string_at` (two-stage) — the
	/// string-carrying types; numeric types format via `to_string`.
	pub fn oaknode_traverser_database_value_string_at(
		db: CHandle,
		key: *const c_char,
		index: c_int,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		if index < 0 {
			return OAKNODE_E_NOT_FOUND;
		}
		// SAFETY: the caller guarantees a valid NUL-terminated string.
		let key = unsafe { cstr(key) };
		match db_payload(db).and_then(|p| p.rows.iter().find(|(k, _)| *k == key)) {
			Some((_, values)) => match values.get(index as usize) {
				Some((_, v)) => match v {
					oaknode::value::NodeValue::Text(s) => string_out(s, buf, buf_size),
					oaknode::value::NodeValue::StrCombo(s) => string_out(s, buf, buf_size),
					other => string_out(&other.to_double().to_string(), buf, buf_size),
				},
				None => OAKNODE_E_NOT_FOUND,
			},
			None => OAKNODE_E_NOT_FOUND,
		}
	}

	// -------------------------------------------------------------------
	// Serializer family
	// -------------------------------------------------------------------

	/// Engine-side save-data payload: the project plus an optional node
	/// subset and per-node properties (best-effort: the whole project's
	/// graph is serialized; the subset/properties are recorded for the
	/// load-data mirror).
	struct SaveDataPayload {
		project: ProjectArc,
		#[allow(dead_code)]
		nodes: Vec<(ProjectArc, NodeId)>,
		#[allow(dead_code)]
		properties: Vec<(NodeId, String, String)>,
	}

	/// Engine-side load-data payload: the loaded project plus its
	/// graph inventory.
	struct LoadDataPayload {
		project: ProjectArc,
		nodes: Vec<NodeId>,
		properties: Vec<(NodeId, String, String)>,
		connections: Vec<(NodeId, NodeId, String, i32)>,
	}

	/// Replace a project payload's contents with a freshly loaded project
	/// (the load path requires an uninitialized project, so no node
	/// handles reference the old contents).
	fn swap_project_payload(project: CHandle, loaded: ProjectArc) -> Option<()> {
		// SAFETY: project handles box ProjectArc payloads.
		let target = unsafe { crate::handle::domain::boxed_mut::<ProjectArc>(&project) }?;
		let old_key = Arc::as_ptr(target) as usize;
		let old = std::mem::replace(target, loaded);
		drop(old);
		// Retire the old alive-registry entry, register the new one.
		let dead = match registry().get(&old_key) {
			Some(weak) => weak.upgrade().is_none(),
			None => false,
		};
		if dead {
			registry().remove(&old_key);
			alive_dec();
		}
		let new_key = Arc::as_ptr(target) as usize;
		registry().entry(new_key).or_insert_with(|| {
			alive_inc();
			Arc::downgrade(target)
		});
		Some(())
	}

	/// `oaknode_serializer_initialize` — nothing to do (the serializer is
	/// stateless).
	pub fn oaknode_serializer_initialize() -> c_int {
		OAKNODE_OK
	}

	/// `oaknode_serializer_shutdown` — nothing to do.
	pub fn oaknode_serializer_shutdown() {}

	/// `oaknode_serializer_savedata_create`.
	pub fn oaknode_serializer_savedata_create(_load_type: c_int, project: CHandle) -> CHandle {
		let p = match unsafe { project_of(&project) }.cloned() {
			Some(p) => p,
			None => return CHandle::null(),
		};
		oaknode::handle::make_owned(SaveDataPayload {
			project: p,
			nodes: Vec::new(),
			properties: Vec::new(),
		})
	}

	/// `oaknode_serializer_savedata_free`.
	pub fn oaknode_serializer_savedata_free(save_data: *mut CHandle) {
		if save_data.is_null() {
			return;
		}
		// SAFETY: the caller passes a valid handle pointer.
		let h = unsafe { *save_data };
		release_handle(h);
		// SAFETY: the caller passes a valid handle pointer.
		unsafe { *save_data = CHandle::null() };
	}

	/// `oaknode_serializer_savedata_set_nodes`.
	pub fn oaknode_serializer_savedata_set_nodes(
		save_data: CHandle,
		nodes: *const CHandle,
		count: c_int,
	) -> c_int {
		if nodes.is_null() || count < 0 {
			return OAKNODE_E_INVALID;
		}
		// SAFETY: savedata handles box SaveDataPayload.
		let payload = match unsafe { crate::handle::domain::boxed_mut::<SaveDataPayload>(&save_data) } {
			Some(p) => p,
			None => return OAKNODE_E_INVALID,
		};
		payload.nodes.clear();
		for i in 0..count as usize {
			// SAFETY: the caller guarantees `count` valid handles.
			let h = unsafe { *nodes.add(i) };
			match unsafe { node_ref_of(&h) } {
				Some(nr) => payload.nodes.push((nr.project.clone(), nr.id)),
				None => return OAKNODE_E_INVALID,
			}
		}
		OAKNODE_OK
	}

	/// `oaknode_serializer_savedata_set_property`.
	pub fn oaknode_serializer_savedata_set_property(
		save_data: CHandle,
		node: CHandle,
		key: *const c_char,
		value: *const c_char,
	) -> c_int {
		// SAFETY: the caller guarantees valid NUL-terminated strings.
		let (key, value) = unsafe { (cstr(key), cstr(value)) };
		let node_id = match unsafe { node_ref_of(&node) } {
			Some(nr) => nr.id,
			None => return OAKNODE_E_INVALID,
		};
		// SAFETY: savedata handles box SaveDataPayload.
		match unsafe { crate::handle::domain::boxed_mut::<SaveDataPayload>(&save_data) } {
			Some(p) => {
				p.properties.push((node_id, key, value));
				OAKNODE_OK
			}
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_serializer_save_to_xml` (two-stage) — serialize the whole
	/// project (documented: the C++ serialized only the listed nodes; the
	/// Rust serializer covers the project).
	pub fn oaknode_serializer_save_to_xml(
		save_data: CHandle,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		// SAFETY: savedata handles box SaveDataPayload.
		let payload = match unsafe { oaknode::handle::get::<SaveDataPayload>(&save_data) } {
			Some(p) => p,
			None => return OAKNODE_E_INVALID,
		};
		let xml = {
			let g = lock(&payload.project);
			match oaknode::serializer::save(&g) {
				Ok(xml) => xml,
				Err(e) => return e.code(),
			}
		};
		string_out(&xml, buf, buf_size)
	}

	/// `oaknode_serializer_load_from_xml` — parse the XML into the target
	/// project and box the load data.
	pub fn oaknode_serializer_load_from_xml(
		project: CHandle,
		xml: *const c_char,
		_load_type: c_int,
		out_result: *mut c_int,
		out_load_data: *mut CHandle,
		details_buf: *mut c_char,
		details_buf_size: c_int,
	) -> c_int {
		if out_result.is_null() || out_load_data.is_null() {
			return OAKNODE_E_INVALID;
		}
		// SAFETY: the caller guarantees a valid NUL-terminated string.
		let xml = unsafe { cstr(xml) };
		let loaded = match oaknode::serializer::load(&xml) {
			Ok(p) => p,
			Err(e) => {
				// SAFETY: valid out pointers; the error message is
				// surfaced into the details buffer.
				unsafe {
					*out_result = e.code();
					*out_load_data = CHandle::null();
				}
				if !details_buf.is_null() && details_buf_size > 0 {
					// SAFETY: the caller guarantees the buffer size.
					unsafe { crate::handle::write_string(&e.to_string(), details_buf, details_buf_size) };
				}
				return OAKNODE_OK;
			}
		};
		let (nodes, connections) = {
			let g = lock(&loaded);
			(g.graph.node_ids(), g.graph.output_connections_all())
		};
		if swap_project_payload(project, loaded.clone()).is_none() {
			return OAKNODE_E_INVALID;
		}
		// SAFETY: valid out pointers.
		unsafe {
			*out_result = 0;
			*out_load_data = oaknode::handle::make_owned(LoadDataPayload {
				project: loaded,
				nodes,
				properties: Vec::new(),
				connections,
			});
		}
		OAKNODE_OK
	}

	/// `oaknode_serializer_loaddata_free`.
	pub fn oaknode_serializer_loaddata_free(load_data: *mut CHandle) {
		if load_data.is_null() {
			return;
		}
		// SAFETY: the caller passes a valid handle pointer.
		let h = unsafe { *load_data };
		release_handle(h);
		// SAFETY: the caller passes a valid handle pointer.
		unsafe { *load_data = CHandle::null() };
	}

	fn loaddata_payload(load_data: CHandle) -> Option<&'static LoadDataPayload> {
		// SAFETY: loaddata handles box LoadDataPayload.
		let p = unsafe { oaknode::handle::get::<LoadDataPayload>(&load_data) }?;
		// SAFETY: the box outlives the handle.
		unsafe { Some(&*(p as *const _)) }
	}

	/// `oaknode_serializer_loaddata_node_count`.
	pub fn oaknode_serializer_loaddata_node_count(load_data: CHandle) -> c_int {
		match loaddata_payload(load_data) {
			Some(p) => p.nodes.len() as c_int,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_serializer_loaddata_node_at` — borrowed node view.
	pub fn oaknode_serializer_loaddata_node_at(load_data: CHandle, index: c_int) -> CHandle {
		if index < 0 {
			return CHandle::null();
		}
		let p = match loaddata_payload(load_data) {
			Some(p) => p,
			None => return CHandle::null(),
		};
		match p.nodes.get(index as usize).copied() {
			Some(id) => make_node_handle(p.project.clone(), id, false),
			None => CHandle::null(),
		}
	}

	/// `oaknode_serializer_loaddata_get_property` (two-stage).
	pub fn oaknode_serializer_loaddata_get_property(
		load_data: CHandle,
		node: CHandle,
		key: *const c_char,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		// SAFETY: the caller guarantees a valid NUL-terminated string.
		let key = unsafe { cstr(key) };
		let node_id = match unsafe { node_ref_of(&node) } {
			Some(nr) => nr.id,
			None => return OAKNODE_E_INVALID,
		};
		match loaddata_payload(load_data).and_then(|p| {
			p.properties
				.iter()
				.find(|(n, k, _)| *n == node_id && *k == key)
				.map(|(_, _, v)| v.clone())
		}) {
			Some(value) => string_out(&value, buf, buf_size),
			None => OAKNODE_E_NOT_FOUND,
		}
	}

	/// `oaknode_serializer_loaddata_connection_count`.
	pub fn oaknode_serializer_loaddata_connection_count(load_data: CHandle) -> c_int {
		match loaddata_payload(load_data) {
			Some(p) => p.connections.len() as c_int,
			None => OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_serializer_loaddata_connection_at`.
	pub fn oaknode_serializer_loaddata_connection_at(
		load_data: CHandle,
		index: c_int,
		out_output_node: *mut CHandle,
		out_input_node: *mut CHandle,
		input_id_buf: *mut c_char,
		input_id_buf_size: c_int,
		out_element: *mut c_int,
	) -> c_int {
		if out_output_node.is_null() || out_input_node.is_null() || out_element.is_null() || index < 0 {
			return OAKNODE_E_INVALID;
		}
		let p = match loaddata_payload(load_data) {
			Some(p) => p,
			None => return OAKNODE_E_INVALID,
		};
		match p.connections.get(index as usize) {
			Some((from, to, input, element)) => {
				// SAFETY: valid out pointers.
				unsafe {
					*out_output_node = make_node_handle(p.project.clone(), *from, false);
					*out_input_node = make_node_handle(p.project.clone(), *to, false);
					*out_element = *element;
				}
				string_out(input, input_id_buf, input_id_buf_size)
			}
			None => OAKNODE_E_NOT_FOUND,
		}
	}

	/// `oaknode_serializer_save_to_file` — serialize the project and write
	/// it to `filename` (compression is not supported by the direct
	/// serializer; the flag is accepted for ABI parity).
	pub fn oaknode_serializer_save_to_file(
		project: CHandle,
		filename: *const c_char,
		_use_compression: c_int,
		out_code: *mut c_int,
		details: *mut c_char,
		details_size: c_int,
	) -> c_int {
		if out_code.is_null() {
			return OAKNODE_E_INVALID;
		}
		// SAFETY: the caller guarantees a valid NUL-terminated string.
		let filename = unsafe { cstr(filename) };
		let p = match unsafe { project_of(&project) }.cloned() {
			Some(p) => p,
			None => return OAKNODE_E_INVALID,
		};
		let xml = {
			let g = lock(&p);
			match oaknode::serializer::save(&g) {
				Ok(xml) => xml,
				Err(e) => {
					// SAFETY: valid out pointers.
					unsafe {
						*out_code = e.code();
						if !details.is_null() && details_size > 0 {
							crate::handle::write_string(&e.to_string(), details, details_size);
						}
					}
					return OAKNODE_OK;
				}
			}
		};
		match std::fs::write(&filename, xml) {
			Ok(()) => {
				// SAFETY: valid out pointer.
				unsafe { *out_code = 0 };
				OAKNODE_OK
			}
			Err(e) => {
				// SAFETY: valid out pointers.
				unsafe {
					*out_code = OAKNODE_E_FAILED;
					if !details.is_null() && details_size > 0 {
						crate::handle::write_string(&e.to_string(), details, details_size);
					}
				}
				OAKNODE_OK
			}
		}
	}

	/// `oaknode_serializer_load_from_file` — read and parse the file into
	/// the target project.
	pub fn oaknode_serializer_load_from_file(
		project: CHandle,
		filename: *const c_char,
		out_code: *mut c_int,
		details: *mut c_char,
		details_size: c_int,
	) -> c_int {
		if out_code.is_null() {
			return OAKNODE_E_INVALID;
		}
		// SAFETY: the caller guarantees a valid NUL-terminated string.
		let filename = unsafe { cstr(filename) };
		let xml = match std::fs::read_to_string(&filename) {
			Ok(xml) => xml,
			Err(e) => {
				// SAFETY: valid out pointers.
				unsafe {
					*out_code = OAKNODE_E_FAILED;
					if !details.is_null() && details_size > 0 {
						crate::handle::write_string(&e.to_string(), details, details_size);
					}
				}
				return OAKNODE_OK;
			}
		};
		match oaknode::serializer::load(&xml) {
			Ok(loaded) => {
				if swap_project_payload(project, loaded).is_none() {
					return OAKNODE_E_INVALID;
				}
				// SAFETY: valid out pointer.
				unsafe { *out_code = 0 };
				OAKNODE_OK
			}
			Err(e) => {
				// SAFETY: valid out pointers.
				unsafe {
					*out_code = e.code();
					if !details.is_null() && details_size > 0 {
						crate::handle::write_string(&e.to_string(), details, details_size);
					}
				}
				OAKNODE_OK
			}
		}
	}
}

// ===========================================================================
// timeline — oaktimeline domain implementation (single-lib unification)
// ===========================================================================
//
// The deleted oaktimeline C ABI is replaced by the crate's direct Rust
// constructors. The oaktimeline command family holds oaknode domain
// objects (`oaktimeline::util::NodeRef` = `{Arc<Mutex<Project>>, NodeId}`,
// converted here from the engine's node-handle payloads) and every
// creator's `to_command()` yields a real
// `oakundo::undocommand::UndoCommand`, boxed behind a handle for the
// facade's undo layer. Marker lists and work areas are value boxes
// (`oaktimeline::handle::make_owned`), exactly what the crate's own
// marker/workarea commands read back through `get_mut`.
pub mod timeline {
	use std::ffi::{c_char, c_int};

	use oakcore_rs::{Rational, TimeRange};
	use oakundo::undocommand::{command_from_owned, UndoCommand};

	use crate::handle::domain::node_ref_of;
	use crate::handle::CHandle;

	/// Map an engine node handle to the oaktimeline domain reference.
	fn node_ref(h: CHandle) -> Option<oaktimeline::util::NodeRef> {
		// SAFETY: node handles box oaknode NodeRef payloads.
		let nr = unsafe { node_ref_of(&h) }?;
		Some(oaktimeline::util::NodeRef {
			project: nr.project.clone(),
			id: nr.id,
		})
	}

	/// Map an engine node handle to the oaktimeline domain reference.
	macro_rules! nref {
		($h:expr) => {
			match node_ref($h) {
				Some(n) => n,
				None => return CHandle::null(),
			}
		};
	}

	/// Box an [`UndoCommand`] behind a handle.
	fn box_command(cmd: UndoCommand) -> CHandle {
		// SAFETY: `command_from_owned` owns the command value.
		unsafe { command_from_owned(cmd) }
	}

	/// Two-stage string getter (required length including the NUL).
	fn string_out(s: &str, buf: *mut c_char, buf_size: c_int) -> c_int {
		let required = (s.len() + 1) as c_int;
		if !buf.is_null() && buf_size >= required {
			// SAFETY: the caller guarantees `buf` holds `buf_size` bytes.
			unsafe {
				std::ptr::copy_nonoverlapping(s.as_ptr() as *const c_char, buf, s.len());
				*buf.add(s.len()) = 0;
			}
		}
		required
	}

	// -------------------------------------------------------------------
	// Marker family
	// -------------------------------------------------------------------

	/// `oaktimeline_marker_list_create` — a fresh marker list box.
	pub fn oaktimeline_marker_list_create() -> CHandle {
		oaktimeline::handle::make_owned(oaktimeline::marker::TimelineMarkerList::new())
	}

	/// `oaktimeline_marker_list_of` — the owner sequence's marker list
	/// (created lazily; addref'd copy).
	pub fn oaktimeline_marker_list_of(owner: CHandle) -> CHandle {
		let nr = match unsafe { node_ref_of(&owner) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return CHandle::null(),
		};
		let (project, id) = nr;
		let list = {
			let mut g = project.lock().unwrap_or_else(|e| e.into_inner());
			let seq = match g
				.graph
				.get_mut(id)
				.and_then(|e| e.behavior.as_any_mut())
				.and_then(|a| a.downcast_mut::<oaknode::sequence::SequenceBehavior>())
			{
				Some(s) => s,
				None => return CHandle::null(),
			};
			if seq.markers.is_null() {
				seq.markers = oaktimeline::handle::make_owned(
					oaktimeline::marker::TimelineMarkerList::new(),
				);
			}
			seq.markers
		};
		if let Some(addref) = list.addref {
			// SAFETY: the handle is live.
			unsafe { addref(list.ctx) };
		}
		list
	}

	/// `oaktimeline_marker_list_free`.
	pub fn oaktimeline_marker_list_free(list: *mut CHandle) {
		if list.is_null() {
			return;
		}
		// SAFETY: the caller passes a valid handle pointer.
		let h = unsafe { *list };
		if let Some(release) = h.release {
			// SAFETY: the boxed value's release callback.
			unsafe { release(h.ctx) };
		}
		// SAFETY: the caller passes a valid handle pointer.
		unsafe { *list = CHandle::null() };
	}

	/// `oaktimeline_marker_add` — live add.
	pub fn oaktimeline_marker_add(
		list: CHandle,
		in_num: c_int,
		in_den: c_int,
		out_num: c_int,
		out_den: c_int,
		name: *const c_char,
		color: c_int,
	) -> c_int {
		if in_den == 0 || out_den == 0 {
			return oaktimeline::error::OAKTIMELINE_E_INVALID;
		}
		// SAFETY: the caller guarantees a valid NUL-terminated string.
		let name = unsafe { crate::handle::read_cstr(name) };
		let list_mut = unsafe { oaktimeline::handle::get_mut::<oaktimeline::marker::TimelineMarkerList>(&list) };
		let Some(list_mut) = list_mut else {
			return oaktimeline::error::OAKTIMELINE_E_INVALID;
		};
		list_mut.add_marker(oaktimeline::marker::TimelineMarker::with_time(
			color,
			TimeRange::new(
				Rational::new(in_num as i64, in_den as i64),
				Rational::new(out_num as i64, out_den as i64),
			),
			&name,
		));
		oaktimeline::error::OAKTIMELINE_OK
	}

	/// `oaktimeline_marker_count`.
	pub fn oaktimeline_marker_count(list: CHandle, out_count: *mut c_int) -> c_int {
		if out_count.is_null() {
			return oaktimeline::error::OAKTIMELINE_E_INVALID;
		}
		// SAFETY: marker-list handles box TimelineMarkerList.
		match unsafe { oaktimeline::handle::get::<oaktimeline::marker::TimelineMarkerList>(&list) } {
			Some(l) => {
				// SAFETY: valid out pointer.
				unsafe { *out_count = l.size() as c_int };
				oaktimeline::error::OAKTIMELINE_OK
			}
			None => oaktimeline::error::OAKTIMELINE_E_INVALID,
		}
	}

	/// `oaktimeline_marker_at` — in/out/color + name (two-stage).
	pub fn oaktimeline_marker_at(
		list: CHandle,
		index: c_int,
		in_num: *mut c_int,
		in_den: *mut c_int,
		out_num: *mut c_int,
		out_den: *mut c_int,
		color: *mut c_int,
		name_buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		if index < 0
			|| in_num.is_null()
			|| in_den.is_null()
			|| out_num.is_null()
			|| out_den.is_null()
			|| color.is_null()
		{
			return oaktimeline::error::OAKTIMELINE_E_INVALID;
		}
		// SAFETY: marker-list handles box TimelineMarkerList.
		let l = match unsafe { oaktimeline::handle::get::<oaktimeline::marker::TimelineMarkerList>(&list) } {
			Some(l) => l,
			None => return oaktimeline::error::OAKTIMELINE_E_INVALID,
		};
		let Some(m) = l.at(index as usize) else {
			return oaktimeline::error::OAKTIMELINE_E_NOT_FOUND;
		};
		let range = m.time();
		// SAFETY: valid out pointers.
		unsafe {
			*in_num = range.in_().numerator() as c_int;
			*in_den = range.in_().denominator() as c_int;
			*out_num = range.out().numerator() as c_int;
			*out_den = range.out().denominator() as c_int;
			*color = m.color();
		}
		string_out(m.name(), name_buf, buf_size)
	}

	/// `oaktimeline_marker_add_command`.
	pub fn oaktimeline_marker_add_command(
		list: CHandle,
		in_num: c_int,
		in_den: c_int,
		out_num: c_int,
		out_den: c_int,
		name: *const c_char,
		color: c_int,
	) -> CHandle {
		if in_den == 0 || out_den == 0 {
			return CHandle::null();
		}
		// SAFETY: the caller guarantees a valid NUL-terminated string.
		let name = unsafe { crate::handle::read_cstr(name) };
		let cmd = oaktimeline::marker::MarkerAddCommand::new(
			list,
			TimeRange::new(
				Rational::new(in_num as i64, in_den as i64),
				Rational::new(out_num as i64, out_den as i64),
			),
			&name,
			color,
		);
		box_command(cmd.to_command())
	}

	/// `oaktimeline_marker_remove_at_command`.
	pub fn oaktimeline_marker_remove_at_command(list: CHandle, index: c_int) -> CHandle {
		if index < 0 {
			return CHandle::null();
		}
		let cmd = oaktimeline::marker::MarkerRemoveCommand::new(list, index as usize);
		box_command(cmd.to_command())
	}

	/// `oaktimeline_marker_set_time_command`.
	pub fn oaktimeline_marker_set_time_command(
		list: CHandle,
		index: c_int,
		in_num: c_int,
		in_den: c_int,
		out_num: c_int,
		out_den: c_int,
	) -> CHandle {
		if index < 0 || in_den == 0 || out_den == 0 {
			return CHandle::null();
		}
		let cmd = oaktimeline::marker::MarkerChangeTimeCommand::new(
			list,
			index as usize,
			TimeRange::new(
				Rational::new(in_num as i64, in_den as i64),
				Rational::new(out_num as i64, out_den as i64),
			),
		);
		box_command(cmd.to_command())
	}

	/// `oaktimeline_marker_set_props_command` — color + name changes in
	/// one multi command.
	pub fn oaktimeline_marker_set_props_command(
		list: CHandle,
		index: c_int,
		color: c_int,
		name: *const c_char,
	) -> CHandle {
		if index < 0 {
			return CHandle::null();
		}
		// SAFETY: the caller guarantees a valid NUL-terminated string.
		let name = unsafe { crate::handle::read_cstr(name) };
		let mut multi = UndoCommand::multi();
		multi.multi_add_child(
			oaktimeline::marker::MarkerChangeColorCommand::new(list, index as usize, color)
				.to_command(),
		);
		multi.multi_add_child(
			oaktimeline::marker::MarkerChangeNameCommand::new(list, index as usize, &name)
				.to_command(),
		);
		box_command(multi)
	}

	/// `oaktimeline_marker_list_load` — STUB: the XML surface of the
	/// deleted marker list (reader-backed) has no direct Rust equivalent
	/// in the oaktimeline crate (documented; the engine does not consume
	/// this path).
	pub fn oaktimeline_marker_list_load(_list: CHandle, _reader: CHandle) -> c_int {
		oaktimeline::error::OAKTIMELINE_E_FAILED
	}

	/// `oaktimeline_marker_list_save` — STUB: see
	/// [`oaktimeline_marker_list_load`].
	pub fn oaktimeline_marker_list_save(_list: CHandle, _writer: CHandle) -> c_int {
		oaktimeline::error::OAKTIMELINE_E_FAILED
	}

	// -------------------------------------------------------------------
	// Work area family
	// -------------------------------------------------------------------

	/// `oaktimeline_workarea_create` — a fresh work-area box.
	pub fn oaktimeline_workarea_create() -> CHandle {
		oaktimeline::handle::make_owned(oaktimeline::workarea::TimelineWorkArea::new())
	}

	/// `oaktimeline_workarea_of` — the owner sequence's work area (created
	/// lazily; addref'd copy).
	pub fn oaktimeline_workarea_of(owner: CHandle) -> CHandle {
		let nr = match unsafe { node_ref_of(&owner) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return CHandle::null(),
		};
		let (project, id) = nr;
		let workarea = {
			let mut g = project.lock().unwrap_or_else(|e| e.into_inner());
			let seq = match g
				.graph
				.get_mut(id)
				.and_then(|e| e.behavior.as_any_mut())
				.and_then(|a| a.downcast_mut::<oaknode::sequence::SequenceBehavior>())
			{
				Some(s) => s,
				None => return CHandle::null(),
			};
			if seq.workarea.is_null() {
				seq.workarea = oaktimeline::handle::make_owned(
					oaktimeline::workarea::TimelineWorkArea::new(),
				);
			}
			seq.workarea
		};
		if let Some(addref) = workarea.addref {
			// SAFETY: the handle is live.
			unsafe { addref(workarea.ctx) };
		}
		workarea
	}

	/// `oaktimeline_workarea_free`.
	pub fn oaktimeline_workarea_free(w: *mut CHandle) {
		if w.is_null() {
			return;
		}
		// SAFETY: the caller passes a valid handle pointer.
		let h = unsafe { *w };
		if let Some(release) = h.release {
			// SAFETY: the boxed value's release callback.
			unsafe { release(h.ctx) };
		}
		// SAFETY: the caller passes a valid handle pointer.
		unsafe { *w = CHandle::null() };
	}

	/// `oaktimeline_workarea_set_enabled`.
	pub fn oaktimeline_workarea_set_enabled(w: CHandle, enabled: c_int) -> c_int {
		// SAFETY: work-area handles box TimelineWorkArea.
		match unsafe { oaktimeline::handle::get_mut::<oaktimeline::workarea::TimelineWorkArea>(&w) } {
			Some(wa) => {
				wa.set_enabled(enabled != 0);
				oaktimeline::error::OAKTIMELINE_OK
			}
			None => oaktimeline::error::OAKTIMELINE_E_INVALID,
		}
	}

	/// `oaktimeline_workarea_get` — range + enabled.
	pub fn oaktimeline_workarea_get(
		w: CHandle,
		in_num: *mut c_int,
		in_den: *mut c_int,
		out_num: *mut c_int,
		out_den: *mut c_int,
		enabled: *mut c_int,
	) -> c_int {
		// SAFETY: work-area handles box TimelineWorkArea.
		let wa = match unsafe { oaktimeline::handle::get::<oaktimeline::workarea::TimelineWorkArea>(&w) } {
			Some(wa) => wa,
			None => return oaktimeline::error::OAKTIMELINE_E_INVALID,
		};
		let range = wa.range();
		// Out params may individually be NULL (the header contract); write
		// each only when the caller supplied a target.
		unsafe {
			if !in_num.is_null() {
				*in_num = range.in_().numerator() as c_int;
			}
			if !in_den.is_null() {
				*in_den = range.in_().denominator() as c_int;
			}
			if !out_num.is_null() {
				*out_num = range.out().numerator() as c_int;
			}
			if !out_den.is_null() {
				*out_den = range.out().denominator() as c_int;
			}
			if !enabled.is_null() {
				*enabled = if wa.enabled() { 1 } else { 0 };
			}
		}
		oaktimeline::error::OAKTIMELINE_OK
	}

	/// `oaktimeline_workarea_set_range`.
	pub fn oaktimeline_workarea_set_range(
		w: CHandle,
		in_num: c_int,
		in_den: c_int,
		out_num: c_int,
		out_den: c_int,
	) -> c_int {
		if in_den == 0 || out_den == 0 {
			return oaktimeline::error::OAKTIMELINE_E_INVALID;
		}
		// SAFETY: work-area handles box TimelineWorkArea.
		match unsafe { oaktimeline::handle::get_mut::<oaktimeline::workarea::TimelineWorkArea>(&w) } {
			Some(wa) => {
				wa.set_range(TimeRange::new(
					Rational::new(in_num as i64, in_den as i64),
					Rational::new(out_num as i64, out_den as i64),
				));
				oaktimeline::error::OAKTIMELINE_OK
			}
			None => oaktimeline::error::OAKTIMELINE_E_INVALID,
		}
	}

	/// `oaktimeline_workarea_set_range_command`.
	pub fn oaktimeline_workarea_set_range_command(
		w: CHandle,
		in_num: c_int,
		in_den: c_int,
		out_num: c_int,
		out_den: c_int,
		old_in_num: c_int,
		old_in_den: c_int,
		old_out_num: c_int,
		old_out_den: c_int,
	) -> CHandle {
		if in_den == 0 || out_den == 0 || old_in_den == 0 || old_out_den == 0 {
			return CHandle::null();
		}
		let range = TimeRange::new(
			Rational::new(in_num as i64, in_den as i64),
			Rational::new(out_num as i64, out_den as i64),
		);
		let old_range = TimeRange::new(
			Rational::new(old_in_num as i64, old_in_den as i64),
			Rational::new(old_out_num as i64, old_out_den as i64),
		);
		let cmd = oaktimeline::workarea::WorkareaSetRangeCommand::new_with_old(w, range, old_range);
		box_command(cmd.to_command())
	}

	/// `oaktimeline_workarea_set_enabled_command`.
	pub fn oaktimeline_workarea_set_enabled_command(w: CHandle, enabled: c_int) -> CHandle {
		let cmd =
			oaktimeline::workarea::WorkareaSetEnabledCommand::new(w, enabled != 0);
		box_command(cmd.to_command())
	}

	/// `oaktimeline_workarea_reset` — the documented reset defaults
	/// (`reset_in`/`reset_out`).
	pub fn oaktimeline_workarea_reset(
		in_num: *mut c_int,
		in_den: *mut c_int,
		out_num: *mut c_int,
		out_den: *mut c_int,
	) -> c_int {
		if in_num.is_null() || in_den.is_null() || out_num.is_null() || out_den.is_null() {
			return oaktimeline::error::OAKTIMELINE_E_INVALID;
		}
		let rin = oaktimeline::workarea::reset_in();
		let rout = oaktimeline::workarea::reset_out();
		// SAFETY: valid out pointers.
		unsafe {
			*in_num = rin.numerator() as c_int;
			*in_den = rin.denominator() as c_int;
			*out_num = rout.numerator() as c_int;
			*out_den = rout.denominator() as c_int;
		}
		oaktimeline::error::OAKTIMELINE_OK
	}

	/// `oaktimeline_workarea_load` — STUB: see
	/// [`oaktimeline_marker_list_load`].
	pub fn oaktimeline_workarea_load(_w: CHandle, _reader: CHandle) -> c_int {
		oaktimeline::error::OAKTIMELINE_E_FAILED
	}

	/// `oaktimeline_workarea_save` — STUB: see
	/// [`oaktimeline_marker_list_load`].
	pub fn oaktimeline_workarea_save(_w: CHandle, _writer: CHandle) -> c_int {
		oaktimeline::error::OAKTIMELINE_E_FAILED
	}

	// -------------------------------------------------------------------
	// Timeline edit commands (oakundo UndoCommand values)
	// -------------------------------------------------------------------

	/// `oaktimeline_add_track_command`.
	pub fn oaktimeline_add_track_command(list: CHandle) -> CHandle {
		let timeline = nref!(list);
		box_command(oaktimeline::undogeneral::TimelineAddTrackCommand::new(timeline).to_command())
	}

	/// `oaktimeline_remove_track_command`.
	pub fn oaktimeline_remove_track_command(track: CHandle) -> CHandle {
		let track = nref!(track);
		box_command(oaktimeline::undogeneral::TimelineRemoveTrackCommand::new(track).to_command())
	}

	/// `oaktimeline_place_block_command`.
	pub fn oaktimeline_place_block_command(
		list: CHandle,
		track_index: c_int,
		block: CHandle,
		in_num: i64,
		in_den: i64,
	) -> CHandle {
		if in_den == 0 {
			return CHandle::null();
		}
		let timeline = nref!(list);
		let block = nref!(block);
		let cmd = oaktimeline::undopointer::TrackPlaceBlockCommand::new(
			timeline,
			track_index,
			block,
			Rational::new(in_num, in_den),
		);
		box_command(cmd.to_command())
	}

	/// `oaktimeline_replace_block_with_gap_command`.
	pub fn oaktimeline_replace_block_with_gap_command(track: CHandle, block: CHandle) -> CHandle {
		let track = nref!(track);
		let block = nref!(block);
		let cmd = oaktimeline::undogeneral::TrackReplaceBlockWithGapCommand::new(
			track, block, true,
		);
		box_command(cmd.to_command())
	}

	/// `oaktimeline_move_block_command`.
	pub fn oaktimeline_move_block_command(
		list: CHandle,
		track_index: c_int,
		block: CHandle,
		in_num: i64,
		in_den: i64,
	) -> CHandle {
		if in_den == 0 {
			return CHandle::null();
		}
		let timeline = nref!(list);
		let block = nref!(block);
		let cmd = oaktimeline::undopointer::TrackMoveBlockCommand::new(
			timeline,
			track_index,
			block,
			Rational::new(in_num, in_den),
		);
		box_command(cmd.to_command())
	}

	/// `oaktimeline_trim_command`.
	pub fn oaktimeline_trim_command(
		track: CHandle,
		block: CHandle,
		new_length_num: i64,
		new_length_den: i64,
		mode: c_int,
	) -> CHandle {
		if new_length_den == 0 {
			return CHandle::null();
		}
		let track = nref!(track);
		let block = nref!(block);
		let mode = match oaktimeline::common::MovementMode::from_c_int(mode) {
			Some(m) => m,
			None => return CHandle::null(),
		};
		let cmd = oaktimeline::undopointer::BlockTrimCommand::new(
			track,
			block,
			Rational::new(new_length_num, new_length_den),
			mode,
		);
		box_command(cmd.to_command())
	}

	/// `oaktimeline_split_command` — one split command per block, combined
	/// into a multi command.
	pub fn oaktimeline_split_command(
		blocks: *const CHandle,
		count: c_int,
		point_num: i64,
		point_den: i64,
	) -> CHandle {
		if blocks.is_null() || count <= 0 || point_den == 0 {
			return CHandle::null();
		}
		let mut children = Vec::new();
		for i in 0..count as usize {
			// SAFETY: the caller guarantees `count` valid handles.
			let h = unsafe { *blocks.add(i) };
			let Some(block) = node_ref(h) else {
				return CHandle::null();
			};
			let cmd = oaktimeline::undosplit::BlockSplitCommand::new(
				block,
				Rational::new(point_num, point_den),
			);
			children.push(cmd.to_command());
		}
		let mut multi = UndoCommand::multi();
		for c in children {
			multi.multi_add_child(c);
		}
		box_command(multi)
	}

	/// `oaktimeline_split_preserving_links_command`.
	pub fn oaktimeline_split_preserving_links_command(
		blocks: *const CHandle,
		count: c_int,
		point_nums: *const i64,
		point_dens: *const i64,
		time_count: c_int,
	) -> CHandle {
		if blocks.is_null() || count <= 0 || point_nums.is_null() || point_dens.is_null() || time_count <= 0 {
			return CHandle::null();
		}
		let mut refs = Vec::new();
		for i in 0..count as usize {
			// SAFETY: the caller guarantees `count` valid handles.
			let h = unsafe { *blocks.add(i) };
			let Some(b) = node_ref(h) else {
				return CHandle::null();
			};
			refs.push(b);
		}
		let mut times = Vec::new();
		for i in 0..time_count as usize {
			// SAFETY: the caller guarantees `time_count` valid entries.
			let (n, d) = unsafe { (*point_nums.add(i), *point_dens.add(i)) };
			if d == 0 {
				return CHandle::null();
			}
			times.push(Rational::new(n, d));
		}
		let cmd = oaktimeline::undosplit::BlockSplitPreservingLinksCommand::new(refs, times);
		box_command(cmd.to_command())
	}

	/// `oaktimeline_ripple_delete_gaps_command`.
	pub fn oaktimeline_ripple_delete_gaps_command(
		sequence: CHandle,
		in_nums: *const i64,
		in_dens: *const i64,
		out_nums: *const i64,
		out_dens: *const i64,
		tracks: *const CHandle,
		range_count: c_int,
	) -> CHandle {
		if in_nums.is_null()
			|| in_dens.is_null()
			|| out_nums.is_null()
			|| out_dens.is_null()
			|| tracks.is_null()
			|| range_count <= 0
		{
			return CHandle::null();
		}
		let timeline = nref!(sequence);
		let mut regions = Vec::new();
		for i in 0..range_count as usize {
			// SAFETY: the caller guarantees `range_count` valid entries.
			let (in_n, in_d, out_n, out_d) = unsafe {
				(
					*in_nums.add(i),
					*in_dens.add(i),
					*out_nums.add(i),
					*out_dens.add(i),
				)
			};
			let track = unsafe { *tracks.add(i) };
			if in_d == 0 || out_d == 0 {
				return CHandle::null();
			}
			let Some(track) = node_ref(track) else {
				return CHandle::null();
			};
			regions.push((
				track,
				TimeRange::new(Rational::new(in_n, in_d), Rational::new(out_n, out_d)),
			));
		}
		let cmd =
			oaktimeline::undoripple::TimelineRippleDeleteGapsAtRegionsCommand::new(timeline, regions);
		box_command(cmd.to_command())
	}

	/// `oaktimeline_slide_command`.
	pub fn oaktimeline_slide_command(
		track: CHandle,
		blocks: *const CHandle,
		block_count: c_int,
		in_adjacent: CHandle,
		out_adjacent: CHandle,
		movement_num: i64,
		movement_den: i64,
	) -> CHandle {
		if blocks.is_null() || block_count <= 0 || movement_den == 0 {
			return CHandle::null();
		}
		let track = nref!(track);
		let mut refs = Vec::new();
		for i in 0..block_count as usize {
			// SAFETY: the caller guarantees `block_count` valid handles.
			let h = unsafe { *blocks.add(i) };
			let Some(b) = node_ref(h) else {
				return CHandle::null();
			};
			refs.push(b);
		}
		let in_adjacent = if in_adjacent.is_null() {
			None
		} else {
			node_ref(in_adjacent)
		};
		let out_adjacent = if out_adjacent.is_null() {
			None
		} else {
			node_ref(out_adjacent)
		};
		let cmd = oaktimeline::undopointer::TrackSlideCommand::new(
			track,
			refs,
			in_adjacent,
			out_adjacent,
			Rational::new(movement_num, movement_den),
		);
		box_command(cmd.to_command())
	}

	/// `oaktimeline_ripple_remove_area_command`.
	pub fn oaktimeline_ripple_remove_area_command(
		track: CHandle,
		in_num: i64,
		in_den: i64,
		out_num: i64,
		out_den: i64,
	) -> CHandle {
		if in_den == 0 || out_den == 0 {
			return CHandle::null();
		}
		let track = nref!(track);
		let cmd = oaktimeline::undoripple::TrackRippleRemoveAreaCommand::new(
			track,
			TimeRange::new(Rational::new(in_num, in_den), Rational::new(out_num, out_den)),
		);
		box_command(cmd.to_command())
	}

	/// `oaktimeline_insert_gaps_command`.
	pub fn oaktimeline_insert_gaps_command(
		list: CHandle,
		point_num: i64,
		point_den: i64,
		length_num: i64,
		length_den: i64,
	) -> CHandle {
		if point_den == 0 || length_den == 0 {
			return CHandle::null();
		}
		let track_list = nref!(list);
		let cmd = oaktimeline::undogeneral::TrackListInsertGaps::new(
			track_list,
			Rational::new(point_num, point_den),
			Rational::new(length_num, length_den),
		);
		box_command(cmd.to_command())
	}
}

// ===========================================================================
// audio — direct Rust shims over the oakaudio crate
// ===========================================================================

/// oakaudio bridge replacements: direct Rust calls into the `oakaudio`
/// crate (single-lib unification). Same names/signatures as the deleted
/// bridge functions; the manager CHandle is a borrow-only validity token
/// (the module owns the process-wide singleton), the processor CHandle
/// boxes an owned `oakaudio::processor::Processor` with a release
/// callback. The `oakcore_audioparams_*` C ABI (opaque `AudioParams`
/// handles in the frozen engine contract) is implemented here, inside the
/// dylib — the accessors were host-provided (C++ liboakcore, later Rust
/// mock shims in the app/cli/test binaries) until M12 P5 folded them in
/// so the cdylib carries no undefined imports.
pub mod audio {
	use std::ffi::{c_char, c_double, c_int, c_void, CStr};

	use oakaudio::error::{
		OAKAUDIO_E_FAILED, OAKAUDIO_E_INVALID, OAKAUDIO_E_NOT_FOUND, OAKAUDIO_E_STATE, OAKAUDIO_OK,
	};
	use oakaudio::manager::ManagerInner;
	use oakaudio::params::{sample_format_from_i32, AudioParams};
	use oakaudio::processor::Processor;
	use oakaudio::synchronizer::{self, SourceClip as ModuleSourceClip};
	use oakaudio::waveform;
	use oakaudio::waveformsync;
	use oakcore_rs::Rational;

	use crate::handle::CHandle;

	pub use crate::pods::{
		AudioEncodingParams as EncodingParams, MinMax, OffsetResult, SourceClip,
		StretchOffsetResult,
	};

	/// The liboakcore `AudioParams` object layout
	/// (`oakcore_audioparams.h`): sample rate, ffmpeg channel-layout mask,
	/// sample format, stream index, duration and time base. Mirrors
	/// `olive::core::AudioParams` (and the app/cli mock shims it replaces),
	/// so the engine contract's borrowed/owned `AudioParams*` handles
	/// round-trip unchanged.
	#[repr(C)]
	struct HostAudioParams {
		sample_rate: c_int,
		channel_layout: u64,
		format: c_int,
		stream_index: c_int,
		duration: i64,
		time_base_num: c_int,
		time_base_den: c_int,
	}

	/// `oakcore_audioparams_create` — new owned audio params (release with
	/// `oakcore_audioparams_free`). The time base defaults to 1/sample_rate,
	/// exactly like the liboakcore constructor.
	#[no_mangle]
	pub extern "C" fn oakcore_audioparams_create(
		sample_rate: c_int,
		channel_layout: u64,
		format: c_int,
	) -> *mut c_void {
		let den = if sample_rate > 0 { sample_rate } else { 1 };
		Box::into_raw(Box::new(HostAudioParams {
			sample_rate,
			channel_layout,
			format,
			stream_index: 0,
			duration: 0,
			time_base_num: 1,
			time_base_den: den,
		})) as *mut c_void
	}

	/// `oakcore_audioparams_free` — NULL no-op.
	///
	/// # Safety
	/// `params` must come from [`oakcore_audioparams_create`] or be NULL.
	#[no_mangle]
	pub extern "C" fn oakcore_audioparams_free(params: *mut c_void) {
		if params.is_null() {
			return;
		}
		// SAFETY: produced by `oakcore_audioparams_create`; we hold the only
		// reference after the box is dropped.
		unsafe { drop(Box::from_raw(params as *mut HostAudioParams)) };
	}

	/// `oakcore_audioparams_sample_rate` — 0 for NULL (liboakcore contract).
	///
	/// # Safety
	/// `params` must come from [`oakcore_audioparams_create`] or be NULL.
	#[no_mangle]
	pub extern "C" fn oakcore_audioparams_sample_rate(params: *const c_void) -> c_int {
		if params.is_null() {
			return 0;
		}
		// SAFETY: contract above.
		unsafe { (*(params as *const HostAudioParams)).sample_rate }
	}

	/// `oakcore_audioparams_channel_layout` — 0 for NULL.
	///
	/// # Safety
	/// `params` must come from [`oakcore_audioparams_create`] or be NULL.
	#[no_mangle]
	pub extern "C" fn oakcore_audioparams_channel_layout(params: *const c_void) -> u64 {
		if params.is_null() {
			return 0;
		}
		// SAFETY: contract above.
		unsafe { (*(params as *const HostAudioParams)).channel_layout }
	}

	/// `oakcore_audioparams_format` — 0 for NULL.
	///
	/// # Safety
	/// `params` must come from [`oakcore_audioparams_create`] or be NULL.
	#[no_mangle]
	pub extern "C" fn oakcore_audioparams_format(params: *const c_void) -> c_int {
		if params.is_null() {
			return 0;
		}
		// SAFETY: contract above.
		unsafe { (*(params as *const HostAudioParams)).format }
	}

	/// `oakcore_audioparams_set_time_base` — NULL no-op.
	///
	/// # Safety
	/// `params` must come from [`oakcore_audioparams_create`] or be NULL.
	#[no_mangle]
	pub extern "C" fn oakcore_audioparams_set_time_base(
		params: *mut c_void,
		num: c_int,
		den: c_int,
	) {
		if params.is_null() {
			return;
		}
		// SAFETY: contract above.
		unsafe {
			let p = &mut *(params as *mut HostAudioParams);
			p.time_base_num = num;
			p.time_base_den = den;
		}
	}

	/// Map an oakaudio `Box<dyn Error>` result to its public module code
	/// (unknown downstream errors collapse to `OAKAUDIO_E_FAILED`, the same
	/// fallback the deleted ffi layer used).
	fn audio_code(e: &(dyn std::error::Error + 'static)) -> c_int {
		match e.downcast_ref::<oakaudio::error::Error>() {
			Some(audio_err) => audio_err.code(),
			None => OAKAUDIO_E_FAILED,
		}
	}

	/// Write `s` into `buf` NUL-terminated with truncation (the deleted
	/// module ffi's `write_error` contract; NULL/zero-sized buffers are
	/// documented no-ops).
	///
	/// # Safety
	/// `buf` must point to `buf_size` writable bytes when non-NULL and
	/// `buf_size > 0`.
	unsafe fn write_error(s: &str, buf: *mut c_char, buf_size: c_int) {
		if buf.is_null() || buf_size <= 0 {
			return;
		}
		// SAFETY: contract above.
		unsafe {
			let bytes = s.as_bytes();
			let n = bytes.len().min((buf_size - 1) as usize);
			std::ptr::copy_nonoverlapping(bytes.as_ptr(), buf as *mut u8, n);
			*(buf as *mut u8).add(n) = 0;
		}
	}

	/// Map a failure's message into `error_buf` when it carries one (the
	/// deleted ffi surfaced `Error::Failed` context strings on the failure
	/// path only).
	unsafe fn write_failed_error(
		e: &(dyn std::error::Error + 'static),
		error_buf: *mut c_char,
		error_buf_size: c_int,
	) {
		if let Some(oakaudio::error::Error::Failed(msg)) =
			e.downcast_ref::<oakaudio::error::Error>()
		{
			// SAFETY: forwarded to `write_error`'s contract.
			unsafe { write_error(msg, error_buf, error_buf_size) };
		}
	}

	/// Non-null sentinel `ctx` for the borrow-only manager handle token.
	/// Never dereferenced; the engine's `oakengine_audio_manager_handle`
	/// hands it to the host as an opaque event-subscription token.
	fn manager_token() -> *mut c_void {
		std::ptr::NonNull::<u8>::dangling().as_ptr() as *mut c_void
	}

	/// Lock the process-wide manager singleton (`None` when absent) and
	/// reject empty handle tokens with the documented state error.
	fn with_manager(self_: CHandle) -> Result<std::sync::MutexGuard<'static, ManagerInner>, c_int> {
		if self_.is_null() {
			return Err(OAKAUDIO_E_STATE);
		}
		oakaudio::manager::instance().ok_or(OAKAUDIO_E_STATE)
	}

	/// Borrow-only token of the manager singleton (empty when none).
	pub fn oakaudio_manager_instance() -> CHandle {
		match oakaudio::manager::instance() {
			Some(_) => CHandle {
				ctx: manager_token(),
				addref: None,
				release: None,
				abi_version: 0,
			},
			None => CHandle::null(),
		}
	}

	/// Create the process-wide manager singleton (no-op when it exists).
	pub fn oakaudio_manager_create_instance() -> c_int {
		match ManagerInner::create_instance() {
			Ok(()) => OAKAUDIO_OK,
			Err(e) => audio_code(&*e),
		}
	}

	/// Destroy the manager singleton (no-op when none exists).
	pub fn oakaudio_manager_destroy_instance() {
		ManagerInner::destroy_instance();
	}

	/// Borrowed singleton handles carry no release callback; free just
	/// nulls the token.
	pub fn oakaudio_manager_free(_self: *mut CHandle) {
		if _self.is_null() {
			return;
		}
		// SAFETY: the caller passes a valid handle pointer.
		unsafe { *_self = CHandle::null() };
	}

	/// Bytes between output-notify pulses.
	pub fn oakaudio_manager_set_output_notify_interval(_self: CHandle, _bytes: i64) -> c_int {
		match with_manager(_self).and_then(|m| m.set_output_notify_interval(_bytes).map_err(|e| audio_code(&*e))) {
			Ok(()) => OAKAUDIO_OK,
			Err(code) => code,
		}
	}

	/// Push a block of samples to the output device.
	#[allow(clippy::too_many_arguments)]
	pub fn oakaudio_manager_push_to_output(
		_self: CHandle,
		_rate: c_int,
		_layout: u64,
		_format: c_int,
		_samples: *const c_char,
		_samples_size: i64,
		_error_buf: *mut c_char,
		_error_buf_size: c_int,
	) -> c_int {
		// CPP-PARITY (deleted ffi): `rate <= 0 || !samples ||
		// samples_size < 0` is invalid; an unrepresentable sample format
		// is additionally rejected.
		if _rate <= 0 || _samples.is_null() || _samples_size < 0 {
			return OAKAUDIO_E_INVALID;
		}
		let format = sample_format_from_i32(_format);
		if format == oakaudio::params::SampleFormat::Invalid {
			return OAKAUDIO_E_INVALID;
		}
		let mut guard = match with_manager(_self) {
			Ok(g) => g,
			Err(code) => return code,
		};
		// SAFETY: the caller guarantees `_samples` points to
		// `_samples_size` readable bytes (size 0 reads nothing).
		let samples = unsafe {
			if _samples_size <= 0 {
				&[][..]
			} else {
				std::slice::from_raw_parts(_samples as *const u8, _samples_size as usize)
			}
		};
		let params = AudioParams {
			sample_rate: _rate,
			channel_layout: _layout,
			format,
		};
		match guard.push_to_output(params, samples, &mut []) {
			Ok(()) => OAKAUDIO_OK,
			Err(e) => {
				// SAFETY: NULL/empty error buffers are documented no-ops.
				unsafe { write_failed_error(&*e, _error_buf, _error_buf_size) };
				audio_code(&*e)
			}
		}
	}

	/// Discard buffered output.
	pub fn oakaudio_manager_clear_buffered_output(_self: CHandle) -> c_int {
		match with_manager(_self).and_then(|m| m.clear_buffered_output().map_err(|e| audio_code(&*e))) {
			Ok(()) => OAKAUDIO_OK,
			Err(code) => code,
		}
	}

	/// Stop the output stream.
	pub fn oakaudio_manager_stop_output(_self: CHandle) -> c_int {
		match with_manager(_self).and_then(|mut m| m.stop_output().map_err(|e| audio_code(&*e))) {
			Ok(()) => OAKAUDIO_OK,
			Err(code) => code,
		}
	}

	/// Restart the output clock at zero.
	pub fn oakaudio_manager_reset_output_clock(_self: CHandle) -> c_int {
		match with_manager(_self).and_then(|m| m.reset_output_clock().map_err(|e| audio_code(&*e))) {
			Ok(()) => OAKAUDIO_OK,
			Err(code) => code,
		}
	}

	/// Current output device index (`paNoDevice` = -1) or a negative error.
	pub fn oakaudio_manager_get_output_device(_self: CHandle) -> c_int {
		match with_manager(_self).and_then(|m| m.get_output_device().map_err(|e| audio_code(&*e))) {
			Ok(device) => device,
			Err(code) => code,
		}
	}

	/// Set the output device index.
	pub fn oakaudio_manager_set_output_device(_self: CHandle, _device: c_int) -> c_int {
		match with_manager(_self).and_then(|mut m| m.set_output_device(_device).map_err(|e| audio_code(&*e))) {
			Ok(()) => OAKAUDIO_OK,
			Err(code) => code,
		}
	}

	/// Current input device index or a negative error code.
	pub fn oakaudio_manager_get_input_device(_self: CHandle) -> c_int {
		match with_manager(_self).and_then(|m| m.get_input_device().map_err(|e| audio_code(&*e))) {
			Ok(device) => device,
			Err(code) => code,
		}
	}

	/// Set the input device index.
	pub fn oakaudio_manager_set_input_device(_self: CHandle, _device: c_int) -> c_int {
		match with_manager(_self).and_then(|mut m| m.set_input_device(_device).map_err(|e| audio_code(&*e))) {
			Ok(()) => OAKAUDIO_OK,
			Err(code) => code,
		}
	}

	/// The number of host output devices (enumeration order == the device
	/// index `set_output_device` takes). Needs no manager instance.
	pub fn oakaudio_output_device_count() -> c_int {
		oakaudio::manager::output_device_names().len() as c_int
	}

	/// The number of host input devices (see [`oakaudio_output_device_count`]).
	pub fn oakaudio_input_device_count() -> c_int {
		oakaudio::manager::input_device_names().len() as c_int
	}

	/// The name of output device `index` (two-stage buf/size; reports the
	/// required size INCLUDING the NUL, the module convention the facade
	/// converts with `string_result`).
	///
	/// # Safety
	/// `buf` must point to `buf_size` writable bytes when non-NULL.
	pub unsafe fn oakaudio_output_device_name(
		index: c_int,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		unsafe { device_name_result(oakaudio::manager::output_device_names(), index, buf, buf_size) }
	}

	/// The name of input device `index` (see [`oakaudio_output_device_name`]).
	///
	/// # Safety
	/// `buf` must point to `buf_size` writable bytes when non-NULL.
	pub unsafe fn oakaudio_input_device_name(
		index: c_int,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		unsafe { device_name_result(oakaudio::manager::input_device_names(), index, buf, buf_size) }
	}

	/// Shared two-stage string write for the device-name getters: reports
	/// `len + 1` (the required buffer size including the NUL), writes the
	/// NUL-terminated name when it fits. `OAKAUDIO_E_NOT_FOUND` for an
	/// out-of-range index.
	unsafe fn device_name_result(
		names: Vec<String>,
		index: c_int,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		let Some(name) = names.get(index.max(0) as usize).filter(|_| index >= 0) else {
			return OAKAUDIO_E_NOT_FOUND;
		};
		// SAFETY: `buf` points to `buf_size` writable bytes when non-NULL.
		unsafe {
			if !buf.is_null() && buf_size > 0 {
				let copy = name.len().min((buf_size as usize).saturating_sub(1));
				std::ptr::copy_nonoverlapping(name.as_ptr(), buf as *mut u8, copy);
				*buf.add(copy) = 0;
			}
		}
		name.len() as c_int + 1
	}

	/// Close the output stream and reset the playback state.
	pub fn oakaudio_manager_hard_reset(_self: CHandle) -> c_int {
		match with_manager(_self).and_then(|mut m| m.hard_reset().map_err(|e| audio_code(&*e))) {
			Ok(()) => OAKAUDIO_OK,
			Err(code) => code,
		}
	}

	/// Start recording the input device through the oakcodec encoder.
	pub fn oakaudio_manager_start_recording(
		_self: CHandle,
		_params: *const EncodingParams,
		_error_buf: *mut c_char,
		_error_buf_size: c_int,
	) -> c_int {
		// CPP-PARITY (deleted ffi): `!params || !params->audio_enabled`
		// is invalid; the error string is written on the invalid path too
		// so callers can always diagnose a failed start.
		if _params.is_null() {
			// SAFETY: NULL/empty error buffers are documented no-ops.
			unsafe { write_error("invalid recording parameters", _error_buf, _error_buf_size) };
			return OAKAUDIO_E_INVALID;
		}
		// SAFETY: the caller guarantees `_params` points to a live
		// `EncodingParams` POD for the duration of the call.
		let params = unsafe { &*_params };
		if params.audio_enabled == 0 {
			// SAFETY: NULL/empty error buffers are documented no-ops.
			unsafe { write_error("invalid recording parameters", _error_buf, _error_buf_size) };
			return OAKAUDIO_E_INVALID;
		}
		let mut guard = match with_manager(_self) {
			Ok(g) => g,
			Err(code) => return code,
		};
		match guard.start_recording(params, &mut []) {
			Ok(()) => OAKAUDIO_OK,
			Err(e) => {
				// SAFETY: NULL/empty error buffers are documented no-ops.
				unsafe { write_failed_error(&*e, _error_buf, _error_buf_size) };
				audio_code(&*e)
			}
		}
	}

	/// Stop recording.
	pub fn oakaudio_manager_stop_recording(_self: CHandle) -> c_int {
		match with_manager(_self).and_then(|mut m| m.stop_recording().map_err(|e| audio_code(&*e))) {
			Ok(()) => OAKAUDIO_OK,
			Err(code) => code,
		}
	}

	/// Seconds of audio consumed by the output device since the last reset.
	pub fn oakaudio_manager_seconds(_self: CHandle, _out: *mut c_double) -> c_int {
		let guard = match with_manager(_self) {
			Ok(g) => g,
			Err(code) => return code,
		};
		if _out.is_null() {
			return OAKAUDIO_E_INVALID;
		}
		// SAFETY: the caller guarantees `_out` is a writable f64.
		let out = unsafe { &mut *_out };
		match guard.seconds(out) {
			Ok(()) => OAKAUDIO_OK,
			Err(e) => audio_code(&*e),
		}
	}

	/// Per-channel peak levels of the buffered, not-yet-consumed output.
	/// Returns the channel count (>= 0) or a negative error code.
	pub fn oakaudio_manager_output_levels(_self: CHandle, _peaks: *mut f32, _capacity: c_int) -> c_int {
		let guard = match with_manager(_self) {
			Ok(g) => g,
			Err(code) => return code,
		};
		if _peaks.is_null() || _capacity <= 0 {
			return OAKAUDIO_E_INVALID;
		}
		// SAFETY: the caller guarantees `_peaks` holds `_capacity` f32s.
		let peaks = unsafe { std::slice::from_raw_parts_mut(_peaks, _capacity as usize) };
		match guard.output_levels(peaks) {
			Ok(n) => n,
			Err(e) => audio_code(&*e),
		}
	}

	// ---- Processor (owned, refcounted box behind a CHandle) ----------------

	/// Test-only leak counter replacing the deleted module C ABI's
	/// `oakaudio_debug_alive_count` (the oakaudio crate dropped its debug
	/// counter together with its ffi layer). Counts live boxed processors
	/// created through [`oakaudio_processor_init`] and released through
	/// [`release_processor`]; the production cdylib has no counter.
	#[cfg(test)]
	static PROCESSOR_ALIVE: std::sync::atomic::AtomicI32 = std::sync::atomic::AtomicI32::new(0);

	/// Test-only: current live-processor count (see [`PROCESSOR_ALIVE`]).
	#[cfg(test)]
	pub fn oakaudio_debug_alive_count() -> c_int {
		PROCESSOR_ALIVE.load(std::sync::atomic::Ordering::SeqCst)
	}

	/// Release callback for a boxed `oakaudio::processor::Processor`.
	unsafe extern "C" fn release_processor(ctx: *mut c_void) {
		if !ctx.is_null() {
			// SAFETY: `ctx` was produced by `oakaudio_processor_init` and
			// this callback runs exactly once.
			unsafe { drop(Box::from_raw(ctx as *mut Processor)) };
			#[cfg(test)]
			PROCESSOR_ALIVE.fetch_sub(1, std::sync::atomic::Ordering::SeqCst);
		}
	}

	/// Create a closed audio processor (boxed behind a CHandle).
	pub fn oakaudio_processor_init() -> CHandle {
		let p = Box::new(Processor::init());
		#[cfg(test)]
		PROCESSOR_ALIVE.fetch_add(1, std::sync::atomic::Ordering::SeqCst);
		CHandle {
			ctx: Box::into_raw(p) as *mut c_void,
			addref: None,
			release: Some(release_processor),
			abi_version: 0,
		}
	}

	/// Release the boxed processor through the handle's release callback.
	pub fn oakaudio_processor_free(_self: *mut CHandle) {
		if _self.is_null() {
			return;
		}
		// SAFETY: the caller passes a valid handle pointer.
		let h = unsafe { &mut *_self };
		if let Some(release) = h.release {
			// SAFETY: `release` is the boxed type's release callback.
			unsafe { release(h.ctx) };
		}
		*h = CHandle::null();
	}

	/// Open the processor's conversion graph.
	#[allow(clippy::too_many_arguments)]
	pub fn oakaudio_processor_open(
		_self: CHandle,
		_in_rate: c_int,
		_in_layout: u64,
		_in_format: c_int,
		_out_rate: c_int,
		_out_layout: u64,
		_out_format: c_int,
		_speed: c_double,
	) -> c_int {
		if _self.is_null() {
			return OAKAUDIO_E_STATE;
		}
		// SAFETY: `_self.ctx` was produced by `oakaudio_processor_init`.
		let p = unsafe { &*(_self.ctx as *const Processor) };
		let from = AudioParams {
			sample_rate: _in_rate,
			channel_layout: _in_layout,
			format: sample_format_from_i32(_in_format),
		};
		let to = AudioParams {
			sample_rate: _out_rate,
			channel_layout: _out_layout,
			format: sample_format_from_i32(_out_format),
		};
		match p.open(from, to, _speed) {
			Ok(()) => OAKAUDIO_OK,
			Err(e) => audio_code(&*e),
		}
	}

	/// Close the processor's conversion graph (safe when closed).
	pub fn oakaudio_processor_close(_self: CHandle) -> c_int {
		if _self.is_null() {
			return OAKAUDIO_E_STATE;
		}
		// SAFETY: see `oakaudio_processor_open`.
		let p = unsafe { &*(_self.ctx as *const Processor) };
		match p.close() {
			Ok(()) => OAKAUDIO_OK,
			Err(e) => audio_code(&*e),
		}
	}

	/// 1 when open, 0 when closed.
	pub fn oakaudio_processor_is_open(_self: CHandle) -> c_int {
		if _self.is_null() {
			return 0;
		}
		// SAFETY: see `oakaudio_processor_open`.
		let p = unsafe { &*(_self.ctx as *const Processor) };
		match p.is_open() {
			Ok(open) => open as c_int,
			Err(_) => 0,
		}
	}

	// ---- Stateless synchronization helpers ---------------------------------

	/// Rebuild the per-window validity mask the C ABI carried as `u8`s.
	/// A NULL pointer (or an impossible length) yields an empty mask —
	/// which the module treats as "all windows valid".
	///
	/// # Safety
	/// `mask` must point to `window_count` readable bytes when non-NULL.
	unsafe fn window_mask(mask: *const u8, sample_len: c_int, window_samples: u64) -> Vec<bool> {
		if mask.is_null() || window_samples == 0 || sample_len <= 0 {
			return Vec::new();
		}
		// SAFETY: contract above; the mask has one byte per RMS window.
		let window_count = (sample_len as usize).div_ceil(window_samples as usize);
		unsafe { std::slice::from_raw_parts(mask, window_count) }
			.iter()
			.map(|&b| b != 0)
			.collect()
	}

	/// Estimate the sample offset between two RMS envelopes.
	#[allow(clippy::too_many_arguments)]
	pub fn oakaudio_sync_estimate_envelope_offset(
		_reference: *const c_double,
		_reference_len: c_int,
		_candidate: *const c_double,
		_candidate_len: c_int,
		_reference_valid: *const u8,
		_candidate_valid: *const u8,
		_window_samples: u64,
		_max_offset_windows: i64,
		_out: *mut OffsetResult,
	) -> c_int {
		// CPP-PARITY (deleted ffi sync.cpp:93): NULL arrays and
		// non-positive lengths are invalid; masks may be NULL (all windows
		// valid).
		if _out.is_null()
			|| _reference.is_null()
			|| _candidate.is_null()
			|| _reference_len <= 0
			|| _candidate_len <= 0
			|| _window_samples == 0
			|| _max_offset_windows < 0
		{
			return OAKAUDIO_E_INVALID;
		}
		// SAFETY: the caller guarantees the sample arrays hold their
		// lengths; the mask slices are built from the same contract.
		let reference = unsafe { std::slice::from_raw_parts(_reference, _reference_len as usize) };
		let candidate = unsafe { std::slice::from_raw_parts(_candidate, _candidate_len as usize) };
		let reference_valid = unsafe { window_mask(_reference_valid, _reference_len, _window_samples) };
		let candidate_valid = unsafe { window_mask(_candidate_valid, _candidate_len, _window_samples) };
		let result = waveformsync::estimate_envelope_offset_valid(
			reference,
			candidate,
			&reference_valid,
			&candidate_valid,
			_window_samples as usize,
			_max_offset_windows,
		);
		// SAFETY: `_out` is a writable `OffsetResult` POD (checked above).
		unsafe {
			(*_out).offset_samples = result.offset_samples;
			(*_out).confidence = result.confidence;
			(*_out).valid = result.valid as c_int;
		}
		OAKAUDIO_OK
	}

	/// Estimate a playback-rate change plus offset.
	#[allow(clippy::too_many_arguments)]
	pub fn oakaudio_sync_estimate_stretch_and_offset(
		_reference: *const c_double,
		_reference_len: c_int,
		_candidate: *const c_double,
		_candidate_len: c_int,
		_reference_valid: *const u8,
		_candidate_valid: *const u8,
		_window_samples: u64,
		_max_offset_windows: i64,
		_min_rate: c_double,
		_max_rate: c_double,
		_rate_step: c_double,
		_out: *mut StretchOffsetResult,
	) -> c_int {
		// CPP-PARITY (deleted ffi sync.cpp:123).
		if _out.is_null()
			|| _reference.is_null()
			|| _candidate.is_null()
			|| _reference_len <= 0
			|| _candidate_len <= 0
			|| _window_samples == 0
			|| _max_offset_windows < 0
			|| _min_rate <= 0.0
			|| _max_rate < _min_rate
			|| _rate_step <= 0.0
		{
			return OAKAUDIO_E_INVALID;
		}
		// SAFETY: see `oakaudio_sync_estimate_envelope_offset`.
		let reference = unsafe { std::slice::from_raw_parts(_reference, _reference_len as usize) };
		let candidate = unsafe { std::slice::from_raw_parts(_candidate, _candidate_len as usize) };
		let reference_valid = unsafe { window_mask(_reference_valid, _reference_len, _window_samples) };
		let candidate_valid = unsafe { window_mask(_candidate_valid, _candidate_len, _window_samples) };
		let result = waveformsync::estimate_stretch_and_offset(
			reference,
			candidate,
			&reference_valid,
			&candidate_valid,
			_window_samples as usize,
			_max_offset_windows,
			_min_rate,
			_max_rate,
			_rate_step,
		);
		// SAFETY: `_out` is a writable `StretchOffsetResult` POD.
		unsafe {
			(*_out).rate = result.rate;
			(*_out).offset_samples = result.offset_samples;
			(*_out).confidence = result.confidence;
			(*_out).valid = result.valid as c_int;
		}
		OAKAUDIO_OK
	}

	/// Rebuild a module `SourceClip` from the engine's POD mirror.
	fn module_source_clip(pod: &SourceClip) -> ModuleSourceClip {
		ModuleSourceClip {
			source_start_time: Rational::new(pod.source_start_time_num, pod.source_start_time_den),
			media_in: Rational::new(pod.media_in_num, pod.media_in_den),
			has_source_start_time: pod.has_source_start_time != 0,
		}
	}

	/// Timeline placement from source timecodes.
	pub fn oakaudio_sync_place_by_source_time(
		_reference: *const SourceClip,
		_candidate: *const SourceClip,
		_reference_timeline_in_num: i64,
		_reference_timeline_in_den: i64,
		_out_num: *mut i64,
		_out_den: *mut i64,
		_out_valid: *mut c_int,
	) -> c_int {
		// CPP-PARITY (deleted ffi sync.cpp:153): every denominator must be
		// non-zero.
		if _reference.is_null() || _candidate.is_null() {
			return OAKAUDIO_E_INVALID;
		}
		// SAFETY: the caller guarantees valid `SourceClip` PODs.
		let (reference, candidate) = unsafe { (&*_reference, &*_candidate) };
		if _out_num.is_null()
			|| _out_den.is_null()
			|| _out_valid.is_null()
			|| reference.source_start_time_den == 0
			|| reference.media_in_den == 0
			|| candidate.source_start_time_den == 0
			|| candidate.media_in_den == 0
			|| _reference_timeline_in_den == 0
		{
			return OAKAUDIO_E_INVALID;
		}
		let reference_clip = module_source_clip(reference);
		let candidate_clip = module_source_clip(candidate);
		let placement = synchronizer::place_by_source_time(
			&reference_clip,
			&candidate_clip,
			Rational::new(_reference_timeline_in_num, _reference_timeline_in_den),
		);
		// SAFETY: the caller guarantees writable out parameters.
		unsafe {
			*_out_num = placement.timeline_in.numerator();
			*_out_den = placement.timeline_in.denominator();
			*_out_valid = placement.valid as c_int;
		}
		OAKAUDIO_OK
	}

	/// Timeline placement from a measured waveform offset.
	pub fn oakaudio_sync_place_by_waveform_offset(
		_reference_timeline_in_num: i64,
		_reference_timeline_in_den: i64,
		_candidate_offset_samples: i64,
		_sample_rate: c_int,
		_out_num: *mut i64,
		_out_den: *mut i64,
		_out_valid: *mut c_int,
	) -> c_int {
		// CPP-PARITY (deleted ffi sync.cpp:191).
		if _out_num.is_null()
			|| _out_den.is_null()
			|| _out_valid.is_null()
			|| _reference_timeline_in_den == 0
		{
			return OAKAUDIO_E_INVALID;
		}
		let placement = synchronizer::place_by_waveform_offset(
			Rational::new(_reference_timeline_in_num, _reference_timeline_in_den),
			_candidate_offset_samples,
			_sample_rate,
		);
		// SAFETY: the caller guarantees writable out parameters.
		unsafe {
			*_out_num = placement.timeline_in.numerator();
			*_out_den = placement.timeline_in.denominator();
			*_out_valid = placement.valid as c_int;
		}
		OAKAUDIO_OK
	}

	/// Two-stage whole-file min/max waveform extraction. The module's
	/// [`waveform::extract`] has no two-stage entry point, so the shim
	/// emulates it: the decode outcome is cached per (filename, stream,
	/// samples-per-point) so the count pass and the data pass decode ONCE
	/// (the deleted ffi cached for the same reason — the FFmpeg decoder is
	/// not safe against back-to-back sessions in the same process). A
	/// missing file reports `OAKAUDIO_E_NOT_FOUND` (the deleted bridge's
	/// codec-probe behavior) before any decode is attempted.
	pub fn oakaudio_waveform_extract(
		_filename: *const c_char,
		_stream_index: c_int,
		_samples_per_point: c_int,
		_out_pairs: *mut MinMax,
		_capacity_points: c_int,
		_out_channel_count: *mut c_int,
	) -> c_int {
		// CPP-PARITY (deleted ffi waveform.cpp:409).
		if _filename.is_null() || _stream_index < 0 || _samples_per_point <= 0 || _capacity_points < 0 {
			return OAKAUDIO_E_INVALID;
		}
		// SAFETY: the caller guarantees a valid NUL-terminated path.
		let filename = unsafe { CStr::from_ptr(_filename) };
		let key = (
			filename.to_string_lossy().into_owned(),
			_stream_index,
			_samples_per_point,
		);
		// The deleted bridge surfaced the codec probe's NOT_FOUND for a
		// missing file; the direct module API reports a generic failure.
		use std::os::unix::ffi::OsStrExt;
		if std::fs::metadata(std::path::Path::new(std::ffi::OsStr::from_bytes(
			filename.to_bytes(),
		)))
		.is_err()
		{
			return OAKAUDIO_E_NOT_FOUND;
		}
		let outcome = {
			static CACHE: std::sync::OnceLock<
				std::sync::Mutex<std::collections::HashMap<
					(String, c_int, c_int),
					waveform::ExtractOutcome,
				>>,
			> = std::sync::OnceLock::new();
			let cache = CACHE.get_or_init(|| std::sync::Mutex::new(std::collections::HashMap::new()));
			let mut guard = cache.lock().unwrap_or_else(|e| e.into_inner());
			if let Some(cached) = guard.get(&key) {
				cached.clone()
			} else {
				let outcome = match waveform::extract(filename, _stream_index, _samples_per_point) {
					Ok(outcome) => outcome,
					Err(e) => return audio_code(e.as_ref()),
				};
				guard.insert(key, outcome.clone());
				outcome
			}
		};
		let channels = outcome.channels.max(0) as usize;
		if !_out_channel_count.is_null() {
			// SAFETY: the caller guarantees a writable c_int.
			unsafe { *_out_channel_count = outcome.channels };
		}
		let points_needed = if channels == 0 {
			0
		} else {
			outcome.points.len() / channels
		};
		// The data pass writes only when the capacity covers the point
		// count (capacity is in points, not pairs).
		if _out_pairs.is_null() || (_capacity_points as usize) < points_needed {
			return points_needed as c_int;
		}
		// SAFETY: `_out_pairs` holds `points_needed * channels` entries
		// (capacity checked above).
		unsafe {
			std::ptr::copy_nonoverlapping(
				outcome.points.as_ptr() as *const MinMax,
				_out_pairs,
				points_needed * channels,
			);
		}
		points_needed as c_int
	}
}

// ===========================================================================
// plugin — direct Rust shims over the oakplugin crate
// ===========================================================================

/// oakplugin bridge replacements: direct Rust calls into the `oakplugin`
/// crate (single-lib unification). Same names/signatures as the deleted
/// bridge functions; the OFX host singleton is [`oakplugin::host::Host::global`].
pub mod plugin {
	use std::ffi::{c_char, c_int, CStr};
	use std::path::Path;

	use oakplugin::error::{OAKPLUGIN_E_INVALID, OAKPLUGIN_E_NOT_FOUND, OAKPLUGIN_OK};
	use oakplugin::host::Host;

	use crate::handle::read_cstr;

	/// Standard two-stage getter copy: copy only when the buffer is large
	/// enough (never truncates); always return the required size incl. NUL.
	fn copy_string(value: &str, buf: *mut c_char, buf_size: c_int) -> c_int {
		let required = (value.len() + 1) as c_int;
		if !buf.is_null() && buf_size >= required {
			// SAFETY: the caller guarantees `buf` holds `buf_size` bytes.
			unsafe {
				std::ptr::copy_nonoverlapping(value.as_ptr() as *const c_char, buf, value.len());
				*buf.add(value.len()) = 0;
			}
		}
		required
	}

	/// Whether `(buf, buf_size)` is a valid two-stage getter output.
	fn is_valid_string_out(buf: *mut c_char, buf_size: c_int) -> bool {
		buf_size >= 0 && (buf_size == 0 || !buf.is_null())
	}

	/// Scan the given plugin bundle directories (oakplugin_host_scan).
	pub fn oakplugin_host_scan(_bundle_dirs: *const *const c_char, _dir_count: c_int) -> c_int {
		if _bundle_dirs.is_null() || _dir_count < 0 {
			return OAKPLUGIN_E_INVALID;
		}
		let host = Host::global();
		for i in 0.._dir_count as usize {
			// SAFETY: the caller guarantees `_dir_count` valid directory
			// pointers; NULL entries are skipped (documented no-op).
			let dir_ptr = unsafe { *_bundle_dirs.add(i) };
			if dir_ptr.is_null() {
				continue;
			}
			// SAFETY: `dir_ptr` is a valid NUL-terminated C string.
			let dir = match unsafe { CStr::from_ptr(dir_ptr) }.to_str() {
				Ok(dir) => dir,
				Err(_) => return OAKPLUGIN_E_INVALID, // non-UTF-8 paths are rejected
			};
			if let Err(e) = host.cache.scan_path(Path::new(dir)) {
				return e.code();
			}
		}
		OAKPLUGIN_OK
	}

	/// Initialize the OFX host (oakplugin_host_init; idempotent singleton).
	pub fn oakplugin_host_init() -> c_int {
		let _ = Host::global();
		OAKPLUGIN_OK
	}

	/// Number of loaded plugins.
	pub fn oakplugin_host_plugin_count() -> c_int {
		Host::global().cache.count() as c_int
	}

	/// Identifier of the plugin at `_index` (two-stage buf/size getter).
	pub fn oakplugin_host_plugin_id_at(_index: c_int, _buf: *mut c_char, _buf_size: c_int) -> c_int {
		if _index < 0 || !is_valid_string_out(_buf, _buf_size) {
			return OAKPLUGIN_E_INVALID;
		}
		match Host::global().cache.at(_index as usize) {
			Some(p) => copy_string(&p.identifier, _buf, _buf_size),
			None => OAKPLUGIN_E_NOT_FOUND,
		}
	}

	/// Label (`OfxPropLabel`) of the named plugin (two-stage getter).
	pub fn oakplugin_host_plugin_label(
		_plugin_id: *const c_char,
		_buf: *mut c_char,
		_buf_size: c_int,
	) -> c_int {
		if _plugin_id.is_null() || !is_valid_string_out(_buf, _buf_size) {
			return OAKPLUGIN_E_INVALID;
		}
		// SAFETY: the caller guarantees a valid NUL-terminated id.
		let id = unsafe { read_cstr(_plugin_id) };
		match Host::global().cache.find(&id) {
			Some(p) => {
				let label = match p.descriptor.props.get("OfxPropLabel", 0) {
					Some(oakplugin::property::Value::String(s)) => s.to_string_lossy().into_owned(),
					_ => String::new(),
				};
				copy_string(&label, _buf, _buf_size)
			}
			None => OAKPLUGIN_E_NOT_FOUND,
		}
	}
}


// ===========================================================================
// render — oakrender domain implementation (single-lib unification)
// ===========================================================================
//
// The deleted oakrender C ABI is replaced by the crate's direct Rust
// types: the manager singleton (`oakrender::manager::RenderManager`),
// the ticket arena (`oakrender::ticket::TicketArena` — owned by the
// manager, so the facade submits through `RenderManager::global()`),
// value-typed frames (`oakrender::texture::Frame`), the color processor
// (`oakrender::color::ColorProcessor`) and the auto-cacher
// (`oakrender::autocacher::PreviewAutoCacher`).
//
// The facade's ticket handles box a [`TicketBox`] payload (arena + id +
// completion result); the engine's synchronous render loop then waits and
// reads the produced frame/samples through it.
pub mod render {
	use std::ffi::{c_char, c_double, c_int, c_void};
	use std::sync::{Arc, Mutex};

	use oakrender::error::{OAKRENDER_E_INVALID, OAKRENDER_E_NOT_FOUND, OAKRENDER_E_STATE};
	use oakrender::ticket::{TicketArena, TicketId, TicketPayload, TicketResult};

	use crate::handle::domain::node_ref_of;
	use crate::handle::CHandle;
	use crate::pods::{OakRenderVideoParams, OakVideoTicketParams};

	// -------------------------------------------------------------------
	// Manager / color config
	// -------------------------------------------------------------------

	/// `oakrender_manager_init` — direct call into
	/// `oakrender::manager::RenderManager::init`.
	pub fn oakrender_manager_init() -> c_int {
		match oakrender::manager::RenderManager::init() {
			Ok(()) => 0,
			Err(e) => e.code(),
		}
	}

	/// `oakrender_manager_available` — 1 when the global manager is up.
	pub fn oakrender_manager_available() -> c_int {
		oakrender::manager::RenderManager::global().is_some() as c_int
	}

	/// `oakrender_manager_shutdown` — direct call into
	/// `oakrender::manager::RenderManager::shutdown`.
	pub fn oakrender_manager_shutdown() {
		oakrender::manager::RenderManager::shutdown();
	}

	/// `oakrender_manager_set_aggressive_gc` — direct call.
	pub fn oakrender_manager_set_aggressive_gc(enabled: c_int) -> c_int {
		match oakrender::manager::RenderManager::global() {
			Some(m) => {
				m.set_aggressive_gc(enabled != 0);
				0
			}
			None => OAKRENDER_E_STATE,
		}
	}

	/// `oakrender_color_manager_set_up_default_config` — direct call into
	/// `oakrender::color::set_up_default_config`.
	pub fn oakrender_color_manager_set_up_default_config() -> c_int {
		match oakrender::color::set_up_default_config() {
			Ok(()) => 0,
			Err(_) => oakrender::error::OAKRENDER_E_FAILED,
		}
	}

	/// `oakrender_color_manager_get_config` (two-stage string getter) —
	/// direct call into `oakrender::color::config_path`.
	pub fn oakrender_color_manager_get_config(buf: *mut c_char, n: c_int) -> c_int {
		match oakrender::color::config_path() {
			Some(path) => {
				let required = (path.len() + 1) as c_int;
				if !buf.is_null() && n >= required {
					// SAFETY: the caller guarantees `buf` holds `n` bytes.
					unsafe {
						std::ptr::copy_nonoverlapping(
							path.as_ptr() as *const c_char,
							buf,
							path.len(),
						);
						*buf.add(path.len()) = 0;
					}
				}
				required
			}
			None => OAKRENDER_E_STATE, // no config = uninitialized color manager
		}
	}

	/// The manager's auto-cacher view.
	fn with_cacher<R>(f: impl FnOnce(&mut oakrender::autocacher::PreviewAutoCacher) -> R) -> Option<R> {
		let m = oakrender::manager::RenderManager::global()?;
		let mut guard = m.get_cacher();
		let cacher = guard.as_mut()?;
		Some(f(cacher))
	}

	/// `oakrender_set_cacher_multicam` — the viewer identity the
	/// auto-cacher previews (null clears it).
	pub fn oakrender_set_cacher_multicam(multicam_or_null: CHandle) -> c_int {
		let identity = if multicam_or_null.is_null() {
			None
		} else {
			// SAFETY: node handles box oaknode NodeRef payloads.
			unsafe { node_ref_of(&multicam_or_null) }
				.map(|nr| nr.id.identity())
		};
		match with_cacher(|c| c.set_viewer_identity(identity)) {
			Some(()) => 0,
			None => OAKRENDER_E_STATE,
		}
	}

	/// `oakrender_set_display_color_processor` — the display color
	/// processor identity the auto-cacher applies (null clears it).
	pub fn oakrender_set_display_color_processor(p_or_null: CHandle) -> c_int {
		let identity = if p_or_null.is_null() {
			None
		} else {
			Some(p_or_null.ctx as u64)
		};
		match with_cacher(|c| c.set_display_color_processor(identity)) {
			Some(()) => 0,
			None => OAKRENDER_E_STATE,
		}
	}

	// -------------------------------------------------------------------
	// Ticket family
	// -------------------------------------------------------------------

	/// Engine-side ticket payload: the arena plus the reserved id and the
	/// completion result slot.
	struct TicketBox {
		arena: Arc<TicketArena>,
		id: TicketId,
	}

	/// Convert the engine's montage POD into the arena's montage clips.
	///
	/// # Safety
	/// `pods` must hold `count` valid [`crate::pods::MontagePod`] entries.
	unsafe fn montage_from_pod(pods: *const crate::pods::MontagePod, count: c_int) -> Vec<oakrender::ticket::MontageClip> {
		if pods.is_null() || count <= 0 {
			return Vec::new();
		}
		// SAFETY: contract above.
		unsafe {
			let slice = std::slice::from_raw_parts(pods, count as usize);
			slice
				.iter()
				.map(|p| oakrender::ticket::MontageClip {
					filename: crate::handle::read_cstr(p.filename),
					stream_index: p.stream_index,
					in_time: oakcore_rs::Rational::new(p.in_num, p.in_den),
					out_time: oakcore_rs::Rational::new(p.out_num, p.out_den),
					media_in: oakcore_rs::Rational::new(p.media_in_num, p.media_in_den),
					gain: p.gain,
				})
				.collect()
		}
	}

	/// `oakrender_ticket_render_frame` — submit a video ticket through the
	/// manager's arena; the callback (when given) fires on completion with
	/// the ticket handle.
	pub fn oakrender_ticket_render_frame(
		params: *const OakVideoTicketParams,
		cb: Option<unsafe extern "C" fn(CHandle, *mut c_void)>,
		userdata: *mut c_void,
	) -> CHandle {
		if params.is_null() {
			return CHandle::null();
		}
		// SAFETY: the caller passes a live POD for the call duration.
		let p = unsafe { &*params };
		let Some(m) = oakrender::manager::RenderManager::global() else {
			return CHandle::null();
		};
		let viewer = unsafe { node_ref_of(&p.output_node) }
			.map(|nr| nr.id.identity())
			.unwrap_or(0);
		let force_size = if p.force_width > 0 && p.force_height > 0 {
			Some((p.force_width, p.force_height))
		} else {
			None
		};
		let force_format = if p.force_format >= 0 {
			Some(crate::pods::pixel_format_from_code(p.force_format))
		} else {
			None
		};
		let footage = if p.footage_filename.is_null() {
			None
		} else {
			// SAFETY: the caller guarantees a valid NUL-terminated string.
			Some((
				unsafe { crate::handle::read_cstr(p.footage_filename) },
				p.footage_stream,
			))
		};
		let montage = unsafe { montage_from_pod(p.montage, p.montage_count) };
		let video_params = oakrender::ticket::VideoTicketParams {
			viewer,
			time: oakcore_rs::Rational::new(p.time_num, p.time_den),
			force_size,
			force_format,
			cache: None,
			cache_dir: None,
			cache_id: None,
			cache_timebase: None,
			footage,
			montage,
		};
		let ticket_box = Arc::new(TicketBox {
			arena: m.tickets.clone(),
			id: m.tickets.next_id(),
		});
		let ticket_h = oakrender::handle::make_owned(ticket_box.clone());
		let userdata_ptr = userdata as usize;
		let completion: oakrender::ticket::Completion = Box::new(move |_result| {
			if let Some(cb) = cb {
				// SAFETY: the callback + userdata follow the caller's
				// contract; the ticket handle outlives the completion.
				unsafe { cb(ticket_h, userdata_ptr as *mut c_void) };
			}
		});
		m.tickets
			.submit_video_with_id(ticket_box.id, video_params, completion);
		oakrender::handle::make_owned(ticket_box)
	}

	/// `oakrender_ticket_render_audio` — submit an audio ticket through
	/// the manager's arena (the `oakrender::eval::render_audio_samples`
	/// producer mixes the montage).
	pub fn oakrender_ticket_render_audio(
		output_node: CHandle,
		in_num: i64,
		in_den: i64,
		out_num: i64,
		out_den: i64,
		params: *const c_void,
		_mode: c_int,
		cb: Option<unsafe extern "C" fn(CHandle, *mut c_void)>,
		userdata: *mut c_void,
		montage: *const crate::pods::MontagePod,
		montage_count: c_int,
	) -> CHandle {
		if in_den == 0 || out_den == 0 {
			return CHandle::null();
		}
		let Some(m) = oakrender::manager::RenderManager::global() else {
			return CHandle::null();
		};
		let viewer = unsafe { node_ref_of(&output_node) }
			.map(|nr| nr.id.identity())
			.unwrap_or(0);
		let sample_rate = if params.is_null() {
			48000
		} else {
			// SAFETY: the oakcore audioparams contract.
			unsafe { crate::stubs::audio::oakcore_audioparams_sample_rate(params) }
		};
		let channel_layout = if params.is_null() {
			0x3
		} else {
			// SAFETY: the oakcore audioparams contract.
			unsafe { crate::stubs::audio::oakcore_audioparams_channel_layout(params) }
		};
		let audio_params = oakrender::ticket::AudioTicketParams {
			viewer,
			range: oakcore_rs::TimeRange::new(
				oakcore_rs::Rational::new(in_num, in_den),
				oakcore_rs::Rational::new(out_num, out_den),
			),
			sample_rate,
			channel_layout,
			montage: unsafe { montage_from_pod(montage, montage_count) },
		};
		let ticket_box = Arc::new(TicketBox {
			arena: m.tickets.clone(),
			id: m.tickets.next_id(),
		});
		let ticket_h = oakrender::handle::make_owned(ticket_box.clone());
		let userdata_ptr = userdata as usize;
		let completion: oakrender::ticket::Completion = Box::new(move |_result| {
			if let Some(cb) = cb {
				// SAFETY: see the video-ticket callback path.
				unsafe { cb(ticket_h, userdata_ptr as *mut c_void) };
			}
		});
		m.tickets
			.submit_audio_with_id(ticket_box.id, audio_params, completion);
		oakrender::handle::make_owned(ticket_box)
	}

	/// `oakrender_audio_samples_free` — release the samples block returned
	/// by `oakrender_ticket_get_samples` (NULL no-op).
	pub fn oakrender_audio_samples_free(samples: *mut c_void) {
		if samples.is_null() {
			return;
		}
		// SAFETY: `samples` must be a box created for the samples block.
		drop(unsafe { Box::from_raw(samples as *mut crate::pods::OakAudioSamplesOut) });
	}

	fn ticket_box(ticket: CHandle) -> Option<&'static TicketBox> {
		// Ticket handles box `Arc<TicketBox>` (the completion closure and
		// the arena slot hold their own clones, so the payload outlives
		// every handle access).
		// SAFETY: ticket handles box Arc<TicketBox> payloads.
		let t = unsafe { oakrender::handle::get::<Arc<TicketBox>>(&ticket) }?;
		// SAFETY: the Arc (and the TicketBox it owns) lives for the whole
		// ticket lifetime; the handle's own reference is just one of several.
		let arc: &'static Arc<TicketBox> = unsafe { std::mem::transmute(t) };
		Some(&**arc)
	}

	/// `oakrender_ticket_wait` — block until the ticket finishes.
	pub fn oakrender_ticket_wait(ticket: CHandle) -> c_int {
		let Some(t) = ticket_box(ticket) else {
			return OAKRENDER_E_INVALID;
		};
		match t.arena.wait(t.id) {
			Ok(()) => 0,
			Err(e) => e.code(),
		}
	}

	/// `oakrender_ticket_cancel` — cancel the pending ticket.
	pub fn oakrender_ticket_cancel(ticket: CHandle) -> c_int {
		let Some(t) = ticket_box(ticket) else {
			return OAKRENDER_E_INVALID;
		};
		t.arena.cancel(t.id);
		0
	}

	/// `oakrender_ticket_get_frame` — the produced video frame (a boxed
	/// `oakrender::texture::Frame`), empty when the ticket is unfinished,
	/// cancelled or audio.
	pub fn oakrender_ticket_get_frame(ticket: CHandle, out: *mut CHandle) -> c_int {
		if out.is_null() {
			return OAKRENDER_E_INVALID;
		}
		let Some(t) = ticket_box(ticket) else {
			return OAKRENDER_E_INVALID;
		};
		// Read the arena slot's result directly: `TicketSlot::finish`
		// stores it BEFORE flipping the slot to Finished, so this is
		// race-free (the completion callback fires after the notify and may
		// not have run yet when the caller reads the frame).
		let result = t.arena.result(t.id);
		match result {
			Some(Ok(TicketPayload::Video(texture))) => match texture.to_frame() {
				Ok(frame) => {
					// SAFETY: valid out pointer.
					unsafe { *out = oakrender::handle::make_owned(frame) };
					0
				}
				Err(e) => e.code(),
			},
			Some(Err(e)) => e.code(),
			_ => {
				// SAFETY: valid out pointer.
				unsafe { *out = CHandle::null() };
				OAKRENDER_E_STATE
			}
		}
	}

	/// `oakrender_ticket_get_samples` — the produced audio samples block
	/// (caller frees with `oakrender_audio_samples_free`).
	pub fn oakrender_ticket_get_samples(ticket: CHandle, out: *mut *mut c_void) -> c_int {
		if out.is_null() {
			return OAKRENDER_E_INVALID;
		}
		let Some(t) = ticket_box(ticket) else {
			return OAKRENDER_E_INVALID;
		};
		// Read the arena slot's result directly: `TicketSlot::finish`
		// stores it BEFORE flipping the slot to Finished, so this is
		// race-free (the completion callback fires after the notify and may
		// not have run yet when the caller reads the frame).
		let result = t.arena.result(t.id);
		match result {
			Some(Ok(TicketPayload::Audio(samples))) => {
				let frame_count =
					(samples.samples.len() / samples.channel_count.max(1) as usize) as c_int;
				let boxed = Box::new(crate::pods::OakAudioSamplesOut {
					data: samples.samples.into_boxed_slice(),
					frame_count,
					sample_rate: samples.sample_rate,
					channel_layout: samples.channel_layout,
					channel_count: samples.channel_count,
				});
				// SAFETY: valid out pointer.
				unsafe { *out = Box::into_raw(boxed) as *mut c_void };
				0
			}
			Some(Err(e)) => e.code(),
			_ => {
				// SAFETY: valid out pointer.
				unsafe { *out = std::ptr::null_mut() };
				OAKRENDER_E_STATE
			}
		}
	}

	/// `oakrender_ticket_free` — release the ticket handle shell.
	pub fn oakrender_ticket_free(ticket: *mut CHandle) {
		if ticket.is_null() {
			return;
		}
		// SAFETY: the caller passes a valid handle pointer.
		let h = unsafe { *ticket };
		if let Some(release) = h.release {
			// SAFETY: the boxed value's release callback.
			unsafe { release(h.ctx) };
		}
		// SAFETY: the caller passes a valid handle pointer.
		unsafe { *ticket = CHandle::null() };
	}

	// -------------------------------------------------------------------
	// Codec frame family (`oakrender::texture::Frame` boxes)
	// -------------------------------------------------------------------

	fn frame_ref(frame: CHandle) -> Option<&'static oakrender::texture::Frame> {
		// SAFETY: frame handles box `oakrender::texture::Frame`.
		let f = unsafe { oakrender::handle::get::<oakrender::texture::Frame>(&frame) }?;
		// SAFETY: the box outlives the handle.
		unsafe { Some(&*(f as *const _)) }
	}

	/// `oakrender_codec_frame_create` — a fresh, unallocated frame.
	pub fn oakrender_codec_frame_create() -> CHandle {
		oakrender::handle::make_owned(oakrender::texture::Frame::new())
	}

	/// `oakrender_codec_frame_retain` — addref'd copy.
	pub fn oakrender_codec_frame_retain(frame: CHandle) -> CHandle {
		if frame.is_null() {
			return CHandle::null();
		}
		if let Some(addref) = frame.addref {
			// SAFETY: the handle is live.
			unsafe { addref(frame.ctx) };
		}
		frame
	}

	/// `oakrender_codec_frame_free` — release the frame shell.
	pub fn oakrender_codec_frame_free(frame: *mut CHandle) {
		if frame.is_null() {
			return;
		}
		// SAFETY: the caller passes a valid handle pointer.
		let h = unsafe { *frame };
		if let Some(release) = h.release {
			// SAFETY: the boxed value's release callback.
			unsafe { release(h.ctx) };
		}
		// SAFETY: the caller passes a valid handle pointer.
		unsafe { *frame = CHandle::null() };
	}

	/// `oakrender_codec_frame_width`.
	pub fn oakrender_codec_frame_width(frame: CHandle) -> c_int {
		frame_ref(frame)
			.map(|f| f.video_params().width)
			.unwrap_or(0)
	}

	/// `oakrender_codec_frame_height`.
	pub fn oakrender_codec_frame_height(frame: CHandle) -> c_int {
		frame_ref(frame)
			.map(|f| f.video_params().height)
			.unwrap_or(0)
	}

	/// `oakrender_codec_frame_linesize_bytes`.
	pub fn oakrender_codec_frame_linesize_bytes(frame: CHandle) -> c_int {
		frame_ref(frame)
			.map(|f| f.linesize_bytes() as c_int)
			.unwrap_or(0)
	}

	/// `oakrender_codec_frame_data` — mutable pixel data view.
	pub fn oakrender_codec_frame_data(frame: CHandle) -> *mut c_void {
		// SAFETY: the caller holds exclusive access for the borrow.
		match unsafe { oakrender::handle::get_mut::<oakrender::texture::Frame>(&frame) } {
			Some(f) => f.data_mut() as *mut c_void,
			None => std::ptr::null_mut(),
		}
	}

	/// `oakrender_codec_frame_const_data` — read-only pixel data view.
	pub fn oakrender_codec_frame_const_data(frame: CHandle) -> *const c_void {
		frame_ref(frame)
			.map(|f| f.data() as *const c_void)
			.unwrap_or(std::ptr::null())
	}

	/// `oakrender_codec_frame_is_allocated`.
	pub fn oakrender_codec_frame_is_allocated(frame: CHandle) -> c_int {
		match frame_ref(frame) {
			Some(f) if f.is_allocated() => 1,
			_ => 0,
		}
	}

	/// `oakrender_codec_frame_get_params` — the frame's video-params POD.
	pub fn oakrender_codec_frame_get_params(
		frame: CHandle,
		out: *mut OakRenderVideoParams,
	) -> c_int {
		if out.is_null() {
			return OAKRENDER_E_INVALID;
		}
		let Some(f) = frame_ref(frame) else {
			return OAKRENDER_E_INVALID;
		};
		let pod = f.video_params();
		// SAFETY: valid out pointer.
		unsafe {
			(*out) = OakRenderVideoParams {
				width: pod.width,
				height: pod.height,
				time_base_num: pod.time_base_num,
				time_base_den: pod.time_base_den,
				format: pod.format,
				pixel_aspect_num: pod.pixel_aspect_num,
				pixel_aspect_den: pod.pixel_aspect_den,
				interlacing: pod.interlacing,
				color_range: pod.color_range,
				divider: pod.divider,
				video_type: pod.video_type,
				premultiplied_alpha: pod.premultiplied_alpha,
			};
		}
		0
	}

	// -------------------------------------------------------------------
	// Color processor family
	// -------------------------------------------------------------------

	/// The C ABI direction -> the domain direction.
	fn direction_from_c(direction: c_int) -> oakrender::color::Direction {
		if direction == 0 {
			oakrender::color::Direction::Normal
		} else {
			oakrender::color::Direction::Inverse
		}
	}

	/// `oakrender_color_processor_create` — build a processor from two
	/// colorspace names (OCIO failures are non-fatal: a pass-through
	/// processor is returned).
	pub fn oakrender_color_processor_create(
		src_space: *const c_char,
		dst_transform: *const c_char,
		direction: c_int,
	) -> CHandle {
		if src_space.is_null() || dst_transform.is_null() {
			return CHandle::null();
		}
		// SAFETY: the caller guarantees valid NUL-terminated strings.
		let (src, dst) = unsafe {
			(
				crate::handle::read_cstr(src_space),
				crate::handle::read_cstr(dst_transform),
			)
		};
		match oakrender::color::ColorProcessor::create(&src, &dst, direction_from_c(direction)) {
			Some(processor) => oakrender::handle::make_owned(processor),
			None => CHandle::null(),
		}
	}

	/// `oakrender_color_processor_free` — release the shell.
	pub fn oakrender_color_processor_free(processor: *mut CHandle) {
		if processor.is_null() {
			return;
		}
		// SAFETY: the caller passes a valid handle pointer.
		let h = unsafe { *processor };
		if let Some(release) = h.release {
			// SAFETY: the boxed value's release callback.
			unsafe { release(h.ctx) };
		}
		// SAFETY: the caller passes a valid handle pointer.
		unsafe { *processor = CHandle::null() };
	}

	/// `oakrender_color_processor_is_valid`.
	pub fn oakrender_color_processor_is_valid(processor: CHandle) -> c_int {
		// SAFETY: processor handles box `oakrender::color::ColorProcessor`.
		match unsafe { oakrender::handle::get::<oakrender::color::ColorProcessor>(&processor) } {
			Some(p) if p.is_valid() => 1,
			_ => 0,
		}
	}

	/// `oakrender_color_processor_create_transform` — build a processor
	/// from the input colorspace and the destination described by an
	/// oakcommon colortransform handle (display/view/look or a plain
	/// output colorspace).
	pub fn oakrender_color_processor_create_transform(
		_manager: CHandle,
		input: *const c_char,
		dest: CHandle,
		direction: c_int,
	) -> CHandle {
		if input.is_null() || dest.is_null() {
			return CHandle::null();
		}
		// SAFETY: the caller guarantees a valid NUL-terminated string and
		// a live oakcommon colortransform handle.
		let (input, ct) = unsafe {
			(
				crate::handle::read_cstr(input),
				oakcommon::handle::get::<oakcommon::colortransform::ColorTransform>(&dest),
			)
		};
		let Some(ct) = ct else {
			return CHandle::null();
		};
		let dst = if ct.is_display() {
			match oakrender::color::display_transform(ct.display(), ct.view()) {
				Some(d) => d,
				None => return CHandle::null(),
			}
		} else {
			ct.output().to_string()
		};
		match oakrender::color::ColorProcessor::create(&input, &dst, direction_from_c(direction)) {
			Some(processor) => oakrender::handle::make_owned(processor),
			None => CHandle::null(),
		}
	}

	/// `oakrender_color_processor_convert` — one RGBA color through the
	/// processor.
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
		if out_r.is_null() || out_g.is_null() || out_b.is_null() || out_a.is_null() {
			return OAKRENDER_E_INVALID;
		}
		// SAFETY: processor handles box `oakrender::color::ColorProcessor`.
		let Some(p) = (unsafe { oakrender::handle::get::<oakrender::color::ColorProcessor>(&processor) })
		else {
			return OAKRENDER_E_INVALID;
		};
		let rgba = p.convert_color([ir, ig, ib, ia]);
		// SAFETY: valid out pointers.
		unsafe {
			*out_r = rgba[0];
			*out_g = rgba[1];
			*out_b = rgba[2];
			*out_a = rgba[3];
		}
		0
	}

	// -------------------------------------------------------------------
	// LUT library (`oakrender::color::SUPPORTED_LUT_EXTENSIONS`)
	// -------------------------------------------------------------------

	/// `oakrender_lut_is_supported_extension`.
	pub fn oakrender_lut_is_supported_extension(extension: *const c_char) -> c_int {
		if extension.is_null() {
			return 0;
		}
		// SAFETY: the caller guarantees a valid NUL-terminated string.
		let extension = unsafe { crate::handle::read_cstr(extension) };
		oakrender::color::is_supported_lut_extension(&extension) as c_int
	}

	/// `oakrender_lut_supported_extensions_count`.
	pub fn oakrender_lut_supported_extensions_count() -> c_int {
		oakrender::color::SUPPORTED_LUT_EXTENSIONS.len() as c_int
	}

	/// `oakrender_lut_supported_extension_at` (two-stage).
	pub fn oakrender_lut_supported_extension_at(i: c_int, buf: *mut c_char, n: c_int) -> c_int {
		if i < 0 {
			return OAKRENDER_E_NOT_FOUND;
		}
		match oakrender::color::SUPPORTED_LUT_EXTENSIONS.get(i as usize) {
			Some(ext) => {
				let required = (ext.len() + 1) as c_int;
				if !buf.is_null() && n >= required {
					// SAFETY: the caller guarantees `buf` holds `n` bytes.
					unsafe {
						std::ptr::copy_nonoverlapping(
							ext.as_ptr() as *const c_char,
							buf,
							ext.len(),
						);
						*buf.add(ext.len()) = 0;
					}
				}
				required
			}
			None => OAKRENDER_E_NOT_FOUND,
		}
	}
}

// ===========================================================================
// task — oaktask domain implementation (single-lib unification)
// ===========================================================================
//
// The deleted oaktask C ABI is replaced by the crate's direct Rust types:
// the process-wide [`oaktask::manager::TaskManager`], the
// [`oaktask::task::Task`] base + the concrete project/render task types
// (`project::load::ProjectLoadTask`, `project::save::ProjectSaveTask`,
// `project::import::ProjectImportTask`, `project::loadotio::LoadOTIOTask`,
// `project::saveotio::SaveOTIOTask`, `precache::PreCacheTask`,
// `export::ExportTask`).
//
// The facade's task handles box a [`TaskPayload`] holding the owned driver
// `Task`; when the task is handed to the manager (`oaktask_task_start`)
// the box is moved into the manager and the payload keeps a raw
// (manager-owned) pointer so title/error/cancel keep working. Task-result
// accessors that the crate keeps private on the behavior
// (`take_project`/`take_command`) are bridged through engine-side
// wrapper behaviors that copy the results into shared slots in the
// payload.
pub mod task {
	use std::ffi::{c_char, c_int, c_void};
	use std::sync::{Arc, Mutex, OnceLock};

	use oaknode::project::Project;
	use oakundo::undocommand::{command_from_owned, UndoCommand};

	use crate::handle::domain::project_of;
	use crate::handle::CHandle;
	use crate::pods::EncodingParamsPOD;

	/// `oaktask_event_fn` callback (`include/task/task.h`).
	pub type OakTaskEventFn = unsafe extern "C" fn(event_id: c_int, value: f64, userdata: *mut c_void);

	/// `oaktask_otio_import_confirm_fn` (`include/task/project.h`).
	pub type OakTaskOtioImportConfirmFn = unsafe extern "C" fn(
		sequence_names: *const *const c_char,
		count: c_int,
		userdata: *mut c_void,
	) -> c_int;

	/// `oaktask_image_sequence_confirm_fn` (`include/task/project.h`).
	pub type OakTaskImageSequenceConfirmFn =
		unsafe extern "C" fn(filename: *const c_char, userdata: *mut c_void) -> c_int;

	/// Raw pointer to a manager-owned task (Send shim).
	struct TaskPtr(*mut oaktask::task::Task);

	// SAFETY: the pointee lives while the manager owns the task; the
	// facade never dereferences it concurrently with the manager's worker.
	unsafe impl Send for TaskPtr {}

	/// Engine-side task payload (boxed behind every facade task handle).
	struct TaskPayload {
		/// The owned driver task (None once handed to the manager).
		owned: Option<Box<oaktask::task::Task>>,
		/// Manager-owned view (valid while the manager keeps the task).
		borrowed: TaskPtr,
		/// Result slots for the specialized task kinds.
		kind: TaskKind,
	}

	enum TaskKind {
		/// Plain task (export/precache/generic).
		Plain,
		/// Interchange load: the loaded project appears here.
		Load {
			result: Arc<Mutex<Option<Arc<Mutex<Project>>>>>,
		},
		/// Media import: the produced undo command + footage + invalid
		/// file count.
		Import {
			command: Arc<Mutex<Option<UndoCommand>>>,
			footage: Arc<Mutex<Vec<CHandle>>>,
			invalid_count: Arc<Mutex<usize>>,
		},
	}

	impl Drop for TaskPayload {
		fn drop(&mut self) {
			TASK_ALIVE.fetch_sub(1, std::sync::atomic::Ordering::SeqCst);
		}
	}

	impl TaskPayload {
		fn task(&self) -> &oaktask::task::Task {
			match &self.owned {
				Some(t) => t,
				None => {
					// SAFETY: the manager owns the pointee for the
					// handle's lifetime (facade contract).
					unsafe { &*self.borrowed.0 }
				}
			}
		}

		fn task_mut(&mut self) -> &mut oaktask::task::Task {
			match &mut self.owned {
				Some(t) => t,
				None => {
					// SAFETY: see `TaskPayload::task`.
					unsafe { &mut *self.borrowed.0 }
				}
			}
		}
	}

	/// Live-task counter (the deleted `oaktask_debug_alive_count`).
	static TASK_ALIVE: std::sync::atomic::AtomicI32 = std::sync::atomic::AtomicI32::new(0);

	fn box_task(driver: oaktask::task::Task, kind: TaskKind) -> CHandle {
		let ptr = Box::into_raw(Box::new(driver));
		TASK_ALIVE.fetch_add(1, std::sync::atomic::Ordering::SeqCst);
		oaktask::handle::make_owned(TaskPayload {
			owned: Some(unsafe { Box::from_raw(ptr) }),
			borrowed: TaskPtr(ptr),
			kind,
		})
	}

	fn task_payload(t: CHandle) -> Option<&'static TaskPayload> {
		// SAFETY: task handles box TaskPayload.
		let p = unsafe { oaktask::handle::get::<TaskPayload>(&t) }?;
		// SAFETY: the box outlives the handle.
		unsafe { Some(&*(p as *const _)) }
	}

	fn task_payload_mut(t: CHandle) -> Option<&'static mut TaskPayload> {
		// SAFETY: task handles box TaskPayload; the caller holds
		// exclusive access.
		let p = unsafe { oaktask::handle::get_mut::<TaskPayload>(&t) }?;
		// SAFETY: the box outlives the handle.
		unsafe { Some(&mut *(p as *mut _)) }
	}

	/// A fresh base task with the given title.
	fn base_task(title: &str) -> oaktask::task::Task {
		oaktask::task::Task::new(title, None)
	}

	// -------------------------------------------------------------------
	// Manager family
	// -------------------------------------------------------------------

	/// `oaktask_manager_init` — create the process-wide manager singleton.
	pub fn oaktask_manager_init() -> c_int {
		match oaktask::manager::TaskManager::init() {
			Ok(()) => oaktask::error::OAKTASK_OK,
			Err(e) => e.code(),
		}
	}

	/// `oaktask_manager_shutdown` — destroy the singleton (idempotent).
	pub fn oaktask_manager_shutdown() {
		oaktask::manager::TaskManager::shutdown();
	}

	/// `oaktask_register_codec_submitter` — record the registration flag.
	pub fn oaktask_register_codec_submitter() -> c_int {
		match oaktask::manager::TaskManager::with_manager_mut(|m| {
			m.set_codec_submitter_registered(true);
		}) {
			Some(()) => oaktask::error::OAKTASK_OK,
			None => oaktask::error::OAKTASK_E_STATE,
		}
	}

	/// `oaktask_manager_count` — live tasks.
	pub fn oaktask_manager_count() -> c_int {
		match oaktask::manager::TaskManager::with_manager(|m| m.get_task_count() as c_int) {
			Some(n) => n,
			None => oaktask::error::OAKTASK_E_STATE,
		}
	}

	/// `oaktask_manager_at` — a borrowed-view task handle of the task at
	/// `i` (a payload box whose task pointer borrows the manager-owned
	/// task; the manager must outlive the handle).
	pub fn oaktask_manager_at(i: c_int) -> CHandle {
		if i < 0 {
			return CHandle::null();
		}
		let ptr = match oaktask::manager::TaskManager::with_manager(|m| m.task_ptr_at(i as usize)) {
			Some(Ok(p)) => p,
			_ => return CHandle::null(),
		};
		TASK_ALIVE.fetch_add(1, std::sync::atomic::Ordering::SeqCst);
		oaktask::handle::make_owned(TaskPayload {
			owned: None,
			borrowed: TaskPtr(ptr),
			kind: TaskKind::Plain,
		})
	}

	/// `oaktask_manager_delete_finished` — remove finished tasks.
	pub fn oaktask_manager_delete_finished() {
		oaktask::manager::TaskManager::with_manager_mut(|m| m.delete_finished());
	}

	/// `oaktask_task_free` — release the task handle shell (drops a still
	/// owned task).
	pub fn oaktask_task_free(t: *mut CHandle) {
		if t.is_null() {
			return;
		}
		// SAFETY: the caller passes a valid handle pointer.
		let h = unsafe { *t };
		let _ = &h;
		if let Some(release) = h.release {
			// SAFETY: the boxed value's release callback (drops the
			// payload: an owned task is destroyed, a manager-owned one is
			// left alone).
			unsafe { release(h.ctx) };
		}
		// SAFETY: the caller passes a valid handle pointer.
		unsafe { *t = CHandle::null() };
	}

	/// `oaktask_task_start_sync` — run on the calling thread (1/0).
	pub fn oaktask_task_start_sync(t: CHandle) -> c_int {
		let Some(p) = task_payload_mut(t) else {
			return 0;
		};
		match p.task_mut().start() {
			Ok(()) => 1,
			Err(_) => 0,
		}
	}

	/// `oaktask_task_start` — hand the owned task to the manager (the
	/// manager spawns its worker and deletes the task when done; the
	/// handle becomes a borrowed view).
	pub fn oaktask_task_start(t: CHandle) -> c_int {
		let Some(p) = task_payload_mut(t) else {
			return oaktask::error::OAKTASK_E_INVALID;
		};
		let Some(task) = p.owned.take() else {
			return oaktask::error::OAKTASK_E_STATE;
		};
		let raw = Box::into_raw(task);
		p.borrowed = TaskPtr(raw);
		if oaktask::manager::TaskManager::with_manager(|_| ()).is_none() {
			p.owned = Some(unsafe { Box::from_raw(raw) });
			return oaktask::error::OAKTASK_E_STATE;
		}
		// SAFETY: the box was just created above; the manager takes
		// ownership of the (stable) allocation.
		oaktask::manager::TaskManager::with_manager_mut(|m| {
			m.add_task(unsafe { Box::from_raw(raw) } as Box<dyn oaktask::manager::TaskBox>)
		});
		oaktask::error::OAKTASK_OK
	}

	/// `oaktask_task_cancel`.
	pub fn oaktask_task_cancel(t: CHandle) -> c_int {
		let Some(p) = task_payload_mut(t) else {
			return oaktask::error::OAKTASK_E_INVALID;
		};
		p.task_mut().cancel();
		oaktask::error::OAKTASK_OK
	}

	/// `oaktask_task_wait` — block until the task finishes.
	pub fn oaktask_task_wait(t: CHandle) -> c_int {
		let Some(p) = task_payload(t) else {
			return oaktask::error::OAKTASK_E_INVALID;
		};
		p.task().wait_finished();
		oaktask::error::OAKTASK_OK
	}

	/// `oaktask_task_is_finished` — 1/0.
	pub fn oaktask_task_is_finished(t: CHandle) -> c_int {
		match task_payload(t) {
			Some(p) if p.task().is_finished() => 1,
			Some(_) => 0,
			None => 0,
		}
	}

	/// `oaktask_task_succeeded` — 1/0.
	pub fn oaktask_task_succeeded(t: CHandle) -> c_int {
		match task_payload(t) {
			Some(p) if p.task().succeeded() => 1,
			Some(_) => 0,
			None => 0,
		}
	}

	/// `oaktask_task_title` (two-stage).
	pub fn oaktask_task_title(t: CHandle, buf: *mut c_char, buf_size: c_int) -> c_int {
		let Some(p) = task_payload(t) else {
			return oaktask::error::OAKTASK_E_INVALID;
		};
		let title = p.task().title().to_string();
		let required = (title.len() + 1) as c_int;
		if !buf.is_null() && buf_size >= required {
			// SAFETY: the caller guarantees `buf` holds `buf_size` bytes.
			unsafe {
				std::ptr::copy_nonoverlapping(title.as_ptr() as *const c_char, buf, title.len());
				*buf.add(title.len()) = 0;
			}
		}
		required
	}

	/// `oaktask_task_error` (two-stage).
	pub fn oaktask_task_error(t: CHandle, buf: *mut c_char, buf_size: c_int) -> c_int {
		let Some(p) = task_payload(t) else {
			return oaktask::error::OAKTASK_E_INVALID;
		};
		let error = p.task().error().unwrap_or("Unknown error").to_string();
		let required = (error.len() + 1) as c_int;
		if !buf.is_null() && buf_size >= required {
			// SAFETY: the caller guarantees `buf` holds `buf_size` bytes.
			unsafe {
				std::ptr::copy_nonoverlapping(error.as_ptr() as *const c_char, buf, error.len());
				*buf.add(error.len()) = 0;
			}
		}
		required
	}

	/// `oaktask_task_subscribe` — register the legacy `(event_id, value,
	/// userdata)` listener (0 on success).
	pub fn oaktask_task_subscribe(
		t: CHandle,
		cb: Option<OakTaskEventFn>,
		userdata: *mut c_void,
	) -> i64 {
		let Some(p) = task_payload_mut(t) else {
			return -1;
		};
		let Some(cb) = cb else {
			return -1;
		};
		let state = Arc::new(oaktask::task::SubscriberState::default());
		let userdata_ptr = userdata as usize;
		p.task_mut().set_subscriber(state.clone());
		p.task_mut().set_event_listener(Box::new(move |ev| {
			// SAFETY: the callback + userdata follow the caller's
			// contract; the task emits on its own thread.
			unsafe {
				match ev {
					oaktask::task::TaskEvent::Started => cb(
						0,
						state
							.start_ms
							.load(std::sync::atomic::Ordering::SeqCst) as f64,
						userdata_ptr as *mut c_void,
					),
					oaktask::task::TaskEvent::Progress(v) => cb(1, v, userdata_ptr as *mut c_void),
					oaktask::task::TaskEvent::Finished => cb(
						2,
						state
							.finished_value
							.load(std::sync::atomic::Ordering::SeqCst) as f64,
						userdata_ptr as *mut c_void,
					),
				}
			}
		}));
		0
	}

	/// `oaktask_debug_alive_count` — live facade task payloads.
	pub fn oaktask_debug_alive_count() -> c_int {
		TASK_ALIVE.load(std::sync::atomic::Ordering::SeqCst)
	}

	// -------------------------------------------------------------------
	// Task creators
	// -------------------------------------------------------------------

	/// Box a project payload for the result paths (reuses the node
	/// module's project box).
	fn box_project_result(project: Arc<Mutex<Project>>) -> CHandle {
		crate::stubs::node::box_project_handle(project)
	}

	/// The engine-side wrapper behavior for interchange loads: delegates
	/// to the real `ProjectLoadTask` and copies the loaded project into
	/// the shared result slot.
	struct LoadTaskBehavior {
		inner: oaktask::project::load::ProjectLoadTask,
		result: Arc<Mutex<Option<Arc<Mutex<Project>>>>>,
	}

	impl oaktask::task::TaskBehavior for LoadTaskBehavior {
		fn run(&mut self, task: &mut oaktask::task::Task) -> oaktask::error::Result<()> {
			self.inner.run(task)?;
			if let Ok(project) = self.inner.base.take_project() {
				*self.result.lock().unwrap_or_else(|e| e.into_inner()) = Some(project);
			}
			Ok(())
		}
	}

	/// `oaktask_create_project_load` — a task that loads an OVE project.
	pub fn oaktask_create_project_load(filename: *const c_char) -> CHandle {
		if filename.is_null() {
			return CHandle::null();
		}
		// SAFETY: the caller guarantees a valid NUL-terminated string.
		let filename = unsafe { crate::handle::read_cstr(filename) };
		let title = format!("Loading '{filename}'");
		let result = Arc::new(Mutex::new(None));
		let mut driver = base_task(&title);
		let inner_base = base_task(&title);
		let inner = oaktask::project::load::ProjectLoadTask {
			base: oaktask::project::load::ProjectLoadBaseTask::new(inner_base, filename),
		};
		driver.set_behavior(Box::new(LoadTaskBehavior {
			inner,
			result: result.clone(),
		}));
		box_task(driver, TaskKind::Load { result })
	}

	/// `oaktask_load_take_project` — take the project a successful load
	/// produced (empty handle before the run).
	pub fn oaktask_load_take_project(t: CHandle) -> CHandle {
		let Some(p) = task_payload(t) else {
			return CHandle::null();
		};
		match &p.kind {
			TaskKind::Load { result } => {
				let project = result
					.lock()
					.unwrap_or_else(|e| e.into_inner())
					.take();
				match project {
					Some(project) => box_project_result(project),
					None => CHandle::null(),
				}
			}
			_ => CHandle::null(),
		}
	}

	/// `oaktask_create_project_save` — a task that saves a project.
	pub fn oaktask_create_project_save(
		project: CHandle,
		filename_or_null: *const c_char,
		use_compression: c_int,
	) -> CHandle {
		// SAFETY: project handles box ProjectArc payloads.
		let project_arc = match unsafe { project_of(&project) }.cloned() {
			Some(p) => p,
			None => return CHandle::null(),
		};
		let override_filename = if filename_or_null.is_null() {
			None
		} else {
			// SAFETY: the caller guarantees a valid NUL-terminated string.
			Some(unsafe { crate::handle::read_cstr(filename_or_null) })
		};
		let mut driver = base_task("Saving project...");
		let inner_base = base_task("Saving project...");
		let inner = oaktask::project::save::ProjectSaveTask {
			base: inner_base,
			project: project_arc,
			override_filename,
			use_compression: use_compression != 0,
		};
		driver.set_behavior(Box::new(inner));
		box_task(driver, TaskKind::Plain)
	}

	/// The engine-side wrapper behavior for imports: delegates to the real
	/// `ProjectImportTask` and copies the produced command/footage/invalid
	/// count into the shared slots.
	struct ImportTaskBehavior {
		inner: oaktask::project::import::ProjectImportTask,
		command: Arc<Mutex<Option<UndoCommand>>>,
		footage: Arc<Mutex<Vec<CHandle>>>,
		invalid_count: Arc<Mutex<usize>>,
	}

	impl oaktask::task::TaskBehavior for ImportTaskBehavior {
		fn run(&mut self, task: &mut oaktask::task::Task) -> oaktask::error::Result<()> {
			let result = self.inner.run(task);
			if let Ok(cmd) = self.inner.take_command() {
				*self.command.lock().unwrap_or_else(|e| e.into_inner()) = Some(cmd);
			}
			{
				let mut footage = self.footage.lock().unwrap_or_else(|e| e.into_inner());
				let n = self.inner.get_file_count();
				for i in 0..n {
					if let Ok(f) = self.inner.get_imported_footage(i) {
						footage.push(crate::stubs::node::box_node_handle(f.0, f.1, false));
					}
				}
			}
			*self.invalid_count.lock().unwrap_or_else(|e| e.into_inner()) =
				self.inner.get_invalid_file_count();
			result
		}
	}

	/// Process-wide image-sequence confirmation callback (the deleted C
	/// ABI kept it as a global; the real import task takes it at
	/// creation).
	static IMAGE_SEQ_CONFIRM: OnceLock<Mutex<Option<(OakTaskImageSequenceConfirmFn, usize)>>> =
		OnceLock::new();

	fn image_seq_confirm() -> Option<OakTaskImageSequenceConfirmFn> {
		IMAGE_SEQ_CONFIRM
			.get_or_init(|| Mutex::new(None))
			.lock()
			.unwrap_or_else(|e| e.into_inner())
			.map(|(cb, _)| cb)
	}

	/// `oaktask_create_project_import` — a task that imports media files
	/// into a folder.
	pub fn oaktask_create_project_import(
		folder: CHandle,
		project: CHandle,
		urls: *const *const c_char,
		url_count: c_int,
	) -> CHandle {
		if url_count < 0 || (urls.is_null() && url_count > 0) {
			return CHandle::null();
		}
		// SAFETY: node handles box oaknode NodeRef payloads.
		let folder_ref = match unsafe { crate::handle::domain::node_ref_of(&folder) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return CHandle::null(),
		};
		// SAFETY: project handles box ProjectArc payloads.
		let project_ref = match unsafe { crate::handle::domain::project_of(&project) }.cloned() {
			Some(p) => p,
			None => return CHandle::null(),
		};
		let mut filenames = Vec::new();
		for i in 0..url_count as usize {
			// SAFETY: the caller guarantees `url_count` valid pointers.
			let url = unsafe { *urls.add(i) };
			if url.is_null() {
				return CHandle::null();
			}
			// SAFETY: the caller guarantees a valid NUL-terminated string.
			filenames.push(unsafe { crate::handle::read_cstr(url) });
		}
		let file_count = filenames.len();
		let confirm = image_seq_confirm().map(|cb| {
			Box::new(move |filename: &str, _pattern: &str| -> bool {
				// SAFETY: the callback + userdata follow the caller's
				// contract.
				let cname = std::ffi::CString::new(filename).unwrap_or_default();
				unsafe { cb(cname.as_ptr(), std::ptr::null_mut()) != 0 }
			}) as oaktask::project::import::ImageSequenceConfirmFn
		});
		let command = Arc::new(Mutex::new(None));
		let footage = Arc::new(Mutex::new(Vec::new()));
		let invalid_count = Arc::new(Mutex::new(0));
		let title = format!("Importing {} file(s)", file_count);
		let mut driver = base_task(&title);
		let inner_base = base_task(&title);
		let inner = oaktask::project::import::ProjectImportTask::new(
			inner_base,
			folder_ref,
			project_ref,
			filenames,
			confirm,
			file_count,
		);
		driver.set_behavior(Box::new(ImportTaskBehavior {
			inner,
			command: command.clone(),
			footage: footage.clone(),
			invalid_count: invalid_count.clone(),
		}));
		box_task(
			driver,
			TaskKind::Import {
				command,
				footage,
				invalid_count,
			},
		)
	}

	/// `oaktask_import_take_command` — the undo command a successful
	/// import produced (detached from the task).
	pub fn oaktask_import_take_command(t: CHandle) -> CHandle {
		let Some(p) = task_payload(t) else {
			return CHandle::null();
		};
		match &p.kind {
			TaskKind::Import { command, .. } => {
				let cmd = command
					.lock()
					.unwrap_or_else(|e| e.into_inner())
					.take();
				match cmd {
					Some(cmd) => {
						// SAFETY: `command_from_owned` owns the command
						// value.
						unsafe { command_from_owned(cmd) }
					}
					None => CHandle::null(),
				}
			}
			_ => CHandle::null(),
		}
	}

	/// `oaktask_import_footage_count` — imported footage entries.
	pub fn oaktask_import_footage_count(t: CHandle) -> c_int {
		let Some(p) = task_payload(t) else {
			return oaktask::error::OAKTASK_E_INVALID;
		};
		match &p.kind {
			TaskKind::Import { footage, .. } => {
				footage.lock().unwrap_or_else(|e| e.into_inner()).len() as c_int
			}
			_ => oaktask::error::OAKTASK_E_INVALID,
		}
	}

	/// `oaktask_import_footage_at` — addref'd imported footage handle.
	pub fn oaktask_import_footage_at(t: CHandle, index: c_int) -> CHandle {
		if index < 0 {
			return CHandle::null();
		}
		let Some(p) = task_payload(t) else {
			return CHandle::null();
		};
		match &p.kind {
			TaskKind::Import { footage, .. } => {
				let h = footage
					.lock()
					.unwrap_or_else(|e| e.into_inner())
					.get(index as usize)
					.copied();
				match h {
					Some(h) => {
						if let Some(addref) = h.addref {
							// SAFETY: the handle is live.
							unsafe { addref(h.ctx) };
						}
						h
					}
					None => CHandle::null(),
				}
			}
			_ => CHandle::null(),
		}
	}

	/// `oaktask_import_invalid_count` — the count the real import task
	/// reported.
	pub fn oaktask_import_invalid_count(t: CHandle) -> c_int {
		let Some(p) = task_payload(t) else {
			return oaktask::error::OAKTASK_E_INVALID;
		};
		match &p.kind {
			TaskKind::Import { invalid_count, .. } => {
				*invalid_count.lock().unwrap_or_else(|e| e.into_inner()) as c_int
			}
			_ => oaktask::error::OAKTASK_E_INVALID,
		}
	}

	/// `oaktask_import_invalid_at` — STUB: the invalid-file list is
	/// private inside `oaktask::project::import::ProjectImportTask` (only
	/// its count is exposed), so the per-file names are unwireable
	/// (documented; returns NOT_FOUND).
	pub fn oaktask_import_invalid_at(
		t: CHandle,
		_index: c_int,
		_buf: *mut c_char,
		_buf_size: c_int,
	) -> c_int {
		let Some(p) = task_payload(t) else {
			return oaktask::error::OAKTASK_E_INVALID;
		};
		match &p.kind {
			TaskKind::Import { .. } => oaktask::error::OAKTASK_E_NOT_FOUND,
			_ => oaktask::error::OAKTASK_E_INVALID,
		}
	}

	/// `oaktask_create_project_load_otio` — an OTIO interchange load.
	pub fn oaktask_create_project_load_otio(filename: *const c_char) -> CHandle {
		if filename.is_null() {
			return CHandle::null();
		}
		// SAFETY: the caller guarantees a valid NUL-terminated string.
		let filename = unsafe { crate::handle::read_cstr(filename) };
		let title = format!("Loading '{filename}'");
		let result = Arc::new(Mutex::new(None));
		let mut driver = base_task(&title);
		let inner_base = base_task(&title);
		let inner = oaktask::project::loadotio::LoadOTIOTask {
			base: oaktask::project::load::ProjectLoadBaseTask::new(inner_base, filename),
		};
		driver.set_behavior(Box::new(OtioLoadTaskBehavior {
			inner,
			result: result.clone(),
		}));
		box_task(driver, TaskKind::Load { result })
	}

	/// The engine-side wrapper behavior for OTIO loads.
	struct OtioLoadTaskBehavior {
		inner: oaktask::project::loadotio::LoadOTIOTask,
		result: Arc<Mutex<Option<Arc<Mutex<Project>>>>>,
	}

	impl oaktask::task::TaskBehavior for OtioLoadTaskBehavior {
		fn run(&mut self, task: &mut oaktask::task::Task) -> oaktask::error::Result<()> {
			self.inner.run(task)?;
			if let Ok(project) = self.inner.base.take_project() {
				*self.result.lock().unwrap_or_else(|e| e.into_inner()) = Some(project);
			}
			Ok(())
		}
	}

	/// `oaktask_load_otio_take_project` — take the project a successful
	/// OTIO load produced.
	pub fn oaktask_load_otio_take_project(t: CHandle) -> CHandle {
		oaktask_load_take_project(t)
	}

	/// `oaktask_create_project_save_otio` — an OTIO interchange save.
	pub fn oaktask_create_project_save_otio(project: CHandle, filename: *const c_char) -> CHandle {
		if filename.is_null() {
			return CHandle::null();
		}
		// SAFETY: the caller guarantees a valid NUL-terminated string.
		let filename = unsafe { crate::handle::read_cstr(filename) };
		// SAFETY: project handles box ProjectArc payloads.
		let project_ref = match unsafe { crate::handle::domain::project_of(&project) }.cloned() {
			Some(p) => p,
			None => return CHandle::null(),
		};
		let mut driver = base_task("Saving project...");
		let inner_base = base_task("Saving project...");
		let inner = oaktask::project::saveotio::SaveOTIOTask {
			base: inner_base,
			project: project_ref,
			filename,
		};
		driver.set_behavior(Box::new(inner));
		box_task(driver, TaskKind::Plain)
	}

	/// `oaktask_load_otio_set_confirm_cb` — register the global OTIO
	/// import-confirmation callback (real oaktask API).
	pub fn oaktask_load_otio_set_confirm_cb(
		cb: Option<OakTaskOtioImportConfirmFn>,
		userdata: *mut c_void,
	) {
		let userdata_ptr = userdata as usize;
		let mapped = cb.map(|cb| {
			Box::new(move |names: &[String]| -> bool {
				let cnames: Vec<std::ffi::CString> = names
					.iter()
					.map(|n| std::ffi::CString::new(n.as_str()).unwrap_or_default())
					.collect();
				let ptrs: Vec<*const c_char> =
					cnames.iter().map(|c| c.as_ptr() as *const c_char).collect();
				// SAFETY: the callback + userdata follow the caller's
				// contract.
				unsafe { cb(ptrs.as_ptr(), cnames.len() as c_int, userdata_ptr as *mut c_void) != 0 }
			}) as oaktask::project::loadotio::ImportConfirmFn
		});
		oaktask::project::loadotio::set_import_confirm_callback(mapped);
	}

	/// `oaktask_create_precache` — a footage precache task.
	pub fn oaktask_create_precache(footage: CHandle, index: c_int, sequence: CHandle) -> CHandle {
		// SAFETY: node handles box oaknode NodeRef payloads.
		let footage_ref = match unsafe { crate::handle::domain::node_ref_of(&footage) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return CHandle::null(),
		};
		// SAFETY: node handles box oaknode NodeRef payloads.
		let sequence_ref = match unsafe { crate::handle::domain::node_ref_of(&sequence) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return CHandle::null(),
		};
		let inner = oaktask::precache::PreCacheTask::new(footage_ref, index, sequence_ref);
		let mut driver = base_task("Precaching footage...");
		driver.set_behavior(Box::new(inner));
		box_task(driver, TaskKind::Plain)
	}

	/// `oaktask_create_export` — an export task over the real
	/// `ExportTask`.
	pub fn oaktask_create_export(
		viewer: CHandle,
		color_manager: CHandle,
		params: *const EncodingParamsPOD,
	) -> CHandle {
		if params.is_null() {
			return CHandle::null();
		}
		// SAFETY: node handles box oaknode NodeRef payloads.
		let viewer_ref = match unsafe { crate::handle::domain::node_ref_of(&viewer) } {
			Some(nr) => (nr.project.clone(), nr.id),
			None => return CHandle::null(),
		};
		// SAFETY: the caller passes a live POD for the call duration.
		let pod = unsafe { &*params };
		let filename = pod
			.filename
			.iter()
			.take_while(|&&b| b != 0)
			.map(|&b| b as char)
			.collect::<String>();
		let encoding = oaktask::export::EncodingParams {
			filename,
			format: pod.format,
			video_enabled: pod.video_enabled != 0,
			video_codec: pod.video_codec,
			video_width: pod.video_width,
			video_height: pod.video_height,
			video_time_base_num: pod.video_time_base_num,
			video_time_base_den: pod.video_time_base_den,
			video_pixel_format: pod.video_pixel_format as i32,
			audio_enabled: pod.audio_enabled != 0,
			audio_codec: pod.audio_codec,
			audio_sample_rate: pod.audio_sample_rate,
			audio_channel_layout: pod.audio_channel_layout,
			subtitles_enabled: pod.subtitles_enabled != 0,
			export_length_num: pod.export_length_num,
			export_length_den: pod.export_length_den,
			has_custom_range: pod.has_custom_range != 0,
			custom_range_in_num: pod.custom_range_in_num as i32,
			custom_range_in_den: pod.custom_range_in_den as i32,
			custom_range_out_num: pod.custom_range_out_num as i32,
			custom_range_out_den: pod.custom_range_out_den as i32,
		};
		let _ = color_manager; // the domain ExportTask dropped the manager slot
		let inner = oaktask::export::ExportTask::new(viewer_ref, encoding);
		let mut driver = base_task("Exporting...");
		driver.set_behavior(Box::new(inner));
		box_task(driver, TaskKind::Plain)
	}

	/// `oaktask_import_set_image_sequence_confirm_cb` — register the
	/// process-wide image-sequence confirmation callback.
	pub fn oaktask_import_set_image_sequence_confirm_cb(
		cb: Option<OakTaskImageSequenceConfirmFn>,
		userdata: *mut c_void,
	) {
		*IMAGE_SEQ_CONFIRM
			.get_or_init(|| Mutex::new(None))
			.lock()
			.unwrap_or_else(|e| e.into_inner()) = cb.map(|cb| (cb, userdata as usize));
	}
}
