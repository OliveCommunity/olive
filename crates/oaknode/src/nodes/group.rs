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
//! `NodeGroupSetOutputPassthrough`; the C ABI's undoable variants
//! (`include/node/group.h`) build equivalent vtable commands through
//! `bridge::undo` in the ffi layer.

use crate::factory::NodeMeta;
use crate::graph::Graph;
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
#[derive(Clone)]
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
	/// (`input.input`, suffixed `_2`, `_3`, ... until unique, or
	/// `force_id` when given), adds a group input with the inner
	/// input's type/default/flags, and records the mapping.
	///
	/// `descriptor` is the inner input's [`crate::input::Input`] (the
	/// C++ reads it off the inner node directly; the caller resolves
	/// it so the group input can be added while the group entry is
	/// mutably borrowed).
	pub fn add_input_passthrough(
		&mut self,
		core: &mut NodeCore,
		input: InnerInput,
		force_id: &str,
		descriptor: &crate::input::Input,
	) -> String {
		for (id, inner) in &self.input_passthroughs {
			if inner == &input {
				return id.clone();
			}
		}

		// Mint the passthrough id.
		let id = if force_id.is_empty() {
			let mut id = input.input.clone();
			let mut i = 2;
			while core.has_input(&id) {
				id = format!("{}_{}", input.input, i);
				i += 1;
			}
			id
		} else {
			force_id.to_string()
		};

		core.add_input(crate::input::Input {
			id: id.clone(),
			value_type: descriptor.value_type,
			default: descriptor.default.clone(),
			flags: descriptor.flags,
			display_name: id.clone(),
			properties: Vec::new(),
			array_size: 0,
		});
		self.input_passthroughs.push((id.clone(), input));
		id
	}

	/// Remove the passthrough (and the group input) for an inner
	/// input (C++ `remove_input_passthrough()`); no-op if absent.
	pub fn remove_input_passthrough(&mut self, core: &mut NodeCore, input: &InnerInput) {
		if let Some(i) = self
			.input_passthroughs
			.iter()
			.position(|(_, inner)| inner == input)
		{
			let id = self.input_passthroughs.remove(i).0;
			core.remove_input(&id);
		}
	}

	/// The inner output node (C++ `get_output_passthrough()`).
	pub fn output_passthrough(&self) -> Option<NodeId> {
		self.output_passthrough
	}

	/// Set the inner output node (C++ `set_output_passthrough()`); the
	/// C++ asserts the node belongs to the group's context (debug-only,
	/// not enforced here).
	pub fn set_output_passthrough(&mut self, node: Option<NodeId>) {
		self.output_passthrough = node;
	}

	/// Whether an inner input is already passed through (C++
	/// `contains_input_passthrough()`).
	pub fn contains_input_passthrough(&self, input: &InnerInput) -> bool {
		self.input_passthroughs
			.iter()
			.any(|(_, inner)| inner == input)
	}

	/// Group input id for an inner input, or empty (C++
	/// `get_id_of_passthrough()`).
	pub fn id_of_passthrough(&self, input: &InnerInput) -> &str {
		for (id, inner) in &self.input_passthroughs {
			if inner == input {
				return id;
			}
		}
		""
	}

	/// Inner input behind a group input id, if any (C++
	/// `get_input_from_id()`).
	pub fn input_from_id(&self, id: &str) -> Option<&InnerInput> {
		self.input_passthroughs
			.iter()
			.find(|(pid, _)| pid == id)
			.map(|(_, inner)| inner)
	}

	/// The passthrough entries (C++ `get_input_passthroughs()`).
	pub fn passthroughs(&self) -> &[InputPassthrough] {
		&self.input_passthroughs
	}

	/// Fully resolve a group input to the innermost non-group input
	/// (C++ `resolve_input()`): loops [`Self::get_inner`] until the
	/// input no longer refers to a group passthrough.
	pub fn resolve_input(graph: &Graph, input: InnerInput) -> InnerInput {
		let mut input = input;
		while Self::get_inner(graph, &mut input) {}
		input
	}

	/// One resolution step (C++ `get_inner()`): if `input` points at a
	/// group node's passthrough input, rewrite it to the mapped inner
	/// input and return true; false otherwise.
	pub fn get_inner(graph: &Graph, input: &mut InnerInput) -> bool {
		let inner = graph
			.get(input.node)
			.and_then(|e| e.behavior.as_any())
			.and_then(|a| a.downcast_ref::<NodeGroup>())
			.and_then(|g| g.input_from_id(&input.input));
		match inner {
			Some(inner) => {
				input.node = inner.node;
				input.input = inner.input.clone();
				true
			}
			None => false,
		}
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

	/// Localized input name (C++ `get_input_name()` override): forwards
	/// to the passed-through inner node's input name. The trait's
	/// borrowed return cannot carry a graph-resolved name, so the
	/// override relies on the default (the plain id / "Enabled"); the
	/// C++ forwarding needs a future graph-capable signature.
	fn input_name<'a>(&self, id: &'a str) -> &'a str {
		id
	}

	/// Custom project save (C++ `save_custom()`). The node-reference
	/// half of the C++ format (inner-node ids, output-passthrough
	/// target, per-input name/flags/properties) requires a graph the
	/// trait signature does not carry, so only the core-visible
	/// passthrough data is written: id, input and element.
	fn save_custom(&self, core: &NodeCore, writer: &mut dyn crate::serializer::XmlWrite) {
		let _ = core;
		writer.start_element("inputpassthroughs");
		for (id, inner) in &self.input_passthroughs {
			writer.start_element("inputpassthrough");
			writer.attribute("input", &inner.input);
			writer.attribute("element", &inner.element.to_string());
			writer.attribute("id", id);
			writer.end_element();
		}
		writer.end_element();
	}

	/// Custom project load (C++ `load_custom()`). Parses the
	/// `inputpassthroughs` entries written by [`Self::save_custom`];
	/// the C++ defers the node references into `SerializedData` and
	/// resolves them in `PostLoadEvent`, a channel this crate's
	/// serializer does not drive — `post_load` is therefore a no-op.
	fn load_custom(
		&mut self,
		_core: &mut NodeCore,
		reader: &mut dyn crate::serializer::XmlRead,
	) -> bool {
		while reader.next_start_element() {
			match reader.name() {
				"inputpassthroughs" => {
					while reader.next_start_element() {
						if reader.name() == "inputpassthrough" {
							let id = reader.attribute("id").unwrap_or_default();
							let input = reader.attribute("input").unwrap_or_default();
							let element = reader
								.attribute("element")
								.and_then(|e| e.parse().ok())
								.unwrap_or(0);
							// The inner node id is not resolvable without
							// the graph channel; keep the entry with a
							// placeholder node so a later graph pass can
							// re-map it.
							self.input_passthroughs.push((
								id,
								InnerInput {
									node: crate::id::NodeId::INVALID,
									input,
									element,
								},
							));
						}
						reader.skip_current_element();
					}
				}
				_ => reader.skip_current_element(),
			}
		}
		true
	}

	/// Post-load fixup (C++ `PostLoadEvent()`). The C++ re-resolves the
	/// deferred passthrough links against the now-live inner nodes and
	/// restores each group's output passthrough; this crate's serializer
	/// does not drive the load_custom/save_custom channel, so there is
	/// nothing to fix up here yet.
	fn post_load(&mut self, _core: &mut NodeCore) {}

	/// Deep copy (C++ `copy()` via `NODE_DEFAULT_FUNCTIONS`); clones
	/// the passthrough table and output reference too.
	fn duplicate(&self, _core: &NodeCore) -> Option<Box<dyn NodeBehavior>> {
		Some(Box::new(self.clone()))
	}

	/// Downcast (C++ `dynamic_cast<NodeGroup *>`).
	fn as_any(&self) -> Option<&dyn std::any::Any> {
		Some(self)
	}

	/// See [`NodeBehavior::as_any`].
	fn as_any_mut(&mut self) -> Option<&mut dyn std::any::Any> {
		Some(self)
	}
}

/// Constructor (C++ `NodeGroup::NodeGroup()`): no inputs of its own
/// (passthroughs are added dynamically); initializes the output
/// passthrough to unset and sets the dont-show-in-create-menu flag
/// (`// CPP-PARITY: src/node/src/group/group.cpp:35`, flag value
/// `k_dont_show_in_create_menu = 0x8` = [`crate::input::flags::HIDDEN`]).
pub fn create() -> (NodeCore, Box<dyn NodeBehavior>) {
	let mut core = NodeCore::new();
	core.flags |= crate::input::flags::HIDDEN as u64;
	(
		core,
		Box::new(NodeGroup {
			input_passthroughs: Vec::new(),
			output_passthrough: None,
		}),
	)
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
