# oakcore-rs — Rust value-type foundation for the oak Rust modules

> Status: **declaration draft for review** (no implementation, not wired
> into any build). Companion to `crates/oaknode` and `crates/oakrender`.

Rust reimplementation of the oakcore C++ value types that cross every
module boundary: `Rational`, `TimeRange`, `TimeRangeList`, pixel/sample
format enums. These are pure data types with value semantics — the one
place where duplicating the C++ layout discipline in plain Rust is both
safe and required (a Rust crate cannot hold C++ objects by value).

Rules:

- Bit-exact arithmetic compatibility with `olive::core::Rational`
  (reduction, overflow behavior, comparison) — the golden rule is the
  C++ test-suite semantics, not "ideal" rational math.
- `#[repr(C)]` only where a type crosses the C ABI; everything else is
  plain Rust with `Copy + Clone + Eq + Hash`.
- No I/O, no allocation in arithmetic paths, no panics on degenerate
  input (denominator zero follows the C++ sentinel semantics).
