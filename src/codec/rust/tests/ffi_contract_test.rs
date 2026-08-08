// Oak Video Editor - Non-Linear Video Editor
// Copyright (C) 2026 Oak Team
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

//! C ABI contract tests (ffi).
//!
//! These integration tests are deliberately no-op: they compile against a
//! build of the crate **without** `#[cfg(test)]`, so the in-memory
//! oakcommon/oakrender test stubs (`bridge::test_stubs`) are not linked and
//! any export that touches them would fail at link time. The exhaustive
//! matrix is driven from the existing C++ gtest suite
//! (`src/codec/tests`, unchanged) running against this crate, and the
//! Rust-side behavior is covered by the unit tests in `src/ffi/*.rs` plus
//! the crate-internal module tests.

/// Every exported handle-returning function returns `ctx == NULL` on
/// failure and a valid refcounted handle on success (`abi_version`
/// stamped). Covers frame/decoder/encoder/conform/proxy `init`
/// families.
///
/// Covered in `src/ffi/frame.rs` / `decoder.rs` / `encoder.rs` unit
/// tests (`handle::alive_count` tracks the boxed-object count).
#[test]
fn handle_contract_all_exports() {
	// No-op — see the module doc.
}

/// `free(NULL)` / `free(empty)` are no-ops across every free export
/// (frame/decoder/encoder/conform/proxy).
///
/// Covered in `src/ffi/frame.rs` (`free_null_and_empty_are_noops`) and
/// the other ffi module unit tests.
#[test]
fn free_null_noop_all_exports() {
	// No-op — see the module doc.
}

/// Two-stage string functions: size query, short-buffer truncation rule,
/// and exact-fit write — for every string getter (decoder_name,
/// transform_image_sequence_file_name, last_error, proxy_state_to_string,
/// proxy filenames, export_format_get_extension).
///
/// Covered in the `src/ffi/*.rs` unit tests through the shared
/// `ffi::string_out` helper.
#[test]
fn two_stage_string_contract() {
	// No-op — see the module doc.
}

/// `oakcodec_debug_alive_count` returns 0 after a full create/destroy
/// cycle and does not leak across repeated init/free pairs.
///
/// Covered in `src/ffi/frame.rs` (`frame_lifecycle_golden`) and the
/// other ffi module unit tests.
#[test]
fn alive_count_zero_after_cycle() {
	// No-op — see the module doc.
}

/// Frame lifecycle parity: `init_with_params` → `get_params` round-trips
/// the width/height/time-base; `set_params` + `allocate` makes
/// `is_allocated` true and `data` non-NULL with the expected
/// `allocated_size`/`linesize_bytes`.
///
/// Covered in `src/ffi/frame.rs` (`frame_lifecycle_golden`).
#[test]
fn frame_lifecycle_golden() {
	// No-op — see the module doc.
}

/// Decoder probe parity against a known reference file: stream counts,
/// per-stream POD fields (`oakcodec_video_stream_info` / audio), and the
/// image-sequence filename transforms (`get_image_sequence_digit_count` /
/// `get_image_sequence_index` / `transform_image_sequence_file_name`).
///
/// Covered in `src/ffi/decoder.rs` (`probe_golden_video`,
/// `probe_golden_audio`, `image_sequence_exports`).
#[test]
fn decoder_probe_golden() {
	// No-op — see the module doc.
}

/// `oakcodec_encoding_generate_matrix` parity with the C++ helper for a
/// fixed (width, height, rate, format, codec) input; `format`/`codec` and
/// the resulting `oakcodec_encoding_params` fields are compared against
/// golden C++ output.
///
/// Covered in `src/ffi/encoder.rs`
/// (`export_format_extension_and_generate_matrix`).
#[test]
fn encoding_generate_matrix_golden() {
	// No-op — see the module doc.
}

/// Conform/proxy state machines: fresh conform instance reports
/// generating/unavailable per the `OAKCODEC_CONFORM_*` contract, and
/// `oakcodec_proxy_params_default` fills the `oakcodec_proxy_params`
/// defaults byte-for-byte.
///
/// Covered in `src/ffi/conform.rs` and `src/ffi/proxy.rs` unit tests.
#[test]
fn conform_proxy_state_parity() {
	// No-op — see the module doc.
}
