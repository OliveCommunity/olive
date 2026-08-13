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
//! Builds the widget's [`ProjectEntry`] tree from the facade's folder
//! hierarchy: the project root folder's children are the top-level
//! entries, folders expand through `oakengine_folder_item_child_*`.
//! Entry ids are the nodes' stable identities (the facade identity),
//! so selection round-trips through `find_by_identity`.

use std::ffi::{c_char, c_int};

use gpui::SharedString;
use gpui_widgets::project_explorer::ProjectEntry;

use crate::oakui::ffi::{
	oakengine_folder_item_child, oakengine_folder_item_child_count, oakengine_node_free,
	oakengine_node_get_label, oakengine_node_get_type_id, oakengine_node_identity,
	oakengine_project_node_at, oakengine_project_node_count, oakengine_project_root,
	OakEngineNode, OakEngineProject,
};

/// The folder behavior's factory type id.
const TYPE_ID_FOLDER: &str = "org.olivevideoeditor.Olive.folder";

/// Two-stage string read over a facade `(buf, size)` getter.
fn read_str(f: impl Fn(*mut c_char, c_int) -> c_int) -> String {
	let needed = f(std::ptr::null_mut(), 0);
	if needed <= 0 {
		return String::new();
	}
	let mut buf = vec![0 as c_char; needed as usize + 1];
	f(buf.as_mut_ptr(), needed as c_int + 1);
	let len = buf.iter().position(|&c| c == 0).unwrap_or(buf.len());
	String::from_utf8_lossy(unsafe { std::slice::from_raw_parts(buf.as_ptr() as *const u8, len) })
		.into_owned()
}

/// Whether `node` (a live box) is a folder node.
///
/// # Safety
/// `node` must be a live box.
unsafe fn node_is_folder(node: *mut OakEngineNode) -> bool {
	unsafe {
		let id = read_str(|buf, size| oakengine_node_get_type_id(node, buf, size));
		id == TYPE_ID_FOLDER
	}
}

/// One child entry of `folder` (a live box) with its identity.
///
/// # Safety
/// `child` must be a live box; freed by the caller.
unsafe fn child_entry(child: *mut OakEngineNode) -> ProjectEntry {
	unsafe {
		let ident = oakengine_node_identity(child);
		let label = read_str(|buf, size| oakengine_node_get_label(child, buf, size));
		let is_dir = node_is_folder(child);
		let mut name = label;
		if name.is_empty() {
			name = read_str(|buf, size| crate::oakui::ffi::oakengine_node_get_name(child, buf, size));
		}
		ProjectEntry::new(ident, name, is_dir)
	}
}

/// The top-level entries: the project root folder's children.
///
/// # Safety
/// `project` must be a live box.
pub unsafe fn roots(project: *mut OakEngineProject) -> Vec<ProjectEntry> {
	unsafe {
		let mut out = Vec::new();
		if project.is_null() {
			return out;
		}
		let root = oakengine_project_root(project);
		if root.is_null() {
			return out;
		}
		out = folder_children(root);
		oakengine_node_free(root);
		out
	}
}

/// The children of the folder whose identity is `parent_id` (empty when
/// not found).
///
/// # Safety
/// `project` must be a live box.
pub unsafe fn children(project: *mut OakEngineProject, parent_id: u64) -> Vec<ProjectEntry> {
	unsafe {
		let Some(node) = find_by_identity(project, parent_id) else {
			return Vec::new();
		};
		if !node_is_folder(node) {
			oakengine_node_free(node);
			return Vec::new();
		}
		let out = folder_children(node);
		oakengine_node_free(node);
		out
	}
}

/// The children of a live folder box.
///
/// # Safety
/// `folder` must be a live folder box.
unsafe fn folder_children(folder: *mut OakEngineNode) -> Vec<ProjectEntry> {
	unsafe {
		let mut out = Vec::new();
		let count = oakengine_folder_item_child_count(folder);
		for i in 0..count.max(0) {
			let child = oakengine_folder_item_child(folder, i);
			if child.is_null() {
				continue;
			}
			out.push(child_entry(child));
			oakengine_node_free(child);
		}
		out
	}
}

/// The boxed project node with `identity` (freed with
/// [`oakengine_node_free`]), or `None`.
///
/// # Safety
/// `project` must be a live box.
pub unsafe fn find_by_identity(
	project: *mut OakEngineProject,
	identity: u64,
) -> Option<*mut OakEngineNode> {
	unsafe {
		if project.is_null() || identity == 0 {
			return None;
		}
		let count = oakengine_project_node_count(project);
		for i in 0..count.max(0) {
			let node = oakengine_project_node_at(project, i);
			if node.is_null() {
				continue;
			}
			if oakengine_node_identity(node) == identity {
				return Some(node);
			}
			oakengine_node_free(node);
		}
		None
	}
}

/// The footage node at `index` (the project's footage list), or `None`.
///
/// # Safety
/// `project` must be a live box.
pub unsafe fn footage_node_at(
	project: *mut OakEngineProject,
	index: c_int,
) -> Option<*mut OakEngineNode> {
	unsafe {
		if project.is_null() {
			return None;
		}
		let node = crate::oakui::ffi::oakengine_project_footage_at(project, index);
		if node.is_null() {
			None
		} else {
			Some(node)
		}
	}
}
