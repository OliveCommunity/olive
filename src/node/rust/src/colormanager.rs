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

//! Color manager (C++ `olive::ColorManager`): the per-project OCIO
//! config handle. OCIO itself stays behind the oakrender C ABI until
//! oakrender is rewritten; this module is the state owner and query
//! facade.

/// Per-project color manager.
pub struct ColorManager {
	/// OCIO config filename (empty = bundled default).
	pub config_filename: String,
	/// Default input colorspace.
	pub default_input_space: String,
	/// Default display.
	pub default_display: String,
	/// Default view.
	pub default_view: String,
	/// Reference colorspace.
	pub reference_space: String,
}

impl ColorManager {
	/// New with the built-in default config selected (C++ `init()`).
	pub fn new() -> Self {
		todo!()
	}

	/// (Re)load the config from `config_filename` via the oakrender
	/// color C ABI (`oakrender_color_manager_*`); E_FAILED on OCIO
	/// errors.
	pub fn reload(&mut self) -> crate::error::Result<()> {
		todo!()
	}

	/// Enumerate colorspaces of the active config (two-stage lists are
	/// flattened here into owned Strings).
	pub fn list_colorspaces(&self) -> Vec<String> {
		todo!()
	}
}
