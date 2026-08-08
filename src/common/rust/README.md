# oakcommon Rust crate

> Status: **implemented**. All `include/common/*.h` contracts are
> implemented in Rust and covered by unit + C ABI integration tests
> (see [Testing](#testing)).

## Scope

Replaces the C++ oakcommon module (`src/common/src`): config store,
command-line parser, XML stream reader/writer, file functions,
debug/logging, ffmpeg/OCIO/OIIO utility queries, video/subtitle
params, color transform, misc utilities. Pure leaf module — depends
only on `oakcore-rs`, `quick-xml`, `log`, `ocio-rs`, `image`, and
system libraries.

Public contract: `include/common/*.h` (18 headers) — frozen,
implemented verbatim by `src/ffi.rs`.

## Third-party crates

| Crate | Version | License | Status |
|---|---|---|---|
| `quick-xml` | 0.41 | MIT | adopted — XML reader/writer (`xmlutils.rs`) |
| `log` | 0.4 | MIT / Apache-2.0 | adopted — logging facade (`debug.rs`); the stderr sink is retained as the always-available backend |
| `ocio-rs` | 0.2.1 | BSD-3-Clause | adopted — real OpenColorIO access for `ocioutils.rs` (`OcioConfig`/`OcioProcessor`, `BitDepth` mapping). Pulled in via `ocio-sys` built with `OCIO_RS_ENABLE_REAL=1` against the Homebrew OCIO install (see `.cargo/config.toml`) |
| `image` | 0.25 | MIT / Apache-2.0 | adopted — per-channel bit-depth tables and 32-bit float TIFF I/O for `oiioutils.rs` (`image_color_type_for`/`bits_per_channel`, `F32Image`). Default features off, `tiff` only |
| `oakcore-rs` | path | GPL-3.0 | adopted — `Rational::from_double` (the C++ `Rational::from_double` port of FFmpeg's `av_d2q`) for `get_pixel_aspect_ratio`; a hand-written port kept in the leaf crate instead of pulling in `ffmpeg-next` |
| `serde_json` | — | MIT / Apache-2.0 | **evaluated, not adopted** — the ConfigStore format is INI (QSettings-style `key=value` with `[group]` sections, `%g` doubles), not JSON; switching would break C++/Rust file interop |
| `pico-args` / `clap` | — | MIT / Apache-2.0 | **evaluated, not adopted** — `commandlineparser.rs` must keep exact C++ quirks (case-insensitive names, first-match-wins, last-value-wins, argv[0] skipping, truncating getter copies, borrowed C ABI handles) that a generic parser cannot express without changing the C ABI shape |

## Architectural decisions

1. **Leaf module discipline**: no `bridge/` to other oak modules.
   FFmpeg is reached through `ffmpeg_bridge`'s C ABI (narrow
   `extern "C"` blocks in `ffmpegutils.rs`); OCIO and OIIO access is
   pure Rust via the crates.io bindings listed in the table above
   (`ocioutils.rs`, `oiioutils.rs`).
2. **`olive::Variant` disappears**: it exists in C++ only because
   QVariant left a hole. Rust modules use closed enums; nothing in
   common needs it. `variant.{h,cpp}` (C++) is retired when all
   consumers are Rust.
3. **XML**: `XmlStreamReader/Writer` keep the C++ streaming API shape
   (the C ABI is built on it), implemented over quick-xml — behavior
   (attribute order, error semantics) pinned by tests against the C++
   oracle.
4. **Config**: the ConfigStore is **INI-backed** (QSettings-style
   `key=value` with `[group]` sections, `;`/`#` comments, `%g`
   double formatting), keeping the exact C++ file format and lookup
   order (user config → app defaults). It is *not* JSON — an earlier
   draft described it as JSON-backed, which was wrong; that claim was
   removed from this document.
5. **Logging**: `debug.rs` provides the leveled logger
   (qWarning/qDebug/qCritical/qInfo replacement) with a printf-style C
   ABI. Every record is written to stderr (the C++ `stderr_sink`
   parity) and additionally forwarded to the `log` crate's global
   logger when the host has installed one. oakcommon never installs a
   global logger itself — the C ABI is loaded into hosts that set
   their own, and `log::set_logger` can only be called once per
   process.
6. **OCIO / OIIO / FFmpeg**: `ocioutils.rs` talks to real OpenColorIO
   through the crates.io `ocio-rs` bindings (`OcioConfig`/`OcioProcessor`,
   `BitDepth` enum — no hand-written constant tables); `oiioutils.rs`
   derives its OIIO base-type mapping from the `image` crate's color-type
   tables (HALF pinned from the frozen OIIO table — `image` has no f16
   sample type) and converts aspect ratios with
   `oakcore_rs::Rational::from_double` — a hand-written port of FFmpeg's
   `av_d2q` matching the C++ `Rational::from_double` exactly, kept in the
   leaf crate rather than adding `ffmpeg-next`/`ffmpeg-sys-next` (narrow
   extern C discipline). 32-bit float image I/O (`F32Image`) is pure
   `image`. All adopted crates are registered in the table above.

## Layout

```
src/
  lib.rs            crate doc + module map
  error.rs          error codes (include/common/error.h)
  handle.rs         refcounted-handle scaffolding
  configstore.rs    INI config (include/common/config.h)
  commandlineparser.rs
  xmlutils.rs       streaming XML reader/writer
  filefunctions.rs  file/dir helpers
  debug.rs          leveled logging
  ffmpegutils.rs    pixfmt/samplefmt mapping (via ffmpeg_bridge C ABI)
  ocioutils.rs      OCIO queries
  oiioutils.rs      OIIO queries
  videoparams.rs    VideoParams plain data + queries
  subtitleparams.rs SubtitleParams
  colortransform.rs ColorTransform plain data
  miscutils.rs      misc (loop mode, drop behavior, power, current…)
  ffi.rs            export layer (one submodule per public header)
tests/              contract tests per module
```

## Testing

`cargo test --release --features test-stubs` runs the full suite:
unit tests, C ABI contract tests, and the integration tests (incl.
`tests/ffi_ffmpegutils.rs`). The `test-stubs` feature substitutes the
in-crate ffmpeg_bridge mock (`fb_find_best_pix_fmt_of_list` stub, see
`src/ffmpegutils.rs`) so the C ABI tests link without
libffmpeg_bridge; without the feature that symbol is imported from
`ffmpeg_bridge` at link time. This mirrors the `test-stubs`
convention of oakplugin / oaktimeline.
