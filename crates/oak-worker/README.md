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
  the main-assigned shared-memory slots. M15 S3 adds `render_audio_batch`
  (audio range pulls mixed by `oakrender::eval::render_audio_samples_into`
  into `SLOT_FORMAT_AUDIO_F32` slots — interleaved f32 — so audio plugin
  crashes take down this process, not the editor).
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

1. **parse `--backend <name>`** (the pool spawns workers with `auto`;
   `none` skips renderer creation and the process exits 1, like the C++
   main; `cpu` is the M15 headless render mode — no renderer, but the
   session stays fully operational and renders through the CPU
   evaluation path).
2. **initialize the render backend** (inside `src/worker.rs`): the
   oakrender `DisplayRenderer` direct Rust API. Under `auto` a failed
   initialization degrades to a cpu-mode session (logged) instead of
   exiting — GPU-less machines keep rendering through the CPU fallback.
   Then the runtime services load (color-manager default config,
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

## Where the rendering happens (no render thread)

A frequent reading trap: **oak-worker spawns no render thread.** The
binary is a deliberately single-threaded NDJSON loop — `main.rs` →
`worker::worker_main` (`src/worker.rs`) reads one control line from
stdin, handles it, writes the response, repeat. Rendering happens
**synchronously on that loop thread** the moment a batch arrives:

```
stdin "render_batch"
  → WorkerSession::handle_render_batch_stream   (src/worker.rs)
    → render_ticket_to_slot                     (acquire shm slot)
      → render_spec_pixels                      (the actual render)
    → stdout "frame_ready" / "frame_failed"     (one line per ticket)
```

`render_spec_pixels` picks between two render paths per ticket:

- **Graph path** (the default when a project snapshot is loaded):
  tickets carry `viewer_node` (the sequence's node identity), the worker
  evaluates the deserialized project through `oak_node::traverser`
  (time-aware, keyframe-resolving), and every node `value()` pushes a
  job payload that `oakrender::eval`'s resolve hooks execute — footage
  decode, **shader effects as wgpu fullscreen passes**
  (`oakrender::shaderfx`: the nodes' embedded GLSL is translated to WGSL
  through naga, uniforms packed std140), OFX plugins through the
  oakplugin render driver. Textures stay GPU-resident across the effect
  chain; the frame is read back once, at the end, into the shm slot.
- **Montage path** (fallback: no snapshot loaded / legacy tickets): the
  flat wire-spec CPU pipeline (`render_montage_frame_into`).

The parallelism is **across processes, not threads**: the main process
(app side) spawns the pool of `oak-worker` children in
`oakrender::procpool::ProcessDispatcher` (`spawn_worker` in
`crates/oak-render/src/procpool.rs`), one shared-memory segment and one
reader thread per child, and shards ticket batches across them. So
"no worker ⇒ no rendering" does not imply a hidden render thread — the
dispatcher has no in-process rendering path at all (M15 S2 deleted it;
only the test-only inline backend renders in-process). Audio follows
the same shape via `render_audio_batch` → `render_audio_ticket_to_slot`
→ `oakrender::eval::render_audio_samples_into`.

Consequences worth knowing before editing this file:

- A frame render blocks the control loop: `cancel` is observed only
  between batches (batch granularity), which is why the main process
  also stops the render loop on its side.
- A crash mid-render kills the whole loop — that is the point (OFX
  crash isolation); the dispatcher reaps, re-queues and respawns.

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

**Deferred / known limits:** the graph path is live (tickets with
`viewer_node` evaluate the loaded project through the node graph). Still
open: transition blocks render as plain cuts, polygon/mask's CPU
rasterization stage (the matte generators pass through with a TODO), and
audio still renders from the montage wire spec (graph audio evaluation
is later work).

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

## M15 S2 status

The process-isolated backend is now the **default** `RenderManager`
backend; the in-process render thread pool was deleted (the app drives the
dispatcher from its UI tick and from blocking ticket waits, and the
pre-render window feeds the scheduler ahead of the playhead). The worker
binary is located at `target/debug/oak-worker` next to the main executable
during development (or via `OAK_WORKER_BIN` / `DispatcherConfig::worker_bin`),
and bundled alongside the main binaries by the packager (root `Cargo.toml`).
