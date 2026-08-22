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

//! Platform display ICC profile lookup for color management.
//!
//! Resolves the filesystem path of the main display's ICC profile for the
//! OCIO pipeline (`oak_render::color::ColorProcessor::create_display_icc`).
//! `system_display_icc()` is the single entry point; it honors the
//! `OAK_DISPLAY_ICC` override first, then asks the platform:
//!
//! * macOS — the CoreGraphics main-display color space; the ICC bytes are
//!   materialized under the disk cache (`<cache>/icc/<hash>.icc`).
//! * Windows — the ICM profile file path (`GetICMProfileW`); Windows
//!   profiles are already files, so no cache copy is needed.
//! * Linux — the `_ICC_PROFILE` X11/XWayland root-window property via
//!   `xprop`, then the `colormgr` CLI chain (colord); bytes from `xprop`
//!   are materialized into the disk cache.
//!
//! Every platform query is best-effort: any failure (headless session,
//! missing tooling, unparseable output) silently degrades to `None`.

// Platform-specific imports live in the cfg-gated modules below; the only
// shared one is `Path`, which `write_icc_cache` (non-Windows) needs.
#[cfg(not(target_os = "windows"))]
use std::path::Path;

/// The filesystem path of the main display's ICC profile, ready for an
/// OCIO FileTransform. None when the platform gives no answer (headless,
/// no colord, no X server). The `OAK_DISPLAY_ICC` environment variable
/// overrides everything (tests, debugging).
pub fn system_display_icc() -> Option<String> {
	// The override wins outright — tests and debugging bypass the platform
	// queries entirely. An empty value is treated as unset and falls through
	// to the platform lookup.
	if let Ok(path) = std::env::var("OAK_DISPLAY_ICC") {
		if !path.is_empty() {
			return Some(path);
		}
	}
	platform_display_icc()
}

#[cfg(target_os = "macos")]
fn platform_display_icc() -> Option<String> {
	macos::display_icc()
}

#[cfg(target_os = "windows")]
fn platform_display_icc() -> Option<String> {
	windows::display_icc()
}

#[cfg(target_os = "linux")]
fn platform_display_icc() -> Option<String> {
	linux::display_icc()
}

#[cfg(not(any(target_os = "macos", target_os = "windows", target_os = "linux")))]
fn platform_display_icc() -> Option<String> {
	None
}

/// Materialize `bytes` under `<disk cache>/icc/<fnv1a hex>.icc`.
///
/// The file name is the FNV-1a hash of the content
/// (`filefunctions::fnv1a_hex`), so an entry with identical bytes is reused
/// as-is and a different profile lands in a different file. Returns `None`
/// when the cache directory is not writable. Windows never needs this: ICM
/// profiles are already files on disk.
#[cfg(not(target_os = "windows"))]
fn write_icc_cache(bytes: &[u8]) -> Option<String> {
	if bytes.is_empty() {
		return None;
	}
	let cache_root = crate::filefunctions::default_disk_cache_path();
	let dir = Path::new(&cache_root).join("icc");
	std::fs::create_dir_all(&dir).ok()?;
	let path = dir.join(format!("{}.icc", crate::filefunctions::fnv1a_hex(bytes)));
	if !path.exists() {
		std::fs::write(&path, bytes).ok()?;
	}
	Some(path.to_string_lossy().into_owned())
}

/// Parse the `0xHH, 0xHH, ...` byte list from an `xprop` `_ICC_PROFILE` line.
///
/// `xprop -root _ICC_PROFILE` prints the property as
/// `_ICC_PROFILE(8)\t= 0x3c, 0x6f, ...`. Everything before the first `=` is
/// ignored (a suffixed `_ICC_PROFILE_1` name parses identically); after it,
/// each `0x`-prefixed 1-2 digit hex token contributes one byte and any other
/// token is skipped. Returns `None` when no byte can be parsed — the
/// property is absent (xprop prints an error instead), empty, or the value
/// is malformed.
#[cfg(any(target_os = "linux", test))]
fn parse_xprop_icc_hex(output: &str) -> Option<Vec<u8>> {
	let after_eq = output.split('=').nth(1)?.to_ascii_lowercase();
	let mut bytes = Vec::new();
	let mut rest = after_eq.as_str();
	while let Some(pos) = rest.find("0x") {
		let digits_start = pos + 2;
		let mut n_digits = 0;
		for ch in rest[digits_start..].chars() {
			if n_digits == 2 || !ch.is_ascii_hexdigit() {
				break;
			}
			n_digits += 1;
		}
		if n_digits > 0 {
			if let Ok(byte) =
				u8::from_str_radix(&rest[digits_start..digits_start + n_digits], 16)
			{
				bytes.push(byte);
			}
		}
		rest = &rest[digits_start + n_digits..];
	}
	if bytes.is_empty() {
		None
	} else {
		Some(bytes)
	}
}

/// macOS: CoreGraphics main-display color space → ICC bytes → cache file.
#[cfg(target_os = "macos")]
mod macos {
	use std::ffi::c_void;

	use super::write_icc_cache;

	#[link(name = "CoreGraphics", kind = "framework")]
	extern "C" {
		/// `CGDirectDisplayID` of the main display.
		fn CGMainDisplayID() -> u32;
		/// Copy rule: returns a retained `CGColorSpaceRef`, or NULL when the
		/// display has no color space (e.g. headless).
		fn CGDisplayCopyColorSpace(display: u32) -> *mut c_void;
		/// Copy rule: returns a retained `CFDataRef` of the ICC bytes, or
		/// NULL when the color space carries no ICC data.
		fn CGColorSpaceCopyICCData(space: *const c_void) -> *mut c_void;
	}

	#[link(name = "CoreFoundation", kind = "framework")]
	extern "C" {
		/// `CFIndex` byte length of a `CFDataRef`.
		fn CFDataGetLength(data: *const c_void) -> isize;
		/// Pointer to a `CFDataRef`'s bytes (valid while the data is alive).
		fn CFDataGetBytePtr(data: *const c_void) -> *const u8;
		/// Release a Core Foundation object (Copy rule).
		fn CFRelease(obj: *const c_void);
	}

	pub(super) fn display_icc() -> Option<String> {
		let display = unsafe { CGMainDisplayID() };
		let space = unsafe { CGDisplayCopyColorSpace(display) };
		if space.is_null() {
			return None;
		}
		let data = unsafe { CGColorSpaceCopyICCData(space) };
		if data.is_null() {
			unsafe { CFRelease(space) };
			return None;
		}
		let len = unsafe { CFDataGetLength(data) };
		let ptr = unsafe { CFDataGetBytePtr(data) };
		// The byte pointer is only valid while `data` is alive, so copy the
		// bytes out before releasing anything.
		let bytes = if len > 0 && !ptr.is_null() {
			unsafe { std::slice::from_raw_parts(ptr, len as usize) }.to_vec()
		} else {
			Vec::new()
		};
		unsafe {
			CFRelease(data);
			CFRelease(space);
		}
		write_icc_cache(&bytes)
	}
}

/// Windows: ICM profile file path of the main display (`GetICMProfileW`).
#[cfg(target_os = "windows")]
mod windows {
	use std::ffi::c_void;
	use std::path::Path;

	#[link(name = "user32")]
	extern "system" {
		/// Device context for the whole screen (`hwnd == NULL`); released
		/// with `ReleaseDC`.
		fn GetDC(hwnd: *const c_void) -> *mut c_void;
		fn ReleaseDC(hwnd: *const c_void, hdc: *mut c_void) -> i32;
	}

	#[link(name = "gdi32")]
	extern "system" {
		/// `BOOL GetICMProfileW`: two-stage — a NULL buffer first yields the
		/// required `WCHAR` count (including the NUL), then the profile file
		/// path is written into the caller's buffer.
		fn GetICMProfileW(hdc: *mut c_void, name_len: *mut u32, name: *mut u16) -> i32;
	}

	pub(super) fn display_icc() -> Option<String> {
		let hdc = unsafe { GetDC(std::ptr::null()) };
		if hdc.is_null() {
			return None;
		}

		// Stage 1: required buffer size, in `WCHAR`s including the NUL.
		let mut len: u32 = 0;
		let ok = unsafe { GetICMProfileW(hdc, &mut len, std::ptr::null_mut()) };
		if ok == 0 || len == 0 {
			unsafe { ReleaseDC(std::ptr::null(), hdc) };
			return None;
		}

		// Stage 2: fetch the path. One spare `WCHAR` guards against drivers
		// that report a length without the terminator.
		let mut buf = vec![0u16; len as usize + 1];
		let ok = unsafe { GetICMProfileW(hdc, &mut len, buf.as_mut_ptr()) };
		unsafe { ReleaseDC(std::ptr::null(), hdc) };
		if ok == 0 {
			return None;
		}

		let path = String::from_utf16_lossy(&buf[..len as usize]);
		let path = path.trim_end_matches('\0');
		if path.is_empty() || !Path::new(path).is_file() {
			return None;
		}
		Some(path.to_string())
	}
}

/// Linux: `_ICC_PROFILE` X11/XWayland property, then the colord `colormgr`
/// CLI chain. Sources are tried in order; every failure degrades to `None`.
#[cfg(target_os = "linux")]
mod linux {
	use std::io::Read;
	use std::process::{Command, Stdio};
	use std::thread;
	use std::time::{Duration, Instant};

	use super::*;

	pub(super) fn display_icc() -> Option<String> {
		// 1. X11 (and XWayland) publish the ICC bytes as root-window
		//    properties. The primary monitor keeps the bare `_ICC_PROFILE`
		//    name; extra monitors append `_1`, `_2`, ... — the bare name is
		//    tried first.
		for prop in ["_ICC_PROFILE", "_ICC_PROFILE_1", "_ICC_PROFILE_2"] {
			let mut cmd = Command::new("xprop");
			cmd.args(["-root", prop]);
			let Some(out) = run_capture(&mut cmd, Duration::from_secs(2)) else {
				continue;
			};
			let Some(bytes) = parse_xprop_icc_hex(&String::from_utf8_lossy(&out)) else {
				continue;
			};
			if let Some(path) = write_icc_cache(&bytes) {
				return Some(path);
			}
		}

		// 2. colord, via the `colormgr` CLI.
		colord_icc_path()
	}

	/// Run `cmd`, returning its captured stdout.
	///
	/// Returns `None` when the command cannot be started, exits non-zero, is
	/// still running after `timeout` (it is killed), or produces no output.
	fn run_capture(cmd: &mut Command, timeout: Duration) -> Option<Vec<u8>> {
		let mut child = cmd
			.stdout(Stdio::piped())
			.stderr(Stdio::null())
			.spawn()
			.ok()?;
		let deadline = Instant::now() + timeout;
		loop {
			match child.try_wait() {
				Ok(Some(status)) => {
					if !status.success() {
						return None;
					}
					break;
				}
				Ok(None) => {
					if Instant::now() >= deadline {
						let _ = child.kill();
						let _ = child.wait();
						return None;
					}
					thread::sleep(Duration::from_millis(10));
				}
				Err(_) => return None,
			}
		}
		let mut out = Vec::new();
		let _ = child.stdout.take()?.read_to_end(&mut out);
		if out.is_empty() {
			None
		} else {
			Some(out)
		}
	}

	/// colord default-display profile file path via the `colormgr` CLI.
	///
	/// The chain is `get-default-device` → `device-get-default-profile` →
	/// `get-profile` (its "Filename:" field). NOT verified on a live colord
	/// installation (none available in this environment): when any step's
	/// output cannot be parsed, we silently return `None` rather than guess
	/// at the format.
	fn colord_icc_path() -> Option<String> {
		// 1. Object path of the default device.
		let device = colormgr_value(&["get-default-device"])?;
		// 2. Object path of that device's default profile.
		let profile = colormgr_value(&["device-get-default-profile", &device])?;
		// 3. The profile's file name.
		let mut cmd = Command::new("colormgr");
		cmd.args(["get-profile", &profile]);
		let out = run_capture(&mut cmd, Duration::from_secs(2))?;
		let text = String::from_utf8_lossy(&out);
		let line = text
			.lines()
			.map(str::trim_start)
			.find(|l| l.starts_with("Filename:"))?;
		let name = line.splitn(2, ':').nth(1)?.trim();
		if name.is_empty() {
			None
		} else {
			Some(name.to_string())
		}
	}

	/// Run `colormgr <args>` and extract its value: a ColorManager object
	/// path when the output contains one, otherwise the first non-empty
	/// line. Deliberately permissive — no specific output layout is assumed.
	fn colormgr_value(args: &[&str]) -> Option<String> {
		let mut cmd = Command::new("colormgr");
		cmd.args(args);
		let out = run_capture(&mut cmd, Duration::from_secs(2))?;
		let text = String::from_utf8_lossy(&out);
		let value = text
			.split_whitespace()
			.find(|t| t.starts_with("/org/freedesktop/ColorManager/"))
			.or_else(|| text.lines().map(str::trim).find(|l| !l.is_empty()))
			.unwrap_or("");
		if value.is_empty() {
			None
		} else {
			Some(value.to_string())
		}
	}
}

#[cfg(test)]
mod tests {
	use super::*;
	use std::sync::{Mutex, MutexGuard};

	// Env-mutating tests serialize on the crate-wide test lock (shared with
	// the configstore/filefunctions tests, which also touch `OAK_CONFIG_DIR`).
	fn env_lock() -> &'static Mutex<()> {
		crate::test_support::env_lock()
	}

	fn unique_temp_dir(tag: &str) -> std::path::PathBuf {
		let dir = std::env::temp_dir().join(format!(
			"oak-displayicc-{}-{}-{}",
			tag,
			std::process::id(),
			std::time::SystemTime::now()
				.duration_since(std::time::UNIX_EPOCH)
				.unwrap()
				.as_nanos()
		));
		std::fs::create_dir_all(&dir).unwrap();
		dir
	}

	#[test]
	fn parse_xprop_hex_normal() {
		let out = "_ICC_PROFILE(8)\t= 0x3c, 0x6f, 0x6f, 0x0";
		assert_eq!(parse_xprop_icc_hex(out), Some(vec![0x3c, 0x6f, 0x6f, 0x0]));
	}

	#[test]
	fn parse_xprop_hex_suffixed_name() {
		// Multi-monitor X servers name extra properties `_ICC_PROFILE_1`,
		// `_ICC_PROFILE_2`, ...; the byte list format is unchanged.
		let out = "_ICC_PROFILE_1(8)\t= 0x00, 0x01";
		assert_eq!(parse_xprop_icc_hex(out), Some(vec![0x00, 0x01]));
	}

	#[test]
	fn parse_xprop_hex_empty_and_absent() {
		// xprop prints an error to stderr for a missing property...
		assert_eq!(parse_xprop_icc_hex("xprop:  error: Invalid atom"), None);
		// ...and an existing-but-empty property has nothing to parse.
		assert_eq!(parse_xprop_icc_hex("_ICC_PROFILE(8)\t= "), None);
		assert_eq!(parse_xprop_icc_hex(""), None);
	}

	#[test]
	fn parse_xprop_hex_malformed() {
		// Invalid `0x` tokens are skipped, valid neighbours still parse.
		assert_eq!(
			parse_xprop_icc_hex("_ICC_PROFILE(8)\t= 0xZZ, 0x3c, 0xGG"),
			Some(vec![0x3c])
		);
		// A lone `0x` prefix contributes no byte.
		assert_eq!(parse_xprop_icc_hex("_ICC_PROFILE(8)\t= 0x"), None);
		// No `=` separator at all.
		assert_eq!(parse_xprop_icc_hex("0x3c 0x6f"), None);
		// `0X` prefixes and uppercase digits are accepted.
		assert_eq!(
			parse_xprop_icc_hex("_ICC_PROFILE(8)\t= 0X3C, 0X6f"),
			Some(vec![0x3c, 0x6f])
		);
		// Bare hex without an `0x` prefix is not a byte token.
		assert_eq!(parse_xprop_icc_hex("_ICC_PROFILE(8)\t= 3c, 6f"), None);
	}

	#[test]
	fn env_override_wins() {
		let _guard: MutexGuard<()> = env_lock().lock().unwrap();
		let dir = unique_temp_dir("override");
		let profile = dir.join("fake.icc");
		std::fs::write(&profile, b"fake").unwrap();
		std::env::set_var("OAK_DISPLAY_ICC", &profile);
		let got = system_display_icc();
		std::env::remove_var("OAK_DISPLAY_ICC");
		assert_eq!(got, Some(profile.to_string_lossy().into_owned()));
	}

	#[test]
	fn empty_override_falls_through() {
		let _guard: MutexGuard<()> = env_lock().lock().unwrap();
		std::env::set_var("OAK_DISPLAY_ICC", "");
		let with_empty = system_display_icc();
		std::env::remove_var("OAK_DISPLAY_ICC");
		let without = system_display_icc();
		assert_eq!(with_empty, without);
	}

	#[test]
	fn platform_query_does_not_panic() {
		// Smoke test for the platform FFI path: it must return without
		// panicking, and any path it yields must be non-empty. A headless
		// environment legitimately produces None.
		if let Some(path) = system_display_icc() {
			assert!(!path.is_empty());
		}
	}

	#[cfg(not(target_os = "windows"))]
	#[test]
	fn icc_cache_roundtrip_and_dedup() {
		let _guard: MutexGuard<()> = env_lock().lock().unwrap();
		let dir = unique_temp_dir("cache");
		std::env::set_var("OAK_CONFIG_DIR", &dir);
		let bytes = b"\xacsp sRGB IEC61966-2.1 profile bytes".to_vec();

		let p1 = write_icc_cache(&bytes).expect("cache write");
		let p2 = write_icc_cache(&bytes).expect("cache write");
		assert_eq!(p1, p2, "identical content reuses the same file");
		assert_eq!(std::fs::read(&p1).unwrap(), bytes);
		assert!(p1.ends_with(".icc"));
		// The file name embeds the 16-hex-digit content hash.
		let stem = Path::new(&p1).file_stem().unwrap().to_str().unwrap();
		assert_eq!(stem.len(), 16);
		assert!(stem.chars().all(|c| c.is_ascii_hexdigit()));
		// The cache lives under the configured location.
		assert!(Path::new(&p1).starts_with(&dir));
		// Different content -> different file.
		let p3 = write_icc_cache(b"other bytes").unwrap();
		assert_ne!(p1, p3);

		std::env::remove_var("OAK_CONFIG_DIR");
		let _ = std::fs::remove_dir_all(&dir);
	}
}
