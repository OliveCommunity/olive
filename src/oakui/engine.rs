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

//! The engine gateway: the narrow Rust API through which the app layer talks
//! to the engine.
//!
//! # Why a gateway trait
//!
//! The UI must never depend on *how* the engine is implemented. Today the
//! only implementation is the mock ([`super::mock::MockEngine`]) feeding demo
//! data; later a real backend will bind the `liboakengine` C ABI
//! (`src/facade/rust`, the frozen `oakengine_*` exports) behind the *same*
//! trait. Swapping backends then touches only the wiring in
//! [`crate::app`] — the panels, the widgets and the view state stay as they
//! are.
//!
//! The trait is intentionally narrow: open a project, inspect the current
//! sequence, and drive the transport (play / pause / step / seek). Timeline
//! edits arrive as widget request events and are applied by the host through
//! methods on the engine type itself (see the `MockEngine` docs for the
//! current mapping), so they do not need to be part of this seam yet.
//!
//! Everything here is plain Rust — no C ABI, no FFI. The C-ABI binding is a
//! later concern of the real backend only.

use std::path::PathBuf;
use std::sync::Arc;

use gpui::effect_stack::{EffectStackDataSource, EffectStackEvent};
use gpui::node_graph::{NodeGraphDataSource, NodeGraphEvent};
use gpui::timeline::{ClipId, Frame, FrameRate, TimelineDataSource, TimelineEvent, TrackKind};
use gpui::{App, Context, Entity, Pixels, RenderImage};
use gpui_widgets::audio_meter::AudioMeterDataSource;
use gpui_widgets::project_explorer::ProjectDataSource;
use gpui_widgets::viewer::PlaybackClock;

pub use super::scopes::ScopeData;

/// A monitor the transport can address.
///
/// Oak has two independent transports: the source monitor plays the clip
/// shown in the source viewer (素材查看器), the program monitor plays the
/// sequence shown in the program viewer (序列查看器).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum Monitor {
	/// The source (footage) monitor.
	Source,
	/// The program (sequence) monitor.
	Program,
}

/// A video format: resolution plus frame rate.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct VideoFormat {
	/// Width in pixels.
	pub width: u32,
	/// Height in pixels.
	pub height: u32,
	/// The frame rate (rational, e.g. 30000/1001 for NTSC 29.97).
	pub rate: FrameRate,
}

impl VideoFormat {
	/// The classic HD television format: 1920×1080 at 25 fps.
	pub fn hd_1080p25() -> Self {
		Self {
			width: 1920,
			height: 1080,
			rate: FrameRate::new(25, 1),
		}
	}
}

/// A project open in the engine.
#[derive(Debug, Clone, PartialEq)]
pub struct Project {
	/// The project's display name.
	pub name: String,
	/// The project file on disk (`.ove`).
	pub path: PathBuf,
}

/// A project-library row, as the project manager lists it (M13 D4). The
/// stats are derived from the row's head state by the backend (they are
/// never stored in the library).
#[derive(Debug, Clone, PartialEq)]
pub struct LibraryProject {
	/// The library row uuid (the open / rename / duplicate / delete /
	/// export selector).
	pub uuid: String,
	/// The row's display name.
	pub name: String,
	/// Row creation time (unix seconds, UTC).
	pub created_at: i64,
	/// Last-write time (unix seconds, UTC; the manager sort key).
	pub modified_at: i64,
	/// Longest sequence duration in milliseconds.
	pub duration_ms: i64,
	/// Total tracks across all sequences.
	pub track_count: i32,
	/// Total clip blocks.
	pub clip_count: i32,
	/// Total footage nodes.
	pub footage_count: i32,
}

/// The sequence currently open in the project.
#[derive(Debug, Clone, PartialEq)]
pub struct Sequence {
	/// The sequence's display name.
	pub name: String,
	/// The sequence's video format.
	pub format: VideoFormat,
	/// The sequence length in frames.
	pub length: Frame,
}

/// The engine gateway.
///
/// Implementations own the "engine" side of the app: project state, the
/// current sequence, and the transport. Query methods are pure reads;
/// mutating methods take a gpui [`Context`](gpui::Context) so the backend can
/// update its observable entities (clocks, models) and notify them.
pub trait EngineGateway: Sized {
	/// The currently open project, or `None` before any project is opened.
	fn project(&self) -> Option<&Project>;

	/// The current sequence of the open project, if any.
	fn current_sequence(&self) -> Option<&Sequence>;

	/// The display name of the source media shown in the source viewer, used
	/// in the viewer header and dock tab. Empty when the engine has no source
	/// media loaded.
	fn source_media_name(&self) -> String {
		String::new()
	}

	/// Open a project file. The backend loads it and becomes the source of
	/// truth for [`project`](EngineGateway::project) /
	/// [`current_sequence`](EngineGateway::current_sequence).
	fn open_project(&mut self, path: PathBuf, cx: &mut gpui::Context<Self>);

	/// Seek `monitor` to `frame` (clamped to the sequence).
	fn request_frame(&mut self, monitor: Monitor, frame: Frame, cx: &mut gpui::Context<Self>);

	/// Start playback on `monitor`.
	fn play(&mut self, monitor: Monitor, cx: &mut gpui::Context<Self>);

	/// Pause playback on `monitor`, leaving the playhead where it is.
	fn pause(&mut self, monitor: Monitor, cx: &mut gpui::Context<Self>);

	/// Step `monitor`'s playhead by `delta` frames (negative steps back).
	fn step(&mut self, monitor: Monitor, delta: i64, cx: &mut gpui::Context<Self>);

	/// Advance the playback clocks by one wall-clock tick. Called on a
	/// periodic timer while any monitor is playing.
	fn tick(&mut self, cx: &mut gpui::Context<Self>);
}

/// The transport clock type an engine drives its monitors with.
///
/// Each engine owns two clocks (source + program), one per
/// [`Monitor`], and exposes them to the viewer widgets through the
/// [`PlaybackClock`] trait.
pub trait EngineClock: PlaybackClock + 'static {}

impl<T: PlaybackClock + 'static> EngineClock for T {}

/// The full app-facing engine surface: the gateway plus every widget
/// data-source trait and the app-only operations (clocks, viewer frames,
/// edits, undo/redo, file operations).
///
/// The app shell (`crate::app::OakApp`) and every panel are generic over
/// `E: AppEngine`, so swapping the backend (mock vs real) is a one-line
/// choice at startup — see [`crate::app::run`].
pub trait AppEngine:
	EngineGateway
	+ TimelineDataSource
	+ EffectStackDataSource
	+ NodeGraphDataSource
	+ ProjectDataSource
	+ AudioMeterDataSource
{
	/// The concrete transport-clock type (see [`EngineClock`]).
	type Clock: EngineClock;

	/// Builds a fresh engine instance (no project open, or demo data for
	/// the mock).
	fn create(cx: &mut Context<Self>) -> Self;

	/// The source monitor's clock entity.
	fn source_clock(&self) -> &Entity<Self::Clock>;

	/// The program monitor's clock entity.
	fn program_clock(&self) -> &Entity<Self::Clock>;

	/// The current playhead frame of `monitor`'s clock.
	fn clock_frame(&self, monitor: Monitor, cx: &App) -> Frame;

	/// The CPU frame the viewers display for `monitor` (cached per playhead
	/// frame, so a paused viewer never regenerates its picture).
	fn cpu_frame(&self, monitor: Monitor, cx: &App) -> Arc<RenderImage>;

	/// The scope samples ([`ScopeData`]) of `monitor`'s current CPU frame.
	/// The analysis runs inside the frame render pass (cached per playhead
	/// frame alongside the image), so this read is an `Arc` clone and never
	/// re-walks the frame.
	fn scope_data(&self, monitor: Monitor, cx: &App) -> ScopeData;

	/// Adds a new empty track of the given kind (undoable where the backend
	/// supports it).
	fn add_track(&mut self, kind: TrackKind, cx: &mut Context<Self>);

	/// Removes the track at display `index` (the index into
	/// [`TimelineDataSource::track`]; undoable where the backend supports
	/// it).
	fn remove_track(&mut self, index: usize, cx: &mut Context<Self>);

	/// Sets the row height of every timeline track (timeline toolbar).
	fn set_track_height(&mut self, height: Pixels, cx: &mut Context<Self>);

	/// Selects a material-bin entry (project-explorer "open").
	fn select_item(&mut self, id: u64, cx: &mut Context<Self>);

	/// Applies an effect-stack edit request to the engine's model.
	fn apply_effect_event(&mut self, event: &EffectStackEvent, cx: &mut Context<Self>);

	/// Updates the timeline clip selection (drives the effect stack's
	/// target). The app shell forwards `TimelineEvent::SelectionChanged`
	/// with the view's selection set. Default: no-op (engines without a
	/// selection-driven stack keep their existing behavior).
	fn set_selected_clips(&mut self, _clips: Vec<ClipId>, _cx: &mut Context<Self>) {}

	/// The effect types the user can add to the selected clip's chain, as
	/// (type id, display name) pairs — the facade factory entries flagged
	/// `video_effect` and not hidden from the create menu. The inspector
	/// panel lists them in its "add effect" menu. Default: empty.
	fn addable_effects(&self) -> Vec<(String, String)> {
		Vec::new()
	}

	/// Inserts the effect `type_id` at `index` into the selected clip's
	/// chain (undoable). `index` is an insertion index into
	/// [`EffectStackDataSource::effects`] (0 = closest to the source);
	/// the panel passes the position carried by the `AddRequested` event.
	/// Returns a user-facing error message on failure. Default: unsupported.
	fn add_effect(
		&mut self,
		index: usize,
		type_id: &str,
		cx: &mut Context<Self>,
	) -> Result<(), String> {
		let _ = (index, type_id, cx);
		Err("add effect not supported".into())
	}

	/// Applies a node-editor edit request to the engine's model.
	fn apply_node_graph_event(&mut self, event: &NodeGraphEvent, cx: &mut Context<Self>);

	/// Applies a timeline widget edit request (trim / move / playhead) to the
	/// engine's model. Edits are applied through the backend's edit commands
	/// with undo packaging; the playhead change is a plain seek.
	fn apply_timeline_event(&mut self, event: &TimelineEvent, cx: &mut Context<Self>);

	/// Splits the clip with `clip` id at `time` (the razor action).
	fn split_clip(&mut self, clip: ClipId, time: Frame, cx: &mut Context<Self>);

	/// Splits every clip whose range spans the program playhead (the razor
	/// tool's menu action).
	fn split_at_playhead(&mut self, cx: &mut Context<Self>);

	// -------------------------------------------------------------------
	// Sequence markers & work area (M12 P4): the facade surfaces are
	// undoable, mirroring Olive (MarkerAdd/MarkerRemove/WorkareaSet*).
	// Defaults: no-op / none, so mock-less engines degrade gracefully.
	// -------------------------------------------------------------------

	/// The sequence work area (render/export in/out range) when enabled, in
	/// sequence frames. `None` when disabled or no sequence is open.
	fn workarea(&self) -> Option<(Frame, Frame)> {
		None
	}

	/// Adds a marker at the program playhead (undoable).
	fn add_marker_at_playhead(&mut self, cx: &mut Context<Self>) {
		let _ = cx;
	}

	/// Removes the marker at the program playhead, if any (undoable).
	fn remove_marker_at_playhead(&mut self, cx: &mut Context<Self>) {
		let _ = cx;
	}

	/// Applies a work-area range **live** (not undoable) — the ruler drag
	/// preview path (Olive's `set_range` during drag).
	fn set_workarea_preview(&mut self, start: Frame, end: Frame, cx: &mut Context<Self>) {
		let _ = (start, end, cx);
	}

	/// Commits a work-area range as ONE undoable entry. `old_start` /
	/// `old_end` are the range before the change (the ruler drag start).
	fn commit_workarea(
		&mut self,
		old_start: Frame,
		old_end: Frame,
		start: Frame,
		end: Frame,
		cx: &mut Context<Self>,
	) {
		let _ = (old_start, old_end, start, end, cx);
	}

	/// Clears (disables) the work area (undoable).
	fn clear_workarea(&mut self, cx: &mut Context<Self>) {
		let _ = cx;
	}

	/// Deletes the clip with `clip` id, rippling following content left when
	/// `ripple` is set.
	fn delete_clip(&mut self, clip: ClipId, ripple: bool, cx: &mut Context<Self>);

	/// Whether the undo stack has an entry to undo.
	fn can_undo(&self) -> bool;

	/// Whether the undo stack has an entry to redo.
	fn can_redo(&self) -> bool;

	/// Steps the undo stack back one entry.
	fn undo(&mut self, cx: &mut Context<Self>);

	/// Steps the undo stack forward one entry.
	fn redo(&mut self, cx: &mut Context<Self>);

	/// Starts a new blank project with a single default sequence.
	fn new_project(&mut self, cx: &mut Context<Self>);

	/// Opens a project file. The format is dispatched by extension: `.ove`
	/// through the OVE serializer, `.otio` / `.fcpxml` through the oaktask
	/// interchange loader.
	fn open_project_path(&mut self, path: PathBuf, cx: &mut Context<Self>) -> Result<(), String>;

	/// Exports the current project to the file `path` (the 导出工程文件…
	/// action's target). The format is dispatched by extension like
	/// [`open_project_path`] (AppEngine::open_project_path). Exporting is a
	/// pure file write — the write-through library already persists every
	/// edit, so there is no "save" anymore.
	fn export_project_path(&mut self, path: PathBuf, cx: &mut Context<Self>)
		-> Result<(), String>;

	/// Closes the current project, leaving the app with no sequence.
	fn close_project(&mut self, cx: &mut Context<Self>);

	// -------------------------------------------------------------------
	// Project library (M13 D4: the write-through database the manager
	// window browses). Default: unsupported (empty list / error strings).
	// -------------------------------------------------------------------

	/// Whether the open project is bound to the library write-through
	/// session (the status bar's write state).
	fn storage_bound(&self) -> bool {
		false
	}

	/// The last write-through / snapshot error of the open project, if any.
	fn storage_last_error(&self) -> Option<String> {
		None
	}

	/// Lists the project library, most recently modified first (the
	/// project manager's data source).
	fn library_projects(&self) -> Result<Vec<LibraryProject>, String> {
		Err("project library not supported".into())
	}

	/// Creates a blank project named `name` in the library and opens it.
	fn library_create_project(&mut self, name: &str, cx: &mut Context<Self>) -> Result<(), String> {
		let _ = (name, cx);
		Err("project library not supported".into())
	}

	/// Opens the library project `uuid` (closing the current project).
	fn library_open_project(&mut self, uuid: &str, cx: &mut Context<Self>) -> Result<(), String> {
		let _ = (uuid, cx);
		Err("project library not supported".into())
	}

	/// Deletes the library project `uuid` (the manager confirms first).
	fn library_delete_project(&mut self, uuid: &str) -> Result<(), String> {
		let _ = uuid;
		Err("project library not supported".into())
	}

	/// Renames the library project `uuid` (the manager's list name).
	fn library_rename_project(&mut self, uuid: &str, name: &str) -> Result<(), String> {
		let _ = (uuid, name);
		Err("project library not supported".into())
	}

	/// Duplicates the library project `uuid` (history included) under a
	/// fresh uuid.
	fn library_duplicate_project(&mut self, uuid: &str) -> Result<(), String> {
		let _ = uuid;
		Err("project library not supported".into())
	}

	/// Imports a `.ove` / `.otio` / `.fcpxml` project file into the library
	/// as a new row; returns the new row's uuid.
	fn library_import_project(&mut self, path: PathBuf) -> Result<String, String> {
		let _ = path;
		Err("project library not supported".into())
	}

	/// Exports the library project `uuid` to `path`; the format is
	/// dispatched by extension (`.ove` / `.otio` / `.fcpxml`).
	fn library_export_project(&mut self, uuid: &str, path: PathBuf) -> Result<(), String> {
		let _ = (uuid, path);
		Err("project library not supported".into())
	}
	/// The timeline waveform cache (M12 P4); `None` when the backend
	/// does not provide waveforms.
	fn waveform_cache(&self) -> Option<std::sync::Arc<crate::oakui::waveform::WaveformCache>> {
		None
	}

	/// Import a media file into the project (M12 P3). Default: no-op.
	fn import_footage(
		&mut self,
		_path: std::path::PathBuf,
		_cx: &mut Context<Self>,
	) -> Result<(), String> {
		Ok(())
	}

	/// Starts an export of the current sequence in `format` to `path` and
	/// returns a session the host polls for progress and can cancel.
	///
	/// The export runs on a background thread; the returned
	/// [`ExportSession`] carries the event channel and the cancel handle.
	fn start_export(&mut self, format: i32, path: PathBuf) -> Result<ExportSession, String>;

	/// The display name of the engine backend ("mock" / "real"), shown in
	/// the status bar.
	fn backend_name(&self) -> &'static str;
}

/// A single progress event from a running export task.
#[derive(Debug, Clone, PartialEq)]
pub enum ExportEvent {
	/// The task started.
	Started,
	/// Fraction done, in `0.0..=1.0`.
	Progress(f64),
	/// The task finished. `true` = succeeded; the string carries the failure
	/// message on error.
	Finished(bool, String),
}

/// A running export: the event channel the host drains plus the cancel
/// handle. Dropping the session does not abort the export thread; the
/// thread owns the task and frees it when it finishes.
pub struct ExportSession {
	/// The event receiver (the background thread's sender lives as long as
	/// the session's `cancel` side, so a dropped receiver just stops
	/// delivering).
	pub events: std::sync::mpsc::Receiver<ExportEvent>,
	/// Cancels the running export as soon as possible.
	pub cancel: Box<dyn Fn() + Send>,
}
