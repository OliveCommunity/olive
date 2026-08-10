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

//! Linkage anchors — force the module crates' rlibs into every link.
//!
//! The facade talks to the modules exclusively through `extern "C"`
//! imports (src/bridge/), so rustc would otherwise consider the module
//! crates unused and prune their rlibs from the link. This module
//! references one `#[no_mangle]` export of every module crate (and
//! `oakcore-rs`) from a `#[used]` static, which (a) marks each crate as
//! used so its rlib reaches the linker and (b) keeps the anchor alive so
//! the referenced object files are pulled. For the `liboakengine` cdylib
//! this is what actually embeds the module C ABIs (oakundo_*,
//! oakcommon_*, ...) into the dylib next to the facade's own oakengine_*
//! exports.
//!
//! The per-crate symbol mirrors the test-force-link in
//! tests/common/mod.rs (same paths, same `as usize` cast idiom), so the
//! crate/module paths are proven against the current module layouts.

#![allow(dead_code)]

/// Pull every module crate into the link. Mirrors
/// `tests/common/mod.rs::force_link`; the oakcommon XML/undo anchors are
/// repeated because oaknode's serializer resolves those C ABI symbols at
/// runtime via dlsym(RTLD_DEFAULT) and they must be present in the dylib
/// for that lookup to succeed.
fn force_link() -> usize {
	let fns: [usize; 13] = [
		// oakcore-rs (pure value types; referenced so its rlib is linked).
		oakcore_rs::Rational::new(1, 2).numerator() as usize,
		// One exported C ABI symbol per module crate.
		oakundo::ffi::undostack::oakundo_undostack_init as usize,
		oakcommon::ffi::config::oakcommon_config_get_int as usize,
		oaktimeline::ffi::marker::oaktimeline_marker_list_create as usize,
		oakcodec::ffi::format::oakcodec_encoding_format_count as usize,
		oakaudio::ffi::waveform::oakaudio_waveform_length as usize,
		oakrender::ffi::cache::oakrender_cache_indicator_height as usize,
		oaktask::ffi::manager::oaktask_manager_init as usize,
		oakplugin::ffi::oakplugin_host_plugin_count as usize,
		oaknode::ffi::project::oaknode_project_init as usize,
		// oaknode's dlsym(RTLD_DEFAULT) targets (see tests/common/mod.rs).
		oakcommon::ffi::xmlutils::oakcommon_xml_writer_init as usize,
		oakcommon::ffi::xmlutils::oakcommon_xml_reader_init as usize,
		oakundo::ffi::command::oakundo_command_init as usize,
	];
	fns.iter().sum()
}

/// Keeps [`force_link`] (and through it every referenced export) alive in
/// the cdylib/staticlib even though nothing calls it directly.
#[used]
static FORCE_LINK_ANCHOR: fn() -> usize = force_link;
