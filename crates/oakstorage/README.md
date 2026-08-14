# oakstorage Rust crate — project persistence

> Status: **implemented** (file backends). Manual:
> docs/zh/plans/riir/M10-oakstorage.md.

## Scope

Project persistence — the single module that knows where projects come
from and where they are saved to. Backends are pluggable via a manual
vtable; shipping in this pass: `ove-xml` (the XML project format) and
`otio` (the `.otio` / `.fcpxml` interchange, via the native oakotio
crate). The **database** backend (PostgreSQL + SQLite, SeaORM) is a
declared stub for a later proxy — not registered, `todo!()` bodies.
Consumers never branch on backend.

## Architectural decisions

1. **URI dispatch, not file paths.** Every entry point takes a URI:
   `file:///…proj.ove` / `file:///…proj.otio` / `oakdb://…`. Bare
   paths are normalized to `file://`. The core resolves scheme +
   backend `can_handle` arbitration (M10 §2.3).
2. **Manual vtable backends** (`backend.rs` `StorageBackend` trait =
   the M10 C vtable's Rust shape). The public C ABI vtable
   (`oakstorage_backend_register`) accepts foreign (C-side) backends —
   the database-swap interface proof — and in-crate backends implement
   the Rust trait directly.
3. **The graph (de)serialization itself stays in oaknode** — every
   backend calls the oaknode serializer (`oaknode::serializer::load` /
   `save`) through `bridge::node` (direct Rust calls, single-lib
   unification) to fetch/rebuild the in-memory graph; backends own
   framing: container bytes, schema, versioning (TOO_OLD/TOO_NEW/
   UNKNOWN_VERSION), sessions. `OAKSTORAGE_SAVE_COMPRESS` is accepted
   but not implemented (the oaknode serializer emits plain XML only).
4. **Database backend shares one logical schema** across PostgreSQL
   and SQLite via **SeaORM**: a private current-thread tokio runtime
   drives the async API behind the synchronous C ABI; the entity set is
   minimal (projects table: id, name, payload blob, version,
   timestamps). The graph payload is the same serialized form the
   ove-xml backend uses — one serialization truth, two containers.
5. **No callbacks/events** (M10: synchronous commands only; the caller
   — oaktask/facade — owns progress reporting).
6. **Errors** follow the project -MMCCCC scheme, module 10
   (`-100001` …); the M10 positive info codes (TOO_OLD/TOO_NEW/…) are
   kept verbatim.
7. **Interchange is lossy.** The otio backend's export/import mapping
   preserves sequences/tracks/clips/gaps/transitions; effect chains,
   keyframes, project bins/settings and exact rational timebases are
   not carried (see the module docs in `backends/otio.rs`).

## Layout

```
src/
  lib.rs          crate doc + module map
  error.rs        error/info codes (M10 §2.1, -MMCCCC module 10)
  handle.rs       refcounted-handle scaffolding (shared oakcore CHandle)
  uri.rs          URI parsing/classification
  session.rs      StorageProject session (open/take/uri)
  registry.rs     backend registry (register/unregister/arbitrate)
  backend.rs      StorageBackend trait + LoadResult
  backends/
    ove_xml.rs    built-in .ove XML backend (via bridge::node)
    otio.rs       built-in .otio/.fcpxml backend (via oakotio)
    database.rs   declared stub (later proxy)
  bridge/
    node.rs       oaknode calls (project + serializer + sequence builder)
  ffi.rs          export layer (M10 §2.2/§2.3 verbatim)
tests/            contract tests incl. the pluggability proof
```

## Build / test

A workspace member (not a default member); build and test explicitly:

```
cargo test -p oakstorage
```
