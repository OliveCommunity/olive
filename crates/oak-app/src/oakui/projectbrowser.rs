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

//! M12 P3: the real project browser.
//!
//! Builds the widget's [`ProjectEntry`] tree from the project graph's
//! folder hierarchy (M14 R3: the direct oaknode walk — no facade): the
//! project root folder's children are the top-level entries, folders
//! expand through their `FolderBehavior` child lists. Entry ids are the
//! nodes' stable identities, so selection round-trips through
//! [`find_by_identity`].

use gpui_widgets::project_explorer::ProjectEntry;

use oak_node::folder::FolderBehavior;
use oak_node::id::NodeId;

use crate::oakui::graphops::{self, ProjectRef};

/// One child entry of a folder with its identity.
fn child_entry(g: &oak_node::graph::Graph, child: NodeId) -> Option<ProjectEntry> {
	let entry = g.get(child)?;
	let mut name = entry.core.label.clone();
	if name.is_empty() {
		name = entry.behavior.name().to_string();
	}
	Some(ProjectEntry::new(
		child.identity(),
		name,
		graphops::is_folder(g, child),
	))
}

/// The children of a folder node.
fn folder_children(g: &oak_node::graph::Graph, folder: NodeId) -> Vec<ProjectEntry> {
	let Some(f) = g
		.get(folder)
		.and_then(|e| e.behavior.as_any())
		.and_then(|a| a.downcast_ref::<FolderBehavior>())
	else {
		return Vec::new();
	};
	f.children
		.iter()
		.filter_map(|&child| child_entry(g, child))
		.collect()
}

/// The top-level entries: the project root folder's children.
pub fn roots(project: &ProjectRef) -> Vec<ProjectEntry> {
	let guard = graphops::lock(project);
	if !guard.root.valid() {
		return Vec::new();
	}
	folder_children(&guard.graph, guard.root)
}

/// The children of the folder whose identity is `parent_id` (empty when
/// not found or not a folder).
pub fn children(project: &ProjectRef, parent_id: u64) -> Vec<ProjectEntry> {
	let Some(folder) = find_by_identity(project, parent_id) else {
		return Vec::new();
	};
	let guard = graphops::lock(project);
	if !graphops::is_folder(&guard.graph, folder) {
		return Vec::new();
	}
	folder_children(&guard.graph, folder)
}

/// The project node with `identity` (validated against the graph), or
/// `None`.
pub fn find_by_identity(project: &ProjectRef, identity: u64) -> Option<NodeId> {
	let id = graphops::id_of(identity)?;
	let guard = graphops::lock(project);
	if guard.graph.is_valid(id) {
		Some(id)
	} else {
		None
	}
}
