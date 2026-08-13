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
//!
//! Resolution is at link time (the crate's existing pattern, see
//! `bridge/codec.rs`): the symbols are satisfied by the real `liboakrender`
//! when it is linked into the same binary (the app links the module dylibs;
//! the `real-oakrender` integration test links the oakrender crate directly),
//! and by the `#[no_mangle]` stubs in `tests/common/mod.rs` in plain
//! `cargo test`. The real-oakrender ticket contract — in particular the
//! 2-argument `oakrender_ticket_finished_fn` — is verified by
//! `tests/render_real_integration_test.rs` against the actual exports.

use std::ffi::{c_char, c_int, c_void};

use crate::bridge::codec::OakFrame;
use crate::bridge::common::{OakAudioParams, OakColorTransform, OakVideoParams};
use crate::bridge::node::{OakNodeColorManager, OakNodeNode, OakNodeProject};
use crate::handle::CHandle;

/// Mirror of `OakCancelAtom` (`include/render/cancelatom.h`).
pub type OakCancelAtom = oakrender::ffi::OakCancelAtom;

/// Mirror of `OakRenderTicket` (`include/render/ticket.h`).
pub type OakRenderTicket = oakrender::ffi::OakRenderTicket;

/// Mirror of `OakRenderCache` (`include/render/cache.h`).
pub type OakRenderCache = oakrender::ffi::OakRenderCache;

/// Mirror of `OakRenderProjectCopier` (`include/render/copier.h`).
pub type OakRenderProjectCopier = oakrender::ffi::OakRenderProjectCopier;

/// Mirror of `OakColorProcessor` (`include/render/color.h`).
pub type OakColorProcessor = oakrender::ffi::OakColorProcessor;

/// Mirror of `oakrender_ticket_finished_fn` (`include/render/ticket.h`).
///
/// Two arguments — `(ticket, userdata)` — mirroring the header verbatim and
/// matching the oakrender implementation (`src/render/rust/src/ffi.rs`).
/// The ticket is a borrowed copy of the submitter's handle (the submitter
/// keeps ownership and releases it); cancelled tickets fire with a NULL
/// result observed through `oakrender_ticket_get_frame`.
pub type OakRenderTicketFinishedFn =
	unsafe extern "C" fn(ticket: OakRenderTicket, userdata: *mut c_void);

/// Mirror of `oakrender_video_ticket_params` (`include/render/ticket.h`).
pub type OakRenderVideoTicketParams = oakrender::ffi::OakVideoTicketParams;

/// Direct call into the `oakrender` crate (single-lib unification).
pub fn oakrender_cancelatom_init() -> OakCancelAtom {
	unsafe { oakrender::ffi::cancelatom::oakrender_cancelatom_init() }
}

/// Direct call into the `oakrender` crate (single-lib unification).
pub fn oakrender_cancelatom_free(atom: *mut OakCancelAtom) {
	unsafe { oakrender::ffi::cancelatom::oakrender_cancelatom_free(atom) }
}

/// Direct call into the `oakrender` crate (single-lib unification).
pub fn oakrender_cancelatom_cancel(atom: OakCancelAtom) -> c_int {
	unsafe { oakrender::ffi::cancelatom::oakrender_cancelatom_cancel(atom) }
}

/// Direct call into the `oakrender` crate (single-lib unification).
pub fn oakrender_cancelatom_is_cancelled(atom: OakCancelAtom, cancelled: *mut c_int) -> c_int {
	unsafe { oakrender::ffi::cancelatom::oakrender_cancelatom_is_cancelled(atom, cancelled) }
}

/// Direct call into the `oakrender` crate (single-lib unification).
pub fn oakrender_cancelatom_heard_cancel(atom: OakCancelAtom, heard: *mut c_int) -> c_int {
	unsafe { oakrender::ffi::cancelatom::oakrender_cancelatom_heard_cancel(atom, heard) }
}

/// Direct call into the `oakrender` crate (single-lib unification).
pub fn oakrender_ticket_render_frame(
	params: *const OakRenderVideoTicketParams,
	cb: Option<OakRenderTicketFinishedFn>,
	userdata: *mut c_void,
) -> OakRenderTicket {
	unsafe { oakrender::ffi::ticket::oakrender_ticket_render_frame(params, cb, userdata) }
}

/// Direct call into the `oakrender` crate (single-lib unification).
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
) -> OakRenderTicket {
	unsafe {
		oakrender::ffi::ticket::oakrender_ticket_render_audio(
			output_node,
			in_num,
			in_den,
			out_num,
			out_den,
			params,
			mode,
			cb,
			userdata,
			std::ptr::null(),
			0,
		)
	}
}

/// Direct call into the `oakrender` crate (single-lib unification).
pub fn oakrender_ticket_is_finished(ticket: OakRenderTicket) -> c_int {
	unsafe { oakrender::ffi::ticket::oakrender_ticket_is_finished(ticket) }
}

/// Direct call into the `oakrender` crate (single-lib unification).
pub fn oakrender_ticket_wait(ticket: OakRenderTicket) -> c_int {
	unsafe { oakrender::ffi::ticket::oakrender_ticket_wait(ticket) }
}

/// Direct call into the `oakrender` crate (single-lib unification).
pub fn oakrender_ticket_cancel(ticket: OakRenderTicket) -> c_int {
	unsafe { oakrender::ffi::ticket::oakrender_ticket_cancel(ticket) }
}

/// Direct call into the `oakrender` crate (single-lib unification).
pub fn oakrender_ticket_get_type(ticket: OakRenderTicket) -> c_int {
	unsafe { oakrender::ffi::ticket::oakrender_ticket_get_type(ticket) }
}

/// Direct call into the `oakrender` crate (single-lib unification).
pub fn oakrender_ticket_get_frame(ticket: OakRenderTicket, out: *mut OakFrame) -> c_int {
	unsafe { oakrender::ffi::ticket::oakrender_ticket_get_frame(ticket, out) }
}

/// Direct call into the `oakrender` crate (single-lib unification).
pub fn oakrender_ticket_get_time(
	ticket: OakRenderTicket,
	out_num: *mut i64,
	out_den: *mut i64,
) -> c_int {
	unsafe { oakrender::ffi::ticket::oakrender_ticket_get_time(ticket, out_num, out_den) }
}

/// Direct call into the `oakrender` crate (single-lib unification).
pub fn oakrender_ticket_get_range(
	ticket: OakRenderTicket,
	in_num: *mut i64,
	in_den: *mut i64,
	out_num: *mut i64,
	out_den: *mut i64,
) -> c_int {
	unsafe {
		oakrender::ffi::ticket::oakrender_ticket_get_range(ticket, in_num, in_den, out_num, out_den)
	}
}

/// Direct call into the `oakrender` crate (single-lib unification).
pub fn oakrender_ticket_get_samples(
	ticket: OakRenderTicket,
	out: *mut *mut std::ffi::c_void,
) -> c_int {
	unsafe { oakrender::ffi::ticket::oakrender_ticket_get_samples(ticket, out) }
}

/// Direct call into the `oakrender` crate (single-lib unification).
pub fn oakrender_ticket_free(ticket: *mut OakRenderTicket) {
	unsafe { oakrender::ffi::ticket::oakrender_ticket_free(ticket) }
}

/// Direct call into the `oakrender` crate (single-lib unification).
pub fn oakrender_project_copier_create() -> OakRenderProjectCopier {
	unsafe { oakrender::ffi::copier::oakrender_project_copier_create() }
}

/// Direct call into the `oakrender` crate (single-lib unification).
pub fn oakrender_project_copier_free(copier: *mut OakRenderProjectCopier) {
	unsafe { oakrender::ffi::copier::oakrender_project_copier_free(copier) }
}

/// Direct call into the `oakrender` crate (single-lib unification).
pub fn oakrender_project_copier_set_project(
	copier: OakRenderProjectCopier,
	project: OakNodeProject,
) -> c_int {
	unsafe { oakrender::ffi::copier::oakrender_project_copier_set_project(copier, project) }
}

/// Direct call into the `oakrender` crate (single-lib unification).
pub fn oakrender_project_copier_get_copy(
	copier: OakRenderProjectCopier,
	original: OakNodeNode,
) -> OakNodeNode {
	unsafe { oakrender::ffi::copier::oakrender_project_copier_get_copy(copier, original) }
}

/// Direct call into the `oakrender` crate (single-lib unification).
pub fn oakrender_project_copier_get_copied_project(
	copier: OakRenderProjectCopier,
) -> OakNodeProject {
	unsafe { oakrender::ffi::copier::oakrender_project_copier_get_copied_project(copier) }
}

/// Direct call into the `oakrender` crate (single-lib unification).
pub fn oakrender_color_processor_create(
	src_space: *const c_char,
	dst_transform: *const c_char,
	direction: c_int,
) -> OakColorProcessor {
	unsafe {
		oakrender::ffi::color::oakrender_color_processor_create(src_space, dst_transform, direction)
	}
}

/// Direct call into the `oakrender` crate (single-lib unification).
pub fn oakrender_color_processor_free(processor: *mut OakColorProcessor) {
	unsafe { oakrender::ffi::color::oakrender_color_processor_free(processor) }
}

/// Direct call into the `oakrender` crate (single-lib unification).
pub fn oakrender_color_processor_is_valid(processor: OakColorProcessor) -> c_int {
	unsafe { oakrender::ffi::color::oakrender_color_processor_is_valid(processor) }
}

/// Direct call into the `oakrender` crate (single-lib unification).
pub fn oakrender_codec_frame_free(frame: *mut crate::bridge::codec::OakFrame) {
	unsafe { oakrender::ffi::renderer::oakrender_codec_frame_free(frame) }
}

/// Direct call into the `oakrender` crate (single-lib unification).
pub fn oakrender_manager_set_aggressive_gc(enabled: c_int) -> c_int {
	unsafe { oakrender::ffi::ticket::oakrender_manager_set_aggressive_gc(enabled) }
}

/// Direct call into the `oakrender` crate (single-lib unification).
pub fn oakrender_cache_get_invalidated_ranges(
	cache: OakRenderCache,
	in_num: i64,
	in_den: i64,
	out_num: i64,
	out_den: i64,
	flat: *mut i64,
	flat_size: c_int,
) -> c_int {
	unsafe {
		oakrender::ffi::cache::oakrender_cache_get_invalidated_ranges(
			cache, in_num, in_den, out_num, out_den, flat, flat_size,
		)
	}
}

/// Direct call into the `oakrender` crate (single-lib unification).
pub fn oakrender_cache_free(cache: *mut OakRenderCache) {
	unsafe { oakrender::ffi::cache::oakrender_cache_free(cache) }
}
