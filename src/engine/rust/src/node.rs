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

//! `engine/include/oakengine/{node,project,footage}.h` — the node graph,
//! project and footage families over the oaknode module.

use std::ffi::{c_char, c_int, c_void};

use crate::bridge::common as c;
use crate::bridge::node as n;
use crate::common::OakVideoParamsPod;
use crate::error::{Error, Result};
use crate::handle::{
	box_handle, free_box, guard, guard_int, guard_ptr, guard_void, read_cstr, string_result,
	unbox, write_string, CHandle, OakEngineClipboard, OakEngineFootage, OakEngineFrameCache,
	OakEngineKeyframe, OakEngineNode, OakEngineNodeDragger, OakEngineProject, OakEngineSequence,
	OakEngineThumbnailCache, OakEngineWaveformCache,
};
use crate::undo::push_or_run;

/// `engine/include/oakengine/node.h` — POD mirror of `oak_node_value`.
///
/// Layout-identical to the module's `oaknode_value`, so the facade hands
/// the engine POD straight to the oaknode bridge.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct OakNodeValue {
	/// `oak_node_value_type`.
	pub type_: c_int,
	/// INT/COMBO value, BOOL 0/1, RATIONAL numerator.
	pub num: i64,
	/// RATIONAL denominator.
	pub den: i64,
	/// FLOAT f[0]; VEC2/3/4 f[0..n-1]; COLOR r,g,b,a.
	pub f: [f64; 4],
}

/// `engine/include/oakengine/footage.h` — POD mirror of
/// `oak_footage_video_info` (olive::VideoParams stream description).
#[repr(C)]
#[derive(Clone, Copy)]
pub struct OakFootageVideoInfo {
	/// Stream index within the video streams.
	pub stream_index: c_int,
	/// Width in pixels.
	pub width: c_int,
	/// Height in pixels.
	pub height: c_int,
	/// Frame rate numerator.
	pub frame_rate_num: c_int,
	/// Frame rate denominator.
	pub frame_rate_den: c_int,
	/// Duration in time-base units.
	pub duration_ts: i64,
	/// Seconds per time-base unit (numerator).
	pub time_base_num: c_int,
	/// Seconds per time-base unit (denominator).
	pub time_base_den: c_int,
	/// ISO/IEC 23001-8 color primaries code point (0 when unknown).
	pub color_primaries: c_int,
	/// ISO/IEC 23001-8 transfer characteristics code point.
	pub color_trc: c_int,
	/// 1 when the stream is interlaced.
	pub interlaced: c_int,
}

/// `engine/include/oakengine/footage.h` — POD mirror of
/// `oak_footage_audio_info` (olive::AudioParams stream description).
#[repr(C)]
#[derive(Clone, Copy)]
pub struct OakFootageAudioInfo {
	/// Stream index within the audio streams.
	pub stream_index: c_int,
	/// Sample rate in Hz.
	pub sample_rate: c_int,
	/// ffmpeg-style channel mask (e.g. 0x3 = stereo).
	pub channel_layout: u64,
	/// Channel count.
	pub channel_count: c_int,
	/// Duration in time-base units.
	pub duration_ts: i64,
	/// Seconds per time-base unit (numerator).
	pub time_base_num: c_int,
	/// Seconds per time-base unit (denominator).
	pub time_base_den: c_int,
}

/// `engine/include/oakengine/footage.h` — POD mirror of `oak_proxy_params`
/// (olive::ProxyManager::ProxyParams).
#[repr(C)]
#[derive(Clone, Copy)]
pub struct OakProxyParams {
	/// Source resolution width (absolute when `divider` == 1).
	pub width: c_int,
	/// Source resolution height.
	pub height: c_int,
	/// Resolution divider (1/2/4/8 fraction of the source).
	pub divider: c_int,
	/// Preset version.
	pub version: c_int,
	/// CRF.
	pub crf: c_int,
	/// Whether the proxy includes audio (1/0).
	pub include_audio: c_int,
	/// ffmpeg output container (NUL-terminated).
	pub extension: [c_char; 32],
	/// ffmpeg encoder preset (NUL-terminated).
	pub preset: [c_char; 32],
}

/// `oak_node_value_type` values (`engine/include/oakengine/node.h`).
#[allow(missing_docs)] // mirror of the engine's oak_node_value_type enum
pub mod value_type {
	use std::ffi::c_int;

	pub const NONE: c_int = 0;
	pub const INT: c_int = 1;
	pub const FLOAT: c_int = 2;
	pub const BOOL: c_int = 3;
	pub const RATIONAL: c_int = 4;
	pub const COLOR: c_int = 5;
	pub const VEC2: c_int = 6;
	pub const VEC3: c_int = 7;
	pub const VEC4: c_int = 8;
	pub const COMBO: c_int = 9;
	pub const STRING: c_int = 10;
	pub const TEXT: c_int = 11;
	pub const FONT: c_int = 12;
	pub const STR_COMBO: c_int = 13;
	pub const BINARY: c_int = 14;
	pub const BEZIER: c_int = 15;
	pub const TEXTURE: c_int = 16;
	pub const SAMPLES: c_int = 17;
	pub const VIDEO_PARAMS: c_int = 18;
	pub const AUDIO_PARAMS: c_int = 19;
}

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

// Node-family thread-local last error (mirrors the C++ capi's per-family
// `thread_local QString`; engine/src/capi/node.cpp:67).
thread_local! {
	static LAST_NODE_ERROR: std::cell::RefCell<String> = std::cell::RefCell::new(String::new());
}

// Footage-family thread-local last error (engine/src/capi/footage.cpp:62).
thread_local! {
	static LAST_FOOTAGE_ERROR: std::cell::RefCell<String> = std::cell::RefCell::new(String::new());
}

/// Record the node-family last error.
fn set_node_error(msg: &str) {
	LAST_NODE_ERROR.with(|c| *c.borrow_mut() = msg.to_string());
}

/// Record the footage-family last error.
fn set_footage_error(msg: &str) {
	LAST_FOOTAGE_ERROR.with(|c| *c.borrow_mut() = msg.to_string());
}

/// oaknode type ids (verified against src/node/rust/src/).
const TYPE_ID_SEQUENCE: &str = "org.olivevideoeditor.Olive.sequence";
const TYPE_ID_FOLDER: &str = "org.olivevideoeditor.Olive.folder";
const TYPE_ID_TRACK: &str = "org.olivevideoeditor.Olive.track";
const TYPE_ID_FOOTAGE: &str = "org.olivevideoeditor.Olive.footage";
const TYPE_ID_CLIP_BLOCK: &str = "org.olivevideoeditor.Olive.clipblock";
const TYPE_ID_GAP_BLOCK: &str = "org.olivevideoeditor.Olive.gapblock";
const TYPE_ID_TRANSITION_BLOCK: &str = "org.olivevideoeditor.Olive.transitionblock";
const TYPE_ID_GROUP: &str = "org.olivevideoeditor.Olive.group";
const TYPE_ID_MULTICAM: &str = "org.olivevideoeditor.Olive.multicam";

/// Run a module two-stage string getter to completion.
///
/// # Safety
/// `f` must call a module two-stage getter (NULL `buf`/0 size queries the
/// required size including NUL; negative returns are module codes).
unsafe fn module_string<F: FnMut(*mut c_char, c_int) -> c_int>(mut f: F) -> Result<String> {
	unsafe {
		let len = f(std::ptr::null_mut(), 0);
		if len < 0 {
			return Err(Error::Module(len));
		}
		if len == 0 {
			return Ok(String::new());
		}
		let mut buf = vec![0 as c_char; (len + 1) as usize];
		let rc = f(buf.as_mut_ptr(), (len + 1) as c_int);
		if rc < 0 {
			return Err(Error::Module(rc));
		}
		Ok(read_cstr(buf.as_ptr()))
	}
}

/// The node's type id (oaknode `oaknode_node_get_id`).
fn node_type_id(node: CHandle) -> Result<String> {
	unsafe { module_string(|buf, size| n::oaknode_node_get_id(node, buf, size)) }
}

/// Whether `node` has type id `id`.
fn is_node_type(node: CHandle, id: &str) -> bool {
	node_type_id(node).map(|t| t == id).unwrap_or(false)
}

/// Unwrap an engine node pointer to the module handle.
///
/// # Safety
/// `node` must be NULL or a live box created by [`box_handle`].
unsafe fn node_handle(node: *const OakEngineNode) -> Result<CHandle> {
	unsafe { unbox(node) }
}

/// Unwrap an engine project pointer to the module handle.
///
/// # Safety
/// `project` must be NULL or a live box created by [`box_handle`].
unsafe fn project_handle(project: *const OakEngineProject) -> Result<CHandle> {
	unsafe { unbox(project) }
}

/// The project that owns `node` (module handle).
fn project_of(node: CHandle) -> Result<CHandle> {
	let mut out = CHandle::null();
	Error::from_module(unsafe { n::oaknode_node_get_project(node, &mut out) })?;
	if out.is_null() {
		return Err(Error::NotFound);
	}
	Ok(out)
}

/// Count project nodes whose type id equals `id`.
fn project_count_of_type(project: CHandle, id: &str) -> c_int {
	let total = unsafe { n::oaknode_project_node_count(project) };
	let mut count = 0;
	for i in 0..total {
		let node = unsafe { n::oaknode_project_node_at(project, i) };
		if !node.is_null() && is_node_type(node, id) {
			count += 1;
		}
	}
	count
}

/// The `index`-th project node of type id `id`, or NULL.
fn project_node_at_of_type(project: CHandle, id: &str, index: c_int) -> CHandle {
	if index < 0 {
		return CHandle::null();
	}
	let total = unsafe { n::oaknode_project_node_count(project) };
	let mut seen = 0;
	for i in 0..total {
		let node = unsafe { n::oaknode_project_node_at(project, i) };
		if node.is_null() {
			continue;
		}
		if is_node_type(node, id) {
			if seen == index {
				return node;
			}
			seen += 1;
		}
	}
	CHandle::null()
}

/// Whether `node` lives in `project`'s graph (identity comparison: handle
/// copies carry distinct `ctx` pointers to the same graph).
fn project_contains_node(project: CHandle, node: CHandle) -> bool {
	let id = unsafe { n::oaknode_node_identity(node) };
	if id == 0 {
		return false;
	}
	let total = unsafe { n::oaknode_project_node_count(project) };
	for i in 0..total {
		let other = unsafe { n::oaknode_project_node_at(project, i) };
		if !other.is_null() && unsafe { n::oaknode_node_identity(other) } == id {
			return true;
		}
	}
	false
}

/// `oakengine_node_frame_time_base` semantics: the frame rate of the
/// project's first sequence flipped (seconds per frame), or the engine
/// default 1001/30000.
fn time_base_for(node: CHandle) -> (i64, i64) {
	if let Ok(project) = project_of(node) {
		let total = unsafe { n::oaknode_project_node_count(project) };
		for i in 0..total {
			let seq = unsafe { n::oaknode_project_node_at(project, i) };
			if seq.is_null() || !is_node_type(seq, TYPE_ID_SEQUENCE) {
				continue;
			}
			let mut params: CHandle = CHandle::null();
			let rc = unsafe { n::oaknode_sequence_get_video_params(seq, 0, &mut params) };
			if rc == 0 && !params.is_null() {
				let mut num: c_int = 0;
				let mut den: c_int = 0;
				let fr = unsafe { c::oakcommon_videoparams_get_frame_rate(params, &mut num, &mut den) };
				let mut h = params;
				unsafe { c::oakcommon_videoparams_free(&mut h) };
				if fr == 0 && num > 0 && den > 0 {
					// Frame rate flipped → seconds per frame.
					return (den as i64, num as i64);
				}
			}
		}
	}
	(1001, 30000)
}

/// Greatest common divisor (1 when both are zero).
fn gcd(a: i64, b: i64) -> i64 {
	let (mut a, mut b) = (a.abs(), b.abs());
	while b != 0 {
		let t = b;
		b = a % b;
		a = t;
	}
	if a == 0 {
		1
	} else {
		a
	}
}

/// Convert a frame timestamp to rational seconds in the project's time
/// base: `time = ts * tb_num / tb_den` (reduced).
fn ts_to_time(ts: i64, tb: (i64, i64)) -> (i64, i64) {
	let num = ts as i128 * tb.0 as i128;
	let den = tb.1 as i128;
	let g = gcd(num as i64, den as i64);
	((num / g as i128) as i64, (den / g as i128) as i64)
}

/// Box a module command handle into the engine command shell.
fn command_box(cmd: CHandle) -> Result<*mut OakEngineClipboard> {
	if cmd.ctx.is_null() {
		return Err(Error::Failed("command creation failed".into()));
	}
	Ok(box_handle::<OakEngineClipboard>(cmd).cast())
}

/// Push a module command produced by a `*out_command` creator.
///
/// # Safety
/// `cmd` must be a live module command handle (or empty).
unsafe fn push_command(cmd: CHandle, name: &str) -> Result<()> {
	unsafe {
		let boxed = command_box(cmd)?;
		let name_c = std::ffi::CString::new(name)
			.map_err(|_| Error::Failed("invalid undo name".into()))?;
		push_or_run(boxed, name_c.as_ptr())
	}
}

/// Assemble several module command handles into ONE multi command and push
/// it (or add it to `parent` when non-NULL).
///
/// # Safety
/// `children` must hold live module command handles.
unsafe fn push_multi_commands(children: &[CHandle], parent: *mut c_void, name: &str) -> Result<()> {
	unsafe {
		let multi = crate::undo::oakengine_undo_command_create_multi();
		if multi.is_null() {
			return Err(Error::Failed("multi command allocation failed".into()));
		}
		let multi = multi as *mut OakEngineClipboard;
		for child in children {
			let rc = crate::undo::oakengine_undo_command_multi_add_child(
				multi as *mut c_void,
				command_box(*child)?.cast(),
			);
			if rc != 0 {
				free_box(multi);
				return Err(Error::Module(rc));
			}
		}
		if parent.is_null() {
			let name_c = std::ffi::CString::new(name)
				.map_err(|_| Error::Failed("invalid undo name".into()))?;
			push_or_run(multi, name_c.as_ptr())
		} else {
			// The caller owns the parent; `multi` is consumed by add_child.
			let rc = crate::undo::oakengine_undo_command_multi_add_child(parent, multi.cast());
			if rc == 0 {
				Ok(())
			} else {
				Err(Error::Module(rc))
			}
		}
	}
}

// ---------------------------------------------------------------------------
// project.h
// ---------------------------------------------------------------------------

/// `oakengine_project_create` — allocate an empty project shell.
#[no_mangle]
pub extern "C" fn oakengine_project_create() -> *mut OakEngineProject {
	guard_ptr(|| {
		let h = unsafe { n::oaknode_project_init() };
		if h.ctx.is_null() {
			return Ok(std::ptr::null_mut());
		}
		Ok(box_handle::<OakEngineProject>(h))
	})
}

/// `oakengine_project_free` — destroy a project and everything it owns.
#[no_mangle]
pub unsafe extern "C" fn oakengine_project_free(self_: *mut OakEngineProject) {
	guard_void(|| unsafe {
		if self_.is_null() {
			return;
		}
		// The module's project_free releases the handle and clears `ctx`;
		// the box shell is then deallocated (no double release).
		let mut h = (*self_).handle;
		n::oaknode_project_free(&mut h);
		drop(Box::from_raw(self_));
	})
}

/// `oakengine_project_new` — initialize `self` as a new blank project.
#[no_mangle]
pub unsafe extern "C" fn oakengine_project_new(self_: *mut OakEngineProject) -> c_int {
	guard(|| unsafe {
		let h = project_handle(self_)?;
		if !n::oaknode_project_root(h).is_null() {
			return Err(Error::State);
		}
		Error::from_module(n::oaknode_project_initialize(h))?;
		// Clearing the global undo stack mirrors the app's new-project
		// behavior (see undo.rs `oakengine_undo_clear`).
		crate::undo::oakengine_undo_clear();
		Ok(())
	})
}

/// `oakengine_project_load` — load project content from a .ove file.
#[no_mangle]
pub unsafe extern "C" fn oakengine_project_load(
	self_: *mut OakEngineProject,
	path: *const c_char,
	err: *mut c_char,
	err_size: c_int,
) -> c_int {
	guard(|| unsafe {
		if self_.is_null() || path.is_null() {
			return Err(Error::Invalid);
		}
		let h = project_handle(self_)?;
		if !n::oaknode_project_root(h).is_null() {
			return Err(Error::State);
		}

		// Normalize to an absolute path so the stored filename matches the
		// file's saved_url (mirrors capi project.cpp).
		let path_str = read_cstr(path);
		let p = std::path::Path::new(&path_str);
		let abs = if p.is_absolute() {
			p.to_path_buf()
		} else {
			std::env::current_dir()
				.map(|d| d.join(p))
				.unwrap_or_else(|_| p.to_path_buf())
		};
		let filename = abs.to_string_lossy().into_owned();
		let filename_c = std::ffi::CString::new(filename.as_str())
			.map_err(|_| Error::Failed("invalid path".into()))?;
		Error::from_module(n::oaknode_project_set_filename(h, filename_c.as_ptr()))?;

		let mut code: c_int = -1;
		let mut details = [0 as c_char; 4096];
		let rc = n::oaknode_serializer_load_from_file(
			h,
			filename_c.as_ptr(),
			&mut code,
			details.as_mut_ptr(),
			details.len() as c_int,
		);
		if rc != 0 || code != 0 {
			let msg = read_cstr(details.as_ptr());
			let full = if msg.is_empty() {
				format!("Failed to load project file \"{}\".", filename)
			} else {
				msg
			};
			write_string(&full, err, err_size);
			return Err(Error::Failed(full));
		}

		// Success: clear the undo stack and the modified flag.
		crate::undo::oakengine_undo_clear();
		Error::from_module(n::oaknode_project_set_modified(h, 0))?;
		if !err.is_null() && err_size > 0 {
			*err = 0;
		}
		Ok(())
	})
}

/// `oakengine_project_save` — save the project to `path` (or its own
/// filename when `path` is NULL).
#[no_mangle]
pub unsafe extern "C" fn oakengine_project_save(
	self_: *mut OakEngineProject,
	path: *const c_char,
) -> c_int {
	guard(|| unsafe {
		let h = project_handle(self_)?;
		let filename = if path.is_null() {
			let mut buf = [0 as c_char; 4096];
			let rc = n::oaknode_project_filename(h, buf.as_mut_ptr(), buf.len() as c_int);
			if rc < 0 {
				return Err(Error::Module(rc));
			}
			let s = read_cstr(buf.as_ptr());
			if s.is_empty() {
				return Err(Error::Invalid);
			}
			s
		} else {
			read_cstr(path)
		};
		let filename_c = std::ffi::CString::new(filename.as_str())
			.map_err(|_| Error::Failed("invalid path".into()))?;
		let compress = !filename.to_lowercase().ends_with(".ovexml");
		let mut code: c_int = -1;
		let mut details = [0 as c_char; 4096];
		let rc = n::oaknode_serializer_save_to_file(
			h,
			filename_c.as_ptr(),
			compress as c_int,
			&mut code,
			details.as_mut_ptr(),
			details.len() as c_int,
		);
		if rc != 0 || code != 0 {
			return Err(Error::Failed(read_cstr(details.as_ptr())));
		}
		// Success: record the target filename and clear the modified flag.
		Error::from_module(n::oaknode_project_set_filename(h, filename_c.as_ptr()))?;
		Error::from_module(n::oaknode_project_set_modified(h, 0))?;
		Ok(())
	})
}

/// `oakengine_project_is_modified`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_project_is_modified(self_: *const OakEngineProject) -> c_int {
	guard_int(|| unsafe {
		if self_.is_null() {
			return Ok(0);
		}
		let rc = n::oaknode_project_is_modified(unbox(self_)?);
		Ok(if rc != 0 { 1 } else { 0 })
	})
}

/// `oakengine_project_set_modified`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_project_set_modified(
	self_: *mut OakEngineProject,
	modified: c_int,
) -> c_int {
	guard(|| unsafe {
		let h = project_handle(self_)?;
		Error::from_module(n::oaknode_project_set_modified(h, if modified != 0 { 1 } else { 0 }))
	})
}

/// `oakengine_project_name` — display name (buf/size).
#[no_mangle]
pub unsafe extern "C" fn oakengine_project_name(
	self_: *const OakEngineProject,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	guard_int(|| unsafe {
		let h = unbox(self_)?;
		let rc = n::oaknode_project_name(h, buf, buf_size);
		if rc < 0 {
			Err(Error::Module(rc))
		} else {
			Ok(string_result(rc))
		}
	})
}

/// `oakengine_project_filename` — full path (buf/size).
#[no_mangle]
pub unsafe extern "C" fn oakengine_project_filename(
	self_: *const OakEngineProject,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	guard_int(|| unsafe {
		let h = unbox(self_)?;
		let rc = n::oaknode_project_filename(h, buf, buf_size);
		if rc < 0 {
			Err(Error::Module(rc))
		} else {
			Ok(string_result(rc))
		}
	})
}

/// `oakengine_project_footage_count`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_project_footage_count(
	self_: *const OakEngineProject,
) -> c_int {
	guard_int(|| unsafe {
		if self_.is_null() {
			return Ok(0);
		}
		Ok(project_count_of_type(unbox(self_)?, TYPE_ID_FOOTAGE))
	})
}

/// `oakengine_project_footage_filename` — stored filename at `index`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_project_footage_filename(
	self_: *const OakEngineProject,
	index: c_int,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	guard_int(|| unsafe {
		let h = unbox(self_)?;
		let footage = project_node_at_of_type(h, TYPE_ID_FOOTAGE, index);
		if footage.is_null() {
			return Err(Error::NotFound);
		}
		let rc = n::oaknode_footage_filename(footage, buf, buf_size);
		if rc < 0 {
			Err(Error::Module(rc))
		} else {
			Ok(string_result(rc))
		}
	})
}

/// `oakengine_project_footage_is_online` — 1 when the footage file exists
/// (as stored, or resolved relative to the project file's directory).
#[no_mangle]
pub unsafe extern "C" fn oakengine_project_footage_is_online(
	self_: *const OakEngineProject,
	index: c_int,
) -> c_int {
	guard_int(|| unsafe {
		if self_.is_null() {
			return Err(Error::Invalid);
		}
		let h = unbox(self_)?;
		let footage = project_node_at_of_type(h, TYPE_ID_FOOTAGE, index);
		if footage.is_null() {
			return Err(Error::NotFound);
		}
		let filename =
			module_string(|buf, size| n::oaknode_footage_filename(footage, buf, size))?;
		let path = std::path::Path::new(&filename);
		if path.exists() {
			return Ok(1);
		}
		// Footage that moved together with the project file.
		if path.is_relative() {
			let mut fbuf = [0 as c_char; 4096];
			let rc = n::oaknode_project_filename(h, fbuf.as_mut_ptr(), fbuf.len() as c_int);
			if rc >= 0 {
				let project_file = read_cstr(fbuf.as_ptr());
				let resolved = std::path::Path::new(&project_file)
					.parent()
					.map(|d| d.join(&filename))
					.unwrap_or_else(|| std::path::PathBuf::from(&filename));
				if resolved.exists() {
					return Ok(1);
				}
			}
		}
		Ok(0)
	})
}

/// `oakengine_project_can_undo`.
#[no_mangle]
pub extern "C" fn oakengine_project_can_undo(self_: *const OakEngineProject) -> c_int {
	if self_.is_null() {
		0
	} else {
		crate::undo::oakengine_undo_can_undo()
	}
}

/// `oakengine_project_can_redo`.
#[no_mangle]
pub extern "C" fn oakengine_project_can_redo(self_: *const OakEngineProject) -> c_int {
	if self_.is_null() {
		0
	} else {
		crate::undo::oakengine_undo_can_redo()
	}
}

/// `oakengine_project_undo` — step the global undo stack back.
#[no_mangle]
pub unsafe extern "C" fn oakengine_project_undo(self_: *mut OakEngineProject) -> c_int {
	guard(|| {
		if self_.is_null() {
			return Err(Error::Invalid);
		}
		let index = crate::undo::oakengine_undo_index();
		crate::undo::oakengine_undo_jump(index - 1);
		Ok(())
	})
}

/// `oakengine_project_redo` — step the global undo stack forward.
#[no_mangle]
pub unsafe extern "C" fn oakengine_project_redo(self_: *mut OakEngineProject) -> c_int {
	guard(|| {
		if self_.is_null() {
			return Err(Error::Invalid);
		}
		let index = crate::undo::oakengine_undo_index();
		crate::undo::oakengine_undo_jump(index + 1);
		Ok(())
	})
}

/// `oakengine_project_sequence_count`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_project_sequence_count(
	self_: *const OakEngineProject,
) -> c_int {
	guard_int(|| unsafe {
		if self_.is_null() {
			return Ok(0);
		}
		Ok(project_count_of_type(unbox(self_)?, TYPE_ID_SEQUENCE))
	})
}

/// `oakengine_project_sequence_at` — borrowed sequence at `index`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_project_sequence_at(
	self_: *const OakEngineProject,
	index: c_int,
) -> *mut OakEngineSequence {
	guard_ptr(|| unsafe {
		if self_.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let seq = project_node_at_of_type(unbox(self_)?, TYPE_ID_SEQUENCE, index);
		if seq.is_null() {
			return Ok(std::ptr::null_mut());
		}
		Ok(box_handle::<OakEngineSequence>(seq))
	})
}

/* ---- Folder operations ---------------------------------------------------- */

/// `oakengine_folder_create` — create a folder node under `parent`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_folder_create(
	project: *mut OakEngineProject,
	parent: *mut OakEngineNode,
	name: *const c_char,
) -> *mut OakEngineNode {
	guard_ptr(|| unsafe {
		let ph = project_handle(project)?;
		let parent_h = node_handle(parent)?;
		if !is_node_type(parent_h, TYPE_ID_FOLDER) {
			return Ok(std::ptr::null_mut());
		}
		let child = n::oaknode_folder_create(ph);
		if child.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let label = if name.is_null() {
			String::new()
		} else {
			read_cstr(name)
		};
		let label_c = std::ffi::CString::new(label.as_str()).map_err(|_| Error::Failed("invalid name".into()))?;
		// The module's folder_create already registers the node in the
		// project's graph, so only the FolderAddChild command is needed
		// (the C++ additionally pushes a NodeAddCommand; that surface does
		// not exist here). The label is applied directly like the capi.
		Error::from_module(n::oaknode_node_set_label(child, label_c.as_ptr()))?;
		let add_child = n::oaknode_command_create_folder_add_child(parent_h, child);
		if add_child.ctx.is_null() {
			return Ok(std::ptr::null_mut());
		}
		push_command(add_child, "Create Folder")?;
		Ok(box_handle::<OakEngineNode>(child))
	})
}

/// `oakengine_folder_has_child_recursive`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_folder_has_child_recursive(
	folder: *const OakEngineNode,
	child: *const OakEngineNode,
) -> c_int {
	guard_int(|| unsafe {
		if folder.is_null() || child.is_null() {
			return Ok(0);
		}
		let f = unbox(folder)?;
		if !is_node_type(f, TYPE_ID_FOLDER) {
			return Ok(0);
		}
		Ok(n::oaknode_folder_has_child_recursive(f, unbox(child)?))
	})
}

/// `oakengine_folder_index_of_child` — index or OAKENGINE_E_NOT_FOUND.
#[no_mangle]
pub unsafe extern "C" fn oakengine_folder_index_of_child(
	folder: *const OakEngineNode,
	child: *const OakEngineNode,
) -> c_int {
	guard_int(|| unsafe {
		if folder.is_null() || child.is_null() {
			return Err(Error::Invalid);
		}
		let f = unbox(folder)?;
		if !is_node_type(f, TYPE_ID_FOLDER) {
			return Err(Error::Invalid);
		}
		let idx = n::oaknode_folder_index_of_child(f, unbox(child)?);
		if idx >= 0 {
			Ok(idx)
		} else {
			Err(Error::NotFound)
		}
	})
}

/// `oakengine_folder_item_child_count`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_folder_item_child_count(
	folder: *const OakEngineNode,
) -> c_int {
	guard_int(|| unsafe {
		if folder.is_null() {
			return Ok(0);
		}
		let f = unbox(folder)?;
		if !is_node_type(f, TYPE_ID_FOLDER) {
			return Ok(0);
		}
		let rc = n::oaknode_folder_child_count(f);
		Ok(if rc < 0 { 0 } else { rc })
	})
}

/// `oakengine_folder_item_child` — borrowed child at `index`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_folder_item_child(
	folder: *const OakEngineNode,
	index: c_int,
) -> *mut OakEngineNode {
	guard_ptr(|| unsafe {
		if folder.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let f = unbox(folder)?;
		if !is_node_type(f, TYPE_ID_FOLDER) {
			return Ok(std::ptr::null_mut());
		}
		let child = n::oaknode_folder_child_at(f, index);
		if child.is_null() {
			return Ok(std::ptr::null_mut());
		}
		Ok(box_handle::<OakEngineNode>(child))
	})
}

/// `oakengine_folder_child_input_key` — static input key string.
#[no_mangle]
pub extern "C" fn oakengine_folder_child_input_key() -> *const c_char {
	// Folder::k_child_input (engine/node/project/folder/folder.cpp:34).
	static S: &[u8] = b"child_in\0";
	S.as_ptr() as *const c_char
}

/// `oakengine_folder_add_child` — undoable add.
#[no_mangle]
pub unsafe extern "C" fn oakengine_folder_add_child(
	folder: *mut OakEngineNode,
	child: *mut OakEngineNode,
) -> c_int {
	guard(|| unsafe {
		if folder.is_null() || child.is_null() {
			return Err(Error::Invalid);
		}
		let f = unbox(folder)?;
		if !is_node_type(f, TYPE_ID_FOLDER) {
			return Err(Error::Invalid);
		}
		let c = unbox(child)?;
		let cmd = n::oaknode_command_create_folder_add_child(f, c);
		if cmd.ctx.is_null() {
			return Err(Error::Failed("folder add child command failed".into()));
		}
		push_command(cmd, "Add Child to Folder")
	})
}

/// `oakengine_folder_remove_element_command` — opaque command pointer.
#[no_mangle]
pub unsafe extern "C" fn oakengine_folder_remove_element_command(
	folder: *mut OakEngineNode,
	child: *mut OakEngineNode,
) -> *mut c_void {
	guard_ptr(|| unsafe {
		if folder.is_null() || child.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let f = unbox(folder)?;
		if !is_node_type(f, TYPE_ID_FOLDER) {
			return Ok(std::ptr::null_mut());
		}
		// Stub: the module has no Folder::RemoveElementCommand creator
		// (its only folder-child command is the ADD).
		let _ = unbox(child)?;
		Ok(std::ptr::null_mut())
	})
}

/// `oakengine_folder_move_child` — move one node into `new_folder`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_folder_move_child(
	node: *mut OakEngineNode,
	new_folder: *mut OakEngineNode,
) -> c_int {
	guard(|| unsafe {
		let mut nodes = [node];
		let rc = oakengine_folder_move_children(nodes.as_mut_ptr(), 1, new_folder, std::ptr::null());
		Error::from_module(rc)
	})
}

/// `oakengine_folder_move_children` — move several nodes as ONE command.
#[no_mangle]
pub unsafe extern "C" fn oakengine_folder_move_children(
	nodes: *mut *mut OakEngineNode,
	count: c_int,
	dest_folder: *mut OakEngineNode,
	undo_name: *const c_char,
) -> c_int {
	guard(|| unsafe {
		if nodes.is_null() || count <= 0 || dest_folder.is_null() {
			return Err(Error::Invalid);
		}
		let dest = unbox(dest_folder)?;
		if !is_node_type(dest, TYPE_ID_FOLDER) {
			return Err(Error::Invalid);
		}
		let mut handles = Vec::with_capacity(count as usize);
		for i in 0..count as usize {
			let node = *nodes.add(i);
			if node.is_null() {
				return Err(Error::Invalid);
			}
			handles.push(unbox(node)?);
		}
		// The module's folder_move_children is a live (direct) move; the
		// capi's move is undoable but the module has no move command, so
		// this applies the move directly. Documented deviation.
		let rc = n::oaknode_folder_move_children(handles.as_ptr(), count, dest);
		if rc != 0 {
			return Err(Error::Module(rc));
		}
		let _ = undo_name;
		Ok(())
	})
}

/* ---- Project extras ------------------------------------------------------- */

/// `oakengine_project_root`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_project_root(
	self_: *mut OakEngineProject,
) -> *mut OakEngineNode {
	guard_ptr(|| unsafe {
		if self_.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let root = n::oaknode_project_root(unbox(self_)?);
		if root.is_null() {
			return Ok(std::ptr::null_mut());
		}
		Ok(box_handle::<OakEngineNode>(root))
	})
}

/// `oakengine_project_pretty_filename`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_project_pretty_filename(
	self_: *const OakEngineProject,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	guard_int(|| unsafe {
		let h = unbox(self_)?;
		let rc = n::oaknode_project_pretty_filename(h, buf, buf_size);
		if rc < 0 {
			Err(Error::Module(rc))
		} else {
			Ok(string_result(rc))
		}
	})
}

/// `oakengine_project_set_filename`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_project_set_filename(
	self_: *mut OakEngineProject,
	path: *const c_char,
) -> c_int {
	guard(|| unsafe {
		if path.is_null() {
			return Err(Error::Invalid);
		}
		let h = project_handle(self_)?;
		Error::from_module(n::oaknode_project_set_filename(h, path))
	})
}

/// `oakengine_project_cache_path`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_project_cache_path(
	self_: *const OakEngineProject,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	guard_int(|| unsafe {
		let h = unbox(self_)?;
		let rc = n::oaknode_project_cache_path(h, buf, buf_size);
		if rc < 0 {
			Err(Error::Module(rc))
		} else {
			Ok(string_result(rc))
		}
	})
}

/// `oakengine_project_cache_alongside_path`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_project_cache_alongside_path(
	self_: *const OakEngineProject,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	// Stub: the oaknode project has no alongside-cache-path export (the
	// C++ derives it from the project file location).
	guard_int(|| unsafe {
		if self_.is_null() {
			return Err(Error::Invalid);
		}
		let _ = unbox(self_)?;
		Ok(write_string("", buf, buf_size))
	})
}

/// `oakengine_project_set_custom_cache_path`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_project_set_custom_cache_path(
	self_: *mut OakEngineProject,
	path: *const c_char,
) -> c_int {
	guard(|| unsafe {
		let h = project_handle(self_)?;
		let path = if path.is_null() {
			crate::common::empty_cstr()
		} else {
			path
		};
		Error::from_module(n::oaknode_project_set_custom_cache_path(h, path))
	})
}

/// `oakengine_project_get_custom_cache_path` (empty → 0).
#[no_mangle]
pub unsafe extern "C" fn oakengine_project_get_custom_cache_path(
	self_: *const OakEngineProject,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	guard_int(|| unsafe {
		let h = unbox(self_)?;
		let rc = n::oaknode_project_get_custom_cache_path(h, buf, buf_size);
		if rc < 0 {
			Err(Error::Module(rc))
		} else if rc == 0 {
			Ok(0)
		} else {
			Ok(string_result(rc))
		}
	})
}

/// `oakengine_project_get_cache_location_setting`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_project_get_cache_location_setting(
	self_: *const OakEngineProject,
) -> c_int {
	guard_int(|| unsafe {
		if self_.is_null() {
			return Ok(-1);
		}
		Ok(n::oaknode_project_get_cache_location_setting(unbox(self_)?))
	})
}

/// `oakengine_project_item_mime_type` — static MIME string.
#[no_mangle]
pub extern "C" fn oakengine_project_item_mime_type() -> *const c_char {
	// Project::k_item_mime_type (engine/node/project.cpp:55).
	static S: &[u8] = b"application/x-oliveprojectitemdata\0";
	S.as_ptr() as *const c_char
}

/// `oakengine_project_from_object` — owning project of a node.
#[no_mangle]
pub unsafe extern "C" fn oakengine_project_from_object(
	node: *const OakEngineNode,
) -> *mut OakEngineProject {
	guard_ptr(|| unsafe {
		if node.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let mut out = CHandle::null();
		Error::from_module(n::oaknode_node_get_project(unbox(node)?, &mut out))?;
		if out.is_null() {
			return Ok(std::ptr::null_mut());
		}
		Ok(box_handle::<OakEngineProject>(out))
	})
}

/// `oakengine_project_get_color_reference_space`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_project_get_color_reference_space(
	self_: *const OakEngineProject,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	// Stub: the oaknode project stores no color reference space (the C++
	// reads Project::k_color_reference_space through the color manager).
	guard_int(|| unsafe {
		if self_.is_null() {
			return Err(Error::Invalid);
		}
		let _ = unbox(self_)?;
		Ok(write_string("", buf, buf_size))
	})
}

/// `oakengine_project_set_color_reference_space`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_project_set_color_reference_space(
	self_: *mut OakEngineProject,
	colorspace: *const c_char,
) -> c_int {
	// Stub: see `oakengine_project_get_color_reference_space`; accepted as
	// a no-op so callers do not break.
	guard(|| unsafe {
		if self_.is_null() || colorspace.is_null() {
			return Err(Error::Invalid);
		}
		let _ = project_handle(self_)?;
		Ok(())
	})
}

// ---------------------------------------------------------------------------
// node.h — enumeration
// ---------------------------------------------------------------------------

/// `oakengine_node_last_error` — last node-family failure reason.
#[no_mangle]
pub extern "C" fn oakengine_node_last_error(buf: *mut c_char, buf_size: c_int) -> c_int {
	crate::handle::guard_int(|| unsafe {
		Ok(write_string(
			&LAST_NODE_ERROR.with(|c| c.borrow().clone()),
			buf,
			buf_size,
		))
	})
}

/// `oakengine_project_node_count` — number of nodes in the graph.
#[no_mangle]
pub unsafe extern "C" fn oakengine_project_node_count(
	self_: *const OakEngineProject,
) -> c_int {
	guard_int(|| unsafe {
		if self_.is_null() {
			return Ok(0);
		}
		Ok(n::oaknode_project_node_count(unbox(self_)?))
	})
}

/// `oakengine_project_node_at` — borrowed node at `index`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_project_node_at(
	self_: *const OakEngineProject,
	index: c_int,
) -> *mut OakEngineNode {
	guard_ptr(|| unsafe {
		if self_.is_null() || index < 0 {
			return Ok(std::ptr::null_mut());
		}
		let node = n::oaknode_project_node_at(unbox(self_)?, index);
		if node.is_null() {
			return Ok(std::ptr::null_mut());
		}
		Ok(box_handle::<OakEngineNode>(node))
	})
}

// ---------------------------------------------------------------------------
// node.h — node factory
// ---------------------------------------------------------------------------

/// `oakengine_node_factory_id_count`.
#[no_mangle]
pub extern "C" fn oakengine_node_factory_id_count() -> c_int {
	guard_int(|| {
		let mut count: c_int = 0;
		Error::from_module(unsafe { n::oaknode_factory_id_count(&mut count) })?;
		Ok(count)
	})
}

/// `oakengine_node_factory_create_from_id` — owned node, not added.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_factory_create_from_id(
	type_id: *const c_char,
) -> *mut OakEngineNode {
	guard_ptr(|| unsafe {
		if type_id.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let node = n::oaknode_factory_create_from_id(type_id);
		if node.ctx.is_null() {
			set_node_error(&format!(
				"unknown node type id \"{}\"",
				read_cstr(type_id)
			));
			return Ok(std::ptr::null_mut());
		}
		Ok(box_handle::<OakEngineNode>(node))
	})
}

/// `oakengine_node_factory_name_from_id` — display name (buf/size).
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_factory_name_from_id(
	type_id: *const c_char,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	guard_int(|| unsafe {
		if type_id.is_null() {
			if !buf.is_null() && buf_size > 0 {
				*buf = 0;
			}
			return Ok(0);
		}
		let rc = n::oaknode_factory_name_from_id(type_id, buf, buf_size);
		if rc < 0 {
			Err(Error::Module(rc))
		} else {
			Ok(string_result(rc))
		}
	})
}

/// `oakengine_node_factory_node_at` — borrowed prototype node.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_factory_node_at(index: c_int) -> *mut OakEngineNode {
	guard_ptr(|| unsafe {
		let mut out = CHandle::null();
		let rc = n::oaknode_factory_node_at(index, &mut out);
		if rc != 0 || out.is_null() {
			return Ok(std::ptr::null_mut());
		}
		Ok(box_handle::<OakEngineNode>(out))
	})
}

/// `oakengine_node_category_count`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_category_count(self_: *const OakEngineNode) -> c_int {
	// Stub: the oaknode factory metadata exposes no category enumeration
	// through the C ABI.
	guard_int(|| unsafe {
		if self_.is_null() {
			return Ok(0);
		}
		let _ = unbox(self_)?;
		Ok(0)
	})
}

/// `oakengine_node_category_at`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_category_at(
	self_: *const OakEngineNode,
	index: c_int,
) -> c_int {
	// Stub: see `oakengine_node_category_count`.
	guard_int(|| unsafe {
		if self_.is_null() {
			return Ok(-1);
		}
		let _ = unbox(self_)?;
		let _ = index;
		Ok(-1)
	})
}

/// `oakengine_node_get_flags`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_get_flags(self_: *const OakEngineNode) -> u64 {
	// Stub: the oaknode module has no per-node flags export.
	crate::handle::guard_i64(|| unsafe {
		if self_.is_null() {
			return Ok(0);
		}
		let _ = unbox(self_)?;
		Ok(0)
	}) as u64
}

/// `oakengine_node_flag_dont_show_in_create_menu`.
#[no_mangle]
pub extern "C" fn oakengine_node_flag_dont_show_in_create_menu() -> u64 {
	// Node::k_dont_show_in_create_menu (engine/node/node.h:118).
	0x8
}

/// `oakengine_node_flag_dont_show_in_param_view`.
#[no_mangle]
pub extern "C" fn oakengine_node_flag_dont_show_in_param_view() -> u64 {
	// Node::k_dont_show_in_param_view (engine/node/node.h:115).
	0x1
}

/// `oakengine_node_flag_video_effect`.
#[no_mangle]
pub extern "C" fn oakengine_node_flag_video_effect() -> u64 {
	// Node::k_video_effect (engine/node/node.h:116).
	0x2
}

/// `oakengine_node_flag_audio_effect`.
#[no_mangle]
pub extern "C" fn oakengine_node_flag_audio_effect() -> u64 {
	// Node::k_audio_effect (engine/node/node.h:117).
	0x4
}

/// `oakengine_node_retranslate` — no-op: the module has no translation
/// pass (the engine re-reads translated strings on language change).
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_retranslate(self_: *mut OakEngineNode) {
	guard_void(|| unsafe {
		let _ = unbox(self_);
	})
}

/// `oakengine_node_get_sub_category`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_get_sub_category(
	self_: *const OakEngineNode,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	// Stub: the oaknode module has no sub-category export.
	guard_int(|| unsafe {
		if self_.is_null() {
			return Err(Error::Invalid);
		}
		let _ = unbox(self_)?;
		Ok(write_string("", buf, buf_size))
	})
}

/// `oakengine_node_get_description`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_get_description(
	self_: *const OakEngineNode,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	// Stub: the oaknode module has no description export.
	guard_int(|| unsafe {
		if self_.is_null() {
			return Err(Error::Invalid);
		}
		let _ = unbox(self_)?;
		Ok(write_string("", buf, buf_size))
	})
}

/// `oakengine_node_create_copy` — standalone copy (caller owns).
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_create_copy(
	self_: *const OakEngineNode,
) -> *mut OakEngineNode {
	guard_ptr(|| unsafe {
		if self_.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let copy = n::oaknode_node_create_copy(unbox(self_)?);
		if copy.ctx.is_null() {
			return Ok(std::ptr::null_mut());
		}
		Ok(box_handle::<OakEngineNode>(copy))
	})
}

// ---------------------------------------------------------------------------
// node.h — metadata
// ---------------------------------------------------------------------------

/// `oakengine_node_get_type_id`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_get_type_id(
	self_: *const OakEngineNode,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	guard_int(|| unsafe {
		let h = unbox(self_)?;
		let rc = n::oaknode_node_get_id(h, buf, buf_size);
		if rc < 0 {
			Err(Error::Module(rc))
		} else {
			Ok(string_result(rc))
		}
	})
}

/// `oakengine_node_get_name`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_get_name(
	self_: *const OakEngineNode,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	guard_int(|| unsafe {
		let h = unbox(self_)?;
		let rc = n::oaknode_node_get_name(h, buf, buf_size);
		if rc < 0 {
			Err(Error::Module(rc))
		} else {
			Ok(string_result(rc))
		}
	})
}

/// `oakengine_node_get_short_name`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_get_short_name(
	self_: *const OakEngineNode,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	// The oaknode module has no short-name export; the engine's default
	// `short_name()` falls back to `name()`, so the name is returned.
	guard_int(|| unsafe {
		let h = unbox(self_)?;
		let rc = n::oaknode_node_get_name(h, buf, buf_size);
		if rc < 0 {
			Err(Error::Module(rc))
		} else {
			Ok(string_result(rc))
		}
	})
}

/// `oakengine_node_get_label`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_get_label(
	self_: *const OakEngineNode,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	guard_int(|| unsafe {
		let h = unbox(self_)?;
		let rc = n::oaknode_node_get_label(h, buf, buf_size);
		if rc < 0 {
			Err(Error::Module(rc))
		} else {
			Ok(string_result(rc))
		}
	})
}

/// `oakengine_node_set_label` — undoable rename (like set_label_ex(1)).
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_set_label(
	self_: *mut OakEngineNode,
	label: *const c_char,
) -> c_int {
	guard(|| unsafe {
		let h = node_handle(self_)?;
		let mut cmd: CHandle = CHandle::null();
		let label = if label.is_null() {
			crate::common::empty_cstr()
		} else {
			label
		};
		let rc = n::oaknode_node_set_label_undoable(h, label, &mut cmd);
		if rc != 0 {
			return Err(Error::Module(rc));
		}
		push_command(cmd, "Rename Node")
	})
}

/// `oakengine_node_set_label_ex` — explicit undoable flag.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_set_label_ex(
	self_: *mut OakEngineNode,
	label: *const c_char,
	undoable: c_int,
) -> c_int {
	guard(|| unsafe {
		if self_.is_null() {
			set_node_error("invalid node");
			return Err(Error::Invalid);
		}
		let h = unbox(self_)?;
		let label = if label.is_null() {
			crate::common::empty_cstr()
		} else {
			label
		};
		if undoable != 0 {
			let mut cmd: CHandle = CHandle::null();
			let rc = n::oaknode_node_set_label_undoable(h, label, &mut cmd);
			if rc != 0 {
				return Err(Error::Module(rc));
			}
			push_command(cmd, "Rename Node")
		} else {
			Error::from_module(n::oaknode_node_set_label(h, label))
		}
	})
}

/// `oakengine_node_set_label_many` — one command for several nodes.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_set_label_many(
	nodes: *mut *mut OakEngineNode,
	count: c_int,
	label: *const c_char,
) -> c_int {
	guard(|| unsafe {
		let rc = oakengine_node_rename_many(nodes, count, label, std::ptr::null_mut());
		Error::from_module(rc)
	})
}

/// `oakengine_node_rename_many` — with optional parent multi command.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_rename_many(
	nodes: *mut *mut OakEngineNode,
	count: c_int,
	label: *const c_char,
	parent_multi_or_null: *mut c_void,
) -> c_int {
	guard(|| unsafe {
		if count < 0 || (count > 0 && nodes.is_null()) {
			set_node_error("invalid arguments");
			return Err(Error::Invalid);
		}
		if count == 0 {
			return Ok(());
		}
		let label = if label.is_null() {
			crate::common::empty_cstr()
		} else {
			label
		};
		let mut cmds = Vec::with_capacity(count as usize);
		for i in 0..count as usize {
			let node = *nodes.add(i);
			if node.is_null() {
				set_node_error(&format!("invalid node at index {}", i));
				return Err(Error::Invalid);
			}
			let h = unbox(node)?;
			let mut cmd: CHandle = CHandle::null();
			let rc = n::oaknode_node_set_label_undoable(h, label, &mut cmd);
			if rc != 0 {
				return Err(Error::Module(rc));
			}
			cmds.push(cmd);
		}
		push_multi_commands(&cmds, parent_multi_or_null, "Rename Nodes")
	})
}

/// `oakengine_node_rename_command` — opaque command pointer.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_rename_command(
	node: *mut OakEngineNode,
	label: *const c_char,
) -> *mut c_void {
	guard_ptr(|| unsafe {
		if node.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let h = unbox(node)?;
		let label = if label.is_null() {
			crate::common::empty_cstr()
		} else {
			label
		};
		let mut cmd: CHandle = CHandle::null();
		let rc = n::oaknode_node_set_label_undoable(h, label, &mut cmd);
		if rc != 0 {
			return Ok(std::ptr::null_mut());
		}
		Ok(command_box(cmd)?.cast())
	})
}

/// `oakengine_node_set_color_label` — one command for several nodes.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_set_color_label(
	nodes: *mut *mut OakEngineNode,
	count: c_int,
	color_index: c_int,
) -> c_int {
	guard(|| unsafe {
		if count < 0 || (count > 0 && nodes.is_null()) {
			set_node_error("invalid arguments");
			return Err(Error::Invalid);
		}
		if count == 0 {
			return Ok(());
		}
		let mut cmds = Vec::with_capacity(count as usize);
		for i in 0..count as usize {
			let node = *nodes.add(i);
			if node.is_null() {
				set_node_error(&format!("invalid node at index {}", i));
				return Err(Error::Invalid);
			}
			let h = unbox(node)?;
			let mut cmd: CHandle = CHandle::null();
			let rc = n::oaknode_node_set_override_color_undoable(h, color_index, &mut cmd);
			if rc != 0 {
				return Err(Error::Module(rc));
			}
			cmds.push(cmd);
		}
		push_multi_commands(&cmds, std::ptr::null_mut(), "Set Node Color Labels")
	})
}

/// `oakengine_node_set_color_label_command` — opaque command pointer.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_set_color_label_command(
	node: *mut OakEngineNode,
	color_index: c_int,
) -> *mut c_void {
	guard_ptr(|| unsafe {
		if node.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let h = unbox(node)?;
		let mut cmd: CHandle = CHandle::null();
		let rc = n::oaknode_node_set_override_color_undoable(h, color_index, &mut cmd);
		if rc != 0 {
			return Ok(std::ptr::null_mut());
		}
		Ok(command_box(cmd)?.cast())
	})
}

/// `oakengine_node_get_color_label`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_get_color_label(
	self_: *const OakEngineNode,
) -> c_int {
	guard_int(|| unsafe {
		if self_.is_null() {
			return Ok(-1);
		}
		let mut value: c_int = -1;
		Error::from_module(n::oaknode_node_get_override_color(unbox(self_)?, &mut value))?;
		Ok(value)
	})
}

/// `oakengine_node_get_effective_color_label`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_get_effective_color_label(
	self_: *const OakEngineNode,
) -> c_int {
	// The C++ falls back to the config "CatColor<N>" value of the node's
	// first category; the module has no category export, so the override
	// color is returned as-is (documented divergence).
	guard_int(|| unsafe {
		if self_.is_null() {
			return Ok(-1);
		}
		let mut value: c_int = -1;
		Error::from_module(n::oaknode_node_get_override_color(unbox(self_)?, &mut value))?;
		Ok(value)
	})
}

/// `oakengine_node_get_brush`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_get_brush(
	self_: *const OakEngineNode,
	_top: f64,
	_bottom: f64,
	_out_qbrush: *mut c_void,
) {
	// Stub: oaknode has no QBrush surface (the C++ writes the node's
	// title-bar brush into a caller QBrush; Qt-only).
	guard_void(|| unsafe {
		let _ = unbox(self_);
	})
}

// ---------------------------------------------------------------------------
// node.h — input introspection
// ---------------------------------------------------------------------------

/// `oakengine_node_input_count`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_input_count(self_: *const OakEngineNode) -> c_int {
	guard_int(|| unsafe {
		if self_.is_null() {
			return Ok(0);
		}
		let mut count: c_int = 0;
		Error::from_module(n::oaknode_node_input_count(unbox(self_)?, &mut count))?;
		Ok(count)
	})
}

/// `oakengine_node_input_id` — input id at `index`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_input_id(
	self_: *const OakEngineNode,
	index: c_int,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	guard_int(|| unsafe {
		let h = unbox(self_)?;
		let mut count: c_int = 0;
		Error::from_module(n::oaknode_node_input_count(h, &mut count))?;
		if index < 0 || index >= count {
			return Err(Error::NotFound);
		}
		let rc = n::oaknode_node_input_id(h, index, buf, buf_size);
		if rc < 0 {
			Err(Error::Module(rc))
		} else {
			Ok(string_result(rc))
		}
	})
}

/// `oakengine_node_input_get_type` — value type as oak_node_value_type.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_input_get_type(
	self_: *const OakEngineNode,
	input_id: *const c_char,
) -> c_int {
	guard_int(|| unsafe {
		if self_.is_null() || input_id.is_null() {
			return Ok(0);
		}
		let mut ty: c_int = 0;
		Error::from_module(n::oaknode_node_input_get_type(unbox(self_)?, input_id, &mut ty))?;
		Ok(ty)
	})
}

/// `oakengine_node_input_is_connected`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_input_is_connected(
	self_: *const OakEngineNode,
	input_id: *const c_char,
) -> c_int {
	guard_int(|| unsafe {
		if self_.is_null() || input_id.is_null() {
			return Ok(0);
		}
		let mut value: c_int = 0;
		Error::from_module(n::oaknode_node_input_is_connected(
			unbox(self_)?,
			input_id,
			&mut value,
		))?;
		Ok(value)
	})
}

// ---------------------------------------------------------------------------
// node.h — parameter access
// ---------------------------------------------------------------------------

/// `oakengine_node_get_input` — standard value mapped into the POD.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_get_input(
	self_: *const OakEngineNode,
	input_id: *const c_char,
	out: *mut OakNodeValue,
) -> c_int {
	guard(|| unsafe {
		if self_.is_null() || input_id.is_null() || out.is_null() {
			set_node_error("invalid arguments");
			return Err(Error::Invalid);
		}
		let rc = n::oaknode_node_get_input(unbox(self_)?, input_id, out);
		Error::from_module(rc)
	})
}

/// `oakengine_node_set_input` — undoable standard-value write.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_set_input(
	self_: *mut OakEngineNode,
	input_id: *const c_char,
	v: *const OakNodeValue,
) -> c_int {
	guard(|| unsafe {
		if self_.is_null() || input_id.is_null() || v.is_null() {
			set_node_error("invalid arguments");
			return Err(Error::Invalid);
		}
		let h = unbox(self_)?;
		let mut cmd: CHandle = CHandle::null();
		let rc = n::oaknode_node_set_input_undoable(h, input_id, v, &mut cmd);
		if rc != 0 {
			return Err(Error::Module(rc));
		}
		push_command(cmd, "Set Node Value")
	})
}

/// `oakengine_node_get_input_string` — string input read (buf/size).
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_get_input_string(
	self_: *const OakEngineNode,
	input_id: *const c_char,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	guard_int(|| unsafe {
		if self_.is_null() || input_id.is_null() {
			set_node_error("invalid arguments");
			return Err(Error::Invalid);
		}
		let rc = n::oaknode_node_get_input_string(unbox(self_)?, input_id, buf, buf_size);
		if rc < 0 {
			Err(Error::Module(rc))
		} else {
			Ok(string_result(rc))
		}
	})
}

/// `oakengine_node_set_input_string` — undoable string write.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_set_input_string(
	self_: *mut OakEngineNode,
	input_id: *const c_char,
	s: *const c_char,
) -> c_int {
	guard(|| unsafe {
		if self_.is_null() || input_id.is_null() {
			set_node_error("invalid arguments");
			return Err(Error::Invalid);
		}
		let h = unbox(self_)?;
		let s = if s.is_null() {
			crate::common::empty_cstr()
		} else {
			s
		};
		let mut cmd: CHandle = CHandle::null();
		let rc = n::oaknode_node_set_input_string_undoable(h, input_id, s, &mut cmd);
		if rc != 0 {
			return Err(Error::Module(rc));
		}
		push_command(cmd, "Set Node Value")
	})
}

/// `oakengine_node_set_standard_value_command` — opaque command pointer.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_set_standard_value_command(
	self_: *mut OakEngineNode,
	input_id: *const c_char,
	_element: c_int,
	_track: c_int,
	v: *const OakNodeValue,
) -> *mut c_void {
	guard_ptr(|| unsafe {
		if self_.is_null() || input_id.is_null() || v.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let h = unbox(self_)?;
		let mut cmd: CHandle = CHandle::null();
		let rc = n::oaknode_node_set_input_undoable(h, input_id, v, &mut cmd);
		if rc != 0 {
			return Ok(std::ptr::null_mut());
		}
		Ok(command_box(cmd)?.cast())
	})
}

/// `oakengine_node_set_input_video_params_command`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_set_input_video_params_command(
	self_: *mut OakEngineNode,
	input_id: *const c_char,
	_params: *const OakVideoParamsPod,
) -> *mut c_void {
	// Stub: the oaknode module has no k_video_params command creator.
	guard_ptr(|| unsafe {
		if self_.is_null() || input_id.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let _ = unbox(self_)?;
		Ok(std::ptr::null_mut())
	})
}

/// `oakengine_node_set_value_at_time_command` — opaque command pointer.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_set_value_at_time_command(
	node: *mut c_void,
	input: *const c_char,
	_element: c_int,
	time_num: i64,
	time_den: i64,
	value: *const OakNodeValue,
	track: c_int,
	insert_on_all_tracks_if_no_key: c_int,
) -> *mut c_void {
	guard_ptr(|| unsafe {
		if node.is_null() || input.is_null() || value.is_null() || time_den == 0 {
			return Ok(std::ptr::null_mut());
		}
		let h = unbox(node.cast::<OakEngineNode>())?;
		let mut cmd: CHandle = CHandle::null();
		let rc = n::oaknode_node_set_input_at_time_undoable(
			h,
			input,
			time_num,
			time_den,
			value,
			track,
			&mut cmd,
		);
		if rc != 0 {
			return Ok(std::ptr::null_mut());
		}
		let _ = insert_on_all_tracks_if_no_key;
		Ok(command_box(cmd)?.cast())
	})
}

/// `oakengine_node_frame_time_base` — seconds per frame of the project's
/// first sequence (flipped), or the engine default 1001/30000.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_frame_time_base(
	self_: *const OakEngineNode,
	num: *mut c_int,
	den: *mut c_int,
) -> c_int {
	guard(|| unsafe {
		if self_.is_null() {
			return Err(Error::Invalid);
		}
		let h = unbox(self_)?;
		let (n_, d_) = time_base_for(h);
		if !num.is_null() {
			*num = n_ as c_int;
		}
		if !den.is_null() {
			*den = d_ as c_int;
		}
		Ok(())
	})
}

/// `oakengine_node_set_input_at_time` — undoable value at a frame
/// timestamp (set_value_at_time semantics).
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_set_input_at_time(
	self_: *mut OakEngineNode,
	input_id: *const c_char,
	element: c_int,
	time_ts: i64,
	track: c_int,
	v: *const OakNodeValue,
	insert_on_all_tracks: c_int,
) -> c_int {
	guard(|| unsafe {
		if self_.is_null() || input_id.is_null() || v.is_null() {
			set_node_error("invalid arguments");
			return Err(Error::Invalid);
		}
		let h = unbox(self_)?;
		let tb = time_base_for(h);
		let (t_num, t_den) = ts_to_time(time_ts, tb);
		let mut cmd: CHandle = CHandle::null();
		// The module's at-time setter is whole-value and ignores the track
		// selector; track -1 (all components) and per-component tracks
		// collapse to the whole value (documented deviation).
		let rc = n::oaknode_node_set_input_at_time_undoable(
			h,
			input_id,
			t_num,
			t_den,
			v,
			if track < 0 { 0 } else { track },
			&mut cmd,
		);
		if rc != 0 {
			return Err(Error::Module(rc));
		}
		let _ = element;
		let _ = insert_on_all_tracks;
		push_command(cmd, "Set Input Value")
	})
}

/// `oakengine_node_set_input_string_at_time`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_set_input_string_at_time(
	self_: *mut OakEngineNode,
	input_id: *const c_char,
	_element: c_int,
	_time_ts: i64,
	_value: *const c_char,
) -> c_int {
	// Stub: the module's at-time setter is POD-only; string inputs have no
	// value-at-time path.
	guard(|| unsafe {
		if self_.is_null() || input_id.is_null() {
			return Err(Error::Invalid);
		}
		let _ = unbox(self_)?;
		Err(Error::Invalid)
	})
}

// ---------------------------------------------------------------------------
// node.h — array inputs
// ---------------------------------------------------------------------------

/// `oakengine_node_array_insert_at` — insert an array element (undoable).
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_array_insert_at(
	self_: *mut OakEngineNode,
	input_id: *const c_char,
	index: c_int,
) -> c_int {
	guard(|| unsafe {
		if self_.is_null() || input_id.is_null() || index < 0 {
			set_node_error("invalid arguments");
			return Err(Error::Invalid);
		}
		let h = unbox(self_)?;
		let mut ty: c_int = 0;
		let rc = n::oaknode_node_input_get_type(h, input_id, &mut ty);
		if rc != 0 {
			return Err(Error::Module(rc));
		}
		// The module has no undoable array-insert command; the live insert
		// is applied directly (documented deviation).
		let rc = n::oaknode_node_input_array_insert(h, input_id, index);
		Error::from_module(rc)
	})
}

/// `oakengine_node_array_remove_at` — remove an array element (undoable).
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_array_remove_at(
	self_: *mut OakEngineNode,
	input_id: *const c_char,
	index: c_int,
) -> c_int {
	guard(|| unsafe {
		if self_.is_null() || input_id.is_null() || index < 0 {
			set_node_error("invalid arguments");
			return Err(Error::Invalid);
		}
		let h = unbox(self_)?;
		// The module has no undoable array-remove command; the live remove
		// is applied directly (documented deviation).
		let rc = n::oaknode_node_input_array_remove(h, input_id, index);
		Error::from_module(rc)
	})
}

// ---------------------------------------------------------------------------
// node.h — graph editing
// ---------------------------------------------------------------------------

/// `oakengine_project_add_node` — create a node of `type_id` in the
/// project (undoable).
#[no_mangle]
pub unsafe extern "C" fn oakengine_project_add_node(
	project: *mut OakEngineProject,
	type_id: *const c_char,
) -> *mut OakEngineNode {
	guard_ptr(|| unsafe {
		if project.is_null() || type_id.is_null() {
			set_node_error("invalid project or type id");
			return Ok(std::ptr::null_mut());
		}
		let ph = unbox(project)?;
		// The identities of the nodes already in the project; the newly
		// added node is the one whose identity is new.
		let existing: Vec<usize> = (0..n::oaknode_project_node_count(ph))
			.filter_map(|i| {
				let other = n::oaknode_project_node_at(ph, i);
				if other.is_null() {
					None
				} else {
					let id = n::oaknode_node_identity(other);
					if id == 0 {
						None
					} else {
						Some(id)
					}
				}
			})
			.collect();
		let node = n::oaknode_factory_create_from_id(type_id);
		if node.ctx.is_null() {
			set_node_error(&format!("unknown node type id \"{}\"", read_cstr(type_id)));
			return Ok(std::ptr::null_mut());
		}
		let cmd = n::oaknode_command_create_add_node(ph, node);
		if cmd.ctx.is_null() {
			return Ok(std::ptr::null_mut());
		}
		push_command(cmd, "Add Node")?;
		// The module's command-based add moves the node without rewriting
		// the caller's handle, so a fresh borrowed view is derived from
		// the project graph (the capi's `wrap(node)` has no module
		// equivalent for the command path).
		let type_id_str = read_cstr(type_id);
		let total = n::oaknode_project_node_count(ph);
		for i in 0..total {
			let other = n::oaknode_project_node_at(ph, i);
			if other.is_null() {
				continue;
			}
			let id = n::oaknode_node_identity(other);
			if existing.contains(&id) {
				continue;
			}
			if is_node_type(other, &type_id_str) {
				return Ok(box_handle::<OakEngineNode>(other));
			}
		}
		// Fallback: return the (stale) factory handle; read-only metadata
		// queries still work while the node lived in its source project.
		Ok(box_handle::<OakEngineNode>(node))
	})
}

/// `oakengine_project_remove_node` — remove a node, disconnecting edges
/// (undoable).
#[no_mangle]
pub unsafe extern "C" fn oakengine_project_remove_node(
	project: *mut OakEngineProject,
	node: *mut OakEngineNode,
) -> c_int {
	guard(|| unsafe {
		if project.is_null() || node.is_null() {
			set_node_error("invalid project or node");
			return Err(Error::Invalid);
		}
		let ph = unbox(project)?;
		let nh = unbox(node)?;
		// Verify the node belongs to this project.
		if !project_contains_node(ph, nh) {
			set_node_error("node does not belong to this project");
			return Err(Error::Invalid);
		}
		let cmd = n::oaknode_command_create_remove_node(nh);
		if cmd.ctx.is_null() {
			return Err(Error::Failed("remove node command failed".into()));
		}
		push_command(cmd, "Remove Node")
	})
}

/// `oakengine_node_delete_later` — deferred deletion (Qt `deleteLater`).
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_delete_later(node: *mut OakEngineNode) {
	// Stub: there is no event-loop based deferred deletion in the module
	// world; orphaned nodes are freed synchronously with
	// `oakengine_node_free` instead.
	guard_void(|| unsafe {
		let _ = unbox(node);
	})
}

/// `oakengine_node_free` — destroy an OWNED, orphaned node immediately.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_free(node: *mut OakEngineNode) {
	guard_void(|| unsafe {
		if node.is_null() {
			return;
		}
		let mut h = (*node).handle;
		n::oaknode_node_free(&mut h);
		drop(Box::from_raw(node));
	})
}

/// `oakengine_node_connect` — undoable edge add.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_connect(
	output_node: *mut OakEngineNode,
	input_node: *mut OakEngineNode,
	input_id: *const c_char,
) -> c_int {
	guard(|| unsafe {
		if output_node.is_null() || input_node.is_null() || input_id.is_null() {
			set_node_error("invalid arguments");
			return Err(Error::Invalid);
		}
		let out_h = unbox(output_node)?;
		let in_h = unbox(input_node)?;
		let mut cmd: CHandle = CHandle::null();
		let rc = n::oaknode_node_connect_undoable(out_h, in_h, input_id, &mut cmd);
		if rc != 0 {
			// Distinguish the module's "already connected" state from other
			// failures so the caller gets the engine's E_STATE.
			return Err(Error::Module(rc));
		}
		push_command(cmd, "Connect Nodes")
	})
}

/// `oakengine_node_disconnect` — remove the edge (element -1).
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_disconnect(
	input_node: *mut OakEngineNode,
	input_id: *const c_char,
) -> c_int {
	guard(|| unsafe {
		let rc = oakengine_node_disconnect_ex(input_node, input_id, -1);
		Error::from_module(rc)
	})
}

/// `oakengine_node_disconnect_ex` — remove the edge at `element`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_disconnect_ex(
	input_node: *mut OakEngineNode,
	input_id: *const c_char,
	element: c_int,
) -> c_int {
	guard(|| unsafe {
		if input_node.is_null() || input_id.is_null() {
			set_node_error("invalid arguments");
			return Err(Error::Invalid);
		}
		let in_h = unbox(input_node)?;
		// The module's disconnect command ignores the element; a
		// whole-input disconnect is the only variant.
		let _ = element;
		let mut cmd: CHandle = CHandle::null();
		let rc = n::oaknode_node_disconnect_undoable(in_h, input_id, &mut cmd);
		if rc != 0 {
			return Err(Error::Module(rc));
		}
		push_command(cmd, "Disconnect Nodes")
	})
}

/// `oakengine_node_connect_command` — opaque NodeEdgeAddCommand pointer.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_connect_command(
	output_node: *mut OakEngineNode,
	input_node: *mut OakEngineNode,
	input_id: *const c_char,
	element: c_int,
) -> *mut c_void {
	guard_ptr(|| unsafe {
		if output_node.is_null() || input_node.is_null() || input_id.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let out_h = unbox(output_node)?;
		let in_h = unbox(input_node)?;
		let _ = element;
		let mut cmd: CHandle = CHandle::null();
		let rc = n::oaknode_node_connect_undoable(out_h, in_h, input_id, &mut cmd);
		if rc != 0 {
			return Ok(std::ptr::null_mut());
		}
		Ok(command_box(cmd)?.cast())
	})
}

/// `oakengine_node_disconnect_command` — opaque NodeEdgeRemoveCommand.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_disconnect_command(
	input_node: *mut OakEngineNode,
	input_id: *const c_char,
	element: c_int,
) -> *mut c_void {
	guard_ptr(|| unsafe {
		if input_node.is_null() || input_id.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let in_h = unbox(input_node)?;
		let _ = element;
		let mut cmd: CHandle = CHandle::null();
		let rc = n::oaknode_node_disconnect_undoable(in_h, input_id, &mut cmd);
		if rc != 0 {
			return Ok(std::ptr::null_mut());
		}
		Ok(command_box(cmd)?.cast())
	})
}

/// `oakengine_block_link` — link/unlink two nodes directly.
#[no_mangle]
pub unsafe extern "C" fn oakengine_block_link(
	a: *mut c_void,
	b: *mut c_void,
	linked: c_int,
) -> c_int {
	guard_int(|| unsafe {
		if a.is_null() || b.is_null() {
			return Err(Error::Invalid);
		}
		let ah = unbox(a.cast::<OakEngineNode>())?;
		let bh = unbox(b.cast::<OakEngineNode>())?;
		let mut value: c_int = 0;
		let rc = if linked != 0 {
			n::oaknode_node_link(ah, bh, &mut value)
		} else {
			n::oaknode_node_unlink(ah, bh, &mut value)
		};
		if rc != 0 {
			return Err(Error::Module(rc));
		}
		Ok(value)
	})
}

/// `oakengine_node_add_to_project_command` — opaque NodeAddCommand.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_add_to_project_command(
	project: *mut OakEngineProject,
	node: *mut OakEngineNode,
) -> *mut c_void {
	guard_ptr(|| unsafe {
		if project.is_null() || node.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let ph = unbox(project)?;
		let nh = unbox(node)?;
		let cmd = n::oaknode_command_create_add_node(ph, nh);
		if cmd.ctx.is_null() {
			return Ok(std::ptr::null_mut());
		}
		Ok(command_box(cmd)?.cast())
	})
}

/// `oakengine_node_set_value_hint` — traverse value hint on an input.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_set_value_hint(
	self_: *mut OakEngineNode,
	input_id: *const c_char,
	element: c_int,
	type_: c_int,
	index: c_int,
	tag: *const c_char,
) -> c_int {
	guard(|| unsafe {
		if self_.is_null() || input_id.is_null() {
			set_node_error("invalid arguments");
			return Err(Error::Invalid);
		}
		let h = unbox(self_)?;
		// The module's hint setter is a simplified single-type variant; the
		// element and tag are not representable (documented deviation).
		let _ = element;
		let _ = tag;
		let rc = n::oaknode_node_set_value_hint_track(h, input_id, type_, index);
		Error::from_module(rc)
	})
}

// ---------------------------------------------------------------------------
// node.h — parameter animation (keyframes)
// ---------------------------------------------------------------------------

/// `oakengine_node_input_is_keyframed`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_input_is_keyframed(
	self_: *const OakEngineNode,
	input_id: *const c_char,
) -> c_int {
	// Stub: the oaknode module has no keyframing-enabled query.
	guard_int(|| unsafe {
		if self_.is_null() || input_id.is_null() {
			return Ok(0);
		}
		let _ = unbox(self_)?;
		Ok(0)
	})
}

/// `oakengine_node_keyframe_count` — keyframes on track 0.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_keyframe_count(
	self_: *const OakEngineNode,
	input_id: *const c_char,
) -> c_int {
	// Stub: the oaknode module has no keyframe enumeration C ABI.
	guard_int(|| unsafe {
		if self_.is_null() || input_id.is_null() {
			return Ok(0);
		}
		let _ = unbox(self_)?;
		Ok(0)
	})
}

/// `oakengine_node_keyframe_at` — read keyframe at `index`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_keyframe_at(
	self_: *const OakEngineNode,
	input_id: *const c_char,
	index: c_int,
	time_ts: *mut i64,
	value: *mut OakNodeValue,
) -> c_int {
	// Stub: see `oakengine_node_keyframe_count`.
	guard(|| unsafe {
		if self_.is_null() || input_id.is_null() {
			return Err(Error::Invalid);
		}
		let _ = unbox(self_)?;
		let _ = index;
		let _ = time_ts;
		let _ = value;
		Err(Error::NotFound)
	})
}

/// `oakengine_node_keyframe_get_easing`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_keyframe_get_easing(
	self_: *const OakEngineNode,
	input_id: *const c_char,
	index: c_int,
	x1: *mut f32,
	y1: *mut f32,
	x2: *mut f32,
	y2: *mut f32,
	type_: *mut c_int,
) -> c_int {
	// Stub: see `oakengine_node_keyframe_count`.
	guard(|| unsafe {
		if self_.is_null() || input_id.is_null() {
			return Err(Error::Invalid);
		}
		let _ = unbox(self_)?;
		let _ = (index, x1, y1, x2, y2, type_);
		Err(Error::NotFound)
	})
}

/// `oakengine_node_keyframe_add` — add a keyframe at `time_ts`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_keyframe_add(
	self_: *mut OakEngineNode,
	input_id: *const c_char,
	time_ts: i64,
	value: *const OakNodeValue,
	type_: c_int,
	x1: f32,
	y1: f32,
	x2: f32,
	y2: f32,
) -> c_int {
	guard(|| unsafe {
		if self_.is_null() || input_id.is_null() || value.is_null() || type_ < 0 || type_ > 2 {
			set_node_error("invalid arguments or easing type");
			return Err(Error::Invalid);
		}
		let h = unbox(self_)?;
		let tb = time_base_for(h);
		let (t_num, t_den) = ts_to_time(time_ts, tb);
		// The module's set_value_at_time is the closest analogue of the
		// engine's insert path: on a keyframed input it inserts/updates the
		// key, otherwise it writes the standard value (the module cannot
		// enable keyframing, so the "already exists" E_STATE check and the
		// keyframing enablement of the capi are not reachable).
		let mut cmd: CHandle = CHandle::null();
		let rc = n::oaknode_node_set_input_at_time_undoable(
			h,
			input_id,
			t_num,
			t_den,
			value,
			0,
			&mut cmd,
		);
		if rc != 0 {
			return Err(Error::Module(rc));
		}
		let _ = (x1, y1, x2, y2);
		push_command(cmd, "Add Keyframe")
	})
}

/// `oakengine_node_keyframe_remove`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_keyframe_remove(
	self_: *mut OakEngineNode,
	input_id: *const c_char,
	time_ts: i64,
) -> c_int {
	// Stub: the oaknode module has no remove-keyframe C ABI.
	guard(|| unsafe {
		if self_.is_null() || input_id.is_null() {
			return Err(Error::Invalid);
		}
		let _ = unbox(self_)?;
		let _ = time_ts;
		Err(Error::NotFound)
	})
}

/// `oakengine_node_insert_keyframe_command` — opaque command pointer.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_insert_keyframe_command(
	self_: *mut OakEngineNode,
	input_id: *const c_char,
	element: c_int,
	track: c_int,
	time_ts: i64,
	value: *const OakNodeValue,
	type_: c_int,
	x1: f32,
	y1: f32,
	x2: f32,
	y2: f32,
) -> *mut c_void {
	guard_ptr(|| unsafe {
		if self_.is_null() || input_id.is_null() || value.is_null() || type_ < 0 || type_ > 2 {
			return Ok(std::ptr::null_mut());
		}
		let h = unbox(self_)?;
		let tb = time_base_for(h);
		let (t_num, t_den) = ts_to_time(time_ts, tb);
		let mut cmd: CHandle = CHandle::null();
		let rc = n::oaknode_node_set_input_at_time_undoable(
			h,
			input_id,
			t_num,
			t_den,
			value,
			track,
			&mut cmd,
		);
		if rc != 0 {
			return Ok(std::ptr::null_mut());
		}
		let _ = element;
		let _ = (x1, y1, x2, y2);
		Ok(command_box(cmd)?.cast())
	})
}

/// `oakengine_node_remove_keyframe_command`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_remove_keyframe_command(
	keyframe: *mut OakEngineKeyframe,
) -> *mut c_void {
	// Stub: the module has no remove-keyframe command creator (the module
	// keyframe handles are detached values, not track members).
	guard_ptr(|| unsafe {
		let _ = unbox(keyframe);
		Ok(std::ptr::null_mut())
	})
}

/// `oakengine_keyframe_set_time_command`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_keyframe_set_time_command(
	keyframe: *mut OakEngineKeyframe,
	new_time_ts: i64,
) -> *mut c_void {
	guard_ptr(|| unsafe {
		let h = unbox(keyframe)?;
		// The keyframe handle's own project is not reachable from the
		// module keyframe box, so the default time base applies.
		let tb = (1001, 30000);
		let (t_num, t_den) = ts_to_time(new_time_ts, tb);
		let mut cmd: CHandle = CHandle::null();
		let rc = n::oaknode_keyframe_set_time_undoable(h, t_num, t_den, &mut cmd);
		if rc != 0 {
			return Ok(std::ptr::null_mut());
		}
		Ok(command_box(cmd)?.cast())
	})
}

/// `oakengine_keyframe_set_value_command`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_keyframe_set_value_command(
	keyframe: *mut OakEngineKeyframe,
	value: *const OakNodeValue,
) -> *mut c_void {
	guard_ptr(|| unsafe {
		let h = unbox(keyframe)?;
		if value.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let mut cmd: CHandle = CHandle::null();
		let rc = n::oaknode_keyframe_set_value_undoable(h, value, &mut cmd);
		if rc != 0 {
			return Ok(std::ptr::null_mut());
		}
		Ok(command_box(cmd)?.cast())
	})
}

/// `oakengine_node_keyframe_set_easing`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_keyframe_set_easing(
	self_: *mut OakEngineNode,
	input_id: *const c_char,
	time_ts: i64,
	type_: c_int,
	x1: f32,
	y1: f32,
	x2: f32,
	y2: f32,
) -> c_int {
	// Stub: see `oakengine_node_keyframe_count` (no easing accessors).
	guard(|| unsafe {
		if self_.is_null() || input_id.is_null() {
			return Err(Error::Invalid);
		}
		let _ = unbox(self_)?;
		let _ = (time_ts, type_, x1, y1, x2, y2);
		Err(Error::NotFound)
	})
}

/// `oakengine_node_keyframes_set_type_many`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_keyframes_set_type_many(
	self_: *mut OakEngineNode,
	input_id: *const c_char,
	element: c_int,
	times_ts: *const i64,
	tracks: *const c_int,
	count: c_int,
	type_: c_int,
) -> c_int {
	// Stub: see `oakengine_node_keyframe_count`.
	guard_int(|| unsafe {
		if self_.is_null() || input_id.is_null() {
			return Err(Error::Invalid);
		}
		let _ = unbox(self_)?;
		let _ = (element, times_ts, tracks, count, type_);
		Err(Error::NotFound)
	})
}

/// `oakengine_node_keyframes_set_time_many`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_keyframes_set_time_many(
	self_: *mut OakEngineNode,
	input_id: *const c_char,
	element: c_int,
	old_times_ts: *const i64,
	tracks: *const c_int,
	count: c_int,
	new_time_ts: i64,
) -> c_int {
	// Stub: see `oakengine_node_keyframe_count`.
	guard_int(|| unsafe {
		if self_.is_null() || input_id.is_null() {
			return Err(Error::Invalid);
		}
		let _ = unbox(self_)?;
		let _ = (element, old_times_ts, tracks, count, new_time_ts);
		Err(Error::NotFound)
	})
}

/// `oakengine_node_keyframes_set_value_many`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_keyframes_set_value_many(
	self_: *mut OakEngineNode,
	input_id: *const c_char,
	element: c_int,
	times_ts: *const i64,
	tracks: *const c_int,
	count: c_int,
	values: *const OakNodeValue,
	old_values: *const OakNodeValue,
) -> c_int {
	// Stub: see `oakengine_node_keyframe_count`.
	guard_int(|| unsafe {
		if self_.is_null() || input_id.is_null() {
			return Err(Error::Invalid);
		}
		let _ = unbox(self_)?;
		let _ = (element, times_ts, tracks, count, values, old_values);
		Err(Error::NotFound)
	})
}

/// `oakengine_node_keyframes_set_bezier_many`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_keyframes_set_bezier_many(
	self_: *mut OakEngineNode,
	input_id: *const c_char,
	element: c_int,
	times_ts: *const i64,
	tracks: *const c_int,
	count: c_int,
	in_x: f64,
	in_y: f64,
	out_x: f64,
	out_y: f64,
) -> c_int {
	// Stub: see `oakengine_node_keyframe_count`.
	guard_int(|| unsafe {
		if self_.is_null() || input_id.is_null() {
			return Err(Error::Invalid);
		}
		let _ = unbox(self_)?;
		let _ = (element, times_ts, tracks, count, in_x, in_y, out_x, out_y);
		Err(Error::NotFound)
	})
}

/// `oakengine_node_keyframe_set_bezier_point`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_keyframe_set_bezier_point(
	self_: *mut OakEngineNode,
	input_id: *const c_char,
	element: c_int,
	time_ts: i64,
	track: c_int,
	point_index: c_int,
	x: f64,
	y: f64,
	old_x: f64,
	old_y: f64,
) -> c_int {
	// Stub: see `oakengine_node_keyframe_count`.
	guard(|| unsafe {
		if self_.is_null() || input_id.is_null() {
			return Err(Error::Invalid);
		}
		let _ = unbox(self_)?;
		let _ = (element, time_ts, track, point_index, x, y, old_x, old_y);
		Err(Error::NotFound)
	})
}

/// `oakengine_node_keyframes_clear` — remove all keyframes (no-op when
/// none).
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_keyframes_clear(
	self_: *mut OakEngineNode,
	input_id: *const c_char,
) -> c_int {
	// Stub: the module has no clear-keyframes command; since it also has
	// no keyframes, the documented no-op result is returned.
	guard(|| unsafe {
		if self_.is_null() || input_id.is_null() {
			return Err(Error::Invalid);
		}
		let _ = unbox(self_)?;
		Ok(())
	})
}

// ---------------------------------------------------------------------------
// node.h — extended input introspection
// ---------------------------------------------------------------------------

/// `oakengine_node_input_is_array`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_input_is_array(
	self_: *const OakEngineNode,
	input_id: *const c_char,
) -> c_int {
	// Stub: the oaknode module has no array-flag query.
	guard_int(|| unsafe {
		if self_.is_null() || input_id.is_null() {
			return Ok(0);
		}
		let _ = unbox(self_)?;
		Ok(0)
	})
}

/// `oakengine_node_input_array_size`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_input_array_size(
	self_: *const OakEngineNode,
	input_id: *const c_char,
) -> c_int {
	// Stub: see `oakengine_node_input_is_array`.
	guard_int(|| unsafe {
		if self_.is_null() || input_id.is_null() {
			return Ok(0);
		}
		let _ = unbox(self_)?;
		Ok(0)
	})
}

/// `oakengine_node_input_get_flags`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_input_get_flags(
	self_: *const OakEngineNode,
	input_id: *const c_char,
) -> c_int {
	// Stub: the oaknode module has no input-flags export.
	guard_int(|| unsafe {
		if self_.is_null() || input_id.is_null() {
			return Ok(0);
		}
		let _ = unbox(self_)?;
		Ok(0)
	})
}

/// `oakengine_node_input_get_data_type` — NodeValue::Type ordinal.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_input_get_data_type(
	self_: *const OakEngineNode,
	input_id: *const c_char,
) -> c_int {
	// Stub: the oaknode module reports the oak value type (which differs
	// from the olive::NodeValue::Type ordinal); the C++ ordinal is not
	// reachable.
	guard_int(|| unsafe {
		if self_.is_null() || input_id.is_null() {
			return Ok(-1);
		}
		let _ = unbox(self_)?;
		Ok(-1)
	})
}

/// `oakengine_node_input_is_connectable`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_input_is_connectable(
	self_: *const OakEngineNode,
	input_id: *const c_char,
) -> c_int {
	guard_int(|| unsafe {
		if self_.is_null() || input_id.is_null() {
			return Ok(0);
		}
		let mut value: c_int = 0;
		Error::from_module(n::oaknode_node_input_is_connectable(
			unbox(self_)?,
			input_id,
			&mut value,
		))?;
		Ok(value)
	})
}

/// `oakengine_node_input_is_keyframable`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_input_is_keyframable(
	self_: *const OakEngineNode,
	input_id: *const c_char,
) -> c_int {
	// Derived from the input's value type: the POD types (int..combo) are
	// keyframable, everything else is not (the module has no keyframable
	// query of its own).
	guard_int(|| unsafe {
		if self_.is_null() || input_id.is_null() {
			return Ok(0);
		}
		let mut ty: c_int = 0;
		Error::from_module(n::oaknode_node_input_get_type(unbox(self_)?, input_id, &mut ty))?;
		let keyframable = matches!(
			ty,
			value_type::INT
				| value_type::FLOAT
				| value_type::BOOL
				| value_type::RATIONAL
				| value_type::COLOR
				| value_type::VEC2
				| value_type::VEC3
				| value_type::VEC4
				| value_type::COMBO
		);
		Ok(if keyframable { 1 } else { 0 })
	})
}

/// `oakengine_node_input_is_hidden`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_input_is_hidden(
	self_: *const OakEngineNode,
	input_id: *const c_char,
) -> c_int {
	// Stub: the oaknode module has no input-flags export.
	guard_int(|| unsafe {
		if self_.is_null() || input_id.is_null() {
			return Ok(0);
		}
		let _ = unbox(self_)?;
		Ok(0)
	})
}

/// `oakengine_node_input_is_keyframed_ex`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_input_is_keyframed_ex(
	self_: *const OakEngineNode,
	input_id: *const c_char,
	element: c_int,
) -> c_int {
	// Stub: see `oakengine_node_input_is_keyframed`.
	guard_int(|| unsafe {
		if self_.is_null() || input_id.is_null() {
			return Ok(0);
		}
		let _ = unbox(self_)?;
		let _ = element;
		Ok(0)
	})
}

/// `oakengine_node_get_label_and_name`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_get_label_and_name(
	self_: *const OakEngineNode,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	guard_int(|| unsafe {
		let h = unbox(self_)?;
		let label =
			module_string(|b, s| n::oaknode_node_get_label(h, b, s))?;
		let name =
			module_string(|b, s| n::oaknode_node_get_name(h, b, s))?;
		// The engine's default `get_label_and_name()`: label when set,
		// otherwise the name.
		Ok(write_string(
			if label.is_empty() { &name } else { &label },
			buf,
			buf_size,
		))
	})
}

/// `oakengine_node_get_input_name`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_get_input_name(
	self_: *const OakEngineNode,
	input_id: *const c_char,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	guard_int(|| unsafe {
		if self_.is_null() || input_id.is_null() {
			return Err(Error::Invalid);
		}
		let rc = n::oaknode_node_get_input_name(unbox(self_)?, input_id, buf, buf_size);
		if rc < 0 {
			Err(Error::Module(rc))
		} else {
			Ok(string_result(rc))
		}
	})
}

/// `oakengine_node_input_get_default_value`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_input_get_default_value(
	self_: *const OakEngineNode,
	input_id: *const c_char,
	track: c_int,
	out: *mut OakNodeValue,
) -> c_int {
	// Stub: the oaknode module has no default-value export.
	guard(|| unsafe {
		if self_.is_null() || input_id.is_null() || out.is_null() {
			return Err(Error::Invalid);
		}
		let _ = unbox(self_)?;
		let _ = track;
		Err(Error::NotFound)
	})
}

/// `oakengine_node_get_project`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_get_project(
	self_: *const OakEngineNode,
) -> *mut OakEngineProject {
	guard_ptr(|| unsafe {
		if self_.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let mut out = CHandle::null();
		Error::from_module(n::oaknode_node_get_project(unbox(self_)?, &mut out))?;
		if out.is_null() {
			return Ok(std::ptr::null_mut());
		}
		Ok(box_handle::<OakEngineProject>(out))
	})
}

/// `oakengine_node_parent`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_parent(
	self_: *const OakEngineNode,
) -> *mut OakEngineProject {
	// The module has no graph-parent concept; the owning project is the
	// same value the C++ reports for both accessors.
	guard_ptr(|| unsafe {
		if self_.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let mut out = CHandle::null();
		Error::from_module(n::oaknode_node_get_project(unbox(self_)?, &mut out))?;
		if out.is_null() {
			return Ok(std::ptr::null_mut());
		}
		Ok(box_handle::<OakEngineProject>(out))
	})
}

/// `oakengine_node_is_item`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_is_item(self_: *const OakEngineNode) -> c_int {
	// Item = folder | footage | sequence | group (the project tree types).
	guard_int(|| unsafe {
		if self_.is_null() {
			return Ok(0);
		}
		let h = unbox(self_)?;
		Ok(if is_node_type(h, TYPE_ID_FOLDER)
			|| is_node_type(h, TYPE_ID_FOOTAGE)
			|| is_node_type(h, TYPE_ID_SEQUENCE)
			|| is_node_type(h, TYPE_ID_GROUP)
		{
			1
		} else {
			0
		})
	})
}

/// `oakengine_node_folder` — the folder owning this item node.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_folder(
	self_: *const OakEngineNode,
) -> *mut OakEngineNode {
	guard_ptr(|| unsafe {
		if self_.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let h = unbox(self_)?;
		// Walk the project's folders for a recursive parent relationship.
		if let Ok(project) = project_of(h) {
			let total = n::oaknode_project_node_count(project);
			for i in 0..total {
				let folder = n::oaknode_project_node_at(project, i);
				if folder.is_null() || !is_node_type(folder, TYPE_ID_FOLDER) {
					continue;
				}
				if n::oaknode_folder_has_child_recursive(folder, h) != 0 {
					return Ok(box_handle::<OakEngineNode>(folder));
				}
			}
		}
		Ok(std::ptr::null_mut())
	})
}

/// `oakengine_node_input_get_connected_node`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_input_get_connected_node(
	self_: *const OakEngineNode,
	input_id: *const c_char,
	element: c_int,
) -> *mut OakEngineNode {
	guard_ptr(|| unsafe {
		if self_.is_null() || input_id.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let h = unbox(self_)?;
		let _ = element;
		let mut out = CHandle::null();
		let rc = n::oaknode_node_input_get_connected_node(h, input_id, &mut out);
		if rc != 0 || out.is_null() {
			return Ok(std::ptr::null_mut());
		}
		Ok(box_handle::<OakEngineNode>(out))
	})
}

/// `oakengine_node_copy_inputs` — copy values from `src` to `dest`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_copy_inputs(
	dest: *mut OakEngineNode,
	src: *const OakEngineNode,
) -> c_int {
	guard(|| unsafe {
		if dest.is_null() || src.is_null() {
			set_node_error("invalid arguments");
			return Err(Error::Invalid);
		}
		let rc = n::oaknode_node_copy_inputs(unbox(dest)?, unbox(src)?, 0);
		Error::from_module(rc)
	})
}

/// `oakengine_node_get_input_at_time` — value at a frame timestamp.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_get_input_at_time(
	self_: *const OakEngineNode,
	input_id: *const c_char,
	element: c_int,
	track: c_int,
	time_ts: i64,
	track_for_time: c_int,
	out: *mut OakNodeValue,
) -> c_int {
	guard(|| unsafe {
		if self_.is_null() || input_id.is_null() || out.is_null() {
			set_node_error("invalid arguments");
			return Err(Error::Invalid);
		}
		let h = unbox(self_)?;
		let tb = time_base_for(h);
		let (t_num, t_den) = ts_to_time(time_ts, tb);
		let _ = (element, track, track_for_time);
		let rc = n::oaknode_node_get_input_at_time(h, input_id, t_num, t_den, out);
		Error::from_module(rc)
	})
}

/// `oakengine_node_get_input_string_at_time`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_get_input_string_at_time(
	self_: *const OakEngineNode,
	input_id: *const c_char,
	element: c_int,
	time_ts: i64,
	track: c_int,
	_buf: *mut c_char,
	_buf_size: c_int,
) -> c_int {
	// Stub: the module's at-time reader is POD-only.
	guard_int(|| unsafe {
		if self_.is_null() || input_id.is_null() {
			return Err(Error::Invalid);
		}
		let _ = unbox(self_)?;
		let _ = (element, time_ts, track);
		Err(Error::Invalid)
	})
}

/// `oakengine_node_get_input_bezier_at_time`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_get_input_bezier_at_time(
	self_: *const OakEngineNode,
	input_id: *const c_char,
	element: c_int,
	time_ts: i64,
	track: c_int,
	out_6: *mut f64,
) -> c_int {
	// Stub: the module's at-time reader is POD-only (no bezier).
	guard(|| unsafe {
		if self_.is_null() || input_id.is_null() || out_6.is_null() {
			return Err(Error::Invalid);
		}
		let _ = unbox(self_)?;
		let _ = (element, time_ts, track);
		Err(Error::Invalid)
	})
}

/// `oakengine_node_get_input_binary_at_time`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_get_input_binary_at_time(
	self_: *const OakEngineNode,
	input_id: *const c_char,
	element: c_int,
	time_ts: i64,
	track: c_int,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	// Stub: the module's at-time reader is POD-only (no binary).
	guard_int(|| unsafe {
		if self_.is_null() || input_id.is_null() {
			return Err(Error::Invalid);
		}
		let _ = unbox(self_)?;
		let _ = (element, time_ts, track, buf, buf_size);
		Err(Error::Invalid)
	})
}

// ---------------------------------------------------------------------------
// node.h — input properties
// ---------------------------------------------------------------------------

/// `oakengine_node_input_has_property`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_input_has_property(
	self_: *const OakEngineNode,
	input_id: *const c_char,
	key: *const c_char,
) -> c_int {
	// Stub: the oaknode module has no input-property C ABI.
	guard_int(|| unsafe {
		if self_.is_null() || input_id.is_null() || key.is_null() {
			return Ok(0);
		}
		let _ = unbox(self_)?;
		Ok(0)
	})
}

/// `oakengine_node_set_input_property_string`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_set_input_property_string(
	self_: *mut OakEngineNode,
	input_id: *const c_char,
	key: *const c_char,
	value: *const c_char,
	notify: c_int,
) -> c_int {
	// Stub: see `oakengine_node_input_has_property`.
	guard(|| unsafe {
		if self_.is_null() || input_id.is_null() || key.is_null() {
			return Err(Error::Invalid);
		}
		let _ = unbox(self_)?;
		let _ = (value, notify);
		Err(Error::NotFound)
	})
}

/// `oakengine_node_input_get_property_string`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_input_get_property_string(
	self_: *const OakEngineNode,
	input_id: *const c_char,
	key: *const c_char,
	_buf: *mut c_char,
	_buf_size: c_int,
) -> c_int {
	// Stub: see `oakengine_node_input_has_property`.
	guard_int(|| unsafe {
		if self_.is_null() || input_id.is_null() || key.is_null() {
			return Err(Error::Invalid);
		}
		let _ = unbox(self_)?;
		Err(Error::NotFound)
	})
}

/// `oakengine_node_input_get_property_number`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_input_get_property_number(
	self_: *const OakEngineNode,
	input_id: *const c_char,
	key: *const c_char,
	track: c_int,
	out: *mut f64,
) -> c_int {
	// Stub: see `oakengine_node_input_has_property`.
	guard(|| unsafe {
		if self_.is_null() || input_id.is_null() || key.is_null() || out.is_null() {
			return Err(Error::Invalid);
		}
		let _ = unbox(self_)?;
		let _ = track;
		Err(Error::NotFound)
	})
}

/// `oakengine_node_input_get_property_int`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_input_get_property_int(
	self_: *const OakEngineNode,
	input_id: *const c_char,
	key: *const c_char,
	out: *mut i64,
) -> c_int {
	// Stub: see `oakengine_node_input_has_property`.
	guard(|| unsafe {
		if self_.is_null() || input_id.is_null() || key.is_null() || out.is_null() {
			return Err(Error::Invalid);
		}
		let _ = unbox(self_)?;
		Err(Error::NotFound)
	})
}

/// `oakengine_node_input_get_property_rational`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_input_get_property_rational(
	self_: *const OakEngineNode,
	input_id: *const c_char,
	key: *const c_char,
	num: *mut c_int,
	den: *mut c_int,
) -> c_int {
	// Stub: see `oakengine_node_input_has_property`.
	guard(|| unsafe {
		if self_.is_null() || input_id.is_null() || key.is_null() {
			return Err(Error::Invalid);
		}
		let _ = unbox(self_)?;
		let _ = (num, den);
		Err(Error::NotFound)
	})
}

/// `oakengine_node_input_get_property_track_number`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_input_get_property_track_number(
	self_: *const OakEngineNode,
	input_id: *const c_char,
	key: *const c_char,
	track: c_int,
	out: *mut f64,
) -> c_int {
	// Stub: see `oakengine_node_input_has_property`.
	guard(|| unsafe {
		if self_.is_null() || input_id.is_null() || key.is_null() || out.is_null() {
			return Err(Error::Invalid);
		}
		let _ = unbox(self_)?;
		let _ = track;
		Err(Error::NotFound)
	})
}

/// `oakengine_node_input_get_property_count`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_input_get_property_count(
	self_: *const OakEngineNode,
	input_id: *const c_char,
) -> c_int {
	// Stub: see `oakengine_node_input_has_property`.
	guard_int(|| unsafe {
		if self_.is_null() || input_id.is_null() {
			return Ok(0);
		}
		let _ = unbox(self_)?;
		Ok(0)
	})
}

/// `oakengine_node_input_get_property_key`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_input_get_property_key(
	self_: *const OakEngineNode,
	input_id: *const c_char,
	index: c_int,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	// Stub: see `oakengine_node_input_has_property`.
	guard_int(|| unsafe {
		if self_.is_null() || input_id.is_null() {
			return Err(Error::Invalid);
		}
		let _ = unbox(self_)?;
		let _ = (index, buf, buf_size);
		Err(Error::NotFound)
	})
}

/// `oakengine_node_input_get_property_string_list_count`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_input_get_property_string_list_count(
	self_: *const OakEngineNode,
	input_id: *const c_char,
	key: *const c_char,
) -> c_int {
	// Stub: see `oakengine_node_input_has_property`.
	guard_int(|| unsafe {
		if self_.is_null() || input_id.is_null() || key.is_null() {
			return Ok(0);
		}
		let _ = unbox(self_)?;
		Ok(0)
	})
}

/// `oakengine_node_input_get_property_string_list`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_input_get_property_string_list(
	self_: *const OakEngineNode,
	input_id: *const c_char,
	key: *const c_char,
	index: c_int,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	// Stub: see `oakengine_node_input_has_property`.
	guard_int(|| unsafe {
		if self_.is_null() || input_id.is_null() || key.is_null() {
			return Err(Error::Invalid);
		}
		let _ = unbox(self_)?;
		let _ = (index, buf, buf_size);
		Err(Error::NotFound)
	})
}

// ---------------------------------------------------------------------------
// node.h — node type queries
// ---------------------------------------------------------------------------

/// `oakengine_node_is_group`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_is_group(self_: *const OakEngineNode) -> c_int {
	guard_int(|| unsafe {
		if self_.is_null() {
			return Ok(0);
		}
		Ok(if is_node_type(unbox(self_)?, TYPE_ID_GROUP) {
			1
		} else {
			0
		})
	})
}

/// `oakengine_node_is_multicam`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_is_multicam(self_: *const OakEngineNode) -> c_int {
	guard_int(|| unsafe {
		if self_.is_null() {
			return Ok(0);
		}
		Ok(if is_node_type(unbox(self_)?, TYPE_ID_MULTICAM) {
			1
		} else {
			0
		})
	})
}

// ---------------------------------------------------------------------------
// node.h — context positions
// ---------------------------------------------------------------------------

/// `oakengine_node_context_node_count`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_context_node_count(
	context: *const OakEngineNode,
) -> c_int {
	guard_int(|| unsafe {
		if context.is_null() {
			return Err(Error::Invalid);
		}
		let mut count: c_int = 0;
		Error::from_module(n::oaknode_node_context_count(unbox(context)?, &mut count))?;
		Ok(count)
	})
}

/// `oakengine_node_context_contains_node`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_context_contains_node(
	context: *const OakEngineNode,
	node: *const OakEngineNode,
) -> c_int {
	guard_int(|| unsafe {
		if context.is_null() || node.is_null() {
			return Err(Error::Invalid);
		}
		let ch = unbox(context)?;
		let nh = unbox(node)?;
		let mut count: c_int = 0;
		Error::from_module(n::oaknode_node_context_count(ch, &mut count))?;
		for i in 0..count {
			let mut other = CHandle::null();
			if n::oaknode_node_context_node_at(ch, i, &mut other) == 0
				&& !other.is_null()
				&& n::oaknode_node_identity(other) == n::oaknode_node_identity(nh)
			{
				return Ok(1);
			}
		}
		Ok(0)
	})
}

/// `oakengine_node_context_node_at`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_context_node_at(
	context: *mut OakEngineNode,
	index: c_int,
	x: *mut f64,
	y: *mut f64,
	expanded: *mut c_int,
) -> *mut OakEngineNode {
	guard_ptr(|| unsafe {
		if context.is_null() || index < 0 {
			return Ok(std::ptr::null_mut());
		}
		let ch = unbox(context)?;
		let mut out = CHandle::null();
		let rc = n::oaknode_node_context_node_at(ch, index, &mut out);
		if rc != 0 || out.is_null() {
			return Ok(std::ptr::null_mut());
		}
		// Positions are read from the context's position map; the module
		// has no per-node position fetch, so x/y/expanded stay untouched.
		let _ = (x, y, expanded);
		Ok(box_handle::<OakEngineNode>(out))
	})
}

/// `oakengine_node_set_context_position` — undoable position set.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_set_context_position(
	context: *mut OakEngineNode,
	node: *mut OakEngineNode,
	x: f64,
	y: f64,
) -> c_int {
	guard(|| unsafe {
		if context.is_null() || node.is_null() {
			set_node_error("invalid arguments");
			return Err(Error::Invalid);
		}
		let ch = unbox(context)?;
		let nh = unbox(node)?;
		let mut cmd: CHandle = CHandle::null();
		let rc = n::oaknode_node_set_context_position_undoable(nh, ch, x, y, 0, &mut cmd);
		if rc != 0 {
			return Err(Error::Module(rc));
		}
		push_command(cmd, "Set Position")
	})
}

/// `oakengine_node_get_context_position`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_get_context_position(
	context: *const OakEngineNode,
	node: *const OakEngineNode,
	x: *mut f64,
	y: *mut f64,
	expanded: *mut c_int,
) -> c_int {
	guard(|| unsafe {
		if context.is_null() || node.is_null() {
			set_node_error("invalid arguments");
			return Err(Error::Invalid);
		}
		let rc = n::oaknode_node_get_context_position(
			unbox(node)?,
			unbox(context)?,
			x,
			y,
			expanded,
		);
		Error::from_module(rc)
	})
}

/// `oakengine_node_set_context_expanded`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_set_context_expanded(
	context: *mut OakEngineNode,
	node: *mut OakEngineNode,
	expanded: c_int,
) -> c_int {
	guard(|| unsafe {
		if context.is_null() || node.is_null() {
			set_node_error("invalid arguments");
			return Err(Error::Invalid);
		}
		let ch = unbox(context)?;
		let nh = unbox(node)?;
		// Preserve the current position and flip only the expanded flag.
		let mut x: f64 = 0.0;
		let mut y: f64 = 0.0;
		let mut was: c_int = 0;
		let rc = n::oaknode_node_get_context_position(nh, ch, &mut x, &mut y, &mut was);
		if rc != 0 {
			return Err(Error::Module(rc));
		}
		let mut cmd: CHandle = CHandle::null();
		let rc = n::oaknode_node_set_context_position_undoable(
			nh,
			ch,
			x,
			y,
			if expanded != 0 { 1 } else { 0 },
			&mut cmd,
		);
		if rc != 0 {
			return Err(Error::Module(rc));
		}
		push_command(cmd, "Set Position")
	})
}

// ---------------------------------------------------------------------------
// node.h — effect input
// ---------------------------------------------------------------------------

/// `oakengine_node_get_effect_input`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_get_effect_input(
	self_: *const OakEngineNode,
	input_id: *mut c_char,
	input_id_size: c_int,
	element: *mut c_int,
) -> c_int {
	// Stub: the oaknode module has no effect-input export.
	guard_int(|| unsafe {
		if self_.is_null() {
			return Err(Error::Invalid);
		}
		let _ = unbox(self_)?;
		let _ = (input_id, input_id_size, element);
		Err(Error::NotFound)
	})
}

// ---------------------------------------------------------------------------
// node.h — group passthrough
// ---------------------------------------------------------------------------

/// `oakengine_node_group_create` — detached group node.
#[no_mangle]
pub extern "C" fn oakengine_node_group_create() -> *mut OakEngineNode {
	guard_ptr(|| {
		let g = unsafe { n::oaknode_group_create() };
		if g.ctx.is_null() {
			return Ok(std::ptr::null_mut());
		}
		Ok(box_handle::<OakEngineNode>(g))
	})
}

/// `oakengine_node_group_get_inner` — walk one group-passthrough level.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_group_get_inner(
	inout_node: *mut *mut OakEngineNode,
	inout_input: *mut c_char,
	inout_input_size: c_int,
	inout_element: *mut c_int,
) -> c_int {
	guard_int(|| unsafe {
		if inout_node.is_null() || (*inout_node).is_null() || inout_input.is_null()
			|| inout_input_size <= 0 || inout_element.is_null()
		{
			return Ok(0);
		}
		let node = unbox(*inout_node)?;
		if !is_node_type(node, TYPE_ID_GROUP) {
			return Ok(0);
		}
		let id = read_cstr(inout_input);
		let mut out_node = CHandle::null();
		let mut out_input = [0 as c_char; 256];
		let mut out_element: c_int = *inout_element;
		let rc = n::oaknode_group_resolve_input(
			node,
			std::ffi::CString::new(id.as_str()).unwrap().as_ptr(),
			out_element,
			&mut out_node,
			out_input.as_mut_ptr(),
			out_input.len() as c_int,
			&mut out_element,
		);
		if rc != 0 || out_node.is_null() {
			return Ok(0);
		}
		// One level only: if the resolved node is the same, nothing moved.
		if n::oaknode_node_identity(out_node) == n::oaknode_node_identity(node)
		{
			return Ok(0);
		}
		*inout_node = box_handle::<OakEngineNode>(out_node);
		let s = read_cstr(out_input.as_ptr());
		write_string(&s, inout_input, inout_input_size);
		*inout_element = out_element;
		Ok(1)
	})
}

/// `oakengine_group_input_passthrough_count`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_group_input_passthrough_count(
	self_: *const OakEngineNode,
) -> c_int {
	guard_int(|| unsafe {
		if self_.is_null() {
			return Err(Error::Invalid);
		}
		let h = unbox(self_)?;
		if !is_node_type(h, TYPE_ID_GROUP) {
			return Err(Error::Invalid);
		}
		let mut count: c_int = 0;
		Error::from_module(n::oaknode_group_passthrough_count(h, &mut count))?;
		Ok(count)
	})
}

/// `oakengine_group_add_input_passthrough` — direct add.
#[no_mangle]
pub unsafe extern "C" fn oakengine_group_add_input_passthrough(
	self_: *mut OakEngineNode,
	inner_node: *mut OakEngineNode,
	inner_input: *const c_char,
	inner_element: c_int,
	preferred_id: *const c_char,
	out_id: *mut c_char,
	out_id_size: c_int,
) -> c_int {
	guard_int(|| unsafe {
		if self_.is_null() || inner_node.is_null() || inner_input.is_null() {
			set_node_error("invalid arguments or not a group");
			return Err(Error::Invalid);
		}
		let gh = unbox(self_)?;
		let ih = unbox(inner_node)?;
		let rc = n::oaknode_group_add_input_passthrough(
			gh,
			ih,
			inner_input,
			inner_element,
			out_id,
			out_id_size,
		);
		if rc < 0 {
			return Err(Error::Module(rc));
		}
		let _ = preferred_id;
		Ok(string_result(rc))
	})
}

/// `oakengine_group_input_passthrough_at`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_group_input_passthrough_at(
	self_: *const OakEngineNode,
	index: c_int,
	id: *mut c_char,
	id_size: c_int,
	node: *mut *mut OakEngineNode,
	input_id: *mut c_char,
	input_id_size: c_int,
	element: *mut c_int,
) -> c_int {
	guard_int(|| unsafe {
		if self_.is_null() {
			set_node_error("not a group");
			return Err(Error::Invalid);
		}
		let gh = unbox(self_)?;
		if !is_node_type(gh, TYPE_ID_GROUP) {
			set_node_error("not a group");
			return Err(Error::Invalid);
		}
		let mut out_node = CHandle::null();
		let rc = n::oaknode_group_passthrough_input_at(
			gh,
			index,
			&mut out_node,
			input_id,
			input_id_size,
			element,
		);
		if rc < 0 {
			return Err(Error::Module(rc));
		}
		let mut id_buf = [0 as c_char; 256];
		let id_rc = n::oaknode_group_passthrough_id_at(gh, index, id_buf.as_mut_ptr(), 256);
		if id_rc < 0 {
			return Err(Error::Module(id_rc));
		}
		let id_len = write_string(&read_cstr(id_buf.as_ptr()), id, id_size);
		if !out_node.is_null() {
			if node.is_null() {
				// The caller owns nothing; the borrowed node must not leak.
				free_box(box_handle::<OakEngineNode>(out_node));
			} else {
				*node = box_handle::<OakEngineNode>(out_node);
			}
		}
		Ok(id_len)
	})
}

/// `oakengine_group_get_id_of_passthrough`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_group_get_id_of_passthrough(
	self_: *const OakEngineNode,
	inner_node: *mut OakEngineNode,
	inner_input: *const c_char,
	inner_element: c_int,
	id: *mut c_char,
	id_size: c_int,
) -> c_int {
	guard_int(|| unsafe {
		if self_.is_null() || inner_node.is_null() || inner_input.is_null() {
			set_node_error("invalid arguments or not a group");
			return Err(Error::Invalid);
		}
		let gh = unbox(self_)?;
		let ih = unbox(inner_node)?;
		let mut count: c_int = 0;
		Error::from_module(n::oaknode_group_passthrough_count(gh, &mut count))?;
		for i in 0..count {
			let mut out_node = CHandle::null();
			let mut out_input = [0 as c_char; 256];
			let mut out_element: c_int = 0;
			let rc = n::oaknode_group_passthrough_input_at(
				gh,
				i,
				&mut out_node,
				out_input.as_mut_ptr(),
				out_input.len() as c_int,
				&mut out_element,
			);
			if rc != 0 || out_node.is_null() {
				continue;
			}
			if n::oaknode_node_identity(out_node) == n::oaknode_node_identity(ih)
				&& read_cstr(out_input.as_ptr()) == read_cstr(inner_input)
				&& out_element == inner_element
			{
				let mut id_buf = [0 as c_char; 256];
				let id_rc = n::oaknode_group_passthrough_id_at(gh, i, id_buf.as_mut_ptr(), 256);
				if id_rc < 0 {
					return Err(Error::Module(id_rc));
				}
				return Ok(write_string(&read_cstr(id_buf.as_ptr()), id, id_size));
			}
		}
		set_node_error("no passthrough for that node/input");
		Err(Error::NotFound)
	})
}

/// `oakengine_group_get_passthrough_from_id`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_group_get_passthrough_from_id(
	self_: *const OakEngineNode,
	id: *const c_char,
	out_node: *mut *mut OakEngineNode,
	out_input: *mut c_char,
	out_input_size: c_int,
	out_element: *mut c_int,
) -> c_int {
	guard(|| unsafe {
		if self_.is_null() || id.is_null() {
			set_node_error("invalid arguments or not a group");
			return Err(Error::Invalid);
		}
		let gh = unbox(self_)?;
		let want = read_cstr(id);
		let mut count: c_int = 0;
		Error::from_module(n::oaknode_group_passthrough_count(gh, &mut count))?;
		for i in 0..count {
			let mut id_buf = [0 as c_char; 256];
			let id_rc = n::oaknode_group_passthrough_id_at(gh, i, id_buf.as_mut_ptr(), 256);
			if id_rc < 0 || read_cstr(id_buf.as_ptr()) != want {
				continue;
			}
			let mut node = CHandle::null();
			let rc = n::oaknode_group_passthrough_input_at(
				gh,
				i,
				&mut node,
				out_input,
				out_input_size,
				out_element,
			);
			if rc != 0 || node.is_null() {
				return Err(Error::Module(rc));
			}
			if !out_node.is_null() {
				*out_node = box_handle::<OakEngineNode>(node);
			}
			return Ok(());
		}
		set_node_error(&format!("no passthrough with id \"{}\"", want));
		Err(Error::NotFound)
	})
}

/// `oakengine_group_get_output_passthrough`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_group_get_output_passthrough(
	self_: *const OakEngineNode,
) -> *mut OakEngineNode {
	guard_ptr(|| unsafe {
		if self_.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let gh = unbox(self_)?;
		if !is_node_type(gh, TYPE_ID_GROUP) {
			return Ok(std::ptr::null_mut());
		}
		let mut out = CHandle::null();
		let rc = n::oaknode_group_get_output_passthrough(gh, &mut out);
		if rc != 0 || out.is_null() {
			return Ok(std::ptr::null_mut());
		}
		Ok(box_handle::<OakEngineNode>(out))
	})
}

/// `oakengine_group_set_output_passthrough` — direct set.
#[no_mangle]
pub unsafe extern "C" fn oakengine_group_set_output_passthrough(
	self_: *mut OakEngineNode,
	inner_node: *mut OakEngineNode,
) -> c_int {
	guard(|| unsafe {
		if self_.is_null() {
			set_node_error("not a group");
			return Err(Error::Invalid);
		}
		let gh = unbox(self_)?;
		let ih = unbox(inner_node)?;
		let rc = n::oaknode_group_set_output_passthrough(gh, ih);
		Error::from_module(rc)
	})
}

/// `oakengine_group_resolve_input`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_group_resolve_input(
	self_: *const OakEngineNode,
	id: *const c_char,
	element: c_int,
	out_node: *mut *mut OakEngineNode,
	out_input: *mut c_char,
	out_input_size: c_int,
	out_element: *mut c_int,
) -> c_int {
	guard(|| unsafe {
		if self_.is_null() || id.is_null() {
			set_node_error("invalid arguments");
			return Err(Error::Invalid);
		}
		let gh = unbox(self_)?;
		let mut node = CHandle::null();
		let rc = n::oaknode_group_resolve_input(
			gh,
			id,
			element,
			&mut node,
			out_input,
			out_input_size,
			out_element,
		);
		if rc != 0 {
			return Err(Error::Module(rc));
		}
		if !out_node.is_null() {
			if node.is_null() {
				// Pass through: the group itself is the resolved node.
				*out_node = box_handle::<OakEngineNode>(gh);
			} else {
				*out_node = box_handle::<OakEngineNode>(node);
			}
		}
		Ok(())
	})
}

/// `oakengine_group_remove_input_passthrough` — direct remove.
#[no_mangle]
pub unsafe extern "C" fn oakengine_group_remove_input_passthrough(
	self_: *mut OakEngineNode,
	inner_node: *mut OakEngineNode,
	inner_input: *const c_char,
	inner_element: c_int,
) -> c_int {
	guard(|| unsafe {
		if self_.is_null() || inner_node.is_null() || inner_input.is_null() {
			set_node_error("invalid arguments or not a group");
			return Err(Error::Invalid);
		}
		let rc = n::oaknode_group_remove_input_passthrough(
			unbox(self_)?,
			unbox(inner_node)?,
			inner_input,
			inner_element,
		);
		Error::from_module(rc)
	})
}

/// `oakengine_group_add_input_passthrough_command` — opaque command.
#[no_mangle]
pub unsafe extern "C" fn oakengine_group_add_input_passthrough_command(
	self_: *mut OakEngineNode,
	inner_node: *mut OakEngineNode,
	inner_input: *const c_char,
	inner_element: c_int,
	preferred_id: *const c_char,
) -> *mut c_void {
	guard_ptr(|| unsafe {
		if self_.is_null() || inner_node.is_null() || inner_input.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let mut cmd: CHandle = CHandle::null();
		let rc = n::oaknode_group_add_input_passthrough_undoable(
			unbox(self_)?,
			unbox(inner_node)?,
			inner_input,
			inner_element,
			&mut cmd,
		);
		if rc != 0 {
			return Ok(std::ptr::null_mut());
		}
		let _ = preferred_id;
		Ok(command_box(cmd)?.cast())
	})
}

/// `oakengine_group_set_output_passthrough_command` — opaque command.
#[no_mangle]
pub unsafe extern "C" fn oakengine_group_set_output_passthrough_command(
	self_: *mut OakEngineNode,
	inner_node: *mut OakEngineNode,
) -> *mut c_void {
	guard_ptr(|| unsafe {
		if self_.is_null() || inner_node.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let mut cmd: CHandle = CHandle::null();
		let rc = n::oaknode_group_set_output_passthrough_undoable(
			unbox(self_)?,
			unbox(inner_node)?,
			&mut cmd,
		);
		if rc != 0 {
			return Ok(std::ptr::null_mut());
		}
		Ok(command_box(cmd)?.cast())
	})
}

/// `oakengine_group_add_input_passthrough_undoable`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_group_add_input_passthrough_undoable(
	self_: *mut OakEngineNode,
	inner_node: *mut OakEngineNode,
	inner_input: *const c_char,
	inner_element: c_int,
	preferred_id: *const c_char,
) -> c_int {
	guard(|| unsafe {
		if self_.is_null() || inner_node.is_null() || inner_input.is_null() {
			set_node_error("invalid arguments or not a group");
			return Err(Error::Invalid);
		}
		let mut cmd: CHandle = CHandle::null();
		let rc = n::oaknode_group_add_input_passthrough_undoable(
			unbox(self_)?,
			unbox(inner_node)?,
			inner_input,
			inner_element,
			&mut cmd,
		);
		if rc != 0 {
			return Err(Error::Module(rc));
		}
		let _ = preferred_id;
		push_command(cmd, "Add Input Passthrough")
	})
}

/// `oakengine_group_set_output_passthrough_undoable`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_group_set_output_passthrough_undoable(
	self_: *mut OakEngineNode,
	inner_node: *mut OakEngineNode,
) -> c_int {
	guard(|| unsafe {
		if self_.is_null() {
			set_node_error("not a group");
			return Err(Error::Invalid);
		}
		let mut cmd: CHandle = CHandle::null();
		let rc = n::oaknode_group_set_output_passthrough_undoable(
			unbox(self_)?,
			unbox(inner_node)?,
			&mut cmd,
		);
		if rc != 0 {
			return Err(Error::Module(rc));
		}
		push_command(cmd, "Set Output Passthrough")
	})
}

// ---------------------------------------------------------------------------
// node.h — multi-camera
// ---------------------------------------------------------------------------

/// `oakengine_multicam_input_current`.
#[no_mangle]
pub extern "C" fn oakengine_multicam_input_current() -> *const c_char {
	unsafe { n::oaknode_multicam_input_current() }
}

/// `oakengine_multicam_input_sources`.
#[no_mangle]
pub extern "C" fn oakengine_multicam_input_sources() -> *const c_char {
	unsafe { n::oaknode_multicam_input_sources() }
}

/// `oakengine_multicam_input_sequence`.
#[no_mangle]
pub extern "C" fn oakengine_multicam_input_sequence() -> *const c_char {
	unsafe { n::oaknode_multicam_input_sequence() }
}

/// `oakengine_multicam_input_sequence_type`.
#[no_mangle]
pub extern "C" fn oakengine_multicam_input_sequence_type() -> *const c_char {
	unsafe { n::oaknode_multicam_input_sequence_type() }
}

/// `oakengine_multicam_get_source_count`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_multicam_get_source_count(
	self_: *const OakEngineNode,
) -> c_int {
	guard_int(|| unsafe {
		if self_.is_null() {
			return Err(Error::Invalid);
		}
		let h = unbox(self_)?;
		if !is_node_type(h, TYPE_ID_MULTICAM) {
			return Err(Error::Invalid);
		}
		let mut count: c_int = 0;
		Error::from_module(n::oaknode_multicam_get_source_count(h, &mut count))?;
		Ok(count)
	})
}

/// `oakengine_multicam_get_rows_and_columns`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_multicam_get_rows_and_columns(
	source_count: c_int,
	rows: *mut c_int,
	cols: *mut c_int,
) -> c_int {
	guard(|| unsafe {
		if source_count < 0 || rows.is_null() || cols.is_null() {
			return Err(Error::Invalid);
		}
		Error::from_module(n::oaknode_multicam_get_rows_and_columns(source_count, rows, cols))
	})
}

/// `oakengine_multicam_index_to_row_cols`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_multicam_index_to_row_cols(
	index: c_int,
	rows: c_int,
	cols: c_int,
	out_row: *mut c_int,
	out_col: *mut c_int,
) -> c_int {
	guard(|| unsafe {
		if index < 0 || rows < 1 || cols < 1 || out_row.is_null() || out_col.is_null() {
			return Err(Error::Invalid);
		}
		Error::from_module(n::oaknode_multicam_index_to_row_cols(
			index,
			rows,
			cols,
			out_row,
			out_col,
		))
	})
}

/// `oakengine_multicam_rows_cols_to_index`.
#[no_mangle]
pub extern "C" fn oakengine_multicam_rows_cols_to_index(
	row: c_int,
	col: c_int,
	rows: c_int,
	cols: c_int,
) -> c_int {
	crate::handle::guard_int(|| {
		if row < 0 || col < 0 || rows < 1 || cols < 1 || row >= rows || col >= cols {
			return Err(Error::Invalid);
		}
		Ok(unsafe { n::oaknode_multicam_rows_cols_to_index(row, col, rows, cols) })
	})
}

/// `oakengine_multicam_get_current_source`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_multicam_get_current_source(
	self_: *const OakEngineNode,
) -> c_int {
	guard_int(|| unsafe {
		if self_.is_null() {
			return Err(Error::Invalid);
		}
		let h = unbox(self_)?;
		if !is_node_type(h, TYPE_ID_MULTICAM) {
			return Err(Error::Invalid);
		}
		let mut source: c_int = 0;
		Error::from_module(n::oaknode_multicam_get_current_source(h, &mut source))?;
		Ok(source)
	})
}

// ---------------------------------------------------------------------------
// node.h — shape node
// ---------------------------------------------------------------------------

/// `oakengine_shape_set_rect_undoable`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_shape_set_rect_undoable(
	node: *mut OakEngineNode,
	_x: f64,
	_y: f64,
	_w: f64,
	_h: f64,
	_video_params: *const OakVideoParamsPod,
	_command: *mut c_void,
) -> c_int {
	// Stub: the oaknode module has no ShapeNodeBase::set_rect surface.
	guard(|| unsafe {
		if node.is_null() || _video_params.is_null() || _command.is_null() {
			return Err(Error::Invalid);
		}
		let _ = unbox(node)?;
		Err(Error::Invalid)
	})
}

// ---------------------------------------------------------------------------
// node.h — subtitle block
// ---------------------------------------------------------------------------

/// `oakengine_subtitle_text_input_id`.
#[no_mangle]
pub extern "C" fn oakengine_subtitle_text_input_id() -> *const c_char {
	// SubtitleBlock::k_text_in (engine/node/block/subtitle/subtitle.cpp:29).
	static S: &[u8] = b"text_in\0";
	S.as_ptr() as *const c_char
}

/// `oakengine_subtitle_get_text`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_subtitle_get_text(
	node: *mut OakEngineNode,
	_buf: *mut c_char,
	_buf_size: c_int,
) -> c_int {
	// Stub: the oaknode module has no subtitle block type.
	guard_int(|| unsafe {
		if node.is_null() {
			return Err(Error::Invalid);
		}
		let _ = unbox(node)?;
		Err(Error::Invalid)
	})
}

/// `oakengine_subtitle_set_text`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_subtitle_set_text(
	node: *mut OakEngineNode,
	_text: *const c_char,
) -> c_int {
	// Stub: see `oakengine_subtitle_get_text`.
	guard(|| unsafe {
		if node.is_null() {
			return Err(Error::Invalid);
		}
		let _ = unbox(node)?;
		Err(Error::Invalid)
	})
}

// ---------------------------------------------------------------------------
// node.h — bulk graph deletion
// ---------------------------------------------------------------------------

/// `oakengine_nodes_delete_many` — delete nodes and edges in one command.
#[no_mangle]
pub unsafe extern "C" fn oakengine_nodes_delete_many(
	nodes: *mut *mut OakEngineNode,
	contexts: *mut *mut OakEngineNode,
	node_count: c_int,
	edge_outputs: *mut *mut OakEngineNode,
	edge_input_nodes: *mut *mut OakEngineNode,
	edge_input_ids: *mut *const c_char,
	edge_input_elements: *const c_int,
	edge_count: c_int,
) -> c_int {
	guard(|| unsafe {
		let rc = oakengine_nodes_delete_many_ex(
			nodes,
			contexts,
			node_count,
			edge_outputs,
			edge_input_nodes,
			edge_input_ids,
			edge_input_elements,
			edge_count,
			std::ptr::null_mut(),
			std::ptr::null_mut(),
			std::ptr::null_mut(),
			std::ptr::null(),
			0,
		);
		Error::from_module(rc)
	})
}

/// `oakengine_nodes_delete_many_ex` — delete plus reconnects in one
/// command.
#[no_mangle]
pub unsafe extern "C" fn oakengine_nodes_delete_many_ex(
	nodes: *mut *mut OakEngineNode,
	contexts: *mut *mut OakEngineNode,
	node_count: c_int,
	edge_outputs: *mut *mut OakEngineNode,
	edge_input_nodes: *mut *mut OakEngineNode,
	edge_input_ids: *mut *const c_char,
	edge_input_elements: *const c_int,
	edge_count: c_int,
	reconnect_outputs: *mut *mut OakEngineNode,
	reconnect_input_nodes: *mut *mut OakEngineNode,
	reconnect_input_ids: *mut *const c_char,
	reconnect_input_elements: *const c_int,
	reconnect_count: c_int,
) -> c_int {
	guard(|| unsafe {
		if node_count <= 0 && edge_count <= 0 {
			set_node_error("nothing to delete");
			return Err(Error::Invalid);
		}
		if node_count > 0 && nodes.is_null() {
			set_node_error("invalid arguments");
			return Err(Error::Invalid);
		}
		let mut children: Vec<CHandle> = Vec::new();
		// Node removals (disconnecting their edges), with context removal.
		for i in 0..node_count as usize {
			let node = *nodes.add(i);
			if node.is_null() {
				set_node_error(&format!("null node at index {}", i));
				return Err(Error::Invalid);
			}
			let nh = unbox(node)?;
			if !contexts.is_null() {
				let ctx = *contexts.add(i);
				if !ctx.is_null() {
					let ch = unbox(ctx)?;
					let rc = n::oaknode_node_remove_from_context(nh, ch);
					if rc != 0 {
						return Err(Error::Module(rc));
					}
				}
			}
			let cmd = n::oaknode_command_create_remove_node(nh);
			if cmd.ctx.is_null() {
				return Err(Error::Failed("remove node command failed".into()));
			}
			children.push(cmd);
		}
		// Edge removals.
		for i in 0..edge_count as usize {
			if edge_outputs.is_null() || edge_input_nodes.is_null() || edge_input_ids.is_null() {
				set_node_error(&format!("invalid edge at index {}", i));
				return Err(Error::Invalid);
			}
			let input = *edge_input_nodes.add(i);
			let input_id = *edge_input_ids.add(i);
			if input.is_null() || input_id.is_null() {
				set_node_error(&format!("invalid edge at index {}", i));
				return Err(Error::Invalid);
			}
			let _ = *edge_outputs.add(i);
			let _ = if edge_input_elements.is_null() { -1 } else { *edge_input_elements.add(i) };
			let mut cmd: CHandle = CHandle::null();
			let rc = n::oaknode_node_disconnect_undoable(unbox(input)?, input_id, &mut cmd);
			if rc != 0 {
				return Err(Error::Module(rc));
			}
			children.push(cmd);
		}
		// Reconnect edges run AFTER the deletion inside the same command.
		for i in 0..reconnect_count as usize {
			if reconnect_outputs.is_null() || reconnect_input_nodes.is_null()
				|| reconnect_input_ids.is_null()
			{
				set_node_error(&format!("invalid reconnect edge at index {}", i));
				return Err(Error::Invalid);
			}
			let out = *reconnect_outputs.add(i);
			let input = *reconnect_input_nodes.add(i);
			let input_id = *reconnect_input_ids.add(i);
			if out.is_null() || input.is_null() || input_id.is_null() {
				set_node_error(&format!("invalid reconnect edge at index {}", i));
				return Err(Error::Invalid);
			}
			let _ = if reconnect_input_elements.is_null() {
				-1
			} else {
				*reconnect_input_elements.add(i)
			};
			let mut cmd: CHandle = CHandle::null();
			let rc = n::oaknode_node_connect_undoable(unbox(out)?, unbox(input)?, input_id, &mut cmd);
			if rc != 0 {
				return Err(Error::Module(rc));
			}
			children.push(cmd);
		}
		push_multi_commands(&children, std::ptr::null_mut(), "Delete Nodes")
	})
}

// ---------------------------------------------------------------------------
// node.h — keyframe best type at time
// ---------------------------------------------------------------------------

/// `oakengine_node_keyframe_best_type_at_time`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_keyframe_best_type_at_time(
	self_: *const OakEngineNode,
	input_id: *const c_char,
	element: c_int,
	time_ts: i64,
	track: c_int,
	default_type: c_int,
) -> c_int {
	// Stub: the module has no keyframe type inspection; the caller's
	// default is returned unchanged.
	guard_int(|| unsafe {
		if self_.is_null() || input_id.is_null() {
			return Ok(default_type);
		}
		let _ = unbox(self_)?;
		let _ = (element, time_ts, track);
		Ok(default_type)
	})
}

// ---------------------------------------------------------------------------
// node.h — handle-based keyframe API
// ---------------------------------------------------------------------------

/// `oakengine_node_keyframe_track_count` — keyframe tracks of the input.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_keyframe_track_count(
	self_: *const OakEngineNode,
	input_id: *const c_char,
	element: c_int,
) -> c_int {
	// Derived from the input's value type (C++ NodeValue::
	// get_number_of_keyframe_tracks): 1 scalar, 2/3/4 VEC2/3/4,
	// 4 COLOR, 6 BEZIER.
	guard_int(|| unsafe {
		if self_.is_null() || input_id.is_null() {
			return Ok(0);
		}
		let h = unbox(self_)?;
		let mut ty: c_int = 0;
		Error::from_module(n::oaknode_node_input_get_type(h, input_id, &mut ty))?;
		let _ = element;
		Ok(match ty {
			value_type::VEC2 => 2,
			value_type::VEC3 => 3,
			value_type::VEC4 | value_type::COLOR => 4,
			value_type::BEZIER => 6,
			_ => 1,
		})
	})
}

/// `oakengine_node_keyframe_count_on_track`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_keyframe_count_on_track(
	self_: *const OakEngineNode,
	input_id: *const c_char,
	element: c_int,
	track: c_int,
) -> c_int {
	// Stub: see `oakengine_node_keyframe_count`.
	guard_int(|| unsafe {
		if self_.is_null() || input_id.is_null() {
			return Ok(0);
		}
		let _ = unbox(self_)?;
		let _ = (element, track);
		Ok(0)
	})
}

/// `oakengine_node_keyframes_toggle_at_time`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_keyframes_toggle_at_time(
	self_: *mut OakEngineNode,
	input_id: *const c_char,
	element: c_int,
	time_ts: i64,
	track: c_int,
	on: c_int,
	undo_name: *const c_char,
) -> c_int {
	// Stub: see `oakengine_node_keyframe_count`.
	guard(|| unsafe {
		if self_.is_null() || input_id.is_null() {
			return Err(Error::Invalid);
		}
		let _ = unbox(self_)?;
		let _ = (element, time_ts, track, on, undo_name);
		Err(Error::NotFound)
	})
}

/// `oakengine_node_has_keyframe_at_time`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_has_keyframe_at_time(
	self_: *const OakEngineNode,
	input_id: *const c_char,
	element: c_int,
	time_ts: i64,
	track: c_int,
) -> c_int {
	// Stub: see `oakengine_node_keyframe_count`.
	guard_int(|| unsafe {
		if self_.is_null() || input_id.is_null() {
			return Ok(0);
		}
		let _ = unbox(self_)?;
		let _ = (element, time_ts, track);
		Ok(0)
	})
}

/// `oakengine_node_keyframe_earliest_time`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_keyframe_earliest_time(
	self_: *const OakEngineNode,
	input_id: *const c_char,
	element: c_int,
	num: *mut i64,
	den: *mut i64,
) -> c_int {
	// Stub: see `oakengine_node_keyframe_count`.
	guard_int(|| unsafe {
		if self_.is_null() || input_id.is_null() {
			set_node_error("invalid arguments");
			return Ok(0);
		}
		let _ = unbox(self_)?;
		let _ = element;
		if !num.is_null() {
			*num = 0;
		}
		if !den.is_null() {
			*den = 1;
		}
		Ok(0)
	})
}

/// `oakengine_node_keyframe_latest_time`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_keyframe_latest_time(
	self_: *const OakEngineNode,
	input_id: *const c_char,
	element: c_int,
	num: *mut i64,
	den: *mut i64,
) -> c_int {
	// Stub: see `oakengine_node_keyframe_count`.
	guard_int(|| unsafe {
		if self_.is_null() || input_id.is_null() {
			set_node_error("invalid arguments");
			return Ok(0);
		}
		let _ = unbox(self_)?;
		let _ = element;
		if !num.is_null() {
			*num = 0;
		}
		if !den.is_null() {
			*den = 1;
		}
		Ok(0)
	})
}

/// `oakengine_node_keyframe_closest_time_before`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_keyframe_closest_time_before(
	self_: *const OakEngineNode,
	input_id: *const c_char,
	element: c_int,
	time_ts: i64,
	track: c_int,
	num: *mut i64,
	den: *mut i64,
) -> c_int {
	// Stub: see `oakengine_node_keyframe_count`.
	guard_int(|| unsafe {
		if self_.is_null() || input_id.is_null() {
			set_node_error("invalid arguments");
			return Ok(0);
		}
		let _ = unbox(self_)?;
		let _ = (element, time_ts, track);
		if !num.is_null() {
			*num = 0;
		}
		if !den.is_null() {
			*den = 1;
		}
		Ok(0)
	})
}

/// `oakengine_node_keyframe_closest_time_after`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_keyframe_closest_time_after(
	self_: *const OakEngineNode,
	input_id: *const c_char,
	element: c_int,
	time_ts: i64,
	track: c_int,
	num: *mut i64,
	den: *mut i64,
) -> c_int {
	// Stub: see `oakengine_node_keyframe_count`.
	guard_int(|| unsafe {
		if self_.is_null() || input_id.is_null() {
			set_node_error("invalid arguments");
			return Ok(0);
		}
		let _ = unbox(self_)?;
		let _ = (element, time_ts, track);
		if !num.is_null() {
			*num = 0;
		}
		if !den.is_null() {
			*den = 1;
		}
		Ok(0)
	})
}

/// `oakengine_node_keyframe_handle_on_track`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_keyframe_handle_on_track(
	self_: *const OakEngineNode,
	input_id: *const c_char,
	element: c_int,
	track: c_int,
	index: c_int,
) -> *mut OakEngineKeyframe {
	// Stub: see `oakengine_node_keyframe_count`.
	guard_ptr(|| {
		let _ = (self_, input_id, element, track, index);
		Ok(std::ptr::null_mut())
	})
}

/// `oakengine_node_keyframe_handle_at_time`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_keyframe_handle_at_time(
	self_: *const OakEngineNode,
	input_id: *const c_char,
	element: c_int,
	track: c_int,
	time_num: i64,
	time_den: i64,
) -> *mut OakEngineKeyframe {
	// Stub: see `oakengine_node_keyframe_count`.
	guard_ptr(|| {
		let _ = (self_, input_id, element, track, time_num, time_den);
		Ok(std::ptr::null_mut())
	})
}

/// `oakengine_node_keyframes_at_time`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_keyframes_at_time(
	self_: *const OakEngineNode,
	input_id: *const c_char,
	element: c_int,
	time_num: i64,
	time_den: i64,
	out_handles: *mut *mut OakEngineKeyframe,
	max_handles: c_int,
) -> c_int {
	// Stub: see `oakengine_node_keyframe_count`.
	guard_int(|| {
		let _ = (self_, input_id, element, time_num, time_den, out_handles, max_handles);
		Ok(0)
	})
}

/// `oakengine_node_set_input_keyframing`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_set_input_keyframing(
	self_: *mut OakEngineNode,
	input_id: *const c_char,
	element: c_int,
	keyframing: c_int,
	track: c_int,
	enable_all_tracks: c_int,
	undo_name: *const c_char,
) -> c_int {
	// Stub: the module has no keyframing-enable command; its keyframe
	// tracks are never populated through the C ABI.
	guard(|| unsafe {
		if self_.is_null() || input_id.is_null() {
			return Err(Error::Invalid);
		}
		let _ = unbox(self_)?;
		let _ = (element, keyframing, track, enable_all_tracks, undo_name);
		Err(Error::Invalid)
	})
}

/// `oakengine_node_set_input_keyframing_command`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_set_input_keyframing_command(
	self_: *mut OakEngineNode,
	input_id: *const c_char,
	element: c_int,
	keyframing: c_int,
) -> *mut c_void {
	// Stub: see `oakengine_node_set_input_keyframing`.
	guard_ptr(|| {
		let _ = (self_, input_id, element, keyframing);
		Ok(std::ptr::null_mut())
	})
}

/// `oakengine_node_keyframes_paste`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_keyframes_paste(
	self_: *mut OakEngineNode,
	keyframes: *mut *mut OakEngineKeyframe,
	count: c_int,
	undo_name: *const c_char,
) -> c_int {
	// Stub: see `oakengine_node_set_input_keyframing` (no insert path).
	guard(|| unsafe {
		if self_.is_null() || keyframes.is_null() || count <= 0 {
			return Err(Error::Invalid);
		}
		let _ = unbox(self_)?;
		let _ = undo_name;
		Err(Error::Invalid)
	})
}

// ---------------------------------------------------------------------------
// node.h — OakEngineKeyframe accessors
// ---------------------------------------------------------------------------

/// `oakengine_keyframe_get_time`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_keyframe_get_time(
	self_: *const OakEngineKeyframe,
	num: *mut i64,
	den: *mut i64,
) -> c_int {
	guard(|| unsafe {
		let h = unbox(self_)?;
		Error::from_module(n::oaknode_keyframe_get_time(h, num, den))
	})
}

/// `oakengine_keyframe_get_input_id`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_keyframe_get_input_id(
	self_: *const OakEngineKeyframe,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	guard_int(|| unsafe {
		let h = unbox(self_)?;
		let rc = n::oaknode_keyframe_get_input(h, buf, buf_size);
		if rc < 0 {
			Err(Error::Module(rc))
		} else {
			Ok(string_result(rc))
		}
	})
}

/// `oakengine_keyframe_get_track`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_keyframe_get_track(
	self_: *const OakEngineKeyframe,
) -> c_int {
	guard_int(|| unsafe {
		if self_.is_null() {
			return Ok(-1);
		}
		let mut track: c_int = -1;
		Error::from_module(n::oaknode_keyframe_get_track(unbox(self_)?, &mut track))?;
		Ok(track)
	})
}

/// `oakengine_keyframe_get_element`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_keyframe_get_element(
	self_: *const OakEngineKeyframe,
) -> c_int {
	guard_int(|| unsafe {
		if self_.is_null() {
			return Ok(-1);
		}
		let mut element: c_int = -1;
		Error::from_module(n::oaknode_keyframe_get_element(unbox(self_)?, &mut element))?;
		Ok(element)
	})
}

/// `oakengine_keyframe_get_node`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_keyframe_get_node(
	self_: *const OakEngineKeyframe,
) -> *mut OakEngineNode {
	guard_ptr(|| unsafe {
		if self_.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let mut out = CHandle::null();
		let rc = n::oaknode_keyframe_get_parent(unbox(self_)?, &mut out);
		if rc != 0 || out.is_null() {
			return Ok(std::ptr::null_mut());
		}
		Ok(box_handle::<OakEngineNode>(out))
	})
}

/// `oakengine_keyframe_get_type` — facade easing type.
#[no_mangle]
pub unsafe extern "C" fn oakengine_keyframe_get_type(
	self_: *const OakEngineKeyframe,
) -> c_int {
	guard_int(|| unsafe {
		if self_.is_null() {
			return Ok(-1);
		}
		let mut ty: c_int = 0;
		Error::from_module(n::oaknode_keyframe_get_type(unbox(self_)?, &mut ty))?;
		Ok(ty)
	})
}

/// `oakengine_keyframe_default_type`.
#[no_mangle]
pub extern "C" fn oakengine_keyframe_default_type() -> c_int {
	// NodeKeyframe::k_default_type is linear → facade type 0.
	0
}

/// `oakengine_keyframe_opposing_bezier_type`.
#[no_mangle]
pub extern "C" fn oakengine_keyframe_opposing_bezier_type(type_: c_int) -> c_int {
	unsafe { n::oaknode_keyframe_opposing_bezier_type(type_) }
}

/// `oakengine_keyframe_get_value`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_keyframe_get_value(
	self_: *const OakEngineKeyframe,
	out: *mut OakNodeValue,
) -> c_int {
	guard(|| unsafe {
		if self_.is_null() || out.is_null() {
			set_node_error("invalid arguments");
			return Err(Error::Invalid);
		}
		Error::from_module(n::oaknode_keyframe_get_value(unbox(self_)?, out))
	})
}

/// `oakengine_keyframe_compute_paste_value`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_keyframe_compute_paste_value(
	target_node: *mut OakEngineNode,
	keyframe: *mut OakEngineKeyframe,
	out: *mut OakNodeValue,
) -> c_int {
	guard(|| unsafe {
		if target_node.is_null() || keyframe.is_null() || out.is_null() {
			set_node_error("invalid arguments");
			return Err(Error::Invalid);
		}
		Error::from_module(n::oaknode_keyframe_compute_paste_value(
			unbox(target_node)?,
			unbox(keyframe)?,
			out,
		))
	})
}

/// `oakengine_keyframe_has_sibling_at_time`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_keyframe_has_sibling_at_time(
	self_: *const OakEngineKeyframe,
	time_ts: i64,
	track: c_int,
) -> c_int {
	guard_int(|| unsafe {
		if self_.is_null() {
			return Ok(0);
		}
		let mut value: c_int = 0;
		// The capi passes the timestamp as seconds with denominator 1.
		let rc = n::oaknode_keyframe_has_sibling_at_time(
			unbox(self_)?,
			time_ts,
			1,
			&mut value,
		);
		if rc != 0 {
			return Err(Error::Module(rc));
		}
		let _ = track;
		Ok(value)
	})
}

/// `oakengine_keyframe_set_bezier_point_live`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_keyframe_set_bezier_point_live(
	self_: *mut OakEngineKeyframe,
	point_index: c_int,
	x: f64,
	y: f64,
) -> c_int {
	guard(|| unsafe {
		if self_.is_null() || point_index < 0 || point_index > 1 {
			set_node_error("invalid arguments");
			return Err(Error::Invalid);
		}
		let rc = n::oaknode_keyframe_set_bezier_control(unbox(self_)?, point_index, x, y);
		Error::from_module(rc)
	})
}

/// `oakengine_keyframe_get_bezier_point`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_keyframe_get_bezier_point(
	self_: *const OakEngineKeyframe,
	point_index: c_int,
	x: *mut f64,
	y: *mut f64,
) -> c_int {
	guard(|| unsafe {
		if self_.is_null() || x.is_null() || y.is_null() || point_index < 0 || point_index > 1 {
			set_node_error("invalid arguments");
			return Err(Error::Invalid);
		}
		Error::from_module(n::oaknode_keyframe_get_bezier_control(
			unbox(self_)?,
			point_index,
			x,
			y,
		))
	})
}

/// `oakengine_keyframe_get_valid_bezier_point`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_keyframe_get_valid_bezier_point(
	self_: *const OakEngineKeyframe,
	point_index: c_int,
	x: *mut f64,
	y: *mut f64,
) -> c_int {
	guard(|| unsafe {
		if self_.is_null() || x.is_null() || y.is_null() || point_index < 0 || point_index > 1 {
			set_node_error("invalid arguments");
			return Err(Error::Invalid);
		}
		Error::from_module(n::oaknode_keyframe_get_valid_bezier_control(
			unbox(self_)?,
			point_index,
			x,
			y,
		))
	})
}

/// `oakengine_keyframe_set_value_live`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_keyframe_set_value_live(
	self_: *mut OakEngineKeyframe,
	value: *const OakNodeValue,
) -> c_int {
	guard(|| unsafe {
		if self_.is_null() || value.is_null() {
			set_node_error("invalid arguments");
			return Err(Error::Invalid);
		}
		let rc = n::oaknode_keyframe_set_value(unbox(self_)?, value);
		Error::from_module(rc)
	})
}

/// `oakengine_keyframe_set_time_live`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_keyframe_set_time_live(
	self_: *mut OakEngineKeyframe,
	num: i64,
	den: i64,
) -> c_int {
	guard(|| unsafe {
		if self_.is_null() {
			set_node_error("invalid arguments");
			return Err(Error::Invalid);
		}
		let rc = n::oaknode_keyframe_set_time(unbox(self_)?, num, den);
		Error::from_module(rc)
	})
}

/// `oakengine_keyframes_remove_many`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_keyframes_remove_many(
	keyframes: *mut *mut OakEngineKeyframe,
	count: c_int,
	undo_name: *const c_char,
) -> c_int {
	// Stub: see `oakengine_node_keyframe_count` (no remove-key path).
	guard(|| {
		if keyframes.is_null() || count <= 0 {
			return Err(Error::Invalid);
		}
		let _ = undo_name;
		Err(Error::Invalid)
	})
}

/// `oakengine_keyframe_create` — detached keyframe handle.
#[no_mangle]
pub unsafe extern "C" fn oakengine_keyframe_create(
	node: *mut OakEngineNode,
	input_id: *const c_char,
	element: c_int,
	track: c_int,
	time_ts: i64,
	type_: c_int,
	value: *const OakNodeValue,
	duration_ts: i64,
) -> *mut OakEngineKeyframe {
	guard_ptr(|| unsafe {
		if node.is_null() || input_id.is_null() || value.is_null() {
			set_node_error("invalid arguments");
			return Ok(std::ptr::null_mut());
		}
		let h = unbox(node)?;
		let tb = time_base_for(h);
		let (t_num, t_den) = ts_to_time(time_ts, tb);
		let kf = n::oaknode_keyframe_create(
			t_num,
			t_den,
			value,
			type_,
			track,
			element,
			input_id,
			h,
		);
		if kf.ctx.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let _ = duration_ts;
		Ok(box_handle::<OakEngineKeyframe>(kf))
	})
}

/// `oakengine_keyframe_dispose` — destroy a detached keyframe.
#[no_mangle]
pub unsafe extern "C" fn oakengine_keyframe_dispose(keyframe: *mut OakEngineKeyframe) {
	guard_void(|| unsafe {
		if keyframe.is_null() {
			return;
		}
		let mut h = (*keyframe).handle;
		n::oaknode_keyframe_free(&mut h);
		drop(Box::from_raw(keyframe));
	})
}

// ---------------------------------------------------------------------------
// node.h — input dragger
// ---------------------------------------------------------------------------

/// `oakengine_dragger_create`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_dragger_create(
	node: *mut OakEngineNode,
	input_id: *const c_char,
	element: c_int,
	track: c_int,
) -> *mut OakEngineNodeDragger {
	guard_ptr(|| unsafe {
		if node.is_null() || input_id.is_null() {
			set_node_error("invalid arguments");
			return Ok(std::ptr::null_mut());
		}
		let d = n::oaknode_dragger_create(unbox(node)?, input_id, element, track);
		if d.ctx.is_null() {
			return Ok(std::ptr::null_mut());
		}
		Ok(box_handle::<OakEngineNodeDragger>(d))
	})
}

/// `oakengine_dragger_start` — start the drag at a frame timestamp.
#[no_mangle]
pub unsafe extern "C" fn oakengine_dragger_start(
	self_: *mut OakEngineNodeDragger,
	time_ts: i64,
	track: c_int,
	insert_on_all_tracks: c_int,
) -> c_int {
	guard(|| unsafe {
		if self_.is_null() {
			set_node_error("invalid dragger");
			return Err(Error::Invalid);
		}
		let h = unbox(self_)?;
		// The engine's dragger contract is 1-based tracks; the module is
		// 0-based. The dragger box does not carry its node, so the
		// engine-default time base (1001/30000) applies to the frame
		// timestamp conversion (documented deviation from the capi, which
		// uses the project's first-sequence time base).
		let tb = (1001, 30000);
		let (t_num, t_den) = ts_to_time(time_ts, tb);
		let rc = n::oaknode_dragger_start(h, t_num, t_den, track - 1, insert_on_all_tracks);
		Error::from_module(rc)
	})
}

/// `oakengine_dragger_drag` — live drag.
#[no_mangle]
pub unsafe extern "C" fn oakengine_dragger_drag(
	self_: *mut OakEngineNodeDragger,
	value: *const OakNodeValue,
) -> c_int {
	guard(|| unsafe {
		if self_.is_null() || value.is_null() {
			set_node_error("invalid arguments");
			return Err(Error::Invalid);
		}
		Error::from_module(n::oaknode_dragger_drag(unbox(self_)?, value))
	})
}

/// `oakengine_dragger_end` — end the drag, pushing ONE undoable command.
#[no_mangle]
pub unsafe extern "C" fn oakengine_dragger_end(
	self_: *mut OakEngineNodeDragger,
	undo_name: *const c_char,
) -> c_int {
	guard(|| unsafe {
		if self_.is_null() {
			set_node_error("invalid dragger");
			return Err(Error::Invalid);
		}
		let h = unbox(self_)?;
		let mut cmd: CHandle = CHandle::null();
		let rc = n::oaknode_dragger_end(h, &mut cmd);
		if rc != 0 {
			return Err(Error::Module(rc));
		}
		let name = if undo_name.is_null() {
			"Drag Input".to_string()
		} else {
			let s = read_cstr(undo_name);
			if s.is_empty() { "Drag Input".to_string() } else { s }
		};
		let name_c = std::ffi::CString::new(name.as_str())
			.map_err(|_| Error::Failed("invalid undo name".into()))?;
		push_or_run(command_box(cmd)?, name_c.as_ptr())
	})
}

/// `oakengine_dragger_is_started`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_dragger_is_started(
	self_: *const OakEngineNodeDragger,
) -> c_int {
	guard_int(|| unsafe {
		if self_.is_null() {
			return Ok(0);
		}
		let mut started: c_int = 0;
		Error::from_module(n::oaknode_dragger_is_started(unbox(self_)?, &mut started))?;
		Ok(started)
	})
}

/// `oakengine_dragger_free`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_dragger_free(self_: *mut OakEngineNodeDragger) {
	guard_void(|| unsafe {
		if self_.is_null() {
			return;
		}
		let mut h = (*self_).handle;
		n::oaknode_dragger_free(&mut h);
		drop(Box::from_raw(self_));
	})
}

// ---------------------------------------------------------------------------
// node.h — node static data and helpers
// ---------------------------------------------------------------------------

/// `oakengine_node_enabled_input_id`.
#[no_mangle]
pub extern "C" fn oakengine_node_enabled_input_id() -> *const c_char {
	// Node::k_enabled_input (engine/node/node.cpp:47).
	static S: &[u8] = b"enabled_in\0";
	S.as_ptr() as *const c_char
}

/// `oakengine_volume_samples_input_id`.
#[no_mangle]
pub extern "C" fn oakengine_volume_samples_input_id() -> *const c_char {
	// VolumeNode::k_samples_input (engine/node/audio/volume/volume.cpp:29).
	static S: &[u8] = b"samples_in\0";
	S.as_ptr() as *const c_char
}

/// `oakengine_transform_texture_input_id`.
#[no_mangle]
pub extern "C" fn oakengine_transform_texture_input_id() -> *const c_char {
	// TransformDistortNode::k_texture_input
	// (engine/node/distort/transform/transformdistortnode.cpp:30).
	static S: &[u8] = b"tex_in\0";
	S.as_ptr() as *const c_char
}

/// `oakengine_transition_in_block_input_id`.
#[no_mangle]
pub extern "C" fn oakengine_transition_in_block_input_id() -> *const c_char {
	// TransitionBlock::k_in_block_input
	// (engine/node/block/transition/transition.cpp:34).
	static S: &[u8] = b"in_block_in\0";
	S.as_ptr() as *const c_char
}

/// `oakengine_transition_out_block_input_id`.
#[no_mangle]
pub extern "C" fn oakengine_transition_out_block_input_id() -> *const c_char {
	// TransitionBlock::k_out_block_input
	// (engine/node/block/transition/transition.cpp:33).
	static S: &[u8] = b"out_block_in\0";
	S.as_ptr() as *const c_char
}

/// `oakengine_audio_waveform_max_sample_rate`.
#[no_mangle]
pub extern "C" fn oakengine_audio_waveform_max_sample_rate() -> f64 {
	// AudioVisualWaveform::k_maximum_sample_rate (engine/audio/
	// audiovisualwaveform.cpp:33) is the Rational 1024.
	1024.0
}

/// `oakengine_node_category_name` — name of a CategoryID ordinal.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_category_name(
	category_id: c_int,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	guard_int(|| {
		// Node::get_category_name (engine/node/node.cpp:2348).
		let name = match category_id {
			0 => "Output",
			1 => "Generator",
			2 => "Math",
			3 => "Keying",
			4 => "Filter",
			5 => "Color",
			6 => "Time",
			7 => "Timeline",
			8 => "Transition",
			9 => "Distort",
			10 => "Project",
			11 => "OpenFX",
			_ => "Uncategorized",
		};
		Ok(unsafe { write_string(name, buf, buf_size) })
	})
}

/// `oakengine_node_link_command` — opaque NodeLinkCommand.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_link_command(
	a: *mut OakEngineNode,
	b: *mut OakEngineNode,
	link: c_int,
) -> *mut c_void {
	guard_ptr(|| unsafe {
		if a.is_null() || b.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let mut cmd: CHandle = CHandle::null();
		let rc = n::oaknode_node_link_undoable(unbox(a)?, unbox(b)?, if link != 0 { 1 } else { 0 }, &mut cmd);
		if rc != 0 {
			return Ok(std::ptr::null_mut());
		}
		Ok(command_box(cmd)?.cast())
	})
}

/// `oakengine_node_copy_in_graph` — copy with an optional parent multi.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_copy_in_graph(
	node: *mut OakEngineNode,
	command: *mut c_void,
) -> *mut OakEngineNode {
	guard_ptr(|| unsafe {
		if node.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let h = unbox(node)?;
		let mut cmd: CHandle = CHandle::null();
		let copy = n::oaknode_node_copy_in_graph(h, &mut cmd);
		if copy.is_null() {
			return Ok(std::ptr::null_mut());
		}
		if cmd.ctx.is_null() {
			return Ok(std::ptr::null_mut());
		}
		if command.is_null() {
			push_command(cmd, "Copy Node")?;
		} else {
			let rc = crate::undo::oakengine_undo_command_multi_add_child(
				command,
				command_box(cmd)?.cast(),
			);
			if rc != 0 {
				return Ok(std::ptr::null_mut());
			}
		}
		Ok(box_handle::<OakEngineNode>(copy))
	})
}

/// `oakengine_node_copy_dependency_graph`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_copy_dependency_graph(
	nodes: *mut *mut OakEngineNode,
	copies: *mut *mut OakEngineNode,
	count: c_int,
	command: *mut c_void,
) -> c_int {
	// Stub: the oaknode module has no dependency-graph copy.
	guard(|| {
		if nodes.is_null() || copies.is_null() || count <= 0 {
			return Err(Error::Invalid);
		}
		let _ = command;
		Err(Error::Invalid)
	})
}

/// `oakengine_node_connect_command_string`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_connect_command_string(
	output: *mut OakEngineNode,
	input_node: *mut OakEngineNode,
	input_id: *const c_char,
	element: c_int,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	// Stub: the module has no connect-command string generator; an empty
	// description is returned.
	guard_int(|| unsafe {
		if output.is_null() || input_node.is_null() || input_id.is_null() {
			return Err(Error::Invalid);
		}
		let _ = (unbox(output)?, unbox(input_node)?);
		let _ = element;
		Ok(write_string("", buf, buf_size))
	})
}

/// `oakengine_node_transform_time_to`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_transform_time_to(
	from: *mut OakEngineNode,
	to: *mut OakEngineNode,
	_direction: c_int,
	_path_index: c_int,
	in_num: i64,
	in_den: i64,
	out_num: i64,
	out_den: i64,
	result_in_num: *mut i64,
	result_in_den: *mut i64,
	result_out_num: *mut i64,
	result_out_den: *mut i64,
) -> c_int {
	// Stub: the oaknode module has no transform_time_to; the identity
	// range is returned.
	guard(|| unsafe {
		if from.is_null() || to.is_null() {
			return Err(Error::Invalid);
		}
		let _ = (unbox(from)?, unbox(to)?);
		if !result_in_num.is_null() {
			*result_in_num = in_num;
		}
		if !result_in_den.is_null() {
			*result_in_den = in_den;
		}
		if !result_out_num.is_null() {
			*result_out_num = out_num;
		}
		if !result_out_den.is_null() {
			*result_out_den = out_den;
		}
		Ok(())
	})
}

// ---------------------------------------------------------------------------
// node.h — NodeValue static methods
// ---------------------------------------------------------------------------

/// `oakengine_node_value_keyframe_track_count` — tracks for a value type.
#[no_mangle]
pub extern "C" fn oakengine_node_value_keyframe_track_count(c_type: c_int) -> c_int {
	// NodeValue::get_number_of_keyframe_tracks (engine/node/value.cpp:181).
	match c_type {
		value_type::VEC2 => 2,
		value_type::VEC3 => 3,
		value_type::VEC4 | value_type::COLOR => 4,
		value_type::BEZIER => 6,
		_ => 1,
	}
}

/// `oakengine_node_value_pretty_type_name` — display name (buf/size).
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_value_pretty_type_name(
	c_type: c_int,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	// NodeValue::get_pretty_data_type_name (engine/node/value.cpp:256).
	guard_int(|| {
		if c_type <= value_type::NONE || c_type > value_type::AUDIO_PARAMS {
			return Err(Error::Invalid);
		}
		let name = match c_type {
			value_type::INT | value_type::COMBO => "Integer",
			value_type::STR_COMBO => "String Combo",
			value_type::FLOAT => "Float",
			value_type::RATIONAL => "Rational",
			value_type::BOOL => "Boolean",
			value_type::COLOR => "Color",
			value_type::TEXT => "Text",
			value_type::FONT => "Font",
			value_type::STRING => "File",
			value_type::TEXTURE => "Texture",
			value_type::SAMPLES => "Samples",
			value_type::VEC2 => "Vector 2D",
			value_type::VEC3 => "Vector 3D",
			value_type::VEC4 => "Vector 4D",
			value_type::BINARY => "Binary",
			value_type::BEZIER => "Bezier",
			value_type::VIDEO_PARAMS => "Video Params",
			value_type::AUDIO_PARAMS => "Audio Params",
			_ => "None",
		};
		Ok(unsafe { write_string(name, buf, buf_size) })
	})
}

/// `oakengine_node_value_split_to_tracks` — split a normal value into
/// per-component track values.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_value_split_to_tracks(
	c_type: c_int,
	normal: *const OakNodeValue,
	tracks_out: *mut OakNodeValue,
	track_count: c_int,
) -> c_int {
	guard(|| unsafe {
		if normal.is_null() || tracks_out.is_null() || track_count <= 0 {
			return Err(Error::Invalid);
		}
		let n = *normal;
		let count = oakengine_node_value_keyframe_track_count(c_type).min(track_count);
		for i in 0..count as usize {
			let mut t = OakNodeValue {
				type_: c_type,
				num: 0,
				den: 0,
				f: [0.0; 4],
			};
			match c_type {
				value_type::INT | value_type::COMBO | value_type::BOOL => {
					t.num = n.num;
				}
				value_type::RATIONAL => {
					t.num = n.num;
					t.den = n.den;
				}
				value_type::COLOR | value_type::VEC2 | value_type::VEC3 | value_type::VEC4 => {
					t.f = n.f;
				}
				value_type::FLOAT | value_type::BEZIER => {
					t.f[0] = n.f[0];
				}
				_ => {
					t.type_ = c_type;
					t.f = n.f;
				}
			}
			*tracks_out.add(i) = t;
		}
		Ok(())
	})
}

/// `oakengine_node_value_combine_tracks` — combine track values into one.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_value_combine_tracks(
	c_type: c_int,
	tracks: *const OakNodeValue,
	track_count: c_int,
	normal_out: *mut OakNodeValue,
) -> c_int {
	guard(|| unsafe {
		if tracks.is_null() || normal_out.is_null() || track_count <= 0 {
			return Err(Error::Invalid);
		}
		let mut out = OakNodeValue {
			type_: c_type,
			num: 0,
			den: 0,
			f: [0.0; 4],
		};
		let first = *tracks;
		match c_type {
			value_type::INT | value_type::COMBO | value_type::BOOL => {
				out.num = first.num;
			}
			value_type::RATIONAL => {
				out.num = first.num;
				out.den = first.den;
			}
			value_type::FLOAT | value_type::BEZIER => {
				out.f[0] = first.f[0];
			}
			value_type::COLOR | value_type::VEC2 | value_type::VEC3 | value_type::VEC4 => {
				for i in 0..(track_count as usize).min(4) {
					out.f[i] = (*tracks.add(i)).f[0];
				}
			}
			_ => {
				out.f = first.f;
			}
		}
		*normal_out = out;
		Ok(())
	})
}

// ---------------------------------------------------------------------------
// node.h — node type queries (dynamic_cast replacements)
// ---------------------------------------------------------------------------

/// `oakengine_node_is_clip`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_is_clip(self_: *const OakEngineNode) -> c_int {
	guard_int(|| unsafe {
		if self_.is_null() {
			return Ok(0);
		}
		Ok(if is_node_type(unbox(self_)?, TYPE_ID_CLIP_BLOCK) {
			1
		} else {
			0
		})
	})
}

/// `oakengine_node_is_track`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_is_track(self_: *const OakEngineNode) -> c_int {
	guard_int(|| unsafe {
		if self_.is_null() {
			return Ok(0);
		}
		Ok(if is_node_type(unbox(self_)?, TYPE_ID_TRACK) {
			1
		} else {
			0
		})
	})
}

/// `oakengine_node_is_viewer_output` — Sequence or Footage.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_is_viewer_output(
	self_: *const OakEngineNode,
) -> c_int {
	guard_int(|| unsafe {
		if self_.is_null() {
			return Ok(0);
		}
		let h = unbox(self_)?;
		Ok(if is_node_type(h, TYPE_ID_SEQUENCE) || is_node_type(h, TYPE_ID_FOOTAGE) {
			1
		} else {
			0
		})
	})
}

/// `oakengine_node_is_footage`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_is_footage(self_: *const OakEngineNode) -> c_int {
	guard_int(|| unsafe {
		if self_.is_null() {
			return Ok(0);
		}
		Ok(if is_node_type(unbox(self_)?, TYPE_ID_FOOTAGE) {
			1
		} else {
			0
		})
	})
}

/// `oakengine_node_is_sequence`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_is_sequence(self_: *const OakEngineNode) -> c_int {
	guard_int(|| unsafe {
		if self_.is_null() {
			return Ok(0);
		}
		Ok(if is_node_type(unbox(self_)?, TYPE_ID_SEQUENCE) {
			1
		} else {
			0
		})
	})
}

/// `oakengine_node_is_folder`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_is_folder(self_: *const OakEngineNode) -> c_int {
	guard_int(|| unsafe {
		if self_.is_null() {
			return Ok(0);
		}
		Ok(if is_node_type(unbox(self_)?, TYPE_ID_FOLDER) {
			1
		} else {
			0
		})
	})
}

// ---------------------------------------------------------------------------
// node.h — clip / track specific
// ---------------------------------------------------------------------------

/// `oakengine_clip_get_track`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_clip_get_track(
	clip: *const OakEngineNode,
) -> *mut OakEngineNode {
	guard_ptr(|| unsafe {
		if clip.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let ch = unbox(clip)?;
		if !is_node_type(ch, TYPE_ID_CLIP_BLOCK) {
			return Ok(std::ptr::null_mut());
		}
		let block = n::oaknode_block_from_node(ch);
		if block.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let mut out = CHandle::null();
		let rc = n::oaknode_block_get_track(block, &mut out);
		if rc != 0 || out.is_null() {
			return Ok(std::ptr::null_mut());
		}
		Ok(box_handle::<OakEngineNode>(out))
	})
}

/// `oakengine_track_get_type`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_track_get_type(track: *const OakEngineNode) -> c_int {
	guard_int(|| unsafe {
		if track.is_null() {
			return Ok(-1);
		}
		let th = unbox(track)?;
		if !is_node_type(th, TYPE_ID_TRACK) {
			return Ok(-1);
		}
		let mut ty: c_int = -1;
		Error::from_module(n::oaknode_track_get_type(th, &mut ty))?;
		Ok(ty)
	})
}

/// `oakengine_track_get_index`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_track_get_index(track: *const OakEngineNode) -> c_int {
	guard_int(|| unsafe {
		if track.is_null() {
			return Ok(-1);
		}
		let th = unbox(track)?;
		if !is_node_type(th, TYPE_ID_TRACK) {
			return Ok(-1);
		}
		let mut index: c_int = -1;
		Error::from_module(n::oaknode_track_get_index(th, &mut index))?;
		Ok(index)
	})
}

/// `oakengine_track_get_sequence`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_track_get_sequence(
	track: *const OakEngineNode,
) -> *mut OakEngineNode {
	guard_ptr(|| unsafe {
		if track.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let th = unbox(track)?;
		if !is_node_type(th, TYPE_ID_TRACK) {
			return Ok(std::ptr::null_mut());
		}
		let mut out = CHandle::null();
		let rc = n::oaknode_track_get_sequence(th, &mut out);
		if rc != 0 || out.is_null() {
			return Ok(std::ptr::null_mut());
		}
		Ok(box_handle::<OakEngineNode>(out))
	})
}

/// `oakengine_block_get_length_rational`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_block_get_length_rational(
	block: *const OakEngineNode,
	num: *mut c_int,
	den: *mut c_int,
) -> c_int {
	guard(|| unsafe {
		block_rational(block, "length", num, den)
	})
}

/// `oakengine_block_get_in_rational`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_block_get_in_rational(
	block: *const OakEngineNode,
	num: *mut c_int,
	den: *mut c_int,
) -> c_int {
	guard(|| unsafe {
		block_rational(block, "in", num, den)
	})
}

/// `oakengine_block_get_out_rational`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_block_get_out_rational(
	block: *const OakEngineNode,
	num: *mut c_int,
	den: *mut c_int,
) -> c_int {
	guard(|| unsafe {
		block_rational(block, "out", num, den)
	})
}

/// Shared body of the block rational getters.
unsafe fn block_rational(block: *const OakEngineNode, which: &str, num: *mut c_int, den: *mut c_int) -> Result<()> {
	unsafe {
		if block.is_null() {
			return Err(Error::Invalid);
		}
		let bh = unbox(block)?;
		if !is_node_type(bh, TYPE_ID_CLIP_BLOCK)
			&& !is_node_type(bh, TYPE_ID_GAP_BLOCK)
			&& !is_node_type(bh, TYPE_ID_TRANSITION_BLOCK)
		{
			return Err(Error::Invalid);
		}
		let b = n::oaknode_block_from_node(bh);
		if b.is_null() {
			return Err(Error::Invalid);
		}
		let (mut n_, mut d) = (0 as c_int, 0 as c_int);
		let rc = match which {
			"in" => n::oaknode_block_get_in(b, &mut n_, &mut d),
			"out" => n::oaknode_block_get_out(b, &mut n_, &mut d),
			_ => n::oaknode_block_get_length(b, &mut n_, &mut d),
		};
		if rc != 0 {
			return Err(Error::Module(rc));
		}
		if !num.is_null() {
			*num = n_;
		}
		if !den.is_null() {
			*den = d;
		}
		Ok(())
	}
}

// ---------------------------------------------------------------------------
// node.h — viewer output specific
// ---------------------------------------------------------------------------

/// `oakengine_viewer_output_get_connected_texture`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_viewer_output_get_connected_texture(
	self_: *const OakEngineNode,
) -> *mut OakEngineNode {
	guard_ptr(|| unsafe {
		if self_.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let h = unbox(self_)?;
		if !is_node_type(h, TYPE_ID_SEQUENCE) && !is_node_type(h, TYPE_ID_FOOTAGE) {
			return Ok(std::ptr::null_mut());
		}
		let mut out = CHandle::null();
		// The viewer's texture input ("tex_in") — the engine default.
		let rc = n::oaknode_node_input_get_connected_node(h, c"tex_in".as_ptr(), &mut out);
		if rc != 0 || out.is_null() {
			return Ok(std::ptr::null_mut());
		}
		Ok(box_handle::<OakEngineNode>(out))
	})
}

// ---------------------------------------------------------------------------
// node.h — gizmo access
// ---------------------------------------------------------------------------

/// `oakengine_node_has_gizmos`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_has_gizmos(self_: *const OakEngineNode) -> c_int {
	// Stub: the oaknode module has no gizmo C ABI.
	guard_int(|| unsafe {
		if self_.is_null() {
			return Ok(0);
		}
		let _ = unbox(self_)?;
		Ok(0)
	})
}

/// `oakengine_node_gizmo_count`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_gizmo_count(self_: *const OakEngineNode) -> c_int {
	// Stub: see `oakengine_node_has_gizmos`.
	guard_int(|| unsafe {
		if self_.is_null() {
			return Ok(0);
		}
		let _ = unbox(self_)?;
		Ok(0)
	})
}

/// `oakengine_node_gizmo_at`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_gizmo_at(
	self_: *const OakEngineNode,
	index: c_int,
) -> *mut c_void {
	// Stub: see `oakengine_node_has_gizmos`.
	guard_ptr(|| {
		let _ = (self_, index);
		Ok(std::ptr::null_mut())
	})
}

/// `oakengine_node_update_gizmo_positions`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_update_gizmo_positions(
	self_: *mut OakEngineNode,
	_node_value_row: *mut c_void,
	_video_width: c_int,
	_video_height: c_int,
	_time_num: i64,
	_time_den: i64,
) -> c_int {
	// Stub: see `oakengine_node_has_gizmos` (no gizmos → the capi's
	// documented no-op result).
	guard(|| unsafe {
		if self_.is_null() {
			return Err(Error::Invalid);
		}
		let _ = unbox(self_)?;
		Ok(())
	})
}

// ---------------------------------------------------------------------------
// node.h — graph topology
// ---------------------------------------------------------------------------

/// `oakengine_node_inputs_from` — whether this node receives from `other`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_inputs_from(
	self_: *const OakEngineNode,
	other: *const OakEngineNode,
	recursive: c_int,
) -> c_int {
	guard_int(|| unsafe {
		if self_.is_null() || other.is_null() {
			return Ok(0);
		}
		let sh = unbox(self_)?;
		let oh = unbox(other)?;
		// BFS over the module's output connections starting at `other`
		// (inputs_from: is `other` reachable feeding into `self`?).
		let target = n::oaknode_node_identity(sh);
		let mut frontier = vec![oh];
		let mut visited: Vec<usize> = Vec::new();
		let mut depth = 0;
		while !frontier.is_empty() && (recursive != 0 || depth == 0) {
			let mut next = Vec::new();
			for cur in frontier {
				let id = n::oaknode_node_identity(cur);
				if id == target {
					return Ok(1);
				}
				if visited.contains(&id) {
					continue;
				}
				visited.push(id);
				let mut count: c_int = 0;
				if n::oaknode_node_output_connection_count(cur, &mut count) != 0 {
					continue;
				}
				for i in 0..count {
					let mut out = CHandle::null();
					if n::oaknode_node_output_connection_node_at(cur, i, &mut out) == 0
						&& !out.is_null()
					{
						next.push(out);
					}
				}
			}
			frontier = next;
			depth += 1;
		}
		Ok(0)
	})
}

/// `oakengine_node_output_connection_count`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_output_connection_count(
	self_: *const OakEngineNode,
) -> c_int {
	guard_int(|| unsafe {
		if self_.is_null() {
			return Ok(0);
		}
		let mut count: c_int = 0;
		Error::from_module(n::oaknode_node_output_connection_count(unbox(self_)?, &mut count))?;
		Ok(count)
	})
}

/// `oakengine_node_output_connection_at`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_output_connection_at(
	self_: *const OakEngineNode,
	index: c_int,
	input_node: *mut *mut OakEngineNode,
	input_id_buf: *mut c_char,
	input_id_size: c_int,
	element: *mut c_int,
) -> c_int {
	guard(|| unsafe {
		if self_.is_null() {
			return Err(Error::Invalid);
		}
		let h = unbox(self_)?;
		let mut count: c_int = 0;
		Error::from_module(n::oaknode_node_output_connection_count(h, &mut count))?;
		if index < 0 || index >= count {
			return Err(Error::NotFound);
		}
		let mut out = CHandle::null();
		let rc = n::oaknode_node_output_connection_node_at(h, index, &mut out);
		if rc != 0 {
			return Err(Error::Module(rc));
		}
		if !out.is_null() {
			if !input_node.is_null() {
				*input_node = box_handle::<OakEngineNode>(out);
			}
		}
		let rc = n::oaknode_node_output_connection_input_id_at(
			h,
			index,
			input_id_buf,
			input_id_size,
		);
		if rc < 0 {
			return Err(Error::Module(rc));
		}
		if !element.is_null() {
			let mut e: c_int = -1;
			let rc = n::oaknode_node_output_connection_element_at(h, index, &mut e);
			if rc != 0 {
				return Err(Error::Module(rc));
			}
			*element = e;
		}
		Ok(())
	})
}

/// `oakengine_node_output_connection_at_ex`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_output_connection_at_ex(
	self_: *const OakEngineNode,
	index: c_int,
	input_node: *mut *mut OakEngineNode,
	input_id_buf: *mut c_char,
	input_id_size: c_int,
	element: *mut c_int,
	hidden: *mut c_int,
) -> c_int {
	guard(|| unsafe {
		let rc = oakengine_node_output_connection_at(
			self_,
			index,
			input_node,
			input_id_buf,
			input_id_size,
			element,
		);
		Error::from_module(rc)?;
		// The module has no per-input hidden flag; reported as 0.
		if !hidden.is_null() {
			*hidden = 0;
		}
		Ok(())
	})
}

/// `oakengine_node_input_connection_count_all`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_input_connection_count_all(
	self_: *const OakEngineNode,
) -> c_int {
	// Stub: the oaknode module exposes output connections only.
	guard_int(|| unsafe {
		if self_.is_null() {
			return Ok(0);
		}
		let _ = unbox(self_)?;
		Ok(0)
	})
}

/// `oakengine_node_input_connection_at_all`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_input_connection_at_all(
	self_: *const OakEngineNode,
	index: c_int,
	input_node: *mut *mut OakEngineNode,
	input_id_buf: *mut c_char,
	input_id_size: c_int,
	element: *mut c_int,
	source_node: *mut *mut OakEngineNode,
	hidden: *mut c_int,
) -> c_int {
	// Stub: see `oakengine_node_input_connection_count_all`.
	guard(|| unsafe {
		if self_.is_null() {
			return Err(Error::Invalid);
		}
		let _ = unbox(self_)?;
		let _ = (index, input_node, input_id_buf, input_id_size, element, source_node, hidden);
		Err(Error::NotFound)
	})
}

/// `oakengine_node_input_connection_count`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_input_connection_count(
	self_: *const OakEngineNode,
	input_id: *const c_char,
	element: c_int,
) -> c_int {
	// Stub: see `oakengine_node_input_connection_count_all`.
	guard_int(|| unsafe {
		if self_.is_null() || input_id.is_null() {
			return Ok(0);
		}
		let _ = unbox(self_)?;
		let _ = element;
		Ok(0)
	})
}

/// `oakengine_node_input_connection_at`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_input_connection_at(
	self_: *const OakEngineNode,
	input_id: *const c_char,
	element: c_int,
	index: c_int,
) -> *mut OakEngineNode {
	// Stub: see `oakengine_node_input_connection_count_all`.
	guard_ptr(|| {
		let _ = (self_, input_id, element, index);
		Ok(std::ptr::null_mut())
	})
}

// ---------------------------------------------------------------------------
// node.h — node data (project tree columns)
// ---------------------------------------------------------------------------

/// `oakengine_node_get_data`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_get_data(
	self_: *const OakEngineNode,
	role: c_int,
	out_type: *mut c_int,
	out_int: *mut i64,
	out_str: *mut c_char,
	out_str_size: c_int,
) -> c_int {
	// Stub: the oaknode module has no Node::data() table; every role
	// reports "no data" (out_type 0) like a node without data.
	guard(|| unsafe {
		if !out_type.is_null() {
			*out_type = 0;
		}
		if !out_int.is_null() {
			*out_int = 0;
		}
		if !out_str.is_null() && out_str_size > 0 {
			*out_str = 0;
		}
		if self_.is_null() {
			return Err(Error::Invalid);
		}
		let _ = unbox(self_)?;
		if role < 0 || role > 5 {
			return Err(Error::Invalid);
		}
		Ok(())
	})
}

/// `oakengine_node_get_exclusive_dependency_count`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_get_exclusive_dependency_count(
	self_: *const OakEngineNode,
) -> c_int {
	// Stub: the oaknode module has no exclusive-dependency export.
	guard_int(|| unsafe {
		if self_.is_null() {
			return Ok(0);
		}
		let _ = unbox(self_)?;
		Ok(0)
	})
}

/// `oakengine_node_get_exclusive_dependency_at`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_get_exclusive_dependency_at(
	self_: *const OakEngineNode,
	index: c_int,
) -> *mut OakEngineNode {
	// Stub: see `oakengine_node_get_exclusive_dependency_count`.
	guard_ptr(|| {
		let _ = (self_, index);
		Ok(std::ptr::null_mut())
	})
}

// ---------------------------------------------------------------------------
// node.h — plugin messages
// ---------------------------------------------------------------------------

/// `oakengine_node_has_plugin`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_has_plugin(self_: *const OakEngineNode) -> c_int {
	// Stub: the oaknode module has no plugin-instance surface.
	guard_int(|| unsafe {
		if self_.is_null() {
			return Ok(0);
		}
		let _ = unbox(self_)?;
		Ok(0)
	})
}

/// `oakengine_node_plugin_message_count`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_plugin_message_count(
	self_: *const OakEngineNode,
) -> c_int {
	// Stub: see `oakengine_node_has_plugin`.
	guard_int(|| unsafe {
		if self_.is_null() {
			return Ok(0);
		}
		let _ = unbox(self_)?;
		Ok(0)
	})
}

/// `oakengine_node_plugin_message_at`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_plugin_message_at(
	self_: *const OakEngineNode,
	index: c_int,
	type_: *mut c_int,
	msg_buf: *mut c_char,
	msg_buf_size: c_int,
) -> c_int {
	// Stub: see `oakengine_node_has_plugin`.
	guard(|| unsafe {
		if self_.is_null() {
			return Err(Error::Invalid);
		}
		let _ = unbox(self_)?;
		let _ = (index, type_, msg_buf, msg_buf_size);
		Err(Error::NotFound)
	})
}

/// `oakengine_node_plugin_clear_messages`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_plugin_clear_messages(
	self_: *mut OakEngineNode,
) -> c_int {
	// Stub: see `oakengine_node_has_plugin`.
	guard(|| unsafe {
		if self_.is_null() {
			return Err(Error::Invalid);
		}
		let _ = unbox(self_)?;
		Err(Error::NotFound)
	})
}

// ---------------------------------------------------------------------------
// node.h — node cache objects
// ---------------------------------------------------------------------------

/// `oakengine_node_get_thumbnail_cache`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_get_thumbnail_cache(
	self_: *const OakEngineNode,
) -> *mut OakEngineThumbnailCache {
	// Stub: the oaknode module has no thumbnail-cache export (only the
	// video frame cache is exposed).
	guard_ptr(|| unsafe {
		if self_.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let _ = unbox(self_)?;
		Ok(std::ptr::null_mut())
	})
}

/// `oakengine_node_get_waveform_cache`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_get_waveform_cache(
	self_: *const OakEngineNode,
) -> *mut OakEngineWaveformCache {
	// Stub: the oaknode module has no waveform-cache export.
	guard_ptr(|| unsafe {
		if self_.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let _ = unbox(self_)?;
		Ok(std::ptr::null_mut())
	})
}

/// `oakengine_node_get_video_frame_cache`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_get_video_frame_cache(
	self_: *const OakEngineNode,
) -> *mut OakEngineFrameCache {
	guard_ptr(|| unsafe {
		if self_.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let mut out = CHandle::null();
		let rc = n::oaknode_node_get_video_frame_cache(unbox(self_)?, &mut out);
		if rc != 0 || out.is_null() {
			return Ok(std::ptr::null_mut());
		}
		Ok(box_handle::<OakEngineFrameCache>(out))
	})
}

// ---------------------------------------------------------------------------
// footage.h
// ---------------------------------------------------------------------------

/// The hidden process-wide project that holds probe nodes: the module has
/// no detached footage create (only `oaknode_footage_create(project, ...)`),
/// so probe handles are backed by nodes in this never-exposed project
/// (leaked like the C++ EngineCore shell).
fn probe_project() -> CHandle {
	static PROBE: std::sync::OnceLock<CHandle> = std::sync::OnceLock::new();
	*PROBE.get_or_init(|| unsafe {
		let p = n::oaknode_project_init();
		if !p.ctx.is_null() {
			n::oaknode_project_initialize(p);
		}
		p
	})
}

/// `oakengine_footage_probe` — probe a media file without a project.
#[no_mangle]
pub unsafe extern "C" fn oakengine_footage_probe(
	path: *const c_char,
) -> *mut OakEngineFootage {
	guard_ptr(|| unsafe {
		set_footage_error("");
		if path.is_null() {
			set_footage_error("file does not exist: (null)");
			return Ok(std::ptr::null_mut());
		}
		let path_str = read_cstr(path);
		if !std::path::Path::new(&path_str).exists() {
			set_footage_error(&format!("file does not exist: {}", path_str));
			return Ok(std::ptr::null_mut());
		}
		let filename = path_str;
		let footage = n::oaknode_footage_create(probe_project(), std::ffi::CString::new(filename.as_str()).unwrap().as_ptr());
		if footage.ctx.is_null() {
			set_footage_error(&format!("failed to probe \"{}\"", filename));
			return Ok(std::ptr::null_mut());
		}
		// The module's footage has no decoder probe cascade (it records
		// the filename only); the returned handle is a real node view, so
		// the stream/count accessors work and `oakengine_footage_free`
		// releases the wrapper. The node itself stays in the hidden probe
		// arena (documented deviation: the module cannot delete a
		// project-graph node on demand).
		Ok(box_handle::<OakEngineFootage>(footage))
	})
}

/// `oakengine_footage_free` — release a footage handle.
#[no_mangle]
pub unsafe extern "C" fn oakengine_footage_free(self_: *mut OakEngineFootage) {
	guard_void(|| unsafe {
		free_box(self_);
	})
}

/// `oakengine_footage_last_error` — last probe/import error on this thread.
#[no_mangle]
pub extern "C" fn oakengine_footage_last_error(buf: *mut c_char, buf_size: c_int) -> c_int {
	crate::handle::guard_int(|| unsafe {
		Ok(write_string(
			&LAST_FOOTAGE_ERROR.with(|c| c.borrow().clone()),
			buf,
			buf_size,
		))
	})
}

/// `oakengine_footage_get_decoder_name`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_footage_get_decoder_name(
	self_: *mut OakEngineFootage,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	guard_int(|| unsafe {
		let h = unbox(self_)?;
		let rc = n::oaknode_footage_decoder(h, buf, buf_size);
		if rc < 0 {
			Err(Error::Module(rc))
		} else {
			Ok(string_result(rc))
		}
	})
}

/// `oakengine_footage_get_video_stream_count`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_footage_get_video_stream_count(
	self_: *const OakEngineFootage,
) -> c_int {
	guard_int(|| unsafe {
		if self_.is_null() {
			return Ok(0);
		}
		let rc = n::oaknode_footage_video_stream_count(unbox(self_)?);
		Ok(if rc < 0 { 0 } else { rc })
	})
}

/// `oakengine_footage_get_audio_stream_count`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_footage_get_audio_stream_count(
	self_: *const OakEngineFootage,
) -> c_int {
	guard_int(|| unsafe {
		if self_.is_null() {
			return Ok(0);
		}
		let rc = n::oaknode_footage_audio_stream_count(unbox(self_)?);
		Ok(if rc < 0 { 0 } else { rc })
	})
}

/// `oakengine_footage_get_subtitle_stream_count`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_footage_get_subtitle_stream_count(
	self_: *const OakEngineFootage,
) -> c_int {
	guard_int(|| unsafe {
		if self_.is_null() {
			return Ok(0);
		}
		let rc = n::oaknode_footage_subtitle_stream_count(unbox(self_)?);
		Ok(if rc < 0 { 0 } else { rc })
	})
}

/// `oakengine_footage_get_video_stream_info`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_footage_get_video_stream_info(
	self_: *mut OakEngineFootage,
	index: c_int,
	out: *mut OakFootageVideoInfo,
) -> c_int {
	guard(|| unsafe {
		if self_.is_null() || out.is_null() {
			return Err(Error::Invalid);
		}
		let h = unbox(self_)?;
		let mut params: CHandle = CHandle::null();
		let rc = n::oaknode_footage_get_video_params(h, index, &mut params);
		if rc != 0 || params.ctx.is_null() {
			return Err(Error::NotFound);
		}
		let mut width: c_int = 0;
		let mut height: c_int = 0;
		let mut fr_num: c_int = 0;
		let mut fr_den: c_int = 0;
		let mut tb_num: c_int = 0;
		let mut tb_den: c_int = 0;
		let mut interlacing: c_int = 0;
		c::oakcommon_videoparams_get_width(params, &mut width);
		c::oakcommon_videoparams_get_height(params, &mut height);
		c::oakcommon_videoparams_get_frame_rate(params, &mut fr_num, &mut fr_den);
		c::oakcommon_videoparams_get_time_base(params, &mut tb_num, &mut tb_den);
		c::oakcommon_videoparams_get_interlacing(params, &mut interlacing);
		(*out) = OakFootageVideoInfo {
			stream_index: index,
			width,
			height,
			frame_rate_num: fr_num,
			frame_rate_den: fr_den,
			// The oakcommon videoparams handle has no duration accessor;
			// the module cannot provide it (documented).
			duration_ts: 0,
			time_base_num: tb_num,
			time_base_den: tb_den,
			// The module's videoparams has no color tag accessors.
			color_primaries: 0,
			color_trc: 0,
			interlaced: if interlacing != 0 { 1 } else { 0 },
		};
		let mut h2 = params;
		c::oakcommon_videoparams_free(&mut h2);
		Ok(())
	})
}

/// `oakengine_footage_get_audio_stream_info`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_footage_get_audio_stream_info(
	self_: *mut OakEngineFootage,
	index: c_int,
	out: *mut OakFootageAudioInfo,
) -> c_int {
	// Stub: the oaknode module exposes video params only; audio stream
	// descriptions are not reachable.
	guard(|| unsafe {
		if self_.is_null() || out.is_null() {
			return Err(Error::Invalid);
		}
		let h = unbox(self_)?;
		let count = n::oaknode_footage_audio_stream_count(h);
		if index < 0 || index >= count {
			return Err(Error::NotFound);
		}
		Err(Error::NotFound)
	})
}

/// `oakengine_footage_get_duration` — media duration in seconds.
#[no_mangle]
pub unsafe extern "C" fn oakengine_footage_get_duration(
	self_: *mut OakEngineFootage,
	seconds: *mut f64,
) -> c_int {
	guard(|| unsafe {
		if self_.is_null() || seconds.is_null() {
			return Err(Error::Invalid);
		}
		let mut num: c_int = 0;
		let mut den: c_int = 0;
		let rc = n::oaknode_footage_duration(unbox(self_)?, &mut num, &mut den);
		if rc != 0 {
			return Err(Error::Module(rc));
		}
		*seconds = if den != 0 { num as f64 / den as f64 } else { 0.0 };
		Ok(())
	})
}

/// `oakengine_footage_is_online` — 1 when the media file exists.
#[no_mangle]
pub unsafe extern "C" fn oakengine_footage_is_online(
	self_: *mut OakEngineFootage,
) -> c_int {
	guard_int(|| unsafe {
		if self_.is_null() {
			return Err(Error::Invalid);
		}
		let h = unbox(self_)?;
		let filename =
			module_string(|buf, size| n::oaknode_footage_filename(h, buf, size))?;
		Ok(if std::path::Path::new(&filename).exists() {
			1
		} else {
			0
		})
	})
}

/// `oakengine_footage_get_source_start_time`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_footage_get_source_start_time(
	self_: *mut OakEngineFootage,
	num: *mut c_int,
	den: *mut c_int,
) -> c_int {
	// Stub: the module's footage has no source-start-time state; the
	// "media carries none" result is returned.
	guard_int(|| unsafe {
		if self_.is_null() {
			return Err(Error::Invalid);
		}
		let _ = unbox(self_)?;
		if !num.is_null() {
			*num = 0;
		}
		if !den.is_null() {
			*den = 1;
		}
		Ok(0)
	})
}

/// `oakengine_project_import_footage` — probe and add to the root folder.
#[no_mangle]
pub unsafe extern "C" fn oakengine_project_import_footage(
	project: *mut OakEngineProject,
	path: *const c_char,
) -> *mut OakEngineFootage {
	guard_ptr(|| unsafe {
		set_footage_error("");
		if project.is_null() || path.is_null() {
			set_footage_error("invalid project or path");
			return Ok(std::ptr::null_mut());
		}
		let path_str = read_cstr(path);
		if !std::path::Path::new(&path_str).exists() {
			set_footage_error(&format!("file does not exist: {}", path_str));
			return Ok(std::ptr::null_mut());
		}
		let ph = unbox(project)?;
		let root = n::oaknode_project_root(ph);
		if root.is_null() {
			set_footage_error("invalid project or path");
			return Ok(std::ptr::null_mut());
		}
		// The module's footage_create registers the node in the project's
		// graph; only the FolderAddChild command is pushed (the capi also
		// pushes a NodeAddCommand, which has no module equivalent here).
		// The module cannot probe media, so the capi's validity rejection
		// is skipped (documented deviation).
		let path_c = std::ffi::CString::new(path_str.as_str()).unwrap();
		let footage = n::oaknode_footage_create(ph, path_c.as_ptr());
		if footage.ctx.is_null() {
			set_footage_error(&format!("failed to probe \"{}\"", path_str));
			return Ok(std::ptr::null_mut());
		}
		let label = std::path::Path::new(&path_str)
			.file_name()
			.map(|f| f.to_string_lossy().into_owned())
			.unwrap_or_else(|| path_str.clone());
		let label_c = std::ffi::CString::new(label.as_str()).unwrap();
		let rc = n::oaknode_node_set_label(footage, label_c.as_ptr());
		if rc != 0 {
			return Ok(std::ptr::null_mut());
		}
		let cmd = n::oaknode_command_create_folder_add_child(root, footage);
		if cmd.ctx.is_null() {
			return Ok(std::ptr::null_mut());
		}
		push_command(cmd, "Import Footage")?;
		Ok(box_handle::<OakEngineFootage>(footage))
	})
}

/// `oakengine_footage_borrow` — wrap a footage node in a borrowed handle.
#[no_mangle]
pub unsafe extern "C" fn oakengine_footage_borrow(
	node: *mut OakEngineNode,
) -> *mut OakEngineFootage {
	guard_ptr(|| unsafe {
		set_footage_error("");
		if node.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let h = unbox(node)?;
		if !is_node_type(h, TYPE_ID_FOOTAGE) {
			set_footage_error("node is not a footage node");
			return Ok(std::ptr::null_mut());
		}
		Ok(box_handle::<OakEngineFootage>(h))
	})
}

/// `oakengine_footage_is_valid` — 1 when the footage node is valid.
#[no_mangle]
pub unsafe extern "C" fn oakengine_footage_is_valid(node: *const OakEngineNode) -> c_int {
	guard_int(|| unsafe {
		if node.is_null() {
			return Ok(0);
		}
		let h = unbox(node)?;
		if !is_node_type(h, TYPE_ID_FOOTAGE) {
			return Ok(0);
		}
		let rc = n::oaknode_footage_is_valid(h);
		Ok(if rc > 0 { 1 } else { 0 })
	})
}

/// `oakengine_footage_relink` — point the footage at a new file.
#[no_mangle]
pub unsafe extern "C" fn oakengine_footage_relink(
	footage: *mut OakEngineFootage,
	new_path: *const c_char,
) -> c_int {
	guard(|| unsafe {
		set_footage_error("");
		if new_path.is_null() {
			set_footage_error("invalid path");
			return Err(Error::Invalid);
		}
		let h = unbox(footage)?;
		let path = read_cstr(new_path);
		if !std::path::Path::new(&path).exists() {
			set_footage_error(&format!("file does not exist: {}", path));
			return Err(Error::NotFound);
		}
		// The module's set_filename records the new path (no reprobe
		// cascade exists in the module; documented deviation).
		let rc = n::oaknode_footage_set_filename(h, new_path);
		Error::from_module(rc)
	})
}

/// `oakengine_project_find_offline_footage`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_project_find_offline_footage(
	project: *mut OakEngineProject,
	search_dir: *const c_char,
) -> c_int {
	guard_int(|| unsafe {
		set_footage_error("");
		if project.is_null() || search_dir.is_null() {
			set_footage_error("invalid arguments");
			return Err(Error::Invalid);
		}
		let search = read_cstr(search_dir);
		let dir = std::path::Path::new(&search);
		if !dir.exists() {
			set_footage_error(&format!("directory does not exist: {}", search));
			return Err(Error::Invalid);
		}
		let ph = unbox(project)?;
		let mut relinked: c_int = 0;
		let total = n::oaknode_project_node_count(ph);
		for i in 0..total {
			let node = n::oaknode_project_node_at(ph, i);
			if node.is_null() || !is_node_type(node, TYPE_ID_FOOTAGE) {
				continue;
			}
			let filename =
				module_string(|buf, size| n::oaknode_footage_filename(node, buf, size))?;
			if filename.is_empty() || std::path::Path::new(&filename).exists() {
				continue;
			}
			let base = std::path::Path::new(&filename)
				.file_name()
				.map(|f| f.to_string_lossy().into_owned())
				.unwrap_or_default();
			let candidate = dir.join(&base);
			if candidate.exists() {
				let cand = candidate.to_string_lossy().into_owned();
				let cand_c = std::ffi::CString::new(cand.as_str()).unwrap();
				let rc = n::oaknode_footage_set_filename(node, cand_c.as_ptr());
				if rc == 0 {
					relinked += 1;
				}
			}
		}
		Ok(relinked)
	})
}

/// `oakengine_footage_proxy_get_state`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_footage_proxy_get_state(
	self_: *mut OakEngineFootage,
) -> c_int {
	guard_int(|| unsafe {
		if self_.is_null() {
			return Err(Error::Invalid);
		}
		Ok(n::oaknode_footage_proxy_state(unbox(self_)?))
	})
}

/// `oakengine_footage_proxy_generate`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_footage_proxy_generate(
	self_: *mut OakEngineFootage,
) -> c_int {
	// Stub: the oaknode module has no ProxyManager / proxy task surface.
	guard(|| unsafe {
		set_footage_error("");
		if self_.is_null() {
			return Err(Error::Invalid);
		}
		let _ = unbox(self_)?;
		set_footage_error("proxy generation is not available through the module");
		Err(Error::State)
	})
}

/// `oakengine_footage_proxy_delete`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_footage_proxy_delete(
	self_: *mut OakEngineFootage,
) -> c_int {
	guard(|| unsafe {
		set_footage_error("");
		let h = unbox(self_)?;
		// The module clears the proxy state; the on-disk proxy files are
		// not removed (no file surface in the module).
		Error::from_module(n::oaknode_footage_clear_proxy(h))
	})
}

/// `oakengine_footage_proxy_is_enabled`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_footage_proxy_is_enabled(
	self_: *mut OakEngineFootage,
) -> c_int {
	guard_int(|| unsafe {
		if self_.is_null() {
			return Err(Error::Invalid);
		}
		Ok(n::oaknode_footage_proxy_enabled(unbox(self_)?))
	})
}

/// `oakengine_footage_proxy_set_enabled`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_footage_proxy_set_enabled(
	self_: *mut OakEngineFootage,
	enabled: c_int,
) -> c_int {
	guard(|| unsafe {
		set_footage_error("");
		let h = unbox(self_)?;
		Error::from_module(n::oaknode_footage_set_proxy_enabled(h, if enabled != 0 { 1 } else { 0 }))
	})
}

/// `oakengine_footage_proxy_get_path`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_footage_proxy_get_path(
	self_: *mut OakEngineFootage,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	guard_int(|| unsafe {
		let h = unbox(self_)?;
		let rc = n::oaknode_footage_proxy_path(h, buf, buf_size);
		if rc < 0 {
			Err(Error::Module(rc))
		} else {
			Ok(string_result(rc))
		}
	})
}

/* ---- Stream parameter overrides --------------------------------------------- */

/// `oakengine_footage_get_video_stream_overrides`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_footage_get_video_stream_overrides(
	self_: *mut OakEngineFootage,
	stream_index: c_int,
	colorspace_buf: *mut c_char,
	colorspace_size: c_int,
	color_range: *mut c_int,
	interlacing: *mut c_int,
	premultiplied: *mut c_int,
) -> c_int {
	guard(|| unsafe {
		set_footage_error("");
		let h = unbox(self_)?;
		let mut params: CHandle = CHandle::null();
		let rc = n::oaknode_footage_get_video_params(h, stream_index, &mut params);
		if rc != 0 || params.ctx.is_null() {
			set_footage_error(&format!("no video stream at index {}", stream_index));
			return Err(Error::NotFound);
		}
		// The oakcommon params carry no colorspace name; written empty.
		if !colorspace_buf.is_null() {
			write_string("", colorspace_buf, colorspace_size);
		}
		if !color_range.is_null() {
			c::oakcommon_videoparams_get_color_range(params, color_range);
		}
		if !interlacing.is_null() {
			c::oakcommon_videoparams_get_interlacing(params, interlacing);
		}
		if !premultiplied.is_null() {
			c::oakcommon_videoparams_get_premultiplied_alpha(params, premultiplied);
		}
		let mut h2 = params;
		c::oakcommon_videoparams_free(&mut h2);
		Ok(())
	})
}

/// `oakengine_footage_set_video_stream_overrides`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_footage_set_video_stream_overrides(
	self_: *mut OakEngineFootage,
	stream_index: c_int,
	colorspace: *const c_char,
	color_range: c_int,
	interlacing: c_int,
	premultiplied: c_int,
) -> c_int {
	guard(|| unsafe {
		set_footage_error("");
		let h = unbox(self_)?;
		let mut params: CHandle = CHandle::null();
		let rc = n::oaknode_footage_get_video_params(h, stream_index, &mut params);
		if rc != 0 || params.ctx.is_null() {
			set_footage_error(&format!("no video stream at index {}", stream_index));
			return Err(Error::NotFound);
		}
		// Colorspace is not representable in the module's params; the
		// numeric overrides are applied. The module has no undoable
		// params command, so this is applied directly (documented).
		let _ = colorspace;
		if color_range >= 0 {
			c::oakcommon_videoparams_set_color_range(params, color_range);
		}
		if interlacing >= 0 {
			c::oakcommon_videoparams_set_interlacing(params, interlacing);
		}
		if premultiplied >= 0 {
			c::oakcommon_videoparams_set_premultiplied_alpha(params, if premultiplied != 0 { 1 } else { 0 });
		}
		let rc = n::oaknode_footage_set_video_params(h, stream_index, &params);
		let mut h2 = params;
		c::oakcommon_videoparams_free(&mut h2);
		Error::from_module(rc)
	})
}

/// `oakengine_footage_get_pixel_aspect`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_footage_get_pixel_aspect(
	self_: *mut OakEngineFootage,
	stream_index: c_int,
	num: *mut c_int,
	den: *mut c_int,
) -> c_int {
	guard(|| unsafe {
		set_footage_error("");
		let h = unbox(self_)?;
		let mut params: CHandle = CHandle::null();
		let rc = n::oaknode_footage_get_video_params(h, stream_index, &mut params);
		if rc != 0 || params.ctx.is_null() {
			set_footage_error(&format!("no video stream at index {}", stream_index));
			return Err(Error::NotFound);
		}
		let mut n_: c_int = 1;
		let mut d: c_int = 1;
		c::oakcommon_videoparams_get_pixel_aspect_ratio(params, &mut n_, &mut d);
		if !num.is_null() {
			*num = n_;
		}
		if !den.is_null() {
			*den = d;
		}
		let mut h2 = params;
		c::oakcommon_videoparams_free(&mut h2);
		Ok(())
	})
}

/// `oakengine_footage_set_pixel_aspect`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_footage_set_pixel_aspect(
	self_: *mut OakEngineFootage,
	stream_index: c_int,
	num: c_int,
	den: c_int,
) -> c_int {
	guard(|| unsafe {
		set_footage_error("");
		let h = unbox(self_)?;
		if num <= 0 || den <= 0 {
			set_footage_error(&format!("invalid pixel aspect ratio {}/{}", num, den));
			return Err(Error::Invalid);
		}
		let mut params: CHandle = CHandle::null();
		let rc = n::oaknode_footage_get_video_params(h, stream_index, &mut params);
		if rc != 0 || params.ctx.is_null() {
			set_footage_error(&format!("no video stream at index {}", stream_index));
			return Err(Error::NotFound);
		}
		c::oakcommon_videoparams_set_pixel_aspect_ratio(params, num, den);
		let rc = n::oaknode_footage_set_video_params(h, stream_index, &params);
		let mut h2 = params;
		c::oakcommon_videoparams_free(&mut h2);
		Error::from_module(rc)
	})
}

/// `oakengine_footage_get_image_sequence_params`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_footage_get_image_sequence_params(
	self_: *mut OakEngineFootage,
	stream_index: c_int,
	start_index: *mut i64,
	duration: *mut i64,
	frame_rate_num: *mut c_int,
	frame_rate_den: *mut c_int,
) -> c_int {
	guard(|| unsafe {
		set_footage_error("");
		let h = unbox(self_)?;
		let mut params: CHandle = CHandle::null();
		let rc = n::oaknode_footage_get_video_params(h, stream_index, &mut params);
		if rc != 0 || params.ctx.is_null() {
			set_footage_error(&format!("no video stream at index {}", stream_index));
			return Err(Error::NotFound);
		}
		// start/duration are not accessible on the module's params;
		// the frame rate is.
		if !start_index.is_null() {
			*start_index = 0;
		}
		if !duration.is_null() {
			*duration = 0;
		}
		if !frame_rate_num.is_null() || !frame_rate_den.is_null() {
			let mut n_: c_int = 0;
			let mut d: c_int = 0;
			c::oakcommon_videoparams_get_frame_rate(params, &mut n_, &mut d);
			if !frame_rate_num.is_null() {
				*frame_rate_num = n_;
			}
			if !frame_rate_den.is_null() {
				*frame_rate_den = d;
			}
		}
		let mut h2 = params;
		c::oakcommon_videoparams_free(&mut h2);
		Ok(())
	})
}

/// `oakengine_footage_set_image_sequence_params`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_footage_set_image_sequence_params(
	self_: *mut OakEngineFootage,
	stream_index: c_int,
	start_index: i64,
	duration: i64,
	frame_rate_num: c_int,
	frame_rate_den: c_int,
) -> c_int {
	guard(|| unsafe {
		set_footage_error("");
		let h = unbox(self_)?;
		if start_index < 0 || duration <= 0 || frame_rate_num <= 0 || frame_rate_den <= 0 {
			set_footage_error("invalid image sequence parameters");
			return Err(Error::Invalid);
		}
		let mut params: CHandle = CHandle::null();
		let rc = n::oaknode_footage_get_video_params(h, stream_index, &mut params);
		if rc != 0 || params.ctx.is_null() {
			set_footage_error(&format!("no video stream at index {}", stream_index));
			return Err(Error::NotFound);
		}
		// The module's params have no start/duration setters; the frame
		// rate and its flipped time base are applied (documented).
		c::oakcommon_videoparams_set_frame_rate(params, frame_rate_num, frame_rate_den);
		c::oakcommon_videoparams_set_time_base(params, frame_rate_den, frame_rate_num);
		let rc = n::oaknode_footage_set_video_params(h, stream_index, &params);
		let mut h2 = params;
		c::oakcommon_videoparams_free(&mut h2);
		Error::from_module(rc)
	})
}

/// `oakengine_footage_get_stream_enabled`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_footage_get_stream_enabled(
	self_: *mut OakEngineFootage,
	track_type: c_int,
	index: c_int,
) -> c_int {
	// Stub: the module's stream params carry no enabled flag.
	guard_int(|| unsafe {
		if self_.is_null() {
			return Err(Error::Invalid);
		}
		let h = unbox(self_)?;
		let count = match track_type {
			0 => n::oaknode_footage_video_stream_count(h),
			1 => n::oaknode_footage_audio_stream_count(h),
			2 => n::oaknode_footage_subtitle_stream_count(h),
			_ => -1,
		};
		if index < 0 || index >= count {
			return Err(Error::NotFound);
		}
		Err(Error::NotFound)
	})
}

/// `oakengine_footage_set_stream_enabled`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_footage_set_stream_enabled(
	self_: *mut OakEngineFootage,
	track_type: c_int,
	index: c_int,
	enabled: c_int,
) -> c_int {
	// Stub: see `oakengine_footage_get_stream_enabled`.
	guard(|| unsafe {
		if self_.is_null() {
			return Err(Error::Invalid);
		}
		let _ = unbox(self_)?;
		let _ = (track_type, index, enabled);
		Err(Error::NotFound)
	})
}

/// `oakengine_footage_set_source_start_time`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_footage_set_source_start_time(
	self_: *mut OakEngineFootage,
	enabled: c_int,
	num: i64,
	den: i64,
) -> c_int {
	// Stub: the module's footage has no source-start-time state.
	guard(|| unsafe {
		if self_.is_null() {
			return Err(Error::Invalid);
		}
		let _ = unbox(self_)?;
		let _ = (enabled, num, den);
		Err(Error::Invalid)
	})
}

/// `oakengine_footage_get_source_start_time_source`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_footage_get_source_start_time_source(
	self_: *mut OakEngineFootage,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	// Stub: see `oakengine_footage_set_source_start_time`.
	guard_int(|| unsafe {
		if self_.is_null() {
			return Err(Error::Invalid);
		}
		let _ = unbox(self_)?;
		Ok(write_string("", buf, buf_size))
	})
}

/* ---- Colorspace candidates --------------------------------------------------- */

/// `oakengine_footage_colorspace_count`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_footage_colorspace_count(
	self_: *mut OakEngineFootage,
) -> c_int {
	// Stub: the module has no color-config surface.
	guard_int(|| unsafe {
		if self_.is_null() {
			return Ok(0);
		}
		let _ = unbox(self_)?;
		Ok(0)
	})
}

/// `oakengine_footage_colorspace_at`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_footage_colorspace_at(
	self_: *mut OakEngineFootage,
	index: c_int,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	// Stub: see `oakengine_footage_colorspace_count`.
	guard_int(|| unsafe {
		if self_.is_null() {
			return Err(Error::Invalid);
		}
		let _ = unbox(self_)?;
		let _ = (index, buf, buf_size);
		Err(Error::NotFound)
	})
}

/* ---- Footage extras ------------------------------------------------------- */

/// `oakengine_footage_get_filename`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_footage_get_filename(
	self_: *mut OakEngineFootage,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	guard_int(|| unsafe {
		let h = unbox(self_)?;
		let rc = n::oaknode_footage_filename(h, buf, buf_size);
		if rc < 0 {
			Err(Error::Module(rc))
		} else {
			Ok(string_result(rc))
		}
	})
}

/// `oakengine_footage_get_stream_reference`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_footage_get_stream_reference(
	self_: *mut OakEngineFootage,
	stream_index_in_footage: c_int,
	out_track_type: *mut c_int,
	out_stream_index: *mut c_int,
) -> c_int {
	guard(|| unsafe {
		if self_.is_null() {
			return Err(Error::Invalid);
		}
		let h = unbox(self_)?;
		let vc = n::oaknode_footage_video_stream_count(h);
		let ac = n::oaknode_footage_audio_stream_count(h);
		let sc = n::oaknode_footage_subtitle_stream_count(h);
		let mut flat = stream_index_in_footage;
		if flat >= 0 && flat < vc {
			if !out_track_type.is_null() {
				*out_track_type = 0; // OAKENGINE_TRACK_TYPE_VIDEO
			}
			if !out_stream_index.is_null() {
				*out_stream_index = flat;
			}
			return Ok(());
		}
		flat -= vc;
		if flat >= 0 && flat < ac {
			if !out_track_type.is_null() {
				*out_track_type = 1; // OAKENGINE_TRACK_TYPE_AUDIO
			}
			if !out_stream_index.is_null() {
				*out_stream_index = flat;
			}
			return Ok(());
		}
		flat -= ac;
		if flat >= 0 && flat < sc {
			if !out_track_type.is_null() {
				*out_track_type = 2; // OAKENGINE_TRACK_TYPE_SUBTITLE
			}
			if !out_stream_index.is_null() {
				*out_stream_index = flat;
			}
			return Ok(());
		}
		Err(Error::NotFound)
	})
}

/// `oakengine_footage_describe_video_stream`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_footage_describe_video_stream(
	self_: *mut OakEngineFootage,
	video_stream_index: c_int,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	guard_int(|| unsafe {
		if self_.is_null() {
			return Err(Error::Invalid);
		}
		let h = unbox(self_)?;
		let mut params: CHandle = CHandle::null();
		let rc = n::oaknode_footage_get_video_params(h, video_stream_index, &mut params);
		if rc != 0 || params.ctx.is_null() {
			return Err(Error::NotFound);
		}
		let mut width: c_int = 0;
		let mut height: c_int = 0;
		let mut fr_num: c_int = 0;
		let mut fr_den: c_int = 0;
		c::oakcommon_videoparams_get_width(params, &mut width);
		c::oakcommon_videoparams_get_height(params, &mut height);
		c::oakcommon_videoparams_get_frame_rate(params, &mut fr_num, &mut fr_den);
		let desc = if fr_den != 0 {
			format!("{}x{}, {:.3} fps", width, height, fr_num as f64 / fr_den as f64)
		} else {
			format!("{}x{}", width, height)
		};
		let mut h2 = params;
		c::oakcommon_videoparams_free(&mut h2);
		Ok(write_string(&desc, buf, buf_size))
	})
}

/// `oakengine_footage_describe_audio_stream`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_footage_describe_audio_stream(
	self_: *mut OakEngineFootage,
	audio_stream_index: c_int,
	_buf: *mut c_char,
	_buf_size: c_int,
) -> c_int {
	// Stub: see `oakengine_footage_get_audio_stream_info` (no audio
	// params surface).
	guard_int(|| unsafe {
		if self_.is_null() {
			return Err(Error::Invalid);
		}
		let h = unbox(self_)?;
		let count = n::oaknode_footage_audio_stream_count(h);
		if audio_stream_index < 0 || audio_stream_index >= count {
			return Err(Error::NotFound);
		}
		Err(Error::NotFound)
	})
}

/// `oakengine_footage_stream_type_name`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_footage_stream_type_name(
	track_type: c_int,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	guard_int(|| {
		let name = match track_type {
			0 => "Video",
			1 => "Audio",
			2 => "Subtitle",
			_ => "Unknown",
		};
		Ok(unsafe { write_string(name, buf, buf_size) })
	})
}

/// `oakengine_footage_has_custom_proxy_params`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_footage_has_custom_proxy_params(
	self_: *mut OakEngineFootage,
) -> c_int {
	// Stub: the module's footage has no custom-proxy-params state.
	guard_int(|| unsafe {
		if self_.is_null() {
			return Err(Error::Invalid);
		}
		let _ = unbox(self_)?;
		Ok(0)
	})
}

/// `oakengine_footage_get_effective_proxy_params`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_footage_get_effective_proxy_params(
	self_: *mut OakEngineFootage,
	out: *mut OakProxyParams,
) -> c_int {
	// Stub: the module has no proxy-params surface; a zeroed struct is
	// returned.
	guard(|| unsafe {
		if self_.is_null() || out.is_null() {
			return Err(Error::Invalid);
		}
		let _ = unbox(self_)?;
		*out = OakProxyParams {
			width: 0,
			height: 0,
			divider: 0,
			version: 0,
			crf: 0,
			include_audio: 0,
			extension: [0; 32],
			preset: [0; 32],
		};
		Ok(())
	})
}

/// `oakengine_footage_set_custom_proxy_params`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_footage_set_custom_proxy_params(
	self_: *mut OakEngineFootage,
	_params: *const OakProxyParams,
) -> c_int {
	// Stub: see `oakengine_footage_has_custom_proxy_params`.
	guard(|| unsafe {
		if self_.is_null() || _params.is_null() {
			return Err(Error::Invalid);
		}
		let _ = unbox(self_)?;
		Err(Error::Invalid)
	})
}

/// `oakengine_footage_clear_custom_proxy_params`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_footage_clear_custom_proxy_params(
	self_: *mut OakEngineFootage,
) -> c_int {
	// Stub: see `oakengine_footage_has_custom_proxy_params`.
	guard(|| unsafe {
		if self_.is_null() {
			return Err(Error::Invalid);
		}
		let _ = unbox(self_)?;
		Err(Error::Invalid)
	})
}

/// `oakengine_footage_set_proxy`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_footage_set_proxy(
	self_: *mut OakEngineFootage,
	path: *const c_char,
	state: c_int,
	stream_index: c_int,
	enabled: c_int,
	version: c_int,
) -> c_int {
	guard(|| unsafe {
		if self_.is_null() {
			return Err(Error::Invalid);
		}
		let h = unbox(self_)?;
		let path = if path.is_null() {
			crate::common::empty_cstr()
		} else {
			path
		};
		Error::from_module(n::oaknode_footage_set_proxy(
			h,
			path,
			state,
			stream_index,
			version,
			if enabled != 0 { 1 } else { 0 },
		))
	})
}

/// `oakengine_footage_clear_proxy`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_footage_clear_proxy(
	self_: *mut OakEngineFootage,
) -> c_int {
	guard(|| unsafe {
		if self_.is_null() {
			return Err(Error::Invalid);
		}
		Error::from_module(n::oaknode_footage_clear_proxy(unbox(self_)?))
	})
}

/// `oakengine_footage_invalidate` — force a re-probe on next use.
#[no_mangle]
pub unsafe extern "C" fn oakengine_footage_invalidate(
	self_: *mut OakEngineFootage,
) -> c_int {
	// Stub: the module's footage has no clear/reprobe cascade; accepted
	// as a no-op.
	guard(|| unsafe {
		if self_.is_null() {
			return Err(Error::Invalid);
		}
		let _ = unbox(self_)?;
		Ok(())
	})
}
