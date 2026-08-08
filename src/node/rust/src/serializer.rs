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

//! Project (de)serialization: the C++ `ProjectSerializer` family.
//!
//! XML I/O goes through [`crate::bridge::common`] (oakcommon C ABI).
//! The version ladder (210528/210907/211228/220403/230220) becomes a
//! single reader with per-version adaptation functions — the C++
//! class-per-version hierarchy collapses into data-driven upgrade
//! steps, because all versions share the same document spine.

use std::sync::{Arc, Mutex};

use crate::project::Project;

/// Minimal XML reader surface the serializer needs (implemented over
/// the oakcommon xml C ABI in `bridge::common`).
pub trait XmlRead {
	/// Advance to the next start element; false at end/close.
	fn next_start_element(&mut self) -> bool;
	/// Current element name.
	fn name(&self) -> &str;
	/// Attribute by name.
	fn attribute(&self, name: &str) -> Option<String>;
	/// Read inner text of the current element.
	fn read_element_text(&mut self) -> String;
	/// Skip the current element subtree.
	fn skip_current_element(&mut self);
}

/// Minimal XML writer surface.
pub trait XmlWrite {
	/// Start an element.
	fn start_element(&mut self, name: &str);
	/// End the current element.
	fn end_element(&mut self);
	/// Write an attribute on the open element.
	fn attribute(&mut self, name: &str, value: &str);
	/// Write a text element.
	fn text_element(&mut self, name: &str, text: &str);
}

/// Detected project version (from the XML header).
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord)]
pub struct ProjectVersion(pub u32);

/// Load a project from XML text. Applies version upgrades in order;
/// rejects versions newer than the build (C++ `k_project_too_new`).
pub fn load(xml: &str) -> crate::error::Result<Arc<Mutex<Project>>> {
	todo!()
}

/// Save to the current-version XML format. Byte-compatible with the
/// C++ 230220 writer (golden project files pin this).
pub fn save(project: &Project) -> crate::error::Result<String> {
	todo!()
}
