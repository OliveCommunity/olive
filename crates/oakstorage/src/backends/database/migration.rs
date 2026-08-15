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

//! Schema bootstrap (plan M13 §1).
//!
//! Chosen over the sea-orm-migration crate on purpose: the schema is
//! four tables, static for the whole D1 surface, and `CREATE TABLE IF
//! NOT EXISTS` is idempotent — no versioned migration machinery, no
//! extra crate in the tree. The DDL runs once at first connection open.

use sea_orm::{ConnectionTrait, DatabaseBackend, Statement};

/// SQLite DDL for the four tables (plan §1, SQLite dialect:
/// `INTEGER PRIMARY KEY AUTOINCREMENT` + `TIMESTAMP`).
const SQLITE_DDL: &[&str] = &[
	"CREATE TABLE IF NOT EXISTS projects (
		id          INTEGER PRIMARY KEY AUTOINCREMENT,
		uuid        TEXT NOT NULL UNIQUE,
		name        TEXT NOT NULL,
		schema_ver  INTEGER NOT NULL,
		created_at  TIMESTAMP NOT NULL,
		modified_at TIMESTAMP NOT NULL,
		command_seq BIGINT NOT NULL DEFAULT 0
	)",
	"CREATE TABLE IF NOT EXISTS settings (
		project_id  INTEGER NOT NULL REFERENCES projects(id) ON DELETE CASCADE,
		key         TEXT NOT NULL,
		value       TEXT NOT NULL,
		PRIMARY KEY (project_id, key)
	)",
	"CREATE TABLE IF NOT EXISTS snapshots (
		project_id  INTEGER NOT NULL REFERENCES projects(id) ON DELETE CASCADE,
		command_seq BIGINT NOT NULL,
		payload     TEXT NOT NULL,
		written_at  TIMESTAMP NOT NULL,
		PRIMARY KEY (project_id, command_seq)
	)",
	"CREATE TABLE IF NOT EXISTS journal (
		project_id    INTEGER NOT NULL REFERENCES projects(id) ON DELETE CASCADE,
		seq           BIGINT NOT NULL,
		node_identity BIGINT NOT NULL,
		kind          TEXT NOT NULL,
		old_xml       TEXT,
		new_xml       TEXT,
		at            TIMESTAMP NOT NULL,
		PRIMARY KEY (project_id, seq, node_identity)
	)",
];

/// Apply the schema (idempotent; safe to run on every connection open).
///
/// PostgreSQL (D3) gets its own `BIGSERIAL` DDL — same shape, currently
/// unreachable because the backend rejects `oakdb+pg://` before any
/// connection is opened.
pub async fn migrate(db: &impl ConnectionTrait) -> crate::error::Result<()> {
	let backend = db.get_database_backend();
	let statements: &[&str] = match backend {
		DatabaseBackend::Sqlite => SQLITE_DDL,
		// D3: `BIGSERIAL PRIMARY KEY` for projects.id, `BIGINT` for the
		// FKs — the rest of the column set is identical.
		DatabaseBackend::Postgres => {
			return Err(crate::error::Error::Failed(
				"oakdb+pg is a D3 milestone; the PostgreSQL schema is not wired yet".to_string(),
			));
		}
		other => {
			return Err(crate::error::Error::Failed(format!(
				"unsupported database backend {other:?}"
			)));
		}
	};
	for sql in statements {
		db.execute_raw(Statement::from_string(backend, sql.to_string()))
			.await
			.map_err(|e| crate::error::Error::Io(format!("schema migration: {e}")))?;
	}
	Ok(())
}
