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

//! OCIO display transform node (C++
//! `src/node/src/color/displaytransform/displaytransform.{h,cpp}`,
//! `olive::DisplayTransformNode`).
//!
//! Note: OpenColorIO itself is never linked here; it is reached through
//! the color manager (`crate::colormanager`) and the oakrender bridge
//! (`crate::bridge::render`), like the C++ node's
//! `oaknode_colormanager_*` / `oakrender_color_processor_*` calls.

use crate::factory::NodeMeta;
use crate::node::{Category, NodeBehavior, NodeCore};

use super::ociobase::OcioBase;

/// Display combo input id (C++ `k_display_input`). Type: combo; default
/// `0`; flags: not-keyframable, not-connectable. Combo strings are the
/// color manager's available displays (refreshed on config change).
pub const DISPLAY_INPUT: &str = "display_in";

/// View combo input id (C++ `k_view_input`). Type: combo; default `0`;
/// flags: not-keyframable, not-connectable. Combo strings are the views
/// available for the selected display.
pub const VIEW_INPUT: &str = "view_in";

/// Direction combo input id (C++ `k_direction_input`). Type: combo;
/// default `0` (forward); flags: not-keyframable, not-connectable.
/// Combo strings (set in `retranslate`): "Forward", "Inverse".
pub const DIRECTION_INPUT: &str = "dir_in";

/// Display transform node. Converts an image to or from a display color
/// space via an OCIO display/view transform. Owns no members beyond the
/// embedded OCIO base state (C++ has no own private members).
pub struct DisplayTransformNode {
	/// Shared OCIO base state (C++ base class `OCIOBaseNode`).
	base: OcioBase,
}

impl DisplayTransformNode {
	/// Selected display name (C++ `get_display()`): the display combo
	/// index mapped through the color manager's display list; empty
	/// string when no manager is attached or the index is out of range.
	fn get_display(&self, core: &NodeCore) -> String {
		todo!()
	}

	/// Selected view name (C++ `get_view()`): the view combo index
	/// mapped through the manager's views for [`Self::get_display`];
	/// empty when unavailable.
	fn get_view(&self, core: &NodeCore) -> String {
		todo!()
	}

	/// Transform direction (C++ `get_direction()`): the direction combo
	/// value cast to `ColorProcessor::Direction` (`0` = normal/forward,
	/// `1` = inverse).
	fn get_direction(&self, core: &NodeCore) -> i64 {
		todo!()
	}

	/// Refresh the display combo strings from the manager (C++
	/// `update_displays()`); no-op without a manager.
	fn update_displays(&mut self, core: &mut NodeCore) {
		todo!()
	}

	/// Refresh the view combo strings for the current display (C++
	/// `update_views()`); no-op without a manager.
	fn update_views(&mut self, core: &mut NodeCore) {
		todo!()
	}

	/// (Re)build the color processor (C++ `generate_processor()`): wraps
	/// the manager, builds a display transform for
	/// display/view/reference-space with the selected direction via
	/// `oakrender_color_processor_create_transform`, and stores it with
	/// [`OcioBase::set_processor`].
	fn generate_processor(&mut self, core: &mut NodeCore) {
		todo!()
	}

	/// OCIO config change hook (C++ `config_changed()` override):
	/// refreshes displays and views, then regenerates the processor.
	fn config_changed(&mut self, core: &mut NodeCore) {
		todo!()
	}
}

impl NodeBehavior for DisplayTransformNode {
	/// Human-readable name (C++ `name()`).
	fn name(&self) -> &str {
		"Display Transform"
	}

	/// Stable type id (C++ `id()`).
	fn type_id(&self) -> &str {
		"org.olivevideoeditor.Olive.displaytransform"
	}

	/// Categories (C++ `category()`).
	fn categories(&self) -> &[Category] {
		&[Category::Color]
	}

	/// Description (C++ `description()`).
	fn description(&self) -> &str {
		"Converts an image to or from a display color space."
	}

	/// Localized input names (C++ `retranslate()`): `tex_in` -> "Input",
	/// `display_in` -> "Display", `view_in` -> "View", `dir_in` ->
	/// "Direction" (also sets the direction combo strings
	/// "Forward"/"Inverse").
	fn input_name<'a>(&self, id: &'a str) -> &'a str {
		todo!()
	}

	/// Input value changed (C++ `InputValueChangedEvent`): for
	/// `display_in`, `view_in` or `dir_in` regenerates the processor;
	/// a `display_in` change additionally refreshes the view combo.
	fn input_value_changed(&mut self, core: &mut NodeCore, input: &str, element: i32) {
		todo!()
	}

	/// Evaluate outputs: inherited from the C++ base
	/// (`OCIOBaseNode::value()`), i.e. delegates to
	/// [`OcioBase::value`] — color-transform job when the processor is
	/// ready, pass-through otherwise.
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

	/// Deep copy (C++ `copy()` via `NODE_DEFAULT_FUNCTIONS`).
	fn duplicate(&self, core: &NodeCore) -> Option<Box<dyn NodeBehavior>> {
		todo!()
	}
}

/// Constructor (C++ `DisplayTransformNode::DisplayTransformNode()`):
/// builds the base (`tex_in` texture input, effect input, video-effect
/// flag) and adds the `display_in`/`view_in`/`dir_in` combo inputs with
/// the defaults and flags documented on the constants.
pub fn create() -> (NodeCore, Box<dyn NodeBehavior>) {
	todo!()
}

/// Register this node type (C++ factory entry for
/// `org.olivevideoeditor.Olive.displaytransform`).
pub fn register(meta: &mut Vec<NodeMeta>) {
	meta.push(NodeMeta {
		type_id: "org.olivevideoeditor.Olive.displaytransform",
		name: "Display Transform",
		categories: &[Category::Color],
		create,
	});
}
