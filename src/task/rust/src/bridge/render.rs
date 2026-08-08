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

//! oakrender C ABI imports. The task module reaches the render side through
//! `include/render/cancelatom.h` (task cancellation), `include/render/ticket.h`
//! (frame/audio render tickets), `include/render/copier.h` (export project
//! copy) and `include/render/color.h` (color processor). Signatures mirror
//! the headers verbatim.

use std::ffi::{c_char, c_int, c_void};

use crate::bridge::codec::OakFrame;
use crate::bridge::common::{OakAudioParams, OakColorTransform, OakVideoParams};
use crate::bridge::node::{OakNodeColorManager, OakNodeNode, OakNodeProject};
use crate::handle::CHandle;

/// Mirror of `OakCancelAtom` (`include/render/cancelatom.h`).
#[repr(C)]
#[derive(Clone, Copy)]
pub struct OakCancelAtom {
	/// Opaque pointer to the reference-counted object.
	pub ctx: *mut c_void,
	/// Atomic increment.
	pub addref: Option<unsafe extern "C" fn(*mut c_void)>,
	/// Atomic decrement; destroys at zero.
	pub release: Option<unsafe extern "C" fn(*mut c_void)>,
	/// ABI version (`OAKRENDER_ABI_VERSION`).
	pub abi_version: u32,
}

impl OakCancelAtom {
	/// The empty (null) atom.
	pub fn null() -> Self {
		OakCancelAtom {
			ctx: std::ptr::null_mut(),
			addref: None,
			release: None,
			abi_version: 0,
		}
	}

	/// Whether the atom is empty (`ctx == NULL`).
	pub fn is_null(&self) -> bool {
		self.ctx.is_null()
	}

	/// Adopt the fields of a generic `CHandle` (layout-identical) — the task
	/// module stores the atom as a `CHandle` for its public API.
	pub fn from_chandle(h: CHandle) -> Self {
		OakCancelAtom {
			ctx: h.ctx,
			addref: h.addref,
			release: h.release,
			abi_version: h.abi_version,
		}
	}

	/// Convert back to a generic `CHandle` (layout-identical).
	pub fn to_chandle(&self) -> CHandle {
		CHandle {
			ctx: self.ctx,
			addref: self.addref,
			release: self.release,
			abi_version: self.abi_version,
		}
	}
}

/// Mirror of `OakRenderTicket` (`include/render/ticket.h`).
#[repr(C)]
#[derive(Clone, Copy)]
pub struct OakRenderTicket {
	/// Opaque pointer to the reference-counted object.
	pub ctx: *mut c_void,
	/// Atomic increment.
	pub addref: Option<unsafe extern "C" fn(*mut c_void)>,
	/// Atomic decrement; destroys at zero.
	pub release: Option<unsafe extern "C" fn(*mut c_void)>,
	/// ABI version (`OAKRENDER_ABI_VERSION`).
	pub abi_version: u32,
}

/// Mirror of `OakRenderCache` (`include/render/cache.h`).
#[repr(C)]
#[derive(Clone, Copy)]
pub struct OakRenderCache {
	/// Opaque pointer to the reference-counted object.
	pub ctx: *mut c_void,
	/// Atomic increment.
	pub addref: Option<unsafe extern "C" fn(*mut c_void)>,
	/// Atomic decrement; destroys at zero.
	pub release: Option<unsafe extern "C" fn(*mut c_void)>,
	/// ABI version (`OAKRENDER_ABI_VERSION`).
	pub abi_version: u32,
}

impl OakRenderCache {
	/// The empty (null) cache handle.
	pub fn null() -> Self {
		OakRenderCache {
			ctx: std::ptr::null_mut(),
			addref: None,
			release: None,
			abi_version: 0,
		}
	}

	/// Convert to the layout-identical generic `CHandle`.
	pub fn to_chandle(&self) -> CHandle {
		CHandle {
			ctx: self.ctx,
			addref: self.addref,
			release: self.release,
			abi_version: self.abi_version,
		}
	}

	/// Adopt the fields of a generic `CHandle` (layout-identical).
	pub fn from_chandle(h: CHandle) -> Self {
		OakRenderCache {
			ctx: h.ctx,
			addref: h.addref,
			release: h.release,
			abi_version: h.abi_version,
		}
	}
}

/// Mirror of `OakRenderProjectCopier` (`include/render/copier.h`).
#[repr(C)]
#[derive(Clone, Copy)]
pub struct OakRenderProjectCopier {
	/// Opaque pointer to the reference-counted object.
	pub ctx: *mut c_void,
	/// Atomic increment.
	pub addref: Option<unsafe extern "C" fn(*mut c_void)>,
	/// Atomic decrement; destroys at zero.
	pub release: Option<unsafe extern "C" fn(*mut c_void)>,
	/// ABI version (`OAKRENDER_ABI_VERSION`).
	pub abi_version: u32,
}

/// Mirror of `OakColorProcessor` (`include/render/color.h`).
#[repr(C)]
#[derive(Clone, Copy)]
pub struct OakColorProcessor {
	/// Opaque pointer to the reference-counted object.
	pub ctx: *mut c_void,
	/// Atomic increment.
	pub addref: Option<unsafe extern "C" fn(*mut c_void)>,
	/// Atomic decrement; destroys at zero.
	pub release: Option<unsafe extern "C" fn(*mut c_void)>,
	/// ABI version (`OAKRENDER_ABI_VERSION`).
	pub abi_version: u32,
}

impl OakColorProcessor {
	/// Convert to the layout-identical generic `CHandle`.
	pub fn to_chandle(&self) -> CHandle {
		CHandle {
			ctx: self.ctx,
			addref: self.addref,
			release: self.release,
			abi_version: self.abi_version,
		}
	}

	/// Adopt the fields of a generic `CHandle` (layout-identical).
	pub fn from_chandle(h: CHandle) -> Self {
		OakColorProcessor {
			ctx: h.ctx,
			addref: h.addref,
			release: h.release,
			abi_version: h.abi_version,
		}
	}
}

/// Mirror of `oakrender_ticket_finished_fn` (`include/render/ticket.h`).
///
/// Two arguments — `(ticket, userdata)` — mirroring the header verbatim.
/// The ticket is a borrowed copy of the submitter's handle (the submitter
/// keeps ownership and releases it); cancelled tickets fire with a NULL
/// result observed through `oakrender_ticket_get_frame`.
pub type OakRenderTicketFinishedFn = unsafe extern "C" fn(ticket: OakRenderTicket, userdata: *mut c_void);

/// Mirror of `oakrender_video_ticket_params` (`include/render/ticket.h`).
#[repr(C)]
pub struct OakRenderVideoTicketParams {
	/// Connected texture output node (borrowed).
	pub output_node: OakNodeNode,
	/// Video params by value (oakcommon handle).
	pub video_params: OakVideoParams,
	/// Borrowed oakcore audio params, may be null ctx.
	pub audio_params: *const OakAudioParams,
	/// Frame timestamp numerator (seconds rational).
	pub time_num: i64,
	/// Frame timestamp denominator.
	pub time_den: i64,
	/// Borrowed color manager; empty ctx = NULL.
	pub color_manager: OakNodeColorManager,
	/// `olive::RenderMode::Mode` as int.
	pub mode: c_int,
	/// Forced output width (0 = off).
	pub force_width: c_int,
	/// Forced output height (0 = off).
	pub force_height: c_int,
	/// Forced color matrix (used when `has_force_matrix` != 0).
	pub force_matrix: [f64; 16],
	/// Whether `force_matrix` is in effect.
	pub has_force_matrix: c_int,
	/// Forced pixel format (`PixelFormat` as int, -1 = off).
	pub force_format: c_int,
	/// Forced channel count (0 = off).
	pub force_channel_count: c_int,
	/// Forced color output (borrowed; empty ctx = none).
	pub force_color_output: OakColorProcessor,
	/// Forced color transform by value (empty ctx = default).
	pub force_color_transform: OakColorTransform,
	/// Borrowed frame cache; empty ctx = none.
	pub cache: OakRenderCache,
}

extern "C" {
	// --- cancelatom.h ---
	/// `oakrender_cancelatom_init`.
	pub fn oakrender_cancelatom_init() -> OakCancelAtom;
	/// `oakrender_cancelatom_free`.
	pub fn oakrender_cancelatom_free(atom: *mut OakCancelAtom);
	/// `oakrender_cancelatom_cancel`.
	pub fn oakrender_cancelatom_cancel(atom: OakCancelAtom) -> c_int;
	/// `oakrender_cancelatom_is_cancelled`.
	pub fn oakrender_cancelatom_is_cancelled(atom: OakCancelAtom, cancelled: *mut c_int) -> c_int;
	/// `oakrender_cancelatom_heard_cancel`.
	pub fn oakrender_cancelatom_heard_cancel(atom: OakCancelAtom, heard: *mut c_int) -> c_int;

	// --- ticket.h ---
	/// `oakrender_ticket_render_frame`.
	pub fn oakrender_ticket_render_frame(
		params: *const OakRenderVideoTicketParams,
		cb: Option<OakRenderTicketFinishedFn>,
		userdata: *mut c_void,
	) -> OakRenderTicket;
	/// `oakrender_ticket_render_audio`.
	pub fn oakrender_ticket_render_audio(
		output_node: OakNodeNode,
		in_num: i64,
		in_den: i64,
		out_num: i64,
		out_den: i64,
		params: *const OakAudioParams,
		mode: c_int,
		cb: Option<OakRenderTicketFinishedFn>,
		userdata: *mut c_void,
	) -> OakRenderTicket;
	/// `oakrender_ticket_is_finished`.
	pub fn oakrender_ticket_is_finished(ticket: OakRenderTicket) -> c_int;
	/// `oakrender_ticket_wait`.
	pub fn oakrender_ticket_wait(ticket: OakRenderTicket) -> c_int;
	/// `oakrender_ticket_cancel`.
	pub fn oakrender_ticket_cancel(ticket: OakRenderTicket) -> c_int;
	/// `oakrender_ticket_get_type`.
	pub fn oakrender_ticket_get_type(ticket: OakRenderTicket) -> c_int;
	/// `oakrender_ticket_get_frame`.
	pub fn oakrender_ticket_get_frame(ticket: OakRenderTicket, out: *mut OakFrame) -> c_int;
	/// `oakrender_ticket_get_time`.
	pub fn oakrender_ticket_get_time(ticket: OakRenderTicket, out_num: *mut i64, out_den: *mut i64) -> c_int;
	/// `oakrender_ticket_get_range`.
	pub fn oakrender_ticket_get_range(
		ticket: OakRenderTicket,
		in_num: *mut i64,
		in_den: *mut i64,
		out_num: *mut i64,
		out_den: *mut i64,
	) -> c_int;
	/// `oakrender_ticket_get_samples`.
	pub fn oakrender_ticket_get_samples(ticket: OakRenderTicket, out: *mut *mut std::ffi::c_void) -> c_int;
	/// `oakrender_ticket_free`.
	pub fn oakrender_ticket_free(ticket: *mut OakRenderTicket);

	// --- copier.h ---
	/// `oakrender_project_copier_create`.
	pub fn oakrender_project_copier_create() -> OakRenderProjectCopier;
	/// `oakrender_project_copier_free`.
	pub fn oakrender_project_copier_free(copier: *mut OakRenderProjectCopier);
	/// `oakrender_project_copier_set_project`.
	pub fn oakrender_project_copier_set_project(
		copier: OakRenderProjectCopier,
		project: OakNodeProject,
	) -> c_int;
	/// `oakrender_project_copier_get_copy`.
	pub fn oakrender_project_copier_get_copy(
		copier: OakRenderProjectCopier,
		original: OakNodeNode,
	) -> OakNodeNode;
	/// `oakrender_project_copier_get_copied_project`.
	pub fn oakrender_project_copier_get_copied_project(
		copier: OakRenderProjectCopier,
	) -> OakNodeProject;

	// --- color.h ---
	/// `oakrender_color_processor_create`.
	pub fn oakrender_color_processor_create(
		src_space: *const c_char,
		dst_space: *const c_char,
		display: *const c_char,
		view: *const c_char,
		look: *const c_char,
	) -> OakColorProcessor;
	/// `oakrender_color_processor_free`.
	pub fn oakrender_color_processor_free(processor: *mut OakColorProcessor);
	/// `oakrender_color_processor_is_valid`.
	pub fn oakrender_color_processor_is_valid(processor: OakColorProcessor) -> c_int;

	// --- renderer.h ---
	/// `oakrender_codec_frame_free` — release a frame handed out by a
	/// render ticket.
	pub fn oakrender_codec_frame_free(frame: *mut crate::bridge::codec::OakFrame);

	// --- manager.h ---
	/// `oakrender_manager_set_aggressive_gc`.
	pub fn oakrender_manager_set_aggressive_gc(enabled: c_int) -> c_int;

	// --- cache.h ---
	/// `oakrender_cache_get_invalidated_ranges` — two-stage range query on
	/// a frame cache; returns the number of ranges (or the required flat
	/// array size).
	pub fn oakrender_cache_get_invalidated_ranges(
		cache: OakRenderCache,
		in_num: i64,
		in_den: i64,
		out_num: i64,
		out_den: i64,
		flat: *mut i64,
		flat_size: c_int,
	) -> c_int;
	/// `oakrender_cache_free`.
	pub fn oakrender_cache_free(cache: *mut OakRenderCache);
}
