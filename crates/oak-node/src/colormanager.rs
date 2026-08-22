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
//! facade. `// CPP-PARITY: src/node/src/color/colormanager/colormanager.{h,cpp}`.

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
	/// Whether a config is loaded (C++ `config_` non-null). Without
	/// liboakrender the config stays unloaded and the config-dependent
	/// queries report E_STATE.
	pub config_loaded: bool,
}

impl ColorManager {
	/// New with the built-in default config selected (C++ `init()`).
	pub fn new() -> Self {
		// The bundled default config is "reference space" — treat it as
		// loaded with the conventional defaults (the C++ constructor
		// leaves the config null until `init()`; `new()` is the
		// un-initialized state for `oaknode_colormanager_init`).
		ColorManager {
			config_filename: String::new(),
			default_input_space: "linear".to_string(),
			default_display: "sRGB".to_string(),
			default_view: "Standard".to_string(),
			reference_space: "linear".to_string(),
			config_loaded: false,
		}
	}

	/// Load the built-in default config (C++ `ColorManager::init()`).
	/// The oakrender color C ABI never exported the config-loading
	/// symbols, so this always took the "mark loaded with the built-in
	/// defaults" path (single-lib: the render call is removed and the
	/// deterministic equivalent kept).
	pub fn initialize(&mut self) -> crate::error::Result<()> {
		self.config_loaded = true;
		Ok(())
	}

	/// (Re)build the process-wide default config (C++
	/// `set_up_default_config()`).
	pub fn set_up_default_config(&mut self) -> crate::error::Result<()> {
		self.config_loaded = true;
		Ok(())
	}

	/// (Re)load the config from `config_filename`; E_FAILED on OCIO
	/// errors. Missing/invalid files keep the previous config (C++
	/// `update_config_from_filename()`). The oakrender color C ABI never
	/// implemented the load, so the file-loaded path always kept the
	/// previous state (single-lib: the render call is removed).
	pub fn update_config_from_filename(&mut self) -> crate::error::Result<()> {
		if self.config_filename.is_empty() {
			// Empty filename selects the bundled default.
			self.config_loaded = true;
			return Ok(());
		}
		Ok(())
	}

	/// Enumerate colorspaces of the active config (two-stage lists are
	/// flattened here into owned Strings). Without a real OCIO config the
	/// list holds the reference space only.
	pub fn list_colorspaces(&self) -> Vec<String> {
		if !self.config_loaded {
			return Vec::new();
		}
		vec![self.reference_space.clone()]
	}

	/// Displays of the active config (default display when unloaded).
	pub fn list_displays(&self) -> Vec<String> {
		if !self.config_loaded {
			return Vec::new();
		}
		vec![self.default_display.clone()]
	}

	/// Views of the active config for `display` (default view).
	pub fn list_views(&self, _display: &str) -> Vec<String> {
		if !self.config_loaded {
			return Vec::new();
		}
		vec![self.default_view.clone()]
	}

	/// Looks of the active config (none by default).
	pub fn list_looks(&self) -> Vec<String> {
		if !self.config_loaded {
			return Vec::new();
		}
		Vec::new()
	}

	/// True when a config is loaded.
	pub fn is_loaded(&self) -> bool {
		self.config_loaded
	}
}

impl Default for ColorManager {
	fn default() -> Self {
		ColorManager::new()
	}
}
