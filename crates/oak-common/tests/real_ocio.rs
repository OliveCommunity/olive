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

//! Real-library smoke tests for the OCIO/image-backed utilities.
//!
//! These exercise the real OpenColorIO library through `ocio-rs`/`ocio-sys`
//! (compiled with `OCIO_RS_ENABLE_REAL=1`, see `.cargo/config.toml`). Each
//! OCIO test loads the project's own config at
//! `engine/render/ocioconf/config.ocio` and sets the `OCIO` environment
//! variable first (the library reads it at config-load time). The image tests
//! round-trip a small float TIFF through a temp file via the `image` crate.

use oak_common::error::Error;
use oak_common::ocioutils::OcioConfig;
use oak_common::oiioutils::{read_image_f32, write_image_f32};

/// Path to the project's OCIO config, relative to this crate's manifest dir.
fn config_path() -> String {
	let manifest = env!("CARGO_MANIFEST_DIR");
	// crates/oakcommon -> repository root
	format!("{}/../../engine/render/ocioconf/config.ocio", manifest)
}

/// The `OCIO` env var is consumed by the library at config-load time; setting
/// it here keeps the test hermetic regardless of the host environment.
fn set_ocio_env() -> String {
	let path = config_path();
	std::env::set_var("OCIO", &path);
	path
}

#[test]
#[ignore = "requires real OpenColorIO (OCIO_RS_ENABLE_REAL=1) and engine/render/ocioconf/config.ocio (removed with the C++ engine tree); run with -- --ignored when both are available"]
fn ocio_load_and_list_colorspaces() {
	let path = set_ocio_env();
	let config = OcioConfig::from_file(&path).expect("config.ocio should load");
	let count = config.colorspace_count().expect("count should work");
	assert!(count > 0, "config should define at least one color space");

	let colorspaces = config.colorspaces().expect("listing should work");
	assert_eq!(colorspaces.len(), count as usize);
	assert!(colorspaces.contains(&"Linear".to_string()));
	assert!(colorspaces.contains(&"sRGB OETF".to_string()));

	eprintln!("color spaces ({}): {:?}", colorspaces.len(), colorspaces);
}

#[test]
#[ignore = "requires real OpenColorIO (OCIO_RS_ENABLE_REAL=1) and engine/render/ocioconf/config.ocio (removed with the C++ engine tree); run with -- --ignored when both are available"]
fn ocio_roles_and_canonical_names() {
	let path = set_ocio_env();
	let config = OcioConfig::from_file(&path).unwrap();

	let roles = config.roles().unwrap();
	assert!(!roles.is_empty(), "config should define roles");
	eprintln!("roles: {:?}", roles);

	assert!(
		config.has_role("scene_linear").unwrap(),
		"scene_linear role should exist"
	);
	assert!(config.has_role("default").unwrap());
	assert!(!config.has_role("no_such_role").unwrap());

	// scene_linear maps to the Linear color space.
	assert_eq!(config.canonical_name("scene_linear").unwrap(), "Linear");
	// Role/name equivalence: role name resolves to its canonical color space.
	assert_eq!(config.canonical_name("Linear").unwrap(), "Linear");
}

#[test]
#[ignore = "requires real OpenColorIO (OCIO_RS_ENABLE_REAL=1) and engine/render/ocioconf/config.ocio (removed with the C++ engine tree); run with -- --ignored when both are available"]
fn ocio_displays_and_views() {
	let path = set_ocio_env();
	let config = OcioConfig::from_file(&path).unwrap();

	let display = config.default_display().unwrap();
	assert_eq!(display, "sRGB");
	let view = config.default_view(&display).unwrap();
	assert_eq!(view, "sRGB OETF");
}

#[test]
#[ignore = "requires real OpenColorIO (OCIO_RS_ENABLE_REAL=1) and engine/render/ocioconf/config.ocio (removed with the C++ engine tree); run with -- --ignored when both are available"]
fn ocio_processor_apply_rgba() {
	let path = set_ocio_env();
	let config = OcioConfig::from_file(&path).unwrap();

	// Linear -> sRGB OETF: a mid-gray 0.18 (a common linear display-referred
	// midpoint) should map well above itself and stay finite.
	let processor = config.processor("Linear", "sRGB OETF").unwrap();
	let mut px = [0.18f32, 0.18f32, 0.18f32, 1.0f32];
	processor.apply_rgba(&mut px).unwrap();
	assert!(
		px[0] > 0.18f32,
		"sRGB OETF should lift 0.18 linear, got {}",
		px[0]
	);
	assert!(
		px[0] < 1.0f32 + 1e-6,
		"sRGB OETF output should be <= 1.0, got {}",
		px[0]
	);
	assert!(px.iter().all(|v| v.is_finite()));

	// Display-referred path: scene_linear -> default sRGB view.
	let processor = config
		.display_processor("scene_linear", "sRGB", "sRGB OETF")
		.unwrap();
	let mut px = [0.18f32, 0.18f32, 0.18f32, 1.0f32];
	processor.apply_rgba(&mut px).unwrap();
	assert!(px.iter().all(|v| v.is_finite()));
	assert!(
		px[0] > 0.18f32,
		"display processor should also lift 0.18, got {}",
		px[0]
	);
}

#[test]
#[ignore = "requires real OpenColorIO (OCIO_RS_ENABLE_REAL=1) and engine/render/ocioconf/config.ocio (removed with the C++ engine tree); run with -- --ignored when both are available"]
fn ocio_error_paths() {
	let path = set_ocio_env();

	// Nonexistent config file.
	let err = OcioConfig::from_file("/nonexistent/oakcommon-real-ocio.ocio").unwrap_err();
	eprintln!("nonexistent config error: {err:?}");
	assert!(matches!(err, Error::Failed(_)));

	let config = OcioConfig::from_file(&path).unwrap();

	// Unknown destination color space.
	let err = config
		.processor("Linear", "No Such Color Space")
		.unwrap_err();
	eprintln!("unknown colorspace error: {err:?}");
	assert!(matches!(err, Error::Failed(_)));

	// Unknown display/view.
	let err = config
		.display_processor("Linear", "No Such Display", "No View")
		.unwrap_err();
	eprintln!("unknown display error: {err:?}");
	assert!(matches!(err, Error::Failed(_)));
}

#[test]
fn image_f32_round_trip() {
	let dir = std::env::temp_dir().join("oakcommon-real-ocio");
	std::fs::create_dir_all(&dir).unwrap();
	let path = dir.join("roundtrip.tif");
	let path_str = path.to_str().unwrap().to_string();

	// 2x2 RGBA float image.
	let w = 2;
	let h = 2;
	let c = 4;
	let pixels: Vec<f32> = vec![
		0.0, 0.25, 0.5, 1.0, 0.75, 0.5, 0.25, 1.0, 1.0, 0.0, 0.5, 0.0, 0.125, 0.625, 0.875, 1.0,
	];

	write_image_f32(&path_str, w, h, c, &pixels).expect("write should succeed");

	let img = read_image_f32(&path_str).expect("read should succeed");
	assert_eq!(img.width, w);
	assert_eq!(img.height, h);
	assert_eq!(img.channels, c);
	assert_eq!(img.pixels.len(), (w * h * c) as usize);

	for (i, (a, b)) in img.pixels.iter().zip(pixels.iter()).enumerate() {
		let diff = (a - b).abs();
		assert!(diff < 1e-6, "pixel {i}: wrote {b}, read back {a}");
	}

	std::fs::remove_file(&path).ok();
}
