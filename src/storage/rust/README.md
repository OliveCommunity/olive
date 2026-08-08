# oakstorage Rust crate (declaration draft, for review)

> Status: **declaration draft**. Signatures + doc comments are the
> spec; every body is `todo!()`. Manual: docs/zh/plans/riir/M10-oakstorage.md.

## Scope

Project persistence — the single module that knows where projects come
from and where they are saved to. Backends are pluggable via a manual
vtable; shipping in this pass: `ove-xml` (the existing XML project
format), `otio` (via the native oakotio crate), and **database**
(PostgreSQL + SQLite). Consumers never branch on backend.

## Architectural decisions

1. **URI dispatch, not file paths.** Every entry point takes a URI:
   `file:///…proj.ove` / `file:///…proj.otio` /
   `oakdb+sqlite:///path/to.db` / `oakdb+pg://host:port/dbname?user=…`.
   The core resolves scheme + backend `can_handle` arbitration
   (M10 §2.3).
2. **Manual vtable backends** (`backend.rs` `StorageBackend` trait =
   the M10 C vtable's Rust shape). The public C ABI vtable
   (`oakstorage_backend_register`) accepts foreign (C-side) backends;
   in-crate backends implement the Rust trait directly.
3. **The graph (de)serialization itself stays in oaknode** — every
   backend calls the oaknode C ABI (`oaknode_serializer_*` family)
   through `bridge::node` to fetch/rebuild the in-memory graph;
   backends own framing: container bytes, schema, versioning,
   compression, sessions.
4. **Database backend shares one logical schema** across PostgreSQL
   and SQLite via **SeaORM** (`sea-orm` crate, sqlx-postgres +
   sqlx-sqlite features): a private current-thread tokio runtime drives
   the async SeaORM API behind the synchronous C ABI; the entity set is
   minimal (projects table: id, name, payload blob, version,
   timestamps). Schema management (create-if-missing, migrate) is
   backend-internal. The graph payload is the same serialized form the
   ove-xml backend uses — one serialization truth, two containers.
5. **No callbacks/events** (M10: synchronous commands only; the caller
   — oaktask/facade — owns progress reporting).
6. **Errors** follow the project -MMCCCC scheme, module 10
   (`-100001` …); the M10 positive info codes (TOO_OLD/TOO_NEW/…)
   are kept verbatim.

## Layout

```
src/
  lib.rs          crate doc + module map
  error.rs        error/info codes (M10 §2.1, -MMCCCC module 10)
  handle.rs       refcounted-handle scaffolding
  uri.rs          URI parsing/classification
  session.rs      StorageProject session (open/take/uri)
  registry.rs     backend registry (register/unregister/arbitrate)
  backend.rs      StorageBackend trait + C vtable marshalling
  backends/
    ove_xml.rs    built-in .ove XML backend (via bridge::node)
    otio.rs       built-in .otio backend (via oakotio)
  bridge/
    node.rs       oaknode C ABI imports (serializer family)
  ffi.rs          export layer (M10 §2.2 verbatim)
tests/            contract tests incl. the pluggability proof
```
