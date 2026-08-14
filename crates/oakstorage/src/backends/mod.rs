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

//! Built-in backends.
//!
//! The database backend (`database`) is *not* registered: it is the
//! future replacement (M10 §3 — "数据库替换路径"), provided by a later
//! proxy. The file backends cover today's surface.

pub mod database;
pub mod otio;
pub mod ove_xml;

use std::sync::Arc;

/// All built-in backends in arbitration order (registered into
/// [`crate::registry::Registry::global`] at crate init).
pub fn builtins() -> Vec<Arc<dyn crate::backend::StorageBackend>> {
	vec![
		Arc::new(ove_xml::OveXmlBackend::new()),
		Arc::new(otio::OtioBackend::new()),
	]
}
