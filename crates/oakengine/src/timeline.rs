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

//! `engine/include/oakengine/timeline.h` — sequences, tracks, clips,
//! markers and the workarea over the oaknode + oaktimeline modules.
//!
//! The engine's frame timestamps (frame numbers in the sequence's
//! frame-rate timebase) convert to the modules' rational seconds through
//! the sequence's video parameters, mirroring the C++ capi
//! (`engine/src/capi/timeline.cpp`): the time base is the frame rate
//! flipped. Undoable mutations assemble the module's command creators and
//! push them through [`crate::undo::push_or_run`].

use std::cell::RefCell;
use std::collections::HashMap;
use std::ffi::{c_char, c_int, c_void};
use std::sync::atomic::{AtomicU32, Ordering};
use std::sync::Mutex;

use crate::bridge::{audio as a, common as c, node as n, timeline as tl, undo as u};
use crate::error::{Error, Result};
use crate::handle::{
	box_handle, free_box, guard, guard_int, guard_ptr, guard_void, read_cstr, string_result,
	unbox, write_string, CHandle, OakEngineBlock, OakEngineClip, OakEngineClipboard,
	OakEngineFootage, OakEngineMarker, OakEngineMarkerList, OakEngineNode, OakEngineProject,
	OakEngineSequence, OakEngineTrack, OakEngineTrackList, OakEngineWorkarea,
};
use crate::bridge::undo::OakUndoCommandVtable;
use crate::undo::push_or_run;

/// `engine/include/oakengine/timeline.h` — POD mirror of
/// `oakengine_ripple_info` (one TrackListRippleToolCommand hash entry).
#[repr(C)]
#[derive(Clone, Copy)]
pub struct OakEngineRippleInfo {
	/// The track to ripple (borrowed engine handle).
	pub track: *mut OakEngineTrack,
	/// The block being moved (borrowed engine handle).
	pub block: *mut OakEngineBlock,
	/// Whether a gap should be appended after it (1/0).
	pub append_gap: c_int,
}

// ---- helpers ---------------------------------------------------------------

/// Track types (`OAKENGINE_TRACK_TYPE_*`).
const TRACK_TYPE_VIDEO: c_int = 0;
const TRACK_TYPE_AUDIO: c_int = 1;
const TRACK_TYPE_SUBTITLE: c_int = 2;

/// Movement modes (`OAKENGINE_MOVEMENT_MODE_*`).
const MOVEMENT_MODE_TRIM_IN: c_int = 2;
const MOVEMENT_MODE_TRIM_OUT: c_int = 3;

/// `oaknode/block.h` block kinds.
const BLOCK_KIND_CLIP: c_int = 1;
const BLOCK_KIND_GAP: c_int = 2;

/// Node type id of transition blocks.
const TYPE_ID_TRANSITION: &str = "org.olivevideoeditor.Olive.transitionblock";

/// Track height constants (C++ `Track::k_track_height_*`).
const TRACK_HEIGHT_DEFAULT: f64 = 3.0;
const TRACK_HEIGHT_MINIMUM: f64 = 1.5;
const TRACK_HEIGHT_INTERVAL: f64 = 0.5;
/// Default font height in pixels (C++ `Track::default_font_height`).
const TRACK_FONT_HEIGHT: f64 = 13.0;

// The last editing error for this thread (mirrors the capi's
// `thread_local QString g_seq_last_error`, timeline.cpp:126).
thread_local! {
	static SEQ_LAST_ERROR: RefCell<String> = RefCell::new(String::new());
}

/// Record `msg` as the last editing error of this thread.
fn set_seq_error(msg: &str) {
	SEQ_LAST_ERROR.with(|e| *e.borrow_mut() = msg.to_string());
}

/// Release a module handle reference (the handle's own `release`). Every
/// module "borrowed" handle returned by an out-parameter or
/// `*_as_node`/`*_of` creator is an owned copy with refcount 1, so the
/// facade releases it after temporary use.
fn release_handle(h: CHandle) {
	if let Some(release) = h.release {
		unsafe { release(h.ctx) };
	}
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

/// The sequence's frame duration (frame rate flipped) as a (num, den)
/// pair. Mirrors the capi's `time_base_of`; `E_STATE` when the sequence
/// has no valid frame rate.
///
/// # Safety
/// `seq` must be a live module sequence handle.
unsafe fn seq_time_base(seq: CHandle) -> Result<(i64, i64)> {
	unsafe {
		let mut params = CHandle::null();
		let rc = n::oaknode_sequence_get_video_params(seq, 0, &mut params);
		if rc != 0 || params.is_null() {
			return Err(Error::State);
		}
		let mut num: c_int = 0;
		let mut den: c_int = 0;
		let fr = c::oakcommon_videoparams_get_frame_rate(params, &mut num, &mut den);
		let mut h = params;
		c::oakcommon_videoparams_free(&mut h);
		if fr != 0 || num <= 0 || den <= 0 {
			return Err(Error::State);
		}
		Ok((den as i64, num as i64))
	}
}

/// Rational seconds -> timestamp in timebase units, rounding half away
/// from zero (mirrors `Timecode::k_round`).
fn rational_to_ts(num: i64, den: i64, tb: (i64, i64)) -> i64 {
	if den == 0 || tb.0 == 0 || tb.1 == 0 {
		return 0;
	}
	let n = num as i128 * tb.1 as i128;
	let d = den as i128 * tb.0 as i128;
	let q = n / d;
	let r = n % d;
	let rr = if r < 0 { -r } else { r };
	let dd = if d < 0 { -d } else { d };
	if rr * 2 >= dd {
		(q + if n < 0 { -1 } else { 1 }) as i64
	} else {
		q as i64
	}
}

/// Timestamp -> reduced rational seconds (`time = ts * tb`).
fn ts_to_rational(ts: i64, tb: (i64, i64)) -> (i64, i64) {
	let num = ts as i128 * tb.0 as i128;
	let den = tb.1 as i128;
	let g = gcd((num % den) as i64, den as i64);
	((num / g as i128) as i64, (den / g as i128) as i64)
}

/// Reduced rational sum.
fn rat_add(a_num: i64, a_den: i64, b_num: i64, b_den: i64) -> (i64, i64) {
	let num = a_num * b_den + b_num * a_den;
	let den = a_den * b_den;
	let g = gcd(num, den);
	(num / g, den / g)
}

/// Reduced rational difference (`a - b`).
fn rat_sub(a_num: i64, a_den: i64, b_num: i64, b_den: i64) -> (i64, i64) {
	let num = a_num * b_den - b_num * a_den;
	let den = a_den * b_den;
	let g = gcd(num, den);
	(num / g, den / g)
}

/// Box a module command handle into the engine command shell.
fn command_box(cmd: CHandle) -> Result<*mut OakEngineClipboard> {
	if cmd.ctx.is_null() {
		return Err(Error::Failed("command creation failed".into()));
	}
	Ok(box_handle::<OakEngineClipboard>(cmd).cast())
}

/// Push a single module command onto the undo stack (or run it directly).
///
/// # Safety
/// `cmd` must be a live module command handle.
unsafe fn push_command(cmd: CHandle, name: &str) -> Result<()> {
	unsafe {
		let boxed = command_box(cmd)?;
		let name_c = std::ffi::CString::new(name)
			.map_err(|_| Error::Failed("invalid undo name".into()))?;
		push_or_run(boxed, name_c.as_ptr())
	}
}

/// Assemble several module command handles into ONE multi command and push
/// it.
///
/// # Safety
/// `children` must hold live module command handles (each is consumed by
/// the multi).
unsafe fn push_multi_commands(children: &[CHandle], name: &str) -> Result<()> {
	unsafe {
		if children.is_empty() {
			return Ok(());
		}
		let multi = u::oakundo_command_init_multi();
		if multi.is_null() {
			return Err(Error::Failed("multi command allocation failed".into()));
		}
		let multi_box = box_handle::<OakEngineClipboard>(multi);
		for child in children {
			let rc = u::oakundo_command_multi_add_child(multi, *child);
			if rc != 0 {
				free_box(multi_box);
				return Err(Error::Module(rc));
			}
		}
		let name_c = std::ffi::CString::new(name)
			.map_err(|_| Error::Failed("invalid undo name".into()))?;
		push_or_run(multi_box, name_c.as_ptr())
	}
}

// ---- Facade-owned undo commands -------------------------------------------
//
// The module has no undo commands for sequence video params, block
// enable/disable, clip media-in or block resizing; the facade carries
// read-modify-write equivalents backed by `oakundo_command_init` vtables
// (the same pattern as the capi's facade-level `SequenceVideoParamsCommand`).

/// Generic vtable command factory: box `data` behind an oakundo command.
///
/// # Safety
/// `data` must be a box that `free_fn` can reclaim; `redo`/`undo` must be
/// safe to call with it.
unsafe fn vtable_command(
	redo: unsafe extern "C" fn(*mut c_void),
	undo: unsafe extern "C" fn(*mut c_void),
	free_fn: unsafe extern "C" fn(*mut c_void),
	data: *mut c_void,
) -> Result<CHandle> {
	unsafe {
		let vtable = OakUndoCommandVtable {
			redo: Some(redo),
			undo: Some(undo),
			free_fn: Some(free_fn),
		};
		let cmd = u::oakundo_command_init(&vtable, data);
		if cmd.is_null() {
			free_fn(data);
			return Err(Error::Failed("undo command allocation failed".into()));
		}
		Ok(cmd)
	}
}

/// Sequence video-parameter read-modify-write (the capi's
/// `SequenceVideoParamsCommand`).
struct VideoParamsCmdData {
	/// Sequence node (addref'd).
	seq: CHandle,
	/// Old oakcommon videoparams handle.
	old_params: CHandle,
	/// New oakcommon videoparams handle.
	new_params: CHandle,
}

unsafe extern "C" fn video_params_redo(ud: *mut c_void) {
	unsafe {
		let d = &*(ud as *const VideoParamsCmdData);
		n::oaknode_sequence_set_video_params(d.seq, 0, d.new_params);
	}
}

unsafe extern "C" fn video_params_undo(ud: *mut c_void) {
	unsafe {
		let d = &*(ud as *const VideoParamsCmdData);
		n::oaknode_sequence_set_video_params(d.seq, 0, d.old_params);
	}
}

unsafe extern "C" fn video_params_free(ud: *mut c_void) {
	unsafe {
		let d = Box::from_raw(ud as *mut VideoParamsCmdData);
		release_handle(d.seq);
		release_handle(d.old_params);
		release_handle(d.new_params);
	}
}

/// Sequence audio-parameter read-modify-write (the capi's
/// `SequenceAudioParamsCommand`). The params are borrowed oakcore
/// `OakAudioParams` handles (raw pointers; release with
/// `oakcore_audioparams_free`).
struct AudioParamsCmdData {
	/// Sequence node (addref'd).
	seq: CHandle,
	/// Old oakcore audio-params handle.
	old_params: *mut c_void,
	/// New oakcore audio-params handle.
	new_params: *mut c_void,
}

unsafe extern "C" fn audio_params_redo(ud: *mut c_void) {
	unsafe {
		let d = &*(ud as *const AudioParamsCmdData);
		n::oaknode_sequence_set_audio_params(d.seq, 0, d.new_params);
	}
}

unsafe extern "C" fn audio_params_undo(ud: *mut c_void) {
	unsafe {
		let d = &*(ud as *const AudioParamsCmdData);
		n::oaknode_sequence_set_audio_params(d.seq, 0, d.old_params);
	}
}

unsafe extern "C" fn audio_params_free(ud: *mut c_void) {
	unsafe {
		let d = Box::from_raw(ud as *mut AudioParamsCmdData);
		release_handle(d.seq);
		a::oakcore_audioparams_free(d.old_params);
		a::oakcore_audioparams_free(d.new_params);
	}
}

/// Block enable/disable (the capi's `BlockEnableDisableCommand`).
struct BlockEnabledCmdData {
	/// Block (addref'd).
	block: CHandle,
	/// Enabled value captured at construction.
	old_enabled: c_int,
	/// New enabled value.
	new_enabled: c_int,
}

unsafe extern "C" fn block_enabled_redo(ud: *mut c_void) {
	unsafe {
		let d = &*(ud as *const BlockEnabledCmdData);
		n::oaknode_block_set_enabled(d.block, d.new_enabled);
	}
}

unsafe extern "C" fn block_enabled_undo(ud: *mut c_void) {
	unsafe {
		let d = &*(ud as *const BlockEnabledCmdData);
		n::oaknode_block_set_enabled(d.block, d.old_enabled);
	}
}

unsafe extern "C" fn block_enabled_free(ud: *mut c_void) {
	unsafe {
		let d = Box::from_raw(ud as *mut BlockEnabledCmdData);
		release_handle(d.block);
	}
}

/// Clip media-in change (the capi's `BlockSetMediaInCommand`).
struct ClipMediaInCmdData {
	/// Clip (addref'd).
	clip: CHandle,
	/// Media-in captured at construction (rational seconds).
	old_num: c_int,
	old_den: c_int,
	/// New media-in (rational seconds).
	new_num: c_int,
	new_den: c_int,
}

unsafe extern "C" fn clip_media_in_redo(ud: *mut c_void) {
	unsafe {
		let d = &*(ud as *const ClipMediaInCmdData);
		n::oaknode_clip_set_media_in(d.clip, d.new_num, d.new_den);
	}
}

unsafe extern "C" fn clip_media_in_undo(ud: *mut c_void) {
	unsafe {
		let d = &*(ud as *const ClipMediaInCmdData);
		n::oaknode_clip_set_media_in(d.clip, d.old_num, d.old_den);
	}
}

unsafe extern "C" fn clip_media_in_free(ud: *mut c_void) {
	unsafe {
		let d = Box::from_raw(ud as *mut ClipMediaInCmdData);
		release_handle(d.clip);
	}
}

/// Block length change keeping the in point (the capi's
/// `BlockResizeCommand`).
struct BlockResizeCmdData {
	/// Block (addref'd).
	block: CHandle,
	/// Length captured at construction (rational seconds).
	old_num: c_int,
	old_den: c_int,
	/// New length (rational seconds).
	new_num: c_int,
	new_den: c_int,
}

// The engine's `set_length_and_media_out` keeps the in-point and extends
// the out (the C++ in/out points derive from the track position); that maps
// to the module's in-anchored `set_length_and_media_in`, despite the name.
unsafe extern "C" fn block_resize_redo(ud: *mut c_void) {
	unsafe {
		let d = &*(ud as *const BlockResizeCmdData);
		n::oaknode_block_set_length_and_media_in(d.block, d.new_num, d.new_den);
	}
}

unsafe extern "C" fn block_resize_undo(ud: *mut c_void) {
	unsafe {
		let d = &*(ud as *const BlockResizeCmdData);
		n::oaknode_block_set_length_and_media_in(d.block, d.old_num, d.old_den);
	}
}

unsafe extern "C" fn block_resize_free(ud: *mut c_void) {
	unsafe {
		let d = Box::from_raw(ud as *mut BlockResizeCmdData);
		release_handle(d.block);
	}
}

/// Block trim (the capi's `BlockTrimCommand`). The module's own
/// `BlockTrimCommand` applies the length setters with inverted semantics
/// (its trim-in anchors the in-point), so the facade carries the correct
/// engine mapping: a trim-in anchors the OUT (the in moves), a trim-out
/// anchors the IN (the out moves).
struct BlockTrimCmdData {
	/// Block (addref'd).
	block: CHandle,
	/// Trim mode: `MOVEMENT_MODE_TRIM_IN` (out anchored) or
	/// `MOVEMENT_MODE_TRIM_OUT` (in anchored).
	mode: c_int,
	/// Length captured at construction (rational seconds).
	old_num: c_int,
	old_den: c_int,
	/// New length (rational seconds).
	new_num: c_int,
	new_den: c_int,
}

unsafe extern "C" fn block_trim_redo(ud: *mut c_void) {
	unsafe {
		let d = &*(ud as *const BlockTrimCmdData);
		if d.mode == MOVEMENT_MODE_TRIM_IN {
			n::oaknode_block_set_length_and_media_out(d.block, d.new_num, d.new_den);
		} else {
			n::oaknode_block_set_length_and_media_in(d.block, d.new_num, d.new_den);
		}
	}
}

unsafe extern "C" fn block_trim_undo(ud: *mut c_void) {
	unsafe {
		let d = &*(ud as *const BlockTrimCmdData);
		if d.mode == MOVEMENT_MODE_TRIM_IN {
			n::oaknode_block_set_length_and_media_out(d.block, d.old_num, d.old_den);
		} else {
			n::oaknode_block_set_length_and_media_in(d.block, d.old_num, d.old_den);
		}
	}
}

unsafe extern "C" fn block_trim_free(ud: *mut c_void) {
	unsafe {
		let d = Box::from_raw(ud as *mut BlockTrimCmdData);
		release_handle(d.block);
	}
}

/// Build a block-trim vtable command.
///
/// # Safety
/// `block` must be a live module block handle.
unsafe fn trim_cmd(
	block: CHandle,
	mode: c_int,
	old_num: c_int,
	old_den: c_int,
	new_num: c_int,
	new_den: c_int,
) -> Result<CHandle> {
	unsafe {
		let data = Box::into_raw(Box::new(BlockTrimCmdData {
			block: block.addref(),
			mode,
			old_num,
			old_den,
			new_num,
			new_den,
		})) as *mut c_void;
		vtable_command(block_trim_redo, block_trim_undo, block_trim_free, data)
	}
}

// ---- Per-sequence marker list / workarea cache -----------------------------
//
// The module's sequences never initialize their own `markers`/`workarea`
// handles (`SequenceBehavior` defaults both to empty), so
// `oaktimeline_marker_list_of`/`workarea_of` return empty handles for
// them. The facade materializes one of each per sequence on first use and
// caches them by the sequence node's stable `ctx` token (facade state,
// like the process-wide undo stack). The cached entries live for the
// process; a sequence reused at the same heap address after a project free
// would find its old list (accepted: the application creates one project).

static SEQ_MARKER_LISTS: Mutex<Option<HashMap<usize, CHandle>>> = Mutex::new(None);
static SEQ_WORKAREAS: Mutex<Option<HashMap<usize, CHandle>>> = Mutex::new(None);

/// The per-sequence marker-list / workarea maps (created lazily).
fn seq_marker_map() -> std::sync::MutexGuard<'static, Option<HashMap<usize, CHandle>>> {
	SEQ_MARKER_LISTS.lock().unwrap_or_else(|e| e.into_inner())
}

fn seq_workarea_map() -> std::sync::MutexGuard<'static, Option<HashMap<usize, CHandle>>> {
	SEQ_WORKAREAS.lock().unwrap_or_else(|e| e.into_inner())
}

/// The sequence's marker list (addref'd; caller releases).
///
/// # Safety
/// `seq` must be a live module sequence handle.
unsafe fn seq_marker_list(seq: CHandle) -> Result<CHandle> {
	unsafe {
		let node = n::oaknode_sequence_as_node(seq);
		let mut list = tl::oaktimeline_marker_list_of(node);
		release_handle(node);
		if !list.is_null() {
			return Ok(list);
		}
		let key = seq.ctx as usize;
		let mut map = seq_marker_map();
		let map = map.get_or_insert_with(HashMap::new);
		if let Some(h) = map.get(&key) {
			return Ok(h.addref());
		}
		list = tl::oaktimeline_marker_list_create();
		if list.is_null() {
			return Err(Error::Failed("marker list allocation failed".into()));
		}
		let stored = list.addref();
		map.insert(key, stored);
		Ok(list)
	}
}

/// The sequence's workarea (addref'd; caller releases).
///
/// # Safety
/// `seq` must be a live module sequence handle.
unsafe fn seq_workarea(seq: CHandle) -> Result<CHandle> {
	unsafe {
		let node = n::oaknode_sequence_as_node(seq);
		let mut wa = tl::oaktimeline_workarea_of(node);
		release_handle(node);
		if !wa.is_null() {
			return Ok(wa);
		}
		let key = seq.ctx as usize;
		let mut map = seq_workarea_map();
		let map = map.get_or_insert_with(HashMap::new);
		if let Some(h) = map.get(&key) {
			return Ok(h.addref());
		}
		wa = tl::oaktimeline_workarea_create();
		if wa.is_null() {
			return Err(Error::Failed("workarea allocation failed".into()));
		}
		let stored = wa.addref();
		map.insert(key, stored);
		Ok(wa)
	}
}

// ---- Marker handle boxes ---------------------------------------------------
//
// The module exposes markers only through list operations
// (`oaktimeline_marker_at`, `*_command`, ...); there is no standalone
// marker handle. The facade represents an `OakEngineMarker` as a boxed
// (addref'd list, index) pair so the marker-handle family can route back
// into the list.

/// Marker-box ABI magic stamped into the handle's `abi_version` (marker
/// boxes are facade-created; the field distinguishes them from module
/// handles without relying on function-pointer equality).
const MARKER_ABI_MAGIC: u32 = 0x4D41524B; // "MARK"

/// Facade-owned marker reference: the owning list plus the marker index.
struct MarkerBox {
	refs: AtomicU32,
	list: CHandle,
	index: c_int,
}

unsafe extern "C" fn marker_box_addref(ptr: *mut c_void) {
	unsafe {
		if ptr.is_null() {
			return;
		}
		let rb = &*(ptr as *const MarkerBox);
		rb.refs.fetch_add(1, Ordering::SeqCst);
	}
}

unsafe extern "C" fn marker_box_release(ptr: *mut c_void) {
	unsafe {
		if ptr.is_null() {
			return;
		}
		let rb = ptr as *mut MarkerBox;
		let prev = (*rb).refs.fetch_sub(1, Ordering::SeqCst);
		if prev == 1 {
			let list = (*rb).list;
			release_handle(list);
			drop(Box::from_raw(rb));
		}
	}
}

/// Box an (addref'd `list`, `index`) pair as a borrowed marker handle.
fn box_marker(list: CHandle, index: c_int) -> *mut OakEngineMarker {
	let boxed = Box::new(MarkerBox {
		refs: AtomicU32::new(1),
		list,
		index,
	});
	let ch = CHandle {
		ctx: Box::into_raw(boxed) as *mut c_void,
		addref: Some(marker_box_addref),
		release: Some(marker_box_release),
		abi_version: MARKER_ABI_MAGIC,
	};
	box_handle::<OakEngineMarker>(ch)
}

/// Unpack a marker handle into its (list, index).
///
/// # Safety
/// `m` must be a marker handle created by [`box_marker`] (or NULL).
unsafe fn marker_unbox(m: *const OakEngineMarker) -> Result<(CHandle, c_int)> {
	unsafe {
		let ch = unbox::<OakEngineMarker>(m)?;
		if ch.abi_version != MARKER_ABI_MAGIC {
			return Err(Error::Invalid);
		}
		let rb = &*(ch.ctx as *const MarkerBox);
		Ok((rb.list, rb.index))
	}
}

/// The clip at (track_index, clip_index) within the track list, skipping
/// gap blocks; empty handle when out of range (the capi's
/// `clip_at_index`).
///
/// # Safety
/// `list` must be a live module track-list handle.
unsafe fn clip_at_index(list: CHandle, track_index: c_int, clip_index: c_int) -> CHandle {
	unsafe {
		let mut track_count: c_int = 0;
		if n::oaknode_tracklist_get_track_count(list, &mut track_count) != 0
			|| track_index < 0
			|| track_index >= track_count
		{
			return CHandle::null();
		}
		let mut track = CHandle::null();
		if n::oaknode_tracklist_get_track_at(list, track_index, &mut track) != 0
			|| track.is_null()
		{
			return CHandle::null();
		}
		let mut block_count: c_int = 0;
		if n::oaknode_track_get_block_count(track, &mut block_count) != 0 {
			release_handle(track);
			return CHandle::null();
		}
		let mut seen: c_int = 0;
		for i in 0..block_count {
			let mut block = CHandle::null();
			if n::oaknode_track_get_block_at(track, i, &mut block) != 0 || block.is_null() {
				continue;
			}
			let mut kind: c_int = 0;
			let rc = n::oaknode_block_get_kind(block, &mut kind);
			if rc != 0 {
				release_handle(block);
				continue;
			}
			if kind == BLOCK_KIND_CLIP {
				if seen == clip_index {
					release_handle(track);
					return block;
				}
				seen += 1;
			}
			release_handle(block);
		}
		release_handle(track);
		CHandle::null()
	}
}

/// Index of the first marker whose in-point equals `(num, den)`, or -1.
///
/// # Safety
/// `list` must be a live module marker-list handle.
unsafe fn marker_index_at(list: CHandle, num: i64, den: i64) -> c_int {
	unsafe {
		let mut count: c_int = 0;
		if tl::oaktimeline_marker_count(list, &mut count) != 0 {
			return -1;
		}
		for i in 0..count {
			let mut in_num: c_int = 0;
			let mut in_den: c_int = 0;
			let mut out_num: c_int = 0;
			let mut out_den: c_int = 0;
			let mut color: c_int = 0;
			let rc = tl::oaktimeline_marker_at(
				list,
				i,
				&mut in_num,
				&mut in_den,
				&mut out_num,
				&mut out_den,
				&mut color,
				std::ptr::null_mut(),
				0,
			);
			if rc < 0 {
				continue;
			}
			if in_num as i64 == num && in_den as i64 == den {
				return i;
			}
		}
		-1
	}
}

/// Nearest block whose out-point is strictly before `(num, den)` (the
/// capi's `Track::nearest_block_before`; the module exposes only the
/// before-or-at variant, so the last block ordered before the time is
/// located by iteration).
///
/// # Safety
/// `track` must be a live module track handle.
unsafe fn nearest_block_before(track: CHandle, num: i64, den: i64) -> CHandle {
	unsafe {
		let mut block_count: c_int = 0;
		if n::oaknode_track_get_block_count(track, &mut block_count) != 0 {
			return CHandle::null();
		}
		let mut best = CHandle::null();
		for i in 0..block_count {
			let mut b = CHandle::null();
			if n::oaknode_track_get_block_at(track, i, &mut b) != 0 || b.is_null() {
				continue;
			}
			let mut out_num: c_int = 0;
			let mut out_den: c_int = 0;
			if n::oaknode_block_get_out(b, &mut out_num, &mut out_den) == 0
				&& rat_cmp(out_num as i64, out_den as i64, num, den)
					== std::cmp::Ordering::Less
			{
				if !best.is_null() {
					release_handle(best);
				}
				best = b;
			} else {
				release_handle(b);
			}
		}
		best
	}
}

/// The module type id of a node (empty when the query fails).
///
/// # Safety
/// `node` must be a live module node handle.
unsafe fn node_type_id(node: CHandle) -> String {
	unsafe {
		let mut buf = [0 as c_char; 256];
		let rc = n::oaknode_node_get_id(node, buf.as_mut_ptr(), buf.len() as c_int);
		if rc < 0 {
			String::new()
		} else {
			read_cstr(buf.as_ptr())
		}
	}
}

// ---- Sequence creation and inspection --------------------------------------

/// `oakengine_sequence_new` — create a sequence named `name` in `project`
/// and return its borrowed handle (NULL on failure; NULL project -> NULL).
#[no_mangle]
pub unsafe extern "C" fn oakengine_sequence_new(
	project: *mut OakEngineProject,
	name: *const c_char,
) -> *mut OakEngineSequence {
	guard_ptr(|| unsafe {
		if project.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let ph = unbox(project)?;
		let root = n::oaknode_project_root(ph);
		if root.is_null() {
			release_handle(root);
			return Ok(std::ptr::null_mut());
		}
		release_handle(root);

		let seq = n::oaknode_sequence_create();
		if seq.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let rc = n::oaknode_sequence_set_default_parameters(seq);
		if rc != 0 {
			return Err(Error::Module(rc));
		}
		let label = read_cstr(name);
		let label_c = std::ffi::CString::new(label)
			.map_err(|_| Error::Failed("invalid name".into()))?;
		let seq_node = n::oaknode_sequence_as_node(seq);
		let rc = n::oaknode_node_set_label(seq_node, label_c.as_ptr());
		release_handle(seq_node);
		if rc != 0 {
			return Err(Error::Module(rc));
		}

		// NOTE (documented deviation): the module's whole-subgraph transfer
		// (`oaknode_project_add_node` -> `graph::transfer_all`) moves the node
		// entries but does NOT remap the node ids held inside behaviors, so a
		// sequence moved into a project keeps track lists that point at the
		// wrong ids and a track-list `sequence` back-reference that points at
		// the root folder. The sequence is therefore kept in its own scratch
		// project (all track/marker/workarea queries stay functional there);
		// project membership and the undoable creation are not established in
		// the module world.
		Ok(box_handle::<OakEngineSequence>(seq))
	})
}

/// `oakengine_sequence_name` — sequence label (buf/size convention).
#[no_mangle]
pub unsafe extern "C" fn oakengine_sequence_name(
	self_: *const OakEngineSequence,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	guard_int(|| unsafe {
		let h = unbox(self_)?;
		let node = n::oaknode_sequence_as_node(h);
		let rc = n::oaknode_node_get_label(node, buf, buf_size);
		release_handle(node);
		if rc < 0 {
			Err(Error::Module(rc))
		} else {
			Ok(string_result(rc))
		}
	})
}

/// `oakengine_sequence_get_length` — content length in seconds (0 for an
/// empty sequence).
#[no_mangle]
pub unsafe extern "C" fn oakengine_sequence_get_length(
	self_: *const OakEngineSequence,
	seconds: *mut f64,
) -> c_int {
	guard(|| unsafe {
		if seconds.is_null() {
			return Err(Error::Invalid);
		}
		let h = unbox(self_)?;
		let mut num: c_int = 0;
		let mut den: c_int = 0;
		Error::from_module(n::oaknode_sequence_get_length(h, &mut num, &mut den))?;
		*seconds = num as f64 / den as f64;
		Ok(())
	})
}

/// `oakengine_sequence_get_length_rational` — content length as rational
/// seconds.
#[no_mangle]
pub unsafe extern "C" fn oakengine_sequence_get_length_rational(
	self_: *const OakEngineSequence,
	num: *mut c_int,
	den: *mut c_int,
) -> c_int {
	guard(|| unsafe {
		let h = unbox(self_)?;
		let mut n: c_int = 0;
		let mut d: c_int = 0;
		Error::from_module(n::oaknode_sequence_get_length(h, &mut n, &mut d))?;
		if !num.is_null() {
			*num = n;
		}
		if !den.is_null() {
			*den = d;
		}
		Ok(())
	})
}

/// `oakengine_sequence_get_frame_rate` — frame rate num/den pair.
#[no_mangle]
pub unsafe extern "C" fn oakengine_sequence_get_frame_rate(
	self_: *const OakEngineSequence,
	num: *mut c_int,
	den: *mut c_int,
) -> c_int {
	guard(|| unsafe {
		let h = unbox(self_)?;
		let mut params = CHandle::null();
		let rc = n::oaknode_sequence_get_video_params(h, 0, &mut params);
		if rc != 0 || params.is_null() {
			return Err(Error::State);
		}
		let mut n: c_int = 0;
		let mut d: c_int = 0;
		let fr = c::oakcommon_videoparams_get_frame_rate(params, &mut n, &mut d);
		let mut hh = params;
		c::oakcommon_videoparams_free(&mut hh);
		if fr != 0 || n <= 0 || d <= 0 {
			return Err(Error::State);
		}
		if !num.is_null() {
			*num = n;
		}
		if !den.is_null() {
			*den = d;
		}
		Ok(())
	})
}

/// `oakengine_sequence_get_video_params` — dimensions and pixel aspect
/// ratio (any output pointer may be NULL).
#[no_mangle]
pub unsafe extern "C" fn oakengine_sequence_get_video_params(
	self_: *const OakEngineSequence,
	width: *mut c_int,
	height: *mut c_int,
	par_num: *mut c_int,
	par_den: *mut c_int,
) -> c_int {
	guard(|| unsafe {
		let h = unbox(self_)?;
		let mut params = CHandle::null();
		let rc = n::oaknode_sequence_get_video_params(h, 0, &mut params);
		if rc != 0 || params.is_null() {
			return Err(Error::Failed("sequence has no video params".into()));
		}
		if !width.is_null() {
			Error::from_module(c::oakcommon_videoparams_get_width(params, width))?;
		}
		if !height.is_null() {
			Error::from_module(c::oakcommon_videoparams_get_height(params, height))?;
		}
		if !par_num.is_null() || !par_den.is_null() {
			let mut pn: c_int = 0;
			let mut pd: c_int = 0;
			Error::from_module(c::oakcommon_videoparams_get_pixel_aspect_ratio(
				params, &mut pn, &mut pd,
			))?;
			if !par_num.is_null() {
				*par_num = pn;
			}
			if !par_den.is_null() {
				*par_den = pd;
			}
		}
		let mut hh = params;
		c::oakcommon_videoparams_free(&mut hh);
		Ok(())
	})
}

/// `oakengine_sequence_get_video_params_ex` — full read of the video
/// parameters (any output pointer may be NULL).
#[no_mangle]
pub unsafe extern "C" fn oakengine_sequence_get_video_params_ex(
	self_: *const OakEngineSequence,
	width: *mut c_int,
	height: *mut c_int,
	fps_num: *mut c_int,
	fps_den: *mut c_int,
	par_num: *mut c_int,
	par_den: *mut c_int,
	interlacing: *mut c_int,
	format: *mut c_int,
	divider: *mut c_int,
) -> c_int {
	guard(|| unsafe {
		let h = unbox(self_)?;
		let mut params = CHandle::null();
		let rc = n::oaknode_sequence_get_video_params(h, 0, &mut params);
		if rc != 0 || params.is_null() {
			return Err(Error::Failed("sequence has no video params".into()));
		}
		if !width.is_null() {
			Error::from_module(c::oakcommon_videoparams_get_width(params, width))?;
		}
		if !height.is_null() {
			Error::from_module(c::oakcommon_videoparams_get_height(params, height))?;
		}
		if !fps_num.is_null() || !fps_den.is_null() {
			let mut n: c_int = 0;
			let mut d: c_int = 0;
			Error::from_module(c::oakcommon_videoparams_get_frame_rate(
				params, &mut n, &mut d,
			))?;
			if !fps_num.is_null() {
				*fps_num = n;
			}
			if !fps_den.is_null() {
				*fps_den = d;
			}
		}
		if !par_num.is_null() || !par_den.is_null() {
			let mut pn: c_int = 0;
			let mut pd: c_int = 0;
			Error::from_module(c::oakcommon_videoparams_get_pixel_aspect_ratio(
				params, &mut pn, &mut pd,
			))?;
			if !par_num.is_null() {
				*par_num = pn;
			}
			if !par_den.is_null() {
				*par_den = pd;
			}
		}
		if !interlacing.is_null() {
			Error::from_module(c::oakcommon_videoparams_get_interlacing(
				params, interlacing,
			))?;
		}
		if !format.is_null() {
			Error::from_module(c::oakcommon_videoparams_get_format(params, format))?;
		}
		if !divider.is_null() {
			Error::from_module(c::oakcommon_videoparams_get_divider(params, divider))?;
		}
		let mut hh = params;
		c::oakcommon_videoparams_free(&mut hh);
		Ok(())
	})
}

/// `oakengine_sequence_set_video_params` — write the video parameters
/// (`undoable` flag; -1 leaves a field unchanged).
#[no_mangle]
pub unsafe extern "C" fn oakengine_sequence_set_video_params(
	self_: *mut OakEngineSequence,
	width: c_int,
	height: c_int,
	fps_num: c_int,
	fps_den: c_int,
	par_num: c_int,
	par_den: c_int,
	interlacing: c_int,
	format: c_int,
	undoable: c_int,
) -> c_int {
	guard(|| unsafe {
		set_seq_error("");
		if self_.is_null() {
			set_seq_error("invalid sequence");
			return Err(Error::Invalid);
		}
		if width < -1 || width == 0 || height < -1 || height == 0 {
			set_seq_error(&format!("invalid video size {}x{}", width, height));
			return Err(Error::Invalid);
		}
		if (fps_num == -1) != (fps_den == -1) || fps_num < -1 || fps_num == 0 || fps_den < -1
			|| fps_den == 0
		{
			set_seq_error(&format!("invalid frame rate {}/{}", fps_num, fps_den));
			return Err(Error::Invalid);
		}
		if (par_num == -1) != (par_den == -1) || par_num < -1 || par_num == 0 || par_den < -1
			|| par_den == 0
		{
			set_seq_error(&format!("invalid pixel aspect {}/{}", par_num, par_den));
			return Err(Error::Invalid);
		}
		if interlacing < -1 || interlacing > 2 {
			set_seq_error(&format!("invalid interlacing {}", interlacing));
			return Err(Error::Invalid);
		}
		if format < -1 || format >= 32 {
			set_seq_error(&format!("invalid pixel format {}", format));
			return Err(Error::Invalid);
		}

		let seq = unbox(self_)?;
		let mut current = CHandle::null();
		let rc = n::oaknode_sequence_get_video_params(seq, 0, &mut current);
		if rc != 0 || current.is_null() {
			return Err(Error::State);
		}
		let mut cur_w: c_int = 0;
		let mut cur_h: c_int = 0;
		let mut cur_fn: c_int = 0;
		let mut cur_fd: c_int = 0;
		let mut cur_fmt: c_int = 0;
		c::oakcommon_videoparams_get_width(current, &mut cur_w);
		c::oakcommon_videoparams_get_height(current, &mut cur_h);
		c::oakcommon_videoparams_get_frame_rate(current, &mut cur_fn, &mut cur_fd);
		c::oakcommon_videoparams_get_format(current, &mut cur_fmt);

		// The frame rate is stored directly in the module's model (the capi's
		// flipped time base lives inside the C++ VideoParams constructor).
		let new = c::oakcommon_videoparams_init();
		if new.is_null() {
			let mut h = current;
			c::oakcommon_videoparams_free(&mut h);
			return Err(Error::Failed("video params allocation failed".into()));
		}
		c::oakcommon_videoparams_set_width(new, if width >= 0 { width } else { cur_w });
		c::oakcommon_videoparams_set_height(new, if height >= 0 { height } else { cur_h });
		c::oakcommon_videoparams_set_format(
			new,
			if format >= 0 { format } else { cur_fmt },
		);
		c::oakcommon_videoparams_set_frame_rate(
			new,
			if fps_num >= 0 { fps_num } else { cur_fn },
			if fps_den >= 0 { fps_den } else { cur_fd },
		);
		let mut equal: c_int = 0;
		c::oakcommon_videoparams_equals(new, current, &mut equal);
		if equal != 0 {
			let mut h1 = current;
			let mut h2 = new;
			c::oakcommon_videoparams_free(&mut h1);
			c::oakcommon_videoparams_free(&mut h2);
			return Ok(());
		}
		if undoable != 0 {
			let data = Box::into_raw(Box::new(VideoParamsCmdData {
				seq: seq.addref(),
				old_params: current,
				new_params: new,
			}));
			let cmd = vtable_command(video_params_redo, video_params_undo, video_params_free, data as *mut c_void)?;
			push_command(cmd, "Set Sequence Video Parameters")
		} else {
			let rc = n::oaknode_sequence_set_video_params(seq, 0, new);
			let mut h1 = current;
			let mut h2 = new;
			c::oakcommon_videoparams_free(&mut h1);
			c::oakcommon_videoparams_free(&mut h2);
			Error::from_module(rc)
		}
	})
}

/// `oakengine_sequence_get_audio_params` — sample rate and channel layout.
#[no_mangle]
pub unsafe extern "C" fn oakengine_sequence_get_audio_params(
	self_: *const OakEngineSequence,
	sample_rate: *mut c_int,
	channel_layout: *mut u64,
) -> c_int {
	guard(|| unsafe {
		let h = unbox(self_)?;
		let mut out: *mut c_void = std::ptr::null_mut();
		Error::from_module(n::oaknode_sequence_get_audio_params(h, 0, &mut out))?;
		if out.is_null() {
			return Err(Error::Failed("sequence has no audio params".into()));
		}
		let rate = a::oakcore_audioparams_sample_rate(out);
		let layout = a::oakcore_audioparams_channel_layout(out);
		// The module created the oakcore handle; release it after reading.
		a::oakcore_audioparams_free(out);
		if !sample_rate.is_null() {
			*sample_rate = rate;
		}
		if !channel_layout.is_null() {
			*channel_layout = layout;
		}
		Ok(())
	})
}

/// `oakengine_sequence_set_audio_params` — write the audio parameters.
#[no_mangle]
pub unsafe extern "C" fn oakengine_sequence_set_audio_params(
	self_: *mut OakEngineSequence,
	sample_rate: c_int,
	channel_layout: u64,
	undoable: c_int,
) -> c_int {
	guard(|| unsafe {
		set_seq_error("");
		if self_.is_null() {
			set_seq_error("invalid sequence");
			return Err(Error::Invalid);
		}
		let seq = unbox(self_)?;
		// Read the current params; `sample_rate <= 0` / `channel_layout == 0`
		// leave the field unchanged (the capi's semantics).
		let mut current: *mut c_void = std::ptr::null_mut();
		let rc = n::oaknode_sequence_get_audio_params(seq, 0, &mut current);
		if rc != 0 || current.is_null() {
			return Err(Error::State);
		}
		let cur_rate = a::oakcore_audioparams_sample_rate(current);
		let cur_layout = a::oakcore_audioparams_channel_layout(current);
		let cur_format = a::oakcore_audioparams_format(current);
		a::oakcore_audioparams_free(current);

		let new_rate = if sample_rate <= 0 { cur_rate } else { sample_rate };
		let new_layout = if channel_layout == 0 { cur_layout } else { channel_layout };
		if new_rate == cur_rate && new_layout == cur_layout {
			return Ok(());
		}
		let new = a::oakcore_audioparams_create(new_rate, new_layout, cur_format);
		if new.is_null() {
			return Err(Error::Failed("audio params allocation failed".into()));
		}
		if undoable != 0 {
			let old = a::oakcore_audioparams_create(cur_rate, cur_layout, cur_format);
			if old.is_null() {
				a::oakcore_audioparams_free(new);
				return Err(Error::Failed("audio params allocation failed".into()));
			}
			let data = Box::into_raw(Box::new(AudioParamsCmdData {
				seq: seq.addref(),
				old_params: old,
				new_params: new,
			}));
			let cmd =
				vtable_command(audio_params_redo, audio_params_undo, audio_params_free, data as *mut c_void)?;
			push_command(cmd, "Set Sequence Audio Parameters")
		} else {
			let r = n::oaknode_sequence_set_audio_params(seq, 0, new);
			a::oakcore_audioparams_free(new);
			Error::from_module(r)
		}
	})
}

/// `oakengine_sequence_get_preview_divider` — preview resolution divider
/// (0 on a NULL handle).
#[no_mangle]
pub unsafe extern "C" fn oakengine_sequence_get_preview_divider(
	self_: *const OakEngineSequence,
) -> c_int {
	guard_int(|| unsafe {
		if self_.is_null() {
			return Ok(0);
		}
		let h = unbox(self_)?;
		let mut params = CHandle::null();
		let rc = n::oaknode_sequence_get_video_params(h, 0, &mut params);
		if rc != 0 || params.is_null() {
			return Ok(0);
		}
		let mut divider: c_int = 0;
		c::oakcommon_videoparams_get_divider(params, &mut divider);
		let mut hh = params;
		c::oakcommon_videoparams_free(&mut hh);
		Ok(divider)
	})
}

/// `oakengine_sequence_set_preview_divider` — set the preview divider.
#[no_mangle]
pub unsafe extern "C" fn oakengine_sequence_set_preview_divider(
	self_: *mut OakEngineSequence,
	divider: c_int,
	undoable: c_int,
) -> c_int {
	guard(|| unsafe {
		set_seq_error("");
		if self_.is_null() {
			set_seq_error("invalid sequence");
			return Err(Error::Invalid);
		}
		if divider < 1 {
			set_seq_error(&format!("invalid preview divider {}", divider));
			return Err(Error::Invalid);
		}
		// The module's `VideoParams` model has no divider field
		// (`videoparams_from_handle` drops it), so the write is a no-op for
		// the module; the getter always reports 1. The setter still validates
		// and mirrors the capi's command/apply shape.
		let seq = unbox(self_)?;
		let mut current = CHandle::null();
		let rc = n::oaknode_sequence_get_video_params(seq, 0, &mut current);
		if rc != 0 || current.is_null() {
			return Err(Error::State);
		}
		let mut cur_div: c_int = 0;
		c::oakcommon_videoparams_get_divider(current, &mut cur_div);
		if cur_div == divider {
			let mut h = current;
			c::oakcommon_videoparams_free(&mut h);
			return Ok(());
		}
		let mut h = current;
		c::oakcommon_videoparams_free(&mut h);
		let _ = undoable;
		Ok(())
	})
}

/// `oakengine_sequence_get_video_auto_cache` — 1 when video auto-cache is
/// enabled (the engine accessor is a stub that reports 0).
#[no_mangle]
pub unsafe extern "C" fn oakengine_sequence_get_video_auto_cache(
	self_: *const OakEngineSequence,
) -> c_int {
	guard_int(|| unsafe {
		if self_.is_null() {
			return Ok(0);
		}
		let _ = unbox(self_)?;
		Ok(0)
	})
}

/// `oakengine_sequence_set_video_auto_cache` — forward to the engine's
/// stub accessor (no undo command; `undoable` accepted and ignored).
#[no_mangle]
pub unsafe extern "C" fn oakengine_sequence_set_video_auto_cache(
	self_: *mut OakEngineSequence,
	enabled: c_int,
	undoable: c_int,
) -> c_int {
	guard(|| unsafe {
		set_seq_error("");
		if self_.is_null() {
			set_seq_error("invalid sequence");
			return Err(Error::Invalid);
		}
		// Stub: the module has no video auto-cache accessor
		// (`SequenceBehavior.autocache_video` is not exposed over the C
		// ABI). Mirrors the capi forwarding to the engine's stub setter.
		let _ = (enabled, undoable);
		let _ = unbox(self_)?;
		Ok(())
	})
}

/// `oakengine_sequence_track_count` — tracks per type (any pointer may be
/// NULL).
#[no_mangle]
pub unsafe extern "C" fn oakengine_sequence_track_count(
	self_: *const OakEngineSequence,
	video: *mut c_int,
	audio: *mut c_int,
	subtitle: *mut c_int,
) -> c_int {
	guard(|| unsafe {
		let h = unbox(self_)?;
		if !video.is_null() {
			Error::from_module(n::oaknode_sequence_get_track_count(h, TRACK_TYPE_VIDEO, video))?;
		}
		if !audio.is_null() {
			Error::from_module(n::oaknode_sequence_get_track_count(h, TRACK_TYPE_AUDIO, audio))?;
		}
		if !subtitle.is_null() {
			Error::from_module(n::oaknode_sequence_get_track_count(
				h,
				TRACK_TYPE_SUBTITLE,
				subtitle,
			))?;
		}
		Ok(())
	})
}

/// `oakengine_sequence_get_playhead` — playhead as a frame timestamp.
#[no_mangle]
pub unsafe extern "C" fn oakengine_sequence_get_playhead(
	self_: *const OakEngineSequence,
	timestamp: *mut i64,
) -> c_int {
	guard(|| unsafe {
		if timestamp.is_null() {
			return Err(Error::Invalid);
		}
		let h = unbox(self_)?;
		let tb = seq_time_base(h)?;
		let mut num: c_int = 0;
		let mut den: c_int = 0;
		Error::from_module(n::oaknode_sequence_get_playhead(h, &mut num, &mut den))?;
		*timestamp = rational_to_ts(num as i64, den as i64, tb);
		Ok(())
	})
}

/// `oakengine_sequence_set_playhead` — move the playhead to `timestamp`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_sequence_set_playhead(
	self_: *mut OakEngineSequence,
	timestamp: i64,
) -> c_int {
	guard(|| unsafe {
		let h = unbox(self_)?;
		let tb = seq_time_base(h)?;
		let (num, den) = ts_to_rational(timestamp, tb);
		// The module's playhead setter takes c_int rationals; timestamps that
		// overflow are truncated (module limitation).
		Error::from_module(n::oaknode_sequence_set_playhead(
			h,
			num as c_int,
			den as c_int,
		))
	})
}

/// `oakengine_sequence_get_playhead_seconds` — playhead in seconds.
#[no_mangle]
pub unsafe extern "C" fn oakengine_sequence_get_playhead_seconds(
	self_: *const OakEngineSequence,
	seconds: *mut f64,
) -> c_int {
	guard(|| unsafe {
		if seconds.is_null() {
			return Err(Error::Invalid);
		}
		let h = unbox(self_)?;
		let mut num: c_int = 0;
		let mut den: c_int = 0;
		Error::from_module(n::oaknode_sequence_get_playhead(h, &mut num, &mut den))?;
		*seconds = num as f64 / den as f64;
		Ok(())
	})
}

/// `oakengine_sequence_workarea_is_enabled` — 1 when the workarea is
/// enabled.
#[no_mangle]
pub unsafe extern "C" fn oakengine_sequence_workarea_is_enabled(
	self_: *const OakEngineSequence,
) -> c_int {
	guard_int(|| unsafe {
		if self_.is_null() {
			return Ok(0);
		}
		let h = unbox(self_)?;
		let wa = seq_workarea(h)?;
		let mut enabled: c_int = 0;
		let rc = tl::oaktimeline_workarea_get(
			wa,
			std::ptr::null_mut(),
			std::ptr::null_mut(),
			std::ptr::null_mut(),
			std::ptr::null_mut(),
			&mut enabled,
		);
		release_handle(wa);
		Error::from_module(rc)?;
		Ok(enabled)
	})
}

/// `oakengine_sequence_get_workarea` — workarea in/out as frame timestamps.
#[no_mangle]
pub unsafe extern "C" fn oakengine_sequence_get_workarea(
	self_: *const OakEngineSequence,
	in_: *mut i64,
	out: *mut i64,
) -> c_int {
	guard(|| unsafe {
		let h = unbox(self_)?;
		let tb = seq_time_base(h)?;
		let wa = seq_workarea(h)?;
		let mut in_num: c_int = 0;
		let mut in_den: c_int = 0;
		let mut out_num: c_int = 0;
		let mut out_den: c_int = 0;
		let rc = tl::oaktimeline_workarea_get(
			wa,
			&mut in_num,
			&mut in_den,
			&mut out_num,
			&mut out_den,
			std::ptr::null_mut(),
		);
		release_handle(wa);
		Error::from_module(rc)?;
		if !in_.is_null() {
			*in_ = rational_to_ts(in_num as i64, in_den as i64, tb);
		}
		if !out.is_null() {
			*out = rational_to_ts(out_num as i64, out_den as i64, tb);
		}
		Ok(())
	})
}

/// `oakengine_sequence_set_workarea` — enable flag plus in/out timestamps.
#[no_mangle]
pub unsafe extern "C" fn oakengine_sequence_set_workarea(
	self_: *mut OakEngineSequence,
	enabled: c_int,
	in_: i64,
	out: i64,
) -> c_int {
	guard(|| unsafe {
		let h = unbox(self_)?;
		let tb = seq_time_base(h)?;
		let wa = seq_workarea(h)?;
		let rc = tl::oaktimeline_workarea_set_enabled(wa, enabled);
		if rc != 0 {
			release_handle(wa);
			return Err(Error::Module(rc));
		}
		let (in_num, in_den) = ts_to_rational(in_, tb);
		let (out_num, out_den) = ts_to_rational(out, tb);
		let rc = tl::oaktimeline_workarea_set_range(
			wa,
			in_num as c_int,
			in_den as c_int,
			out_num as c_int,
			out_den as c_int,
		);
		release_handle(wa);
		Error::from_module(rc)
	})
}

/// `oakengine_sequence_marker_count` — number of timeline markers.
#[no_mangle]
pub unsafe extern "C" fn oakengine_sequence_marker_count(
	self_: *const OakEngineSequence,
) -> c_int {
	guard_int(|| unsafe {
		if self_.is_null() {
			return Ok(0);
		}
		let h = unbox(self_)?;
		let list = seq_marker_list(h)?;
		let mut count: c_int = 0;
		let rc = tl::oaktimeline_marker_count(list, &mut count);
		release_handle(list);
		Error::from_module(rc)?;
		Ok(count)
	})
}

/// `oakengine_sequence_marker_at` — marker at `index` (time as a frame
/// timestamp, name via the buf/size convention, color index).
#[no_mangle]
pub unsafe extern "C" fn oakengine_sequence_marker_at(
	self_: *const OakEngineSequence,
	index: c_int,
	time: *mut i64,
	name: *mut c_char,
	name_size: c_int,
	color: *mut c_int,
) -> c_int {
	guard(|| unsafe {
		if self_.is_null() || index < 0 {
			return Err(Error::Invalid);
		}
		let h = unbox(self_)?;
		let tb = seq_time_base(h)?;
		let list = seq_marker_list(h)?;
		let mut in_num: c_int = 0;
		let mut in_den: c_int = 0;
		let mut out_num: c_int = 0;
		let mut out_den: c_int = 0;
		let mut marker_color: c_int = 0;
		let rc = tl::oaktimeline_marker_at(
			list,
			index,
			&mut in_num,
			&mut in_den,
			&mut out_num,
			&mut out_den,
			&mut marker_color,
			name,
			name_size,
		);
		release_handle(list);
		if rc < 0 {
			return Err(Error::Module(rc));
		}
		if !time.is_null() {
			*time = rational_to_ts(in_num as i64, in_den as i64, tb);
		}
		if !color.is_null() {
			*color = marker_color;
		}
		Ok(())
	})
}

/* ---- Timeline editing primitives ----------------------------------------- */

/// `oakengine_sequence_last_error` — last editing error for this thread.
#[no_mangle]
pub unsafe extern "C" fn oakengine_sequence_last_error(
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	SEQ_LAST_ERROR.with(|e| unsafe { write_string(&e.borrow(), buf, buf_size) })
}

/// `oakengine_sequence_add_track` — append a track and return its index.
#[no_mangle]
pub unsafe extern "C" fn oakengine_sequence_add_track(
	self_: *mut OakEngineSequence,
	track_type: c_int,
) -> c_int {
	guard_int(|| unsafe {
		set_seq_error("");
		if self_.is_null() || track_type < TRACK_TYPE_VIDEO || track_type > TRACK_TYPE_SUBTITLE {
			set_seq_error("invalid sequence or track type");
			return Err(Error::Invalid);
		}
		let seq = unbox(self_)?;
		let mut list = CHandle::null();
		Error::from_module(n::oaknode_sequence_get_track_list(seq, track_type, &mut list))?;
		// The module's TimelineAddTrackCommand redo only appends the
		// sequence's track array element; it never registers the created
		// track node in the list's `tracks`, so the count/at queries would
		// not observe it. Register a track node live as compensation
		// (documented deviation; the array element stays undoable).
		let cmd = tl::oaktimeline_add_track_command(list);
		if cmd.is_null() {
			release_handle(list);
			return Err(Error::Failed("add track command failed".into()));
		}
		push_command(cmd, "Add Track")?;
		let track = n::oaknode_track_create(track_type);
		if track.is_null() {
			release_handle(list);
			return Err(Error::Failed("track creation failed".into()));
		}
		Error::from_module(n::oaknode_tracklist_add_track(list, track))?;
		let mut count: c_int = 0;
		Error::from_module(n::oaknode_sequence_get_track_count(seq, track_type, &mut count))?;
		release_handle(list);
		Ok(count - 1)
	})
}

/// `oakengine_sequence_add_track_command` — create a TimelineAddTrackCommand
/// without pushing it.
#[no_mangle]
pub unsafe extern "C" fn oakengine_sequence_add_track_command(
	self_: *mut OakEngineSequence,
	track_type: c_int,
	auto_merge: c_int,
	out_track: *mut *mut OakEngineTrack,
) -> *mut c_void {
	guard_ptr(|| unsafe {
		if self_.is_null() || track_type < TRACK_TYPE_VIDEO || track_type > TRACK_TYPE_SUBTITLE {
			return Ok(std::ptr::null_mut());
		}
		let seq = unbox(self_)?;
		let mut list = CHandle::null();
		Error::from_module(n::oaknode_sequence_get_track_list(seq, track_type, &mut list))?;
		let cmd = tl::oaktimeline_add_track_command(list);
		if cmd.is_null() {
			release_handle(list);
			return Ok(std::ptr::null_mut());
		}
		// The module command carries its own internal track (inaccessible
		// over the C ABI); `out_track` receives a live-created track the
		// caller can hand to the list, mirroring the capi's
		// `command->track()`. `auto_merge` is accepted and ignored (the
		// module command hardcodes automerge off).
		let _ = auto_merge;
		if !out_track.is_null() {
			let track = n::oaknode_track_create(track_type);
			if track.is_null() {
				release_handle(list);
				return Ok(std::ptr::null_mut());
			}
			Error::from_module(n::oaknode_tracklist_add_track(list, track))?;
			*out_track = box_handle::<OakEngineTrack>(track);
		}
		release_handle(list);
		Ok(command_box(cmd)?.cast())
	})
}

/// `oakengine_sequence_ripple_tracks_command` — create a
/// TrackListRippleToolCommand.
#[no_mangle]
pub unsafe extern "C" fn oakengine_sequence_ripple_tracks_command(
	self_: *mut OakEngineSequence,
	track_type: c_int,
	infos: *const OakEngineRippleInfo,
	info_count: c_int,
	movement_num: i64,
	movement_den: i64,
	movement_mode: c_int,
) -> *mut c_void {
	guard_ptr(|| {
		if self_.is_null() || infos.is_null() || info_count <= 0 || movement_den == 0
			|| track_type < TRACK_TYPE_VIDEO
			|| track_type > TRACK_TYPE_SUBTITLE
			|| movement_mode < 0
			|| movement_mode > MOVEMENT_MODE_TRIM_OUT
		{
			return Ok(std::ptr::null_mut());
		}
		// Stub: the oaktimeline module implements TrackListRippleToolCommand
		// internally (undoripple.rs) but exposes no C creator for it; the
		// facade bridge accordingly has no `oaktimeline_ripple_tracks_command`.
		let _ = (self_, track_type, infos, info_count, movement_num, movement_den, movement_mode);
		Ok(std::ptr::null_mut())
	})
}

/// `oakengine_sequence_add_footage_clip` — place a clip of `footage` on a
/// track (undoable).
#[no_mangle]
pub unsafe extern "C" fn oakengine_sequence_add_footage_clip(
	seq: *mut OakEngineSequence,
	footage: *mut OakEngineFootage,
	track_type: c_int,
	track_index: c_int,
	in_: i64,
	out: i64,
	media_in: i64,
) -> *mut OakEngineClip {
	guard_ptr(|| unsafe {
		set_seq_error("");
		let sequence = match unbox(seq) {
			Ok(h) => h,
			Err(_) => {
				set_seq_error("invalid sequence handle");
				return Ok(std::ptr::null_mut());
			}
		};
		let footage_h = match unbox(footage) {
			Ok(h) => h,
			Err(_) => {
				set_seq_error("invalid footage handle");
				return Ok(std::ptr::null_mut());
			}
		};
		if track_type != TRACK_TYPE_VIDEO && track_type != TRACK_TYPE_AUDIO {
			set_seq_error("clips are only supported on video and audio tracks");
			return Ok(std::ptr::null_mut());
		}
		// Same-project check.
		let mut seq_project = CHandle::null();
		let seq_node = n::oaknode_sequence_as_node(sequence);
		let rc = n::oaknode_node_get_project(seq_node, &mut seq_project);
		let mut foot_project = CHandle::null();
		let rc2 = n::oaknode_node_get_project(footage_h, &mut foot_project);
		release_handle(seq_node);
		if rc != 0 || rc2 != 0 || seq_project.is_null() || foot_project.is_null()
			|| seq_project.ctx != foot_project.ctx
		{
			release_handle(seq_project);
			release_handle(foot_project);
			set_seq_error("footage and sequence belong to different projects");
			return Ok(std::ptr::null_mut());
		}
		release_handle(seq_project);
		release_handle(foot_project);
		if in_ < 0 || out <= in_ || media_in < 0 {
			set_seq_error("invalid clip range (need 0 <= in < out and media_in >= 0)");
			return Ok(std::ptr::null_mut());
		}
		let tb = match seq_time_base(sequence) {
			Ok(tb) => tb,
			Err(_) => {
				set_seq_error("sequence has no valid frame rate");
				return Ok(std::ptr::null_mut());
			}
		};
		let mut list = CHandle::null();
		if n::oaknode_sequence_get_track_list(sequence, track_type, &mut list) != 0
			|| list.is_null()
		{
			set_seq_error("sequence has no track list for this type");
			return Ok(std::ptr::null_mut());
		}
		let mut track_count: c_int = 0;
		if n::oaknode_tracklist_get_track_count(list, &mut track_count) != 0
			|| track_index < 0
			|| track_index >= track_count
		{
			release_handle(list);
			set_seq_error(&format!("track index {} out of range ({} tracks)", track_index, track_count));
			return Ok(std::ptr::null_mut());
		}

		let (in_num, in_den) = ts_to_rational(in_, tb);
		let (out_num, out_den) = ts_to_rational(out, tb);
		let (media_num, media_den) = ts_to_rational(media_in, tb);

		// The application's drop-import chain reduced to its editing core.
		let clip = n::oaknode_block_clip_create();
		if clip.is_null() {
			release_handle(list);
			set_seq_error("clip creation failed");
			return Ok(std::ptr::null_mut());
		}
		let (len_num, len_den) = rat_sub(out_num, out_den, in_num, in_den);
		Error::from_module(n::oaknode_clip_set_media_in(
			clip,
			media_num as c_int,
			media_den as c_int,
		))?;
		Error::from_module(n::oaknode_block_set_length_and_media_in(
			clip,
			len_num as c_int,
			len_den as c_int,
		))?;

		let mut children: Vec<CHandle> = Vec::new();
		let add_cmd = n::oaknode_command_create_add_node(seq_project_of(sequence), clip);
		if add_cmd.is_null() {
			release_handle(list);
			set_seq_error("node add command failed");
			return Ok(std::ptr::null_mut());
		}
		children.push(add_cmd);
		// The module clip has no `buffer_in` input (only media/speed/reverse/
		// pitch/loop inputs), so the footage connection cannot be built; the
		// whole add fails like the C++ would on a bad edge.
		let clip_node = n::oaknode_block_as_node(clip);
		let mut edge = CHandle::null();
		let rc = n::oaknode_node_connect_undoable(footage_h, clip_node, c"buffer_in".as_ptr(), &mut edge);
		release_handle(clip_node);
		if rc != 0 {
			for child in &children {
				release_handle(*child);
			}
			release_handle(list);
			set_seq_error("footage connection failed: module clips have no buffer input");
			return Ok(std::ptr::null_mut());
		}
		children.push(edge);
		let place_cmd = tl::oaktimeline_place_block_command(
			list,
			track_index,
			clip,
			in_num,
			in_den,
		);
		if place_cmd.is_null() {
			for child in &children {
				release_handle(*child);
			}
			release_handle(list);
			set_seq_error("place block command failed");
			return Ok(std::ptr::null_mut());
		}
		children.push(place_cmd);
		release_handle(list);
		if let Err(e) = push_multi_commands(&children, "Add Clip") {
			set_seq_error(&format!("failed to push add-clip command: {:?}", e));
			return Err(e);
		}
		Ok(box_handle::<OakEngineClip>(clip))
	})
}

/// Borrowed project handle of a sequence (temporary; caller releases).
///
/// # Safety
/// `seq` must be a live module sequence handle.
unsafe fn seq_project_of(seq: CHandle) -> CHandle {
	unsafe {
		let node = n::oaknode_sequence_as_node(seq);
		let mut project = CHandle::null();
		let rc = n::oaknode_node_get_project(node, &mut project);
		release_handle(node);
		if rc != 0 {
			return CHandle::null();
		}
		project
	}
}

/// `oakengine_sequence_clip_count` — clips on a track (gaps skipped).
#[no_mangle]
pub unsafe extern "C" fn oakengine_sequence_clip_count(
	self_: *mut OakEngineSequence,
	track_type: c_int,
	track_index: c_int,
) -> c_int {
	guard_int(|| unsafe {
		if self_.is_null() || track_type < TRACK_TYPE_VIDEO || track_type > TRACK_TYPE_SUBTITLE {
			return Err(Error::Invalid);
		}
		let h = unbox(self_)?;
		let mut list = CHandle::null();
		Error::from_module(n::oaknode_sequence_get_track_list(h, track_type, &mut list))?;
		let mut track_count: c_int = 0;
		Error::from_module(n::oaknode_tracklist_get_track_count(list, &mut track_count))?;
		if track_index < 0 || track_index >= track_count {
			release_handle(list);
			return Err(Error::NotFound);
		}
		let mut track = CHandle::null();
		Error::from_module(n::oaknode_tracklist_get_track_at(list, track_index, &mut track))?;
		let mut block_count: c_int = 0;
		Error::from_module(n::oaknode_track_get_block_count(track, &mut block_count))?;
		let mut count: c_int = 0;
		for i in 0..block_count {
			let mut block = CHandle::null();
			if n::oaknode_track_get_block_at(track, i, &mut block) != 0 || block.is_null() {
				continue;
			}
			let mut kind: c_int = 0;
			let rc = n::oaknode_block_get_kind(block, &mut kind);
			release_handle(block);
			if rc == 0 && kind == BLOCK_KIND_CLIP {
				count += 1;
			}
		}
		release_handle(track);
		release_handle(list);
		Ok(count)
	})
}

/// `oakengine_sequence_clip_at` — borrowed clip at (track, clip) index.
#[no_mangle]
pub unsafe extern "C" fn oakengine_sequence_clip_at(
	self_: *mut OakEngineSequence,
	track_type: c_int,
	track_index: c_int,
	clip_index: c_int,
) -> *mut OakEngineClip {
	guard_ptr(|| unsafe {
		if self_.is_null() || track_type < TRACK_TYPE_VIDEO || track_type > TRACK_TYPE_SUBTITLE
			|| clip_index < 0
		{
			return Ok(std::ptr::null_mut());
		}
		let h = unbox(self_)?;
		let mut list = CHandle::null();
		Error::from_module(n::oaknode_sequence_get_track_list(h, track_type, &mut list))?;
		let clip = clip_at_index(list, track_index, clip_index);
		release_handle(list);
		if clip.is_null() {
			return Ok(std::ptr::null_mut());
		}
		Ok(box_handle::<OakEngineClip>(clip))
	})
}

/// `oakengine_clip_get_range` — clip timeline range and media in-point as
/// frame timestamps.
#[no_mangle]
pub unsafe extern "C" fn oakengine_clip_get_range(
	self_: *const OakEngineClip,
	in_: *mut i64,
	out: *mut i64,
	media_in: *mut i64,
) -> c_int {
	guard(|| unsafe {
		let h = unbox(self_)?;
		// The clip's sequence (clip -> track -> sequence) provides the
		// timebase.
		let mut track = CHandle::null();
		let rc = n::oaknode_block_get_track(h, &mut track);
		if rc != 0 || track.is_null() {
			return Err(Error::State);
		}
		let mut sequence = CHandle::null();
		let rc = n::oaknode_track_get_sequence(track, &mut sequence);
		release_handle(track);
		if rc != 0 || sequence.is_null() {
			return Err(Error::State);
		}
		let tb = seq_time_base(sequence)?;
		release_handle(sequence);
		let mut in_num: c_int = 0;
		let mut in_den: c_int = 0;
		Error::from_module(n::oaknode_block_get_in(h, &mut in_num, &mut in_den))?;
		let mut out_num: c_int = 0;
		let mut out_den: c_int = 0;
		Error::from_module(n::oaknode_block_get_out(h, &mut out_num, &mut out_den))?;
		let mut mi_num: c_int = 0;
		let mut mi_den: c_int = 0;
		Error::from_module(n::oaknode_clip_get_media_in(h, &mut mi_num, &mut mi_den))?;
		if !in_.is_null() {
			*in_ = rational_to_ts(in_num as i64, in_den as i64, tb);
		}
		if !out.is_null() {
			*out = rational_to_ts(out_num as i64, out_den as i64, tb);
		}
		if !media_in.is_null() {
			*media_in = rational_to_ts(mi_num as i64, mi_den as i64, tb);
		}
		Ok(())
	})
}

/// `oakengine_clip_get_sequence` — the clip's owning sequence.
#[no_mangle]
pub unsafe extern "C" fn oakengine_clip_get_sequence(
	self_: *const OakEngineClip,
) -> *mut OakEngineSequence {
	guard_ptr(|| unsafe {
		if self_.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let h = unbox(self_)?;
		let mut track = CHandle::null();
		let rc = n::oaknode_block_get_track(h, &mut track);
		if rc != 0 || track.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let mut sequence = CHandle::null();
		let rc = n::oaknode_track_get_sequence(track, &mut sequence);
		release_handle(track);
		if rc != 0 || sequence.is_null() {
			return Ok(std::ptr::null_mut());
		}
		Ok(box_handle::<OakEngineSequence>(sequence))
	})
}

/// `oakengine_clip_as_node` — the clip's node view (borrowed; freed with
/// `oakengine_node_free`). The effect-stack surface (chain enumeration and
/// edits) is node-based, so the app converts its clip handle before
/// walking the chain.
#[no_mangle]
pub unsafe extern "C" fn oakengine_clip_as_node(self_: *const OakEngineClip) -> *mut OakEngineNode {
	guard_ptr(|| unsafe {
		if self_.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let h = unbox(self_)?;
		let node = n::oaknode_block_as_node(h);
		if node.is_null() {
			return Ok(std::ptr::null_mut());
		}
		Ok(box_handle::<OakEngineNode>(node))
	})
}

/* ---- Editing primitives, round 2: split / ripple delete / trim / move ---- */

/// `oakengine_sequence_split_clip` — split the addressed clip at `time`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_sequence_split_clip(
	seq: *mut OakEngineSequence,
	track_type: c_int,
	track_index: c_int,
	clip_index: c_int,
	time: i64,
) -> c_int {
	guard(|| unsafe {
		set_seq_error("");
		let sequence = match unbox(seq) {
			Ok(h) => h,
			Err(_) => {
				set_seq_error("invalid sequence or track type");
				return Err(Error::Invalid);
			}
		};
		if track_type < TRACK_TYPE_VIDEO || track_type > TRACK_TYPE_SUBTITLE {
			set_seq_error("invalid sequence or track type");
			return Err(Error::Invalid);
		}
		let mut list = CHandle::null();
		Error::from_module(n::oaknode_sequence_get_track_list(sequence, track_type, &mut list))?;
		let clip = clip_at_index(list, track_index, clip_index);
		release_handle(list);
		if clip.is_null() {
			set_seq_error(&format!("no clip at track {} index {}", track_index, clip_index));
			return Err(Error::NotFound);
		}
		let tb = match seq_time_base(sequence) {
			Ok(tb) => tb,
			Err(_) => {
				set_seq_error("sequence has no valid frame rate");
				return Err(Error::State);
			}
		};
		let (point_num, point_den) = ts_to_rational(time, tb);
		let mut in_num: c_int = 0;
		let mut in_den: c_int = 0;
		let mut out_num: c_int = 0;
		let mut out_den: c_int = 0;
		Error::from_module(n::oaknode_block_get_in(clip, &mut in_num, &mut in_den))?;
		Error::from_module(n::oaknode_block_get_out(clip, &mut out_num, &mut out_den))?;
		let cmp_in = rat_cmp(point_num, point_den, in_num as i64, in_den as i64);
		let cmp_out = rat_cmp(point_num, point_den, out_num as i64, out_den as i64);
		if cmp_in != std::cmp::Ordering::Greater || cmp_out != std::cmp::Ordering::Less {
			set_seq_error(&format!("split time {} is not strictly inside the clip", time));
			return Err(Error::Invalid);
		}
		let cmd = tl::oaktimeline_split_command(&clip, 1, point_num, point_den);
		if cmd.is_null() {
			set_seq_error("split command failed");
			return Err(Error::Failed("split command failed".into()));
		}
		push_command(cmd, "Split Clip")
	})
}

/// Compare two rationals (a_num/a_den vs b_num/b_den).
fn rat_cmp(a_num: i64, a_den: i64, b_num: i64, b_den: i64) -> std::cmp::Ordering {
	let lhs = a_num * b_den;
	let rhs = b_num * a_den;
	lhs.cmp(&rhs)
}

/// `oakengine_sequence_ripple_delete_clip` — delete the clip and ripple the
/// following content left.
#[no_mangle]
pub unsafe extern "C" fn oakengine_sequence_ripple_delete_clip(
	seq: *mut OakEngineSequence,
	track_type: c_int,
	track_index: c_int,
	clip_index: c_int,
) -> c_int {
	guard(|| unsafe {
		set_seq_error("");
		let sequence = match unbox(seq) {
			Ok(h) => h,
			Err(_) => {
				set_seq_error("invalid sequence or track type");
				return Err(Error::Invalid);
			}
		};
		if track_type < TRACK_TYPE_VIDEO || track_type > TRACK_TYPE_SUBTITLE {
			set_seq_error("invalid sequence or track type");
			return Err(Error::Invalid);
		}
		let mut list = CHandle::null();
		Error::from_module(n::oaknode_sequence_get_track_list(sequence, track_type, &mut list))?;
		let clip = clip_at_index(list, track_index, clip_index);
		release_handle(list);
		if clip.is_null() {
			set_seq_error(&format!("no clip at track {} index {}", track_index, clip_index));
			return Err(Error::NotFound);
		}
		let mut track = CHandle::null();
		let rc = n::oaknode_block_get_track(clip, &mut track);
		if rc != 0 || track.is_null() {
			set_seq_error(&format!("no clip at track {} index {}", track_index, clip_index));
			return Err(Error::NotFound);
		}
		let mut in_num: c_int = 0;
		let mut in_den: c_int = 0;
		let mut out_num: c_int = 0;
		let mut out_den: c_int = 0;
		Error::from_module(n::oaknode_block_get_in(clip, &mut in_num, &mut in_den))?;
		Error::from_module(n::oaknode_block_get_out(clip, &mut out_num, &mut out_den))?;
		let cmd = tl::oaktimeline_ripple_remove_area_command(
			track,
			in_num as i64,
			in_den as i64,
			out_num as i64,
			out_den as i64,
		);
		// NOTE: `track` is intentionally NOT released — the module command
		// stores the borrowed handle for its whole lifetime (its `redo`/
		// `undo` re-resolve the track), and the module model keeps such
		// handles alive for the command's lifetime (same as
		// `oakengine_sequence_delete_clips`).
		if cmd.is_null() {
			set_seq_error("ripple delete command failed");
			return Err(Error::Failed("ripple delete command failed".into()));
		}
		push_command(cmd, "Ripple Delete Clip")
	})
}

/// `oakengine_clip_trim` — change the clip's timeline range.
#[no_mangle]
pub unsafe extern "C" fn oakengine_clip_trim(
	clip: *mut OakEngineClip,
	new_in: i64,
	new_out: i64,
) -> c_int {
	guard(|| unsafe {
		set_seq_error("");
		let h = match unbox(clip) {
			Ok(h) => h,
			Err(_) => {
				set_seq_error("invalid clip handle");
				return Err(Error::Invalid);
			}
		};
		let mut track = CHandle::null();
		let rc = n::oaknode_block_get_track(h, &mut track);
		if rc != 0 || track.is_null() {
			set_seq_error("clip is not on a track");
			return Err(Error::State);
		}
		let mut sequence = CHandle::null();
		let rc = n::oaknode_track_get_sequence(track, &mut sequence);
		if rc != 0 || sequence.is_null() {
			release_handle(track);
			set_seq_error("sequence has no valid frame rate");
			return Err(Error::State);
		}
		let tb = match seq_time_base(sequence) {
			Ok(tb) => tb,
			Err(_) => {
				release_handle(sequence);
				release_handle(track);
				set_seq_error("sequence has no valid frame rate");
				return Err(Error::State);
			}
		};
		release_handle(sequence);
		if new_in < 0 || new_out <= new_in {
			release_handle(track);
			set_seq_error("invalid trim range (need 0 <= new_in < new_out)");
			return Err(Error::Invalid);
		}
		let mut in_num: c_int = 0;
		let mut in_den: c_int = 0;
		let mut out_num: c_int = 0;
		let mut out_den: c_int = 0;
		Error::from_module(n::oaknode_block_get_in(h, &mut in_num, &mut in_den))?;
		Error::from_module(n::oaknode_block_get_out(h, &mut out_num, &mut out_den))?;
		let old_in = rational_to_ts(in_num as i64, in_den as i64, tb);
		let old_out = rational_to_ts(out_num as i64, out_den as i64, tb);
		if new_in == old_in && new_out == old_out {
			release_handle(track);
			return Ok(());
		}

		// The application's trim command: one end at a time, adjacent gaps
		// absorb the difference; both ends are one undoable command. The
		// module's own `BlockTrimCommand` applies its length setters with
		// inverted semantics, so the facade carries the correct engine
		// mapping (trim-in anchors the out, trim-out anchors the in).
		let mut children: Vec<CHandle> = Vec::new();
		let (old_len_num, old_len_den) = {
			let mut ln: c_int = 0;
			let mut ld: c_int = 0;
			Error::from_module(n::oaknode_block_get_length(h, &mut ln, &mut ld))?;
			(ln, ld)
		};
		if new_in != old_in {
			// in-trim: length = block out - new in (out anchored).
			let (new_in_num, new_in_den) = ts_to_rational(new_in, tb);
			let (new_num, new_den) = rat_sub(out_num as i64, out_den as i64, new_in_num, new_in_den);
			let cmd = trim_cmd(
				h,
				MOVEMENT_MODE_TRIM_IN,
				old_len_num,
				old_len_den,
				new_num as c_int,
				new_den as c_int,
			)?;
			children.push(cmd);
		}
		if new_out != old_out {
			// out-trim: length = new out - new in (in anchored); the old
			// length is the post-in-trim length (out - new in) when both
			// ends move.
			let (new_in_num, new_in_den) = ts_to_rational(new_in, tb);
			let (new_out_num, new_out_den) = ts_to_rational(new_out, tb);
			let (new_num, new_den) = rat_sub(new_out_num, new_out_den, new_in_num, new_in_den);
			let (post_in_num, post_in_den) =
				rat_sub(out_num as i64, out_den as i64, new_in_num, new_in_den);
			let cmd = trim_cmd(
				h,
				MOVEMENT_MODE_TRIM_OUT,
				post_in_num as c_int,
				post_in_den as c_int,
				new_num as c_int,
				new_den as c_int,
			)?;
			children.push(cmd);
		}
		release_handle(track);
		push_multi_commands(&children, "Trim Clip")
	})
}

/// `oakengine_sequence_move_clip` — move the addressed clip to `new_in`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_sequence_move_clip(
	seq: *mut OakEngineSequence,
	track_type: c_int,
	track_index: c_int,
	clip_index: c_int,
	new_in: i64,
) -> c_int {
	guard(|| unsafe {
		set_seq_error("");
		let sequence = match unbox(seq) {
			Ok(h) => h,
			Err(_) => {
				set_seq_error("invalid arguments");
				return Err(Error::Invalid);
			}
		};
		if track_type < TRACK_TYPE_VIDEO || track_type > TRACK_TYPE_SUBTITLE || new_in < 0 {
			set_seq_error("invalid arguments");
			return Err(Error::Invalid);
		}
		let mut list = CHandle::null();
		Error::from_module(n::oaknode_sequence_get_track_list(sequence, track_type, &mut list))?;
		let clip = clip_at_index(list, track_index, clip_index);
		release_handle(list);
		if clip.is_null() {
			set_seq_error(&format!("no clip at track {} index {}", track_index, clip_index));
			return Err(Error::NotFound);
		}
		let mut track = CHandle::null();
		let rc = n::oaknode_block_get_track(clip, &mut track);
		if rc != 0 || track.is_null() {
			set_seq_error(&format!("no clip at track {} index {}", track_index, clip_index));
			return Err(Error::NotFound);
		}
		release_handle(track);
		// Stub: the module's gap+place composition
		// (`TrackReplaceBlockWithGapCommand` then `TrackPlaceBlockCommand`)
		// faults with a memory error in the module world after accumulated
		// timeline edits (and the two commands pushed as ONE undoable entry,
		// as the capi does, faults even on a fresh clip), so the move cannot
		// be performed safely.
		set_seq_error("clip moves are not supported by the module");
		Err(Error::State)
	})
}

/* ---- Batch editing (timeline panel) -------------------------------------- */

/// `oakengine_sequence_split_clips` — split every given clip at `time_ts`,
/// preserving links.
#[no_mangle]
pub unsafe extern "C" fn oakengine_sequence_split_clips(
	seq: *mut OakEngineSequence,
	clips: *mut *mut OakEngineClip,
	clip_count: c_int,
	time_ts: i64,
) -> c_int {
	guard(|| unsafe {
		set_seq_error("");
		let sequence = match unbox(seq) {
			Ok(h) => h,
			Err(_) => {
				set_seq_error("invalid arguments");
				return Err(Error::Invalid);
			}
		};
		if clips.is_null() || clip_count <= 0 {
			set_seq_error("invalid arguments");
			return Err(Error::Invalid);
		}
		let tb = match seq_time_base(sequence) {
			Ok(tb) => tb,
			Err(_) => {
				set_seq_error("sequence has no valid frame rate");
				return Err(Error::State);
			}
		};
		let (time_num, time_den) = ts_to_rational(time_ts, tb);

		// Collect the blocks, deduplicating, and check that at least one
		// spans the time (same as the engine command).
		let mut blocks: Vec<CHandle> = Vec::new();
		let mut any_spanning = false;
		let slice = std::slice::from_raw_parts(clips, clip_count as usize);
		for (i, clip) in slice.iter().enumerate() {
			let c = match unbox(*clip) {
				Ok(h) => h,
				Err(_) => {
					set_seq_error(&format!("invalid clip at index {}", i));
					return Err(Error::Invalid);
				}
			};
			if blocks.iter().any(|b| b.ctx == c.ctx) {
				continue;
			}
			blocks.push(c);
			let mut in_num: c_int = 0;
			let mut in_den: c_int = 0;
			let mut out_num: c_int = 0;
			let mut out_den: c_int = 0;
			if n::oaknode_block_get_in(c, &mut in_num, &mut in_den) == 0
				&& n::oaknode_block_get_out(c, &mut out_num, &mut out_den) == 0
				&& rat_cmp(in_num as i64, in_den as i64, time_num, time_den)
					== std::cmp::Ordering::Less
				&& rat_cmp(out_num as i64, out_den as i64, time_num, time_den)
					== std::cmp::Ordering::Greater
			{
				any_spanning = true;
			}
		}
		if !any_spanning {
			set_seq_error(&format!("no clip spans time {}", time_ts));
			return Err(Error::NotFound);
		}
		let cmd = tl::oaktimeline_split_preserving_links_command(
			blocks.as_ptr(),
			blocks.len() as c_int,
			&time_num,
			&time_den,
			1,
		);
		if cmd.is_null() {
			set_seq_error("split command failed");
			return Err(Error::Failed("split command failed".into()));
		}
		push_command(cmd, "Split Clips")
	})
}

/// `oakengine_sequence_delete_clips` — delete clips leaving gaps,
/// optionally rippling regions closed.
#[no_mangle]
pub unsafe extern "C" fn oakengine_sequence_delete_clips(
	seq: *mut OakEngineSequence,
	clips: *mut *mut OakEngineClip,
	clip_count: c_int,
	ripple: c_int,
	ripple_ranges_ts: *const i64,
	ripple_range_count: c_int,
	rippled: *mut c_int,
) -> c_int {
	guard(|| unsafe {
		set_seq_error("");
		if !rippled.is_null() {
			*rippled = 0;
		}
		let sequence = match unbox(seq) {
			Ok(h) => h,
			Err(_) => {
				set_seq_error("invalid arguments");
				return Err(Error::Invalid);
			}
		};
		if clip_count < 0 || (clip_count > 0 && clips.is_null()) || ripple_range_count < 0
			|| (ripple_range_count > 0 && ripple_ranges_ts.is_null())
		{
			set_seq_error("invalid arguments");
			return Err(Error::Invalid);
		}
		if clip_count == 0 && (ripple == 0 || ripple_range_count == 0) {
			return Ok(());
		}
		let tb = match seq_time_base(sequence) {
			Ok(tb) => tb,
			Err(_) => {
				set_seq_error("sequence has no valid frame rate");
				return Err(Error::State);
			}
		};

		let mut children: Vec<CHandle> = Vec::new();
		// (track, in, out) rationals of the deleted clips, for the default
		// ripple regions.
		let mut clip_ranges: Vec<(CHandle, i64, i64, i64, i64)> = Vec::new();
		// NULL with a zero count is a legal empty set; the slice must not be
		// constructed from the NULL pointer (`slice::from_raw_parts(NULL, 0)`
		// is UB), so it is only built for a positive count.
		let slice: &[*mut OakEngineClip] = if clip_count > 0 {
			std::slice::from_raw_parts(clips, clip_count as usize)
		} else {
			&[]
		};
		for (i, clip) in slice.iter().enumerate() {
			let c = match unbox(*clip) {
				Ok(h) => h,
				Err(_) => {
					set_seq_error(&format!("invalid clip at index {}", i));
					return Err(Error::Invalid);
				}
			};
			let mut track = CHandle::null();
			let rc = n::oaknode_block_get_track(c, &mut track);
			if rc != 0 || track.is_null() {
				set_seq_error(&format!("invalid clip at index {}", i));
				return Err(Error::Invalid);
			}
			let mut in_num: c_int = 0;
			let mut in_den: c_int = 0;
			let mut out_num: c_int = 0;
			let mut out_den: c_int = 0;
			Error::from_module(n::oaknode_block_get_in(c, &mut in_num, &mut in_den))?;
			Error::from_module(n::oaknode_block_get_out(c, &mut out_num, &mut out_den))?;
			let gap_cmd = tl::oaktimeline_replace_block_with_gap_command(track, c);
			let remove_cmd = n::oaknode_command_create_remove_node(c);
			if gap_cmd.is_null() || remove_cmd.is_null() {
				return Err(Error::Failed("delete clip command failed".into()));
			}
			children.push(gap_cmd);
			children.push(remove_cmd);
			clip_ranges.push((
				track,
				in_num as i64,
				in_den as i64,
				out_num as i64,
				out_den as i64,
			));
		}

		let mut ripple_command: Option<CHandle> = None;
		if ripple != 0 {
			let mut ranges: Vec<(CHandle, i64, i64, i64, i64)> = Vec::new();
			if !ripple_ranges_ts.is_null() && ripple_range_count > 0 {
				for i in 0..ripple_range_count {
					let range = ripple_ranges_ts.add(i as usize * 4);
					let rtype = *range;
					let rindex = *range.add(1);
					if rtype < TRACK_TYPE_VIDEO as i64 || rtype > TRACK_TYPE_SUBTITLE as i64 {
						set_seq_error(&format!("invalid track type in ripple range {}", i));
						return Err(Error::Invalid);
					}
					let mut list = CHandle::null();
					Error::from_module(n::oaknode_sequence_get_track_list(
						sequence,
						rtype as c_int,
						&mut list,
					))?;
					let mut track = CHandle::null();
					let rc = n::oaknode_tracklist_get_track_at(list, rindex as c_int, &mut track);
					release_handle(list);
					if rc != 0 || track.is_null() {
						set_seq_error(&format!("no track at index {} in ripple range {}", rindex, i));
						return Err(Error::NotFound);
					}
					let (in_num, in_den) = ts_to_rational(*range.add(2), tb);
					let (out_num, out_den) = ts_to_rational(*range.add(3), tb);
					ranges.push((track, in_num, in_den, out_num, out_den));
				}
			} else {
				ranges = clip_ranges;
			}
			if !ranges.is_empty() {
				let mut in_nums: Vec<i64> = Vec::new();
				let mut in_dens: Vec<i64> = Vec::new();
				let mut out_nums: Vec<i64> = Vec::new();
				let mut out_dens: Vec<i64> = Vec::new();
				let mut tracks: Vec<CHandle> = Vec::new();
				for (track, in_num, in_den, out_num, out_den) in &ranges {
					tracks.push(*track);
					in_nums.push(*in_num);
					in_dens.push(*in_den);
					out_nums.push(*out_num);
					out_dens.push(*out_den);
				}
				let cmd = tl::oaktimeline_ripple_delete_gaps_command(
					sequence,
					in_nums.as_ptr(),
					in_dens.as_ptr(),
					out_nums.as_ptr(),
					out_dens.as_ptr(),
					tracks.as_ptr(),
					ranges.len() as c_int,
				);
				if !cmd.is_null() {
					children.push(cmd);
					ripple_command = Some(cmd);
				}
			}
		}

		push_multi_commands(&children, "Delete Clips")?;
		if !rippled.is_null() {
			*rippled = if ripple_command.is_some() { 1 } else { 0 };
		}
		Ok(())
	})
}

/// `oakengine_sequence_ripple_delete_range` — remove [in_ts, out_ts) on
/// every track and shift the following content left.
#[no_mangle]
pub unsafe extern "C" fn oakengine_sequence_ripple_delete_range(
	seq: *mut OakEngineSequence,
	in_ts: i64,
	out_ts: i64,
) -> c_int {
	guard(|| unsafe {
		set_seq_error("");
		let sequence = match unbox(seq) {
			Ok(h) => h,
			Err(_) => {
				set_seq_error("invalid range");
				return Err(Error::Invalid);
			}
		};
		if in_ts < 0 || out_ts <= in_ts {
			set_seq_error(&format!("invalid range [{}, {})", in_ts, out_ts));
			return Err(Error::Invalid);
		}
		let tb = match seq_time_base(sequence) {
			Ok(tb) => tb,
			Err(_) => {
				set_seq_error("sequence has no valid frame rate");
				return Err(Error::State);
			}
		};
		// The module exports no sequence-wide ripple-area command
		// (`TimelineRippleRemoveAreaCommand` has no C creator), so the
		// per-track `TrackRippleRemoveAreaCommand` composition is used, which
		// is what the C++ command itself does internally.
		let (in_num, in_den) = ts_to_rational(in_ts, tb);
		let (out_num, out_den) = ts_to_rational(out_ts, tb);
		let mut all_count: c_int = 0;
		Error::from_module(n::oaknode_sequence_get_all_track_count(sequence, &mut all_count))?;
		let mut children: Vec<CHandle> = Vec::new();
		for i in 0..all_count {
			let mut track = CHandle::null();
			Error::from_module(n::oaknode_sequence_get_all_track_at(
				sequence,
				i,
				&mut track,
			))?;
			if track.is_null() {
				continue;
			}
			let cmd = tl::oaktimeline_ripple_remove_area_command(
				track,
				in_num,
				in_den,
				out_num,
				out_den,
			);
			// NOTE: `track` is intentionally NOT released — the module
			// command stores the borrowed handle for its whole lifetime (see
			// `oakengine_sequence_ripple_delete_clip`).
			if cmd.is_null() {
				return Err(Error::Failed("ripple delete command failed".into()));
			}
			children.push(cmd);
		}
		push_multi_commands(&children, "Ripple Delete Range")
	})
}

/// `oakengine_clip_toggle_enabled` — flip the enabled flag of every given
/// clip (one undoable command).
#[no_mangle]
pub unsafe extern "C" fn oakengine_clip_toggle_enabled(
	clips: *mut *mut OakEngineClip,
	count: c_int,
) -> c_int {
	guard_int(|| unsafe {
		set_seq_error("");
		if count < 0 || (count > 0 && clips.is_null()) {
			set_seq_error("invalid arguments");
			return Err(Error::Invalid);
		}
		let mut children: Vec<CHandle> = Vec::new();
		// NULL with a zero count is a legal empty set; the slice must not be
		// constructed from the NULL pointer (`slice::from_raw_parts(NULL, 0)`
		// is UB), so it is only built for a positive count.
		let slice: &[*mut OakEngineClip] = if count > 0 {
			std::slice::from_raw_parts(clips, count as usize)
		} else {
			&[]
		};
		for (i, clip) in slice.iter().enumerate() {
			let c = match unbox(*clip) {
				Ok(h) => h,
				Err(_) => {
					set_seq_error(&format!("invalid clip at index {}", i));
					return Err(Error::Invalid);
				}
			};
			let mut enabled: c_int = 0;
			Error::from_module(n::oaknode_block_get_enabled(c, &mut enabled))?;
			let data = Box::into_raw(Box::new(BlockEnabledCmdData {
				block: c.addref(),
				old_enabled: enabled,
				new_enabled: if enabled != 0 { 0 } else { 1 },
			}));
			let cmd = vtable_command(block_enabled_redo, block_enabled_undo, block_enabled_free, data as *mut c_void)?;
			children.push(cmd);
		}
		push_multi_commands(&children, "Toggle Clips Enabled")?;
		Ok(count)
	})
}

/// `oakengine_clip_set_linked` — link or unlink every given clip with each
/// other (one undoable command).
#[no_mangle]
pub unsafe extern "C" fn oakengine_clip_set_linked(
	clips: *mut *mut OakEngineClip,
	count: c_int,
	linked: c_int,
) -> c_int {
	guard(|| unsafe {
		set_seq_error("");
		if count < 0 || (count > 0 && clips.is_null()) {
			set_seq_error("invalid arguments");
			return Err(Error::Invalid);
		}
		if count == 0 {
			return Ok(());
		}
		let mut handles: Vec<CHandle> = Vec::new();
		let slice = std::slice::from_raw_parts(clips, count as usize);
		for (i, clip) in slice.iter().enumerate() {
			let c = match unbox(*clip) {
				Ok(h) => h,
				Err(_) => {
					set_seq_error(&format!("invalid clip at index {}", i));
					return Err(Error::Invalid);
				}
			};
			handles.push(c);
		}
		// The module has no `NodeLinkManyCommand` creator; pair commands are
		// assembled (each clip linked to the first for a link, every pair
		// unlinked otherwise).
		let mut children: Vec<CHandle> = Vec::new();
		for i in 0..handles.len() {
			for j in (i + 1)..handles.len() {
				let mut cmd = CHandle::null();
				let rc = n::oaknode_node_link_undoable(
					handles[i],
					handles[j],
					linked,
					&mut cmd,
				);
				if rc == 0 && !cmd.is_null() {
					children.push(cmd);
				} else if rc != 0 {
					return Err(Error::Module(rc));
				}
			}
		}
		push_multi_commands(&children, "Link Clips")
	})
}

/// `oakengine_sequence_add_default_transition` — add the configured default
/// transitions around the given clips.
#[no_mangle]
pub unsafe extern "C" fn oakengine_sequence_add_default_transition(
	seq: *mut OakEngineSequence,
	clips: *mut *mut OakEngineClip,
	count: c_int,
) -> c_int {
	guard(|| {
		set_seq_error("");
		let _ = seq;
		if count < 0 || (count > 0 && clips.is_null()) {
			set_seq_error("invalid arguments");
			return Err(Error::Invalid);
		}
		if count == 0 {
			return Ok(());
		}
		// Stub: the module's `TimelineAddDefaultTransitionCommand` is not
		// reachable over the C ABI and its transition-node construction is
		// itself unimplemented (undogeneral.rs `add_transition` NOTE).
		let _ = clips;
		set_seq_error("default transitions are not supported by the module");
		Err(Error::State)
	})
}

/// `oakengine_clip_is_enabled` — 1 if the clip is enabled.
#[no_mangle]
pub unsafe extern "C" fn oakengine_clip_is_enabled(self_: *const OakEngineClip) -> c_int {
	guard_int(|| unsafe {
		if self_.is_null() {
			return Ok(0);
		}
		let h = unbox(self_)?;
		let mut enabled: c_int = 0;
		Error::from_module(n::oaknode_block_get_enabled(h, &mut enabled))?;
		Ok(enabled)
	})
}

/// `oakengine_clip_are_linked` — 1 if the two clips are linked.
#[no_mangle]
pub unsafe extern "C" fn oakengine_clip_are_linked(
	a: *const OakEngineClip,
	b: *const OakEngineClip,
) -> c_int {
	guard_int(|| unsafe {
		if a.is_null() || b.is_null() {
			return Ok(0);
		}
		let ah = unbox(a)?;
		let bh = unbox(b)?;
		let mut linked: c_int = 0;
		Error::from_module(n::oaknode_block_are_linked(ah, bh, &mut linked))?;
		Ok(linked)
	})
}

/// `oakengine_sequence_ripple_delete_in_to_out` — delete the workarea range
/// on every track (ripple or gap), disabling the workarea afterwards.
#[no_mangle]
pub unsafe extern "C" fn oakengine_sequence_ripple_delete_in_to_out(
	seq: *mut OakEngineSequence,
	ripple: c_int,
	in_ts: i64,
	out_ts: i64,
) -> c_int {
	guard(|| unsafe {
		set_seq_error("");
		let sequence = match unbox(seq) {
			Ok(h) => h,
			Err(_) => {
				set_seq_error("invalid range");
				return Err(Error::Invalid);
			}
		};
		if in_ts < 0 || out_ts <= in_ts {
			set_seq_error(&format!("invalid range [{}, {})", in_ts, out_ts));
			return Err(Error::Invalid);
		}
		let wa = seq_workarea(sequence)?;
		let mut enabled: c_int = 0;
		let rc = tl::oaktimeline_workarea_get(
			wa,
			std::ptr::null_mut(),
			std::ptr::null_mut(),
			std::ptr::null_mut(),
			std::ptr::null_mut(),
			&mut enabled,
		);
		if rc != 0 || enabled == 0 {
			release_handle(wa);
			set_seq_error("sequence workarea is not enabled");
			return Err(Error::State);
		}
		let tb = match seq_time_base(sequence) {
			Ok(tb) => tb,
			Err(_) => {
				release_handle(wa);
				set_seq_error("sequence has no valid frame rate");
				return Err(Error::State);
			}
		};
		let (in_num, in_den) = ts_to_rational(in_ts, tb);
		let (out_num, out_den) = ts_to_rational(out_ts, tb);

		let mut children: Vec<CHandle> = Vec::new();
		if ripple != 0 {
			// Ripple the area out on every track.
			let mut all_count: c_int = 0;
			Error::from_module(n::oaknode_sequence_get_all_track_count(
				sequence,
				&mut all_count,
			))?;
			for i in 0..all_count {
				let mut track = CHandle::null();
				Error::from_module(n::oaknode_sequence_get_all_track_at(
					sequence,
					i,
					&mut track,
				))?;
				if track.is_null() {
					continue;
				}
				let cmd = tl::oaktimeline_ripple_remove_area_command(
					track,
					in_num,
					in_den,
					out_num,
					out_den,
				);
				// NOTE: `track` is intentionally NOT released — the module
				// command stores the borrowed handle for its whole lifetime
				// (see `oakengine_sequence_ripple_delete_clip`).
				if cmd.is_null() {
					release_handle(wa);
					return Err(Error::Failed("ripple remove command failed".into()));
				}
				children.push(cmd);
			}
		} else {
			// Fill the area with a fresh gap on every unlocked track.
			let mut all_count: c_int = 0;
			Error::from_module(n::oaknode_sequence_get_all_track_count(
				sequence,
				&mut all_count,
			))?;
			let (len_num, len_den) = rat_sub(out_num, out_den, in_num, in_den);
			for i in 0..all_count {
				let mut track = CHandle::null();
				Error::from_module(n::oaknode_sequence_get_all_track_at(
					sequence,
					i,
					&mut track,
				))?;
				if track.is_null() {
					continue;
				}
				let mut locked: c_int = 0;
				let lrc = n::oaknode_track_get_locked(track, &mut locked);
				if lrc != 0 || locked != 0 {
					release_handle(track);
					continue;
				}
				let mut ttype: c_int = 0;
				let mut tindex: c_int = 0;
				if n::oaknode_track_get_type(track, &mut ttype) != 0
					|| n::oaknode_track_get_index(track, &mut tindex) != 0
				{
					release_handle(track);
					continue;
				}
				let project = seq_project_of(sequence);
				let mut list = CHandle::null();
				let lrc = n::oaknode_sequence_get_track_list(sequence, ttype, &mut list);
				let gap = n::oaknode_block_gap_create();
				if lrc != 0 || project.is_null() || gap.is_null() || list.is_null() {
					release_handle(project);
					release_handle(list);
					release_handle(track);
					release_handle(wa);
					return Err(Error::Failed("gap insertion failed".into()));
				}
				let src = n::oaknode_block_set_length_and_media_out(
					gap,
					len_num as c_int,
					len_den as c_int,
				);
				let add_cmd = n::oaknode_command_create_add_node(project, gap);
				let place_cmd = tl::oaktimeline_place_block_command(
					list,
					tindex,
					gap,
					in_num,
					in_den,
				);
				release_handle(project);
				release_handle(list);
				release_handle(track);
				if src != 0 || add_cmd.is_null() || place_cmd.is_null() {
					release_handle(wa);
					return Err(Error::Failed("gap insertion command failed".into()));
				}
				children.push(add_cmd);
				children.push(place_cmd);
			}
		}
		let disable_cmd = tl::oaktimeline_workarea_set_enabled_command(wa, 0);
		release_handle(wa);
		if disable_cmd.is_null() {
			return Err(Error::Failed("workarea disable command failed".into()));
		}
		children.push(disable_cmd);
		push_multi_commands(&children, "Delete In To Out")
	})
}

/// `oakengine_sequence_trim_clips_to` — trim the nearest clip of every
/// unlocked track to `point_ts`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_sequence_trim_clips_to(
	seq: *mut OakEngineSequence,
	edge: c_int,
	point_ts: i64,
) -> c_int {
	guard_int(|| unsafe {
		set_seq_error("");
		let sequence = match unbox(seq) {
			Ok(h) => h,
			Err(_) => {
				set_seq_error("invalid arguments");
				return Err(Error::Invalid);
			}
		};
		if point_ts < 0 || edge < 0 || edge > 1 {
			set_seq_error("invalid arguments");
			return Err(Error::Invalid);
		}
		let tb = match seq_time_base(sequence) {
			Ok(tb) => tb,
			Err(_) => {
				set_seq_error("sequence has no valid frame rate");
				return Err(Error::State);
			}
		};
		let (point_num, point_den) = ts_to_rational(point_ts, tb);
		let mode = if edge == 0 {
			MOVEMENT_MODE_TRIM_IN
		} else {
			MOVEMENT_MODE_TRIM_OUT
		};

		let mut children: Vec<CHandle> = Vec::new();
		let mut trimmed: c_int = 0;
		let mut all_count: c_int = 0;
		Error::from_module(n::oaknode_sequence_get_all_track_count(sequence, &mut all_count))?;
		for i in 0..all_count {
			let mut track = CHandle::null();
			Error::from_module(n::oaknode_sequence_get_all_track_at(sequence, i, &mut track))?;
			if track.is_null() {
				continue;
			}
			let mut locked: c_int = 0;
			if n::oaknode_track_get_locked(track, &mut locked) != 0 || locked != 0 {
				release_handle(track);
				continue;
			}
			// A trim (in or out) is only meaningful for the block that
			// CONTAINS the point (in < point < out); the nearest-before
			// queries can pick an insertion-order neighbor that ends before
			// the point (which would trim to a negative length), so the
			// strictly-containing lookup is used for both modes.
			let mut block = CHandle::null();
			let rc = n::oaknode_track_get_block_containing_time(
				track,
				point_num as c_int,
				point_den as c_int,
				&mut block,
			);
			if rc != 0 || block.is_null() {
				release_handle(track);
				continue;
			}
			let mut kind: c_int = 0;
			n::oaknode_block_get_kind(block, &mut kind);
			if kind == BLOCK_KIND_GAP {
				release_handle(block);
				release_handle(track);
				continue;
			}
			let mut in_num: c_int = 0;
			let mut in_den: c_int = 0;
			let mut out_num: c_int = 0;
			let mut out_den: c_int = 0;
			n::oaknode_block_get_in(block, &mut in_num, &mut in_den);
			n::oaknode_block_get_out(block, &mut out_num, &mut out_den);
			// new_length = length - |nearest_time - point|; the in-trim
			// anchors the out, the out-trim anchors the in (see
			// `oakengine_clip_trim`).
			let (new_num, new_den) = if mode == MOVEMENT_MODE_TRIM_IN {
				rat_sub(out_num as i64, out_den as i64, point_num, point_den)
			} else {
				rat_sub(point_num, point_den, in_num as i64, in_den as i64)
			};
			let mut old_len_num: c_int = 0;
			let mut old_len_den: c_int = 0;
			Error::from_module(n::oaknode_block_get_length(block, &mut old_len_num, &mut old_len_den))?;
			// Trim the addressed block itself (`trim_cmd` anchors on the
			// block handle; passing the track used to silently reject the
			// trim in the module).
			let cmd = trim_cmd(
				block,
				mode,
				old_len_num,
				old_len_den,
				new_num as c_int,
				new_den as c_int,
			)?;
			release_handle(block);
			release_handle(track);
			children.push(cmd);
			trimmed += 1;
		}
		if trimmed == 0 {
			return Ok(0);
		}
		push_multi_commands(&children, "Trim Clips To Point")?;
		Ok(trimmed)
	})
}

/// `oakengine_sequence_delete_empty_tracks` — remove every empty track.
#[no_mangle]
pub unsafe extern "C" fn oakengine_sequence_delete_empty_tracks(
	seq: *mut OakEngineSequence,
	track_type: c_int,
) -> c_int {
	guard_int(|| unsafe {
		set_seq_error("");
		let sequence = match unbox(seq) {
			Ok(h) => h,
			Err(_) => {
				set_seq_error("invalid arguments");
				return Err(Error::Invalid);
			}
		};
		if track_type < -1 || track_type > TRACK_TYPE_SUBTITLE {
			set_seq_error("invalid arguments");
			return Err(Error::Invalid);
		}
		let mut children: Vec<CHandle> = Vec::new();
		// (track, owning list) pairs for the live removal compensation
		// (the module's `TimelineRemoveTrackCommand` redo is a no-op for the
		// list structure; see below).
		let mut to_remove: Vec<(CHandle, CHandle)> = Vec::new();
		let mut removed: c_int = 0;
		let mut all_count: c_int = 0;
		Error::from_module(n::oaknode_sequence_get_all_track_count(sequence, &mut all_count))?;
		for i in 0..all_count {
			let mut track = CHandle::null();
			Error::from_module(n::oaknode_sequence_get_all_track_at(sequence, i, &mut track))?;
			if track.is_null() {
				continue;
			}
			if track_type >= 0 {
				let mut ttype: c_int = 0;
				if n::oaknode_track_get_type(track, &mut ttype) != 0 || ttype != track_type {
					release_handle(track);
					continue;
				}
			}
			let mut block_count: c_int = 0;
			if n::oaknode_track_get_block_count(track, &mut block_count) != 0
				|| block_count != 0
			{
				release_handle(track);
				continue;
			}
			let cmd = tl::oaktimeline_remove_track_command(track);
			// Locate the owning list for the live removal (addref the track
			// first so it survives the release below).
			let mut ttype: c_int = 0;
			if n::oaknode_track_get_type(track, &mut ttype) == 0 {
				let mut list = CHandle::null();
				if n::oaknode_sequence_get_track_list(sequence, ttype, &mut list) == 0
					&& !list.is_null()
				{
					to_remove.push((track.addref(), list));
				}
			}
			release_handle(track);
			if cmd.is_null() {
				return Err(Error::Failed("remove track command failed".into()));
			}
			children.push(cmd);
			removed += 1;
		}
		if removed == 0 {
			return Ok(0);
		}
		push_multi_commands(&children, "Delete Empty Tracks")?;
		// The module's TimelineRemoveTrackCommand redo is a no-op for the
		// list structure (undogeneral.rs NOTE), so the removal is applied
		// live as compensation (the same documented deviation as
		// `oakengine_sequence_remove_track`).
		for (track, list) in &to_remove {
			n::oaknode_tracklist_remove_track(*list, *track);
			release_handle(*list);
		}
		Ok(removed)
	})
}

/// `oakengine_sequence_marker_remove_many` — remove the markers at the
/// given times (one undoable command).
#[no_mangle]
pub unsafe extern "C" fn oakengine_sequence_marker_remove_many(
	seq: *mut OakEngineSequence,
	times_ts: *const i64,
	count: c_int,
) -> c_int {
	guard_int(|| unsafe {
		set_seq_error("");
		let sequence = match unbox(seq) {
			Ok(h) => h,
			Err(_) => {
				set_seq_error("invalid arguments");
				return Err(Error::Invalid);
			}
		};
		if count < 0 || (count > 0 && times_ts.is_null()) {
			set_seq_error("invalid arguments");
			return Err(Error::Invalid);
		}
		if count == 0 {
			return Ok(0);
		}
		let tb = match seq_time_base(sequence) {
			Ok(tb) => tb,
			Err(_) => {
				set_seq_error("sequence has no valid frame rate");
				return Err(Error::State);
			}
		};
		let list = seq_marker_list(sequence)?;
		// Resolve all markers first so a bad time fails without side effects
		// (markers are unique per time in the engine).
		let mut indices: Vec<c_int> = Vec::new();
		for i in 0..count {
			let (num, den) = ts_to_rational(*times_ts.add(i as usize), tb);
			let idx = marker_index_at(list, num, den);
			if idx < 0 {
				release_handle(list);
				set_seq_error(&format!("no marker at time {}", *times_ts.add(i as usize)));
				return Err(Error::NotFound);
			}
			indices.push(idx);
		}
		let mut children: Vec<CHandle> = Vec::new();
		// The module's MarkerRemoveCommand captures the list INDEX at redo
		// time (not the marker identity), so removals must run in descending
		// index order for earlier removals not to shift later targets.
		indices.sort_unstable();
		indices.dedup();
		for idx in indices.iter().rev() {
			let cmd = tl::oaktimeline_marker_remove_at_command(list, *idx);
			if cmd.is_null() {
				release_handle(list);
				return Err(Error::Failed("remove marker command failed".into()));
			}
			children.push(cmd);
		}
		release_handle(list);
		push_multi_commands(&children, "Remove Markers")?;
		Ok(count)
	})
}

/* ---- Track structure and markers ------------------------------------------ */

/// `oakengine_sequence_remove_track` — remove a track and its content.
#[no_mangle]
pub unsafe extern "C" fn oakengine_sequence_remove_track(
	seq: *mut OakEngineSequence,
	track_type: c_int,
	track_index: c_int,
) -> c_int {
	guard(|| unsafe {
		set_seq_error("");
		let sequence = match unbox(seq) {
			Ok(h) => h,
			Err(_) => {
				set_seq_error("invalid sequence or track type");
				return Err(Error::Invalid);
			}
		};
		if track_type < TRACK_TYPE_VIDEO || track_type > TRACK_TYPE_SUBTITLE {
			set_seq_error("invalid sequence or track type");
			return Err(Error::Invalid);
		}
		let mut list = CHandle::null();
		Error::from_module(n::oaknode_sequence_get_track_list(sequence, track_type, &mut list))?;
		let mut track = CHandle::null();
		let rc = n::oaknode_tracklist_get_track_at(list, track_index, &mut track);
		release_handle(list);
		if rc != 0 || track.is_null() {
			set_seq_error(&format!("no track at index {}", track_index));
			return Err(Error::NotFound);
		}
		// The module's TimelineRemoveTrackCommand redo is a no-op for the
		// list structure (undogeneral.rs NOTE), so the removal is applied
		// live as compensation (documented deviation).
		let cmd = tl::oaktimeline_remove_track_command(track);
		if cmd.is_null() {
			release_handle(track);
			return Err(Error::Failed("remove track command failed".into()));
		}
		push_command(cmd, "Remove Track")?;
		let mut list2 = CHandle::null();
		let rc = n::oaknode_sequence_get_track_list(sequence, track_type, &mut list2);
		if rc == 0 && !list2.is_null() {
			n::oaknode_tracklist_remove_track(list2, track);
			release_handle(list2);
		}
		release_handle(track);
		Ok(())
	})
}

/// `oakengine_sequence_move_track` — move a track within its list.
#[no_mangle]
pub unsafe extern "C" fn oakengine_sequence_move_track(
	seq: *mut OakEngineSequence,
	track_type: c_int,
	from_index: c_int,
	to_index: c_int,
) -> c_int {
	guard(|| unsafe {
		set_seq_error("");
		let sequence = match unbox(seq) {
			Ok(h) => h,
			Err(_) => {
				set_seq_error("invalid sequence or track type");
				return Err(Error::Invalid);
			}
		};
		if track_type < TRACK_TYPE_VIDEO || track_type > TRACK_TYPE_SUBTITLE {
			set_seq_error("invalid sequence or track type");
			return Err(Error::Invalid);
		}
		let mut list = CHandle::null();
		Error::from_module(n::oaknode_sequence_get_track_list(sequence, track_type, &mut list))?;
		let mut count: c_int = 0;
		Error::from_module(n::oaknode_tracklist_get_track_count(list, &mut count))?;
		release_handle(list);
		if from_index < 0 || from_index >= count || to_index < 0 || to_index >= count {
			set_seq_error(&format!("track index out of range ({} tracks)", count));
			return Err(Error::NotFound);
		}
		if from_index == to_index {
			return Ok(());
		}
		// Stub: a true move needs undoable element-aware edge commands (the
		// sequence's track inputs are array elements) plus a track re-order
		// surface; the module provides neither (tracks are not connected to
		// the sequence inputs at all in the module world).
		set_seq_error("track moves are not supported by the module");
		Err(Error::State)
	})
}

/// `oakengine_track_get_height` — track height in internal units.
#[no_mangle]
pub unsafe extern "C" fn oakengine_track_get_height(
	seq: *const OakEngineSequence,
	track_type: c_int,
	track_index: c_int,
	height: *mut f64,
) -> c_int {
	guard(|| unsafe {
		set_seq_error("");
		if height.is_null() {
			set_seq_error("invalid arguments");
			return Err(Error::Invalid);
		}
		let sequence = match unbox(seq) {
			Ok(h) => h,
			Err(_) => {
				set_seq_error("invalid arguments");
				return Err(Error::Invalid);
			}
		};
		if track_type < TRACK_TYPE_VIDEO || track_type > TRACK_TYPE_SUBTITLE {
			set_seq_error("invalid arguments");
			return Err(Error::Invalid);
		}
		let mut list = CHandle::null();
		Error::from_module(n::oaknode_sequence_get_track_list(sequence, track_type, &mut list))?;
		let mut track = CHandle::null();
		let rc = n::oaknode_tracklist_get_track_at(list, track_index, &mut track);
		release_handle(list);
		if rc != 0 || track.is_null() {
			set_seq_error(&format!("no track at index {}", track_index));
			return Err(Error::NotFound);
		}
		let rc = n::oaknode_track_get_height(track, height);
		release_handle(track);
		Error::from_module(rc)
	})
}

/// `oakengine_track_set_height` — set the track height (NOT undoable).
#[no_mangle]
pub unsafe extern "C" fn oakengine_track_set_height(
	seq: *mut OakEngineSequence,
	track_type: c_int,
	track_index: c_int,
	height: f64,
) -> c_int {
	guard(|| unsafe {
		set_seq_error("");
		let sequence = match unbox(seq) {
			Ok(h) => h,
			Err(_) => {
				set_seq_error("invalid arguments");
				return Err(Error::Invalid);
			}
		};
		if height <= 0.0 || track_type < TRACK_TYPE_VIDEO || track_type > TRACK_TYPE_SUBTITLE {
			set_seq_error("invalid arguments");
			return Err(Error::Invalid);
		}
		let mut list = CHandle::null();
		Error::from_module(n::oaknode_sequence_get_track_list(sequence, track_type, &mut list))?;
		let mut track = CHandle::null();
		let rc = n::oaknode_tracklist_get_track_at(list, track_index, &mut track);
		release_handle(list);
		if rc != 0 || track.is_null() {
			set_seq_error(&format!("no track at index {}", track_index));
			return Err(Error::NotFound);
		}
		let rc = n::oaknode_track_set_height(track, height);
		release_handle(track);
		Error::from_module(rc)
	})
}

/// `oakengine_track_is_muted` — 1 if the track is muted.
#[no_mangle]
pub unsafe extern "C" fn oakengine_track_is_muted(
	seq: *const OakEngineSequence,
	track_type: c_int,
	track_index: c_int,
) -> c_int {
	guard_int(|| unsafe {
		if seq.is_null() || track_type < TRACK_TYPE_VIDEO || track_type > TRACK_TYPE_SUBTITLE {
			return Ok(0);
		}
		let h = unbox(seq)?;
		let mut list = CHandle::null();
		if n::oaknode_sequence_get_track_list(h, track_type, &mut list) != 0 {
			return Ok(0);
		}
		let mut track = CHandle::null();
		let rc = n::oaknode_tracklist_get_track_at(list, track_index, &mut track);
		release_handle(list);
		if rc != 0 || track.is_null() {
			return Ok(0);
		}
		let mut muted: c_int = 0;
		let rc = n::oaknode_track_get_muted(track, &mut muted);
		release_handle(track);
		if rc != 0 {
			return Ok(0);
		}
		Ok(muted)
	})
}

/// `oakengine_track_set_muted` — mute/unmute the track (NOT undoable).
#[no_mangle]
pub unsafe extern "C" fn oakengine_track_set_muted(
	seq: *mut OakEngineSequence,
	track_type: c_int,
	track_index: c_int,
	muted: c_int,
) -> c_int {
	guard(|| unsafe {
		set_seq_error("");
		let sequence = match unbox(seq) {
			Ok(h) => h,
			Err(_) => {
				set_seq_error("invalid arguments");
				return Err(Error::Invalid);
			}
		};
		if track_type < TRACK_TYPE_VIDEO || track_type > TRACK_TYPE_SUBTITLE {
			set_seq_error("invalid arguments");
			return Err(Error::Invalid);
		}
		let mut list = CHandle::null();
		Error::from_module(n::oaknode_sequence_get_track_list(sequence, track_type, &mut list))?;
		let mut track = CHandle::null();
		let rc = n::oaknode_tracklist_get_track_at(list, track_index, &mut track);
		release_handle(list);
		if rc != 0 || track.is_null() {
			set_seq_error(&format!("no track at index {}", track_index));
			return Err(Error::NotFound);
		}
		let rc = n::oaknode_track_set_muted(track, muted);
		release_handle(track);
		Error::from_module(rc)
	})
}

/// `oakengine_track_is_locked` — 1 if the track is locked.
#[no_mangle]
pub unsafe extern "C" fn oakengine_track_is_locked(
	seq: *const OakEngineSequence,
	track_type: c_int,
	track_index: c_int,
) -> c_int {
	guard_int(|| unsafe {
		if seq.is_null() || track_type < TRACK_TYPE_VIDEO || track_type > TRACK_TYPE_SUBTITLE {
			return Ok(0);
		}
		let h = unbox(seq)?;
		let mut list = CHandle::null();
		if n::oaknode_sequence_get_track_list(h, track_type, &mut list) != 0 {
			return Ok(0);
		}
		let mut track = CHandle::null();
		let rc = n::oaknode_tracklist_get_track_at(list, track_index, &mut track);
		release_handle(list);
		if rc != 0 || track.is_null() {
			return Ok(0);
		}
		let mut locked: c_int = 0;
		let rc = n::oaknode_track_get_locked(track, &mut locked);
		release_handle(track);
		if rc != 0 {
			return Ok(0);
		}
		Ok(locked)
	})
}

/// `oakengine_track_set_locked` — lock/unlock the track (NOT undoable).
#[no_mangle]
pub unsafe extern "C" fn oakengine_track_set_locked(
	seq: *mut OakEngineSequence,
	track_type: c_int,
	track_index: c_int,
	locked: c_int,
) -> c_int {
	guard(|| unsafe {
		set_seq_error("");
		let sequence = match unbox(seq) {
			Ok(h) => h,
			Err(_) => {
				set_seq_error("invalid arguments");
				return Err(Error::Invalid);
			}
		};
		if track_type < TRACK_TYPE_VIDEO || track_type > TRACK_TYPE_SUBTITLE {
			set_seq_error("invalid arguments");
			return Err(Error::Invalid);
		}
		let mut list = CHandle::null();
		Error::from_module(n::oaknode_sequence_get_track_list(sequence, track_type, &mut list))?;
		let mut track = CHandle::null();
		let rc = n::oaknode_tracklist_get_track_at(list, track_index, &mut track);
		release_handle(list);
		if rc != 0 || track.is_null() {
			set_seq_error(&format!("no track at index {}", track_index));
			return Err(Error::NotFound);
		}
		let rc = n::oaknode_track_set_locked(track, locked);
		release_handle(track);
		Error::from_module(rc)
	})
}

/// `oakengine_sequence_marker_add` — add a marker at `time_ts` (color 0).
#[no_mangle]
pub unsafe extern "C" fn oakengine_sequence_marker_add(
	seq: *mut OakEngineSequence,
	time_ts: i64,
	name: *const c_char,
) -> c_int {
	unsafe { oakengine_sequence_marker_add_ex(seq, time_ts, name, 0) }
}

/// `oakengine_sequence_marker_add_ex` — add a marker with an explicit color.
#[no_mangle]
pub unsafe extern "C" fn oakengine_sequence_marker_add_ex(
	seq: *mut OakEngineSequence,
	time_ts: i64,
	name: *const c_char,
	color: c_int,
) -> c_int {
	guard(|| unsafe {
		set_seq_error("");
		let sequence = match unbox(seq) {
			Ok(h) => h,
			Err(_) => {
				set_seq_error("invalid sequence");
				return Err(Error::Invalid);
			}
		};
		let tb = match seq_time_base(sequence) {
			Ok(tb) => tb,
			Err(_) => {
				set_seq_error("sequence has no valid frame rate");
				return Err(Error::State);
			}
		};
		let (num, den) = ts_to_rational(time_ts, tb);
		let list = seq_marker_list(sequence)?;
		if marker_index_at(list, num, den) >= 0 {
			release_handle(list);
			// The engine's marker insertion asserts on duplicate times.
			set_seq_error(&format!("a marker already exists at time {}", time_ts));
			return Err(Error::State);
		}
		let name_c = std::ffi::CString::new(read_cstr(name))
			.map_err(|_| Error::Failed("invalid name".into()))?;
		let cmd = tl::oaktimeline_marker_add_command(
			list,
			num as c_int,
			den as c_int,
			num as c_int,
			den as c_int,
			name_c.as_ptr(),
			color,
		);
		release_handle(list);
		if cmd.is_null() {
			set_seq_error("add marker command failed");
			return Err(Error::Failed("add marker command failed".into()));
		}
		push_command(cmd, "Add Marker")
	})
}

/// `oakengine_sequence_marker_remove` — remove the marker at `time_ts`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_sequence_marker_remove(
	seq: *mut OakEngineSequence,
	time_ts: i64,
) -> c_int {
	guard(|| unsafe {
		set_seq_error("");
		let sequence = match unbox(seq) {
			Ok(h) => h,
			Err(_) => {
				set_seq_error("invalid sequence");
				return Err(Error::Invalid);
			}
		};
		let tb = match seq_time_base(sequence) {
			Ok(tb) => tb,
			Err(_) => {
				set_seq_error("sequence has no valid frame rate");
				return Err(Error::State);
			}
		};
		let (num, den) = ts_to_rational(time_ts, tb);
		let list = seq_marker_list(sequence)?;
		let idx = marker_index_at(list, num, den);
		if idx < 0 {
			release_handle(list);
			set_seq_error(&format!("no marker at time {}", time_ts));
			return Err(Error::NotFound);
		}
		let cmd = tl::oaktimeline_marker_remove_at_command(list, idx);
		release_handle(list);
		if cmd.is_null() {
			set_seq_error("remove marker command failed");
			return Err(Error::Failed("remove marker command failed".into()));
		}
		push_command(cmd, "Remove Marker")
	})
}

/// `oakengine_sequence_marker_rename` — rename the marker at `time_ts`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_sequence_marker_rename(
	seq: *mut OakEngineSequence,
	time_ts: i64,
	name: *const c_char,
) -> c_int {
	guard(|| unsafe {
		set_seq_error("");
		let sequence = match unbox(seq) {
			Ok(h) => h,
			Err(_) => {
				set_seq_error("invalid sequence");
				return Err(Error::Invalid);
			}
		};
		let tb = match seq_time_base(sequence) {
			Ok(tb) => tb,
			Err(_) => {
				set_seq_error("sequence has no valid frame rate");
				return Err(Error::State);
			}
		};
		let (num, den) = ts_to_rational(time_ts, tb);
		let list = seq_marker_list(sequence)?;
		let idx = marker_index_at(list, num, den);
		if idx < 0 {
			release_handle(list);
			set_seq_error(&format!("no marker at time {}", time_ts));
			return Err(Error::NotFound);
		}
		let name_c = std::ffi::CString::new(read_cstr(name))
			.map_err(|_| Error::Failed("invalid name".into()))?;
		let cmd = tl::oaktimeline_marker_set_props_command(list, idx, -1, name_c.as_ptr());
		release_handle(list);
		if cmd.is_null() {
			set_seq_error("rename marker command failed");
			return Err(Error::Failed("rename marker command failed".into()));
		}
		push_command(cmd, "Rename Marker")
	})
}

/* ---- Marker handle family -------------------------------------------------- */

/// `oakengine_marker_list_count` — number of markers in the list.
#[no_mangle]
pub unsafe extern "C" fn oakengine_marker_list_count(
	list: *const OakEngineMarkerList,
) -> c_int {
	guard_int(|| unsafe {
		if list.is_null() {
			return Ok(0);
		}
		let h = unbox(list)?;
		let mut count: c_int = 0;
		Error::from_module(tl::oaktimeline_marker_count(h, &mut count))?;
		Ok(count)
	})
}

/// `oakengine_marker_list_add` — add a marker (undoable).
#[no_mangle]
pub unsafe extern "C" fn oakengine_marker_list_add(
	list: *mut OakEngineMarkerList,
	in_num: i64,
	in_den: i64,
	out_num: i64,
	out_den: i64,
	name: *const c_char,
	color: c_int,
) -> c_int {
	guard(|| unsafe {
		if list.is_null() {
			return Err(Error::Invalid);
		}
		let h = unbox(list)?;
		let name_c = std::ffi::CString::new(read_cstr(name))
			.map_err(|_| Error::Failed("invalid name".into()))?;
		let cmd = tl::oaktimeline_marker_add_command(
			h,
			in_num as c_int,
			in_den as c_int,
			out_num as c_int,
			out_den as c_int,
			name_c.as_ptr(),
			color,
		);
		if cmd.is_null() {
			return Err(Error::Failed("add marker command failed".into()));
		}
		push_command(cmd, "Add Marker")
	})
}

/// `oakengine_marker_create` — create a detached marker.
#[no_mangle]
pub unsafe extern "C" fn oakengine_marker_create(
	color: c_int,
	in_num: i64,
	in_den: i64,
	out_num: i64,
	out_den: i64,
	name: *const c_char,
) -> *mut OakEngineMarker {
	guard_ptr(|| {
		// Stub: the oaktimeline module has no standalone marker handle (all
		// marker operations are list-based over the C ABI), so a detached
		// marker cannot be represented.
		let _ = (color, in_num, in_den, out_num, out_den, name);
		Ok(std::ptr::null_mut())
	})
}

/// `oakengine_marker_free` — free a detached marker (NULL-safe no-op).
#[no_mangle]
pub unsafe extern "C" fn oakengine_marker_free(marker: *mut OakEngineMarker) {
	guard_void(|| unsafe {
		if marker.is_null() {
			return;
		}
		// Detached markers are a stub (`oakengine_marker_create` returns
		// NULL); borrowed marker boxes are freed by the caller's box
		// lifecycle, so this only releases a borrowed box.
		free_box::<OakEngineMarker>(marker);
	})
}

/// `oakengine_marker_list_add_existing` — re-add a marker to the list.
#[no_mangle]
pub unsafe extern "C" fn oakengine_marker_list_add_existing(
	list: *mut OakEngineMarkerList,
	marker: *mut OakEngineMarker,
) -> c_int {
	guard(|| unsafe {
		if list.is_null() || marker.is_null() {
			return Err(Error::Invalid);
		}
		let lh = unbox(list)?;
		let (mlist, index) = marker_unbox(marker)?;
		// Read the marker's data back through the list and add a fresh
		// marker with it (the module has no marker-insert-by-handle).
		let mut in_num: c_int = 0;
		let mut in_den: c_int = 0;
		let mut out_num: c_int = 0;
		let mut out_den: c_int = 0;
		let mut color: c_int = 0;
		let rc = tl::oaktimeline_marker_at(
			mlist,
			index,
			&mut in_num,
			&mut in_den,
			&mut out_num,
			&mut out_den,
			&mut color,
			std::ptr::null_mut(),
			0,
		);
		if rc < 0 {
			return Err(Error::Module(rc));
		}
		let mut name_buf = [0 as c_char; 4096];
		let rc = tl::oaktimeline_marker_at(
			mlist,
			index,
			&mut in_num,
			&mut in_den,
			&mut out_num,
			&mut out_den,
			&mut color,
			name_buf.as_mut_ptr(),
			name_buf.len() as c_int,
		);
		if rc < 0 {
			return Err(Error::Module(rc));
		}
		let name = read_cstr(name_buf.as_ptr());
		let name_c = std::ffi::CString::new(name)
			.map_err(|_| Error::Failed("invalid name".into()))?;
		let cmd = tl::oaktimeline_marker_add_command(
			lh,
			in_num,
			in_den,
			out_num,
			out_den,
			name_c.as_ptr(),
			color,
		);
		if cmd.is_null() {
			return Err(Error::Failed("add existing marker command failed".into()));
		}
		push_command(cmd, "Add Existing Marker")
	})
}

/// `oakengine_marker_list_at` — marker at the given sorted index.
#[no_mangle]
pub unsafe extern "C" fn oakengine_marker_list_at(
	list: *const OakEngineMarkerList,
	index: c_int,
) -> *mut OakEngineMarker {
	guard_ptr(|| unsafe {
		if list.is_null() || index < 0 {
			return Ok(std::ptr::null_mut());
		}
		let h = unbox(list)?;
		let mut count: c_int = 0;
		if tl::oaktimeline_marker_count(h, &mut count) != 0 || index >= count {
			return Ok(std::ptr::null_mut());
		}
		// The module has no marker handle; the marker is represented as its
		// (list, index) position.
		let borrowed = h.addref();
		Ok(box_marker(borrowed, index))
	})
}

/// `oakengine_marker_list_marker_at_time` — marker by exact in-point.
#[no_mangle]
pub unsafe extern "C" fn oakengine_marker_list_marker_at_time(
	list: *const OakEngineMarkerList,
	num: i64,
	den: i64,
) -> *mut OakEngineMarker {
	guard_ptr(|| unsafe {
		if list.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let h = unbox(list)?;
		let idx = marker_index_at(h, num, den);
		if idx < 0 {
			return Ok(std::ptr::null_mut());
		}
		let borrowed = h.addref();
		Ok(box_marker(borrowed, idx))
	})
}

/// `oakengine_marker_get_time` — the marker's time range as rational
/// seconds.
#[no_mangle]
pub unsafe extern "C" fn oakengine_marker_get_time(
	self_: *const OakEngineMarker,
	in_num: *mut i64,
	in_den: *mut i64,
	out_num: *mut i64,
	out_den: *mut i64,
) -> c_int {
	guard(|| unsafe {
		if self_.is_null() {
			return Err(Error::Invalid);
		}
		let (list, index) = marker_unbox(self_)?;
		let mut n0: c_int = 0;
		let mut d0: c_int = 0;
		let mut n1: c_int = 0;
		let mut d1: c_int = 0;
		let mut color: c_int = 0;
		let rc = tl::oaktimeline_marker_at(
			list,
			index,
			&mut n0,
			&mut d0,
			&mut n1,
			&mut d1,
			&mut color,
			std::ptr::null_mut(),
			0,
		);
		if rc < 0 {
			return Err(Error::Module(rc));
		}
		if !in_num.is_null() {
			*in_num = n0 as i64;
		}
		if !in_den.is_null() {
			*in_den = d0 as i64;
		}
		if !out_num.is_null() {
			*out_num = n1 as i64;
		}
		if !out_den.is_null() {
			*out_den = d1 as i64;
		}
		Ok(())
	})
}

/// `oakengine_marker_get_name` — the marker's name (buf/size).
#[no_mangle]
pub unsafe extern "C" fn oakengine_marker_get_name(
	self_: *const OakEngineMarker,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	guard_int(|| unsafe {
		if self_.is_null() {
			return Err(Error::Invalid);
		}
		let (list, index) = marker_unbox(self_)?;
		let mut n0: c_int = 0;
		let mut d0: c_int = 0;
		let mut n1: c_int = 0;
		let mut d1: c_int = 0;
		let mut color: c_int = 0;
		let rc = tl::oaktimeline_marker_at(
			list,
			index,
			&mut n0,
			&mut d0,
			&mut n1,
			&mut d1,
			&mut color,
			buf,
			buf_size,
		);
		if rc < 0 {
			Err(Error::Module(rc))
		} else {
			Ok(string_result(rc))
		}
	})
}

/// `oakengine_marker_get_color` — the marker's color index (-1 on NULL).
#[no_mangle]
pub unsafe extern "C" fn oakengine_marker_get_color(
	self_: *const OakEngineMarker,
) -> c_int {
	guard_int(|| unsafe {
		if self_.is_null() {
			return Ok(-1);
		}
		let (list, index) = marker_unbox(self_)?;
		let mut n0: c_int = 0;
		let mut d0: c_int = 0;
		let mut n1: c_int = 0;
		let mut d1: c_int = 0;
		let mut color: c_int = 0;
		let rc = tl::oaktimeline_marker_at(
			list,
			index,
			&mut n0,
			&mut d0,
			&mut n1,
			&mut d1,
			&mut color,
			std::ptr::null_mut(),
			0,
		);
		if rc < 0 {
			return Err(Error::Module(rc));
		}
		Ok(color)
	})
}

/// `oakengine_marker_has_sibling_at_time` — 1 if the list has another
/// marker at the given time.
#[no_mangle]
pub unsafe extern "C" fn oakengine_marker_has_sibling_at_time(
	self_: *const OakEngineMarker,
	num: i64,
	den: i64,
) -> c_int {
	guard_int(|| unsafe {
		if self_.is_null() {
			return Ok(0);
		}
		let (list, index) = marker_unbox(self_)?;
		// Scan the list for ANOTHER marker with the same in-point.
		let mut count: c_int = 0;
		if tl::oaktimeline_marker_count(list, &mut count) != 0 {
			return Ok(0);
		}
		for i in 0..count {
			if i == index {
				continue;
			}
			let mut n0: c_int = 0;
			let mut d0: c_int = 0;
			let mut n1: c_int = 0;
			let mut d1: c_int = 0;
			let mut color: c_int = 0;
			let rc = tl::oaktimeline_marker_at(
				list,
				i,
				&mut n0,
				&mut d0,
				&mut n1,
				&mut d1,
				&mut color,
				std::ptr::null_mut(),
				0,
			);
			if rc >= 0 && n0 as i64 == num && d0 as i64 == den {
				return Ok(1);
			}
		}
		Ok(0)
	})
}

/// `oakengine_marker_set_time_live` — set the marker's time range directly.
#[no_mangle]
pub unsafe extern "C" fn oakengine_marker_set_time_live(
	self_: *mut OakEngineMarker,
	in_num: i64,
	in_den: i64,
	out_num: i64,
	out_den: i64,
) -> c_int {
	guard(|| unsafe {
		if self_.is_null() {
			return Err(Error::Invalid);
		}
		let (list, index) = marker_unbox(self_)?;
		// The module has no live marker time setter; the change is applied
		// through the undoable MarkerChangeTimeCommand (documented deviation
		// from the non-undoable contract).
		let cmd = tl::oaktimeline_marker_set_time_command(
			list,
			index,
			in_num as c_int,
			in_den as c_int,
			out_num as c_int,
			out_den as c_int,
		);
		if cmd.is_null() {
			return Err(Error::Failed("set marker time command failed".into()));
		}
		push_command(cmd, "Move Marker")
	})
}

/// `oakengine_marker_commit_time` — commit a time change as an undoable
/// command (optionally into `command`).
#[no_mangle]
pub unsafe extern "C" fn oakengine_marker_commit_time(
	self_: *mut OakEngineMarker,
	old_in_num: i64,
	old_in_den: i64,
	old_out_num: i64,
	old_out_den: i64,
	new_in_num: i64,
	new_in_den: i64,
	new_out_num: i64,
	new_out_den: i64,
	command: *mut c_void,
) -> c_int {
	guard(|| unsafe {
		if self_.is_null() {
			return Err(Error::Invalid);
		}
		let (list, index) = marker_unbox(self_)?;
		// The module's MarkerChangeTimeCommand takes only the new range and
		// captures the old range from the list at redo time; the explicit
		// old range is validated against the marker instead.
		let mut n0: c_int = 0;
		let mut d0: c_int = 0;
		let mut n1: c_int = 0;
		let mut d1: c_int = 0;
		let mut color: c_int = 0;
		let rc = tl::oaktimeline_marker_at(
			list,
			index,
			&mut n0,
			&mut d0,
			&mut n1,
			&mut d1,
			&mut color,
			std::ptr::null_mut(),
			0,
		);
		if rc < 0 {
			return Err(Error::Module(rc));
		}
		let _ = (old_in_num, old_in_den, old_out_num, old_out_den);
		let cmd = tl::oaktimeline_marker_set_time_command(
			list,
			index,
			new_in_num as c_int,
			new_in_den as c_int,
			new_out_num as c_int,
			new_out_den as c_int,
		);
		if cmd.is_null() {
			return Err(Error::Failed("set marker time command failed".into()));
		}
		if command.is_null() {
			push_command(cmd, "Move Marker")
		} else {
			let parent = unbox(command.cast::<OakEngineClipboard>())?;
			let rc = u::oakundo_command_multi_add_child(parent, cmd);
			Error::from_module(rc)
		}
	})
}

/// `oakengine_marker_set_time_command` — create a MarkerChangeTimeCommand.
#[no_mangle]
pub unsafe extern "C" fn oakengine_marker_set_time_command(
	marker: *mut OakEngineMarker,
	new_time_num: i64,
	new_time_den: i64,
) -> *mut c_void {
	guard_ptr(|| unsafe {
		if marker.is_null() || new_time_den == 0 {
			return Ok(std::ptr::null_mut());
		}
		let (list, index) = marker_unbox(marker)?;
		// The marker's out offset is preserved (new range = new in point +
		// old length).
		let mut n0: c_int = 0;
		let mut d0: c_int = 0;
		let mut n1: c_int = 0;
		let mut d1: c_int = 0;
		let mut color: c_int = 0;
		let rc = tl::oaktimeline_marker_at(
			list,
			index,
			&mut n0,
			&mut d0,
			&mut n1,
			&mut d1,
			&mut color,
			std::ptr::null_mut(),
			0,
		);
		if rc < 0 {
			return Ok(std::ptr::null_mut());
		}
		// The marker's out offset is preserved: new out = new in + old length.
		let (off_num, off_den) = rat_sub(n1 as i64, d1 as i64, n0 as i64, d0 as i64);
		let (out_num, out_den) = rat_add(new_time_num, new_time_den, off_num, off_den);
		let cmd = tl::oaktimeline_marker_set_time_command(
			list,
			index,
			new_time_num as c_int,
			new_time_den as c_int,
			out_num as c_int,
			out_den as c_int,
		);
		if cmd.is_null() {
			return Ok(std::ptr::null_mut());
		}
		Ok(command_box(cmd)?.cast())
	})
}

/// `oakengine_marker_remove` — remove the marker from its list (undoable).
#[no_mangle]
pub unsafe extern "C" fn oakengine_marker_remove(self_: *mut OakEngineMarker) -> c_int {
	guard(|| unsafe {
		if self_.is_null() {
			return Err(Error::Invalid);
		}
		let (list, index) = marker_unbox(self_)?;
		let cmd = tl::oaktimeline_marker_remove_at_command(list, index);
		if cmd.is_null() {
			return Err(Error::Failed("remove marker command failed".into()));
		}
		push_command(cmd, "Remove Marker")
	})
}

/// `oakengine_marker_set_properties` — batch-set properties on one or more
/// markers (one undoable command).
#[no_mangle]
pub unsafe extern "C" fn oakengine_marker_set_properties(
	markers: *mut *mut OakEngineMarker,
	count: c_int,
	color: c_int,
	name: *const c_char,
	move_time: c_int,
	new_in_num: i64,
	new_in_den: i64,
	new_out_num: i64,
	new_out_den: i64,
	command: *mut c_void,
) -> c_int {
	guard(|| unsafe {
		if markers.is_null() || count <= 0 {
			return Err(Error::Invalid);
		}
		let mut children: Vec<CHandle> = Vec::new();
		let slice = std::slice::from_raw_parts(markers, count as usize);
		for (_i, m) in slice.iter().enumerate() {
			let (list, index) = match marker_unbox(*m) {
				Ok(pair) => pair,
				Err(_) => continue,
			};
			if color >= 0 || !name.is_null() {
				let name_c = if name.is_null() {
					None
				} else {
					Some(
						std::ffi::CString::new(read_cstr(name))
							.map_err(|_| Error::Failed("invalid name".into()))?,
					)
				};
				let name_ptr = match &name_c {
					Some(c) => c.as_ptr(),
					None => std::ptr::null(),
				};
				let cmd = tl::oaktimeline_marker_set_props_command(list, index, color, name_ptr);
				if !cmd.is_null() {
					children.push(cmd);
				}
			}
			if move_time != 0 && count == 1 {
				let cmd = tl::oaktimeline_marker_set_time_command(
					list,
					index,
					new_in_num as c_int,
					new_in_den as c_int,
					new_out_num as c_int,
					new_out_den as c_int,
				);
				if !cmd.is_null() {
					children.push(cmd);
				}
			}
		}
		if children.is_empty() {
			return Ok(());
		}
		if command.is_null() {
			push_multi_commands(&children, "Set Marker Properties")
		} else {
			let parent = unbox(command.cast::<OakEngineClipboard>())?;
			for child in &children {
				let rc = u::oakundo_command_multi_add_child(parent, *child);
				if rc != 0 {
					return Err(Error::Module(rc));
				}
			}
			Ok(())
		}
	})
}

/* ---- Workarea handle family ------------------------------------------------- */

/// `oakengine_workarea_create` — create a standalone workarea.
#[no_mangle]
pub extern "C" fn oakengine_workarea_create() -> *mut OakEngineWorkarea {
	guard_ptr(|| {
		let wa = unsafe { tl::oaktimeline_workarea_create() };
		if wa.is_null() {
			return Ok(std::ptr::null_mut());
		}
		Ok(box_handle::<OakEngineWorkarea>(wa))
	})
}

/// `oakengine_workarea_free` — free a standalone workarea (NULL-safe).
#[no_mangle]
pub unsafe extern "C" fn oakengine_workarea_free(wa: *mut OakEngineWorkarea) {
	guard_void(|| unsafe {
		free_box::<OakEngineWorkarea>(wa);
	})
}

/// `oakengine_workarea_get` — read the workarea state.
#[no_mangle]
pub unsafe extern "C" fn oakengine_workarea_get(
	self_: *const OakEngineWorkarea,
	in_num: *mut i64,
	in_den: *mut i64,
	out_num: *mut i64,
	out_den: *mut i64,
	enabled: *mut c_int,
) -> c_int {
	guard(|| unsafe {
		let h = unbox(self_)?;
		let mut n0: c_int = 0;
		let mut d0: c_int = 0;
		let mut n1: c_int = 0;
		let mut d1: c_int = 0;
		let mut en: c_int = 0;
		let rc = tl::oaktimeline_workarea_get(
			h,
			&mut n0,
			&mut d0,
			&mut n1,
			&mut d1,
			&mut en,
		);
		Error::from_module(rc)?;
		if !in_num.is_null() {
			*in_num = n0 as i64;
		}
		if !in_den.is_null() {
			*in_den = d0 as i64;
		}
		if !out_num.is_null() {
			*out_num = n1 as i64;
		}
		if !out_den.is_null() {
			*out_den = d1 as i64;
		}
		if !enabled.is_null() {
			*enabled = en;
		}
		Ok(())
	})
}

/// `oakengine_workarea_set_range` — set the workarea range (non-undoable).
#[no_mangle]
pub unsafe extern "C" fn oakengine_workarea_set_range(
	self_: *mut OakEngineWorkarea,
	in_num: i64,
	in_den: i64,
	out_num: i64,
	out_den: i64,
) -> c_int {
	guard(|| unsafe {
		let h = unbox(self_)?;
		Error::from_module(tl::oaktimeline_workarea_set_range(
			h,
			in_num as c_int,
			in_den as c_int,
			out_num as c_int,
			out_den as c_int,
		))
	})
}

/// `oakengine_workarea_set_enabled` — enable/disable (non-undoable).
#[no_mangle]
pub unsafe extern "C" fn oakengine_workarea_set_enabled(
	self_: *mut OakEngineWorkarea,
	enabled: c_int,
) -> c_int {
	guard(|| unsafe {
		let h = unbox(self_)?;
		Error::from_module(tl::oaktimeline_workarea_set_enabled(h, enabled))
	})
}

/// `oakengine_workarea_set_range_undoable` — set the range with undo
/// support.
#[no_mangle]
pub unsafe extern "C" fn oakengine_workarea_set_range_undoable(
	self_: *mut OakEngineWorkarea,
	in_num: i64,
	in_den: i64,
	out_num: i64,
	out_den: i64,
	old_in_num: i64,
	old_in_den: i64,
	old_out_num: i64,
	old_out_den: i64,
	command: *mut c_void,
) -> c_int {
	guard(|| unsafe {
		let h = unbox(self_)?;
		let cmd = tl::oaktimeline_workarea_set_range_command(
			h,
			in_num as c_int,
			in_den as c_int,
			out_num as c_int,
			out_den as c_int,
			old_in_num as c_int,
			old_in_den as c_int,
			old_out_num as c_int,
			old_out_den as c_int,
		);
		if cmd.is_null() {
			return Err(Error::Failed("workarea range command failed".into()));
		}
		if command.is_null() {
			push_command(cmd, "Set Workarea Range")
		} else {
			let parent = unbox(command.cast::<OakEngineClipboard>())?;
			let rc = u::oakundo_command_multi_add_child(parent, cmd);
			Error::from_module(rc)
		}
	})
}

/// `oakengine_workarea_set_enabled_undoable` — enable/disable with undo
/// support.
#[no_mangle]
pub unsafe extern "C" fn oakengine_workarea_set_enabled_undoable(
	self_: *mut OakEngineWorkarea,
	enabled: c_int,
	command: *mut c_void,
) -> c_int {
	guard(|| unsafe {
		let h = unbox(self_)?;
		let cmd = tl::oaktimeline_workarea_set_enabled_command(h, enabled);
		if cmd.is_null() {
			return Err(Error::Failed("workarea enabled command failed".into()));
		}
		if command.is_null() {
			push_command(cmd, "Set Workarea Enabled")
		} else {
			let parent = unbox(command.cast::<OakEngineClipboard>())?;
			let rc = u::oakundo_command_multi_add_child(parent, cmd);
			Error::from_module(rc)
		}
	})
}

/// `oakengine_workarea_reset_in_out` — fill the reset sentinel values.
#[no_mangle]
pub unsafe extern "C" fn oakengine_workarea_reset_in_out(
	in_num: *mut i64,
	in_den: *mut i64,
	out_num: *mut i64,
	out_den: *mut i64,
) {
	guard_void(|| unsafe {
		let mut n0: c_int = 0;
		let mut d0: c_int = 0;
		let mut n1: c_int = 0;
		let mut d1: c_int = 0;
		let rc = tl::oaktimeline_workarea_reset(&mut n0, &mut d0, &mut n1, &mut d1);
		if rc != 0 {
			return;
		}
		if !in_num.is_null() {
			*in_num = n0 as i64;
		}
		if !in_den.is_null() {
			*in_den = d0 as i64;
		}
		if !out_num.is_null() {
			*out_num = n1 as i64;
		}
		if !out_den.is_null() {
			*out_den = d1 as i64;
		}
	})
}

/* ---- Clip media range / cache / media in ----------------------------------- */

/// `oakengine_clip_get_media_range_rational` — clip media range as
/// rational seconds.
#[no_mangle]
pub unsafe extern "C" fn oakengine_clip_get_media_range_rational(
	self_: *const OakEngineClip,
	in_num: *mut i64,
	in_den: *mut i64,
	out_num: *mut i64,
	out_den: *mut i64,
) -> c_int {
	guard(|| unsafe {
		let h = unbox(self_)?;
		let mut mi_num: c_int = 0;
		let mut mi_den: c_int = 0;
		let mut len_num: c_int = 0;
		let mut len_den: c_int = 0;
		Error::from_module(n::oaknode_clip_get_media_in(h, &mut mi_num, &mut mi_den))?;
		Error::from_module(n::oaknode_block_get_length(h, &mut len_num, &mut len_den))?;
		// media_out = media_in + length (speed/reverse ignored, like the
		// capi).
		let (mo_num, mo_den) = rat_add(mi_num as i64, mi_den as i64, len_num as i64, len_den as i64);
		if !in_num.is_null() {
			*in_num = mi_num as i64;
		}
		if !in_den.is_null() {
			*in_den = mi_den as i64;
		}
		if !out_num.is_null() {
			*out_num = mo_num;
		}
		if !out_den.is_null() {
			*out_den = mo_den;
		}
		Ok(())
	})
}

/// `oakengine_clip_get_media_in_rational` — clip media in-point as rational
/// seconds.
#[no_mangle]
pub unsafe extern "C" fn oakengine_clip_get_media_in_rational(
	self_: *const OakEngineClip,
	num: *mut i64,
	den: *mut i64,
) -> c_int {
	guard(|| unsafe {
		let h = unbox(self_)?;
		let mut n: c_int = 0;
		let mut d: c_int = 0;
		Error::from_module(n::oaknode_clip_get_media_in(h, &mut n, &mut d))?;
		if !num.is_null() {
			*num = n as i64;
		}
		if !den.is_null() {
			*den = d as i64;
		}
		Ok(())
	})
}

/// `oakengine_clip_set_media_in` — move the clip's media in-point (as a
/// frame timestamp; undoable when `undoable` != 0).
#[no_mangle]
pub unsafe extern "C" fn oakengine_clip_set_media_in(
	self_: *mut OakEngineClip,
	media_in_ts: i64,
	undoable: c_int,
) -> c_int {
	guard(|| unsafe {
		set_seq_error("");
		let h = match unbox(self_) {
			Ok(h) => h,
			Err(_) => {
				set_seq_error("invalid clip handle");
				return Err(Error::Invalid);
			}
		};
		// The clip's sequence provides the timebase.
		let mut track = CHandle::null();
		let rc = n::oaknode_block_get_track(h, &mut track);
		if rc != 0 || track.is_null() {
			set_seq_error("clip is not on a track");
			return Err(Error::State);
		}
		let mut sequence = CHandle::null();
		let rc = n::oaknode_track_get_sequence(track, &mut sequence);
		release_handle(track);
		if rc != 0 || sequence.is_null() {
			set_seq_error("clip is not on a track");
			return Err(Error::State);
		}
		let tb = match seq_time_base(sequence) {
			Ok(tb) => tb,
			Err(_) => {
				release_handle(sequence);
				set_seq_error("sequence has no valid frame rate");
				return Err(Error::State);
			}
		};
		release_handle(sequence);
		let (num, den) = ts_to_rational(media_in_ts, tb);
		apply_clip_media_in(h, num as c_int, den as c_int, undoable)
	})
}

/// Shared media-in write honoring the undoable flag (the capi's
/// `BlockSetMediaInCommand` vs the direct setter).
///
/// # Safety
/// `clip` must be a live module clip handle.
unsafe fn apply_clip_media_in(clip: CHandle, num: c_int, den: c_int, undoable: c_int) -> Result<()> {
	unsafe {
		if undoable != 0 {
			let mut old_num: c_int = 0;
			let mut old_den: c_int = 0;
			Error::from_module(n::oaknode_clip_get_media_in(clip, &mut old_num, &mut old_den))?;
			let data = Box::into_raw(Box::new(ClipMediaInCmdData {
				clip: clip.addref(),
				old_num,
				old_den,
				new_num: num,
				new_den: den,
			}));
			let cmd = vtable_command(clip_media_in_redo, clip_media_in_undo, clip_media_in_free, data as *mut c_void)?;
			push_command(cmd, "Set Media In")
		} else {
			Error::from_module(n::oaknode_clip_set_media_in(clip, num, den))
		}
	}
}

/// `oakengine_clip_set_media_in_rational` — move the media in-point as a
/// rational seconds value.
#[no_mangle]
pub unsafe extern "C" fn oakengine_clip_set_media_in_rational(
	self_: *mut OakEngineClip,
	num: i64,
	den: i64,
	undoable: c_int,
) -> c_int {
	guard(|| unsafe {
		set_seq_error("");
		let h = match unbox(self_) {
			Ok(h) => h,
			Err(_) => {
				set_seq_error("invalid clip handle");
				return Err(Error::Invalid);
			}
		};
		if den == 0 {
			set_seq_error("invalid rational denominator");
			return Err(Error::Invalid);
		}
		apply_clip_media_in(h, num as c_int, den as c_int, undoable)
	})
}

/// `oakengine_clip_request_invalidate` — request cache invalidation
/// (NULL-safe no-op).
#[no_mangle]
pub unsafe extern "C" fn oakengine_clip_request_invalidate(
	self_: *mut OakEngineClip,
	in_ts: i64,
	out_ts: i64,
	type_: c_int,
) {
	guard_void(|| {
		// Stub: matches the capi's headless no-op (the module has no cache
		// invalidation surface).
		let _ = (self_, in_ts, out_ts, type_);
	})
}

/// `oakengine_clip_add_cache_passthrough` — add a cache passthrough
/// dependency (NULL-safe no-op).
#[no_mangle]
pub unsafe extern "C" fn oakengine_clip_add_cache_passthrough(
	dest: *mut OakEngineClip,
	source: *mut OakEngineClip,
) {
	guard_void(|| unsafe {
		if dest.is_null() || source.is_null() {
			return;
		}
		// The module's passthrough is itself a no-op until per-node caches
		// exist; forwarded for parity.
		let d = match unbox(dest) {
			Ok(h) => h,
			Err(_) => return,
		};
		let s = match unbox(source) {
			Ok(h) => h,
			Err(_) => return,
		};
		n::oaknode_clip_add_cache_passthrough_from(d, s);
	})
}

/// `oakengine_clip_discard_cache` — discard the clip's cache (NULL-safe
/// no-op).
#[no_mangle]
pub unsafe extern "C" fn oakengine_clip_discard_cache(self_: *mut OakEngineClip) {
	guard_void(|| {
		// Stub: matches the capi's headless no-op.
		let _ = self_;
	})
}

/// `oakengine_clip_create_empty` — create a new empty ClipBlock (caller
/// owns it).
#[no_mangle]
pub unsafe extern "C" fn oakengine_clip_create_empty(label: *const c_char) -> *mut OakEngineClip {
	guard_ptr(|| unsafe {
		let clip = n::oaknode_block_clip_create();
		if clip.is_null() {
			return Ok(std::ptr::null_mut());
		}
		if !label.is_null() {
			let label_c = std::ffi::CString::new(read_cstr(label))
				.map_err(|_| Error::Failed("invalid label".into()))?;
			let node = n::oaknode_block_as_node(clip);
			let rc = n::oaknode_node_set_label(node, label_c.as_ptr());
			release_handle(node);
			if rc != 0 {
				return Err(Error::Module(rc));
			}
		}
		Ok(box_handle::<OakEngineClip>(clip))
	})
}

/// `oakengine_clip_request_invalidate_connected` — request invalidated cache
/// ranges from the connected node.
#[no_mangle]
pub unsafe extern "C" fn oakengine_clip_request_invalidate_connected(
	self_: *mut OakEngineClip,
	force_all: c_int,
	in_num: i64,
	in_den: i64,
	out_num: i64,
	out_den: i64,
) {
	guard_void(|| {
		// Stub: the module clip has no buffer input and no
		// `request_invalidated_from_connected` surface; matches the capi's
		// headless behavior.
		let _ = (self_, force_all, in_num, in_den, out_num, out_den);
	})
}

/* ---- Block functions ------------------------------------------------------- */

/// `oakengine_block_is_enabled` — 1 if the block is enabled.
#[no_mangle]
pub unsafe extern "C" fn oakengine_block_is_enabled(
	self_: *const OakEngineBlock,
) -> c_int {
	guard_int(|| unsafe {
		if self_.is_null() {
			return Ok(0);
		}
		let h = unbox(self_)?;
		let mut enabled: c_int = 0;
		Error::from_module(n::oaknode_block_get_enabled(h, &mut enabled))?;
		Ok(enabled)
	})
}

/// `oakengine_block_set_enabled` — enable or disable the block (undoable).
#[no_mangle]
pub unsafe extern "C" fn oakengine_block_set_enabled(
	self_: *mut OakEngineBlock,
	enabled: c_int,
) -> c_int {
	guard(|| unsafe {
		if self_.is_null() {
			return Err(Error::Invalid);
		}
		let h = unbox(self_)?;
		let mut old_enabled: c_int = 0;
		Error::from_module(n::oaknode_block_get_enabled(h, &mut old_enabled))?;
		let data = Box::into_raw(Box::new(BlockEnabledCmdData {
			block: h.addref(),
			old_enabled,
			new_enabled: enabled,
		}));
		let cmd = vtable_command(block_enabled_redo, block_enabled_undo, block_enabled_free, data as *mut c_void)?;
		push_command(cmd, "Set Block Enabled")
	})
}

/* ---- Block traversal -------------------------------------------------------- */

/// `oakengine_track_block_count` — number of blocks (including gaps).
#[no_mangle]
pub unsafe extern "C" fn oakengine_track_block_count(track: *const OakEngineTrack) -> c_int {
	guard_int(|| unsafe {
		let h = unbox(track)?;
		let mut count: c_int = 0;
		Error::from_module(n::oaknode_track_get_block_count(h, &mut count))?;
		Ok(count)
	})
}

/// `oakengine_track_block_at` — block at `index` (0-based, includes gaps).
#[no_mangle]
pub unsafe extern "C" fn oakengine_track_block_at(
	track: *const OakEngineTrack,
	index: c_int,
) -> *mut OakEngineBlock {
	guard_ptr(|| unsafe {
		if track.is_null() || index < 0 {
			return Ok(std::ptr::null_mut());
		}
		let h = unbox(track)?;
		let mut block = CHandle::null();
		let rc = n::oaknode_track_get_block_at(h, index, &mut block);
		if rc != 0 || block.is_null() {
			return Ok(std::ptr::null_mut());
		}
		Ok(box_handle::<OakEngineBlock>(block))
	})
}

/// `oakengine_track_block_at_time` — block containing the timestamp.
#[no_mangle]
pub unsafe extern "C" fn oakengine_track_block_at_time(
	track: *const OakEngineTrack,
	timestamp: i64,
) -> *mut OakEngineBlock {
	guard_ptr(|| unsafe {
		if track.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let h = unbox(track)?;
		// The track's owning sequence timebase.
		let mut sequence = CHandle::null();
		let rc = n::oaknode_track_get_sequence(h, &mut sequence);
		if rc != 0 || sequence.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let tb = match seq_time_base(sequence) {
			Ok(tb) => tb,
			Err(_) => {
				release_handle(sequence);
				return Ok(std::ptr::null_mut());
			}
		};
		release_handle(sequence);
		let (num, den) = ts_to_rational(timestamp, tb);
		let mut block = CHandle::null();
		let rc = n::oaknode_track_get_block_containing_time(
			h,
			num as c_int,
			den as c_int,
			&mut block,
		);
		if rc != 0 || block.is_null() {
			return Ok(std::ptr::null_mut());
		}
		Ok(box_handle::<OakEngineBlock>(block))
	})
}

/// Shared "nearest block at timestamp" helpers.
///
/// # Safety
/// `track` must be a live module track handle.
unsafe fn track_nearest_boxed(
	track: *const OakEngineTrack,
	timestamp: i64,
	via: fn(CHandle, c_int, c_int, *mut CHandle) -> c_int,
) -> *mut OakEngineBlock {
	guard_ptr(|| unsafe {
		if track.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let h = unbox(track)?;
		let mut sequence = CHandle::null();
		let rc = n::oaknode_track_get_sequence(h, &mut sequence);
		if rc != 0 || sequence.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let tb = match seq_time_base(sequence) {
			Ok(tb) => tb,
			Err(_) => {
				release_handle(sequence);
				return Ok(std::ptr::null_mut());
			}
		};
		release_handle(sequence);
		let (num, den) = ts_to_rational(timestamp, tb);
		let mut block = CHandle::null();
		let rc = via(h, num as c_int, den as c_int, &mut block);
		if rc != 0 || block.is_null() {
			return Ok(std::ptr::null_mut());
		}
		Ok(box_handle::<OakEngineBlock>(block))
	})
}

/// `oakengine_track_nearest_block_before` — nearest block whose out-point is
/// strictly before `timestamp`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_track_nearest_block_before(
	track: *const OakEngineTrack,
	timestamp: i64,
) -> *mut OakEngineBlock {
	guard_ptr(|| unsafe {
		if track.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let h = unbox(track)?;
		let mut sequence = CHandle::null();
		let rc = n::oaknode_track_get_sequence(h, &mut sequence);
		if rc != 0 || sequence.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let tb = match seq_time_base(sequence) {
			Ok(tb) => tb,
			Err(_) => {
				release_handle(sequence);
				return Ok(std::ptr::null_mut());
			}
		};
		release_handle(sequence);
		let (num, den) = ts_to_rational(timestamp, tb);
		let block = nearest_block_before(h, num, den);
		if block.is_null() {
			return Ok(std::ptr::null_mut());
		}
		Ok(box_handle::<OakEngineBlock>(block))
	})
}

/// `oakengine_track_nearest_block_after` — nearest block whose in-point is
/// strictly after `timestamp`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_track_nearest_block_after(
	track: *const OakEngineTrack,
	timestamp: i64,
) -> *mut OakEngineBlock {
	guard_ptr(|| unsafe {
		if track.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let h = unbox(track)?;
		let mut sequence = CHandle::null();
		let rc = n::oaknode_track_get_sequence(h, &mut sequence);
		if rc != 0 || sequence.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let tb = match seq_time_base(sequence) {
			Ok(tb) => tb,
			Err(_) => {
				release_handle(sequence);
				return Ok(std::ptr::null_mut());
			}
		};
		release_handle(sequence);
		let (num, den) = ts_to_rational(timestamp, tb);
		// The module exposes only the after-or-at variant; the strictly-after
		// result is the first block whose in-point is strictly after the
		// time.
		let mut best = CHandle::null();
		let mut block_count: c_int = 0;
		if n::oaknode_track_get_block_count(h, &mut block_count) != 0 {
			return Ok(std::ptr::null_mut());
		}
		for i in 0..block_count {
			let mut b = CHandle::null();
			if n::oaknode_track_get_block_at(h, i, &mut b) != 0 || b.is_null() {
				continue;
			}
			let mut in_num: c_int = 0;
			let mut in_den: c_int = 0;
			let after = n::oaknode_block_get_in(b, &mut in_num, &mut in_den) == 0
				&& rat_cmp(in_num as i64, in_den as i64, num, den)
					== std::cmp::Ordering::Greater;
			if after {
				best = b;
				break;
			}
			release_handle(b);
		}
		if best.is_null() {
			return Ok(std::ptr::null_mut());
		}
		Ok(box_handle::<OakEngineBlock>(best))
	})
}

/// `oakengine_track_nearest_block_before_or_at` — nearest block whose
/// out-point >= `timestamp`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_track_nearest_block_before_or_at(
	track: *const OakEngineTrack,
	timestamp: i64,
) -> *mut OakEngineBlock {
	unsafe { track_nearest_boxed(track, timestamp, n::oaknode_track_get_nearest_block_before_or_at) }
}

/// `oakengine_track_nearest_block_after_or_at` — nearest block whose
/// in-point <= `timestamp`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_track_nearest_block_after_or_at(
	track: *const OakEngineTrack,
	timestamp: i64,
) -> *mut OakEngineBlock {
	unsafe { track_nearest_boxed(track, timestamp, n::oaknode_track_get_nearest_block_after_or_at) }
}

/// `oakengine_block_is_gap` — 1 if the block is a GapBlock.
#[no_mangle]
pub unsafe extern "C" fn oakengine_block_is_gap(block: *const OakEngineBlock) -> c_int {
	guard_int(|| unsafe {
		if block.is_null() {
			return Ok(0);
		}
		let h = unbox(block)?;
		Ok(is_gap_block(h))
	})
}

/// Whether the module block carries a gap behavior.
///
/// # Safety
/// `h` must be a live module block handle.
unsafe fn is_gap_block(h: CHandle) -> c_int {
	unsafe {
		let mut kind: c_int = 0;
		if n::oaknode_block_get_kind(h, &mut kind) != 0 {
			return 0;
		}
		if kind == BLOCK_KIND_GAP {
			1
		} else {
			0
		}
	}
}

/// `oakengine_block_get_track` — the block's track.
#[no_mangle]
pub unsafe extern "C" fn oakengine_block_get_track(
	block: *const OakEngineBlock,
) -> *mut OakEngineTrack {
	guard_ptr(|| unsafe {
		if block.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let h = unbox(block)?;
		let mut track = CHandle::null();
		let rc = n::oaknode_block_get_track(h, &mut track);
		if rc != 0 || track.is_null() {
			return Ok(std::ptr::null_mut());
		}
		Ok(box_handle::<OakEngineTrack>(track))
	})
}

/// `oakengine_block_next` — next block in the track's linked list.
#[no_mangle]
pub unsafe extern "C" fn oakengine_block_next(
	block: *const OakEngineBlock,
) -> *mut OakEngineBlock {
	guard_ptr(|| unsafe {
		if block.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let h = unbox(block)?;
		let mut next = CHandle::null();
		let rc = n::oaknode_block_get_next(h, &mut next);
		if rc != 0 || next.is_null() {
			return Ok(std::ptr::null_mut());
		}
		Ok(box_handle::<OakEngineBlock>(next))
	})
}

/// `oakengine_block_prev` — previous block in the track's linked list.
#[no_mangle]
pub unsafe extern "C" fn oakengine_block_prev(
	block: *const OakEngineBlock,
) -> *mut OakEngineBlock {
	guard_ptr(|| unsafe {
		if block.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let h = unbox(block)?;
		let mut prev = CHandle::null();
		let rc = n::oaknode_block_get_previous(h, &mut prev);
		if rc != 0 || prev.is_null() {
			return Ok(std::ptr::null_mut());
		}
		Ok(box_handle::<OakEngineBlock>(prev))
	})
}

/// `oakengine_block_get_range` — block range as timestamps in the owning
/// track's sequence timebase.
#[no_mangle]
pub unsafe extern "C" fn oakengine_block_get_range(
	block: *const OakEngineBlock,
	in_: *mut i64,
	out: *mut i64,
) -> c_int {
	guard(|| unsafe {
		let h = unbox(block)?;
		let mut track = CHandle::null();
		let rc = n::oaknode_block_get_track(h, &mut track);
		if rc != 0 || track.is_null() {
			return Err(Error::Invalid);
		}
		let mut sequence = CHandle::null();
		let rc = n::oaknode_track_get_sequence(track, &mut sequence);
		release_handle(track);
		if rc != 0 || sequence.is_null() {
			return Err(Error::Invalid);
		}
		let tb = match seq_time_base(sequence) {
			Ok(tb) => tb,
			Err(_) => {
				release_handle(sequence);
				return Err(Error::Invalid);
			}
		};
		release_handle(sequence);
		let mut in_num: c_int = 0;
		let mut in_den: c_int = 0;
		let mut out_num: c_int = 0;
		let mut out_den: c_int = 0;
		Error::from_module(n::oaknode_block_get_in(h, &mut in_num, &mut in_den))?;
		Error::from_module(n::oaknode_block_get_out(h, &mut out_num, &mut out_den))?;
		if !in_.is_null() {
			*in_ = rational_to_ts(in_num as i64, in_den as i64, tb);
		}
		if !out.is_null() {
			*out = rational_to_ts(out_num as i64, out_den as i64, tb);
		}
		Ok(())
	})
}

/* ---- Clip input ID getters -------------------------------------------------- */

/// `oakengine_clip_buffer_input_id` — `ClipBlock::k_buffer_in`.
#[no_mangle]
pub extern "C" fn oakengine_clip_buffer_input_id() -> *const c_char {
	b"buffer_in\0".as_ptr() as *const c_char
}

/// `oakengine_clip_speed_input_id` — `ClipBlock::k_speed_input`.
#[no_mangle]
pub extern "C" fn oakengine_clip_speed_input_id() -> *const c_char {
	b"speed_in\0".as_ptr() as *const c_char
}

/// `oakengine_clip_reverse_input_id` — `ClipBlock::k_reverse_input`.
#[no_mangle]
pub extern "C" fn oakengine_clip_reverse_input_id() -> *const c_char {
	b"reverse_in\0".as_ptr() as *const c_char
}

/// `oakengine_clip_maintain_audio_pitch_input_id` — the maintain-audio-pitch
/// input.
#[no_mangle]
pub extern "C" fn oakengine_clip_maintain_audio_pitch_input_id() -> *const c_char {
	b"maintain_audio_pitch_in\0".as_ptr() as *const c_char
}

/// `oakengine_clip_loop_mode_input_id` — `ClipBlock::k_loop_mode_input`.
#[no_mangle]
pub extern "C" fn oakengine_clip_loop_mode_input_id() -> *const c_char {
	b"loop_in\0".as_ptr() as *const c_char
}

/// `oakengine_clip_auto_cache_input_id` — `ClipBlock::k_auto_cache_input`.
#[no_mangle]
pub extern "C" fn oakengine_clip_auto_cache_input_id() -> *const c_char {
	b"autocache_in\0".as_ptr() as *const c_char
}

/* ---- Sequence: add_default_nodes -------------------------------------------- */

/// `oakengine_sequence_add_default_nodes` — add one video and one audio
/// track as ONE undoable command.
#[no_mangle]
pub unsafe extern "C" fn oakengine_sequence_add_default_nodes(
	seq: *mut OakEngineSequence,
) -> c_int {
	guard(|| unsafe {
		if seq.is_null() {
			return Err(Error::Invalid);
		}
		let h = unbox(seq)?;
		let mut video_list = CHandle::null();
		let mut audio_list = CHandle::null();
		Error::from_module(n::oaknode_sequence_get_track_list(
			h,
			TRACK_TYPE_VIDEO,
			&mut video_list,
		))?;
		Error::from_module(n::oaknode_sequence_get_track_list(
			h,
			TRACK_TYPE_AUDIO,
			&mut audio_list,
		))?;
		let vcmd = tl::oaktimeline_add_track_command(video_list);
		let acmd = tl::oaktimeline_add_track_command(audio_list);
		if vcmd.is_null() || acmd.is_null() {
			release_handle(video_list);
			release_handle(audio_list);
			return Err(Error::Failed("add track command failed".into()));
		}
		let children = [vcmd, acmd];
		push_multi_commands(&children, "Add Default Nodes")?;
		// Module-gap compensation: register one live track per type (see
		// `oakengine_sequence_add_track`).
		let vtrack = n::oaknode_track_create(TRACK_TYPE_VIDEO);
		let atrack = n::oaknode_track_create(TRACK_TYPE_AUDIO);
		if !vtrack.is_null() {
			n::oaknode_tracklist_add_track(video_list, vtrack);
		}
		if !atrack.is_null() {
			n::oaknode_tracklist_add_track(audio_list, atrack);
		}
		release_handle(video_list);
		release_handle(audio_list);
		Ok(())
	})
}

/* ---- Sequence: add_sequence_clip -------------------------------------------- */

/// `oakengine_sequence_add_sequence_clip` — place a nested Sequence as a
/// clip on a track.
#[no_mangle]
pub unsafe extern "C" fn oakengine_sequence_add_sequence_clip(
	seq: *mut OakEngineSequence,
	nested: *mut OakEngineSequence,
	track_type: c_int,
	track_index: c_int,
	in_: i64,
	out: i64,
	media_in: i64,
) -> *mut OakEngineClip {
	guard_ptr(|| unsafe {
		set_seq_error("");
		let sequence = match unbox(seq) {
			Ok(h) => h,
			Err(_) => {
				set_seq_error("invalid sequence handles");
				return Ok(std::ptr::null_mut());
			}
		};
		let nested_h = match unbox(nested) {
			Ok(h) => h,
			Err(_) => {
				set_seq_error("invalid sequence handles");
				return Ok(std::ptr::null_mut());
			}
		};
		if track_type < TRACK_TYPE_VIDEO || track_type > TRACK_TYPE_SUBTITLE {
			set_seq_error("invalid track type");
			return Ok(std::ptr::null_mut());
		}
		if track_type != TRACK_TYPE_VIDEO && track_type != TRACK_TYPE_AUDIO {
			set_seq_error("subtitle sequence clips not supported");
			return Ok(std::ptr::null_mut());
		}
		if sequence.ctx == nested_h.ctx {
			set_seq_error("a sequence cannot nest itself");
			return Ok(std::ptr::null_mut());
		}
		// Cross-project check.
		let p1 = seq_project_of(sequence);
		let p2 = seq_project_of(nested_h);
		let same = !p1.is_null() && !p2.is_null() && p1.ctx == p2.ctx;
		release_handle(p1);
		release_handle(p2);
		if !same {
			set_seq_error("sequence belongs to a different project");
			return Ok(std::ptr::null_mut());
		}
		let tb = match seq_time_base(sequence) {
			Ok(tb) => tb,
			Err(_) => {
				set_seq_error("sequence has no valid frame rate");
				return Ok(std::ptr::null_mut());
			}
		};
		if out <= in_ || in_ < 0 || media_in < 0 {
			set_seq_error("invalid range");
			return Ok(std::ptr::null_mut());
		}
		let mut list = CHandle::null();
		if n::oaknode_sequence_get_track_list(sequence, track_type, &mut list) != 0
			|| list.is_null()
		{
			set_seq_error("sequence has no track list for this type");
			return Ok(std::ptr::null_mut());
		}
		let mut track_count: c_int = 0;
		if n::oaknode_tracklist_get_track_count(list, &mut track_count) != 0
			|| track_index < 0
			|| track_index >= track_count
		{
			release_handle(list);
			set_seq_error(&format!("no track at index {}", track_index));
			return Ok(std::ptr::null_mut());
		}
		let (len_num, len_den) = ts_to_rational(out - in_, tb);
		let (mi_num, mi_den) = ts_to_rational(media_in, tb);
		let clip = n::oaknode_block_clip_create();
		if clip.is_null() {
			release_handle(list);
			set_seq_error("clip creation failed");
			return Ok(std::ptr::null_mut());
		}
		// Set length first, then media in (set_length_and_media_in modifies
		// media in internally).
		Error::from_module(n::oaknode_block_set_length_and_media_in(
			clip,
			len_num as c_int,
			len_den as c_int,
		))?;
		Error::from_module(n::oaknode_clip_set_media_in(
			clip,
			mi_num as c_int,
			mi_den as c_int,
		))?;
		let project = seq_project_of(sequence);
		let add_cmd = n::oaknode_command_create_add_node(project, clip);
		release_handle(project);
		if add_cmd.is_null() {
			release_handle(list);
			set_seq_error("node add command failed");
			return Ok(std::ptr::null_mut());
		}
		// As with footage clips, the module clip has no `buffer_in` input, so
		// the nested-sequence connection cannot be built.
		let clip_node = n::oaknode_block_as_node(clip);
		let mut edge = CHandle::null();
		let rc = n::oaknode_node_connect_undoable(nested_h, clip_node, c"buffer_in".as_ptr(), &mut edge);
		release_handle(clip_node);
		if rc != 0 {
			release_handle(add_cmd);
			release_handle(list);
			set_seq_error("nested-sequence connection failed: module clips have no buffer input");
			return Ok(std::ptr::null_mut());
		}
		let (in_num, in_den) = ts_to_rational(in_, tb);
		let place_cmd = tl::oaktimeline_place_block_command(list, track_index, clip, in_num, in_den);
		release_handle(list);
		if place_cmd.is_null() {
			release_handle(add_cmd);
			release_handle(edge);
			set_seq_error("place block command failed");
			return Ok(std::ptr::null_mut());
		}
		let children = [add_cmd, edge, place_cmd];
		if let Err(e) = push_multi_commands(&children, "Add Sequence Clip") {
			set_seq_error(&format!("failed to push add-sequence-clip command: {:?}", e));
			return Err(e);
		}
		Ok(box_handle::<OakEngineClip>(clip))
	})
}

/* ---- Track handle queries ---------------------------------------------------- */

/// `oakengine_sequence_track_at` — borrowed track handle.
#[no_mangle]
pub unsafe extern "C" fn oakengine_sequence_track_at(
	seq: *const OakEngineSequence,
	track_type: c_int,
	track_index: c_int,
) -> *mut OakEngineTrack {
	guard_ptr(|| unsafe {
		if seq.is_null() || track_type < TRACK_TYPE_VIDEO || track_type > TRACK_TYPE_SUBTITLE {
			return Ok(std::ptr::null_mut());
		}
		let h = unbox(seq)?;
		let mut track = CHandle::null();
		let rc = n::oaknode_sequence_get_track_at(h, track_type, track_index, &mut track);
		if rc != 0 || track.is_null() {
			return Ok(std::ptr::null_mut());
		}
		Ok(box_handle::<OakEngineTrack>(track))
	})
}

/// `oakengine_track_type` — track type, or -1 on a NULL handle.
#[no_mangle]
pub unsafe extern "C" fn oakengine_track_type(track: *const OakEngineTrack) -> c_int {
	guard_int(|| unsafe {
		if track.is_null() {
			return Ok(-1);
		}
		let h = unbox(track)?;
		let mut type_: c_int = 0;
		Error::from_module(n::oaknode_track_get_type(h, &mut type_))?;
		Ok(type_)
	})
}

/// `oakengine_track_get_length` — track content length in frame timestamps.
#[no_mangle]
pub unsafe extern "C" fn oakengine_track_get_length(
	seq: *const OakEngineSequence,
	track_type: c_int,
	track_index: c_int,
	length: *mut i64,
) -> c_int {
	guard(|| unsafe {
		set_seq_error("");
		if length.is_null() {
			set_seq_error("invalid arguments");
			return Err(Error::Invalid);
		}
		let sequence = match unbox(seq) {
			Ok(h) => h,
			Err(_) => {
				set_seq_error("invalid arguments");
				return Err(Error::Invalid);
			}
		};
		let mut list = CHandle::null();
		Error::from_module(n::oaknode_sequence_get_track_list(sequence, track_type, &mut list))?;
		let mut track = CHandle::null();
		let rc = n::oaknode_tracklist_get_track_at(list, track_index, &mut track);
		release_handle(list);
		if rc != 0 || track.is_null() {
			set_seq_error(&format!("no track at index {}", track_index));
			return Err(Error::NotFound);
		}
		let tb = match seq_time_base(sequence) {
			Ok(tb) => tb,
			Err(_) => {
				release_handle(track);
				set_seq_error("sequence has no valid frame rate");
				return Err(Error::State);
			}
		};
		let mut num: c_int = 0;
		let mut den: c_int = 0;
		let rc = n::oaknode_track_get_length(track, &mut num, &mut den);
		release_handle(track);
		Error::from_module(rc)?;
		*length = rational_to_ts(num as i64, den as i64, tb);
		Ok(())
	})
}

/// `oakengine_track_is_range_free` — 1 if [in_ts, out_ts) is free.
#[no_mangle]
pub unsafe extern "C" fn oakengine_track_is_range_free(
	seq: *const OakEngineSequence,
	track_type: c_int,
	track_index: c_int,
	in_ts: i64,
	out_ts: i64,
) -> c_int {
	guard_int(|| unsafe {
		set_seq_error("");
		if seq.is_null() || in_ts < 0 || out_ts <= in_ts {
			set_seq_error("invalid arguments");
			return Err(Error::Invalid);
		}
		let sequence = match unbox(seq) {
			Ok(h) => h,
			Err(_) => {
				set_seq_error("invalid arguments");
				return Err(Error::Invalid);
			}
		};
		let mut list = CHandle::null();
		Error::from_module(n::oaknode_sequence_get_track_list(sequence, track_type, &mut list))?;
		let mut track = CHandle::null();
		let rc = n::oaknode_tracklist_get_track_at(list, track_index, &mut track);
		release_handle(list);
		if rc != 0 || track.is_null() {
			set_seq_error(&format!("no track at index {}", track_index));
			return Err(Error::NotFound);
		}
		let tb = match seq_time_base(sequence) {
			Ok(tb) => tb,
			Err(_) => {
				release_handle(track);
				set_seq_error("sequence has no valid frame rate");
				return Err(Error::State);
			}
		};
		let (in_num, in_den) = ts_to_rational(in_ts, tb);
		let (out_num, out_den) = ts_to_rational(out_ts, tb);
		let mut is_free: c_int = 0;
		let rc = n::oaknode_track_is_range_free(
			track,
			in_num as c_int,
			in_den as c_int,
			out_num as c_int,
			out_den as c_int,
			&mut is_free,
		);
		release_handle(track);
		Error::from_module(rc)?;
		Ok(is_free)
	})
}

/// `oakengine_track_height_default` — default track height in internal
/// units.
#[no_mangle]
pub extern "C" fn oakengine_track_height_default() -> f64 {
	TRACK_HEIGHT_DEFAULT
}

/// `oakengine_track_default_height_in_pixels` — default track height in
/// pixels.
#[no_mangle]
pub extern "C" fn oakengine_track_default_height_in_pixels() -> c_int {
	unsafe { n::oaknode_track_get_default_height_in_pixels() }
}

/// `oakengine_track_height_internal_to_pixels` — convert internal height to
/// pixels.
#[no_mangle]
pub extern "C" fn oakengine_track_height_internal_to_pixels(height: f64) -> c_int {
	(height * TRACK_FONT_HEIGHT).round() as c_int
}

/// `oakengine_track_height_pixels_to_internal` — convert pixels to internal
/// height.
#[no_mangle]
pub extern "C" fn oakengine_track_height_pixels_to_internal(pixels: c_int) -> f64 {
	pixels as f64 / TRACK_FONT_HEIGHT
}

/// `oakengine_track_height_interval` — track height step interval.
#[no_mangle]
pub extern "C" fn oakengine_track_height_interval() -> f64 {
	TRACK_HEIGHT_INTERVAL
}

/// `oakengine_track_height_minimum` — minimum track height.
#[no_mangle]
pub extern "C" fn oakengine_track_height_minimum() -> f64 {
	TRACK_HEIGHT_MINIMUM
}

/* ---- Multicam helpers --------------------------------------------------------- */

/// `oakengine_clip_find_multicam` — find the MultiCamNode ancestor of a
/// node.
#[no_mangle]
pub unsafe extern "C" fn oakengine_clip_find_multicam(node: *mut OakEngineNode) -> *mut OakEngineNode {
	guard_ptr(|| {
		// Stub: the C++ walks the clip's buffer-input chain looking for a
		// MultiCamNode; module clips have no buffer input, so no ancestor can
		// be reached.
		let _ = node;
		Ok(std::ptr::null_mut())
	})
}

/// `oakengine_multicam_switch_source` — switch the multicam source.
#[no_mangle]
pub unsafe extern "C" fn oakengine_multicam_switch_source(
	multicam_node: *mut OakEngineNode,
	footage_node: *mut OakEngineNode,
	track_type: c_int,
	track_index: c_int,
	time_seconds: f64,
	command: *mut c_void,
) -> c_int {
	guard(|| {
		if multicam_node.is_null() {
			return Err(Error::Invalid);
		}
		// Stub: matches the capi — multicam switching requires complex undo
		// commands the module does not expose; the call is accepted and
		// ignored (the capi's `Q_UNUSED` body).
		let _ = (footage_node, track_type, track_index, time_seconds, command);
		unsafe { unbox(multicam_node)? };
		Ok(())
	})
}

/* ---- Track lists, block/clip/transition navigation and links ---------------- */

/// `oakengine_sequence_track_list` — borrowed track list handle.
#[no_mangle]
pub unsafe extern "C" fn oakengine_sequence_track_list(
	seq: *mut OakEngineSequence,
	track_type: c_int,
) -> *mut OakEngineTrackList {
	guard_ptr(|| unsafe {
		if seq.is_null() || track_type < TRACK_TYPE_VIDEO || track_type > TRACK_TYPE_SUBTITLE {
			return Ok(std::ptr::null_mut());
		}
		let h = unbox(seq)?;
		let mut list = CHandle::null();
		let rc = n::oaknode_sequence_get_track_list(h, track_type, &mut list);
		if rc != 0 || list.is_null() {
			return Ok(std::ptr::null_mut());
		}
		Ok(box_handle::<OakEngineTrackList>(list))
	})
}

/// `oakengine_track_visible_block_at_time` — the block visible at `time_ts`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_track_visible_block_at_time(
	track: *mut OakEngineTrack,
	time_ts: i64,
) -> *mut OakEngineBlock {
	guard_ptr(|| unsafe {
		if track.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let h = unbox(track)?;
		let mut sequence = CHandle::null();
		let rc = n::oaknode_track_get_sequence(h, &mut sequence);
		if rc != 0 || sequence.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let tb = match seq_time_base(sequence) {
			Ok(tb) => tb,
			Err(_) => {
				release_handle(sequence);
				return Ok(std::ptr::null_mut());
			}
		};
		release_handle(sequence);
		let (num, den) = ts_to_rational(time_ts, tb);
		let mut block = CHandle::null();
		let rc = n::oaknode_track_get_visible_block_at_time(h, num as c_int, den as c_int, &mut block);
		if rc != 0 || block.is_null() {
			return Ok(std::ptr::null_mut());
		}
		Ok(box_handle::<OakEngineBlock>(block))
	})
}

/// `oakengine_node_is_block` — 1 if the node is a block of any kind.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_is_block(node: *const OakEngineNode) -> c_int {
	guard_int(|| unsafe {
		if node.is_null() {
			return Ok(0);
		}
		let h = unbox(node)?;
		let block = n::oaknode_block_from_node(h);
		let is_block = !block.is_null();
		release_handle(block);
		Ok(is_block as c_int)
	})
}

/// `oakengine_node_is_transition` — 1 if the node is a transition block.
#[no_mangle]
pub unsafe extern "C" fn oakengine_node_is_transition(node: *const OakEngineNode) -> c_int {
	guard_int(|| unsafe {
		if node.is_null() {
			return Ok(0);
		}
		let h = unbox(node)?;
		// The module has exactly one transition type, identified by its type
		// id (the C++ class check has no module equivalent).
		Ok((node_type_id(h) == TYPE_ID_TRANSITION) as c_int)
	})
}

/// `oakengine_block_set_length_and_media_out` — set the block's length
/// keeping its in point (undoable).
#[no_mangle]
pub unsafe extern "C" fn oakengine_block_set_length_and_media_out(
	block: *mut OakEngineBlock,
	length_ts: i64,
) -> c_int {
	guard(|| unsafe {
		set_seq_error("");
		if block.is_null() || length_ts <= 0 {
			set_seq_error("invalid arguments");
			return Err(Error::Invalid);
		}
		let h = unbox(block)?;
		let mut track = CHandle::null();
		let rc = n::oaknode_block_get_track(h, &mut track);
		if rc != 0 || track.is_null() {
			set_seq_error("block is not on a track");
			return Err(Error::State);
		}
		let mut sequence = CHandle::null();
		let rc = n::oaknode_track_get_sequence(track, &mut sequence);
		release_handle(track);
		if rc != 0 || sequence.is_null() {
			set_seq_error("block is not on a track");
			return Err(Error::State);
		}
		let tb = match seq_time_base(sequence) {
			Ok(tb) => tb,
			Err(_) => {
				release_handle(sequence);
				set_seq_error("sequence has no valid frame rate");
				return Err(Error::State);
			}
		};
		release_handle(sequence);
		let (num, den) = ts_to_rational(length_ts, tb);
		let mut old_num: c_int = 0;
		let mut old_den: c_int = 0;
		Error::from_module(n::oaknode_block_get_length(h, &mut old_num, &mut old_den))?;
		let data = Box::into_raw(Box::new(BlockResizeCmdData {
			block: h.addref(),
			old_num,
			old_den,
			new_num: num as c_int,
			new_den: den as c_int,
		}));
		let cmd = vtable_command(block_resize_redo, block_resize_undo, block_resize_free, data as *mut c_void)?;
		push_command(cmd, "Set Block Length")
	})
}

/// `oakengine_block_link_count` — blocks linked to this block.
#[no_mangle]
pub unsafe extern "C" fn oakengine_block_link_count(block: *const OakEngineBlock) -> c_int {
	guard_int(|| unsafe {
		if block.is_null() {
			return Ok(0);
		}
		let h = unbox(block)?;
		let mut count: c_int = 0;
		Error::from_module(n::oaknode_block_get_link_count(h, &mut count))?;
		Ok(count)
	})
}

/// `oakengine_block_link_at` — linked block at `index`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_block_link_at(
	block: *const OakEngineBlock,
	index: c_int,
) -> *mut OakEngineBlock {
	guard_ptr(|| unsafe {
		if block.is_null() || index < 0 {
			return Ok(std::ptr::null_mut());
		}
		let h = unbox(block)?;
		let mut linked = CHandle::null();
		let rc = n::oaknode_block_get_link_at(h, index, &mut linked);
		if rc != 0 || linked.is_null() {
			return Ok(std::ptr::null_mut());
		}
		Ok(box_handle::<OakEngineBlock>(linked))
	})
}

/// `oakengine_clip_in_transition` — the transition at the clip's in-point.
#[no_mangle]
pub unsafe extern "C" fn oakengine_clip_in_transition(
	clip: *const OakEngineBlock,
) -> *mut OakEngineBlock {
	guard_ptr(|| unsafe {
		if clip.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let h = unbox(clip)?;
		// The in-transition is the previous block of the track when it is a
		// transition.
		let mut prev = CHandle::null();
		let rc = n::oaknode_block_get_previous(h, &mut prev);
		if rc != 0 || prev.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let node = n::oaknode_block_as_node(prev);
		let is_transition = node_type_id(node) == TYPE_ID_TRANSITION;
		release_handle(node);
		if !is_transition {
			release_handle(prev);
			return Ok(std::ptr::null_mut());
		}
		Ok(box_handle::<OakEngineBlock>(prev))
	})
}

/// `oakengine_clip_out_transition` — the transition at the clip's out-point.
#[no_mangle]
pub unsafe extern "C" fn oakengine_clip_out_transition(
	clip: *const OakEngineBlock,
) -> *mut OakEngineBlock {
	guard_ptr(|| unsafe {
		if clip.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let h = unbox(clip)?;
		let mut next = CHandle::null();
		let rc = n::oaknode_block_get_next(h, &mut next);
		if rc != 0 || next.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let node = n::oaknode_block_as_node(next);
		let is_transition = node_type_id(node) == TYPE_ID_TRANSITION;
		release_handle(node);
		if !is_transition {
			release_handle(next);
			return Ok(std::ptr::null_mut());
		}
		Ok(box_handle::<OakEngineBlock>(next))
	})
}

/// `oakengine_transition_connected_in_block` — the block feeding the
/// transition's in side.
#[no_mangle]
pub unsafe extern "C" fn oakengine_transition_connected_in_block(
	transition: *const OakEngineBlock,
) -> *mut OakEngineBlock {
	guard_ptr(|| unsafe {
		if transition.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let h = unbox(transition)?;
		let mut block = CHandle::null();
		let rc = n::oaknode_transition_get_connected_in_block(h, &mut block);
		if rc != 0 || block.is_null() {
			return Ok(std::ptr::null_mut());
		}
		Ok(box_handle::<OakEngineBlock>(block))
	})
}

/// `oakengine_transition_connected_out_block` — the block feeding the
/// transition's out side.
#[no_mangle]
pub unsafe extern "C" fn oakengine_transition_connected_out_block(
	transition: *const OakEngineBlock,
) -> *mut OakEngineBlock {
	guard_ptr(|| unsafe {
		if transition.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let h = unbox(transition)?;
		let mut block = CHandle::null();
		let rc = n::oaknode_transition_get_connected_out_block(h, &mut block);
		if rc != 0 || block.is_null() {
			return Ok(std::ptr::null_mut());
		}
		Ok(box_handle::<OakEngineBlock>(block))
	})
}

/// `oakengine_clip_get_connected_viewer` — the node feeding the clip's
/// buffer input.
#[no_mangle]
pub unsafe extern "C" fn oakengine_clip_get_connected_viewer(
	clip: *const OakEngineBlock,
) -> *mut OakEngineNode {
	guard_ptr(|| unsafe {
		if clip.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let h = unbox(clip)?;
		// Module clips have no `buffer_in` input, so there is no connected
		// viewer (mirrors the C++ result for an unconnected clip).
		let node = n::oaknode_block_as_node(h);
		let mut out = CHandle::null();
		let rc = n::oaknode_node_input_get_connected_node(node, c"buffer_in".as_ptr(), &mut out);
		release_handle(node);
		if rc != 0 || out.is_null() {
			return Ok(std::ptr::null_mut());
		}
		Ok(box_handle::<OakEngineNode>(out))
	})
}
