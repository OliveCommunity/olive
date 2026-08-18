# oak-worker (Rust)

Headless render worker process — the Rust rewrite of `worker/workermain.cpp`
(contract: `engine/include/oakengine/worker.h` and
`engine/include/oakengine/ipc.h`).

## Build and test

```sh
cargo build --release      # binary: target/release/oak-worker
cargo test                 # unit + integration tests
```

The worker is **self-contained** (M14 R2): the whole runtime is compiled
into this binary and links the module crates directly — no `liboakengine`
dylib is needed at build or run time.

- `src/worker.rs` is the port of `engine/src/capi/worker.cpp`
  `oakengine_worker_main()` and owns the whole runtime: render backend
  selection through the oakrender crate's direct Rust API (dynamic →
  OpenGL fallback), the startup handshake and the NDJSON control loop.
  Since M15 S1 it also renders for real: `load_graph` deserializes the
  snapshot through `oaknode::serializer`, and `render_frame` /
  `render_batch` render through `oakrender::eval` (generated frames,
  footage decode via oakcodec/ffmpeg, montage compositing) directly into
  the main-assigned shared-memory slots.
- `src/ipc.rs` is a shim re-exporting `oakrender::ipc` (M15 S1): the
  NDJSON protocol and the shared-memory frame-slot transport moved to the
  oakrender crate so both ends of the pipe link one copy (the
  main-process dispatcher in `oakrender::procpool` creates the segments;
  this worker attaches to them).

The oakrender module crate (`../oakrender`) is a plain Rust dependency;
it depends on `ocio-rs` with the `bundled` feature, whose first-time build
fetches a vendored OpenColorIO dependency (`sse2neon`) from github.com. On
networks without github access, build with a shared target directory that
already contains a completed oakrender build tree, e.g.:

```sh
CARGO_TARGET_DIR=/path/to/oak/crates/oakrender/target cargo build --release
```

## What the worker does

Same flow as the C++ main, in the same order:

1. **parse `--backend <name>`** (default `opengl`; `none` skips
   renderer creation and the process exits 1, like the C++ main;
   `cpu` is the M15 headless render mode — no renderer, but the session
   stays fully operational and renders through the CPU evaluation path).
2. **initialize the render backend** (inside `src/worker.rs`): the
   oakrender `DisplayRenderer` direct Rust API, falling back to the direct
   OpenGL renderer exactly like the C++ `create_renderer()` fallback
   chain. Then the runtime services load (color-manager default config,
   the oakplugin render executor).
3. **write the startup handshake** (protocol version 1, empty shared-memory
   geometry — same as the C++ worker's startup handshake; the parent
   creates the segments and announces their geometry in its reply).
4. **serve the NDJSON control loop** on stdin/stdout until a `shutdown`
   message or EOF: `handshake` attaches the announced shared-memory
   frame-slot pools through the real transport and answers `hello_caps`
   (protocol v2: supported slot formats + max slot size); `load_graph`
   deserializes the graph snapshot; `render_frame` renders one frame
   into an acquired slot; `render_batch` renders a batch of
   main-assigned-slot tickets (`batch_accepted` claim confirmation, then
   one `frame_ready`/`frame_failed` per ticket); `cancel` / `shutdown`
   are dispatched by the session. Responses are one compact JSON line
   per message.

## Implemented vs stubbed (nothing is faked)

**Real:** argument parsing, render backend initialization (real wgpu
renderer, dynamic → OpenGL fallback), runtime initialization (color
config + oakplugin render executor), startup handshake, NDJSON framing,
message validation (protocol version, handshake geometry, `load_graph`
file existence/size), the **shared-memory frame-slot transport**
(`oakrender::ipc` — POSIX `shm_open`/`mmap`/`munmap`/`shm_unlink`, the
SPSC ring buffer and the frame-slot pool with the exact version-1 shared
layout; a `handshake` genuinely attaches the output and input pools),
**graph deserialization** (`oaknode::serializer::load`, plus the minimal
`{"project_copy":N}` identity payload), **frame rendering into shm
slots** (`render_frame` v1 + `render_batch` v2: generated frames,
footage decode, montage compositing, end-of-pipe F32→BGRA8 conversion),
unknown-type/malformed-message errors, shutdown/EOF termination.

**Deferred to M15 S2/S3 (documented in `src/worker.rs`):** the loaded
project's node-graph render path (plugin-node evaluation per graph
snapshot update) — today tickets render from their wire spec
(montage/footage/generate), which covers the preview pipeline.

**Deviation from the C++:** the startup handshake omits `gl_major`/
`gl_minor` — the oakrender module exposes no GL context version (the C++
worker reads them off its `QOpenGLContext`).

## Crash-isolation test hooks

The batch render path honors two environment variables used by the
crash-isolation integration tests (`tests/procpool_integration.rs`):

- `OAK_WORKER_CRASH_ON_TICKET=<n>` — raise `SIGSEGV` while rendering
  ticket `n` (like a real plugin crash).
- `OAK_WORKER_CRASH_MARKER=<path>` — when the marker file exists the
  crash is skipped; the hook writes the marker before dying, making the
  crash one-shot so the restarted worker renders the re-queued frame.

They are test-only; unset in production.

## Layout

```
src/
  main.rs     argv --backend scanning (default opengl; last flag wins);
              forwards to worker::worker_main
  worker.rs   the real worker runtime: backend selection (oakrender
              DisplayRenderer, dynamic -> OpenGL fallback), WorkerSession,
              handshake + NDJSON loop, real load_graph + render_frame +
              render_batch (M15 S1)
  ipc.rs      shim re-exporting oakrender::ipc (M15 S1: both pipe ends
              link one copy of the protocol + shm transport)
tests/worker.rs             binary-level tests (--backend none exit 1;
                            --backend cpu handshake + clean exit)
tests/procpool_integration.rs  M15 S1 end-to-end: real workers spawned by
                            oakrender::procpool::ProcessDispatcher — batch
                            renders into shm slots, crash isolation with
                            restart + re-dispatch, zero-copy assertions
```

The NDJSON control-loop behavior is exercised in-process in `src/worker.rs`
against the local real shared memory (`--backend none` / `--backend cpu`
sessions, no GPU needed); `tests/procpool_integration.rs` drives real
worker processes end-to-end through the main-process dispatcher. Run the
binary against a created segment to see the real attach path:

```sh
target/release/oak-worker --backend cpu <<< '{"type":"shutdown"}'
```
