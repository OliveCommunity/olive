# oakundo Rust crate

> Status: **implemented**. Ports the C++ oakundo module
> (`src/undo/src`) to Rust behind its frozen C ABI
> (`include/undo/*.h`). Template follows `src/plugin/rust`.

## Scope

Replaces the C++ oakundo module (`src/undo/src`): undoable commands
and the undo/redo history stack. Public contract: `include/undo/*.h`
(3 headers: `error.h`, `undocommand.h`, `undostack.h`) — frozen,
implemented verbatim by `src/ffi.rs`.

## Architectural decisions

1. **Vtable-command pattern is the centerpiece.** In C++ other modules
   *subclass* `olive::UndoCommand` (`redo()`/`undo()` overrides) and
   plug themselves in polymorphically. Rust has no inheritance, so the
   C ABI already models exactly this with
   `OakUndoCommandVtable { redo, undo, free_fn }` plus a caller-owned
   `userdata` pointer. The safe layer's [`undocommand::CommandKind`]
   is the direct analog: either a caller-defined vtable command
   (function pointers + userdata) or a [`undocommand::MultiUndoCommand`]
   composite. Domain logic dispatches on the vtable the same way the
   C++ virtual dispatch does.
2. **Modified-state callbacks are intentionally not part of the C ABI.**
   The C++ `UndoCommand::redo_and_set_modified` pair records/restores a
   project dirty flag via `std::function` accessors. The public headers
   expose none of this; the stack drives state via `done_` on the safe
   type instead, and the flag callbacks are left as a documented future
   extension.
3. **`UndoStack` state machine** is modeled directly on the C++:
   two deques — `commands_` (done, oldest at front) and
   `undone_commands_` (most-recently-undone at front); `push` clears any
   redoable tail, executes redo, and drops the oldest when the cap
   (200) is exceeded; `jump` clamps and walks via `undo`/`redo`. The
   fresh stack holds a single "New/Open Project" empty command so
   `can_undo` is false at the bottom (per `undostack.cpp`).
4. **No merge semantics.** `include/undo/*.h` and `src/undo/src/*`
   define no `merge_with`/`can_merge`; commands are never coalesced.
   Tests reflect this (no merge tests).

## Layout

```
src/
  lib.rs            crate doc + module map
  error.rs          error codes (include/undo/error.h)
  handle.rs         refcounted-handle scaffolding (OAKUNDO_ABI_VERSION=1)
  undocommand.rs    UndoCommand / vtable command / MultiUndoCommand
  undostack.rs      UndoStack + empty bottom command
  ffi.rs            export layer (one submodule per public header)
tests/              contract tests per module
```

`error.h` exports macros only and is folded into `ffi.rs`'s preamble
(no own submodule), matching the codec crate convention.

## Dependency policy

Prefer mature third-party crates (MIT/Apache-2.0/BSD, GPL-compatible)
over hand-rolling; register each addition (name + reason) here. Large
existing C++ libraries (OTIO, OCIO, OIIO, FFmpeg) are NEVER rewritten
— they are consumed through their C ABI / bridge layers.
