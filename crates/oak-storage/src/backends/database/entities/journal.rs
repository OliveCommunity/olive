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

//! The `journal` table: the command log (plan §1). One row per affected
//! node per command — the node's before image (`old_xml`) and after
//! image (`new_xml`), whole-node XML produced by the diff. `node_identity`
//! 0 is the settings pseudo-node; real node identities are stored
//! offset by +1 so the 0 sentinel never collides with a graph slot.

use sea_orm::entity::prelude::*;

/// The `journal` entity.
#[derive(Clone, Debug, PartialEq, Eq, DeriveEntityModel)]
#[sea_orm(table_name = "journal")]
pub struct Model {
	/// Owning project (ON DELETE CASCADE).
	pub project_id: i64,
	/// Monotonic per-project command sequence.
	#[sea_orm(primary_key)]
	pub seq: i64,
	/// The affected node's identity (+1; 0 = settings pseudo-node).
	#[sea_orm(primary_key)]
	pub node_identity: i64,
	/// Command kind: 'redo' | 'undo' | 'jump' | 'group' | 'import'.
	pub kind: String,
	/// Before image (whole node XML; NULL for newly added nodes).
	pub old_xml: Option<String>,
	/// After image (whole node XML; NULL for deleted nodes).
	pub new_xml: Option<String>,
	/// Journal write time.
	pub at: DateTime,
}

/// `journal` has no relations yet.
#[derive(Copy, Clone, Debug, EnumIter, DeriveRelation)]
pub enum Relation {}

impl ActiveModelBehavior for ActiveModel {}
