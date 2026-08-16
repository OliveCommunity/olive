# Project Storage Architecture

[中文](zh/project-storage.md)

Oak persists projects in a **database** (SQLite by default, PostgreSQL
supported), with **write-through persistence**: every edit is committed
to the database as it happens — there is no Save button and nothing to
lose. The `.ove` XML file, OTIO and FCPXML are import/export formats,
not the working store.

## Design principles

- **Aggregate-granular persistence, not full relational mapping.** The
  database stores four things: project metadata, key/value settings,
  periodic full snapshots, and a per-node command journal. Domain
  semantics (timeline structure, node connections, keyframes, effect
  chains) stay inside each node's XML payload. A project is exactly
  "the node graph plus settings" — nothing else — so node granularity
  is a closed, complete model.
- **One serialization truth.** The node XML in the database is the same
  document the `.ove` serializer produces (`oaknode::serializer`).
  New features (e.g. adjustment layers) extend the XML schema only —
  the database schema never changes.
- **The journal is produced by diffing, not by instrumenting commands.**
  After every undoable command the in-memory project is re-serialized
  and compared node-by-node with the previous state; changed/added/
  removed nodes each become one journal row. Existing and future
  command types are covered automatically.
- **The journal is the persistent undo history.** Replaying applies
  each node's newest image in command order; rewinding applies the
  previous images in reverse. Undo survives restarts.

## Schema (SQLite / PostgreSQL, sea-orm)

```sql
projects(id PK, uuid UNIQUE, name, schema_ver, created_at, modified_at, command_seq)
settings(project_id FK, key, value, PK(project_id, key))
snapshots(project_id FK, command_seq, payload, written_at, PK(project_id, command_seq))
journal(project_id FK, seq, node_identity, kind, old_xml, new_xml, at,
        PK(project_id, seq, node_identity))
```

- `snapshots.payload` is the full project XML, written every
  `Storage/SnapshotIntervalSec` (default 600) while dirty, pruned to
  the newest 3. It only accelerates loading — the journal alone can
  rebuild the project from scratch.
- `journal` rows are whole-node before/after images: `old_xml` is NULL
  for created nodes, `new_xml` is NULL for removed nodes. Loading takes
  the newest snapshot and replaces each node with its newest image in
  `seq` order; rewinding to command `N` applies `old_xml` backwards.
  `node_identity = 0` is the settings pseudo-node.
- Every command is one synchronous transaction (journal rows +
  `command_seq + 1`), so a `kill -9` loses zero commands.
- `Storage/JournalRetentionDays` (default 0 = keep forever) bounds the
  undo window; rows are kilobytes each.

## Timeline representation

The timeline is a chain of node references inside node XML:

```
sequence ──<tracklists>──▶ tracklist ──<tracks>──▶ track ──<blocks>──▶ clip ──<footage>──▶ footage
```

The clip row carries its timeline range (`<range in out/>`), media
offset (`<media_in>`) and footage reference; effect chains are
connection records inside the effect nodes' XML. Loading re-links
these identities in two passes (see `oaknode::serializer`), so no
join tables are needed. Timeline edits map to a handful of node rows:
moving a clip touches its track and the clip; splitting adds one node
and updates two; ripple edits touch the affected tracks and delete the
removed clips.

## Write-through flow

1. A facade undo push (`oakengine_undo_push`, group end, undo/redo/
   jump) succeeds.
2. The project is re-serialized in memory (microseconds to low
   milliseconds) and diffed against the last state.
3. One transaction writes the changed node rows, bumps `command_seq`
   and `modified_at`.
4. A background thread writes snapshots latest-wins; on exit the queue
   is flushed.

## Import / export

- Import: `.ove` / `.otio` / `.fcpxml` are parsed by their existing
  oakstorage backends and inserted as a new project row with
  `kind = 'import'` journal entries.
- Export: the in-memory serialization is written through the ove-xml
  or otio backend; nothing is read from or written to the database
  beyond the current state.

## Configuration

Library selection and behavior are driven by the `Storage` config group
(read by `crates/oakengine/src/storage.rs`):

- `Storage/Backend` — `"sqlite"` (the documented default), `"database"`
  or `"pg"` enables write-through; any other value (e.g. `"off"`)
  disables it. When the key is absent no library is configured and
  projects stay unbound (headless consumers and the test suite never
  touch the user's real library).
- `Storage/SqlitePath` — the SQLite library file; default
  `<system data dir>/library.db` (honoring `OAK_CONFIG_DIR`).
- `Storage/PgUrl` — the PostgreSQL connection string, used when
  `Backend = "pg"`: `user:pass@host:5432/dbname` (libpq URL form; an
  optional `postgres://`/`postgresql://` scheme is stripped). The
  resolved library URI is `oakdb+pg://<PgUrl>`.
- `Storage/SnapshotIntervalSec` (default 600) and
  `Storage/JournalRetentionDays` (default 0 = keep forever) — see above.

## Multi-writer and platforms

v1 assumes a single writer per database (SQLite `busy_timeout`, PG row
locks). Multi-writer collaboration is future work (M14). The default
database is a single user-level SQLite file; PostgreSQL is selected with
`Storage/Backend = "pg"` + `Storage/PgUrl`, or directly with an
`oakdb+pg://` URI.

Database tests: `cargo test -p oakstorage` is green without PostgreSQL —
the SQLite suite always runs; the PG suite (`tests/database_pg_test.rs`)
connects to a real server when `OAK_TEST_PG_URL` is set (e.g.
`postgres://user:pass@host:5432/db`) and skips with a note otherwise.
The URL should point at a dedicated test database: each test resets the
four tables.

See also: [M10 oakstorage manual](plans/riir/M10-oakstorage.md),
[M13 write-through plan](plans/riir/M13-storage-live.md),
[project file reference](project-file-reference.md).
