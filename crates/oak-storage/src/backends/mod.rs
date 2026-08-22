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
//! Arbitration order inside `file://`: ove-xml before otio so a `.ove`
//! path (which the otio backend would also claim as JSON) stays on the
//! ove backend. The database backend claims its own `oakdb+…` schemes.

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
		Arc::new(database::DatabaseBackend::new()),
	]
}
