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

//! The `snapshots` table: periodic full payloads that only accelerate
//! loading (plan §0/§1). The newest snapshot at or before a target seq
//! is the replay base; older ones are pruned to keep the newest
//! [`crate::backends::database::SNAPSHOT_KEEP`].

use sea_orm::entity::prelude::*;

/// The `snapshots` entity.
#[derive(Clone, Debug, PartialEq, Eq, DeriveEntityModel)]
#[sea_orm(table_name = "snapshots")]
pub struct Model {
	/// Owning project (ON DELETE CASCADE).
	#[sea_orm(primary_key)]
	pub project_id: i64,
	/// The journal command seq this snapshot captures.
	#[sea_orm(primary_key)]
	pub command_seq: i64,
	/// Full-feature project XML (identical to a `.ove` payload).
	pub payload: String,
	/// Snapshot write time.
	pub written_at: DateTime,
}

/// `snapshots` has no relations yet.
#[derive(Copy, Clone, Debug, EnumIter, DeriveRelation)]
pub enum Relation {}

impl ActiveModelBehavior for ActiveModel {}
