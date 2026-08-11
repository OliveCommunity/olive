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

//! C ABI contract tests. These assert the load-bearing constants, enum
//! discriminants, and handle layout that the C headers rely on. They do
//! NOT call any crate function, only public constants/types: these checks
//! are compile-time/constant-only and document the ABI surface that must
//! not drift from the headers.

use std::mem::{align_of, size_of};

use oakcommon::error::{
	OAKCOMMON_E_FAILED, OAKCOMMON_E_INVALID, OAKCOMMON_E_NOMEM, OAKCOMMON_E_NOT_FOUND,
	OAKCOMMON_E_STATE, OAKCOMMON_OK,
};
use oakcommon::ffmpegutils::{RGBA_CHANNEL_COUNT, RGB_CHANNEL_COUNT};
use oakcommon::handle::{CHandle, OAKCOMMON_ABI_VERSION};
use oakcommon::miscutils::{DropWorkflowBehavior, LoopMode, DECIBEL_MINIMUM};
use oakcommon::ocioutils::PixelFormat;
use oakcommon::videoparams::{ColorRange, Interlacing, VideoType};

/// Error codes must match `include/common/error.h`.
#[test]
fn error_codes_match_header() {
	assert_eq!(OAKCOMMON_OK, 0);
	assert_eq!(OAKCOMMON_E_INVALID, -10001);
	assert_eq!(OAKCOMMON_E_STATE, -10002);
	assert_eq!(OAKCOMMON_E_FAILED, -10003);
	assert_eq!(OAKCOMMON_E_NOT_FOUND, -10004);
	assert_eq!(OAKCOMMON_E_NOMEM, -10005);
}

/// Handle ABI version must match `include/common/handle.h`.
#[test]
fn handle_abi_version() {
	assert_eq!(OAKCOMMON_ABI_VERSION, 1);
}

/// The handle struct must be a plain `{ctx, addref, release, abi_version}`
/// `#[repr(C)]` record: 3 pointers + a u32, padded to pointer alignment.
#[test]
fn handle_layout() {
	let ptr = size_of::<*const ()>();
	let align = align_of::<*const ()>();
	let expected = (3 * ptr + size_of::<u32>()).div_ceil(align) * align;
	assert_eq!(size_of::<CHandle>(), expected);
	assert_eq!(align_of::<CHandle>(), align);
}

/// Pixel-format codes must match `olive::core::PixelFormat`.
#[test]
fn pixel_format_discriminants() {
	assert_eq!(PixelFormat::Invalid as i32, -1);
	assert_eq!(PixelFormat::U8 as i32, 0);
	assert_eq!(PixelFormat::U10 as i32, 1);
	assert_eq!(PixelFormat::U16 as i32, 2);
	assert_eq!(PixelFormat::F16 as i32, 3);
	assert_eq!(PixelFormat::F32 as i32, 4);
	assert_eq!(PixelFormat::Count as i32, 5);
}

/// Decibel minimum must match `include/common/miscutils.h`.
#[test]
fn decibel_minimum() {
	assert_eq!(DECIBEL_MINIMUM, -200.0);
}

/// Channel-count constants must match `include/common/ffmpegutils.h`.
#[test]
fn channel_count_constants() {
	assert_eq!(RGB_CHANNEL_COUNT, 3);
	assert_eq!(RGBA_CHANNEL_COUNT, 4);
}

/// Loop-mode codes must match `include/common/loopmode.h`.
#[test]
fn loop_mode_discriminants() {
	assert_eq!(LoopMode::Off as i32, 0);
	assert_eq!(LoopMode::Loop as i32, 1);
	assert_eq!(LoopMode::Clamp as i32, 2);
}

/// Drop-workflow behavior codes must match `include/common/dropworkflowbehavior.h`.
#[test]
fn drop_workflow_behavior_discriminants() {
	assert_eq!(DropWorkflowBehavior::Ask as i32, 0);
	assert_eq!(DropWorkflowBehavior::Auto as i32, 1);
	assert_eq!(DropWorkflowBehavior::Manual as i32, 2);
	assert_eq!(DropWorkflowBehavior::Disable as i32, 3);
}

/// Interlacing codes must match `include/common/videoparams.h`.
#[test]
fn interlacing_discriminants() {
	assert_eq!(Interlacing::None as i32, 0);
	assert_eq!(Interlacing::TopFirst as i32, 1);
	assert_eq!(Interlacing::BottomFirst as i32, 2);
}

/// Video-type codes must match `include/common/videoparams.h`.
#[test]
fn video_type_discriminants() {
	assert_eq!(VideoType::Video as i32, 0);
	assert_eq!(VideoType::Still as i32, 1);
	assert_eq!(VideoType::ImageSequence as i32, 2);
}

/// Color-range codes must match `include/common/videoparams.h`.
#[test]
fn color_range_discriminants() {
	assert_eq!(ColorRange::Limited as i32, 0);
	assert_eq!(ColorRange::Full as i32, 1);
}

/// The public type names must exist and be usable at their intended ABI
/// shape (compile-time contract).
#[test]
fn public_types_exist() {
	// Enums are plain C-like int enums.
	let _ = PixelFormat::U8;
	let _ = Interlacing::TopFirst;
	let _ = VideoType::Still;
	let _ = ColorRange::Full;
	let _ = LoopMode::Loop;
	let _ = DropWorkflowBehavior::Ask;

	// The handle is a plain struct constructible without a panic.
	let h = CHandle {
		ctx: std::ptr::null_mut(),
		addref: None,
		release: None,
		abi_version: 0,
	};
	assert!(h.ctx.is_null());
	assert!(h.addref.is_none());
	assert!(h.release.is_none());
	assert_eq!(h.abi_version, 0);
}
