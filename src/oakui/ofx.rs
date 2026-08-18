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

//! OFX plugin startup wiring (stage 6b).
//!
//! The app is the only place that holds both the oakplugin host and the UI
//! services the OFX suites consult at runtime, so the wiring lives here:
//!
//! - [`init`] scans the standard plugin paths ([`oakplugin::host::Host`]
//!   default path set, `host.rs:440-449`), registers every discovered
//!   plugin into the node factory (the effect library and the add-effect
//!   menu consume those entries), installs the render executor and the
//!   plugin-node duplicator (both idempotent), and registers the
//!   progress-reporter factory plus the active-viewer provider.
//! - [`update_project_extent`] / [`update_viewer_time`] keep the
//!   oakplugin side's fallback project size and timeline time in sync with
//!   the current sequence (the engine calls them on open / seek / tick).
//! - [`set_progress_tx`] wires a progress-event channel the app drains in
//!   its tick loop to drive the progress dialog.
//!
//! Every failure degrades to a log: plugin support is an optional
//! capability, never a startup dependency.
//!
//! ## Rendering topology and progress
//!
//! Preview/export rendering runs through the process-isolated oak-worker
//! pool (M15 S2), so plugin rendering happens in the worker process where
//! this main-process reporter factory is not in effect. The wiring still
//! serves the in-process render paths (e.g. the test-only inline backend)
//! and future work; worker-side progress forwarding over IPC is a TODO.

use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex, OnceLock};

use oakplugin::progress::{ReporterFactory, UiProgressReporter};
use oakplugin::suites::timeline::{ActiveViewerProvider, ViewerTimeInfo};

/// One progress event a plugin reporter pushed to the app channel (drained
/// by the app tick, which drives a progress dialog).
#[derive(Debug, Clone, Default)]
pub struct PluginProgressEvent {
	/// The label the plugin passed to progressStart.
	pub label: String,
	/// The message the plugin passed to progressStart.
	pub message: String,
	/// Progress fraction in 0.0..=1.0.
	pub fraction: f64,
}

/// The app's progress-event channel (registered by [`set_progress_tx`]).
static PROGRESS_TX: OnceLock<Mutex<Option<std::sync::mpsc::Sender<PluginProgressEvent>>>> =
	OnceLock::new();

/// The sticky cancel flag read by every live reporter (`update` returns
/// false once set; the progress dialog's cancel button sets it).
static CANCEL: AtomicBool = AtomicBool::new(false);

/// The last active-viewer time snapshot (the timeline-suite provider reads
/// it; the engine refreshes it on seek / tick).
static VIEWER_TIME: OnceLock<Mutex<ViewerTimeInfo>> = OnceLock::new();

/// The last known project extent (normalised-coordinate default conversion;
/// the engine refreshes it whenever the sequence changes).
static PROJECT_EXTENT: OnceLock<Mutex<(f64, f64)>> = OnceLock::new();

fn viewer_slot() -> &'static Mutex<ViewerTimeInfo> {
	VIEWER_TIME.get_or_init(|| {
		Mutex::new(ViewerTimeInfo {
			time: 0.0,
			range_min: 0.0,
			range_max: 0.0,
		})
	})
}

fn extent_slot() -> &'static Mutex<(f64, f64)> {
	PROJECT_EXTENT.get_or_init(|| Mutex::new((1920.0, 1080.0)))
}

// ---------------------------------------------------------------------------
// App-driven state sync
// ---------------------------------------------------------------------------

/// Wires the app's progress-event channel into the OFX progress suite. The
/// app keeps the receiving half and drains it in its tick loop.
pub fn set_progress_tx(tx: std::sync::mpsc::Sender<PluginProgressEvent>) {
	*PROGRESS_TX
		.get_or_init(|| Mutex::new(None))
		.lock()
		.unwrap_or_else(|e| e.into_inner()) = Some(tx);
}

/// Clone of the registered sender, or `None` before
/// [`set_progress_tx`] (a reporter then silently continues).
fn progress_tx() -> Option<std::sync::mpsc::Sender<PluginProgressEvent>> {
	PROGRESS_TX
		.get_or_init(|| Mutex::new(None))
		.lock()
		.unwrap_or_else(|e| e.into_inner())
		.clone()
}

/// Updates the active-viewer time snapshot the timeline suite falls back
/// to when no render context is live (engine seek / tick path).
pub fn update_viewer_time(time: f64, range_min: f64, range_max: f64) {
	*viewer_slot().lock().unwrap_or_else(|e| e.into_inner()) = ViewerTimeInfo {
		time,
		range_min,
		range_max,
	};
}

/// Updates the project extent (width/height) the OFX normalised-coordinate
/// default conversion uses, and pushes it into oakplugin.
pub fn update_project_extent(width: f64, height: f64) {
	let (w, h) = (width.max(1.0), height.max(1.0));
	*extent_slot().lock().unwrap_or_else(|e| e.into_inner()) = (w, h);
	oakplugin::node_factory::set_project_extent(w, h);
}

/// Requests cancellation of the running plugin render (the progress
/// dialog's Cancel button). The next progressStart resets the flag.
pub fn cancel_plugin_render() {
	CANCEL.store(true, Ordering::Relaxed);
}

// ---------------------------------------------------------------------------
// Reporters / providers
// ---------------------------------------------------------------------------

/// A reporter that forwards (label, message, fraction) to the app channel
/// and honours the global cancel flag.
struct ChannelProgressReporter {
	tx: Option<std::sync::mpsc::Sender<PluginProgressEvent>>,
	label: String,
	message: String,
}

impl UiProgressReporter for ChannelProgressReporter {
	fn update(&mut self, progress: f64) -> bool {
		if let Some(tx) = &self.tx {
			let _ = tx.send(PluginProgressEvent {
				label: self.label.clone(),
				message: self.message.clone(),
				fraction: progress,
			});
		}
		!CANCEL.load(Ordering::Relaxed)
	}
}

fn reporter_factory() -> ReporterFactory {
	Arc::new(|label, message| {
		// A fresh render begins: reset the sticky cancel flag.
		CANCEL.store(false, Ordering::Relaxed);
		Box::new(ChannelProgressReporter {
			tx: progress_tx(),
			label: label.to_string(),
			message: message.to_string(),
		})
	})
}

fn viewer_provider() -> ActiveViewerProvider {
	Arc::new(|| {
		let info = *viewer_slot().lock().unwrap_or_else(|e| e.into_inner());
		Some(info)
	})
}

// ---------------------------------------------------------------------------
// Startup
// ---------------------------------------------------------------------------

/// Idempotent OFX startup wiring. Scans the standard plugin directories,
/// registers every discovered plugin into the node factory, installs the
/// render executor / duplicator, and registers the progress factory and
/// the active-viewer provider. Returns the number of plugin node types
/// registered (0 when no plugins were discovered or the scan failed).
pub fn init() -> usize {
	// 1. Scan the standard OFX plugin directories (host.rs:440-449 default
	//    path set: ~/.OFX/Plugins, ~/.local/share/OFX/Plugins, ...
	//    plus the OLIVE_OFX_PLUGIN_PATH / OLIVE_PLUGIN_PATH /
	//    OFX_PLUGIN_PATH environment variables). A scan failure only
	//    logs — plugins are optional.
	if let Err(e) = oakplugin::host::Host::global().cache.scan() {
		eprintln!("[ofx] plugin scan failed: {e}");
	}
	// 2. Register discovered plugins into the node factory (idempotent;
	//    also installs the render executor and the plugin-node duplicator).
	let registered = oakplugin::node_factory::register_plugin_nodes();
	// 3. Progress reporter factory -> the app progress channel.
	oakplugin::progress::set_reporter_factory(Some(reporter_factory()));
	// 4. Active-viewer time provider (timeline suite fallback).
	oakplugin::suites::timeline::set_active_viewer_provider(Some(viewer_provider()));
	// 5. Project extent (the engine refreshes it whenever the sequence
	//    changes; keep the oakplugin side in sync with the default).
	let (w, h) = *extent_slot().lock().unwrap_or_else(|e| e.into_inner());
	oakplugin::node_factory::set_project_extent(w, h);
	registered.len()
}

#[cfg(test)]
mod tests {
	use super::*;

	#[test]
	fn viewer_time_roundtrip() {
		update_viewer_time(42.5, 10.0, 200.0);
		let provider = viewer_provider();
		let info = provider().expect("provider always reports a snapshot");
		assert_eq!(info.time, 42.5);
		assert_eq!((info.range_min, info.range_max), (10.0, 200.0));
	}

	#[test]
	fn cancel_is_sticky_until_a_new_reporter() {
		CANCEL.store(false, Ordering::Relaxed);
		let factory = reporter_factory();
		let mut a = factory("a", "b");
		assert!(a.update(0.1));
		cancel_plugin_render();
		assert!(!a.update(0.5), "a cancelled render reports no");
		// A fresh progressStart resets the flag.
		let mut b = factory("a", "b");
		assert!(b.update(0.1));
	}

	#[test]
	fn project_extent_is_forwarded() {
		update_project_extent(1280.0, 720.0);
		let slot = extent_slot();
		assert_eq!(*slot.lock().unwrap(), (1280.0, 720.0));
	}
}
