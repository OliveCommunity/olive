# oak-worker (Rust)

Headless render worker process — the Rust rewrite of `worker/workermain.cpp`
(contract: `engine/include/oakengine/worker.h` and
`engine/include/oakengine/ipc.h`).

## Build and test

```sh
cargo build --release      # binary: target/release/oak-worker
cargo test                 # unit + integration tests (29 tests)
```

The worker is a **thin shell over the facade**, exactly like the C++
`worker/workermain.cpp` is a thin shell over `liboakengine`:

- `oakfacade::worker::worker_main` (the port of
  `engine/src/capi/worker.cpp` `oakengine_worker_main()`) owns the whole
  runtime: render backend selection through the oakrender module C ABI
  (dynamic → OpenGL fallback), the startup handshake and the NDJSON
  control loop. `src/main.rs` only parses `--backend` (clap) and forwards.
- `oakfacade::ipc` owns the shared-memory frame-slot transport (the real
  `SpscRingBuffer` + `FrameSlotPool` over POSIX `shm_open`/`mmap`);
  `src/transport.rs` attaches through it.

The oakrender module crate (`../../src/render/rust`) is linked so the
facade's renderer imports resolve; oakrender depends on `ocio-rs` with the
`bundled` feature, whose first-time build fetches a vendored OpenColorIO
dependency (`sse2neon`) from github.com. On networks without github access,
build with a shared target directory that already contains a completed
oakrender build tree, e.g.:

```sh
CARGO_TARGET_DIR=/path/to/oak/src/render/rust/target cargo build --release
```

## What the worker does

Same flow as the C++ main, in the same order:

1. **parse `--backend <name>`** (clap; default `opengl`; `none` skips
   renderer creation and the process exits 1, like the C++ main).
2. **initialize the render backend** (inside `oakfacade::worker`): the
   oakrender module C ABI `oakrender_display_renderer_create_dynamic` +
   `_init`, falling back to the direct OpenGL renderer exactly like the
   C++ `create_renderer()` fallback chain.
3. **write the startup handshake** (protocol version 1, empty shared-memory
   geometry — same as the C++ worker's startup handshake; the parent
   creates the segments and announces their geometry in its reply).
4. **serve the NDJSON control loop** on stdin/stdout until a `shutdown`
   message or EOF: `handshake` attaches the announced shared-memory
   frame-slot pools through the real transport; `load_graph` /
   `render_frame` / `cancel` / `shutdown` are dispatched by the facade
   session. Responses are one compact JSON line per message.

## Implemented vs stubbed (nothing is faked)

**Real:** argument parsing, render backend initialization (real wgpu
renderer, dynamic → OpenGL fallback), startup handshake, NDJSON framing,
message validation (protocol version, handshake geometry, `load_graph`
file existence/size — the same messages the C++ worker emits), the
**shared-memory frame-slot transport** (`oakfacade::ipc` — POSIX
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
`gl_minor` — the oakrender module C ABI exposes no GL context version (the
C++ worker reads them off its `QOpenGLContext`).

## Layout

```
src/
  main.rs       clap entry; thin shell forwarding to oakfacade::worker
                (renderer init, handshake, NDJSON loop all live there)
  ipc.rs        control-plane message structs + NDJSON framing (serde)
  session.rs    in-process session mirror (message dispatch + real shm
                handshake attach), exercised by the unit tests
  transport.rs  real shared-memory frame-slot transport over oakfacade::ipc
tests/worker.rs binary-level tests (help, clap errors, --backend none exit 1)
```

The NDJSON control-loop behavior is exercised in-process in `src/session.rs`
against the facade's real shared memory (no GPU needed via `--backend none`
sessions); a binary-level loop test would require a working GPU backend and
is deliberately not part of the unit suite. Run the binary against a
created segment to see the real attach path:

```sh
target/release/oak-worker --backend opengl <<< '{"type":"shutdown"}'
```
