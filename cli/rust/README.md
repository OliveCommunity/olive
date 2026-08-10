# oak-cli (Rust)

Headless command-line consumer of the `liboakengine` C ABI facade — the Rust
rewrite of `cli/main.cpp` (which stays in the tree until cutover). Same
subcommands, same output format, same exit codes:

| exit | meaning |
|---|---|
| 0 | success |
| 1 | general error (bad project/media file, no sequence, I/O failure) |
| 2 | rendering unavailable or failed (e.g. no GL render backend) |
| 64 | usage error |

## Build and test

```sh
cargo build --release      # binary: target/release/oak-cli
cargo test                 # unit + integration tests (29 tests)
```

The crate builds standalone: its only dependency besides `clap` is the
`oakfacade` rlib (`../../src/facade/rust`), which has no third-party
dependencies.

## Subcommands

Every subcommand of the C++ original is implemented:

```
oak-cli info <project.ove> <start> <end> <out_dir>   project name/sequences/footage
oak-cli render <project.ove> <start_seconds> <end_seconds> <out_dir>
oak-cli probe <mediafile>
oak-cli transcode <input_media> <out> [width] [--format ppm|mp4]
```

Argument validation is faithful to the C++ (`invalid start seconds`,
`invalid width`, `unknown --format` … all exit 64). The output formatters
(`src/fmt.rs`) reproduce the C++ `printf` output byte for byte and are
golden-tested against the output captured from the C++ binary on the test
fixtures (`tests/project_with_footage.ove`, `tests/demo.mp4`); the PPM and
WAV writers (`src/ppm.rs`, `src/wav.rs`) are the exact ports of the C++
`write_ppm`/`write_wav` and are unit-tested.

## Facade status: everything is currently deferred

All four subcommands depend on facade families that are still **deferred**
in the `oakfacade` crate (`src/facade/rust/src/deferred.rs`), so today each
subcommand validates its arguments, then prints a clear "not yet available"
error naming the missing families and the reasons, and exits with the
C++-compatible code — it never crashes and never fakes output:

| subcommand | needs | current behavior |
|---|---|---|
| `info` | init + node (project/footage) + timeline | "not yet available", exit 1 |
| `probe` | init + node (footage) | "not yet available", exit 1 |
| `render` | init + node + timeline + render | "not yet available", exit 2 |
| `transcode` | init + node + timeline + render + exporter | "not yet available", exit 2 |

The deferral registry is `src/deferred.rs` (field-for-field in sync with the
facade's own `deferred.rs`). When a family is wrapped by the facade:

1. remove its entry from `src/deferred.rs`,
2. wire the call-through in `src/cmd/` using the extern declarations in
   `src/ffi.rs` (verbatim mirrors of the engine headers) and the tested
   formatters/writers — no manifest or signature change is needed, because
   the externs resolve against the already-linked `oakfacade` rlib.

## Layout

```
src/
  main.rs       clap surface, --help/-h + unknown-command handling, dispatch
  ffi.rs        the oakengine_* surface oak-cli consumes (declarations only)
  deferred.rs   facade-family availability registry (mirror of facade deferred.rs)
  fmt.rs        golden output formatters (info/probe)
  ppm.rs        P6 PPM writer (f32/u8 frames)
  wav.rs        PCM s16 WAV writer (interleaved float samples)
  cmd/          per-subcommand validation + deferred gate
tests/cli.rs    binary-level tests (exit codes, messages, usage errors)
```
