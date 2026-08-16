# oakfacade — the `liboakengine` facade (Rust)

Re-exports the frozen `oakengine_*` C ABI (`engine/include/oakengine/*.h`)
verbatim on top of the module C ABIs (`include/<mod>/*.h`, implemented by
the oakundo/oaknode/oaktimeline/oakcodec/oakaudio/oakrender/oaktask/
oakcommon/oakplugin crates). This is the M9 §4 assembly layer: every
module call crosses the module C ABI as an `extern "C"` import; the
facade itself owns only cross-cutting state.

## Architecture

```
src/
  lib.rs          crate docs, module list
  error.rs        OAKENGINE error codes (module 00 → -1..-6)
  handle.rs       CHandle mirror, OakEngine* opaque wrappers, box/unbox,
                  catch_unwind guards, buf/size string helpers
  bridge/         extern "C" imports per module crate (the only way the
                  facade talks to modules)
  undo.rs         engine/include/oakengine/undo.h
  common.rs       config.h + videoparams.h (facade-static tables + POD↔handle)
  audio.rs        audio.h (manager + sync + processor)
  codec.rs        encoding.h (metadata over oakcodec + facade params POD box)
  render.rs       renderer.h + color.h + lut.h (renderer/frame/color processor)
  plugin.rs       plugin.h
  node.rs         node.h + project.h + footage.h (node graph / project / footage)
  timeline.rs     timeline.h (sequences, clips, tracks, markers, workarea)
  task.rs         task.h (background tasks over oaktask)
  deferred.rs     documented deferrals (stub detail lives here and in the
                  family modules' stub bodies)
tests/
  common/mod.rs   test support: force-link + oakcore/ffmpeg_bridge stubs
  undo.rs, common.rs, audio.rs, codec.rs, render.rs, plugin.rs, linkage.rs
```

### Dependencies

The facade's regular dependency is `thiserror` (the `Display` +
`std::error::Error` impl for the facade error enum, `src/error.rs`). Every
module
call crosses the module C ABI as an `extern "C"` import (`src/bridge/`),
and the module crates themselves are real dependencies: [`linkage`](src/linkage.rs)
anchors them so their `#[no_mangle]` exports are linked into the
`liboakengine` cdylib — the dylib carries the module C ABIs (oakundo_*,
oakcommon_*, oaktimeline_*, oakcodec_*, oakaudio_*, oakrender_*,
oaktask_*, oakplugin_*, oaknode_*) next to the facade's oakengine_*
exports. The `oakcore_audioparams_*` accessors the audio paths read
through are implemented in the dylib too (src/stubs.rs, module `audio`,
M12 P5) — they used to be C++ host symbols left as runtime lookups
(macOS `-undefined dynamic_lookup`); the cdylib now carries no undefined
imports.

### Handle mapping

The engine headers' opaque pointers (`OakEngineNode*`, `OakEngineTrack*`,
...) are thin newtype wrappers around the module C ABI's `CHandle`
(`{ctx, addref, release, abi_version}`) values. A box is created by
`handle::box_handle` and freed by `handle::free_box` (release + dealloc);
consuming exports (`oakengine_*_free`, `oakengine_undo_push`, ...) free
their box, borrowed results never are. The facade's own process-wide undo
stack and open undo group live in `undo.rs` (module 00 analogues of
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
| undo | undo.h | 37 | stack/group/command lifecycle + Qt leftovers (update_actions/actions → no-op/NULL) |
| common | config.h, videoparams.h | 34 | config over oakcommon; videoparams static tables ported from `engine/render/videoparams.cpp` |
| audio | audio.h | 26 | manager + sync; processor convert/output_params stubbed (interface mismatch) |
| plugin | plugin.h | 4 | callbacks are facade state; push_button stubbed (no module API) |
| codec | encoding.h | 81/85 | metadata family over oakcodec (`include/codec/format.h`); params handle is a facade box over the `oakcodec_encoding_params` POD; presets/load-save/export stubs |
| render | renderer.h, color.h, lut.h | 60/60 | renderer over oakrender tickets; frame accessors over `OakCodecFrame`; color processor over `oakrender_color_processor_*`; color-manager list queries + LUT library stubs |
| node | node.h, project.h, footage.h | 226/327 | the node graph, project and footage families over the oaknode C ABI (`include/node/*.h`); 101 documented stubs where the module lacks the surface (gizmos, plugin messages, input properties, brush, thumbnail/waveform caches, shape/subtitle, keyframe enumeration, ...) — see the stub bodies |
| timeline | timeline.h | 126/139 | sequences/clips/tracks/markers/workarea over oaknode + oaktimeline; 13 documented stubs (ripple-tracks command, default transitions, move-track/clip, marker-create, auto-cache, cache invalidation, multicam find/switch — module-surface gaps, see the stub bodies) |
| task | task.h | 27 | the background-task system over oaktask (manager + load/save/import/export creators + result accessors); `create_proxy` stubbed (the module has no proxy-task C creator); start-time/is-cancelled are facade-approximated |

Deferred/stub detail lives in [`deferred`] and in the stub bodies' doc
comments. The worker/IPC families (`worker.h`/`ipc.h`) are **not** part of
the facade anymore: the frozen C++ ABI does not include them, so the
render worker's runtime and the shared-memory frame-slot transport moved
into the `oak-worker` crate (`crates/oak-worker/src/{worker,ipc}.rs`,
self-contained, direct Rust calls into oakrender); the ipc.h control-plane
message serializers remain unwrapped.

## Testing

The module crates are real dependencies, so `cargo test` links the same
rlibs the cdylib embeds; the dev-dependencies re-declare
`oakcommon`/`oakplugin` with their `test-stubs` features so the test
binaries keep the in-crate mocks (ffmpeg_bridge stub / render mocks):

- `oaknode`/`oaktimeline`/`oaktask` are linked WITHOUT their `test-stubs`
  features: their in-crate mocks would collide with the real oakundo rlib
  in one test binary. Without test-stubs their real exports reference the
  oaknode/oakundo/oakcommon C ABI symbols as link-time externs, which the
  sibling crate rlibs provide; oaknode itself resolves cross-module
  symbols at runtime with `dlsym(RTLD_DEFAULT)`.
- `tests/common/mod.rs` re-exports the facade's in-dylib
  `oakcore_audioparams_*` accessors (M12 P5 folded them in) and
  force-links the oakcommon XML writer/reader + the oakundo command
  factory so the oaknode serializer's dlsym lookups resolve in every test
  binary.
- `src/lib.rs`'s test-only `test_link` forces the oakrender/oaknode/
  oaktimeline/oaktask rlibs into the lib unit-test binary (the always-on
  `src/linkage.rs` anchors are `#[cfg(not(test))]`; they are what embeds
  the module C ABIs in the cdylib for `cargo build`).

Families whose wrapped behavior requires the real module dylibs carry
`#[ignore]` tests with a documented reason; the smoke tests here exercise
the module crates' real implementations.

```
cargo test          # lib tests + integration families (undo, common, audio,
                    # plugin, codec, render, linkage, node, timeline, task)
cargo build         # cdylib embeds the module C ABIs + the folded-in
                    # oakcore_audioparams_* accessors (no undefined imports)
```

## FFI discipline

Every export goes through a `catch_unwind` guard
(`handle::guard*`); `*_free` is a NULL no-op; strings use the two-stage
buf/size convention; module error codes pass through untranslated;
handles are refcounted module values wrapped in opaque boxes.
