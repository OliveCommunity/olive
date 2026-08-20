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

//! Display color management: the display's ICC profile applied to viewer
//! frames at present time.
//!
//! The frame content is treated as display-referred sRGB/Rec.709 (the
//! decode/render pipeline performs no input transfer conversion today);
//! the chain maps it through the display ICC (system profile or a custom
//! file from Preferences) so wide-gamut displays render correctly.
//!
//! Double-correction discipline: when this module transforms pixels, the
//! OS must not transform them again. macOS: the app sets the CAMetalLayer
//! colorspace to the display profile at startup (see the `OAK_METAL_*`
//! wiring in app.rs) so ColorSync passes our output through. Windows:
//! the SDR desktop applies no per-app transform (and ACM honors the
//! swapchain's declared sRGB space, which is the default). Linux: no
//! compositor-level correction exists to conflict with.

use std::sync::{Arc, LazyLock, Mutex};

use oakcommon::configstore::ConfigStore;
use oakrender::color::ColorProcessor;

/// Config key: the display color management mode ("icc" / "off").
pub const CONFIG_KEY_COLOR_MODE: &str = "DisplayColorMode";
/// Config key: a custom ICC profile path (empty = the system display
/// profile).
pub const CONFIG_KEY_CUSTOM_ICC: &str = "DisplayColorCustomIcc";
/// Config key: the content colorspace the chain starts from (an OCIO
/// colorspace name of the active config).
pub const CONFIG_KEY_CONTENT_SPACE: &str = "DisplayColorContentSpace";

/// The default content space (OCIO 2.2 builtin config name for
/// gamma-encoded Rec.709/sRGB display-referred content).
const DEFAULT_CONTENT_SPACE: &str = "sRGB Encoded Rec.709 (sRGB)";

/// The cached processor pair (F32 RGBA and packed BGRA8 variants of the
/// same chain), keyed by (mode, icc path, content space).
struct State {
	key: (String, String, String),
	f32: Option<Arc<ColorProcessor>>,
	bgra: Option<Arc<ColorProcessor>>,
}

static STATE: LazyLock<Mutex<Option<State>>> = LazyLock::new(|| Mutex::new(None));

/// Bumped every time the effective key changes (mode / ICC path /
/// content space): the engine's frame caches compare against it and drop
/// images produced with a stale transform.
static GENERATION: std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(0);

/// The current transform generation (see [`GENERATION`]).
pub fn generation() -> u64 {
	GENERATION.load(std::sync::atomic::Ordering::Relaxed)
}

/// The active (mode, icc, content-space) key from the config.
fn current_key() -> (String, String, String) {
	let store = ConfigStore::instance();
	let mode = store
		.get(None, CONFIG_KEY_COLOR_MODE)
		.unwrap_or_else(|_| "icc".to_string());
	let custom = store
		.get(None, CONFIG_KEY_CUSTOM_ICC)
		.unwrap_or_default();
	let space = store
		.get(None, CONFIG_KEY_CONTENT_SPACE)
		.unwrap_or_else(|_| DEFAULT_CONTENT_SPACE.to_string());
	(mode, custom, space)
}

/// Drop the cached processors (call after a preferences change).
pub fn invalidate() {
	*STATE.lock().unwrap_or_else(|e| e.into_inner()) = None;
}

/// The cached state, (re)built when the config key changed.
fn current() -> Option<State> {
	let key = current_key();
	let mut guard = STATE.lock().unwrap_or_else(|e| e.into_inner());
	if let Some(state) = guard.as_ref() {
		if state.key == key {
			return clone_state(state);
		}
	}
	// The key changed: everything rendered with the old transform is
	// stale — bump the generation so frame caches drop their contents.
	// The FIRST build (no prior state) must not bump: no frame can be
	// staler than a transform that did not exist yet, and a spurious
	// bump would drop the frames being cached right now.
	if guard.is_some() {
		GENERATION.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
	}
	let (mode, icc_path, space) = &key;
	if mode != "icc" {
		let state = State {
			key,
			f32: None,
			bgra: None,
		};
		let out = clone_state(&state);
		*guard = Some(state);
		return out;
	}
	// The custom override wins; empty = the platform's display profile.
	let icc = if icc_path.is_empty() {
		oakcommon::displayicc::system_display_icc()
	} else {
		Some(icc_path.clone())
	};
	let (f32p, bgrap) = match icc {
		Some(path) => (
			ColorProcessor::create_display_icc(space, &path).map(Arc::new),
			ColorProcessor::create_display_icc_bgra8(space, &path).map(Arc::new),
		),
		None => (None, None),
	};
	let state = State {
		key,
		f32: f32p,
		bgra: bgrap,
	};
	let out = clone_state(&state);
	*guard = Some(state);
	out
}

fn clone_state(state: &State) -> Option<State> {
	Some(State {
		key: state.key.clone(),
		f32: state.f32.clone(),
		bgra: state.bgra.clone(),
	})
}

/// Whether display color management is active (a valid ICC processor
/// exists). When false the OS owns the output mapping.
pub fn is_active() -> bool {
	current().map(|s| s.f32.is_some() || s.bgra.is_some()).unwrap_or(false)
}

/// Apply the display transform to an F32 RGBA buffer in place (no-op
/// when inactive).
pub fn apply_f32_rgba(samples: &mut [f32], pixels: i64) {
	let Some(state) = current() else {
		return;
	};
	if let Some(processor) = &state.f32 {
		let _ = processor.convert_f32_rgba(samples, pixels);
	}
}

/// Apply the display transform to a packed BGRA8 buffer in place (no-op
/// when inactive).
pub fn apply_bgra8(data: &mut [u8], pixels: i64) {
	let Some(state) = current() else {
		return;
	};
	if let Some(processor) = &state.bgra {
		let _ = processor.convert_bgra8(data, pixels);
	}
}
