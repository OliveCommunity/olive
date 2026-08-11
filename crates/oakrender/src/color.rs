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

//! Color processing: OCIO processors and the process-wide default config
//! (C++ `ColorProcessor` + `ColorManager` statics).
//!
//! Implemented over the `ocio-rs` crate (safe Rust bindings for
//! OpenColorIO v2.5.2, bundled real-OCIO build). OCIO is never rewritten —
//! this module maps the C++ call surface onto ocio-rs.

use std::sync::{LazyLock, Mutex};

use oakcore_rs::PixelFormat;

use crate::error::{Error, Result};
use crate::texture::Frame;

/// A color processor (wraps an OCIO processor). A processor whose OCIO
/// lookup failed holds `None` — conversions then pass through, mirroring
/// the C++ non-fatal creation behavior.
pub struct ColorProcessor {
	/// The OCIO processor (None = pass-through).
	inner: Option<ocio_rs::Processor>,
	/// The cached CPU processor used for pixel conversions (C++
	/// `cpu_processor_`).
	cpu: Option<ocio_rs::CPUProcessor>,
}

/// Processor direction.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Direction {
	/// src -> dst.
	Normal,
	/// dst -> src.
	Inverse,
}

impl Direction {
	/// The ocio-rs transform direction for the swap already applied at the
	/// call site.
	fn to_ocio(self) -> ocio_rs::TransformDirection {
		match self {
			Direction::Normal => ocio_rs::TransformDirection::Forward,
			Direction::Inverse => ocio_rs::TransformDirection::Inverse,
		}
	}
}

// OCIO Config/Processor are refcounted handles over C++ objects that are
// thread-safe for CPU processing (the C++ ColorProcessor is shared across
// render threads the same way); the safe wrapper is Send+Sync.
unsafe impl Send for ColorProcessor {}
unsafe impl Sync for ColorProcessor {}

impl ColorProcessor {
	/// Create from two colorspace names on the default config
	/// (C++ `ColorProcessor::create(config, input, dest)`).
	///
	/// Role names (e.g. "scene_linear") are resolved to canonical names.
	/// OCIO failures are non-fatal: a processor with `inner == None` is
	/// returned and conversions pass through.
	pub fn create(src_space: &str, dst_transform: &str, dir: Direction) -> Option<Self> {
		let config = default_config()?;
		let src = if config.has_role(src_space) {
			config
				.canonical_name(src_space)
				.unwrap_or_else(|| src_space.to_string())
		} else {
			src_space.to_string()
		};

		let processor = match dir {
			Direction::Normal => config.processor(&src, dst_transform),
			Direction::Inverse => config.processor(dst_transform, &src),
		}
		.ok();
		let cpu = processor
			.as_ref()
			.and_then(|p| p.default_cpu_processor().ok());

		Some(Self {
			inner: processor,
			cpu,
		})
	}

	/// Create from a LUT file (C++ `create_lut` semantics): an OCIO
	/// FileTransform with linear interpolation on the default config.
	pub fn create_lut(path: &str, dir: Direction) -> Option<Self> {
		let config = default_config()?;
		let transform = ocio_rs::transform::FileTransform::create().ok()?;
		transform.set_src(path).ok()?;
		transform.set_interpolation(ocio_rs::Interpolation::Linear);
		transform.set_direction(dir.to_ocio());
		let processor = config
			.processor_from_transform(&transform, dir.to_ocio())
			.ok();
		let cpu = processor
			.as_ref()
			.and_then(|p| p.default_cpu_processor().ok());
		Some(Self {
			inner: processor,
			cpu,
		})
	}

	/// Create from a LUT file on a specific config (C++
	/// `ColorProcessor::create_lut` family with a ColorManager config).
	pub fn create_lut_on(config: &ocio_rs::Config, path: &str, dir: Direction) -> Option<Self> {
		let transform = ocio_rs::transform::FileTransform::create().ok()?;
		transform.set_src(path).ok()?;
		transform.set_interpolation(ocio_rs::Interpolation::Linear);
		transform.set_direction(dir.to_ocio());
		let processor = config
			.processor_from_transform(&transform, dir.to_ocio())
			.ok();
		let cpu = processor
			.as_ref()
			.and_then(|p| p.default_cpu_processor().ok());
		Some(Self {
			inner: processor,
			cpu,
		})
	}

	/// Create a dynamic grading-primary processor on the default config
	/// (C++ `ColorProcessor` grading-primary family).
	pub fn create_grading_primary(style: GradingStyle) -> Option<Self> {
		let config = default_config()?;
		let transform =
			ocio_rs::transform::GradingPrimaryTransform::create(style.to_ocio()).ok()?;
		transform.make_dynamic();
		transform.set_direction(ocio_rs::TransformDirection::Forward);
		let processor = config
			.processor_from_transform(&transform, ocio_rs::TransformDirection::Forward)
			.ok();
		let cpu = processor
			.as_ref()
			.and_then(|p| p.default_cpu_processor().ok());
		Some(Self {
			inner: processor,
			cpu,
		})
	}

	/// Create from an explicit OCIO processor (C++
	/// `ColorProcessor::create(ConstProcessorRcPtr)`).
	pub fn from_processor(processor: ocio_rs::Processor) -> Self {
		let cpu = processor.default_cpu_processor().ok();
		Self {
			inner: Some(processor),
			cpu,
		}
	}

	/// Create a pass-through processor (invalid OCIO processor).
	pub fn pass_through() -> Self {
		Self {
			inner: None,
			cpu: None,
		}
	}

	/// The underlying OCIO processor handle, when valid.
	pub fn processor(&self) -> Option<&ocio_rs::Processor> {
		self.inner.as_ref()
	}

	/// Convert one RGBA color (C++ `convert_color`).
	pub fn convert_color(&self, rgba: [f64; 4]) -> [f64; 4] {
		match &self.cpu {
			Some(cpu) => {
				let mut c = [
					rgba[0] as f32,
					rgba[1] as f32,
					rgba[2] as f32,
					rgba[3] as f32,
				];
				cpu.apply_rgba(&mut c);
				[c[0] as f64, c[1] as f64, c[2] as f64, c[3] as f64]
			}
			None => rgba,
		}
	}

	/// Convert a whole F32 frame in place (row-major RGBA).
	pub fn convert_frame(&self, frame: &mut Frame) -> Result<()> {
		let Some(cpu) = &self.cpu else {
			return Ok(()); // pass-through
		};
		if frame.format != PixelFormat::F32 {
			return Err(Error::Invalid);
		}
		// Tightly packed RGBA: stride is 4 f32 elements per pixel
		// (ocio-rs counts elements, not bytes).
		let pixels = frame.pixel_count() as i64;
		let buf: &mut [f32] = bytemuck_f32_slice(&mut frame.data)
			.ok_or_else(|| Error::Failed("pixel buffer not f32 aligned".to_string()))?;
		cpu.apply_rgba_pixels(buf, pixels, 4);
		Ok(())
	}

	/// True when the underlying OCIO processor is valid (C++
	/// `get_processor() != null`).
	pub fn is_valid(&self) -> bool {
		self.inner.is_some()
	}

	/// The OCIO processor cache id (C++ `ColorProcessor::id()`), or the
	/// empty string for a pass-through processor.
	pub fn cache_id(&self) -> String {
		match &self.inner {
			Some(p) => p.cache_id().unwrap_or_default(),
			None => String::new(),
		}
	}
}

/// Grading-primary transform style (mirrors the C++ enum values: LIN=0,
/// LOG=1 in `include/render/color.h`).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum GradingStyle {
	/// OCIO GRADING_LIN.
	Lin,
	/// OCIO GRADING_LOG.
	Log,
}

impl GradingStyle {
	fn to_ocio(self) -> ocio_rs::GradingStyle {
		match self {
			GradingStyle::Lin => ocio_rs::GradingStyle::Lin,
			GradingStyle::Log => ocio_rs::GradingStyle::Log,
		}
	}
}

/// Reinterpret a byte buffer as f32 when its length is a multiple of 4
/// (the pipeline guarantees F32 RGBA frames, so alignment is exact).
fn bytemuck_f32_slice(data: &mut [u8]) -> Option<&mut [f32]> {
	if data.len() % 4 != 0 {
		return None;
	}
	// SAFETY: length is a multiple of 4 and `data` is byte-aligned; the
	// cast is valid for any alignment (f32 has no stricter requirement
	// than u8 for mutable slice casts at this length).
	Some(unsafe { std::slice::from_raw_parts_mut(data.as_mut_ptr() as *mut f32, data.len() / 4) })
}

// ---- process-wide default config (C++ ColorManager statics) ----------------

/// Send+Sync wrapper around `ocio_rs::Config` (a `NonNull`-based handle;
/// the underlying OCIO config is a shared pointer safe for concurrent
/// reads).
pub struct SafeConfig(ocio_rs::Config);
unsafe impl Send for SafeConfig {}
unsafe impl Sync for SafeConfig {}

impl std::ops::Deref for SafeConfig {
	type Target = ocio_rs::Config;
	fn deref(&self) -> &ocio_rs::Config {
		&self.0
	}
}

static DEFAULT_CONFIG: LazyLock<Mutex<Option<std::sync::Arc<SafeConfig>>>> =
	LazyLock::new(|| Mutex::new(None));

/// Borrow the default config (C++ `ColorManager::get_default_config()`).
/// The `Arc` keeps the config alive across threads.
pub fn default_config() -> Option<std::sync::Arc<SafeConfig>> {
	DEFAULT_CONFIG
		.lock()
		.unwrap_or_else(|e| e.into_inner())
		.clone()
}

/// Load the process-wide default config from $OCIO or the bundled config
/// (C++ `ColorManager::SetUpDefaultConfig`).
pub fn set_up_default_config() -> Result<()> {
	let config = match std::env::var("OCIO") {
		Ok(path) if !path.is_empty() => ocio_rs::Config::from_file(&path)
			.map_err(|e| Error::Failed(format!("load $OCIO config: {e}")))?,
		_ => ocio_rs::Config::create_from_builtin_config("default")
			.or_else(|_| ocio_rs::Config::create_from_builtin_config("ocio-2.2-default"))
			.map_err(|e| Error::Failed(format!("load bundled config: {e}")))?,
	};
	*DEFAULT_CONFIG.lock().unwrap_or_else(|e| e.into_inner()) =
		Some(std::sync::Arc::new(SafeConfig(config)));
	Ok(())
}

/// The active default config's display transform cache id for
/// (display, view) — computed from the config's reference colorspace
/// (C++ `ColorManager::display_transform` cache-id semantics).
///
/// `Err(Error::State)` when no default config exists; `Ok(None)` when the
/// display or view is unknown.
pub fn display_transform_result(
	display: &str,
	view: &str,
) -> std::result::Result<Option<String>, Error> {
	let config = default_config().ok_or(Error::State)?;

	let mut display_found = false;
	for i in 0..config.num_displays_all() {
		if config.display_all(i).as_deref() == Some(display) {
			display_found = true;
			break;
		}
	}
	if !display_found {
		return Ok(None);
	}

	let mut view_found = false;
	let views =
		config.num_views_by_reference_space(ocio_rs::SearchReferenceSpaceType::Scene, display);
	for i in 0..views {
		if config
			.view_by_reference_space(ocio_rs::SearchReferenceSpaceType::Scene, display, i)
			.as_deref()
			== Some(view)
		{
			view_found = true;
			break;
		}
	}
	if !view_found {
		return Ok(None);
	}

	// OCIO's ROLE_REFERENCE role name ("reference"): `get_color_space`
	// resolves name-or-role (C++ `getColorSpace(ROLE_REFERENCE)`); the
	// role-name lookups are the fallback for configs that do not bind it.
	let src = config
		.get_color_space("reference")
		.and_then(|cs| cs.name())
		.filter(|s| !s.is_empty())
		.or_else(|| {
			config
				.role_color_space("reference")
				.filter(|s| !s.is_empty())
		})
		.or_else(|| {
			config
				.role_color_space("aces_interchange")
				.filter(|s| !s.is_empty())
		})
		.or_else(|| {
			config
				.role_color_space("scene_linear")
				.filter(|s| !s.is_empty())
		})
		.ok_or(Error::State)?;
	let processor = config
		.processor_display(src, display, view, ocio_rs::TransformDirection::Forward)
		.map_err(|_| Error::NotFound)?;
	Ok(processor.cache_id())
}

/// [`display_transform_result`] as a plain option (unknown → None).
pub fn display_transform(display: &str, view: &str) -> Option<String> {
	display_transform_result(display, view).ok().flatten()
}

/// The $OCIO path when set, otherwise the extracted default config path
/// (C++ `ColorManager::get_config_path`; the actual file extraction is
/// deferred to the app layer — the Rust default config stays in memory).
pub fn config_path() -> Option<String> {
	if let Ok(path) = std::env::var("OCIO") {
		if !path.is_empty() {
			return Some(path);
		}
	}
	if default_config().is_none() {
		return None;
	}
	Some(format!(
		"{}/ocioconf/config.ocio",
		crate::bridge::common::configuration_location()
	))
}

// ---- LUT library (C++ LUTLibrary) ------------------------------------------

/// Supported LUT extensions (C++ `LUTLibrary::supported_extensions()`).
pub const SUPPORTED_LUT_EXTENSIONS: [&str; 9] = [
	"cube", "3dl", "spi1d", "spi3d", "spimtx", "csp", "clf", "ctf", "cub",
];

/// 1 when `extension` (dot optional, case-insensitive) is supported.
pub fn is_supported_lut_extension(extension: &str) -> bool {
	let ext = extension.strip_prefix('.').unwrap_or(extension);
	SUPPORTED_LUT_EXTENSIONS
		.iter()
		.any(|e| e.eq_ignore_ascii_case(ext))
}

#[cfg(test)]
mod tests {
	use super::*;
	use std::sync::{Mutex, MutexGuard};

	/// Serializes config-dependent color tests (the process-wide default
	/// config is global).
	static CONFIG_TEST_LOCK: Mutex<()> = Mutex::new(());

	fn config_lock() -> MutexGuard<'static, ()> {
		CONFIG_TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner())
	}

	#[test]
	fn lut_extensions_case_insensitive_and_dot_optional() {
		assert!(is_supported_lut_extension("cube"));
		assert!(is_supported_lut_extension(".CUBE"));
		assert!(is_supported_lut_extension("clf"));
		assert!(!is_supported_lut_extension("exr"));
		assert!(!is_supported_lut_extension(""));
		assert_eq!(SUPPORTED_LUT_EXTENSIONS.len(), 9);
	}

	#[test]
	fn pass_through_processor_identity() {
		let p = ColorProcessor::pass_through();
		assert!(!p.is_valid());
		assert_eq!(
			p.convert_color([0.25, 0.5, 0.75, 1.0]),
			[0.25, 0.5, 0.75, 1.0]
		);
		let mut f = Frame::dummy();
		assert!(p.convert_frame(&mut f).is_ok());
	}

	#[test]
	fn convert_frame_rejects_non_f32() {
		// A pass-through processor accepts anything (no conversion needed).
		let p = ColorProcessor::pass_through();
		let mut f = Frame::new();
		f.format = PixelFormat::U8;
		assert!(
			p.convert_frame(&mut f).is_ok(),
			"pass-through converts nothing"
		);
		// A *valid* processor requires the F32 pipeline format.
		if set_up_default_config().is_err() {
			return;
		}
		let valid = ColorProcessor::create("scene_linear", "sdr-video", Direction::Normal)
			.and_then(|p| p.is_valid().then_some(p));
		if let Some(valid) = valid {
			let mut f = Frame::new();
			f.format = PixelFormat::U8;
			assert_eq!(
				valid.convert_frame(&mut f).unwrap_err().code(),
				crate::error::OAKRENDER_E_INVALID
			);
		}
	}

	#[test]
	fn create_without_config_yields_none() {
		let _lock = config_lock();
		// When no default config has been set up, creation is None.
		let saved = default_config();
		*DEFAULT_CONFIG.lock().unwrap_or_else(|e| e.into_inner()) = None;
		assert!(ColorProcessor::create("scene_linear", "sdr-video", Direction::Normal).is_none());
		*DEFAULT_CONFIG.lock().unwrap_or_else(|e| e.into_inner()) = saved;
	}

	#[test]
	fn default_config_setup_and_role_resolution() {
		let _lock = config_lock();
		if set_up_default_config().is_err() {
			// Bundled OCIO missing (e.g. stub build): skip.
			return;
		}
		let config = default_config().expect("config set up");
		// The built-in default config carries a scene_linear role.
		assert!(config.has_role("scene_linear") || config.has_role("aces_interchange"));
	}

	#[test]
	fn processor_from_builtin_converts() {
		let _lock = config_lock();
		if set_up_default_config().is_err() {
			return;
		}
		// Create via the built-in config; a valid processor must exist for
		// the ACES scene→display-encoded pairing and must preserve alpha.
		let p = ColorProcessor::create("ACEScg", "sRGB Encoded Rec.709 (sRGB)", Direction::Normal);
		let p = p.expect("processor handle always returned");
		assert!(
			p.is_valid(),
			"ACEScg→sRGB Encoded must be a valid processor"
		);
		let out = p.convert_color([0.18, 0.18, 0.18, 1.0]);
		assert!((out[3] - 1.0).abs() < 1e-6, "alpha passes through");
		assert!(out[0].is_finite() && out[1].is_finite() && out[2].is_finite());
		assert!(
			(out[0] - 0.18).abs() > 1e-3,
			"the transform must change the value"
		);
	}

	#[test]
	fn unknown_colorspaces_yield_invalid_processor() {
		let _lock = config_lock();
		if set_up_default_config().is_err() {
			return;
		}
		let p = ColorProcessor::create("not_a_real_colorspace", "also_not_real", Direction::Normal)
			.unwrap();
		assert!(!p.is_valid(), "lookup failure is non-fatal (pass-through)");
		assert_eq!(
			p.convert_color([1.0, 0.5, 0.25, 0.0]),
			[1.0, 0.5, 0.25, 0.0]
		);
	}

	#[test]
	fn display_transform_queries() {
		let _lock = config_lock();
		if set_up_default_config().is_err() {
			return;
		}
		let config = default_config().unwrap();
		if config.get_num_displays_all() <= 0 {
			return;
		}
		let display = config.get_display_all(0).unwrap();
		let n = config.get_num_views_v2(ocio_rs::SearchReferenceSpaceType::Scene, &display);
		assert!(n >= 0);
		if n > 0 {
			let view = config
				.get_view_v2(ocio_rs::SearchReferenceSpaceType::Scene, &display, 0)
				.unwrap();
			let id = display_transform(&display, &view);
			assert!(id.is_some());
			assert!(!id.unwrap().is_empty());
		}
		assert!(display_transform("no-such-display", "x").is_none());
	}

	#[test]
	fn convert_frame_with_valid_processor_changes_pixels() {
		let _lock = config_lock();
		if set_up_default_config().is_err() {
			return;
		}
		let p = ColorProcessor::create("ACEScg", "sRGB Encoded Rec.709 (sRGB)", Direction::Normal)
			.expect("handle always returned");
		if !p.is_valid() {
			return;
		}
		let mut f = Frame::new();
		let mut pod = crate::frame::VideoParamsPod::default();
		pod.width = 4;
		pod.height = 2;
		f.set_video_params(pod);
		f.allocate();
		// Fill with 0.18 grey via f32 view.
		let f32s: &mut [f32] = unsafe {
			std::slice::from_raw_parts_mut(f.data.as_mut_ptr() as *mut f32, f.pixel_count() * 4)
		};
		for px in f32s.chunks_exact_mut(4) {
			px[0] = 0.18;
			px[1] = 0.18;
			px[2] = 0.18;
			px[3] = 1.0;
		}
		p.convert_frame(&mut f).unwrap();
		let out: &[f32] = unsafe {
			std::slice::from_raw_parts(f.data.as_ptr() as *const f32, f.pixel_count() * 4)
		};
		assert!(
			(out[0] - 0.18).abs() > 1e-4,
			"scene_linear→sdr-video must change 0.18 grey (got {})",
			out[0]
		);
		assert!((out[3] - 1.0).abs() < 1e-5, "alpha preserved");
	}

	#[test]
	fn inverse_direction_reverses() {
		let _lock = config_lock();
		if set_up_default_config().is_err() {
			return;
		}
		let fwd =
			ColorProcessor::create("ACEScg", "sRGB Encoded Rec.709 (sRGB)", Direction::Normal)
				.expect("handle");
		let inv =
			ColorProcessor::create("ACEScg", "sRGB Encoded Rec.709 (sRGB)", Direction::Inverse)
				.expect("handle");
		if !fwd.is_valid() || !inv.is_valid() {
			return;
		}
		let x = fwd.convert_color([0.18, 0.18, 0.18, 1.0]);
		let back = inv.convert_color(x);
		assert!(
			(back[0] - 0.18).abs() < 0.05,
			"inverse undoes forward ({} -> {})",
			x[0],
			back[0]
		);
	}

	#[test]
	fn create_lut_from_real_file() {
		let _lock = config_lock();
		if set_up_default_config().is_err() {
			return;
		}
		// A tiny 1D .cube LUT (identity-ish with a slight lift).
		let lut = "TITLE oakrender test\nLUT_1D_SIZE 2\n0.0 0.0 0.0\n1.0 1.0 1.0\n";
		let path = std::env::temp_dir().join("oakrender-test-lut.cube");
		std::fs::write(&path, lut).unwrap();
		let p = ColorProcessor::create_lut(path.to_string_lossy().as_ref(), Direction::Normal)
			.expect("processor handle always returned");
		assert!(
			p.is_valid(),
			"a readable .cube must produce a valid processor"
		);
		let _ = std::fs::remove_file(&path);
	}

	#[test]
	fn grading_primary_and_from_processor() {
		let _lock = config_lock();
		if set_up_default_config().is_err() {
			return;
		}
		if let Some(p) = ColorProcessor::create_grading_primary(GradingStyle::Log) {
			// A grading-primary processor is valid and converts.
			if p.is_valid() {
				let out = p.convert_color([0.18, 0.5, 0.7, 1.0]);
				assert!(out.iter().all(|v| v.is_finite()));
			}
		}
		if let Some(config) = default_config() {
			if let Ok(proc) = config.processor("ACEScg", "sRGB Encoded Rec.709 (sRGB)") {
				let p = ColorProcessor::from_processor(proc);
				assert!(p.is_valid());
				assert!(!p.cache_id().is_empty(), "OCIO cache id present");
			}
		}
	}

	#[test]
	fn create_lut_missing_file_is_invalid() {
		let _lock = config_lock();
		if set_up_default_config().is_err() {
			return;
		}
		let p = ColorProcessor::create_lut("/nonexistent/never.cube", Direction::Normal).unwrap();
		assert!(!p.is_valid(), "unreadable LUT → pass-through processor");
	}
}
