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

//! Cache contract tests (cache.rs). Mirrors the C++ oakrender cache
//! gtest expectations plus Rust-side ownership rules.

mod common;

use std::ffi::c_char;

use oakrender::error::{OAKRENDER_E_INVALID, OAKRENDER_E_NOT_FOUND, OAKRENDER_OK};
use oakrender::ffi;
use oakrender::handle::CHandle;

/// A fresh detached video-frame cache.
fn cache() -> ffi::OakRenderCache {
	unsafe { ffi::cache::oakrender_cache_create() }
}

fn free(mut c: ffi::OakRenderCache) {
	unsafe { ffi::cache::oakrender_cache_free(&mut c) };
}

/// invalidate/validate state machine: fresh cache reports the whole
/// query range invalidated; validate shrinks it; invalidate splits.
#[test]
fn invalidate_validate_state_machine() {
	let _dir = common::CacheDirGuard::new();
	let c = cache();
	unsafe {
		// Fresh cache: nothing validated.
		assert_eq!(ffi::cache::oakrender_cache_has_validated_ranges(c), 0);
		assert_eq!(ffi::cache::oakrender_cache_set_timebase(c, 1, 25), OAKRENDER_OK);

		// Whole query range invalidated on a fresh cache (1 second).
		let mut ranges = [0i64; 16];
		let count = ffi::cache::oakrender_cache_get_invalidated_ranges(
			c, 0, 1, 1, 1, ranges.as_mut_ptr(), 4,
		);
		assert_eq!(count, 1);
		assert_eq!(ranges[0], 0);
		assert_eq!(ranges[1], 1);
		assert_eq!(ranges[2], 1);
		assert_eq!(ranges[3], 1);

		// Validate timestamps [0, 25) at 25fps → the second [0, 1).
		ffi::cache::oakrender_cache_validate(c, 0, 25);
		assert_eq!(ffi::cache::oakrender_cache_has_validated_ranges(c), 1);
		let count = ffi::cache::oakrender_cache_get_invalidated_ranges(
			c, 0, 1, 1, 1, ranges.as_mut_ptr(), 4,
		);
		assert_eq!(count, 0);

		// Invalidate timestamps [10, 20) → [0.4, 0.8) invalidated.
		ffi::cache::oakrender_cache_invalidate(c, 10, 20);
		assert_eq!(ffi::cache::oakrender_cache_has_validated_ranges(c), 1);
		let count = ffi::cache::oakrender_cache_get_invalidated_ranges(
			c, 0, 1, 1, 1, ranges.as_mut_ptr(), 4,
		);
		assert_eq!(count, 1);
		assert_eq!(ranges[0], 2);
		assert_eq!(ranges[1], 5);
		assert_eq!(ranges[2], 4);
		assert_eq!(ranges[3], 5);

		// Invalidate everything: back to empty.
		ffi::cache::oakrender_cache_invalidate(c, 0, 25);
		assert_eq!(ffi::cache::oakrender_cache_has_validated_ranges(c), 0);

		// Empty cache: no-op + zero results.
		ffi::cache::oakrender_cache_invalidate(CHandle::null(), 0, 10);
		ffi::cache::oakrender_cache_validate(CHandle::null(), 0, 10);
		assert_eq!(ffi::cache::oakrender_cache_has_validated_ranges(CHandle::null()), 0);
		// Negative max_ranges rejected.
		assert_eq!(
			ffi::cache::oakrender_cache_get_invalidated_ranges(
				c, 0, 1, 1, 1, std::ptr::null_mut(), -1
			),
			OAKRENDER_E_INVALID
		);
	}
	free(c);
}

/// Timebase validation + timestamp semantics.
#[test]
fn timebase_and_timestamp_semantics() {
	let c = cache();
	unsafe {
		assert_eq!(ffi::cache::oakrender_cache_set_timebase(c, 1, 25), OAKRENDER_OK);
		assert_eq!(
			ffi::cache::oakrender_cache_set_timebase(CHandle::null(), 1, 25),
			OAKRENDER_E_INVALID
		);
		assert_eq!(ffi::cache::oakrender_cache_set_timebase(c, 0, 25), OAKRENDER_E_INVALID);
		assert_eq!(ffi::cache::oakrender_cache_set_timebase(c, 1, 0), OAKRENDER_E_INVALID);

		// Validate [0,25) timestamps at 25fps → 1 second.
		ffi::cache::oakrender_cache_validate(c, 0, 25);
		let mut num = 0;
		let mut den = 0;
		assert_eq!(ffi::cache::oakrender_cache_get_timebase(c, &mut num, &mut den), OAKRENDER_OK);
		assert_eq!(num, 1);
		assert_eq!(den, 25);

		// set_uuid round-trips through the two-stage getter.
		assert_eq!(
			ffi::cache::oakrender_cache_set_uuid(
				c,
				c"{01234567-89ab-cdef-0123-456789abcdef}".as_ptr()
			),
			OAKRENDER_OK
		);
		let (_, uuid) = common::read_two_stage(|buf, n| ffi::cache::oakrender_cache_get_uuid(c, buf, n));
		assert_eq!(uuid.as_deref(), Some("{01234567-89ab-cdef-0123-456789abcdef}"));
		assert_eq!(ffi::cache::oakrender_cache_set_uuid(c, std::ptr::null()), OAKRENDER_E_INVALID);
	}
	free(c);
}

/// Passthrough: linked cache ranges are excluded from
/// invalidated_ranges; unlink restores them.
#[test]
fn passthrough_excludes_ranges() {
	let a = cache();
	let b = cache();
	unsafe {
		ffi::cache::oakrender_cache_validate(b, 0, 10);
		// Empty `other` → invalid.
		assert_eq!(
			ffi::cache::oakrender_cache_set_passthrough(a, CHandle::null()),
			OAKRENDER_E_INVALID
		);
		assert_eq!(ffi::cache::oakrender_cache_set_passthrough(a, b), OAKRENDER_OK);

		// Passthrough ranges report count; the range excluded from invalidated.
		let mut passthroughs = [0i64; 16];
		let count = ffi::cache::oakrender_cache_get_passthroughs(a, passthroughs.as_mut_ptr(), 4);
		assert_eq!(count, 1);
		assert_eq!(passthroughs[0], 0);
		assert_eq!(passthroughs[2], 10);

		let mut ranges = [0i64; 16];
		let count = ffi::cache::oakrender_cache_get_invalidated_ranges(
			a, 0, 1, 20, 1, ranges.as_mut_ptr(), 4,
		);
		assert_eq!(count, 1, "only [10,20) remains invalidated");
		assert_eq!(ranges[0], 10);

		// Invalidating over the passthrough unlinks it (C++ semantics).
		ffi::cache::oakrender_cache_invalidate_range(a, 5, 1, 15, 1);
		let count = ffi::cache::oakrender_cache_get_passthroughs(a, std::ptr::null_mut(), 0);
		assert_eq!(count, 0);
	}
	free(a);
	free(b);
}

/// Disk state round-trip: save_state → clear → load_state restores
/// validated ranges byte-identically (C++ binary format parity).
#[test]
fn disk_state_roundtrip() {
	let _dir = common::CacheDirGuard::new();
	let dir = std::env::temp_dir().join(format!(
		"oakrender-ffi-cache-{}",
		std::process::id()
	));

	let c = cache();
	unsafe {
		ffi::cache::oakrender_cache_set_timebase(c, 1001, 30000);
		ffi::cache::oakrender_cache_validate(c, 0, 30);
		ffi::cache::oakrender_cache_validate(c, 60, 90);
		assert_eq!(ffi::cache::oakrender_cache_save_state(c), OAKRENDER_OK);

		// Load into a fresh cache: the saved uuid + state come back.
		let c2 = cache();
		let (_, uuid) = common::read_two_stage(|buf, n| ffi::cache::oakrender_cache_get_uuid(c, buf, n));
		ffi::cache::oakrender_cache_set_uuid(c2, uuid.unwrap().as_ptr() as *const c_char);
		ffi::cache::oakrender_cache_set_timebase(c2, 1001, 30000);
		assert_eq!(ffi::cache::oakrender_cache_load_state(c2), OAKRENDER_OK);
		assert_eq!(ffi::cache::oakrender_cache_has_validated_ranges(c2), 1);
		let mut ranges = [0i64; 16];
		let count = ffi::cache::oakrender_cache_get_invalidated_ranges(
			c2, 0, 1, 300, 1, ranges.as_mut_ptr(), 8,
		);
		// validated [0,30) + [60,90) at 30000/1001 fps → two gaps:
		// [1001/1000, 1001/500) and [3003/1000, 300).
		assert_eq!(count, 2);
		assert_eq!((ranges[0], ranges[1], ranges[2], ranges[3]), (1001, 1000, 1001, 500));
		assert_eq!((ranges[4], ranges[5], ranges[6], ranges[7]), (3003, 1000, 300, 1));
		// Empty cache load → invalid.
		assert_eq!(
			ffi::cache::oakrender_cache_load_state(CHandle::null()),
			OAKRENDER_E_INVALID
		);
		free(c2);
	}
	free(c);
}

/// frame_filename is deterministic for (uuid, timebase, time) and
/// matches the C++ naming scheme exactly (shared disk caches between
/// C++ and Rust builds must not diverge).
#[test]
fn frame_filename_parity() {
	let _dir = common::CacheDirGuard::new();
	let c = cache();
	unsafe {
		ffi::cache::oakrender_cache_set_timebase(c, 1, 30);
		ffi::cache::oakrender_cache_set_uuid(c, c"{01234567-89ab-cdef-0123-456789abcdef}".as_ptr());
		// Frame 15 at 30fps = 0.5 s.
		let (size, name) = common::read_two_stage(|buf, n| {
			ffi::cache::oakrender_cache_get_valid_cache_filename(c, 1, 2, buf, n)
		});
		// Not validated yet → NOT_FOUND.
		assert_eq!(size, OAKRENDER_E_NOT_FOUND);
		assert!(name.is_none());

		// Validate frame 15 (0.5s → 1.0s) and query again.
		ffi::cache::oakrender_cache_validate(c, 15, 16);
		let (size, name) = common::read_two_stage(|buf, n| {
			ffi::cache::oakrender_cache_get_valid_cache_filename(c, 1, 2, buf, n)
		});
		assert!(size > 0);
		let name = name.unwrap();
		let suffix = format!(
			"{}/15",
			"{01234567-89ab-cdef-0123-456789abcdef}"
		);
		assert!(
			name.ends_with(&suffix),
			"filename scheme `<dir>/<uuid>/<timestamp>`; got {name}"
		);

		// Non-frame-hash caches reject the query (audio kind).
		let audio = ffi::cache::oakrender_cache_create_for_node(common::fake_handle(1), 2);
		assert_eq!(
			ffi::cache::oakrender_cache_get_valid_cache_filename(audio, 1, 2, std::ptr::null_mut(), 0),
			OAKRENDER_E_INVALID
		);
		let mut audio = audio;
		ffi::cache::oakrender_cache_free(&mut audio);
	}
	free(c);
}

/// Lock/unlock pairing and empty-cache no-op.
#[test]
fn lock_pairing() {
	let c = cache();
	unsafe {
		ffi::cache::oakrender_cache_lock(c);
		ffi::cache::oakrender_cache_unlock(c);
		// Empty cache: no-ops.
		ffi::cache::oakrender_cache_lock(CHandle::null());
		ffi::cache::oakrender_cache_unlock(CHandle::null());
	}
	free(c);
}

/// Frame-cache load failure paths (the success path needs oakcodec).
#[test]
fn frame_cache_load_errors() {
	let c = cache();
	unsafe {
		// NULL args → invalid.
		assert_eq!(
			ffi::cache::oakrender_frame_cache_load(
				c,
				std::ptr::null(),
				std::ptr::null(),
				0,
				std::ptr::null_mut()
			),
			OAKRENDER_E_INVALID
		);
		// Missing file → NOT_FOUND.
		let dir = std::env::temp_dir().join("oakrender-nonexistent-cache");
		let dir_c = std::ffi::CString::new(dir.to_string_lossy().as_bytes()).unwrap();
		let mut out = CHandle::null();
		assert_eq!(
			ffi::cache::oakrender_frame_cache_load(
				c,
				dir_c.as_ptr(),
				c"{00000000-0000-0000-0000-000000000000}".as_ptr(),
				5,
				&mut out
			),
			OAKRENDER_E_NOT_FOUND
		);
		// Existing file but no codec ABI → FAILED (explainable).
		let dir2 = std::env::temp_dir().join("oakrender-existing-cache");
		std::fs::create_dir_all(dir2.join("{00000000-0000-0000-0000-000000000000}")).unwrap();
		std::fs::write(
			dir2.join("{00000000-0000-0000-0000-000000000000}").join("5"),
			b"not-an-exr",
		)
		.unwrap();
		let dir2_c = std::ffi::CString::new(dir2.to_string_lossy().as_bytes()).unwrap();
		let rc = ffi::cache::oakrender_frame_cache_load(
			c,
			dir2_c.as_ptr(),
			c"{00000000-0000-0000-0000-000000000000}".as_ptr(),
			5,
			&mut out,
		);
		assert!(
			rc != OAKRENDER_OK,
			"decode is codec-dependent; without oakcodec it must fail explainably"
		);
		// save with empty args: no-op.
		ffi::cache::oakrender_frame_cache_save(CHandle::null(), std::ptr::null(), std::ptr::null(), CHandle::null());
	}
	free(c);
}

/// Borrowed cache handles are opaque this pass (C++ interop pending).
#[test]
fn borrowed_cache_is_opaque() {
	unsafe {
		let borrowed = ffi::cache::oakrender_cache_wrap_borrowed(0x1234 as *mut std::ffi::c_void);
		assert!(!borrowed.is_null(), "non-null native pointer → non-empty handle");
		// Queries on a borrowed box are invalid (cannot dereference the
		// C++ object through the Rust ABI).
		assert_eq!(
			ffi::cache::oakrender_cache_get_uuid(borrowed, std::ptr::null_mut(), 0),
			OAKRENDER_E_INVALID
		);
		let mut b = borrowed;
		ffi::cache::oakrender_cache_free(&mut b);
	}
}

/// Frame-cache save/load round-trip through the oakcodec EXR/JPEG
/// payload codec. Gated: the oakcodec crate is finished concurrently.
#[test]
#[ignore = "needs oakcodec final"]
fn frame_cache_save_load_roundtrip() {
	let _dir = common::CacheDirGuard::new();
	let c = cache();
	unsafe {
		ffi::cache::oakrender_cache_set_timebase(c, 1, 30);
		let frame = ffi::renderer::oakrender_codec_frame_create();
		let mut pod = std::mem::zeroed::<ffi::OakRenderVideoParams>();
		pod.width = 8;
		pod.height = 8;
		pod.format = 4; // F32
		assert_eq!(ffi::renderer::oakrender_codec_frame_set_video_params(frame, &pod), 0);
		assert_eq!(ffi::renderer::oakrender_codec_frame_allocate(frame), 0);
		let dir = _dir.dir();
		let dir_c = std::ffi::CString::new(dir.to_string_lossy().as_bytes()).unwrap();
		let uuid = c"{01234567-89ab-cdef-0123-456789abcdef}".as_ptr();
		ffi::cache::oakrender_frame_cache_save(c, dir_c.as_ptr(), uuid, frame);
		let mut out = CHandle::null();
		assert_eq!(
			ffi::cache::oakrender_frame_cache_load(c, dir_c.as_ptr(), uuid, 0, &mut out),
			0,
			"cached frame decodes back"
		);
		assert!(!out.is_null());
		assert_eq!(ffi::renderer::oakrender_codec_frame_width(out), 8);
		let mut out = out;
		ffi::renderer::oakrender_codec_frame_free(&mut out);
		let mut frame = frame;
		ffi::renderer::oakrender_codec_frame_free(&mut frame);
	}
	free(c);
}

/// Saving toggle affects the auto-save behavior.
#[test]
fn saving_enabled_toggle() {
	let c = cache();
	unsafe {
		assert_eq!(ffi::cache::oakrender_cache_set_saving_enabled(c, 0), OAKRENDER_OK);
		assert_eq!(
			ffi::cache::oakrender_cache_set_saving_enabled(CHandle::null(), 1),
			OAKRENDER_E_INVALID
		);
		assert_eq!(ffi::cache::oakrender_cache_indicator_height(), 4);
	}
	free(c);
}
