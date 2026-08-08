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

use crate::id::NodeId;

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
		todo!()
	}
}
