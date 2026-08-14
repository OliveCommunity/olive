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

//! Bin folders (C++ `olive::Folder`): tree structure over `NodeId`s.

use crate::factory::NodeMeta;
use crate::id::NodeId;
use crate::node::{Category, NodeBehavior, NodeCore};

/// A folder node behavior: children are folder-tree members (folders
/// and footage), not graph edges.
pub struct FolderBehavior {
	/// Display name.
	pub name: String,
	/// Child node ids (folders/footage) in bin order.
	pub children: Vec<NodeId>,
}

impl FolderBehavior {
	/// New empty folder.
	pub fn new(name: &str) -> Self {
		FolderBehavior {
			name: name.to_string(),
			children: Vec::new(),
		}
	}

	/// Add a child (C++ `Folder::add_child`).
	pub fn add_child(&mut self, child: NodeId) {
		if !self.children.contains(&child) {
			self.children.push(child);
		}
	}

	/// Remove a child; false when absent.
	pub fn remove_child(&mut self, child: NodeId) -> bool {
		let before = self.children.len();
		self.children.retain(|c| *c != child);
		self.children.len() != before
	}

	/// Index of `child` in the direct children.
	pub fn index_of_child(&self, child: NodeId) -> Option<usize> {
		self.children.iter().position(|c| *c == child)
	}

	/// True when `child` is contained recursively (C++
	/// `Folder::has_child_recursive`).
	pub fn has_child_recursive(&self, child: NodeId, graph: &crate::graph::Graph) -> bool {
		for c in &self.children {
			if *c == child {
				return true;
			}
			if let Some(entry) = graph.get(*c) {
				if let Some(f) = entry
					.behavior
					.as_any()
					.and_then(|a| a.downcast_ref::<FolderBehavior>())
				{
					if f.has_child_recursive(child, graph) {
						return true;
					}
				}
			}
		}
		false
	}
}

impl NodeBehavior for FolderBehavior {
	/// Human-readable name (C++ `name()`): the folder's display name.
	fn name(&self) -> &str {
		&self.name
	}

	/// Concrete downcast for the folder family.
	fn as_any(&self) -> Option<&dyn std::any::Any> {
		Some(self)
	}

	/// Concrete downcast for the folder family.
	fn as_any_mut(&mut self) -> Option<&mut dyn std::any::Any> {
		Some(self)
	}

	/// Stable type id (C++ `id()`).
	fn type_id(&self) -> &str {
		"org.olivevideoeditor.Olive.folder"
	}

	/// Categories (C++ `category()`): items live in the bin, not the
	/// render graph.
	fn categories(&self) -> &[Category] {
		&[Category::Timeline]
	}

	/// Deep copy (C++ `copy()`).
	fn duplicate(&self, _core: &NodeCore) -> Option<Box<dyn NodeBehavior>> {
		Some(Box::new(FolderBehavior::new(&self.name)))
	}

	/// Custom project save: the bin children (C++ attaches children
	/// through the `child_in` input connections; the Rust model keeps
	/// them in [`FolderBehavior::children`], so the custom segment is
	/// the persistence channel — old readers skip it).
	fn save_custom(&self, core: &NodeCore, writer: &mut dyn crate::serializer::XmlWrite) {
		let _ = core;
		if !self.children.is_empty() {
			writer.start_element("children");
			for c in &self.children {
				writer.text_element("child", &c.identity().to_string());
			}
			writer.end_element(); // children
		}
	}

	/// Custom project load; the child references resolve in the
	/// serializer's post-load pass (which also folds in `child_in`
	/// connections from C++ files).
	fn load_custom(
		&mut self,
		_core: &mut NodeCore,
		reader: &mut dyn crate::serializer::XmlRead,
	) -> bool {
		while reader.next_start_element() {
			match reader.name() {
				"children" => {
					self.children.clear();
					while reader.next_start_element() {
						if reader.name() == "child" {
							if let Some(id) =
								crate::serializer::parse_node_ref(&reader.read_element_text())
							{
								self.children.push(id);
							}
						} else {
							reader.skip_current_element();
						}
					}
				}
				_ => reader.skip_current_element(),
			}
		}
		true
	}
}

/// Constructor (C++ `Folder::Folder()`): a folder node declares the
/// `child_in` array input (C++ attaches bin children through it) but no
/// `enabled_in` (`// CPP-PARITY: folder.cpp:26`).
pub fn create(name: &str) -> (NodeCore, Box<dyn NodeBehavior>) {
	let mut core = NodeCore::empty();
	let mut child = crate::input::Input::new(
		"child_in",
		crate::value::ValueType::None,
		crate::value::NodeValue::None,
	);
	child.flags |= crate::input::flags::ARRAY | crate::input::flags::NOT_KEYFRAMABLE;
	core.add_input(child);
	(core, Box::new(FolderBehavior::new(name)))
}

/// Register a folder-typed node (used by the serializer for bin folders;
/// folders are not in the factory menu).
pub fn register(meta: &mut Vec<NodeMeta>) {
	meta.push(NodeMeta {
		type_id: "org.olivevideoeditor.Olive.folder",
		name: "Folder",
		categories: &[Category::Timeline],
		create: || create("Folder"),
	});
}
