# oakcodec Rust crate

> Status: **implemented**. Implements `include/codec/*.h` verbatim
> (`src/ffi/`); every export has success + failure-path tests
> (`cargo test`: unit tests in `src/ffi/*.rs`, the contract tests in
> `tests/`, and real-media tests in `src/realmedia_tests.rs`). The FFmpeg
> engine is fully implemented through the [`ffmpeg-next`] crate (decode,
> probe, audio conform, encode); the OIIO engine remains a stub in this
> build. This crate mirrors the `src/node/rust/` template (same FFI
> discipline, same testing layers).

## Scope

Replaces the C++ oakcodec module (`src/codec/src`, ~10k lines): CPU
frame buffers (`Frame`), the frame pool (`FrameManager`), media
decoders/encoders with their FFmpeg and OIIO implementations, audio
conform and proxy generation managers, export format/codec tables,
encoding parameters, and the background-task submit hook.

Public contract: `include/codec/*.h` (7 headers: frame.h, decoder.h,
encoder.h, conform.h, proxy.h, task.h, error.h) — frozen, implemented
verbatim by `src/ffi.rs`. Interim state (pre-M8) is documented in
`src/codec/NOTES.md`: conform/proxy work is delegated to the global
task submit callback and otherwise reports unavailable, never crashes
and never blocks.

## Key architectural decisions (C++ → Rust mapping)

1. **`shared_ptr` → refcounted `RefBox` handle.** The C++ `Frame`/
   `Decoder`/`Encoder` objects are heap boxes behind the neutral
   by-value handle struct `{ctx, addref, release, abi_version}` (see
   `handle.rs`), exactly as oaknode/oakplugin do. Handles are
   deliberately duplicated per module: the function pointers always
   point into the DLL that created the object.
2. **Inheritance → traits.** The C++ `Decoder`/`Encoder` abstract
   bases plus their FFmpeg/OIIO subclasses become a Rust trait with
   two implementors. The probe/dispatch (decide which implementation
   recognizes a file) stays in `decoder.rs`. `Encoder`'s per-codec
   `PixelFormat`/`SampleFormat` support is a trait query, not a
   virtual chain.
3. **`Frame` owns its params by value.** `olive::Frame` wraps an
   `OakVideoParams` handle (an oakcommon by-value handle, NOT owned by
   codec) plus a `Vec<u8>` pixel buffer. In Rust the params are held as
   the oakcommon handle (refcounted through `bridge::common`) so the
   byte-level ABI stays unchanged; the buffer is a plain `Vec<u8>`.
4. **No adapter layer.** Codec calls other modules' C ABIs directly
   (`bridge/common.rs`, `bridge/render.rs`), keeping the 2026-08
   decision recorded in NOTES.md §6. Only genuinely repeated
   conversions survive as small module-local helpers (e.g.
   `fill_render_params`, `cancel_atom_is_cancelled`).
5. **XML stays on the C++ side.** `EncodingParams::load/save` use
   oakcommon's C++ `XmlStreamWriter/Reader` classes
   (`src/common/src/xmlutils.h`), exactly as oaknode/oakrender do —
   the one C++-to-C++ coupling the bridge cannot cover (NOTES.md §7).
6. **Threading.** `FrameManager` keeps its background GC thread behind
   a `Mutex`; the C++ code's reliance on Qt's event thread is gone. The
   threading contract is documented per function.
7. **Enum values are the C contract.** `ExportFormat::Format`,
   `ExportCodec::Codec`, `Interlacing`, `VideoScalingMethod`,
   `SampleFormat::Format` all stay as the raw int values the C ABI
   documents (oakengine/encoding.h), so `ffi.rs` marshals them without
   translation.

## Layout

```
src/
  lib.rs         crate doc + module map
  error.rs       error codes (mirrors include/codec/error.h)
  handle.rs      refcounted-handle scaffolding (same pattern as node)
  frame.rs       Frame (CPU pixel buffer + OakVideoParams handle)
  framemanager.rs FrameManager (buffer pool + background GC thread)
  decoder.rs     Decoder trait + CodecStream + RenderMode + probe
  ffmpeg.rs      FFmpegDecoder / FFmpegEncoder (ffmpeg-next)
  oiio.rs        OIIODecoder / OIIOEncoder (OpenImageIO)
  oiioframebridge.rs oiioutils frame<->buffer conversion
  encoder.rs     Encoder trait (abstract base)
  encodingparams.rs EncodingParams (flattened ABI POD + generate_matrix)
  exportcodec.rs ExportCodec enum + codec-name table
  exportformat.rs ExportFormat enum + extension/format table
  conformmanager.rs ConformManager (stateless, task-callback driven)
  proxymanager.rs ProxyManager (stateless, task-callback driven)
  task.rs        OakCodecTaskKind / OakCodecTaskRequest / submit hook
  timecodemetadata.rs TimecodeMetadata (SMPTE/BWF parsers)
  footagedescription.rs FootageDescription (codec-internal stream desc)
  planarfiledevice.rs PlanarFileDevice (stdio plane-channel I/O)
  realmedia_tests.rs real-media tests (demo.mp4, H.264 round-trip)
  bridge/        C ABI imports: common.rs, render.rs
  ffi.rs         include/codec/*.h export layer
tests/           contract + golden tests (see test section below)
```

## Hard rules for the implementer

1. Every `extern "C"` body goes through `handle::guard*`; no panic
   crosses FFI.
2. The handle is the only way out of the crate; the public API never
   hands out raw `&Frame`/`&Decoder` references.
3. Behavior parity with C++ is proven by the unchanged C ABI test
   suite (`src/codec/tests`) plus the contract tests in `tests/`.
4. Where C++ behavior is genuinely load-bearing but ugly, port the
   behavior, not the aesthetics; leave a `// CPP-PARITY:` comment with
   the C++ file:line.

## Dependency policy

Prefer mature third-party crates (MIT/Apache-2.0/BSD, GPL-compatible)
over hand-rolling; register each addition (name + reason) here. Large
existing C++ libraries (OTIO, OCIO, OIIO, FFmpeg) are NEVER rewritten
— they are consumed through their C ABI / bridge layers.

### Dependencies

- `oakcore-rs` (path) — oakcore value types (Rational, TimeRange,
  PixelFormat/SampleFormat) mirrored as Rust enums.
- `ffmpeg-next` 9 — the FFmpeg decode/encode engine. The C++
  `ffmpeg_bridge` library (`liboakffmpeg`) existed only to absorb FFmpeg
  API churn; the Rust crate calls `ffmpeg-next` directly (per the 2026-08
  decision that dropped the binding-library plan). `ffmpeg-next` builds
  against the system FFmpeg via `ffmpeg-sys-next` (bindgen); the
  implementation dips into `ffmpeg-sys-next` (`ffmpeg::ffi`) only for
  swscale/swresample details and channel-layout construction that the safe
  wrapper does not expose.
