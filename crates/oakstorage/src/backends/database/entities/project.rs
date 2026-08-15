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

//! The `projects` table: one row per project library entry (plan §1).

use sea_orm::entity::prelude::*;

/// The `projects` entity.
#[derive(Clone, Debug, PartialEq, Eq, DeriveEntityModel)]
#[sea_orm(table_name = "projects")]
pub struct Model {
	/// Surrogate row id (BIGSERIAL/INTEGER PK).
	#[sea_orm(primary_key)]
	pub id: i64,
	/// Project uuid (same value the serializer persists; unique).
	#[sea_orm(unique)]
	pub uuid: String,
	/// Display name (the project manager lists this).
	pub name: String,
	/// Payload format version (the serializer's CURRENT_VERSION).
	pub schema_ver: i32,
	/// Row creation timestamp.
	pub created_at: DateTime,
	/// Last-write timestamp (manager sort key).
	pub modified_at: DateTime,
	/// Current head command seq (the newest journal seq).
	pub command_seq: i64,
}

/// `projects` has no relations yet (queries join explicitly).
#[derive(Copy, Clone, Debug, EnumIter, DeriveRelation)]
pub enum Relation {}

impl ActiveModelBehavior for ActiveModel {}
