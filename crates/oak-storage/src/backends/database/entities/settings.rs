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

//! The `settings` table: the project's KV settings (plan §1). The
//! current values are mirrored here on every save; the history lives in
//! the journal (node_identity = 0 pseudo-node).

use sea_orm::entity::prelude::*;

/// The `settings` entity.
#[derive(Clone, Debug, PartialEq, Eq, DeriveEntityModel)]
#[sea_orm(table_name = "settings")]
pub struct Model {
	/// Owning project (ON DELETE CASCADE).
	#[sea_orm(primary_key)]
	pub project_id: i64,
	/// Setting key.
	#[sea_orm(primary_key)]
	pub key: String,
	/// Setting value.
	pub value: String,
}

/// `settings` has no relations yet.
#[derive(Copy, Clone, Debug, EnumIter, DeriveRelation)]
pub enum Relation {}

impl ActiveModelBehavior for ActiveModel {}
