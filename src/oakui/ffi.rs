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

//! The app's pure-C surface of the `liboakengine` dylib.
//!
//! The app links the built `liboakengine.dylib` (see `build.rs`) and calls
//! ONLY its `oakengine_*` C ABI — it never depends on the `oakengine` crate
//! as an rlib. This module declares every exported function the real engine
//! binding uses, with the exact signatures from the facade's `#[no_mangle]`
//! exports (`crates/oakengine/src/*.rs`), plus the two `oaktask_*` module
//! exports the dylib carries alongside the facade (interchange
//! load/save getter and the task event subscription, see the comments
//! below).
//!
//! The facade also exports the module C ABIs (`oakundo_*`, `oakcommon_*`,
//! ...) inside the same dylib; the module functions the app needs beyond
//! the facade's wrapping (`oaktask_load_take_project`,
//! `oaktask_task_subscribe`) are declared here too and resolve from the
//! dylib.
//!
//! # Handle layout mirrors
//!
//! The facade's opaque `OakEngine*` handle types are thin `#[repr(C)]`
//! newtypes around one module [`CHandle`] value (see
//! `crates/oakengine/src/handle.rs`), and boxes created with
//! `box_handle`/`free_box` live in the heap. The dylib ABI passes those
//! boxes as opaque pointers, but a pure-C consumer that needs to (a) build
//! a project box from a module handle (interchange load) or (b) free a
//! borrowed handle box (sequences / clips the facade returns but has no
//! `oakengine_*_free` for) must know the box layout. The mirrors below
//! reproduce it exactly (identical `repr(C)` field layout), so boxes
//! created by the facade can be read/freed from the app and vice versa.
//!
//! The module handle type itself is the frozen `{ctx, addref, release,
//! abi_version}` value handle (`include/common/handle.h`); `release` is
//! what `free_box` calls before deallocating the box.

use std::ffi::{c_char, c_int, c_void};

/// The module value handle (`{ctx, addref, release, abi_version}`), mirror
/// of `oakcore_rs::handle::CHandle` / `include/common/handle.h`.
#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct CHandle {
	/// Opaque refcounted box pointer.
	pub ctx: *mut c_void,
	/// Atomic increment.
	pub addref: Option<unsafe extern "C" fn(*mut c_void)>,
	/// Atomic decrement; destroys at zero.
	pub release: Option<unsafe extern "C" fn(*mut c_void)>,
	/// ABI version.
	pub abi_version: u32,
}

impl CHandle {
	/// Whether this is the empty (zero) handle.
	pub fn is_null(&self) -> bool {
		self.ctx.is_null()
	}
}

/// Opaque engine handle boxes, mirroring the facade's `engine_handle!`
/// newtypes (one `CHandle` per box). Only the types the app actually
/// touches are declared.
macro_rules! engine_handle {
	($($name:ident),* $(,)?) => {
		$(
			/// Opaque engine handle: a `#[repr(C)]` box holding one module
			/// [`CHandle`].
			#[repr(C)]
			#[derive(Clone, Copy)]
			pub struct $name {
				/// The wrapped module handle.
				pub handle: CHandle,
			}

			impl HandleBox for $name {
				fn boxed_new(handle: CHandle) -> Self {
					$name { handle }
				}
				fn handle(&self) -> CHandle {
					self.handle
				}
			}
		)*
	};
}

engine_handle! {
	OakEngineClip,
	OakEngineEncodingParams,
	OakEngineProject,
	OakEngineSequence,
	OakEngineTask,
}

/// Uniform construction/extraction surface of the engine opaque boxes.
pub trait HandleBox: Sized {
	/// Build the box from a module handle.
	fn boxed_new(handle: CHandle) -> Self;
	/// Extract the wrapped module handle (copy).
	fn handle(&self) -> CHandle;
}

/// Allocate a heap box for a module handle and return its raw pointer.
/// The box must later be released with [`free_box`] or a consuming
/// `oakengine_*_free` export.
///
/// # Safety
/// The handle must be a live module handle (e.g. from
/// `oaktask_load_take_project`).
pub unsafe fn box_handle<T: HandleBox>(handle: CHandle) -> *mut T {
	// SAFETY: the caller passes a live handle; the box is managed by the
	// C ABI consumers from here on.
	Box::into_raw(Box::new(T::boxed_new(handle)))
}

/// Dereference an engine opaque box and copy out its module handle.
/// Returns `None` for a NULL pointer or an empty handle.
///
/// # Safety
/// `ptr` must point to a live box created by [`box_handle`] or by the
/// facade (or be NULL).
pub unsafe fn unbox<T: HandleBox>(ptr: *const T) -> Option<CHandle> {
	// SAFETY: see the function docs.
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

/// Free a box created by [`box_handle`] (or returned by the facade):
/// release the module handle (via its `release` function pointer) and
/// deallocate the box. NULL and empty handles are no-ops. After the call
/// `ptr` is dangling; the caller must not use it again.
///
/// # Safety
/// `ptr` must be a pointer previously returned by [`box_handle`] or by the
/// facade (or NULL) and must not be freed twice.
pub unsafe fn free_box<T: HandleBox>(ptr: *mut T) {
	// SAFETY: see the function docs.
	if ptr.is_null() {
		return;
	}
	let handle = (*ptr).handle();
	if let Some(release) = handle.release {
		release(handle.ctx);
	}
	drop(Box::from_raw(ptr));
}

/// `engine/include/oakengine/videoparams.h` — POD mirror of VideoParams'
/// user-facing fields (Rust mirror of `oak_video_params`; the facade's
/// `oakengine::common::OakVideoParamsPod`).
#[repr(C)]
#[derive(Clone, Copy)]
pub struct OakVideoParamsPod {
	/// Width.
	pub width: c_int,
	/// Height.
	pub height: c_int,
	/// Frame duration numerator (e.g. 1001/30000 s).
	pub time_base_num: c_int,
	/// Frame duration denominator.
	pub time_base_den: c_int,
	/// PixelFormat::Format value.
	pub format: c_int,
	/// Pixel aspect numerator.
	pub pixel_aspect_num: c_int,
	/// Pixel aspect denominator.
	pub pixel_aspect_den: c_int,
	/// Interlacing value.
	pub interlacing: c_int,
	/// ColorRange value.
	pub color_range: c_int,
	/// Preview resolution divider (1 = full).
	pub divider: c_int,
	/// VideoParams::Type value.
	pub video_type: c_int,
	/// 0/1 premultiplied alpha.
	pub premultiplied_alpha: c_int,
}

/// The task event callback the module subscription invokes on the task's
/// own thread (`oaktask` C ABI, `include/task/task.h`).
pub type OakTaskEventFn = unsafe extern "C" fn(event_id: c_int, value: f64, userdata: *mut c_void);

// The `oakengine_*` C ABI surface the app binds. Signatures mirror the
// facade's `#[no_mangle] pub extern "C"` exports verbatim (the facade
// declares a few of them without `unsafe`; calling any extern-block item
// still requires an unsafe context on the current toolchain, so the call
// sites in `real.rs` carry their own `unsafe` blocks).
//
// String outputs follow the engine buf/size convention: the return value
// is the required length excluding the terminating NUL; negative values
// are error codes.
#[link(name = "oakengine")]
unsafe extern "C" {
	// -- oakengine::codec (encoding formats + params) --

	/// `oakengine_encoding_format_count` — number of export formats.
	pub fn oakengine_encoding_format_count() -> c_int;
	/// `oakengine_encoding_format_name` (buf/size; -1 invalid).
	pub fn oakengine_encoding_format_name(format: c_int, buf: *mut c_char, buf_size: c_int) -> c_int;
	/// `oakengine_encoding_format_extension` (buf/size).
	pub fn oakengine_encoding_format_extension(
		format: c_int,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int;
	/// `oakengine_encoding_format_video_codec_at` — codec id, -1 invalid.
	pub fn oakengine_encoding_format_video_codec_at(format: c_int, index: c_int) -> c_int;
	/// `oakengine_encoding_format_audio_codec_at` — codec id, -1 invalid.
	pub fn oakengine_encoding_format_audio_codec_at(format: c_int, index: c_int) -> c_int;
	/// `oakengine_encoding_params_create` — owned params box.
	pub fn oakengine_encoding_params_create() -> *mut OakEngineEncodingParams;
	/// `oakengine_encoding_params_destroy` — consuming free.
	pub fn oakengine_encoding_params_destroy(params: *mut OakEngineEncodingParams);
	/// `oakengine_encoding_params_set_filename`.
	pub fn oakengine_encoding_params_set_filename(
		params: *mut OakEngineEncodingParams,
		filename: *const c_char,
	) -> c_int;
	/// `oakengine_encoding_params_set_format` — rejects out-of-range values.
	pub fn oakengine_encoding_params_set_format(
		params: *mut OakEngineEncodingParams,
		format: c_int,
	) -> c_int;
	/// `oakengine_encoding_params_enable_video` — copy the POD-carryable
	/// fields of `video` and enable the video track.
	pub fn oakengine_encoding_params_enable_video(
		params: *mut OakEngineEncodingParams,
		video: *const OakVideoParamsPod,
		codec: c_int,
	) -> c_int;
	/// `oakengine_encoding_params_enable_audio`.
	pub fn oakengine_encoding_params_enable_audio(
		params: *mut OakEngineEncodingParams,
		sample_rate: c_int,
		channel_layout: u64,
		sample_format: c_int,
		codec: c_int,
	) -> c_int;
	/// `oakengine_encoding_params_set_export_length`.
	pub fn oakengine_encoding_params_set_export_length(
		params: *mut OakEngineEncodingParams,
		num: c_int,
		den: c_int,
	);

	// -- oakengine::common (config) --

	/// `oakengine_config_get_string` — read a config string; a missing key
	/// reads as an empty string.
	pub fn oakengine_config_get_string(key: *const c_char, buf: *mut c_char, buf_size: c_int) -> c_int;
	/// `oakengine_config_set_string` — write a string value.
	pub fn oakengine_config_set_string(key: *const c_char, value: *const c_char) -> c_int;

	// -- oakengine::node (project) --

	/// `oakengine_project_create` — owned project box (no content yet).
	pub fn oakengine_project_create() -> *mut OakEngineProject;
	/// `oakengine_project_free` — consuming free of an owned project box.
	pub fn oakengine_project_free(self_: *mut OakEngineProject);
	/// `oakengine_project_new` — initialize a blank project.
	pub fn oakengine_project_new(self_: *mut OakEngineProject) -> c_int;
	/// `oakengine_project_load` — load from `path`; fills `err` (buf/size)
	/// on failure.
	pub fn oakengine_project_load(
		self_: *mut OakEngineProject,
		path: *const c_char,
		err: *mut c_char,
		err_size: c_int,
	) -> c_int;
	/// `oakengine_project_save` — save to `path` (or the recorded
	/// filename when NULL).
	pub fn oakengine_project_save(self_: *mut OakEngineProject, path: *const c_char) -> c_int;
	/// `oakengine_project_is_modified`.
	pub fn oakengine_project_is_modified(self_: *const OakEngineProject) -> c_int;
	/// `oakengine_project_name` (buf/size).
	pub fn oakengine_project_name(
		self_: *const OakEngineProject,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int;
	/// `oakengine_project_filename` (buf/size).
	pub fn oakengine_project_filename(
		self_: *const OakEngineProject,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int;
	/// `oakengine_project_footage_count`.
	pub fn oakengine_project_footage_count(self_: *const OakEngineProject) -> c_int;
	/// `oakengine_project_footage_filename` (buf/size).
	pub fn oakengine_project_footage_filename(
		self_: *const OakEngineProject,
		index: c_int,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int;
	/// `oakengine_project_can_undo`.
	pub fn oakengine_project_can_undo(self_: *const OakEngineProject) -> c_int;
	/// `oakengine_project_can_redo`.
	pub fn oakengine_project_can_redo(self_: *const OakEngineProject) -> c_int;
	/// `oakengine_project_undo`.
	pub fn oakengine_project_undo(self_: *mut OakEngineProject) -> c_int;
	/// `oakengine_project_redo`.
	pub fn oakengine_project_redo(self_: *mut OakEngineProject) -> c_int;
	/// `oakengine_project_sequence_count`.
	pub fn oakengine_project_sequence_count(self_: *const OakEngineProject) -> c_int;
	/// `oakengine_project_sequence_at` — borrowed sequence box (free with
	/// [`free_box`]).
	pub fn oakengine_project_sequence_at(
		self_: *const OakEngineProject,
		index: c_int,
	) -> *mut OakEngineSequence;
	/// `oakengine_project_set_filename`.
	pub fn oakengine_project_set_filename(self_: *mut OakEngineProject, path: *const c_char) -> c_int;
	/// `oakengine_project_node_count` — parseable content check.
	pub fn oakengine_project_node_count(self_: *const OakEngineProject) -> c_int;

	// -- oakengine::task --

	/// `oakengine_task_error` (buf/size).
	pub fn oakengine_task_error(task: *mut OakEngineTask, buf: *mut c_char, buf_size: c_int) -> c_int;
	/// `oakengine_task_cancel` — set the task's cancel atom.
	pub fn oakengine_task_cancel(task: *mut OakEngineTask) -> c_int;
	/// `oakengine_task_start_sync` — run the task to completion.
	pub fn oakengine_task_start_sync(task: *mut OakEngineTask) -> c_int;
	/// `oakengine_task_free` — consuming free of an owned task box.
	pub fn oakengine_task_free(task: *mut OakEngineTask) -> c_int;
	/// `oakengine_task_create_project_load_otio` — owned interchange load
	/// task box.
	pub fn oakengine_task_create_project_load_otio(filename: *const c_char) -> *mut OakEngineTask;
	/// `oakengine_task_create_project_save_otio` — owned interchange save
	/// task box.
	pub fn oakengine_task_create_project_save_otio(
		project: *mut OakEngineProject,
	) -> *mut OakEngineTask;
	/// `oakengine_task_create_export` — owned export task box.
	pub fn oakengine_task_create_export(
		sequence: *mut OakEngineSequence,
		params: *mut OakEngineEncodingParams,
	) -> *mut OakEngineTask;

	// -- oakengine::timeline --

	/// `oakengine_sequence_new` — in-memory sequence (facade scratch
	/// project).
	pub fn oakengine_sequence_new(
		project: *mut OakEngineProject,
		name: *const c_char,
	) -> *mut OakEngineSequence;
	/// `oakengine_sequence_name` (buf/size).
	pub fn oakengine_sequence_name(
		self_: *const OakEngineSequence,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int;
	/// `oakengine_sequence_get_length` — length in seconds.
	pub fn oakengine_sequence_get_length(self_: *const OakEngineSequence, seconds: *mut f64) -> c_int;
	/// `oakengine_sequence_get_frame_rate` — num/den rational.
	pub fn oakengine_sequence_get_frame_rate(
		self_: *const OakEngineSequence,
		num: *mut c_int,
		den: *mut c_int,
	) -> c_int;
	/// `oakengine_sequence_get_video_params` — width/height/par.
	pub fn oakengine_sequence_get_video_params(
		self_: *const OakEngineSequence,
		width: *mut c_int,
		height: *mut c_int,
		par_num: *mut c_int,
		par_den: *mut c_int,
	) -> c_int;
	/// `oakengine_sequence_track_count` — per-type counts.
	pub fn oakengine_sequence_track_count(
		self_: *const OakEngineSequence,
		video: *mut c_int,
		audio: *mut c_int,
		subtitle: *mut c_int,
	) -> c_int;
	/// `oakengine_sequence_set_playhead`.
	pub fn oakengine_sequence_set_playhead(self_: *mut OakEngineSequence, timestamp: i64) -> c_int;
	/// `oakengine_sequence_add_track`.
	pub fn oakengine_sequence_add_track(self_: *mut OakEngineSequence, track_type: c_int) -> c_int;
	/// `oakengine_sequence_clip_count`.
	pub fn oakengine_sequence_clip_count(
		self_: *mut OakEngineSequence,
		track_type: c_int,
		track_index: c_int,
	) -> c_int;
	/// `oakengine_sequence_clip_at` — borrowed clip box (free with
	/// [`free_box`]).
	pub fn oakengine_sequence_clip_at(
		self_: *mut OakEngineSequence,
		track_type: c_int,
		track_index: c_int,
		clip_index: c_int,
	) -> *mut OakEngineClip;
	/// `oakengine_clip_get_range` — clip timeline range and media in-point
	/// as frame timestamps.
	pub fn oakengine_clip_get_range(
		self_: *const OakEngineClip,
		in_: *mut i64,
		out: *mut i64,
		media_in: *mut i64,
	) -> c_int;
	/// `oakengine_sequence_split_clip`.
	pub fn oakengine_sequence_split_clip(
		seq: *mut OakEngineSequence,
		track_type: c_int,
		track_index: c_int,
		clip_index: c_int,
		time: i64,
	) -> c_int;
	/// `oakengine_sequence_ripple_delete_clip`.
	pub fn oakengine_sequence_ripple_delete_clip(
		seq: *mut OakEngineSequence,
		track_type: c_int,
		track_index: c_int,
		clip_index: c_int,
	) -> c_int;
	/// `oakengine_clip_trim` — change the clip's timeline range.
	pub fn oakengine_clip_trim(clip: *mut OakEngineClip, new_in: i64, new_out: i64) -> c_int;
	/// `oakengine_sequence_delete_clips` — delete a clip array, optionally
	/// rippling; `rippled` reports the ripple length.
	pub fn oakengine_sequence_delete_clips(
		seq: *mut OakEngineSequence,
		clips: *mut *mut OakEngineClip,
		clip_count: c_int,
		ripple: c_int,
		ripple_ranges_ts: *const i64,
		ripple_range_count: c_int,
		rippled: *mut c_int,
	) -> c_int;
	/// `oakengine_sequence_remove_track`.
	pub fn oakengine_sequence_remove_track(
		seq: *mut OakEngineSequence,
		track_type: c_int,
		track_index: c_int,
	) -> c_int;
	/// `oakengine_track_get_height` — height in internal units.
	pub fn oakengine_track_get_height(
		seq: *const OakEngineSequence,
		track_type: c_int,
		track_index: c_int,
		height: *mut f64,
	) -> c_int;
	/// `oakengine_track_set_height` — height in internal units.
	pub fn oakengine_track_set_height(
		seq: *mut OakEngineSequence,
		track_type: c_int,
		track_index: c_int,
		height: f64,
	) -> c_int;

	/// `oakengine_track_height_internal_to_pixels`.
	pub fn oakengine_track_height_internal_to_pixels(height: f64) -> c_int;
	/// `oakengine_track_height_pixels_to_internal`.
	pub fn oakengine_track_height_pixels_to_internal(pixels: c_int) -> f64;

	// -- oaktask module C ABI (carried by the dylib) --

	/// `oaktask_load_take_project` — take the project an interchange
	/// load/load-otio task produced (ownership moves to the caller).
	pub fn oaktask_load_take_project(t: CHandle) -> CHandle;
	/// `oaktask_task_subscribe` — register the task event callback
	/// (`OAKTASK_EVENT_STARTED`=0, `OAKTASK_EVENT_PROGRESS`=1,
	/// `OAKTASK_EVENT_FINISHED`=2).
	pub fn oaktask_task_subscribe(
		t: CHandle,
		cb: Option<OakTaskEventFn>,
		userdata: *mut c_void,
	) -> i64;
}
