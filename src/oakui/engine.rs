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
//! The UI must never depend on *how* the engine is implemented. The mock
//! ([`super::mock::MockEngine`]) feeds demo data; the real backend
//! ([`super::real::RealEngine`]) drives the oak* module crates' Rust APIs
//! directly (M14 R3: no C ABI, no FFI) behind the *same* trait. Swapping
//! backends touches only the wiring in [`crate::app`] — the panels, the
//! widgets and the view state stay as they are.
//!
//! The trait is intentionally narrow: open a project, inspect the current
//! sequence, and drive the transport (play / pause / step / seek). Timeline
//! edits arrive as widget request events and are applied by the host through
//! methods on the engine type itself (see the `MockEngine` docs for the
//! current mapping), so they do not need to be part of this seam yet.

use std::path::PathBuf;
use std::sync::Arc;

use gpui::effect_stack::{EffectId, EffectStackDataSource, EffectStackEvent};
use gpui::node_graph::{NodeGraphDataSource, NodeGraphEvent};
use gpui::timeline::{
	ClipId, Frame, FrameRate, TimelineDataSource, TimelineEvent, TrackData, TrackKind,
};
use gpui::{App, Context, Entity, Pixels, Point, RenderImage};
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

/// One row of the undo history (the C++ `HistoryModel` row): a command
/// label plus whether the command is currently done (undone rows render
/// gray in the history panel).
#[derive(Debug, Clone, PartialEq)]
pub struct HistoryEntry {
	/// The command's user-visible label (may be empty; the panel falls
	/// back to a generic "Command" like the C++ widget).
	pub name: String,
	/// Whether the command is currently done (`false` = the redoable
	/// tail).
	pub done: bool,
}

/// One creatable node type the node editor's "Add" submenu lists — the
/// Rust counterpart of a C++ `NodeFactory` menu entry. `category_key` is
/// the i18n key of the category submenu the entry belongs to (the first
/// of the factory entry's categories; entries whose only category never
/// appears in the create menu are skipped).
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct NodeLibraryEntry {
	/// The factory type id handed to [`AppEngine::add_node_at`].
	pub type_id: String,
	/// The node's display name.
	pub name: String,
	/// The i18n key of the category submenu (`node.category.*`).
	pub category_key: &'static str,
}

/// One addable effect entry (the effect library list and the inspector's
/// add-effect menu).
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct EffectEntry {
	/// The factory type id handed to [`AppEngine::add_effect`].
	pub type_id: String,
	/// The effect's display name.
	pub name: String,
	/// The effect-library group: `Some(sub_category)` for OpenFX plugin
	/// entries (Filter / Generator / Transition / General — the C++
	/// `factorymenu` OpenFX branch), `None` for built-in effects (rendered
	/// without a group header).
	pub group: Option<String>,
}

/// A snapshot of one effect parameter (a node input) for the inspector's
/// parameter view. For OFX plugin nodes each entry maps 1:1 to an OFX
/// parameter (input id = param name, display name = param label).
#[derive(Debug, Clone)]
pub struct EffectParam {
	/// The input id (the OFX param name for plugin nodes).
	pub input_id: String,
	/// The display name (the OFX param label).
	pub display_name: String,
	/// The value type.
	pub value_type: oaknode::value::ValueType,
	/// The current value.
	pub value: oaknode::value::NodeValue,
	/// The input flag bits (`oaknode::input::flags::*`).
	pub flags: u32,
	/// The input properties (`combo_option` / `combo_value` / `ui_group` /
	/// `ui_page` / `min` / `max` / ...).
	pub properties: Vec<(String, oaknode::value::NodeValue)>,
}

/// The i18n key of a node category submenu, or `None` for categories that
/// never appear in the node editor's Add menu (timeline-structural nodes).
pub fn node_category_key(category: oaknode::node::Category) -> Option<&'static str> {
	use oaknode::node::Category as C;
	Some(match category {
		C::Output => "node.category.output",
		C::Effect => "node.category.effect",
		C::Generator => "node.category.generator",
		C::Input => "node.category.input",
		C::Math => "node.category.math",
		C::Color => "node.category.color",
		C::Distort => "node.category.distort",
		C::Filter => "node.category.filter",
		C::Keying => "node.category.keying",
		C::OpenFx => "node.category.openfx",
		C::Group => "node.category.group",
		// Tracks/blocks are timeline-structural; the user never creates
		// them from the node editor.
		C::Timeline => return None,
	})
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

	/// The node-graph selection mirror: the single node currently selected
	/// in the node editor (or the effect card clicked in the inspector),
	/// when exactly one is selected. The node-editor panel uses this to
	/// push the highlight into the graph widget; the inspector derives its
	/// stack target and card highlight from it. Default: none.
	fn selected_graph_node(&self) -> Option<u64> {
		None
	}

	/// The effect types the user can add to the selected clip's chain — the
	/// factory entries flagged `video_effect` and not hidden from the create
	/// menu, plus every runtime-registered OpenFX plugin entry (grouped by
	/// its sub-category). The inspector panel lists them in its "add
	/// effect" menu. Default: empty.
	fn addable_effects(&self) -> Vec<EffectEntry> {
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

	/// The parameter controls of `effect` for the inspector, or `None`
	/// when the effect exposes no parameter UI (not a plugin node, or no
	/// editable parameters). Default: `None` (the inspector renders its
	/// placeholder).
	fn effect_params(&self, _effect: EffectId) -> Option<Vec<EffectParam>> {
		None
	}

	/// Sets an effect parameter (a node input) undoably. Returns a
	/// user-facing error on failure. Default: unsupported.
	fn set_effect_param(
		&mut self,
		_effect: EffectId,
		_input_id: &str,
		_value: oaknode::value::NodeValue,
		_cx: &mut Context<Self>,
	) -> Result<(), String> {
		Err("effect params not supported".into())
	}

	/// Triggers a push-button parameter of `effect` (the OFX push-button
	/// press). Returns a user-facing error on failure. Default:
	/// unsupported.
	fn effect_push_button(
		&mut self,
		_effect: EffectId,
		_input_id: &str,
		_cx: &mut Context<Self>,
	) -> Result<(), String> {
		Err("effect push button not supported".into())
	}

	/// The plugin instance handle (the oakplugin registry key) of the OFX
	/// effect the program viewer's interact should target — the inspector's
	/// current selection, i.e. the first expanded OFX plugin card in the
	/// selected clip's chain. `None` when there is no candidate (no
	/// project, no selected clip, no expanded plugin card). The program
	/// viewer feeds this to `oakui::ofx::sync_active_interact`; the default
	/// keeps engines without a plugin selection inert.
	fn ofx_interact_target(&self, _cx: &App) -> Option<u64> {
		None
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
	// Right-click menu support: the node editor's Add menu, the track-head
	// "delete all empty tracks" action, and the project explorer's footage
	// operations. Defaults degrade to "unsupported" / "unknown".
	// -------------------------------------------------------------------

	/// The creatable node types the node editor's Add submenu lists (the
	/// C++ `NodeFactory` menu listing): the global factory's entries minus
	/// the ones flagged `dont_show_in_create_menu`, each tagged with its
	/// first category's i18n key. Runtime-registered (plugin) entries are
	/// included.
	fn node_library(&self) -> Vec<NodeLibraryEntry> {
		let mut out = Vec::new();
		let factory = oaknode::factory::Factory::global();
		for meta in factory.entries() {
			// A scratch instance per entry just to read its flags (the
			// factory metadata carries no flag copy).
			let (core, _behavior) = (meta.create)();
			if core.flags & oaknode::node::flags::DONT_SHOW_IN_CREATE_MENU != 0 {
				continue;
			}
			let Some(category_key) = meta.categories.first().copied().and_then(node_category_key)
			else {
				continue;
			};
			let name = if meta.name.is_empty() {
				meta.type_id.to_string()
			} else {
				meta.name.to_string()
			};
			out.push(NodeLibraryEntry {
				type_id: meta.type_id.to_string(),
				name,
				category_key,
			});
		}
		for meta in factory.dynamic_entries() {
			let Some(category_key) = meta.categories.first().copied().and_then(node_category_key)
			else {
				continue;
			};
			out.push(NodeLibraryEntry {
				type_id: meta.type_id,
				name: meta.name,
				category_key,
			});
		}
		out
	}

	/// Creates a node of type `type_id` at `position` (graph space) in the
	/// current sequence's node graph — the node editor's Add menu action.
	/// Default: unsupported.
	fn add_node_at(
		&mut self,
		type_id: &str,
		position: Point<Pixels>,
		cx: &mut Context<Self>,
	) -> Result<(), String> {
		let _ = (type_id, position, cx);
		Err("add node not supported".into())
	}

	/// Removes every clip-less track through
	/// [`remove_track`](Self::remove_track), so each removal keeps the
	/// backend's undo packaging (the C++ asks for confirmation first; this
	/// port does not — a known deviation).
	fn delete_empty_tracks(&mut self, cx: &mut Context<Self>) {
		let budget = self.track_count();
		for _ in 0..budget {
			let Some(index) = (0..self.track_count())
				.find(|index| self.track(*index).is_some_and(|track| track.clips().is_empty()))
			else {
				break;
			};
			self.remove_track(index, cx);
		}
	}

	/// The on-disk path of the footage behind project-explorer entry `id`
	/// (enables "Reveal in Finder" / drives "Replace Footage"), if the
	/// backend knows it. Default: unknown.
	fn entry_path(&self, id: u64) -> Option<PathBuf> {
		let _ = id;
		None
	}

	/// Replaces the footage of project-explorer entry `id` with the media
	/// file at `path` (the C++ `ReplaceFootage` flow). Default:
	/// unsupported.
	fn replace_footage(
		&mut self,
		id: u64,
		path: PathBuf,
		cx: &mut Context<Self>,
	) -> Result<(), String> {
		let _ = (id, path, cx);
		Err("replace footage not supported".into())
	}

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

	/// Copy the selected clips to the engine clipboard (C++ Copy).
	fn clipboard_copy(&mut self, _clips: Vec<ClipId>, _cx: &mut Context<Self>) {}
	/// Copy then gap-delete the selected clips (C++ Cut).
	fn clipboard_cut(&mut self, _clips: Vec<ClipId>, _cx: &mut Context<Self>) {}
	/// Paste the clipboard at the playhead (C++ Paste), one undoable entry.
	fn clipboard_paste(&mut self, _cx: &mut Context<Self>) {}

	/// Whether the undo stack has an entry to undo.
	fn can_undo(&self) -> bool;

	/// Whether the undo stack has an entry to redo.
	fn can_redo(&self) -> bool;

	/// Steps the undo stack back one entry.
	fn undo(&mut self, cx: &mut Context<Self>);

	/// Steps the undo stack forward one entry.
	fn redo(&mut self, cx: &mut Context<Self>);

	/// The undo-stack rows the history panel lists (every command, done
	/// first then the redoable tail; the C++ `HistoryModel` order).
	/// Default: no history (the mock keeps no undo stack).
	fn history_entries(&self) -> Vec<HistoryEntry> {
		Vec::new()
	}

	/// The current stack position (done-command count); the history panel
	/// selects row `index - 1` and grays rows at/after `index`.
	/// Default: 0.
	fn history_index(&self) -> i64 {
		0
	}

	/// Undo/redo until the done-command count equals `index` (a history
	/// panel row click jumps to `row + 1`, the C++ `HistoryWidget`
	/// behavior). Default: no-op.
	fn jump_history(&mut self, index: i64, cx: &mut Context<Self>) {
		let _ = (index, cx);
	}

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

	/// Places the footage with project-explorer entry `id` on the timeline:
	/// a clip on the track at display `track_index` starting at `time`
	/// (undoable where the backend supports it — the facade
	/// `oakengine_sequence_add_footage_clip_ex` pushes one "Add Clip" undo
	/// entry).
	///
	/// The timeline panel resolves the cursor to a display track + frame and
	/// forwards them here; the backend resolves `id` to its footage, picks
	/// the target track and places the clip. Track policy: the pointed track
	/// is used when its kind matches the footage's media type; a mismatch
	/// (audio footage onto a video track, or vice versa) auto-selects the
	/// topmost track of the footage's kind, and the drop is rejected when no
	/// such track exists (the facade validates only the track type — video
	/// or audio, subtitles are rejected — never the media/track pairing).
	/// Default: no-op.
	fn drop_footage(
		&mut self,
		_id: u64,
		_track_kind: TrackKind,
		_track_index: usize,
		_time: Frame,
		_cx: &mut Context<Self>,
	) {
	}

	/// The footage's length in sequence frames (for the timeline's drop
	/// ghost): the probed duration times the frame rate. `None` when the
	/// entry is not footage or has no probed duration.
	fn footage_length_frames(&self, _id: u64) -> Option<i64> {
		None
	}

	/// Starts an export of the current sequence in `format` to `path` and
	/// returns a session the host polls for progress and can cancel.
	///
	/// The export runs on a background thread; the returned
	/// [`ExportSession`] carries the event channel and the cancel handle.
	fn start_export(&mut self, format: i32, path: PathBuf) -> Result<ExportSession, String>;

	// -------------------------------------------------------------------
	// Proxy media (the C++ Tools > proxy pipeline): global switch, per
	// footage state and the generate / delete / reveal entries. Defaults
	// degrade to "no proxy support" so engines without a footage surface
	// keep compiling.
	// -------------------------------------------------------------------

	/// The global "Use Proxy Media" switch (the C++ `UseProxyMedia`
	/// config; preview-only — exports always decode the original media).
	fn use_proxy_media(&self) -> bool {
		oakcommon::configstore::ConfigStore::instance()
			.get_bool(None, "UseProxyMedia", 1)
			!= 0
	}

	/// Toggles the global "Use Proxy Media" switch and invalidates every
	/// footage's rendered frames (the C++ toggles the config and
	/// re-renders; the preview path reads the switch on every montage).
	fn set_use_proxy_media(&mut self, enabled: bool, cx: &mut Context<Self>) {
		oakcommon::configstore::ConfigStore::instance().set(
			None,
			"UseProxyMedia",
			if enabled { "true" } else { "false" },
		);
		let _ = cx;
	}

	/// The playback resolution divider (the C++ viewer `Playback
	/// Resolution ▸` menu / `PlaybackDivider` config): 1 = full preview
	/// size, 2/4/8 = progressively smaller preview renders for machines
	/// that cannot keep up. Preview-only; exports always render native.
	fn playback_divider(&self) -> i64 {
		oakcommon::configstore::ConfigStore::instance()
			.get_int(None, "PlaybackDivider", 1)
			.clamp(1, 8) as i64
	}

	/// Sets the playback resolution divider and invalidates the rendered
	/// frames so the next pull re-renders at the new geometry.
	fn set_playback_divider(&mut self, divider: i64, cx: &mut Context<Self>) {
		oakcommon::configstore::ConfigStore::instance().set(
			None,
			"PlaybackDivider",
			&divider.clamp(1, 8).to_string(),
		);
		let _ = cx;
	}

	/// The footage rows the proxy dialog's footage mode lists (every
	/// footage node in the open project).
	fn proxy_rows(&self) -> Vec<ProxyFootageRow> {
		Vec::new()
	}

	/// The proxy state of footage `id` (a project-explorer entry id /
	/// node identity), or `None` when the entry is not footage.
	fn proxy_state(&self, id: u64) -> Option<ProxyMediaState> {
		let _ = id;
		None
	}

	/// The full proxy row of footage `id` (the project explorer's
	/// per-entry proxy submenu state), or `None` when the entry is not
	/// footage.
	fn proxy_row(&self, id: u64) -> Option<ProxyFootageRow> {
		let _ = id;
		None
	}

	/// Starts generating the proxy of footage `id` (a background ffmpeg
	/// transcode through `oaktask::ProxyTask`). Progress is reported
	/// through [`proxy_task_progress`](Self::proxy_task_progress) and
	/// drained on the engine tick; completion invalidates the footage's
	/// rendered frames.
	fn proxy_generate(&mut self, id: u64, cx: &mut Context<Self>) -> Result<(), String> {
		let _ = (id, cx);
		Err("proxy generation not supported".into())
	}

	/// The in-flight proxy task's label and progress (`0.0..=1.0`), when
	/// one is running (the status bar's proxy segment).
	fn proxy_task_progress(&self) -> Option<(String, f64)> {
		None
	}

	/// Deletes footage `id`'s proxy file and clears its proxy fields.
	fn proxy_delete(&mut self, id: u64, cx: &mut Context<Self>) {
		let _ = (id, cx);
	}

	/// Toggles footage `id`'s per-footage proxy-use flag (the C++
	/// `Footage::set_proxy_enabled`).
	fn proxy_set_enabled(&mut self, id: u64, enabled: bool, cx: &mut Context<Self>) {
		let _ = (id, enabled, cx);
	}

	/// Reveals footage `id`'s proxy file in the file manager
	/// (macOS `open -R`); no-op when there is no proxy yet.
	fn proxy_reveal(&self, id: u64) {
		let _ = id;
	}

	/// Sets footage `id`'s custom proxy generation params (the proxy
	/// dialog's per-footage "custom" checkbox path).
	fn proxy_set_custom_params(
		&mut self,
		id: u64,
		params: ProxyParamsUi,
		cx: &mut Context<Self>,
	) {
		let _ = (id, params, cx);
	}

	/// Clears footage `id`'s custom proxy generation params (the footage
	/// falls back to the global settings).
	fn proxy_clear_custom_params(&mut self, id: u64, cx: &mut Context<Self>) {
		let _ = (id, cx);
	}

	/// The custom proxy params of footage `id` (`None` = global params).
	fn proxy_custom_params(&self, id: u64) -> Option<ProxyParamsUi> {
		let _ = id;
		None
	}

	/// The proxy generation params that would apply to footage `id`
	/// (custom when set, otherwise the global config params).
	fn proxy_effective_params(&self, id: u64) -> ProxyParamsUi {
		let _ = id;
		proxy_params_from_config()
	}

	/// The distinct footage entries feeding `clips`, as proxy rows (the
	/// timeline clip menu's proxy group targets; duplicates collapse).
	fn clip_footage_entries(&self, clips: &[ClipId]) -> Vec<ProxyFootageRow> {
		let _ = clips;
		Vec::new()
	}

	// -------------------------------------------------------------------
	// Audio/video synchronization (the C++ timeline Synchronize menu):
	// eligibility counts for the context menu plus the two apply paths.
	// -------------------------------------------------------------------

	/// How many of `clips` can sync by source timecode / by waveform
	/// (the context menu enables each entry at ≥ 2).
	fn sync_eligibility(&self, clips: &[ClipId]) -> SyncEligibility {
		let _ = clips;
		SyncEligibility::default()
	}

	/// Synchronizes `clips` by their footage's source start timecode
	/// (one multi-undo; the C++ `Synchronize Clips by Source Time`).
	fn sync_clips_by_source_time(&mut self, clips: Vec<ClipId>, cx: &mut Context<Self>) {
		let _ = (clips, cx);
	}

	/// Synchronizes `clips` by waveform correlation, optionally adjusting
	/// speed (one multi-undo; the C++ `Synchronize Clips by Waveform`).
	fn sync_clips_by_waveform(
		&mut self,
		clips: Vec<ClipId>,
		adjust_speed: bool,
		cx: &mut Context<Self>,
	) {
		let _ = (clips, adjust_speed, cx);
	}

	// -------------------------------------------------------------------
	// Multi-camera (the C++ MulticamWidget / timeline Multi-Cam menu):
	// detection state for the panel, angle-frame rendering, the timeline
	// menu's enable/disable and the source switch. Defaults degrade to "no
	// multicam", so engines without a multicam surface keep compiling.
	// -------------------------------------------------------------------

	/// The currently detected multicam state (the panel's grid), or `None`
	/// when there is nothing to display. The backend performs the
	/// detection on demand (selected clip → `find_multicam`, falling back
	/// to the clip at the program playhead), so the panel always reads a
	/// fresh answer.
	fn multicam_state(&self) -> Option<MulticamState> {
		None
	}

	/// The rendered frame of one multicam angle, when a frame for the
	/// current playhead is cached. The panel calls this for every source it
	/// draws; `None` means the frame is not ready (the engine schedules a
	/// background render and notifies when it lands). The backend caches
	/// per (multicam node, source) with an LRU cap, so a paused panel never
	/// re-renders a cell.
	fn multicam_angle_frame(&mut self, source: i32, cx: &mut Context<Self>) -> Option<Arc<RenderImage>> {
		let _ = (source, cx);
		None
	}

	/// Whether any of `clips` can host multicam — the timeline clip menu's
	/// enable condition (the C++ `connected_viewer()` of the clip is a
	/// sequence).
	fn multicam_eligible(&self, clips: &[ClipId]) -> bool {
		let _ = clips;
		false
	}

	/// Whether the selected clips are currently multicam-enabled — the
	/// timeline menu's checked state.
	fn multicam_enabled_on_selection(&self, clips: &[ClipId]) -> bool {
		let _ = clips;
		false
	}

	/// Enables / disables multicam on `clips` (the timeline menu's checkable
	/// item), as ONE undo entry (`Multi-Cam Enabled On %1 Clip(s)` /
	/// `Multi-Cam Disabled On %1 Clip(s)`). Clips whose connected viewer is
	/// not a sequence are skipped.
	fn multicam_enable_selected(
		&mut self,
		clips: Vec<ClipId>,
		enabled: bool,
		cx: &mut Context<Self>,
	) {
		let _ = (clips, enabled, cx);
	}

	/// Switches the currently detected multicam to `source` (the digit
	/// keys and grid clicks), as ONE undo entry (`Switched Multi-Camera
	/// Source`). `split_clip` = the change applies from the playhead
	/// forward (the clip is split first).
	fn multicam_switch_to(&mut self, source: i32, split_clip: bool, cx: &mut Context<Self>) {
		let _ = (source, split_clip, cx);
	}

	/// The display name of the engine backend ("mock" / "real"), shown in
	/// the status bar.
	fn backend_name(&self) -> &'static str;
}

/// The detected multicam state the Multicam panel displays (the C++
/// `MulticamWidget`'s `node_` / `clip_` plus the resolved source count /
/// current source). `None` in the engine means there is no multicam to
/// show — the panel falls back to its empty state.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct MulticamState {
	/// The source sequence node identity (the multicam's `sequence_in` edge
	/// target; its track list supplies the angles).
	pub sequence_id: u64,
	/// The multicam node identity.
	pub node_id: u64,
	/// The timeline clip node identity whose texture input the multicam
	/// feeds.
	pub clip_id: u64,
	/// The number of angle sources (the source sequence's track count of
	/// the multicam's `sequence_type_in` kind).
	pub source_count: i32,
	/// The currently selected source index (`current_in`).
	pub current_source: i32,
}

/// The lifecycle state of one footage's proxy (the UI mirror of
/// `oakcodec::proxymanager::ProxyState`).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ProxyMediaState {
	/// No proxy on disk (the menu offers "Generate").
	Missing,
	/// A transcode is running (or a stale working file remains).
	Generating,
	/// The proxy file is on disk and usable.
	Ready,
	/// The last generation attempt failed.
	Failed,
}

/// The proxy generation parameters the proxy dialog edits (the app-side
/// mirror of `oakcodec::proxymanager::ProxyParams`, minus the fixed
/// version / extension).
#[derive(Debug, Clone, PartialEq)]
pub struct ProxyParamsUi {
	/// Absolute target width (ignored when `divider > 1`).
	pub width: i32,
	/// Absolute target height (ignored when `divider > 1`).
	pub height: i32,
	/// Source resolution divider (1 = absolute width/height, 2/4/8).
	pub divider: i32,
	/// x264 crf.
	pub crf: i32,
	/// ffmpeg encoder preset name (e.g. "veryfast").
	pub preset: String,
	/// Include the audio track.
	pub include_audio: bool,
}

/// The proxy generation params from the global config (the dialog's
/// default values): `ProxyWidth`/`ProxyHeight`/`ProxyDivider`/`ProxyCRF`/
/// `ProxyPreset`/`ProxyIncludeAudio`.
pub fn proxy_params_from_config() -> ProxyParamsUi {
	let codec = oakcodec::proxymanager::ProxyManager::proxy_params_from_config();
	let end = |a: &[u8; 32]| a.iter().position(|&b| b == 0).unwrap_or(a.len());
	let preset = std::str::from_utf8(&codec.preset[..end(&codec.preset)])
		.unwrap_or("")
		.to_string();
	ProxyParamsUi {
		width: codec.width,
		height: codec.height,
		divider: codec.divider,
		crf: codec.crf,
		preset,
		include_audio: codec.include_audio != 0,
	}
}

impl ProxyParamsUi {
	/// The codec-side params these UI params stand for.
	pub fn to_codec(&self) -> oakcodec::proxymanager::ProxyParams {
		let mut p = oakcodec::proxymanager::ProxyParams::default();
		p.width = self.width;
		p.height = self.height;
		p.divider = self.divider;
		p.crf = self.crf;
		p.include_audio = if self.include_audio { 1 } else { 0 };
		let bytes = self.preset.as_bytes();
		let n = bytes.len().min(31);
		p.preset[..n].copy_from_slice(&bytes[..n]);
		p
	}

	/// UI params from codec-side params.
	pub fn from_codec(codec: &oakcodec::proxymanager::ProxyParams) -> ProxyParamsUi {
		let end = |a: &[u8; 32]| a.iter().position(|&b| b == 0).unwrap_or(a.len());
		ProxyParamsUi {
			width: codec.width,
			height: codec.height,
			divider: codec.divider,
			crf: codec.crf,
			preset: std::str::from_utf8(&codec.preset[..end(&codec.preset)])
				.unwrap_or("")
				.to_string(),
			include_audio: codec.include_audio != 0,
		}
	}
}

/// One footage row of the proxy dialog's footage-mode list.
#[derive(Debug, Clone, PartialEq)]
pub struct ProxyFootageRow {
	/// The footage's project-explorer entry id (its node identity).
	pub id: u64,
	/// The footage's display name.
	pub name: String,
	/// The proxy's lifecycle state.
	pub state: ProxyMediaState,
	/// Whether preview playback uses this footage's proxy (the per
	/// footage switch).
	pub enabled: bool,
	/// Whether the footage carries custom generation params.
	pub has_custom: bool,
	/// Whether the footage has a valid video stream (the proxy pipeline
	/// only applies to video-bearing footage).
	pub can_generate: bool,
	/// Whether the footage has a proxy path recorded (the timeline proxy
	/// menu's Reveal / Delete enable condition; a superset of `state`
	/// being `Ready`, since a recorded path can outlive its file).
	pub has_proxy: bool,
}

/// The number of clips eligible for each synchronization mode (the
/// context menu enables an entry at ≥ 2, the C++ enable conditions).
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct SyncEligibility {
	/// Clips with a footage source start timecode.
	pub source_time: usize,
	/// Clips with at least one validated waveform window.
	pub waveform: usize,
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
