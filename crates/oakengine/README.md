# oakengine — the `liboakengine` cdylib (plugin / external C ABI)

The frozen `oakengine_*` C ABI (`engine/include/oakengine/*.h`) as a
**pure cdylib**. This is the plugin / external-consumer layer: OFX plugins
and third-party embedders link `liboakengine` and call the C ABI. The app,
oak-cli and oak-worker do not use it anymore (M14 R4) — they link the
module crates directly as Rust rlibs. The C ABI itself is frozen: only
additive changes, plus major version bumps.

Downward, every export is a direct Rust call into the module crates
(oakundo/oaknode/oaktimeline/oakcodec/oakaudio/oakrender/oaktask/
oakcommon/oakplugin/oakstorage/oakcore-rs) through `src/stubs.rs` (the
rewired replacement for the deleted `bridge/`). The facade itself owns
only the engine's box/unbox, buf/size and error-code conventions;
cross-cutting state that used to live here (the process-wide undo stack,
the open undo group) has sunk into the modules (M14 R1:
`oakundo::global`).

## Architecture

```
src/
  lib.rs          crate docs, module list, test-only test_link
  error.rs        OAKENGINE error codes (module 00 → -1..-6)
  handle.rs       CHandle mirror, OakEngine* opaque wrappers, box/unbox,
                  catch_unwind guards, buf/size string helpers
  stubs.rs        direct-Rust shims replacing the deleted bridge/ (the
                  engine's only downward path to the modules)
  linkage.rs      #[used] anchors pulling every module rlib into the cdylib
  undo.rs         engine/include/oakengine/undo.h (thin forward to
                  oakundo::global)
  common.rs       config.h + videoparams.h (facade-static tables + POD↔handle)
  audio.rs        audio.h (manager + sync + processor)
  codec.rs        encoding.h + exporter.h (metadata, params POD, exporter)
  render.rs       renderer.h + color.h + lut.h (renderer/frame/color processor)
  plugin.rs       plugin.h
  node.rs         node.h + project.h + footage.h (node graph / project / footage)
  timeline.rs     timeline.h (sequences, clips, tracks, markers, workarea)
  task.rs         task.h (background tasks over oaktask)
  storage.rs      write-through session (project ↔ oakstorage backend)
  deferred.rs     documented deferrals (stub detail lives here and in the
                  family modules' stub bodies)
  test_support/   the former tests/*.rs, now in-crate unit tests
```

### Dependencies

The facade's regular dependency is `thiserror` (the `Display` +
`std::error::Error` impl for the facade error enum, `src/error.rs`). Every
module call is a compile-time Rust call into the module crate's direct API
(`src/stubs.rs`); the module crates are real dependencies, and
[`linkage`](src/linkage.rs) anchors them so their rlibs are embedded in
the `liboakengine` cdylib next to the facade's `oakengine_*` exports. The
`oakcore_audioparams_*` accessors the audio paths read through are
implemented in the dylib too (src/stubs.rs, module `audio`, M12 P5) —
they used to be C++ host symbols left as runtime lookups (macOS
`-undefined dynamic_lookup`); the cdylib now carries no undefined imports.

### Handle mapping

The engine headers' opaque pointers (`OakEngineNode*`, `OakEngineTrack*`,
...) are thin newtype wrappers around the module layer's `CHandle`
(`{ctx, addref, release, abi_version}`) values. A box is created by
`handle::box_handle` and freed by `handle::free_box` (release + dealloc);
consuming exports (`oakengine_*_free`, `oakengine_undo_push`, ...) free
their box, borrowed results never are. The process-wide undo stack and
open undo group live in the oakundo module (`oakundo::global`); the
undo.h exports are thin forwards over it (module 00 analogues of
`EngineCore::undo_stack()` and the C++ capi's `g_undo_group`).

### Error codes

Facade-local codes are -1..-6 (`OAKENGINE_E_*`); module codes pass
through **untranslated** (the -MMCCCC prefix preserves provenance, e.g.
-20004 is oakundo's NOT_FOUND). String getters follow the engine buf/size
convention: the return value is the length excluding the NUL
(`handle::string_result` converts the modules' size-including-NUL).

## Scope

| Family | Header | Wrapped | Notes |
|---|---|---|---|
| undo | undo.h | 37 | stack/group/command lifecycle over oakundo (`oakundo::global` + undocommand) + Qt leftovers (update_actions/actions → no-op/NULL) |
| common | config.h, videoparams.h | 34 | config over oakcommon; videoparams static tables ported from `engine/render/videoparams.cpp` |
| audio | audio.h | 26 | manager + sync; processor convert/output_params stubbed (interface mismatch) |
| plugin | plugin.h | 4 | callbacks are facade state; push_button stubbed (no module API) |
| codec | encoding.h, exporter.h | 81/85 | metadata family over oakcodec (`include/codec/format.h`); params handle is a facade box over the `oakcodec_encoding_params` POD; the exporter family drives the export task synchronously (integration-tested, real mp4 via FFmpeg); presets/load-save deferred |
| render | renderer.h, color.h, lut.h | 60/60 | renderer over oakrender tickets; frame accessors over `OakCodecFrame`; color processor over `oakrender_color_processor_*`; color-manager list queries + LUT library stubs |
| node | node.h, project.h, footage.h | 226/327 | the node graph, project and footage families over the oaknode crate; documented stubs where the module lacks the surface (gizmos, plugin messages, input properties, brush, thumbnail/waveform caches, shape/subtitle, keyframe enumeration, ...) — see the stub bodies |
| timeline | timeline.h | 126/139 | sequences/clips/tracks/markers/workarea over oaknode + oaktimeline; documented stubs (ripple-tracks command, default transitions, move-track/clip, marker-create, auto-cache, cache invalidation, multicam find/switch — module-surface gaps, see the stub bodies) |
| task | task.h | 27 | the background-task system over oaktask (manager + load/save/import/export creators + result accessors); `create_proxy` stubbed (the module has no proxy-task C creator); start-time/is-cancelled are facade-approximated |

Deferred/stub detail lives in [`deferred`](src/deferred.rs) and in the
stub bodies' doc comments. The worker/IPC families (`worker.h`/`ipc.h`)
are **not** part of the facade anymore: the frozen C++ ABI does not
include them, so the render worker's runtime and the shared-memory
frame-slot transport moved into the `oak-worker` crate
(`crates/oak-worker/src/{worker,ipc}.rs`, self-contained, direct Rust
calls into oakrender); the ipc.h control-plane message serializers remain
unwrapped.

## Testing

The crate is cdylib-only (no rlib artifact), so integration tests cannot
link it as a crate; the former `tests/*.rs` moved into `src/test_support/`
and run as in-crate unit tests (pulled in from `src/lib.rs` under
`#[cfg(test)]`), addressing the modules through `crate::*`. The module
crates are real dependencies, so the test binary statically links the same
rlibs the cdylib embeds:

- `src/linkage.rs` (always-on) anchors the module rlibs into the cdylib
  for `cargo build`; the test-only `test_link` module in `src/lib.rs`
  forces the oakrender/oaknode/oaktimeline/oaktask rlibs into the lib
  unit-test binary, and `src/test_support/common/mod.rs::force_link`
  covers the test-support module (same symbol list, so the anchor paths
  stay proven against the current module layouts).
- Tests that touch process-wide state (the audio manager, the undo stack,
  the task manager) take a shared serialization lock instead of relying
  on the process isolation the old integration tests had
  (`test_support/common::with_manager`, the per-family `SERIAL` mutexes).

The smoke tests exercise the module crates' real implementations. Where a
wrapped family needs module behavior the crates do not implement yet, the
engine function is a documented stub with its reason (see `deferred.rs`).

```
cargo test          # in-crate unit tests (undo, common, audio, codec,
                    # exporter, plugin, render, node, timeline, task,
                    # library/storage families)
cargo build         # liboakengine cdylib embeds the module rlibs + the
                    # folded-in oakcore_audioparams_* accessors (no
                    # undefined imports)
```

## FFI discipline

Every export goes through a `catch_unwind` guard
(`handle::guard*`); `*_free` is a NULL no-op; strings use the two-stage
buf/size convention; module error codes pass through untranslated;
handles are refcounted module values wrapped in opaque boxes.
