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

//! SeaORM entity models for the four-table schema (plan M13 §1).
//!
//! One module per entity: the `DeriveEntityModel` macro generates
//! `Entity`/`Column`/`Model`/`ActiveModel` in the defining module, so
//! each table gets its own scope. The same definitions serve SQLite and
//! PostgreSQL — the column types (`i64`, `String`, `Option<String>`,
//! `NaiveDateTime`) map to BIGINT/TEXT/TIMESTAMP on both, so the shared
//! save/replay logic never branches on the dialect.

pub mod journal;
pub mod project;
pub mod settings;
pub mod snapshot;
