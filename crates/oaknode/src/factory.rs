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

use std::sync::OnceLock;

use crate::node::{Category, NodeBehavior, NodeCore};

/// Constructor for a node type: behavior + default core inputs.
pub type NodeConstructor = fn() -> (NodeCore, Box<dyn NodeBehavior>);

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

/// The registry (built at crate init by `nodes::register_all`).
pub struct Factory {
	entries: Vec<NodeMeta>,
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
			Factory { entries }
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
