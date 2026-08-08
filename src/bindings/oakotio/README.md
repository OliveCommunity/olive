# oakotio

Pure-Rust OpenTimelineIO JSON binding for the Oak Video Editor's Rust
rewrite. This crate is a self-contained serde model of the OTIO JSON format,
covering exactly the object graph Oak's project load/save tasks use
(`src/task/src/project/loadotio/loadotio.cpp` and
`src/task/src/project/saveotio/saveotio.cpp`): `RationalTime`, `TimeRange`,
`Clip`, `Gap`, `Transition`, `Track`, `Stack`, `Timeline`,
`ExternalReference`, `MissingReference` and `SerializableCollection`.

The writer reproduces the opentimelineio C++ writer's output byte for byte
(4-space indentation, `": "` separators, inline empty objects and arrays,
shortest float representation, no trailing newline); the reader tolerates
hand-written files, preserving unknown fields verbatim across a round-trip
and defaulting missing fields.

Part of the Oak `src/bindings/` family (siblings: `oakaudioout`).
This is an **rlib** — nothing is exported dynamically.

## Structure

```
src/bindings/oakotio/
├── Cargo.toml            # rlib; deps: serde, serde_json (crates.io), oakcore-rs (path)
├── src/
│   ├── lib.rs            # crate docs + module wiring + re-exports + from_json_* entry points
│   ├── error.rs          # OtioError (Json | Io) + Result<T>
│   └── model.rs          # serde structs for the 10 OTIO schemas + value types +
│                         #   Rational::from_double port + in-crate unit tests
└── tests/
    ├── data/             # C++-writer golden files (golden_timeline.json,
    │                     #   golden_collection.json, golden_typed_transition.json,
    │                     #   floatfmt.json)
    ├── parity.rs         # read parity: every golden file parses and round-trips
    ├── semantic.rs       # semantic checks over golden_timeline.json (what the
    │                     #   C++ load task reads back)
    └── save_parity.rs    # save parity: a built Timeline serializes byte-identical
                          #   to golden_timeline.json and re-parses identically
```

## API summary

- `from_json_string(&str) -> Result<Serializable>` /
  `from_json_file(path) -> Result<Serializable>` — parse a document whose root
  is a `Timeline`, a `SerializableCollection`, or an unknown schema kept whole
  as `Serializable::Raw`.
- `RationalTime` — `new(value, rate)` (C++ argument order), `value`, `rate`,
  `to_seconds`, `is_invalid_time`, `invalid_time`, `rescaled_to`,
  `to_rational`, `from_rational`.
- `TimeRange` — `new(start_time, duration)` (C++ argument order), `start_time`,
  `duration`.
- `Clip` — `new(name)`, `name`, `source_range`, `set_source_range`,
  `media_reference`, `media_references`, `set_media_reference`.
- `Gap` — `new(source_range, name)`, `name`, `source_range`.
- `Transition` — `new(name)`, `name`, `in_offset`, `out_offset`,
  `transition_type`, `set_in_offset`, `set_out_offset`.
- `Track` — `new(kind)`, `kind`, `children`, `append_child`.
- `Stack` — `children`, `append_child`.
- `Timeline` — `new(name)`, `name`, `tracks`, `tracks_mut`,
  `global_start_time`, `to_json_string`, `to_json_file`.
- `SerializableCollection` — `new(name, children)`, `name`, `children`,
  `to_json_string`, `to_json_file`.
- `MediaReference` / `Composable` / `Serializable` enums — downcasts
  (`as_clip`, `as_track`, ...) and `schema_name` for dynamic dispatch by
  `OTIO_SCHEMA`.

All fallible operations return `Result<T, OtioError>`.

## Backend choice: hand-written serde over the `opentimelineio` crate

| Option | Verdict |
| --- | --- |
| crates.io `opentimelineio` | Not viable: the crate is unmaintained, binds the C++ library via FFI (large, ABI-fragile), and does not build a pure-Rust model Oak's load/save tasks can read directly. No actively maintained pure-Rust OTIO implementation exists on crates.io. |
| **This crate: `serde` + `serde_json`** | Pure-Rust (no C++ runtime), fully controllable field order and formatting, preserves unknown fields for forward compatibility, and ports the only piece of C++ numeric behavior Oak needs (`Rational::from_double`) on top of `oakcore_rs::Rational`. |

The C++ side serializes with `opentimelineio::schema::Timeline::to_json_string`
(4-space pretty formatter); this crate reproduces that exact writer with a
`serde_json::PrettyFormatter` (`with_indent(b"    ")`), `preserve_order` maps
so insertion order is kept, and ryu float formatting, which is what the C++
writer (rapidjson) emits. The result is byte-for-byte parity with C++-written
files (verified against the golden files).

## Dependency registry

Runtime dependencies (crates.io):

- `serde` 1 (with `derive`) — (de)serialization for the OTIO schema structs.
- `serde_json` 1 (with `preserve_order`) — JSON codec; `preserve_order` keeps
  map insertion order so metadata and unknown fields round-trip in file
  order.
- `oakcore-rs` (path: `../../oakcore-rs`) — shared `Rational` value type
  (used by the `Rational::from_double` port); same path dependency the other
  bindings use.

Build and test:

```sh
cd src/bindings/oakotio
cargo build
cargo test
```

## C++ parity notes

The C++ anchors this crate reproduces:

- **`Rational::from_double`** (`core/src/util/rational.cpp`) — ported in
  `model.rs` on top of `oakcore_rs::Rational::new` (which applies the exact
  C++ `reduce_fraction(INT_MAX)` reduction). NaN and out-of-range magnitudes
  collapse to the null sentinel `Rational::NULL`; the retry pass against
  `INT64_MAX` fires for tiny magnitudes and is itself reduced back to 0/1 by
  the `INT_MAX` ceiling, matching the C++ result.
- **Writer format** — `opentimelineio::schema::Timeline::to_json_string`:
  4-space indentation, `": "` separators, inline empty `{}`/`[]`, shortest
  float representation (ryu = rapidjson), no trailing newline. Golden files
  written by the C++ writer round-trip byte-identically.
- **Field order** — struct field order matches the C++ writer's output order
  (e.g. `RationalTime`: `rate` then `value`; `TimeRange`: `duration` then
  `start_time`; `Track`: `children` then `kind`).
- **`media_references`** — the C++ `Clip` stores a `std::map<string, ...>`;
  this crate uses `BTreeMap`, which serializes keys in the same sorted order.
- **Missing-reference serialization** — `MissingReference` writes
  `available_range`/`available_image_bounds` as `null` and omits `target_url`,
  exactly like the C++ writer.

## Deviations from the C++ code (deliberate)

- **Unknown fields are kept, not dropped** — the C++ reader discards
  unrecognized JSON fields; this crate preserves them (via `#[serde(flatten)]`
  catch-all maps) so a document written by a newer opentimelineio still
  round-trips. This is a superset of the C++ behavior.
- **Defaults are lenient** — missing fields deserialize to their type's
  default (the C++ `AnyDictionary` fill defaults), so hand-written files
  without optional fields parse cleanly.
- **`RationalTime`/`TimeRange` are `Clone`, not `Copy`** — they carry a
  `String` schema field, so value accessors (`value()`, `rate()`,
  `duration()`, ...) take `&self` and return clones; the C++ value semantics
  (`to_seconds`, `rescaled_to`) are unaffected.

## Scope

Covers only what `loadotio.cpp` / `saveotio.cpp` touch. No media-resolution,
no `Marker`/`Effect` schemas (kept as raw `Value` for round-tripping), and no
plugin API — `src/plugin/` is intentionally untouched.
