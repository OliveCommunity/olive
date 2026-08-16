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
//! exports (`crates/oakengine/src/*.rs`). The facade wraps the module
//! C ABIs that are also embedded in the same dylib (`oakundo_*`,
//! `oakcommon_*`, ...), so everything the app touches resolves through an
//! `oakengine_*` symbol.
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
	OakEngineAudioBuffer,
	OakEngineClip,
	OakEngineEncodingParams,
	OakEngineFootage,
	OakEngineFrame,
	OakEngineNode,
	OakEngineProject,
	OakEngineRenderer,
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
/// The handle must be a live module handle (e.g. from a facade export that
/// hands one over for the app to box).
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
	pub fn oakengine_encoding_format_name(
		format: c_int,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int;
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
	/// `oakengine_encoding_params_set_custom_range` — in/out export range as
	/// seconds rationals (the work-area export; the task renders exactly
	/// `[in, out)`).
	pub fn oakengine_encoding_params_set_custom_range(
		params: *mut OakEngineEncodingParams,
		in_num: i64,
		in_den: i64,
		out_num: i64,
		out_den: i64,
	);

	// -- oakengine::common (config) --

	/// `oakengine_config_get_string` — read a config string; a missing key
	/// reads as an empty string.
	pub fn oakengine_config_get_string(
		key: *const c_char,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int;
	/// `oakengine_config_set_string` — write a string value.
	pub fn oakengine_config_set_string(key: *const c_char, value: *const c_char) -> c_int;

	// -- oakengine::storage (write-through session state) --

	/// `oakengine_storage_flush` — flush every bound project and stop the
	/// snapshot thread (the app calls it on exit).
	pub fn oakengine_storage_flush() -> c_int;
	/// `oakengine_storage_is_bound` — 1 when the project is bound to a
	/// library session.
	pub fn oakengine_storage_is_bound(project: *mut OakEngineProject) -> c_int;
	/// `oakengine_storage_last_error` — the last write-through / snapshot
	/// error (buf/size; empty when none or not bound).
	pub fn oakengine_storage_last_error(
		project: *mut OakEngineProject,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int;

	// -- oakengine::library (project manager, M13 D4) --

	/// `oakengine_library_list` — the library rows as a JSON array
	/// (buf/size), most recently modified first; `"[]"` with storage off.
	pub fn oakengine_library_list(buf: *mut c_char, buf_size: c_int) -> c_int;
	/// `oakengine_library_create` — create a blank project row; reports its
	/// uuid (buf/size on `out_uuid`; the return value is the uuid length,
	/// negative on error).
	pub fn oakengine_library_create(
		name: *const c_char,
		out_uuid: *mut c_char,
		out_size: c_int,
	) -> c_int;
	/// `oakengine_library_delete` — delete a row by uuid.
	pub fn oakengine_library_delete(uuid: *const c_char) -> c_int;
	/// `oakengine_library_rename` — rename a row.
	pub fn oakengine_library_rename(uuid: *const c_char, name: *const c_char) -> c_int;
	/// `oakengine_library_duplicate` — copy a row (history included);
	/// reports the new uuid like `oakengine_library_create`.
	pub fn oakengine_library_duplicate(
		uuid: *const c_char,
		name: *const c_char,
		out_uuid: *mut c_char,
		out_size: c_int,
	) -> c_int;
	/// `oakengine_library_import` — import a `.ove`/`.otio`/`.fcpxml` file
	/// as a new row; reports the new uuid like `oakengine_library_create`.
	pub fn oakengine_library_import(
		path: *const c_char,
		out_uuid: *mut c_char,
		out_size: c_int,
	) -> c_int;
	/// `oakengine_library_export` — export a row to `path` (format by
	/// extension).
	pub fn oakengine_library_export(uuid: *const c_char, path: *const c_char) -> c_int;
	/// `oakengine_project_load_library` — load a library row into a fresh
	/// project shell (same contract as `oakengine_project_load`); binds the
	/// project to the library session.
	pub fn oakengine_project_load_library(
		self_: *mut OakEngineProject,
		uuid: *const c_char,
		err: *mut c_char,
		err_size: c_int,
	) -> c_int;

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
	/// `oakengine_project_save` — write the project to `path` (or the
	/// recorded filename when NULL). Legacy manual-save ABI, now used only
	/// as the .ove export path (导出工程文件…); the write-through library is
	/// the primary persistence.
	pub fn oakengine_project_save(self_: *mut OakEngineProject, path: *const c_char) -> c_int;
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
	/// `oakengine_project_import_footage` — probe and add to the root
	/// folder; owned footage box (free with `oakengine_footage_free`).
	pub fn oakengine_project_import_footage(
		self_: *mut OakEngineProject,
		path: *const c_char,
	) -> *mut OakEngineFootage;
	/// `oakengine_footage_free` — release a footage handle.
	pub fn oakengine_footage_free(self_: *mut OakEngineFootage);
	/// `oakengine_footage_last_error` — last probe/import error on this
	/// thread (two-stage buf/size getter; empty when the last call
	/// succeeded).
	pub fn oakengine_footage_last_error(buf: *mut c_char, buf_size: c_int) -> c_int;
	/// `oakengine_sequence_add_footage_clip_ex` — place a clip of
	/// `footage` on the track, skipping the unenforceable same-project
	/// check (sequences live in their own scratch project — documented
	/// module deviation; M12 P0 montage path). Owned clip box.
	pub fn oakengine_sequence_add_footage_clip_ex(
		self_: *mut OakEngineSequence,
		footage: *mut OakEngineFootage,
		track_type: c_int,
		track_index: c_int,
		in_: i64,
		out: i64,
		media_in: i64,
	) -> *mut OakEngineClip;
	/// `oakengine_project_footage_at` — boxed footage node at `index` (free
	/// with `oakengine_node_free`); NULL for an invalid index.
	pub fn oakengine_project_footage_at(
		self_: *const OakEngineProject,
		index: c_int,
	) -> *mut OakEngineNode;
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
	pub fn oakengine_project_set_filename(
		self_: *mut OakEngineProject,
		path: *const c_char,
	) -> c_int;
	/// `oakengine_project_node_count` — parseable content check.
	pub fn oakengine_project_node_count(self_: *const OakEngineProject) -> c_int;
	/// `oakengine_project_node_at` — boxed project node at `index` (free
	/// with `oakengine_node_free`); NULL for an invalid index.
	pub fn oakengine_project_node_at(
		self_: *const OakEngineProject,
		index: c_int,
	) -> *mut OakEngineNode;
	/// `oakengine_project_root` — the project's root folder node (boxed,
	/// free with `oakengine_node_free`).
	pub fn oakengine_project_root(self_: *const OakEngineProject) -> *mut OakEngineNode;

	/// `oakengine_folder_item_child_count` — direct children of a folder
	/// node.
	pub fn oakengine_folder_item_child_count(folder: *const OakEngineNode) -> c_int;
	/// `oakengine_folder_item_child` — boxed child at `index` (free with
	/// `oakengine_node_free`); NULL for an invalid index.
	pub fn oakengine_folder_item_child(
		folder: *const OakEngineNode,
		index: c_int,
	) -> *mut OakEngineNode;
	/// `oakengine_node_get_name` (buf/size).
	pub fn oakengine_node_get_name(
		self_: *const OakEngineNode,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int;
	/// `oakengine_node_get_label` (buf/size).
	pub fn oakengine_node_get_label(
		self_: *const OakEngineNode,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int;
	/// `oakengine_node_input_count`.
	pub fn oakengine_node_input_count(self_: *const OakEngineNode) -> c_int;
	/// `oakengine_node_input_id` (buf/size) at `index`.
	pub fn oakengine_node_input_id(
		self_: *const OakEngineNode,
		index: c_int,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int;
	/// `oakengine_node_input_is_connected` — 1 when the input has an edge.
	pub fn oakengine_node_input_is_connected(
		self_: *const OakEngineNode,
		input_id: *const c_char,
	) -> c_int;
	/// `oakengine_node_output_connection_count`.
	pub fn oakengine_node_output_connection_count(self_: *const OakEngineNode) -> c_int;
	/// `oakengine_node_output_connection_at_ex` — boxed input node + input
	/// id (free the boxed node with `oakengine_node_free`).
	pub fn oakengine_node_output_connection_at_ex(
		self_: *const OakEngineNode,
		index: c_int,
		input_node: *mut *mut OakEngineNode,
		input_id_buf: *mut c_char,
		input_id_size: c_int,
		element: *mut c_int,
		hidden: *mut c_int,
	) -> c_int;
	/// `oakengine_node_get_context_position` — graph position of `node`
	/// in `context`'s position map.
	pub fn oakengine_node_get_context_position(
		context: *const OakEngineNode,
		node: *const OakEngineNode,
		x: *mut f64,
		y: *mut f64,
		expanded: *mut c_int,
	) -> c_int;
	/// `oakengine_node_set_context_position` — undoable graph move.
	pub fn oakengine_node_set_context_position(
		context: *mut OakEngineNode,
		node: *mut OakEngineNode,
		x: f64,
		y: f64,
	) -> c_int;
	/// `oakengine_node_connect` — undoable edge from `output_node` into
	/// `input_node`'s `input_id`.
	pub fn oakengine_node_connect(
		output_node: *mut OakEngineNode,
		input_node: *mut OakEngineNode,
		input_id: *const c_char,
	) -> c_int;
	/// `oakengine_node_disconnect_ex` — undoable edge removal.
	pub fn oakengine_node_disconnect_ex(
		input_node: *mut OakEngineNode,
		input_id: *const c_char,
		element: c_int,
	) -> c_int;
	/// `oakengine_project_remove_node` — undoable node removal (edges
	/// disconnected).
	pub fn oakengine_project_remove_node(
		self_: *mut OakEngineProject,
		node: *mut OakEngineNode,
	) -> c_int;

	// -- oakengine::task --

	/// `oakengine_task_error` (buf/size).
	pub fn oakengine_task_error(
		task: *mut OakEngineTask,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int;
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
	pub fn oakengine_sequence_get_length(
		self_: *const OakEngineSequence,
		seconds: *mut f64,
	) -> c_int;
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
	/// `oakengine_sequence_last_error` — last editing error for this
	/// thread (two-stage string).
	pub fn oakengine_sequence_last_error(buf: *mut c_char, buf_size: c_int) -> c_int;
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
	/// `oakengine_sequence_move_clip_to_track` — move the clip to a
	/// different track (M12 P4, cross-track).
	pub fn oakengine_sequence_move_clip_to_track(
		seq: *mut OakEngineSequence,
		track_type: c_int,
		track_index: c_int,
		clip_index: c_int,
		dest_track_index: c_int,
		new_in: i64,
	) -> c_int;
	/// `oakengine_sequence_move_clip` — move the clip so its in point becomes
	/// `new_in` on the same track.
	pub fn oakengine_sequence_move_clip(
		seq: *mut OakEngineSequence,
		track_type: c_int,
		track_index: c_int,
		clip_index: c_int,
		new_in: i64,
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

	/// `oakengine_sequence_marker_count` — number of timeline markers
	/// (0 for NULL/invalid).
	pub fn oakengine_sequence_marker_count(self_: *const OakEngineSequence) -> c_int;
	/// `oakengine_sequence_marker_at` — marker at `index`: time as a frame
	/// timestamp (the sequence's timebase), name via the buf/size
	/// convention, and the color index.
	pub fn oakengine_sequence_marker_at(
		self_: *const OakEngineSequence,
		index: c_int,
		time: *mut i64,
		name: *mut c_char,
		name_size: c_int,
		color: *mut c_int,
	) -> c_int;
	/// `oakengine_sequence_marker_add` — undoable marker at `time_ts`.
	pub fn oakengine_sequence_marker_add(
		seq: *mut OakEngineSequence,
		time_ts: i64,
		name: *const c_char,
	) -> c_int;
	/// `oakengine_sequence_marker_remove` — undoable removal of the marker
	/// at `time_ts`.
	pub fn oakengine_sequence_marker_remove(seq: *mut OakEngineSequence, time_ts: i64) -> c_int;

	/// `oakengine_sequence_workarea_is_enabled` — 1 when the work area is
	/// enabled.
	pub fn oakengine_sequence_workarea_is_enabled(self_: *const OakEngineSequence) -> c_int;
	/// `oakengine_sequence_get_workarea` — work-area in/out as frame
	/// timestamps (the reset sentinel out when never set).
	pub fn oakengine_sequence_get_workarea(
		self_: *const OakEngineSequence,
		in_: *mut i64,
		out: *mut i64,
	) -> c_int;
	/// `oakengine_sequence_set_workarea` — set the enabled flag + range
	/// live (NOT undoable; the ruler-drag preview path).
	pub fn oakengine_sequence_set_workarea(
		self_: *mut OakEngineSequence,
		enabled: c_int,
		in_: i64,
		out: i64,
	) -> c_int;
	/// `oakengine_sequence_set_workarea_undoable` — set the enabled flag +
	/// range as ONE undoable entry ("Set Workarea"). `old_in`/`old_out` are
	/// the range before the change (the caller captured it, e.g. the
	/// drag-start range); the old enabled flag is captured by the engine.
	pub fn oakengine_sequence_set_workarea_undoable(
		self_: *mut OakEngineSequence,
		enabled: c_int,
		in_: i64,
		out: i64,
		old_in: i64,
		old_out: i64,
	) -> c_int;

	/// `oakengine_track_height_internal_to_pixels`.
	pub fn oakengine_track_height_internal_to_pixels(height: f64) -> c_int;
	/// `oakengine_track_height_pixels_to_internal`.
	pub fn oakengine_track_height_pixels_to_internal(pixels: c_int) -> f64;

	// -- oakengine::node (effect chain) --
	//
	// The effect-stack surface over a selected clip: convert the clip to its
	// node view, enumerate its effect chain (index 0 = closest to the
	// source), and edit the chain (insert / remove / reorder / enable
	// toggle), each edit packaged as an undoable facade command. Factory
	// enumeration feeds the "add effect" menu. Node boxes are freed with
	// `oakengine_node_free`; the clip box returned by
	// `oakengine_sequence_clip_at` is freed with [`free_box`].

	/// `oakengine_clip_as_node` — the clip's node view (borrowed box;
	/// freed with `oakengine_node_free`).
	pub fn oakengine_clip_as_node(self_: *const OakEngineClip) -> *mut OakEngineNode;
	/// `oakengine_sequence_as_node` — the sequence's node view (borrowed
	/// box; freed with `oakengine_node_free`). The node editor's graph
	/// output node and the context for its position map.
	pub fn oakengine_sequence_as_node(self_: *const OakEngineSequence) -> *mut OakEngineNode;
	/// `oakengine_sequence_node_count` — nodes in the sequence's owning
	/// project (its graph; 0 for NULL/invalid).
	pub fn oakengine_sequence_node_count(self_: *const OakEngineSequence) -> c_int;
	/// `oakengine_sequence_node_at` — boxed graph node at `index` (freed
	/// with `oakengine_node_free`); NULL for an invalid index or sequence.
	pub fn oakengine_sequence_node_at(
		self_: *const OakEngineSequence,
		index: c_int,
	) -> *mut OakEngineNode;
	/// `oakengine_sequence_remove_node` — undoable removal of `node` from
	/// the sequence's graph (the sequence node itself is protected).
	pub fn oakengine_sequence_remove_node(
		self_: *mut OakEngineSequence,
		node: *mut OakEngineNode,
	) -> c_int;
	/// `oakengine_clip_get_media_filename` — the clip's upstream footage
	/// filename (two-stage buf/size; M12 P4).
	pub fn oakengine_clip_get_media_filename(
		self_: *const OakEngineClip,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int;
	/// `oakengine_waveform_extract` — real waveform extraction (M12 P4):
	/// two-stage min/max extraction of `filename`'s audio stream.
	pub fn oakengine_waveform_extract(
		filename: *const c_char,
		stream_index: c_int,
		samples_per_point: c_int,
		out_pairs: *mut crate::oakui::waveform::MinMax,
		capacity_points: c_int,
		out_channel_count: *mut c_int,
	) -> c_int;


	/// `oakengine_node_effect_count` — the chain length (0 without effects).
	pub fn oakengine_node_effect_count(self_: *const OakEngineNode) -> c_int;
	/// `oakengine_node_effect_at` — the `index`-th effect (borrowed box;
	/// NULL out of range / no chain).
	pub fn oakengine_node_effect_at(
		self_: *const OakEngineNode,
		index: c_int,
	) -> *mut OakEngineNode;
	/// `oakengine_node_identity` — the node's stable identity (the stack's
	/// card id). 0 for NULL/invalid.
	pub fn oakengine_node_identity(self_: *const OakEngineNode) -> u64;
	/// `oakengine_node_is_enabled` — 1/0 (the stack's enable switch).
	pub fn oakengine_node_is_enabled(self_: *const OakEngineNode) -> c_int;
	/// `oakengine_node_effect_set_enabled` — undoable enable toggle.
	pub fn oakengine_node_effect_set_enabled(self_: *mut OakEngineNode, enabled: c_int) -> c_int;
	/// `oakengine_node_effect_insert` — undoable insert at `index`
	/// (0 = closest to the source; clamped to the ends).
	pub fn oakengine_node_effect_insert(
		self_: *mut OakEngineNode,
		index: c_int,
		type_id: *const c_char,
	) -> c_int;
	/// `oakengine_node_effect_remove` — undoable removal of `effect` from
	/// `self_`'s chain (the node is left orphaned in the project graph).
	pub fn oakengine_node_effect_remove(
		self_: *mut OakEngineNode,
		effect: *mut OakEngineNode,
	) -> c_int;
	/// `oakengine_node_effect_move` — undoable reorder of `effect` to
	/// `new_index` (post-removal insertion index, matching the effect
	/// stack's `ReorderRequested`).
	pub fn oakengine_node_effect_move(
		self_: *mut OakEngineNode,
		effect: *mut OakEngineNode,
		new_index: c_int,
	) -> c_int;
	/// `oakengine_node_get_effect_input` — the id of the input the effect
	/// chain attaches to (negative error when the node cannot host
	/// effects; `element` receives -1 for non-array inputs).
	pub fn oakengine_node_get_effect_input(
		self_: *const OakEngineNode,
		input_id: *mut c_char,
		input_id_size: c_int,
		element: *mut c_int,
	) -> c_int;
	/// `oakengine_node_get_type_id` — the node's factory type id (buf/size).
	pub fn oakengine_node_get_type_id(
		self_: *const OakEngineNode,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int;
	/// `oakengine_node_get_flags` — the node's flags bitmask (0 for
	/// NULL/invalid).
	pub fn oakengine_node_get_flags(self_: *const OakEngineNode) -> u64;
	/// `oakengine_node_free` — free a node box (NULL no-op).
	pub fn oakengine_node_free(node: *mut OakEngineNode);
	/// `oakengine_node_factory_id_count` — factory entry count.
	pub fn oakengine_node_factory_id_count() -> c_int;
	/// `oakengine_node_factory_id_at` — type id at `index` (buf/size;
	/// negative error out of range).
	pub fn oakengine_node_factory_id_at(index: c_int, buf: *mut c_char, buf_size: c_int) -> c_int;
	/// `oakengine_node_factory_name_from_id` — display name (buf/size).
	pub fn oakengine_node_factory_name_from_id(
		type_id: *const c_char,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int;
	/// `oakengine_node_factory_create_from_id` — owned node, not added.
	pub fn oakengine_node_factory_create_from_id(type_id: *const c_char) -> *mut OakEngineNode;
	/// `oakengine_node_flag_video_effect`.
	pub fn oakengine_node_flag_video_effect() -> u64;
	/// `oakengine_node_flag_dont_show_in_create_menu`.
	pub fn oakengine_node_flag_dont_show_in_create_menu() -> u64;

	// -- oakengine::render (CPU frame renderer) --
	//
	// The renderer binds a sequence handle to an output geometry; each
	// `render_frame` submits an oakrender ticket, waits for it and returns
	// the produced frame (F32 RGBA when created with pixel format 4). The
	// renderer box is opaque: it is freed ONLY with
	// `oakengine_renderer_free` (never [`free_box`] — the facade box is not
	// a module-handle box). Frames are freed with `oakengine_frame_free`.

	/// `oakengine_renderer_create` — owned renderer box (NULL on invalid
	/// arguments: NULL sequence, non-positive geometry/rate, or a pixel
	/// format outside 0..=4). `output_colorspace` may be NULL.
	pub fn oakengine_renderer_create(
		seq: *mut OakEngineSequence,
		width: c_int,
		height: c_int,
		pixel_format: c_int,
		frame_rate_num: c_int,
		frame_rate_den: c_int,
		output_colorspace: *const c_char,
	) -> *mut OakEngineRenderer;
	/// `oakengine_renderer_create_for_node` — like `oakengine_renderer_create`,
	/// but binds any node instead of a sequence: the surface for rendering a
	/// single footage node (the source monitor). Freed and rendered exactly
	/// like the sequence renderer.
	pub fn oakengine_renderer_create_for_node(
		node: *mut OakEngineNode,
		width: c_int,
		height: c_int,
		pixel_format: c_int,
		frame_rate_num: c_int,
		frame_rate_den: c_int,
		output_colorspace: *const c_char,
	) -> *mut OakEngineRenderer;
	/// `oakengine_renderer_free` — consuming free (NULL no-op).
	pub fn oakengine_renderer_free(self_: *mut OakEngineRenderer);
	/// `oakengine_renderer_render_frame` — synchronous render of the frame at
	/// `timestamp` (in the renderer's frame-rate units). NULL on failure
	/// (see `oakengine_renderer_last_error`).
	pub fn oakengine_renderer_render_frame(
		self_: *mut OakEngineRenderer,
		timestamp: i64,
	) -> *mut OakEngineFrame;
	/// `oakengine_renderer_last_error` (buf/size).
	pub fn oakengine_renderer_last_error(
		self_: *const OakEngineRenderer,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int;
	/// `oakengine_renderer_render_audio` — synchronously render
	/// `length_timestamp` frames of audio starting at `start_timestamp`
	/// (M12 P1). Owned `OakEngineAudioBuffer`.
	pub fn oakengine_renderer_render_audio(
		self_: *mut OakEngineRenderer,
		start_timestamp: i64,
		length_timestamp: i64,
	) -> *mut OakEngineAudioBuffer;
	/// `oakengine_audio_sample_rate` — the rendered buffer's rate (Hz).
	pub fn oakengine_audio_sample_rate(self_: *const OakEngineAudioBuffer) -> c_int;
	/// `oakengine_audio_channel_count`.
	pub fn oakengine_audio_channel_count(self_: *const OakEngineAudioBuffer) -> c_int;
	/// `oakengine_audio_sample_count` — interleaved frame count.
	pub fn oakengine_audio_sample_count(self_: *const OakEngineAudioBuffer) -> i64;
	/// `oakengine_audio_data` — interleaved f32 samples.
	pub fn oakengine_audio_data(
		self_: *const OakEngineAudioBuffer,
		channel: c_int,
	) -> *const f32;
	/// `oakengine_audio_free` — release the buffer.
	pub fn oakengine_audio_free(self_: *mut OakEngineAudioBuffer);
	/// `oakengine_audio_push_to_output` — queue interleaved samples
	/// described by `params` for playback (M12 P1).
	pub fn oakengine_audio_push_to_output(
		params: *const c_void,
		samples: *const c_char,
		samples_size: i64,
		error_buf: *mut c_char,
		error_buf_size: c_int,
	) -> c_int;
	/// `oakcore_audioparams_create` — new owned audio params (release
	/// with `oakcore_audioparams_free`).
	pub fn oakcore_audioparams_create(
		sample_rate: c_int,
		channel_layout: u64,
		format: c_int,
	) -> *mut c_void;
	/// `oakcore_audioparams_free` (NULL no-op).
	pub fn oakcore_audioparams_free(params: *mut c_void);
	/// `oakengine_render_manager_shutdown` — tear down the render manager
	/// (test/tooling; the app keeps it for the process lifetime).
	pub fn oakengine_render_manager_shutdown() -> c_int;
	/// `oakengine_testmedia_write_clip` — encode the known test pattern
	/// into `path` (test/tooling only; M12 P0).
	pub fn oakengine_testmedia_write_clip(
		path: *const c_char,
		width: c_int,
		height: c_int,
		frame_count: c_int,
		fps: c_int,
	) -> c_int;
	/// `oakengine_frame_width`.
	pub fn oakengine_frame_width(self_: *const OakEngineFrame) -> c_int;
	/// `oakengine_frame_height`.
	pub fn oakengine_frame_height(self_: *const OakEngineFrame) -> c_int;
	/// `oakengine_frame_format` — `PixelFormat::Format` (4 = F32).
	pub fn oakengine_frame_format(self_: *const OakEngineFrame) -> c_int;
	/// `oakengine_frame_linesize_bytes` — row stride in bytes.
	pub fn oakengine_frame_linesize_bytes(self_: *const OakEngineFrame) -> c_int;
	/// `oakengine_frame_data` — borrowed pixel data (valid until free).
	pub fn oakengine_frame_data(self_: *const OakEngineFrame) -> *const c_void;
	/// `oakengine_frame_free` — consuming free (NULL no-op).
	pub fn oakengine_frame_free(self_: *mut OakEngineFrame);

	// -- oakengine::audio --

	/// `oakengine_audio_output_levels` — per-channel linear peaks of the
	/// buffered output into `peaks` (up to `capacity` entries); returns
	/// the channel count (0 = nothing buffered), negative on error.
	pub fn oakengine_audio_output_levels(peaks: *mut f32, capacity: c_int) -> c_int;

	// -- oakengine::render (manager lifecycle) --

	/// `oakengine_render_manager_init` — bring up the module's
	/// process-global render manager. Without it `render_frame` fails with
	/// NULL + last_error. Fails (nonzero) when the manager is already
	/// initialized.
	pub fn oakengine_render_manager_init() -> c_int;
	/// `oakengine_render_manager_available` — 1 when the render manager is up.
	pub fn oakengine_render_manager_available() -> c_int;

	// -- oakengine::task (interchange load result + event subscription) --

	/// `oakengine_task_load_take_project` — take the project an
	/// interchange load/load-otio task produced (ownership moves to the
	/// caller; release with `oakengine_project_free`). NULL when the task
	/// is not a load task or has no project yet.
	pub fn oakengine_task_load_take_project(task: *mut OakEngineTask) -> *mut OakEngineProject;
	/// `oakengine_task_subscribe` — register the task event callback
	/// (`OAKTASK_EVENT_STARTED`=0, `OAKTASK_EVENT_PROGRESS`=1,
	/// `OAKTASK_EVENT_FINISHED`=2).
	pub fn oakengine_task_subscribe(
		task: *mut OakEngineTask,
		cb: Option<OakTaskEventFn>,
		userdata: *mut c_void,
	) -> i64;
}

/// The C-ABI `MinMax` mirror (exported for the integration tests).
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct oakapp_minmax {
	/// Minimum amplitude.
	pub min: f32,
	/// Maximum amplitude.
	pub max: f32,
}
