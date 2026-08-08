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

//! Multi-cam source switcher node (C++
//! `src/node/src/input/multicam/multicamnode.{h,cpp}`,
//! `olive::MultiCamNode`).

use crate::factory::NodeMeta;
use crate::id::NodeId;
use crate::node::{Category, NodeBehavior, NodeCore};

/// Current source selector input id (C++ `k_current_input`). Type:
/// combo; default `0`; flags: static. Its combo-box strings are built
/// in `retranslate()` as `"<index + 1>: <source name>"` entries.
pub const CURRENT_INPUT: &str = "current_in";

/// Sources array input id (C++ `k_sources_input`). Type: none (any);
/// flags: not-keyframable, array; properties: `arraystart = 1`.
pub const SOURCES_INPUT: &str = "sources_in";

/// Sequence input id (C++ `k_sequence_input`). Type: none (node
/// reference to a `Sequence`); flags: not-keyframable. Excluded from
/// rendering via `ignore_inputs_for_rendering()`.
pub const SEQUENCE_INPUT: &str = "sequence_in";

/// Sequence track-type selector input id (C++ `k_sequence_type_input`).
/// Type: combo; default `0` (video); flags: static, hidden (unhidden
/// while a sequence is connected). Combo strings: `"Video"`, `"Audio"`
/// (C++ `Track::Type` values).
pub const SEQUENCE_TYPE_INPUT: &str = "sequence_type_in";

/// Multi-cam node. Switches between multiple sources either from the
/// `sources_in` array or, when a `Sequence` is connected to
/// `sequence_in`, from that sequence's track list.
pub struct MultiCamNode {
	/// Connected sequence whose track list supplies the sources (C++
	/// `sequence_`, a raw `Sequence *` set/cleared by the
	/// `sequence_in` connect/disconnect events). Stored here as the
	/// sequence node's id; `None` = no sequence connected.
	sequence: Option<NodeId>,
}

impl MultiCamNode {
	/// Index of the currently selected source (C++
	/// `get_current_source()` — the standard value of
	/// [`CURRENT_INPUT`] as int).
	pub fn current_source(&self, core: &NodeCore) -> i32 {
		todo!()
	}

	/// Number of available sources (C++ `get_source_count()`): the
	/// connected sequence's track count when a sequence is set,
	/// otherwise the `sources_in` array size.
	pub fn source_count(&self, core: &NodeCore) -> i32 {
		todo!()
	}

	/// Grid layout for `sources` sources (C++ static
	/// `get_rows_and_columns(sources, rows, cols)`): starts at 1x1 and
	/// grows the smaller dimension until rows*cols >= sources.
	pub fn rows_and_columns(sources: i32) -> (i32, i32) {
		todo!()
	}

	/// Grid position of a source index (C++ static
	/// `index_to_row_cols()`): `col = index % total_cols`,
	/// `row = index / total_cols` (`total_rows` is unused in C++).
	pub fn index_to_row_cols(index: i32, total_rows: i32, total_cols: i32) -> (i32, i32) {
		todo!()
	}

	/// Inverse of [`Self::index_to_row_cols`] (C++ static
	/// `rows_cols_to_index()`): `col + row * total_cols`.
	pub fn rows_cols_to_index(row: i32, col: i32, total_rows: i32, total_cols: i32) -> i32 {
		todo!()
	}
}

impl NodeBehavior for MultiCamNode {
	/// Human-readable name (C++ `name()`).
	fn name(&self) -> &str {
		"Multi-Cam"
	}

	/// Stable type id (C++ `id()`).
	fn type_id(&self) -> &str {
		"org.olivevideoeditor.Olive.multicam"
	}

	/// Categories (C++ `category()` returns `{ k_category_timeline }`).
	fn categories(&self) -> &[Category] {
		&[Category::Timeline]
	}

	/// Description (C++ `description()`).
	fn description(&self) -> &str {
		"Allows easy switching between multiple sources."
	}

	/// Localized input names (C++ `retranslate()`): `current_in` ->
	/// "Current", `sources_in` -> "Sources", `sequence_in` ->
	/// "Sequence", `sequence_type_in` -> "Sequence Type". The C++
	/// override also refreshes the combo strings: `sequence_type_in`
	/// gets {"Video", "Audio"} and `current_in` gets per-source
	/// `"<i + 1>: <connected source name>"` entries — that part has no
	/// trait surface and is noted here only.
	fn input_name<'a>(&self, id: &'a str) -> &'a str {
		todo!()
	}

	/// Inputs excluded from rendering (C++
	/// `ignore_inputs_for_rendering()`): always
	/// `{ k_sequence_input }`.
	fn ignore_inputs_for_rendering(&self) -> &[String] {
		todo!()
	}

	/// Active array elements (C++ `get_active_elements_at_time()`): for
	/// `sources_in`, only the element at the current source index (or
	/// no elements if the index is out of range); any other input
	/// defers to the base-class behavior.
	fn active_elements_at_time(&self, input: &str, time: oakcore_rs::Rational) -> Vec<i32> {
		todo!()
	}

	/// Render-time connection resolution (C++
	/// `get_connected_render_output()`): with a sequence connected,
	/// `sources_in` element `i` (in range) resolves to the track at
	/// index `i` of the selected track list instead of any connected
	/// edge; otherwise defers to the base-class behavior.
	///
	/// NOTE: the C++ class also overrides
	/// `is_input_connected_for_render()` (reports `sources_in` elements
	/// as connected whenever a sequence is set); the trait has no such
	/// method, so that behavior folds into this one.
	fn connected_render_output(&self, core: &NodeCore, input: &str, element: i32) -> Option<NodeId> {
		todo!()
	}

	/// Evaluate outputs (C++ `value()`): pushes the first value of the
	/// `sources_in` value array (which, per
	/// `active_elements_at_time`, is the currently selected source);
	/// pushes nothing when the array is empty.
	fn value(
		&self,
		core: &NodeCore,
		inputs: &crate::value::NodeValueRow,
		time: oakcore_rs::Rational,
		table: &mut crate::value::NodeValueTable,
	) {
		todo!()
	}

	/// Edge connected (C++ `InputConnectedEvent()`): when a `Sequence`
	/// connects to `sequence_in`, stores it and unhides
	/// `sequence_type_in`.
	fn input_connected(&mut self, core: &mut NodeCore, input: &str, element: i32, source: NodeId) {
		todo!()
	}

	/// Edge disconnected (C++ `InputDisconnectedEvent()`): on
	/// `sequence_in` disconnect, clears the stored sequence and re-hides
	/// `sequence_type_in`.
	fn input_disconnected(&mut self, core: &mut NodeCore, input: &str, element: i32, source: NodeId) {
		todo!()
	}

	/// Deep copy (C++ `copy()` via `NODE_DEFAULT_FUNCTIONS`).
	fn duplicate(&self, core: &NodeCore) -> Option<Box<dyn NodeBehavior>> {
		todo!()
	}
}

/// Constructor (C++ `MultiCamNode::MultiCamNode()`): adds
/// `current_in`/`sources_in`/`sequence_in`/`sequence_type_in` with the
/// defaults, flags and properties documented on the constants, and
/// initializes `sequence_` to null.
pub fn create() -> (NodeCore, Box<dyn NodeBehavior>) {
	todo!()
}

/// Register this node type (C++ `k_multicam_node` in
/// `factory.cpp::create_from_factory_index`).
pub fn register(meta: &mut Vec<NodeMeta>) {
	meta.push(NodeMeta {
		type_id: "org.olivevideoeditor.Olive.multicam",
		name: "Multi-Cam",
		categories: &[Category::Timeline],
		create,
	});
}
