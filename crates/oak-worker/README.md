# oak-worker (Rust)

Headless render worker process — the Rust rewrite of `worker/workermain.cpp`
(contract: `engine/include/oakengine/worker.h` and
`engine/include/oakengine/ipc.h`).

## Build and test

```sh
cargo build --release      # binary: target/release/oak-worker
cargo test                 # unit + integration tests
```

The worker is **self-contained** (single-lib unification): the engine's
frozen C++ ABI does not include the worker/IPC families, so the whole
runtime is compiled into this binary and links the module crates directly
— no `liboakengine` dylib is needed at build or run time.

- `src/worker.rs` is the port of `engine/src/capi/worker.cpp`
  `oakengine_worker_main()` and owns the whole runtime: render backend
  selection through the oakrender crate's direct Rust API (dynamic →
  OpenGL fallback), the startup handshake and the NDJSON control loop.
  `src/main.rs` only parses `--backend` (clap) and forwards.
- `src/ipc.rs` owns the shared-memory frame-slot transport (the real
  `SpscRingBuffer` + `FrameSlotPool` over POSIX `shm_open`/`mmap`) and the
  NDJSON control-plane message structs; `src/transport.rs` attaches
  through it.

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

1. **parse `--backend <name>`** (clap; default `opengl`; `none` skips
   renderer creation and the process exits 1, like the C++ main).
2. **initialize the render backend** (inside `src/worker.rs`): the
   oakrender `DisplayRenderer` direct Rust API, falling back to the direct
   OpenGL renderer exactly like the C++ `create_renderer()` fallback
   chain.
3. **write the startup handshake** (protocol version 1, empty shared-memory
   geometry — same as the C++ worker's startup handshake; the parent
   creates the segments and announces their geometry in its reply).
4. **serve the NDJSON control loop** on stdin/stdout until a `shutdown`
   message or EOF: `handshake` attaches the announced shared-memory
   frame-slot pools through the real transport; `load_graph` /
   `render_frame` / `cancel` / `shutdown` are dispatched by the session.
   Responses are one compact JSON line per message.

## Implemented vs stubbed (nothing is faked)

**Real:** argument parsing, render backend initialization (real wgpu
renderer, dynamic → OpenGL fallback), startup handshake, NDJSON framing,
message validation (protocol version, handshake geometry, `load_graph`
file existence/size — the same messages the C++ worker emits), the
**shared-memory frame-slot transport** (`src/ipc.rs` — POSIX
`shm_open`/`mmap`/`munmap`/`shm_unlink`, the SPSC ring buffer and the
frame-slot pool with the exact version-1 shared layout; a `handshake`
genuinely attaches the output and input pools), unknown-type/
malformed-message errors, shutdown/EOF termination.

**Stubbed (documented in `src/transport.rs`):**

| area | reason |
|---|---|
| node-graph deserialization (`load_graph` beyond the file checks) | the oaknode crate is a `todo!()` skeleton |
| frame rendering (`render_frame`) | no graph/render-pipeline backing (the shm frame-slot transport is attached, but there is no graph to render) |

Stubbed requests answer with a clear `{"type":"error","message":…}` that
names the missing piece (a `render_frame` error also carries the ticket,
mirroring the C++ `error_message()` shape). A real `load_graph` on a
non-existent/empty file produces the C++-identical error before reaching
the stub.

**Deviation from the C++:** the startup handshake omits `gl_major`/
`gl_minor` — the oakrender module exposes no GL context version (the C++
worker reads them off its `QOpenGLContext`).

## Layout

```
src/
  main.rs       clap entry; thin shell forwarding to worker::worker_main
                (renderer init, handshake, NDJSON loop all live there)
  worker.rs     the real worker runtime: backend selection (oakrender
                DisplayRenderer), WorkerSession, handshake + NDJSON loop
  ipc.rs        control-plane message structs + NDJSON framing (serde),
                AND the real shared-memory frame-slot transport
                (SpscRingBuffer + FrameSlotPool over POSIX shm)
  session.rs    in-process session mirror (message dispatch + real shm
                handshake attach), exercised by the unit tests
  transport.rs  shared-memory frame-slot transport over crate::ipc
tests/worker.rs binary-level tests (help, clap errors, --backend none exit 1)
```

The NDJSON control-loop behavior is exercised in-process in `src/worker.rs`
and `src/session.rs` against the local real shared memory (no GPU needed
via `--backend none` sessions); a binary-level loop test would require a
working GPU backend and is deliberately not part of the unit suite. Run
the binary against a created segment to see the real attach path:

```sh
target/release/oak-worker --backend opengl <<< '{"type":"shutdown"}'
```
