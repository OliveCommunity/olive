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

//! Node group (C++ `src/node/src/group/group.{h,cpp}`,
//! `olive::NodeGroup`).
//!
//! A group wraps an inner node context and exposes selected inner
//! inputs as its own ("passthroughs"). The C++ class also declares the
//! undo commands `NodeGroupAddInputPassthrough` /
//! `NodeGroupSetOutputPassthrough`; those belong to the undo system
//! and are not modeled here.

use crate::factory::NodeMeta;
use crate::id::NodeId;
use crate::node::{Category, NodeBehavior, NodeCore};

/// Reference to an input of a node inside the group's context
/// (C++ `NodeInput`: node pointer + input id + array element).
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct InnerInput {
	/// The inner node this input belongs to.
	pub node: NodeId,
	/// Input id on the inner node.
	pub input: String,
	/// Array element index (-1/0 for non-array inputs).
	pub element: i32,
}

/// One passthrough entry (C++ `NodeGroup::InputPassthrough`):
/// the group's own input id mapped to the inner input it mirrors.
pub type InputPassthrough = (String, InnerInput);

/// Node group node. Exposes inner-node inputs as its own inputs and
/// forwards its output from a designated inner node.
///
/// The C++ `output_passthrough_` is a raw `Node*`; here it is the
/// inner node's [`NodeId`] (`None` = unset). Note: the brief for this
/// port mentioned `connected_render_output`, but the actual C++
/// `NodeGroup` does not override it — only the methods below are
/// overridden.
pub struct NodeGroup {
	/// Passthrough table: group input id -> inner input
	/// (C++ `input_passthroughs_`).
	input_passthroughs: Vec<InputPassthrough>,
	/// Inner node whose output this group forwards
	/// (C++ `output_passthrough_`).
	output_passthrough: Option<NodeId>,
}

impl NodeGroup {
	/// Add (or find) a passthrough for an inner input (C++
	/// `add_input_passthrough()`): returns the existing id if the
	/// input is already passed through; otherwise mints a fresh id
	/// (`input.input()`, suffixed `_2`, `_3`, ... until unique, or
	/// `force_id` when given — which must not already exist), adds a
	/// group input with the inner input's type/default/flags, and
	/// records the mapping.
	pub fn add_input_passthrough(&mut self, core: &mut NodeCore, input: InnerInput, force_id: &str) -> String {
		let _ = (core, input, force_id);
		todo!()
	}

	/// Remove the passthrough (and the group input) for an inner
	/// input (C++ `remove_input_passthrough()`); no-op if absent.
	pub fn remove_input_passthrough(&mut self, core: &mut NodeCore, input: &InnerInput) {
		let _ = (core, input);
		todo!()
	}

	/// The inner output node (C++ `get_output_passthrough()`).
	pub fn output_passthrough(&self) -> Option<NodeId> {
		todo!()
	}

	/// Set the inner output node (C++ `set_output_passthrough()`);
	/// the node must belong to the group's context (C++ asserts).
	pub fn set_output_passthrough(&mut self, node: Option<NodeId>) {
		let _ = node;
		todo!()
	}

	/// Whether an inner input is already passed through (C++
	/// `contains_input_passthrough()`).
	pub fn contains_input_passthrough(&self, input: &InnerInput) -> bool {
		let _ = input;
		todo!()
	}

	/// Group input id for an inner input, or empty (C++
	/// `get_id_of_passthrough()`).
	pub fn id_of_passthrough(&self, input: &InnerInput) -> &str {
		let _ = input;
		todo!()
	}

	/// Inner input behind a group input id, if any (C++
	/// `get_input_from_id()`).
	pub fn input_from_id(&self, id: &str) -> Option<&InnerInput> {
		let _ = id;
		todo!()
	}

	/// Fully resolve a group input to the innermost non-group input
	/// (C++ `resolve_input()` / `get_inner()`): loops
	/// [`Self::get_inner`] until the input no longer refers to a
	/// group passthrough.
	pub fn resolve_input(input: InnerInput) -> InnerInput {
		let _ = input;
		todo!()
	}

	/// One resolution step (C++ `get_inner()`): if `input` points at
	/// a group node's passthrough input, rewrite it to the mapped
	/// inner input and return true; false otherwise.
	pub fn get_inner(input: &mut InnerInput) -> bool {
		let _ = input;
		todo!()
	}
}

impl NodeBehavior for NodeGroup {
	/// Human-readable name (C++ `name()`).
	fn name(&self) -> &str {
		"Group"
	}

	/// Stable type id (C++ `id()`).
	fn type_id(&self) -> &str {
		"org.olivevideoeditor.Olive.group"
	}

	/// Categories (C++ `category()` returns `{k_category_unknown}` =
	/// -1, which has no `Category` enum counterpart; modeled as an
	/// empty slice — the group is hidden from the create menu via
	/// the dont-show-in-create-menu flag set in [`create`]).
	fn categories(&self) -> &[Category] {
		&[]
	}

	/// Description (C++ `description()`).
	fn description(&self) -> &str {
		"A group of nodes that is represented as a single node."
	}

	/// Localized input name (C++ `get_input_name()` override):
	/// returns the group's own override name if one was set;
	/// otherwise forwards to the passed-through inner node's input
	/// name (recursively, if that node is itself a group); empty
	/// when the id is not a passthrough.
	///
	/// C++ `retranslate()` additionally recurses `retranslate()`
	/// into every node in the group's context; that traversal is a
	/// graph-level concern and lives with the caller, not here.
	fn input_name<'a>(&self, id: &'a str) -> &'a str {
		let _ = id;
		todo!()
	}

	/// Custom project load (C++ `load_custom()`): reads the
	/// `inputpassthroughs` element (each `inputpassthrough` records
	/// inner node/input/element, passthrough id, custom name, flags,
	/// data type, default value, and a `properties` map of key/value
	/// pairs, deferred into the serialized data's group link list)
	/// and the `outputpassthrough` element (inner node reference,
	/// deferred likewise). Returns true.
	fn load_custom(&mut self, core: &mut NodeCore, reader: &mut dyn crate::serializer::XmlRead) -> bool {
		let _ = (core, reader);
		todo!()
	}

	/// Custom project save (C++ `save_custom()`): writes every
	/// passthrough (inner node reference, input id, element,
	/// passthrough id, display name, flags minus the inner input's
	/// own flags, data type name, default value, and properties) and
	/// the output passthrough node reference.
	fn save_custom(&self, core: &NodeCore, writer: &mut dyn crate::serializer::XmlWrite) {
		let _ = (core, writer);
		todo!()
	}

	/// Post-load fixup (C++ `PostLoadEvent()`): resolves the
	/// deferred group links — re-adds each input passthrough against
	/// the now-live inner nodes and restores its custom flags, name,
	/// data type, default value, and properties — then restores each
	/// group's output passthrough.
	fn post_load(&mut self, core: &mut NodeCore) {
		let _ = core;
		todo!()
	}

	/// Deep copy (C++ `copy()` via `NODE_DEFAULT_FUNCTIONS`);
	/// clones the passthrough table and output reference too.
	fn duplicate(&self, core: &NodeCore) -> Option<Box<dyn NodeBehavior>> {
		let _ = core;
		todo!()
	}
}

/// Constructor (C++ `NodeGroup::NodeGroup()`): no inputs of its own
/// (passthroughs are added dynamically); initializes the output
/// passthrough to unset and sets the dont-show-in-create-menu flag.
pub fn create() -> (NodeCore, Box<dyn NodeBehavior>) {
	todo!()
}

/// Register this node type (C++ factory entry for
/// `org.olivevideoeditor.Olive.group`; note C++ files it under
/// `k_category_unknown`, which is unrepresentable in [`Category`]).
pub fn register(meta: &mut Vec<NodeMeta>) {
	meta.push(NodeMeta {
		type_id: "org.olivevideoeditor.Olive.group",
		name: "Group",
		categories: &[],
		create,
	});
}
