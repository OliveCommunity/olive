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

//! The node type registry (C++ `NodeFactory` / `node/factory`):
//! type id -> constructor, plus menu metadata.
//!
//! Two entry kinds: the static built-in table installed by
//! [`crate::nodes::register_all`] (compile-time [`NodeMeta`]), and
//! runtime entries appended after plugin discovery
//! ([`DynamicNodeMeta`] — the C++ `NodeFactory::register_plugin_nodes`
//! appends one library entry per discovered OFX plugin; those have no
//! compile-time ids, so their metadata is owned and their constructor
//! is a closure capturing the plugin identifier).

use std::sync::{Mutex, OnceLock};

use crate::node::{Category, NodeBehavior, NodeCore};

/// Constructor for a node type: behavior + default core inputs.
pub type NodeConstructor = fn() -> (NodeCore, Box<dyn NodeBehavior>);

/// Constructor for a runtime-registered node type (captures per-plugin
/// state; each call builds a fresh node with a fresh backing instance,
/// matching the C++ `NodeFactory::create` `n->copy()` semantics for
/// plugin library entries).
pub type DynNodeConstructor =
	std::sync::Arc<dyn Fn() -> (NodeCore, Box<dyn NodeBehavior>) + Send + Sync>;

/// Static metadata for the node menu (C++ factory listing).
#[derive(Clone)]
pub struct NodeMeta {
	/// Type id (matches [`NodeBehavior::type_id`]).
	pub type_id: &'static str,
	/// Display name.
	pub name: &'static str,
	/// Categories.
	pub categories: &'static [Category],
	/// Constructor.
	pub create: NodeConstructor,
}

/// Runtime-registered node type metadata (the C++ factory's plugin
/// entries). Owned strings: plugin identifiers and labels are only
/// known at scan time.
#[derive(Clone)]
pub struct DynamicNodeMeta {
	/// Type id (the OFX plugin identifier).
	pub type_id: String,
	/// Display name (the plugin descriptor's label).
	pub name: String,
	/// Categories.
	pub categories: Vec<Category>,
	/// Sub-category (the OFX context display name, e.g. "Filter").
	pub sub_category: String,
	/// Description (the plugin descriptor's description).
	pub description: String,
	/// Constructor.
	pub create: DynNodeConstructor,
}

/// The registry (built at crate init by `nodes::register_all`; plugin
/// entries are appended at runtime via [`Factory::register_dynamic`]).
pub struct Factory {
	entries: Vec<NodeMeta>,
	dynamic: Mutex<Vec<DynamicNodeMeta>>,
}

impl Factory {
	/// Global registry. Registration (`nodes::register_all`) runs on
	/// first access, so the menu order is deterministic without an
	/// explicit init call.
	pub fn global() -> &'static Factory {
		GLOBAL.get_or_init(|| {
			crate::nodes::register_all();
			let entries = ENTRIES
				.get()
				.expect("nodes::register_all installs the entry table")
				.clone();
			Factory {
				entries,
				dynamic: Mutex::new(Vec::new()),
			}
		})
	}

	/// Look up by type id.
	pub fn find(&self, type_id: &str) -> Option<&NodeMeta> {
		self.entries.iter().find(|e| e.type_id == type_id)
	}

	/// All entries (menu order = registration order, C++ parity).
	pub fn entries(&self) -> &[NodeMeta] {
		&self.entries
	}

	fn dynamic(&self) -> std::sync::MutexGuard<'_, Vec<DynamicNodeMeta>> {
		self.dynamic.lock().unwrap_or_else(|e| e.into_inner())
	}

	/// Append a runtime-discovered node type (C++
	/// `NodeFactory::register_plugin_nodes` library append). Returns
	/// false when the type id is already registered (static or dynamic
	/// — the C++ `existing_ids` skip).
	pub fn register_dynamic(&self, meta: DynamicNodeMeta) -> bool {
		if self.find(&meta.type_id).is_some() {
			return false;
		}
		let mut dyns = self.dynamic();
		if dyns.iter().any(|m| m.type_id == meta.type_id) {
			return false;
		}
		dyns.push(meta);
		true
	}

	/// The runtime entries (clone of the current list; registration
	/// order).
	pub fn dynamic_entries(&self) -> Vec<DynamicNodeMeta> {
		self.dynamic().clone()
	}

	/// Look up a runtime entry by type id.
	pub fn find_dynamic(&self, type_id: &str) -> Option<DynamicNodeMeta> {
		self.dynamic().iter().find(|m| m.type_id == type_id).cloned()
	}

	/// Look up either entry kind and construct a node (static first,
	/// then dynamic — C++ `NodeFactory::create` walks one combined
	/// library).
	pub fn create_any(&self, type_id: &str) -> Option<(NodeCore, Box<dyn NodeBehavior>)> {
		if let Some(meta) = self.find(type_id) {
			return Some((meta.create)());
		}
		self.find_dynamic(type_id).map(|m| (m.create)())
	}

	/// Combined entry count (static + dynamic).
	pub fn total_count(&self) -> usize {
		self.entries.len() + self.dynamic().len()
	}

	/// Type id at a combined index (static entries first, then dynamic
	/// in registration order).
	pub fn type_id_at(&self, index: usize) -> Option<String> {
		if index < self.entries.len() {
			return Some(self.entries[index].type_id.to_string());
		}
		self.dynamic()
			.get(index - self.entries.len())
			.map(|m| m.type_id.clone())
	}

	/// Display name for a type id (either entry kind).
	pub fn name_of(&self, type_id: &str) -> Option<String> {
		if let Some(meta) = self.find(type_id) {
			return Some(meta.name.to_string());
		}
		self.find_dynamic(type_id).map(|m| m.name)
	}
}

/// Entries installed by [`crate::nodes::register_all`].
static ENTRIES: OnceLock<Vec<NodeMeta>> = OnceLock::new();

/// The lazily built global [`Factory`].
static GLOBAL: OnceLock<Factory> = OnceLock::new();

/// Install the registered node entries (called by
/// [`crate::nodes::register_all`]; a no-op if already installed).
pub(crate) fn install_entries(entries: Vec<NodeMeta>) {
	let _ = ENTRIES.set(entries);
}
