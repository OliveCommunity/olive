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

//! OCIO LUT file node (C++ `src/node/src/color/ociolut/ociolut.{h,cpp}`,
//! `olive::OCIOLutNode`).
//!
//! Note: OpenColorIO itself is never linked here; it is reached through
//! the color manager (`crate::colormanager`) and the oakrender bridge
//! (`crate::bridge::render`), like the C++ node's
//! `oakrender_color_processor_create_lut` / `oakrender_lut_*` calls.

use std::sync::Mutex;

use crate::factory::NodeMeta;
use crate::node::{Category, NodeBehavior, NodeCore};

use super::ociobase::OcioBase;

/// LUT file input id (C++ `k_file_input`). Type: file; default: empty
/// string; flags: not-keyframable, not-connectable; properties:
/// `filter = "LUT Files (*.<ext> ...);;All Files (*)"` (extensions from
/// `oakrender_lut_supported_extension_at`, `*.*` fallback),
/// `placeholder = "Select a LUT file"`, `lut_library = true`.
pub const FILE_INPUT: &str = "lut_file_in";

/// Direction combo input id (C++ `k_direction_input`). Type: combo;
/// default `0` (forward); flags: not-keyframable, not-connectable.
/// Combo strings (set in `retranslate`): "Forward", "Inverse".
pub const DIRECTION_INPUT: &str = "lut_dir_in";

/// Deferred processor-generation state (C++ `mutable` members
/// `gen_mutex_`, `processor_dirty_`, `last_path_`, `last_direction_`,
/// `last_processor_`, `last_error_`). Grouped behind one mutex: the C++
/// mutable-in-const-method pattern maps to interior mutability here,
/// and the C++ code already serializes all of these under `gen_mutex_`.
struct ProcessorState {
	/// Regeneration pending (C++ `processor_dirty_`, starts `true`).
	dirty: bool,
	/// Path of the LUT the cached processor was built from (C++
	/// `last_path_`).
	last_path: String,
	/// Direction the cached processor was built for (C++
	/// `last_direction_`, starts `-1`).
	last_direction: i64,
	/// Cached processor for change detection (C++ `last_processor_`);
	/// released with the node.
	last_processor: Option<crate::bridge::render::ColorProcessorHandle>,
	/// Human-readable reason no LUT processor is active (C++
	/// `last_error_`); empty when a valid processor is in use or no LUT
	/// file has been selected yet.
	last_error: String,
}

// The cached processor handle wraps a refcounted C object that is only
// dereferenced from the render path; see `OcioBase` for the rationale.
unsafe impl Send for ProcessorState {}

/// OCIO LUT node. Applies a LUT file through OpenColorIO.
pub struct OCIOLutNode {
	/// Shared OCIO base state (C++ base class `OCIOBaseNode`).
	base: OcioBase,
	/// Processor generation cache/lock (C++ `gen_mutex_` + the mutable
	/// `last_*` members).
	state: Mutex<ProcessorState>,
}

impl OCIOLutNode {
	/// Human-readable description of why no LUT processor is active
	/// (C++ `last_error()`); empty when a valid LUT processor is in use
	/// or no LUT file has been selected yet.
	pub fn last_error(&self) -> String {
		todo!()
	}

	/// Record a new error string (C++ `set_last_error()`); no-op when
	/// unchanged. The Qt version surfaced the error on the main-window
	/// status bar; here it is only recorded and read back via
	/// [`Self::last_error`].
	fn set_last_error(&self, error: &str) {
		todo!()
	}

	/// Direction combo value with legacy fallback (C++ file-static
	/// `read_direction_input()`): integer combo value, or — for old
	/// serializers that stored the combo as a string — case-insensitive
	/// "forward"/"0" -> 0 and "inverse"/"1" -> 1, defaulting to 0 with a
	/// stderr warning for anything else.
	fn read_direction_input(core: &NodeCore) -> i64 {
		todo!()
	}

	/// Whether this is the main GUI process (C++ file-static
	/// `is_main_process()`): true when a render manager exists (the
	/// render worker never creates one).
	fn is_main_process() -> bool {
		todo!()
	}

	/// (Re)generate the processor now (C++ `generate_processor()`):
	/// ensures the processor is current, then — in the main process
	/// only — invalidates the texture-input cache and cancels background
	/// video cache tasks so in-flight renders cannot write stale frames.
	fn generate_processor(&mut self, core: &mut NodeCore) {
		todo!()
	}

	/// Ensure the processor matches the current inputs (C++
	/// `ensure_processor()`): under the generation mutex, returns early
	/// when not dirty, a cached processor exists, and path/direction are
	/// unchanged; otherwise rebuilds from the inputs.
	fn ensure_processor(&self, core: &NodeCore) {
		todo!()
	}

	/// Rebuild the processor from the LUT path and direction (C++
	/// `create_processor_from_inputs()`): no manager, empty path,
	/// non-regular file, or unsupported extension -> clear both
	/// processors, reset the cache markers, record the error, and return
	/// false; unchanged path+direction with a live cached processor ->
	/// clear the dirty flag and return false (reuse); otherwise create
	/// the LUT processor via `oakrender_color_processor_create_lut`
	/// (direction 0 = forward), update the cache markers and both
	/// processor slots, and return true.
	fn create_processor_from_inputs(&self, core: &NodeCore) -> bool {
		todo!()
	}

	/// OCIO config change hook (C++ `config_changed()` override):
	/// regenerates immediately in the main process, or just marks the
	/// processor dirty in the render worker (deferred to render time).
	fn config_changed(&mut self, core: &mut NodeCore) {
		todo!()
	}
}

impl NodeBehavior for OCIOLutNode {
	/// Human-readable name (C++ `name()`).
	fn name(&self) -> &str {
		"OCIO LUT"
	}

	/// Stable type id (C++ `id()`).
	fn type_id(&self) -> &str {
		"org.olivevideoeditor.Olive.ociolut"
	}

	/// Categories (C++ `category()`).
	fn categories(&self) -> &[Category] {
		&[Category::Color]
	}

	/// Description (C++ `description()`).
	fn description(&self) -> &str {
		"Applies a LUT file through OpenColorIO."
	}

	/// Localized input names (C++ `retranslate()`): `tex_in` -> "Input",
	/// `lut_file_in` -> "LUT File", `lut_dir_in` -> "Direction" (also
	/// sets the direction combo strings "Forward"/"Inverse").
	fn input_name<'a>(&self, id: &'a str) -> &'a str {
		todo!()
	}

	/// Input value changed (C++ `InputValueChangedEvent`): for
	/// `lut_file_in` or `lut_dir_in`, regenerates the processor
	/// immediately in the main process; in the render worker (where
	/// generation can be slow and the main process may be blocked
	/// waiting on LoadGraph) only marks the processor dirty so it is
	/// rebuilt at render time.
	fn input_value_changed(&mut self, core: &mut NodeCore, input: &str, element: i32) {
		todo!()
	}

	/// Evaluate outputs (C++ `value()`): first `ensure_processor()` so
	/// the processor is up to date before the base class emits the color
	/// transform job (essential in the render worker, where creation is
	/// deferred until the first render), then delegates to
	/// [`OcioBase::value`].
	fn value(
		&self,
		core: &NodeCore,
		inputs: &crate::value::NodeValueRow,
		time: oakcore_rs::Rational,
		table: &mut crate::value::NodeValueTable,
	) {
		todo!()
	}

	/// Added to a graph (C++ base `AddedToGraphEvent`): captures the
	/// project's color manager and runs `config_changed()` via
	/// [`OcioBase::added_to_graph`].
	fn added_to_graph(&mut self, core: &mut NodeCore) {
		todo!()
	}

	/// Removed from a graph (C++ base `RemovedFromGraphEvent`): clears
	/// the color manager pointer via [`OcioBase::removed_from_graph`].
	fn removed_from_graph(&mut self, core: &mut NodeCore) {
		todo!()
	}

	/// Deep copy (C++ `copy()` via `NODE_COPY_FUNCTION`; the destructor
	/// additionally disconnects all signals and frees the cached
	/// processor).
	fn duplicate(&self, core: &NodeCore) -> Option<Box<dyn NodeBehavior>> {
		todo!()
	}
}

/// Constructor (C++ `OCIOLutNode::OCIOLutNode()`): builds the base
/// (`tex_in` texture input, effect input, video-effect flag) and adds
/// `lut_file_in` (with the LUT filter/placeholder/lut-library
/// properties) and `lut_dir_in` with the defaults, flags and properties
/// documented on the constants; the processor state starts dirty.
pub fn create() -> (NodeCore, Box<dyn NodeBehavior>) {
	todo!()
}

/// Register this node type (C++ factory entry for
/// `org.olivevideoeditor.Olive.ociolut`).
pub fn register(meta: &mut Vec<NodeMeta>) {
	meta.push(NodeMeta {
		type_id: "org.olivevideoeditor.Olive.ociolut",
		name: "OCIO LUT",
		categories: &[Category::Color],
		create,
	});
}
