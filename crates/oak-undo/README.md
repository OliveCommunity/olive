# oakundo Rust crate

> Status: **implemented**. Ports the C++ oakundo module
> (`src/undo/src`) to Rust. Template follows `crates/oakplugin`.

## Scope

Replaces the C++ oakundo module (`src/undo/src`): undoable commands
and the undo/redo history stack. The frozen C ABI (`include/undo/*.h`)
and the engine facade that consumed it are gone (see the root
`Cargo.toml` note on `crates/oakengine.bk`): every consumer links the
crate as a plain rlib and uses the value-typed API below.

## Architectural decisions

1. **Trait-object commands replace the vtable pattern.** In C++ other
   modules *subclass* `olive::UndoCommand` (`redo()`/`undo()` overrides)
   and plug themselves in polymorphically. Rust models the same
   polymorphism with a boxed [`undocommand::Command`] trait object:
   one-off edits arrive as closure commands
   ([`undocommand::UndoCommand::from_closures`]) and whole-struct
   commands implement the trait and are boxed with
   [`undocommand::UndoCommand::new`]; composites are
   [`undocommand::MultiUndoCommand`]. The former
   `OakUndoCommandVtable` callback table, its `extern "C"` trampolines
   and the refcounted `CHandle` layer were deleted with the C ABI —
   domain logic dispatches through the trait the same way C++ virtual
   dispatch does.
2. **Modified-state callbacks are intentionally omitted.** The C++
   `UndoCommand::redo_and_set_modified` pair records/restores a project
   dirty flag via `std::function` accessors. The public headers exposed
   none of this; the stack drives state via `done_` on the safe type
   instead, and the flag callbacks are left as a documented future
   extension.
3. **`UndoStack` state machine** is modeled directly on the C++:
   two deques — `commands_` (done, oldest at front) and
   `undone_commands_` (most-recently-undone at front); `push` clears any
   redoable tail, executes redo, and drops the oldest when the cap
   (200) is exceeded; `jump` clamps and walks via `undo`/`redo`. The
   fresh stack holds a single "New/Open Project" empty command so
   `can_undo` is false at the bottom (per `undostack.cpp`).
4. **No merge semantics.** `src/undo/src/*` defines no
   `merge_with`/`can_merge`; commands are never coalesced. Tests
   reflect this (no merge tests).

## Layout

```
src/
  lib.rs            crate doc + module map
  error.rs          error codes (mirrors include/undo/error.h values)
  undocommand.rs    UndoCommand / Command trait / MultiUndoCommand
  undostack.rs      UndoStack + empty bottom command
  global.rs         process-wide stack, groups, observers
tests/              contract tests per module
```

The module has no `unsafe` code and no `extern "C"` surface; panics in
command callbacks propagate as normal process-internal panics (the
process-wide stack recovers a poisoned mutex the same way the former
`guard*` FFI wrappers did).

## Dependency policy

Prefer mature third-party crates (MIT/Apache-2.0/BSD, GPL-compatible)
over hand-rolling; register each addition (name + reason) here. Large
existing C++ libraries (OTIO, OCIO, OIIO, FFmpeg) are NEVER rewritten
— they are consumed through their C ABI / bridge layers.
