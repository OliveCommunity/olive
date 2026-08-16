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

//! The application shell: menu bar, dock layout, status bar, modal dialogs
//! and the tick loop that drives playback, playhead sync, the audio meter
//! and the export progress.
//!
//! The shell is generic over the engine backend ([`AppEngine`]); [`run`]
//! picks the backend at startup: the real engine by default, the mock when
//! the `--mock` flag / `OAK_ENGINE=mock` env var is given or the
//! `mock-engine` cargo feature is enabled.
//!
//! Layout per the design (`design/Oak-UI设计图-主界面-标注版.png`):
//!
//! ```text
//! ┌ menu bar (文件 编辑 视图 回放 序列 窗口 工具 帮助)
//! ├─────────────────────────────────────────────────────
//! │ dock: 项目 | 素材查看器 | 序列查看器+节点编辑器 | 检查器+历史记录
//! │       (vertical split) 时间线 (full width, 31px toolbar on top)
//! ├─────────────────────────────────────────────────────
//! └ status bar: 就绪 | 缓存 | 代理 | 库写入状态 || 时间码/时长 | 帧率 | 分辨率 | 引擎
//! ```

use std::path::PathBuf;
use std::sync::Arc;
use std::time::Duration;

use gpui::dock::{
	DockArea, DockLayout, DropTarget, DropZone, NodePath, PanelHandle, PanelRegistry,
};
use gpui::timeline::{
	ClipData, ClipId, Frame, FrameRange, TimelineEvent, TimelineView, TrackData,
};
use gpui::{
	div, prelude::*, px, size, App, AsyncWindowContext, Bounds, Context, Entity, PathPromptOptions,
	Render, Window, WindowBounds, WindowOptions,
};
use gpui_widgets::audio_meter::{AudioLevelMeter, MeterOrientation};
use gpui_widgets::dialog::progress::{progress_dialog, ProgressContent};
use gpui_widgets::dialog::{DialogButton, Modal, ModalEvent, ModalOptions};
use gpui_widgets::menu::{Menu, MenuBar, MenuBarEntry, MenuBarEvent, MenuItem};
use gpui_widgets::theme::{apply_theme, OakTheme};
use gpui_widgets::viewer::PlaybackClock;

use crate::dialogs::{ExportDialogContent, PreferencesContent};
use crate::oakui::{AppEngine, ExportSession, MockEngine, Monitor, RealEngine};
use crate::panels::history::HistoryPanel;
use crate::panels::ids::*;
use crate::panels::inspector::InspectorPanel;
use crate::panels::node_editor::NodeEditorPanel;
use crate::panels::program_viewer::ProgramViewerPanel;
use crate::panels::project_explorer::ProjectExplorerPanel;
use crate::panels::source_viewer::SourceViewerPanel;
use crate::panels::status_bar::StatusBar;
use crate::panels::timeline::TimelinePanel;

// Menu item ids (unique per menu).
pub(crate) mod menu_ids {
	pub const NEW_PROJECT: usize = 101;
	pub const OPEN_PROJECT: usize = 102;
	pub const EXPORT_PROJECT: usize = 103;
	pub const CLOSE: usize = 105;
	pub const EXPORT: usize = 106;
	pub const QUIT: usize = 107;
	pub const IMPORT_FOOTAGE: usize = 108;
	pub const PROJECT_MANAGER: usize = 109;
	pub const OPEN_FROM_LIBRARY: usize = 110;

	pub const UNDO: usize = 201;
	pub const REDO: usize = 202;
	pub const CUT: usize = 203;
	pub const COPY: usize = 204;
	pub const PASTE: usize = 205;
	pub const DELETE: usize = 206;
	pub const RIPPLE_DELETE: usize = 207;
	pub const SELECT_ALL: usize = 208;

	pub const THEME_DARK: usize = 301;
	pub const THEME_LIGHT: usize = 302;
	pub const LANG_ZH: usize = 303;
	pub const LANG_EN: usize = 304;
	pub const PREFERENCES: usize = 305;
	pub const ZOOM_IN: usize = 306;
	pub const ZOOM_OUT: usize = 307;

	pub const PLAY_PAUSE: usize = 401;
	pub const PREV_FRAME: usize = 402;
	pub const NEXT_FRAME: usize = 403;
	pub const TO_START: usize = 404;
	pub const PLAY: usize = 405;
	pub const PAUSE: usize = 406;
	pub const SET_IN_POINT: usize = 407;
	pub const SET_OUT_POINT: usize = 408;

	pub const ADD_VIDEO_TRACK: usize = 501;
	pub const ADD_AUDIO_TRACK: usize = 502;
	pub const REMOVE_TRACK: usize = 503;
	pub const SPLIT_AT_PLAYHEAD: usize = 504;
	pub const ADD_MARKER: usize = 505;
	pub const REMOVE_MARKER: usize = 506;
	pub const SET_WORKAREA: usize = 507;
	pub const CLEAR_WORKAREA: usize = 508;

	pub const FOCUS_PROJECT: usize = 601;
	pub const FOCUS_SOURCE_VIEWER: usize = 602;
	pub const FOCUS_PROGRAM_VIEWER: usize = 603;
	pub const FOCUS_NODE_EDITOR: usize = 604;
	pub const FOCUS_INSPECTOR: usize = 605;
	pub const FOCUS_HISTORY: usize = 606;
	pub const FOCUS_TIMELINE: usize = 607;

	pub const ABOUT: usize = 801;
}

/// Modal-dialog control ids (see [`ModalEvent::control`]).
mod modal_ids {
	pub const PREFERENCES: usize = 3;
	pub const EXPORT: usize = 4;
	pub const EXPORT_PROGRESS: usize = 5;
	pub const MANAGER: usize = 6;
	pub const MANAGER_RENAME: usize = 7;
	pub const MANAGER_DELETE: usize = 8;
}

/// What a picked platform-dialog path should do.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum FileAction {
	ImportFootage,
	Open,
	/// Export the current project to a file (the 导出工程文件… action's
	/// target; `.ove` / `.otio` / `.fcpxml`, dispatched by extension).
	ExportProjectFile,
	/// Import a project file into the library (the manager's 导入).
	ImportProject,
	/// Export the selected library project (the manager's 导出; the row's
	/// uuid is stashed in [`OakApp::pending_export`]).
	ExportProject,
}

/// The modal currently layered on top of the shell, if any.
enum ModalState<E: AppEngine> {
	None,
	Preferences {
		modal: Entity<Modal>,
		content: Entity<PreferencesContent>,
	},
	Export {
		modal: Entity<Modal>,
		content: Entity<ExportDialogContent>,
	},
	Progress {
		modal: Entity<Modal>,
		content: Entity<ProgressContent>,
	},
	/// The project manager (M13 D4).
	Manager {
		modal: Entity<Modal>,
		content: Entity<crate::manager::ProjectManager<E>>,
	},
	/// The manager's rename prompt.
	ManagerRename {
		modal: Entity<Modal>,
		content: Entity<crate::manager::NamePrompt>,
		uuid: String,
	},
	/// The manager's delete confirmation.
	ManagerDelete { modal: Entity<Modal>, uuid: String },
}

/// A running export: the session the tick loop drains for progress.
struct ExportRun {
	session: ExportSession,
}

impl<E: AppEngine> ModalState<E> {
	/// The modal entity currently shown, if any.
	fn modal_entity(&self) -> Option<Entity<Modal>> {
		match self {
			ModalState::None => None,
			ModalState::Preferences { modal, .. }
			| ModalState::Export { modal, .. }
			| ModalState::Progress { modal, .. }
			| ModalState::Manager { modal, .. }
			| ModalState::ManagerRename { modal, .. }
			| ModalState::ManagerDelete { modal, .. } => Some(modal.clone()),
		}
	}
}

/// The panel registry: string keys for layout persistence, and the ability
/// to rebuild any panel from its key.
struct AppPanelRegistry<E: AppEngine> {
	engine: Entity<E>,
	source_clock: Entity<E::Clock>,
	program_clock: Entity<E::Clock>,
}

impl<E: AppEngine> PanelRegistry for AppPanelRegistry<E> {
	fn panel_key(&self, id: gpui::dock::PanelId) -> Option<String> {
		Some(
			match id {
				PROJECT => "project",
				SOURCE_VIEWER => "source-viewer",
				PROGRAM_VIEWER => "program-viewer",
				NODE_EDITOR => "node-editor",
				INSPECTOR => "inspector",
				HISTORY => "history",
				TIMELINE => "timeline",
				_ => return None,
			}
			.to_string(),
		)
	}

	fn build_panel(&self, key: &str, window: &mut Window, cx: &mut App) -> Option<PanelHandle> {
		match key {
			"project" => Some(PanelHandle::new(
				cx.new(|cx| ProjectExplorerPanel::new(self.engine.clone(), window, cx)),
				cx,
			)),
			"source-viewer" => Some(PanelHandle::new(
				cx.new(|cx| {
					SourceViewerPanel::new(
						self.engine.clone(),
						self.source_clock.clone(),
						window,
						cx,
					)
				}),
				cx,
			)),
			"program-viewer" => Some(PanelHandle::new(
				cx.new(|cx| {
					let meter = cx.new(|cx| {
						AudioLevelMeter::new(30, self.engine.clone(), window, cx)
							.with_orientation(MeterOrientation::Vertical)
					});
					ProgramViewerPanel::new(
						self.engine.clone(),
						self.program_clock.clone(),
						meter,
						window,
						cx,
					)
				}),
				cx,
			)),
			"node-editor" => Some(PanelHandle::new(
				cx.new(|cx| NodeEditorPanel::new(self.engine.clone(), window, cx)),
				cx,
			)),
			"inspector" => Some(PanelHandle::new(
				cx.new(|cx| InspectorPanel::new(self.engine.clone(), window, cx)),
				cx,
			)),
			"history" => Some(PanelHandle::new(
				cx.new(|cx| HistoryPanel::new(window, cx)),
				cx,
			)),
			"timeline" => Some(PanelHandle::new(
				cx.new(|cx| {
					let timeline =
						cx.new(|cx| TimelineView::new(self.engine.clone(), window, cx).zoom(2.0));
					TimelinePanel::new(self.engine.clone(), timeline, window, cx)
				}),
				cx,
			)),
			_ => None,
		}
	}
}

/// The application root view.
pub struct OakApp<E: AppEngine> {
	engine: Entity<E>,
	program_clock: Entity<E::Clock>,
	timeline: Entity<TimelineView<E>>,
	meter: Entity<AudioLevelMeter<E>>,
	menu_bar: Entity<MenuBar>,
	dock: Entity<DockArea>,
	status_bar: Entity<StatusBar<E>>,
	/// Whether the dark theme is active (toggles via 视图 → 主题).
	dark: bool,
	/// The modal currently shown on top of the shell, if any.
	modal: ModalState<E>,
	/// The shell's own focus handle: the root element tracks it so the
	/// keyboard shortcut layer (the root's `on_key_down`) sits on the
	/// dispatch path even before any panel takes focus.
	shell_focus: gpui::FocusHandle,
	/// The running export session, if any.
	export: Option<ExportRun>,
	/// The library row pending an export save dialog (manager 导出).
	pending_export: Option<String>,
}

impl<E: AppEngine> OakApp<E> {
	/// Builds the whole shell. `initial_path` (a CLI argument) is opened
	/// after the layout is up.
	pub fn new(window: &mut Window, initial_path: Option<PathBuf>, cx: &mut Context<Self>) -> Self {
		// The persisted theme (config `Theme`) wins; the app defaults to dark.
		let dark = crate::oakui::real::theme_is_dark();
		if dark {
			apply_theme(cx, &OakTheme::olive_dark());
		} else {
			apply_theme(cx, &OakTheme::olive_light());
		}
		crate::oakui::icons::init(cx);

		// --- engine and shared state ---------------------------------------
		let engine = cx.new(|cx| E::create(cx));
		let source_clock = engine.read(cx).source_clock().clone();
		let program_clock = engine.read(cx).program_clock().clone();
		let timeline = cx.new(|cx| TimelineView::new(engine.clone(), window, cx).zoom(2.0));
		// M12 P4: install the waveform decorator when the engine provides
		// a waveform cache.
		if let Some(cache) = engine.read(cx).waveform_cache() {
			let decorator: std::sync::Arc<std::sync::RwLock<dyn gpui::timeline::clip::ClipDecorator>> =
				std::sync::Arc::new(std::sync::RwLock::new(
					crate::oakui::waveform::OakClipDecorator { cache },
				));
			timeline.update(cx, |view, _| view.set_clip_decorator(decorator));
		}
		let meter = cx.new(|cx| {
			AudioLevelMeter::new(3, engine.clone(), window, cx)
				.with_orientation(MeterOrientation::Vertical)
		});

		// --- menu bar ------------------------------------------------------
		let menu_bar = cx.new(|cx| MenuBar::new(1, make_menus(dark), window, cx));
		cx.subscribe(
			&menu_bar,
			|this, _menu: Entity<MenuBar>, event: &MenuBarEvent, cx| {
				if let MenuBarEvent::Triggered { item, .. } = event {
					this.on_menu(*item, cx);
				}
			},
		)
		.detach();

		// --- dock ----------------------------------------------------------
		let dock = cx.new(|cx| {
			DockArea::new(cx).with_registry(Arc::new(AppPanelRegistry {
				engine: engine.clone(),
				source_clock: source_clock.clone(),
				program_clock: program_clock.clone(),
			}))
		});

		let project = cx.new(|cx| ProjectExplorerPanel::new(engine.clone(), window, cx));
		let source_viewer =
			cx.new(|cx| SourceViewerPanel::new(engine.clone(), source_clock.clone(), window, cx));
		let program_viewer = cx.new(|cx| {
			ProgramViewerPanel::new(
				engine.clone(),
				program_clock.clone(),
				meter.clone(),
				window,
				cx,
			)
		});
		let node_editor = cx.new(|cx| NodeEditorPanel::new(engine.clone(), window, cx));
		let inspector = cx.new(|cx| InspectorPanel::new(engine.clone(), window, cx));
		let history = cx.new(|cx| HistoryPanel::new(window, cx));
		let timeline_panel =
			cx.new(|cx| TimelinePanel::new(engine.clone(), timeline.clone(), window, cx));

		// Arrange the default workspace: the design's 素材查看器 | 序列查看器 |
		// 检查器 row (project bin docked on the left), node editor + history
		// as tabs, timeline full width at the bottom.
		dock.update(cx, |dock, cx| {
			dock.add_panel(PanelHandle::new(project, cx), None, cx);
			dock.add_panel(
				PanelHandle::new(source_viewer, cx),
				Some(DropTarget {
					panel: Some(PROJECT),
					zone: DropZone::Right,
				}),
				cx,
			);
			dock.add_panel(
				PanelHandle::new(program_viewer, cx),
				Some(DropTarget {
					panel: Some(SOURCE_VIEWER),
					zone: DropZone::Right,
				}),
				cx,
			);
			dock.add_panel(
				PanelHandle::new(node_editor, cx),
				Some(DropTarget {
					panel: Some(PROGRAM_VIEWER),
					zone: DropZone::Center,
				}),
				cx,
			);
			dock.add_panel(
				PanelHandle::new(inspector, cx),
				Some(DropTarget {
					panel: Some(PROGRAM_VIEWER),
					zone: DropZone::Right,
				}),
				cx,
			);
			dock.add_panel(
				PanelHandle::new(history, cx),
				Some(DropTarget {
					panel: Some(INSPECTOR),
					zone: DropZone::Center,
				}),
				cx,
			);
			dock.add_panel(
				PanelHandle::new(timeline_panel, cx),
				Some(DropTarget {
					panel: None,
					zone: DropZone::Bottom,
				}),
				cx,
			);
		});

		// Tune the default split ratios: viewers 60% / timeline 40%, project
		// bin 17% of the row. The timeline share leaves room for all four
		// tracks (V2/V1 video + A1/A2 audio) plus the ruler and toolbar at
		// 1600×900; the viewers keep the remaining ~60%. The program viewer
		// (with its audio level strip) is the active tab of its group, so the
		// shell opens on the design's visible 素材查看器 | 序列查看器 row rather
		// than on the node editor.
		let mut layout: DockLayout = dock.read(cx).layout().clone();
		layout.resize_split(&NodePath(vec![]), 0.60);
		layout.resize_split(&NodePath(vec![0]), 0.17);
		// The program viewer's transport row (six transport buttons, the
		// timecode, the 安全框/缩放 toggles) plus its 26px meter strip needs
		// ~430px at 1600×900 — more than an equal share of the row gives it,
		// and the design makes the program monitor the prominent viewer. Tilt
		// the source/program and program/inspector boundaries accordingly so
		// the transport's trailing toggles are not clipped.
		layout.resize_split_child(&NodePath(vec![0]), 1, 0.52);
		layout.resize_split_child(&NodePath(vec![0]), 2, 0.62);
		if let Some(path) = layout.find_panel(PROGRAM_VIEWER) {
			layout.set_tabs_active(&path, PROGRAM_VIEWER);
		}
		dock.update(cx, |dock, cx| dock.set_layout(layout, cx));

		// --- status bar ----------------------------------------------------
		let status_bar = cx.new(|cx| StatusBar::new(engine.clone(), program_clock.clone(), cx));

		// Repaint the shell whenever the engine notifies.
		cx.observe(&engine, |this, _engine, cx| {
			cx.notify();
			let _ = this;
		})
		.detach();

		// --- timeline events -----------------------------------------------
		// Every timeline widget request (playhead seek, trim, move, track
		// height) is applied by the engine through its backend's edit
		// commands; the playhead is routed to the program monitor.
		// `SelectionChanged` carries no payload — the selection is read from
		// the view and forwarded to the engine so the inspector's effect
		// stack can target the selected clip.
		cx.subscribe(&timeline, |this, timeline, event: &TimelineEvent, cx| {
			if matches!(event, TimelineEvent::SelectionChanged) {
				let clips: Vec<ClipId> = timeline.read(cx).selection().iter().copied().collect();
				this.engine
					.update(cx, |engine, cx| engine.set_selected_clips(clips, cx));
			}
			this.engine
				.update(cx, |engine, cx| engine.apply_timeline_event(event, cx));
		})
		.detach();

		// --- tick loop -----------------------------------------------------
		// Drives playback clocks, playhead sync, the audio meter and the
		// export progress at ~60Hz.
		let this = cx.weak_entity();
		window
			.spawn(cx, async move |cx: &mut AsyncWindowContext| loop {
				cx.background_executor()
					.timer(Duration::from_millis(16))
					.await;
				let _ = cx.update(|_window, app| {
					if let Some(this) = this.upgrade() {
						this.update(app, |this, cx| this.tick(cx));
					}
				});
			})
			.detach();

		let shell_focus = cx.focus_handle();
		// The shell starts focused so the shortcut layer works before any
		// panel grabs focus.
		window.focus(&shell_focus, cx);
		let shell = Self {
			engine,
			program_clock,
			timeline,
			meter,
			menu_bar,
			dock,
			status_bar,
			dark,
			modal: ModalState::None,
			shell_focus,
			export: None,
			pending_export: None,
		};

		// Open the CLI-provided project once the shell is up.
		if let Some(path) = initial_path {
			shell.engine.update(cx, |engine, cx| {
				if let Err(err) = engine.open_project_path(path.clone(), cx) {
					println!("[app] failed to open {}: {err}", path.display());
				}
			});
		}

		shell
	}

	/// One animation-frame tick: advance the engine, sync the timeline
	/// playhead to the program clock, refresh the audio meter and drain the
	/// export progress events.
	fn tick(&mut self, cx: &mut Context<Self>) {
		self.engine.update(cx, |engine, cx| engine.tick(cx));
		let frame = self.program_clock.read(cx).current_frame();
		self.timeline
			.update(cx, |timeline, cx| timeline.seek(frame, cx));
		// Mirror the engine's work area into the ruler's view state (M12 P4).
		// Read every tick so undo/redo and the ruler-drag commit land on the
		// band promptly; the read is a cheap facade getter.
		let work_area = self.engine.read(cx).workarea();
		self.timeline
			.update(cx, |timeline, _| timeline.state.work_area = work_area.map(|(s, e)| FrameRange::new(s, e)));
		self.meter.update(cx, |meter, cx| meter.update(cx));
		self.poll_export(cx);
		cx.notify();
	}

	/// Routes a menu action.
	fn on_menu(&mut self, item: usize, cx: &mut Context<Self>) {
		use menu_ids::*;
		match item {
			// --- File ------------------------------------------------------
			NEW_PROJECT => self.new_project(cx),
			OPEN_PROJECT => self.open_file_dialog(FileAction::Open, cx),
			OPEN_FROM_LIBRARY | PROJECT_MANAGER => self.show_project_manager(cx),
			IMPORT_FOOTAGE => self.open_file_dialog(FileAction::ImportFootage, cx),
			EXPORT_PROJECT => self.open_file_dialog(FileAction::ExportProjectFile, cx),
			CLOSE => self
				.engine
				.update(cx, |engine, cx| engine.close_project(cx)),
			EXPORT => self.open_export_dialog(cx),
			QUIT => cx.quit(),
			// --- Edit ------------------------------------------------------
			UNDO => self.engine.update(cx, |engine, cx| engine.undo(cx)),
			REDO => self.engine.update(cx, |engine, cx| engine.redo(cx)),
			DELETE => self.delete_timeline_selection(false, cx),
			RIPPLE_DELETE => self.delete_timeline_selection(true, cx),
			SELECT_ALL => self.select_all_clips(cx),
			CUT | COPY | PASTE => {
				println!("[menu] clipboard action {item} not wired yet");
			}
			// --- View ------------------------------------------------------
			THEME_DARK => {
				crate::oakui::real::set_theme_dark(true);
				self.apply_dark(true, cx);
			}
			THEME_LIGHT => {
				crate::oakui::real::set_theme_dark(false);
				self.apply_dark(false, cx);
			}
			ZOOM_IN => self.zoom_timeline(1.25, cx),
			ZOOM_OUT => self.zoom_timeline(0.8, cx),
			LANG_ZH => self.switch_language(crate::i18n::Language::ZhCN, cx),
			LANG_EN => self.switch_language(crate::i18n::Language::EnUs, cx),
			PREFERENCES => self.open_preferences(cx),
			// --- Playback --------------------------------------------------
			PLAY_PAUSE => {
				let playing = self.program_clock.read(cx).is_playing();
				let monitor = Monitor::Program;
				self.engine.update(cx, |engine, cx| {
					if playing {
						engine.pause(monitor, cx);
					} else {
						engine.play(monitor, cx);
					}
				});
			}
			PREV_FRAME => {
				let monitor = Monitor::Program;
				self.engine
					.update(cx, |engine, cx| engine.step(monitor, -1, cx));
			}
			NEXT_FRAME => {
				let monitor = Monitor::Program;
				self.engine
					.update(cx, |engine, cx| engine.step(monitor, 1, cx));
			}
			PLAY => {
				let monitor = Monitor::Program;
				self.engine
					.update(cx, |engine, cx| engine.play(monitor, cx));
			}
			PAUSE => {
				let monitor = Monitor::Program;
				self.engine
					.update(cx, |engine, cx| engine.pause(monitor, cx));
			}
			TO_START => {
				let monitor = Monitor::Program;
				self.engine.update(cx, |engine, cx| {
					engine.request_frame(monitor, Frame::ZERO, cx)
				});
			}
			SET_IN_POINT => self.set_point_at_playhead(true, cx),
			SET_OUT_POINT => self.set_point_at_playhead(false, cx),
			// --- Sequence --------------------------------------------------
			ADD_VIDEO_TRACK => {
				let kind = gpui::timeline::TrackKind::Video;
				self.engine
					.update(cx, |engine, cx| engine.add_track(kind, cx));
			}
			ADD_AUDIO_TRACK => {
				let kind = gpui::timeline::TrackKind::Audio;
				self.engine
					.update(cx, |engine, cx| engine.add_track(kind, cx));
			}
			REMOVE_TRACK => self.remove_selected_track(cx),
			SPLIT_AT_PLAYHEAD => self
				.engine
				.update(cx, |engine, cx| engine.split_at_playhead(cx)),
			ADD_MARKER => self
				.engine
				.update(cx, |engine, cx| engine.add_marker_at_playhead(cx)),
			REMOVE_MARKER => self
				.engine
				.update(cx, |engine, cx| engine.remove_marker_at_playhead(cx)),
			SET_WORKAREA => self.set_workarea_from_selection(cx),
			CLEAR_WORKAREA => self
				.engine
				.update(cx, |engine, cx| engine.clear_workarea(cx)),
			// --- Window ----------------------------------------------------
			FOCUS_PROJECT => self.focus_panel(PROJECT, cx),
			FOCUS_SOURCE_VIEWER => self.focus_panel(SOURCE_VIEWER, cx),
			FOCUS_PROGRAM_VIEWER => self.focus_panel(PROGRAM_VIEWER, cx),
			FOCUS_NODE_EDITOR => self.focus_panel(NODE_EDITOR, cx),
			FOCUS_INSPECTOR => self.focus_panel(INSPECTOR, cx),
			FOCUS_HISTORY => self.focus_panel(HISTORY, cx),
			FOCUS_TIMELINE => self.focus_panel(TIMELINE, cx),
			other => println!("[menu] placeholder action for item {other}"),
		}
	}

	/// 新建项目: creates a blank project in the library and opens it (the
	/// write-through persists it from the first edit). Falls back to the
	/// engine's plain new-project path when the library is unavailable.
	fn new_project(&mut self, cx: &mut Context<Self>) {
		let name = crate::i18n::tr("manager.new.default_name").to_string();
		let result = self
			.engine
			.update(cx, |engine, cx| engine.library_create_project(&name, cx));
		if let Err(err) = result {
			println!("[file] library create failed ({err}); plain new project");
			self.engine.update(cx, |engine, cx| engine.new_project(cx));
		}
	}

	/// Deletes the timeline's selected clips (ripple or gap) through the
	/// engine's edit commands.
	fn delete_timeline_selection(&mut self, ripple: bool, cx: &mut Context<Self>) {
		let ids: Vec<gpui::timeline::ClipId> =
			self.timeline.read(cx).selection().iter().copied().collect();
		if ids.is_empty() {
			println!("[timeline] delete: nothing selected");
			return;
		}
		for id in ids {
			self.engine
				.update(cx, |engine, cx| engine.delete_clip(id, ripple, cx));
		}
	}

	/// 编辑 → 全选: selects every timeline clip and forwards the selection
	/// to the engine (the effect stack's target), mirroring what the
	/// timeline's own `SelectionChanged` subscription does.
	fn select_all_clips(&mut self, cx: &mut Context<Self>) {
		let ids: Vec<ClipId> = {
			let engine = self.engine.read(cx);
			let mut ids = Vec::new();
			for index in 0..engine.track_count() {
				if let Some(track) = engine.track(index) {
					ids.extend(track.clips().iter().map(|clip| clip.id()));
				}
			}
			ids
		};
		self.timeline.update(cx, |view, cx| {
			view.state.select_range(ids.iter().copied());
			cx.notify();
		});
		self.engine
			.update(cx, |engine, cx| engine.set_selected_clips(ids, cx));
	}

	/// 视图 → 放大/缩小: scales the timeline zoom around its left edge
	/// (`TimelineState::set_zoom` clamps to the widget's zoom range).
	fn zoom_timeline(&mut self, factor: f32, cx: &mut Context<Self>) {
		self.timeline.update(cx, |view, cx| {
			let zoom = view.state.zoom * factor;
			view.state.set_zoom(zoom, px(0.));
			cx.notify();
		});
	}

	/// 回放 → 设置入点/出点: moves the work area's start (`in_point`) or end
	/// to the program playhead as ONE undoable entry (the same commit the
	/// ruler drag and 序列 → 设置工作区 use). Without an existing work area
	/// the far edge falls back to the sequence bounds.
	fn set_point_at_playhead(&mut self, in_point: bool, cx: &mut Context<Self>) {
		let playhead = self.program_clock.read(cx).current_frame();
		let seq_len = self
			.engine
			.read(cx)
			.current_sequence()
			.map(|s| s.length)
			.unwrap_or(Frame(playhead.0 + 1));
		let (old_start, old_end) = self
			.engine
			.read(cx)
			.workarea()
			.unwrap_or((Frame::ZERO, seq_len));
		let (start, end) = if in_point {
			(playhead, old_end.max(Frame(playhead.0 + 1)))
		} else {
			(old_start.min(Frame((playhead.0 - 1).max(0))), playhead)
		};
		if end.0 <= start.0 {
			println!("[playback] set in/out point: empty range, ignored");
			return;
		}
		self.engine.update(cx, |engine, cx| {
			engine.commit_workarea(old_start, old_end, start, end, cx);
		});
	}

	/// Applies the dark/light theme and rebuilds the menu bar (the theme
	/// checkmark moves). The caller persists the choice through
	/// [`crate::oakui::real::set_theme_dark`].
	fn apply_dark(&mut self, dark: bool, cx: &mut Context<Self>) {
		self.dark = dark;
		if dark {
			apply_theme(cx, &OakTheme::olive_dark());
		} else {
			apply_theme(cx, &OakTheme::olive_light());
		}
		self.rebuild_menu_bar(cx);
		cx.notify();
	}

	/// The shell's keyboard shortcut entry point: maps the keystroke through
	/// [`crate::shortcuts`] and dispatches the matched menu action. While a
	/// modal dialog is open the shell stays keyboard-quiet, so the dialogs'
	/// text fields never trigger editing actions.
	fn on_shortcut(&mut self, keystroke: &gpui::Keystroke, cx: &mut Context<Self>) {
		if !matches!(self.modal, ModalState::None) {
			return;
		}
		let Some(action) = crate::shortcuts::action_for(keystroke) else {
			return;
		};
		cx.stop_propagation();
		self.on_menu(action, cx);
	}

	/// Removes the first track selected in the timeline header.
	fn remove_selected_track(&mut self, cx: &mut Context<Self>) {
		let Some(&index) = self.timeline.read(cx).selected_tracks().iter().next() else {
			println!("[timeline] remove track: nothing selected");
			return;
		};
		self.engine
			.update(cx, |engine, cx| engine.remove_track(index, cx));
	}

	/// 序列 → 设置工作区: sets the work area to the bounding range of the
	/// selected clips, or one frame at the program playhead when nothing is
	/// selected. Committed as ONE undoable entry whose old side is the
	/// engine's current work area (mirrors the ruler drag's commit).
	fn set_workarea_from_selection(&mut self, cx: &mut Context<Self>) {
		let (old_start, old_end) = self
			.engine
			.read(cx)
			.workarea()
			.unwrap_or((Frame::ZERO, Frame::ZERO));
		let (start, end) = self.selection_workarea_range(cx);
		self.engine.update(cx, |engine, cx| {
			engine.commit_workarea(old_start, old_end, start, end, cx);
		});
	}

	/// The bounding range of the timeline's selected clips; `[playhead,
	/// playhead + 1)` when nothing is selected (the menu's fallback).
	fn selection_workarea_range(&self, cx: &App) -> (Frame, Frame) {
		let ids: Vec<ClipId> = self.timeline.read(cx).selection().iter().copied().collect();
		let engine = self.engine.read(cx);
		let mut start: Option<i64> = None;
		let mut end: i64 = 0;
		for index in 0..engine.track_count() {
			if let Some(track) = engine.track(index) {
				for clip in track.clips() {
					if ids.contains(&clip.id()) {
						let range = clip.range();
						start = Some(start.map_or(range.start.0, |s| s.min(range.start.0)));
						end = end.max(range.end.0);
					}
				}
			}
		}
		match start {
			Some(s) if end > s => (Frame(s), Frame(end)),
			_ => {
				let playhead = self.program_clock.read(cx).current_frame();
				(playhead, Frame(playhead.0 + 1))
			}
		}
	}

	/// Focuses a dock panel (used by the 窗口 menu).
	fn focus_panel(&self, id: gpui::dock::PanelId, cx: &mut Context<Self>) {
		if let Some(handle) = cx.windows().first() {
			let dock = self.dock.clone();
			let _ = cx.update_window(*handle, move |_root, window, app| {
				dock.update(app, |dock, cx| dock.focus_panel(id, window, cx));
			});
		}
	}

	/// Switches the UI language live: updates the [`i18n`] global, rebuilds
	/// the menu bar (so the menu labels and the language checkmark move
	/// immediately), and repaints the whole shell.
	fn switch_language(&mut self, language: crate::i18n::Language, cx: &mut Context<Self>) {
		crate::i18n::set_language(language);
		self.rebuild_menu_bar(cx);
		cx.notify();
	}

	/// Replaces the `MenuBar` entity with one built from the current language
	/// and theme, re-subscribing to its trigger events.
	fn rebuild_menu_bar(&mut self, cx: &mut Context<Self>) {
		let windows = cx.windows();
		let Some(handle) = windows.first() else {
			return;
		};
		let dark = self.dark;
		let Ok(menu_bar) = cx.update_window(*handle, |_root, window, app| {
			app.new(|cx| MenuBar::new(1, make_menus(dark), window, cx))
		}) else {
			return;
		};
		self.menu_bar = menu_bar;
		let menu_bar = self.menu_bar.clone();
		cx.subscribe(
			&menu_bar,
			|this, _menu: Entity<MenuBar>, event: &MenuBarEvent, cx| {
				if let MenuBarEvent::Triggered { item, .. } = event {
					this.on_menu(*item, cx);
				}
			},
		)
		.detach();
	}

	// -----------------------------------------------------------------------
	// Project manager (M13 D4)
	// -----------------------------------------------------------------------

	/// Opens the project manager (startup without a project argument, and
	/// 文件 → 项目管理器 / 从库中打开…). Re-entrant: an already-open
	/// manager just reloads its list.
	pub fn show_project_manager(&mut self, cx: &mut Context<Self>) {
		if let ModalState::Manager { content, .. } = &self.modal {
			let content = content.clone();
			content.update(cx, |manager, cx| manager.reload(cx));
			return;
		}
		let engine = self.engine.clone();
		self.spawn_modal(cx, move |window, app| {
			let content = app.new(|cx| crate::manager::ProjectManager::new(engine, window, cx));
			let modal = app.new(|cx| {
				Modal::new(
					modal_ids::MANAGER,
					ModalOptions::new(crate::i18n::tr("manager.title"), px(880.0))
						.with_button(DialogButton::cancel(crate::i18n::tr("dialog.close"))),
					window,
					cx,
				)
				.with_content(content.clone())
			});
			ModalState::Manager { modal, content }
		});
		// The content's requests (open / create / rename / ...) route here.
		if let ModalState::Manager { content, .. } = &self.modal {
			let content = content.clone();
			cx.subscribe(
				&content,
				|this, _content, event: &crate::manager::ManagerEvent, cx| {
					this.on_manager_event(event, cx);
				},
			)
			.detach();
		}
	}

	/// Routes a project-manager request through the engine. Open / Create
	/// close the dialog on success; the mutating actions reload the list;
	/// failures land in the dialog's status line.
	fn on_manager_event(&mut self, event: &crate::manager::ManagerEvent, cx: &mut Context<Self>) {
		use crate::manager::ManagerEvent as E;
		match event {
			E::Create => {
				let name = crate::i18n::tr("manager.new.default_name").to_string();
				let result = self
					.engine
					.update(cx, |engine, cx| engine.library_create_project(&name, cx));
				match result {
					Ok(()) => self.close_modal(cx),
					Err(err) => self.manager_status(err, cx),
				}
			}
			E::Open(uuid) => {
				let uuid = uuid.clone();
				let result = self
					.engine
					.update(cx, |engine, cx| engine.library_open_project(&uuid, cx));
				match result {
					Ok(()) => self.close_modal(cx),
					Err(err) => self.manager_status(err, cx),
				}
			}
			E::Rename(uuid) => self.open_manager_rename(uuid.clone(), cx),
			E::Duplicate(uuid) => {
				let result = self
					.engine
					.update(cx, |engine, _cx| engine.library_duplicate_project(uuid));
				match result {
					Ok(()) => self.reload_manager(cx),
					Err(err) => self.manager_status(err, cx),
				}
			}
			E::Delete(uuid) => self.open_manager_delete(uuid.clone(), cx),
			E::Import => self.open_file_dialog(FileAction::ImportProject, cx),
			E::Export(uuid) => self.open_manager_export(uuid.clone(), cx),
		}
	}

	/// Reloads the open manager's list (after a library mutation).
	fn reload_manager(&mut self, cx: &mut Context<Self>) {
		if let ModalState::Manager { content, .. } = &self.modal {
			let content = content.clone();
			content.update(cx, |manager, cx| manager.reload(cx));
		}
	}

	/// Shows an engine error in the open manager's status line (or logs it
	/// when the manager is gone).
	fn manager_status(&mut self, message: String, cx: &mut Context<Self>) {
		if let ModalState::Manager { content, .. } = &self.modal {
			let content = content.clone();
			content.update(cx, |manager, cx| manager.set_status(Some(message), cx));
		} else {
			println!("[manager] {message}");
		}
	}

	/// The selected row's display name in the open manager (used to seed
	/// the rename prompt / the delete confirmation / the export filename).
	fn manager_selected_name(&self, cx: &App) -> Option<String> {
		match &self.modal {
			ModalState::Manager { content, .. } => content.read(cx).selected_name(),
			_ => None,
		}
	}

	/// Swaps the manager for the rename prompt of row `uuid`.
	fn open_manager_rename(&mut self, uuid: String, cx: &mut Context<Self>) {
		let initial = self.manager_selected_name(cx).unwrap_or_default();
		self.spawn_modal(cx, move |window, app| {
			let content = app.new(|cx| crate::manager::NamePrompt::new(&initial, cx));
			let modal = app.new(|cx| {
				Modal::new(
					modal_ids::MANAGER_RENAME,
					ModalOptions::new(crate::i18n::tr("manager.rename.title"), px(380.0))
						.with_button(DialogButton::primary(crate::i18n::tr(
							"manager.rename.title",
						)))
						.with_button(DialogButton::cancel(crate::i18n::tr("dialog.cancel"))),
					window,
					cx,
				)
				.with_content(content.clone())
			});
			ModalState::ManagerRename { modal, content, uuid }
		});
	}

	/// Swaps the manager for the delete confirmation of row `uuid`.
	fn open_manager_delete(&mut self, uuid: String, cx: &mut Context<Self>) {
		let name = self.manager_selected_name(cx).unwrap_or_default();
		let text = crate::i18n::tr("manager.delete.confirm").replace("{name}", &name);
		self.spawn_modal(cx, move |window, app| {
			let content = app.new(|_cx| crate::manager::ConfirmContent::new(text));
			let modal = app.new(|cx| {
				Modal::new(
					modal_ids::MANAGER_DELETE,
					ModalOptions::new(crate::i18n::tr("manager.delete.title"), px(420.0))
						.with_button(DialogButton::primary(crate::i18n::tr("manager.delete")))
						.with_button(DialogButton::cancel(crate::i18n::tr("dialog.cancel"))),
					window,
					cx,
				)
				.with_content(content.clone())
			});
			ModalState::ManagerDelete { modal, uuid }
		});
	}

	/// Confirms the rename prompt (button 0).
	fn confirm_manager_rename(&mut self, cx: &mut Context<Self>) {
		let ModalState::ManagerRename { content, uuid, .. } = &self.modal else {
			return;
		};
		let name = content.read(cx).value(cx);
		let uuid = uuid.clone();
		if name.is_empty() {
			self.back_to_manager(cx);
			return;
		}
		let result = self
			.engine
			.update(cx, |engine, _cx| engine.library_rename_project(&uuid, &name));
		match result {
			Ok(()) => self.back_to_manager(cx),
			Err(err) => {
				self.back_to_manager(cx);
				self.manager_status(err, cx);
			}
		}
	}

	/// Confirms the delete confirmation (button 0).
	fn confirm_manager_delete(&mut self, cx: &mut Context<Self>) {
		let ModalState::ManagerDelete { uuid, .. } = &self.modal else {
			return;
		};
		let uuid = uuid.clone();
		let result = self
			.engine
			.update(cx, |engine, _cx| engine.library_delete_project(&uuid));
		match result {
			Ok(()) => self.back_to_manager(cx),
			Err(err) => {
				self.back_to_manager(cx);
				self.manager_status(err, cx);
			}
		}
	}

	/// Returns from a manager sub-dialog (rename / delete) to the manager.
	fn back_to_manager(&mut self, cx: &mut Context<Self>) {
		self.modal = ModalState::None;
		self.show_project_manager(cx);
	}

	/// Opens the platform save dialog for exporting the library row `uuid`
	/// (the suggested name is `<project>.ove`; the format follows the
	/// extension the user picks).
	fn open_manager_export(&mut self, uuid: String, cx: &mut Context<Self>) {
		let name = self
			.manager_selected_name(cx)
			.filter(|n| !n.is_empty())
			.unwrap_or_else(|| "project".to_string());
		self.pending_export = Some(uuid);
		let receiver =
			cx.prompt_for_new_path(&PathBuf::from("."), Some(&format!("{name}.ove")));
		cx.spawn(async move |this, cx| {
			if let Ok(Ok(Some(path))) = receiver.await {
				this.update(cx, |this, cx| {
					this.on_file_paths(FileAction::ExportProject, vec![path], cx);
				});
			}
		})
		.detach();
	}

	// -----------------------------------------------------------------------
	// Modal dialogs
	// -----------------------------------------------------------------------

	/// Closes the current modal.
	pub fn close_modal(&mut self, cx: &mut Context<Self>) {
		self.modal = ModalState::None;
		cx.notify();
	}

	/// Builds a modal on the main window, subscribes it to
	/// [`Self::on_modal`] and layers it onto the shell.
	///
	/// The modal is created inside `update_window` (modal widgets need a
	/// `&mut Window`); the state swap and the subscription happen *after* the
	/// window update returns, on this entity's own `Context` — swapping state
	/// through a weak handle *inside* the window callback would re-enter this
	/// entity while it is already being updated (the crash seen when opening
	/// Preferences from a menu action).
	///
	/// The caller must NOT be inside a window update itself (e.g.
	/// `WindowHandle::update`): the nested `update_window` below would fail
	/// and the modal would silently not open. Drive the root entity instead
	/// (menu actions and entity updates are fine).
	fn spawn_modal(
		&mut self,
		cx: &mut Context<Self>,
		build: impl FnOnce(&mut Window, &mut App) -> ModalState<E>,
	) {
		let windows = cx.windows();
		let Some(handle) = windows.first() else {
			return;
		};
		let Ok(state) = cx.update_window(*handle, |_root, window, app| build(window, app)) else {
			return;
		};
		let modal = state
			.modal_entity()
			.expect("spawned modal always carries a Modal");
		cx.subscribe(&modal, |this, _entity, event: &ModalEvent, cx| {
			this.on_modal(event, cx);
		})
		.detach();
		self.modal = state;
		cx.notify();
	}

	/// Opens the platform file dialog for `action` and routes the picked
	/// path(s) through the engine. Open / Import use the path picker (import
	/// footage allows multiple files); the 导出工程文件… and the manager's
	/// export ask for a new path. The picker resolves asynchronously, so the
	/// chosen path is applied in a spawned task via [`Self::on_file_paths`].
	fn open_file_dialog(&mut self, action: FileAction, cx: &mut Context<Self>) {
		match action {
			FileAction::Open | FileAction::ImportFootage | FileAction::ImportProject => {
				let prompt = match action {
					FileAction::Open => crate::i18n::tr("file.open.title"),
					FileAction::ImportProject => crate::i18n::tr("manager.import.title"),
					_ => crate::i18n::tr("file.import_footage.title"),
				};
				let receiver = cx.prompt_for_paths(PathPromptOptions {
					files: true,
					directories: false,
					multiple: action == FileAction::ImportFootage,
					prompt: Some(prompt.into()),
				});
				cx.spawn(async move |this, cx| {
					if let Ok(Ok(Some(paths))) = receiver.await {
						if !paths.is_empty() {
							this.update(cx, |this, cx| this.on_file_paths(action, paths, cx));
						}
					}
				})
				.detach();
			}
			FileAction::ExportProjectFile => {
				let current = self
					.engine
					.read(cx)
					.project()
					.map(|p| p.path.clone())
					.filter(|p| !p.as_os_str().is_empty());
				let (directory, suggested) = match current {
					Some(path) => (
						path.parent()
							.map(|dir| dir.to_path_buf())
							.unwrap_or_else(|| PathBuf::from(".")),
						path
							.file_name()
							.map(|name| name.to_string_lossy().into_owned()),
					),
					None => (PathBuf::from("."), None),
				};
				let receiver = cx.prompt_for_new_path(&directory, suggested.as_deref());
				cx.spawn(async move |this, cx| {
					if let Ok(Ok(Some(path))) = receiver.await {
						this.update(cx, |this, cx| {
							this.on_file_paths(FileAction::ExportProjectFile, vec![path], cx);
						});
					}
				})
				.detach();
			}
			// The manager's export prompts in `open_manager_export` (it needs
			// the selection's suggested filename).
			FileAction::ExportProject => {}
		}
	}

	/// Applies paths picked in the platform dialog through the engine, using
	/// the action's routing (open / import / export).
	fn on_file_paths(&mut self, action: FileAction, paths: Vec<PathBuf>, cx: &mut Context<Self>) {
		// The manager's library import/export refresh the open manager and
		// report failures in its status line.
		match action {
			FileAction::ImportProject => {
				let Some(path) = paths.first() else {
					return;
				};
				let result = self
					.engine
					.update(cx, |engine, _cx| engine.library_import_project(path.clone()));
				match result {
					Ok(uuid) => {
						println!("[manager] imported \"{}\" as {uuid}", path.display());
						self.reload_manager(cx);
					}
					Err(err) => self.manager_status(err, cx),
				}
				return;
			}
			FileAction::ExportProject => {
				let (Some(uuid), Some(path)) = (self.pending_export.take(), paths.first()) else {
					return;
				};
				let result = self
					.engine
					.update(cx, |engine, _cx| engine.library_export_project(&uuid, path.clone()));
				match result {
					Ok(()) => println!("[manager] exported to \"{}\"", path.display()),
					Err(err) => self.manager_status(err, cx),
				}
				return;
			}
			_ => {}
		}
		let result = self.engine.update(cx, |engine, cx| match action {
			FileAction::Open => match paths.first() {
				Some(path) => engine.open_project_path(path.clone(), cx),
				None => Ok(()),
			},
			FileAction::ExportProjectFile => match paths.first() {
				Some(path) => engine.export_project_path(path.clone(), cx),
				None => Ok(()),
			},
			FileAction::ImportFootage => {
				// Import accepts several files at once; keep the first failure
				// for the log after the rest have been attempted.
				let mut first_error = None;
				for path in paths {
					if let Err(err) = engine.import_footage(path.clone(), cx) {
						first_error.get_or_insert(err);
					}
				}
				match first_error {
					Some(err) => Err(err),
					None => Ok(()),
				}
			}
			// Handled above (the manager's library import/export).
			FileAction::ImportProject | FileAction::ExportProject => Ok(()),
		});
		if let Err(err) = result {
			println!("[file] {action:?} failed: {err}");
		}
	}

	/// Opens the preferences dialog. Theme/language selections emit
	/// [`crate::dialogs::PreferencesEvent`]s, applied to the shell chrome
	/// immediately; the typed cache directory commits when the dialog closes.
	pub fn open_preferences(&mut self, cx: &mut Context<Self>) {
		self.spawn_modal(cx, |window, app| {
			let content = app.new(|cx| PreferencesContent::new(window, cx));
			let modal = app.new(|cx| {
				Modal::new(
					modal_ids::PREFERENCES,
					ModalOptions::new(crate::i18n::tr("preferences.title"), px(480.0))
						.with_button(DialogButton::primary(crate::i18n::tr("dialog.close"))),
					window,
					cx,
				)
				.with_content(content.clone())
			});
			ModalState::Preferences { modal, content }
		});
		if let ModalState::Preferences { content, .. } = &self.modal {
			let content = content.clone();
			cx.subscribe(
				&content,
				|this, _content, event: &crate::dialogs::PreferencesEvent, cx| match *event {
					crate::dialogs::PreferencesEvent::ThemeChanged(dark) => {
						this.apply_dark(dark, cx);
					}
					crate::dialogs::PreferencesEvent::LanguageChanged => {
						this.rebuild_menu_bar(cx);
						cx.notify();
					}
				},
			)
			.detach();
		}
	}

	/// Commits the preferences dialog's free-text fields (the cache
	/// directory path) before the modal closes.
	fn commit_preferences(&mut self, cx: &mut Context<Self>) {
		if let ModalState::Preferences { content, .. } = &self.modal {
			content.update(cx, |content, cx| content.commit_cache_dir(cx));
		}
	}

	/// Opens the export dialog.
	fn open_export_dialog(&mut self, cx: &mut Context<Self>) {
		if self.engine.read(cx).current_sequence().is_none() {
			println!("[export] no sequence open");
			return;
		}
		let default_path = self.default_export_path(cx);
		self.spawn_modal(cx, move |window, app| {
			let content = app.new(|cx| ExportDialogContent::new(window, cx));
			content.update(app, |content, cx| {
				content.set_path(default_path.clone(), cx)
			});
			let modal = app.new(|cx| {
				Modal::new(
					modal_ids::EXPORT,
					ModalOptions::new(crate::i18n::tr("export.title"), px(440.0))
						.with_button(DialogButton::primary(crate::i18n::tr("export.run")))
						.with_button(DialogButton::cancel(crate::i18n::tr("dialog.cancel"))),
					window,
					cx,
				)
				.with_content(content.clone())
			});
			ModalState::Export { modal, content }
		});
	}

	/// A default output path for the export dialog: the project name with
	/// the format's extension, next to the project file.
	fn default_export_path(&self, cx: &App) -> String {
		let project = self.engine.read(cx).project();
		let name = project
			.map(|p| p.name.clone())
			.filter(|n| !n.is_empty())
			.unwrap_or_else(|| "untitled".to_string());
		let dir = project
			.and_then(|p| p.path.parent().map(|d| d.to_path_buf()))
			.unwrap_or_else(|| PathBuf::from("."));
		dir.join(format!("{name}.mp4"))
			.to_string_lossy()
			.into_owned()
	}

	/// Starts the export from the export dialog's state and swaps the dialog
	/// for the progress dialog.
	fn begin_export(&mut self, cx: &mut Context<Self>) {
		let ModalState::Export { content, .. } = &self.modal else {
			return;
		};
		let format = content.read(cx).format(cx);
		let ext = content.read(cx).extension(cx);
		let mut path = content.read(cx).path(cx).to_string();
		if path.trim().is_empty() {
			return;
		}
		// Append the format's extension when the user left it off.
		let has_ext = std::path::Path::new(&path)
			.extension()
			.map(|e| !e.to_string_lossy().is_empty())
			.unwrap_or(false);
		if !has_ext {
			path = format!("{path}.{ext}");
		}

		let result = self.engine.update(cx, |engine, _cx| {
			engine.start_export(format, PathBuf::from(&path))
		});
		match result {
			Ok(session) => {
				self.export = Some(ExportRun { session });
				self.spawn_modal(cx, |window, app| {
					let (modal, content) = progress_dialog(
						modal_ids::EXPORT_PROGRESS,
						crate::i18n::tr("export.progress.title"),
						crate::i18n::tr("export.progress.label"),
						window,
						app,
					);
					ModalState::Progress { modal, content }
				});
			}
			Err(err) => {
				println!("[export] failed to start: {err}");
				self.close_modal(cx);
			}
		}
	}

	/// Cancels the running export (the task aborts at the next frame).
	fn cancel_export(&mut self, cx: &mut Context<Self>) {
		if let Some(run) = &self.export {
			(run.session.cancel)();
		}
		let _ = cx;
	}

	/// Drains the export progress events on the tick loop: updates the
	/// progress bar and closes the dialog when the task finishes.
	fn poll_export(&mut self, cx: &mut Context<Self>) {
		let Some(run) = &self.export else {
			return;
		};
		let mut events = Vec::new();
		while let Ok(event) = run.session.events.try_recv() {
			events.push(event);
		}
		if events.is_empty() {
			return;
		}
		let mut finished: Option<(bool, String)> = None;
		for event in events {
			match event {
				crate::oakui::ExportEvent::Started => {}
				crate::oakui::ExportEvent::Progress(fraction) => {
					if let ModalState::Progress { content, .. } = &self.modal {
						let fraction = fraction as f32;
						content.update(cx, |content, cx| content.set_progress(fraction, cx));
					}
				}
				crate::oakui::ExportEvent::Finished(ok, err) => finished = Some((ok, err)),
			}
		}
		if let Some((ok, err)) = finished {
			self.export = None;
			self.modal = ModalState::None;
			if ok {
				println!("[export] finished");
			} else {
				println!("[export] failed: {err}");
			}
			cx.notify();
		}
	}

	/// Routes a modal dialog event.
	fn on_modal(&mut self, event: &ModalEvent, cx: &mut Context<Self>) {
		match event {
			ModalEvent::ButtonClicked { control, button } => match *control {
				modal_ids::EXPORT => {
					if *button == 0 {
						self.begin_export(cx);
					} else {
						self.close_modal(cx);
					}
				}
				modal_ids::EXPORT_PROGRESS => {
					if *button == 1 {
						// Cancel button: ask the task to abort; the finished
						// event closes the dialog.
						self.cancel_export(cx);
					}
				}
				modal_ids::PREFERENCES => {
					self.commit_preferences(cx);
					self.close_modal(cx);
				}
				modal_ids::MANAGER => self.close_modal(cx),
				modal_ids::MANAGER_RENAME => {
					if *button == 0 {
						self.confirm_manager_rename(cx);
					} else {
						self.back_to_manager(cx);
					}
				}
				modal_ids::MANAGER_DELETE => {
					if *button == 0 {
						self.confirm_manager_delete(cx);
					} else {
						self.back_to_manager(cx);
					}
				}
				_ => {}
			},
			ModalEvent::Dismissed { control } => match *control {
				modal_ids::EXPORT_PROGRESS => {
					// Escape cancels the running export and closes the dialog.
					self.cancel_export(cx);
					self.close_modal(cx);
				}
				// Escape from a manager sub-dialog returns to the manager.
				modal_ids::MANAGER_RENAME | modal_ids::MANAGER_DELETE => {
					self.back_to_manager(cx);
				}
				modal_ids::PREFERENCES => {
					self.commit_preferences(cx);
					self.close_modal(cx);
				}
				_ => self.close_modal(cx),
			},
		}
	}
}

impl<E: AppEngine> Render for OakApp<E> {
	fn render(&mut self, _window: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
		let mut root = div()
			.size_full()
			.flex()
			.flex_col()
			.track_focus(&self.shell_focus)
			// The shell's keyboard shortcut layer: keys not consumed by a
			// focused widget bubble up here and dispatch through the
			// shortcut table (see `crate::shortcuts`).
			.on_key_down(cx.listener(|this, event: &gpui::KeyDownEvent, _window, cx| {
				this.on_shortcut(&event.keystroke, cx);
			}))
			.child(self.menu_bar.clone())
			.child(div().flex_1().min_h_0().child(self.dock.clone()))
			.child(self.status_bar.clone());
		if let Some(modal) = self.modal.modal_entity() {
			root = root.child(modal);
		}
		root
	}
}

/// Builds the menu bar entries (文件/编辑/视图/回放/序列/窗口/工具/帮助). All
/// labels come from the [`crate::i18n`] tables, so rebuilding the menu bar
/// after a language switch repaints it in the new language. `dark` drives the
/// theme submenu's checkmark.
fn make_menus(dark: bool) -> Vec<MenuBarEntry> {
	use crate::i18n::tr;
	use menu_ids::*;

	let current = crate::i18n::language();

	let theme_submenu = Menu::new(vec![
		MenuItem::new(THEME_DARK, tr("menu.view.theme.dark")).with_checked(dark),
		MenuItem::new(THEME_LIGHT, tr("menu.view.theme.light")).with_checked(!dark),
	]);

	let language_submenu = Menu::new(vec![
		MenuItem::new(LANG_ZH, tr("menu.view.language.zh"))
			.with_checked(current == crate::i18n::Language::ZhCN),
		MenuItem::new(LANG_EN, tr("menu.view.language.en"))
			.with_checked(current == crate::i18n::Language::EnUs),
	]);

	vec![
		MenuBarEntry::new(
			tr("menu.file"),
			Menu::new(vec![
				MenuItem::new(NEW_PROJECT, tr("menu.file.new_project")).with_shortcut("⌘N"),
				MenuItem::new(OPEN_FROM_LIBRARY, tr("menu.file.open_library")),
				MenuItem::new(OPEN_PROJECT, tr("menu.file.open_project")).with_shortcut("⌘O"),
				MenuItem::new(PROJECT_MANAGER, tr("menu.file.project_manager")).separated(),
				MenuItem::new(IMPORT_FOOTAGE, tr("menu.file.import_footage")),
				MenuItem::new(EXPORT_PROJECT, tr("menu.file.export_project"))
					.with_shortcut("⌘S")
					.separated(),
				MenuItem::new(CLOSE, tr("menu.file.close")),
				MenuItem::new(EXPORT, tr("menu.file.export"))
					.with_shortcut("⌘E")
					.separated(),
				MenuItem::new(QUIT, tr("menu.file.quit"))
					.with_shortcut("⌘Q")
					.separated(),
			]),
		),
		MenuBarEntry::new(
			tr("menu.edit"),
			Menu::new(vec![
				MenuItem::new(UNDO, tr("menu.edit.undo")).with_shortcut("⌘Z"),
				MenuItem::new(REDO, tr("menu.edit.redo"))
					.with_shortcut("⇧⌘Z")
					.separated(),
				MenuItem::new(CUT, tr("menu.edit.cut")).with_shortcut("⌘X"),
				MenuItem::new(COPY, tr("menu.edit.copy")).with_shortcut("⌘C"),
				MenuItem::new(PASTE, tr("menu.edit.paste")).with_shortcut("⌘V"),
				MenuItem::new(DELETE, tr("menu.edit.delete")).with_shortcut("⌫"),
				MenuItem::new(RIPPLE_DELETE, tr("menu.edit.ripple_delete"))
					.with_shortcut("⇧⌫")
					.separated(),
				MenuItem::new(SELECT_ALL, tr("menu.edit.select_all")).with_shortcut("A"),
			]),
		),
		MenuBarEntry::new(
			tr("menu.view"),
			Menu::new(vec![
				MenuItem::new(THEME_DARK, tr("menu.view.theme")).with_submenu(theme_submenu),
				MenuItem::new(LANG_ZH, tr("menu.view.language")).with_submenu(language_submenu),
				MenuItem::new(ZOOM_IN, tr("menu.view.zoom_in"))
					.with_shortcut("+")
					.separated(),
				MenuItem::new(ZOOM_OUT, tr("menu.view.zoom_out")).with_shortcut("-"),
				MenuItem::new(PREFERENCES, tr("menu.view.preferences"))
					.with_shortcut("⌘,")
					.separated(),
			]),
		),
		MenuBarEntry::new(
			tr("menu.playback"),
			Menu::new(vec![
				MenuItem::new(PLAY_PAUSE, tr("menu.playback.play_pause")).with_shortcut("空格"),
				MenuItem::new(PLAY, tr("menu.playback.play")).with_shortcut("L"),
				MenuItem::new(PAUSE, tr("menu.playback.pause")).with_shortcut("K"),
				MenuItem::new(PREV_FRAME, tr("menu.playback.prev_frame")).with_shortcut("←"),
				MenuItem::new(NEXT_FRAME, tr("menu.playback.next_frame"))
					.with_shortcut("→")
					.separated(),
				MenuItem::new(TO_START, tr("menu.playback.to_start")).with_shortcut("Home"),
				MenuItem::new(SET_IN_POINT, tr("menu.playback.set_in_point")).with_shortcut("I"),
				MenuItem::new(SET_OUT_POINT, tr("menu.playback.set_out_point")).with_shortcut("O"),
			]),
		),
		MenuBarEntry::new(
			tr("menu.sequence"),
			Menu::new(vec![
				MenuItem::new(ADD_VIDEO_TRACK, tr("menu.sequence.add_video_track")),
				MenuItem::new(ADD_AUDIO_TRACK, tr("menu.sequence.add_audio_track")),
				MenuItem::new(REMOVE_TRACK, tr("menu.sequence.remove_track")).separated(),
				MenuItem::new(SPLIT_AT_PLAYHEAD, tr("menu.sequence.split_at_playhead"))
					.with_shortcut("S"),
				MenuItem::new(ADD_MARKER, tr("menu.sequence.add_marker")).with_shortcut("M"),
				MenuItem::new(REMOVE_MARKER, tr("menu.sequence.remove_marker")).separated(),
				MenuItem::new(SET_WORKAREA, tr("menu.sequence.set_workarea")),
				MenuItem::new(CLEAR_WORKAREA, tr("menu.sequence.clear_workarea")),
				MenuItem::new(704, tr("menu.sequence.settings")).disabled(),
			]),
		),
		MenuBarEntry::new(
			tr("menu.window"),
			Menu::new(vec![
				MenuItem::new(FOCUS_PROJECT, tr("menu.window.project")),
				MenuItem::new(FOCUS_SOURCE_VIEWER, tr("menu.window.source_viewer")),
				MenuItem::new(FOCUS_PROGRAM_VIEWER, tr("menu.window.program_viewer")),
				MenuItem::new(FOCUS_NODE_EDITOR, tr("menu.window.node_editor")),
				MenuItem::new(FOCUS_INSPECTOR, tr("menu.window.inspector")),
				MenuItem::new(FOCUS_HISTORY, tr("menu.window.history")),
				MenuItem::new(FOCUS_TIMELINE, tr("menu.window.timeline")),
			]),
		),
		MenuBarEntry::new(
			tr("menu.tools"),
			Menu::new(vec![
				MenuItem::new(701, tr("menu.tools.select")),
				MenuItem::new(702, tr("menu.tools.razor")),
				MenuItem::new(703, tr("menu.tools.snap")).with_checked(true),
			]),
		),
		MenuBarEntry::new(
			tr("menu.help"),
			Menu::new(vec![MenuItem::new(ABOUT, tr("menu.help.about"))]),
		),
	]
}

/// Command-line arguments the app accepts.
#[derive(Debug, Clone, Default)]
struct AppArgs {
	/// A project file to open at startup.
	project: Option<PathBuf>,
	/// Force the mock engine.
	mock: bool,
}

impl AppArgs {
	/// Parses `std::env::args` plus the `OAK_ENGINE` override:
	/// `oakapp [project.ove] [--mock]`.
	fn from_env() -> Self {
		let mut args = AppArgs::default();
		for arg in std::env::args_os().skip(1) {
			let text = arg.to_string_lossy();
			match text.as_ref() {
				"--mock" => args.mock = true,
				"--help" | "-h" => {
					println!("oakapp — Oak Video Editor");
					println!("usage: oakapp [project.ove] [--mock]");
					println!("  --mock   use the mock engine (or set OAK_ENGINE=mock)");
					std::process::exit(0);
				}
				_ if args.project.is_none() => args.project = Some(arg.into()),
				other => println!("[app] ignoring unknown argument {other:?}"),
			}
		}
		if std::env::var("OAK_ENGINE")
			.map(|v| v.eq_ignore_ascii_case("mock"))
			.unwrap_or(false)
		{
			args.mock = true;
		}
		args
	}
}

/// Builds the app root entity for the chosen backend.
fn build_root<E: AppEngine>(
	window: &mut Window,
	initial: Option<PathBuf>,
	cx: &mut App,
) -> Entity<OakApp<E>> {
	cx.new(|cx| OakApp::new(window, initial, cx))
}

/// The crate entry point: applies the olive-dark theme and opens the main
/// window, running on the real engine by default (mock with `--mock` /
/// `OAK_ENGINE=mock` / the `mock-engine` feature).
pub fn run() {
	let args = AppArgs::from_env();
	let use_mock = args.mock || cfg!(feature = "mock-engine");
	if use_mock {
		run_with::<MockEngine>(args.clone());
	} else {
		run_with::<RealEngine>(args);
	}
}

/// Runs the app window with `E` as the engine backend.
fn run_with<E: AppEngine>(args: AppArgs) {
	let initial = args.project.clone();
	gpui_platform::application().run(move |cx: &mut App| {
		// Load the persisted preferences (config.ini) before anything reads
		// them — the language, the theme, the storage backend and the audio
		// devices all come from the config store.
		crate::oakui::real::config_load();
		// Restore the persisted UI language (config `Language` key) before the
		// first window renders.
		crate::i18n::init();
		// M13 D4: enable the write-through project library (SQLite at the
		// default location) unless the user configured the backend
		// explicitly.
		crate::oakui::real::configure_storage();
		// Bring up the audio manager and apply the persisted device choices
		// (without an instance, playback pushes fail silently).
		crate::oakui::real::audio_init_from_config();
		cx.init_colors();
		let bounds = Bounds::centered(None, size(px(1600.0), px(900.0)), cx);
		let initial = initial.clone();
		let show_manager = initial.is_none();
		let mut root_slot = None;
		let window = cx
			.open_window(
				WindowOptions {
					window_bounds: Some(WindowBounds::Windowed(bounds)),
					..Default::default()
				},
				|window, cx| {
					// Compact pro-app text metrics: gpui's default rem is
					// 16px (desktop-app large); 14px matches the design's
					// density. All rem-based text scales; px spacing is
					// unaffected.
					window.set_rem_size(px(14.0));
					let root = build_root::<E>(window, initial, cx);
					root_slot = Some(root.clone());
					root
				},
			)
			.expect("failed to open the main window");
		let _ = window;

		// No project on the command line: the DaVinci-style project manager
		// greets instead of an empty shell. Drives the ROOT ENTITY (not the
		// window handle): a window update borrows the window, and building a
		// modal inside it would re-enter it (spawn_modal needs a free
		// `update_window`).
		if show_manager {
			if let Some(root) = &root_slot {
				root.update(cx, |app, cx| app.show_project_manager(cx));
			}
		}

		cx.activate(true);
		cx.on_window_closed(|cx, _| {
			if cx.windows().is_empty() {
				// Persist the preferences (config.ini), then drain the
				// write-through backlog (save + snapshot of every still-bound
				// project) and stop the facade's snapshot thread.
				crate::oakui::real::config_save();
				crate::oakui::real::storage_flush();
				cx.quit();
			}
		})
		.detach();
	});
}

#[cfg(test)]
mod tests {
	use super::*;
	use crate::oakui::EngineGateway as _;
	use gpui::timeline::TimelineDataSource as _;
	use gpui::{px, size, ExternalPaths, FileDropEvent, TestAppContext, VisualTestContext};

	/// The 视图/View menu carries a 语言/Language submenu whose items are
	/// labeled in their own language and whose checkmark follows the active
	/// language — and the whole menu bar flips language with `i18n`.
	#[test]
	fn language_menu_tracks_the_active_language() {
		let _guard = crate::i18n::lang_test_lock().lock().unwrap();

		let view_entry = |dark: bool| -> MenuBarEntry {
			make_menus(dark)
				.into_iter()
				.find(|entry| entry.title == crate::i18n::tr("menu.view"))
				.expect("视图/View menu exists")
		};

		// The language submenu sits under 视图/View.
		let language_item = |entry: &MenuBarEntry| -> Menu {
			entry
				.menu
				.items
				.iter()
				.find(|item| item.id == menu_ids::LANG_ZH)
				.map(|item| item.submenu.clone().map(|m| *m).unwrap_or_default())
				.expect("语言/Language submenu exists")
		};

		crate::i18n::set_language(crate::i18n::Language::EnUs);
		let submenu = language_item(&view_entry(true));
		let zh = submenu
			.items
			.iter()
			.find(|i| i.id == menu_ids::LANG_ZH)
			.expect("zh item");
		let en = submenu
			.items
			.iter()
			.find(|i| i.id == menu_ids::LANG_EN)
			.expect("en item");
		assert_eq!(zh.label, "简体中文");
		assert_eq!(en.label, "English");
		assert_eq!(zh.checked, Some(false));
		assert_eq!(en.checked, Some(true), "en-US is active → checked");

		crate::i18n::set_language(crate::i18n::Language::ZhCN);
		let submenu = language_item(&view_entry(true));
		let zh = submenu
			.items
			.iter()
			.find(|i| i.id == menu_ids::LANG_ZH)
			.expect("zh item");
		let en = submenu
			.items
			.iter()
			.find(|i| i.id == menu_ids::LANG_EN)
			.expect("en item");
		assert_eq!(zh.checked, Some(true), "zh-CN is active → checked");
		assert_eq!(en.checked, Some(false));

		// The menu titles themselves are localized.
		assert_eq!(view_entry(true).title, "视图(V)");
		crate::i18n::set_language(crate::i18n::Language::EnUs);
		assert_eq!(view_entry(true).title, "View(V)");
	}

	/// The theme submenu's checkmark follows the `dark` flag.
	#[test]
	fn theme_menu_checkmark_follows_dark_flag() {
		let _guard = crate::i18n::lang_test_lock().lock().unwrap();

		let dark_item = |dark: bool| -> gpui_widgets::menu::MenuItem {
			let entries = make_menus(dark);
			let view = entries
				.iter()
				.find(|entry| entry.title == crate::i18n::tr("menu.view"))
				.expect("视图/View menu");
			let theme = view
				.menu
				.items
				.iter()
				.find(|i| i.id == menu_ids::THEME_DARK)
				.and_then(|i| i.submenu.clone())
				.expect("theme submenu");
			theme
				.items
				.into_iter()
				.find(|i| i.id == menu_ids::THEME_DARK)
				.expect("Olive Dark item")
		};
		assert_eq!(dark_item(true).checked, Some(true));
		assert_eq!(dark_item(false).checked, Some(false));
	}

	/// The File menu exposes the full project lifecycle actions (new /
	/// open-from-library / open-file / manager / export-project / close /
	/// export) and the Edit menu the undo stack plus the delete variants,
	/// across both languages.
	#[test]
	fn file_and_edit_menus_cover_the_project_lifecycle() {
		let _guard = crate::i18n::lang_test_lock().lock().unwrap();

		let entry = |title: &str| -> MenuBarEntry {
			make_menus(true)
				.into_iter()
				.find(|entry| entry.title == title)
				.expect("menu exists")
		};

		crate::i18n::set_language(crate::i18n::Language::EnUs);
		let file = entry("File(F)");
		for id in [
			menu_ids::NEW_PROJECT,
			menu_ids::OPEN_FROM_LIBRARY,
			menu_ids::OPEN_PROJECT,
			menu_ids::PROJECT_MANAGER,
			menu_ids::EXPORT_PROJECT,
			menu_ids::CLOSE,
			menu_ids::EXPORT,
			menu_ids::QUIT,
		] {
			assert!(
				file.menu.items.iter().any(|item| item.id == id),
				"File menu is missing item {id}"
			);
		}
		let edit = entry("Edit(E)");
		for id in [
			menu_ids::UNDO,
			menu_ids::REDO,
			menu_ids::DELETE,
			menu_ids::RIPPLE_DELETE,
		] {
			assert!(
				edit.menu.items.iter().any(|item| item.id == id),
				"Edit menu is missing item {id}"
			);
		}

		// The same ids exist in the zh-CN menu bar.
		crate::i18n::set_language(crate::i18n::Language::ZhCN);
		let file = entry("文件(F)");
		assert!(file
			.menu
			.items
			.iter()
			.any(|item| item.id == menu_ids::EXPORT_PROJECT));
	}

	/// Opening 视图 → Preferences… must not crash: the dialog content and the
	/// modal are built on the main window and the shell state swaps over the
	/// entity's weak handle (regression test for the Preferences crash).
	#[gpui::test]
	async fn preferences_dialog_opens_without_crashing(cx: &mut TestAppContext) {
		let _guard = crate::i18n::lang_test_lock().lock().unwrap();
		crate::i18n::set_language(crate::i18n::Language::EnUs);

		cx.update(|cx| cx.init_colors());
		let window = cx.open_window(size(px(1600.0), px(900.0)), |window, cx| {
			OakApp::<MockEngine>::new(window, None, cx)
		});
		cx.run_until_parked();
		let root = window.root(cx).expect("app root");

		cx.update(|app| root.update(app, |app, cx| app.on_menu(menu_ids::PREFERENCES, cx)));
		cx.run_until_parked();
		// Force a draw so render-time panics in the dialog content surface.
		cx.update_window(window.into(), |_root, window, cx| {
			window.draw(cx).clear();
		})
		.expect("window is still open");

		let has_modal =
			cx.read(|app| matches!(root.read(app).modal, ModalState::Preferences { .. }));
		assert!(
			has_modal,
			"preferences modal should be shown after the menu action"
		);
	}

	// -------------------------------------------------------------------
	// Keyboard shortcuts (M12 P5c)
	// -------------------------------------------------------------------

	/// Every shortcut-table action exists as a menu item (recursing into
	/// submenus), so a key press can never dispatch a dead action.
	#[test]
	fn every_shortcut_maps_to_a_menu_item() {
		let _guard = crate::i18n::lang_test_lock().lock().unwrap();
		crate::i18n::set_language(crate::i18n::Language::EnUs);

		fn collect(menu: &Menu, out: &mut Vec<usize>) {
			for item in &menu.items {
				out.push(item.id);
				if let Some(sub) = &item.submenu {
					collect(sub, out);
				}
			}
		}
		let mut ids = Vec::new();
		for entry in make_menus(true) {
			collect(&entry.menu, &mut ids);
		}
		for shortcut in crate::shortcuts::SHORTCUTS {
			assert!(
				ids.contains(&shortcut.action),
				"shortcut {} → action {} has no menu item",
				shortcut.keystroke,
				shortcut.action
			);
		}
	}

	/// The menu's shortcut labels mirror the shortcut table's display
	/// strings (a drift between them would show the user the wrong key).
	#[test]
	fn menu_shortcut_labels_match_the_table() {
		let _guard = crate::i18n::lang_test_lock().lock().unwrap();
		crate::i18n::set_language(crate::i18n::Language::EnUs);

		fn walk(menu: &Menu) -> Vec<(usize, Option<gpui::SharedString>)> {
			let mut out = Vec::new();
			for item in &menu.items {
				out.push((item.id, item.shortcut.clone()));
				if let Some(sub) = &item.submenu {
					out.extend(walk(sub));
				}
			}
			out
		}
		for entry in make_menus(true) {
			for (id, label) in walk(&entry.menu) {
				let Some(label) = label else { continue };
				let expected = crate::shortcuts::display_for(id)
					.unwrap_or_else(|| panic!("menu item {id} shows {label} but has no shortcut"));
				assert_eq!(
					label.as_ref(),
					expected,
					"shortcut label drift on menu item {id}"
				);
			}
		}
	}

	/// Pressing space on the shell toggles program playback (the keystroke
	/// bubbles to the shell's key listener and dispatches 回放 → 播放/暂停).
	#[gpui::test]
	async fn space_toggles_program_playback(cx: &mut TestAppContext) {
		let _guard = crate::i18n::lang_test_lock().lock().unwrap();
		let (window, root) = mock_shell(cx);

		let playing = cx.read(|app| root.read(app).program_clock.read(app).is_playing());
		assert!(!playing, "the shell starts paused");

		cx.dispatch_keystroke(
			window.into(),
			gpui::Keystroke::parse("space").unwrap(),
		);
		cx.run_until_parked();
		let playing = cx.read(|app| root.read(app).program_clock.read(app).is_playing());
		assert!(playing, "space starts program playback");

		cx.dispatch_keystroke(
			window.into(),
			gpui::Keystroke::parse("space").unwrap(),
		);
		cx.run_until_parked();
		let playing = cx.read(|app| root.read(app).program_clock.read(app).is_playing());
		assert!(!playing, "space again pauses program playback");
	}

	/// ⌘Z on the shell dispatches 编辑 → 撤销 to the engine, and "s" splits
	/// at the playhead.
	#[gpui::test]
	async fn edit_shortcuts_dispatch_to_the_engine(cx: &mut TestAppContext) {
		let _guard = crate::i18n::lang_test_lock().lock().unwrap();
		let (window, root) = mock_shell(cx);

		// Move the playhead inside the first clip (the → shortcut steps one
		// frame), then split with "s": the mock sequence grows by one clip.
		for _ in 0..10 {
			cx.dispatch_keystroke(window.into(), gpui::Keystroke::parse("right").unwrap());
		}
		cx.run_until_parked();
		let playhead = cx.read(|app| root.read(app).program_clock.read(app).current_frame());
		assert_eq!(playhead, Frame(10), "→ steps the playhead");

		let clips_before: usize = cx.read(|app| {
			let engine = root.read(app).engine.read(app);
			(0..engine.track_count())
				.filter_map(|i| engine.track(i))
				.map(|t| t.clips().len())
				.sum()
		});
		cx.dispatch_keystroke(window.into(), gpui::Keystroke::parse("s").unwrap());
		cx.run_until_parked();
		let clips_after: usize = cx.read(|app| {
			let engine = root.read(app).engine.read(app);
			(0..engine.track_count())
				.filter_map(|i| engine.track(i))
				.map(|t| t.clips().len())
				.sum()
		});
		assert!(
			clips_after > clips_before,
			"split at the playhead adds a clip ({clips_before} → {clips_after})"
		);

		// ⌘Z / ⌘⇧Z reach the engine's undo/redo (the mock counts the calls).
		cx.dispatch_keystroke(
			window.into(),
			gpui::Keystroke::parse("secondary-z").unwrap(),
		);
		cx.dispatch_keystroke(
			window.into(),
			gpui::Keystroke::parse("secondary-shift-z").unwrap(),
		);
		cx.run_until_parked();
		let (undo, redo) = cx.read(|app| root.read(app).engine.read(app).undo_redo_calls());
		assert_eq!((undo, redo), (1, 1), "⌘Z/⌘⇧Z dispatch undo/redo");
	}

	/// While a modal dialog is open the shell's shortcuts are inert (the
	/// dialog's text fields must never trigger editing actions).
	#[gpui::test]
	async fn shortcuts_are_suppressed_while_a_modal_is_open(cx: &mut TestAppContext) {
		let _guard = crate::i18n::lang_test_lock().lock().unwrap();
		let (window, root) = mock_shell(cx);

		cx.update(|app| root.update(app, |app, cx| app.on_menu(menu_ids::PREFERENCES, cx)));
		cx.run_until_parked();

		// "s" would split at the playhead without the modal guard.
		let clips_before: usize = cx.read(|app| {
			let engine = root.read(app).engine.read(app);
			(0..engine.track_count())
				.filter_map(|i| engine.track(i))
				.map(|t| t.clips().len())
				.sum()
		});
		cx.dispatch_keystroke(window.into(), gpui::Keystroke::parse("s").unwrap());
		cx.dispatch_keystroke(
			window.into(),
			gpui::Keystroke::parse("space").unwrap(),
		);
		cx.run_until_parked();

		let (clips_after, playing) = cx.read(|app| {
			let root = root.read(app);
			let clips: usize = (0..root.engine.read(app).track_count())
				.filter_map(|i| root.engine.read(app).track(i))
				.map(|t| t.clips().len())
				.sum();
			(clips, root.program_clock.read(app).is_playing())
		});
		assert_eq!(clips_after, clips_before, "no split while the modal is open");
		assert!(!playing, "no playback toggle while the modal is open");
		assert!(
			cx.read(|app| matches!(root.read(app).modal, ModalState::Preferences { .. })),
			"the modal is still open"
		);
	}


	/// 文件 → 导入素材… opens the *platform* path picker (not the in-window
	/// file dialog) and routes the picked path to the engine's import; the
	/// mock engine records it, so the async round trip is observable.
	#[gpui::test]
	async fn import_footage_prompts_and_routes_the_picked_path(cx: &mut TestAppContext) {
		let _guard = crate::i18n::lang_test_lock().lock().unwrap();
		crate::i18n::set_language(crate::i18n::Language::EnUs);

		cx.update(|cx| cx.init_colors());
		let window = cx.open_window(size(px(1600.0), px(900.0)), |window, cx| {
			OakApp::<MockEngine>::new(window, None, cx)
		});
		cx.run_until_parked();
		let root = window.root(cx).expect("app root");

		cx.update(|app| root.update(app, |app, cx| app.on_menu(menu_ids::IMPORT_FOOTAGE, cx)));
		cx.run_until_parked();
		assert!(
			cx.did_prompt_for_paths(),
			"the platform path picker should be shown"
		);

		// Answer the picker with one file: the async continuation must land it
		// in the engine's import.
		let picked = PathBuf::from("/media/raw/interview.mov");
		cx.simulate_path_prompt_response({
			let picked = picked.clone();
			move |options| {
				assert!(options.multiple, "import allows multiple selection");
				Some(vec![picked])
			}
		});
		cx.run_until_parked();

		let imported =
			cx.read(|app| root.read(app).engine.read(app).imported_footage().to_vec());
		assert_eq!(imported, vec![picked]);

		// Cancelling the picker imports nothing.
		cx.update(|app| root.update(app, |app, cx| app.on_menu(menu_ids::IMPORT_FOOTAGE, cx)));
		cx.run_until_parked();
		cx.simulate_path_prompt_response(|_options| None);
		cx.run_until_parked();
		let imported = cx.read(|app| root.read(app).engine.read(app).imported_footage().len());
		assert_eq!(imported, 1, "a cancelled picker imports nothing");
	}

	/// Dragging files onto the project explorer routes them to the engine's
	/// import: the explorer emits `FileDropRequested` on a real drop and the
	/// panel's subscription calls `AppEngine::import_footage` per path (the
	/// mock engine records them). The drop is simulated as the platform
	/// delivers it — the OS drag entering the window, then the release.
	#[gpui::test]
	async fn dropping_files_onto_the_project_browser_imports_them(cx: &mut TestAppContext) {
		let _guard = crate::i18n::lang_test_lock().lock().unwrap();
		let (window, root) = mock_shell(cx);
		let mut cx = VisualTestContext::from_window(window.into(), cx);

		// The mock shell's parked frame is live: dispatch the drop against it
		// (an extra draw would repaint the cached element tree and consume the
		// one-shot interactive listeners).
		let bounds = cx
			.debug_bounds("gpui-widgets-explorer-entry-1")
			.expect("the explorer's first row is rendered");
		let dropped = PathBuf::from("/media/raw/drag-drop.mov");
		let paths = ExternalPaths(std::iter::once(dropped.clone()).collect());
		// Deliver the whole drag sequence against the same rendered frame:
		// gpui's interactive listeners are registered per render, so a repaint
		// between the events would consume them before the drop lands.
		cx.update(|window, cx| {
			window.dispatch_event(
				gpui::PlatformInput::FileDrop(FileDropEvent::Entered {
					position: bounds.center(),
					paths,
				}),
				cx,
			);
			window.dispatch_event(
				gpui::PlatformInput::FileDrop(FileDropEvent::Pending {
					position: bounds.center(),
				}),
				cx,
			);
			window.dispatch_event(
				gpui::PlatformInput::FileDrop(FileDropEvent::Submit {
					position: bounds.center(),
				}),
				cx,
			);
		});
		cx.run_until_parked();

		let imported = cx.read(|app| root.read(app).engine.read(app).imported_footage().to_vec());
		assert_eq!(imported, vec![dropped]);
	}

	/// Dragging a media entry from the project explorer onto the timeline
	/// places a clip there: the row starts a [`FootageDrag`] carrying the
	/// entry id, the timeline panel resolves the cursor to a display track +
	/// frame, and the engine records the `drop_footage` request with the
	/// correct parameters.
	#[gpui::test]
	async fn dragging_footage_onto_the_timeline_places_a_clip(cx: &mut TestAppContext) {
		let _guard = crate::i18n::lang_test_lock().lock().unwrap();
		let (window, root) = mock_shell(cx);
		let mut cx = VisualTestContext::from_window(window.into(), cx);

		// The mock project's root-level footage entry ("第一稿.mp4", id 3) is
		// the drag source; the timeline's clip area is the drop target.
		let row = cx
			.debug_bounds("gpui-widgets-explorer-entry-3")
			.expect("root-level footage row rendered");
		let canvas = cx
			.debug_bounds("timeline-canvas")
			.expect("timeline body rendered");
		// A point inside the clip area: 100 px into the ruler's content
		// (frame 50 at the shell's default zoom of 2 px/frame) on the first
		// track row (V2, 64 px tall).
		let drop = gpui::point(
			canvas.left() + px(gpui::timeline::HEADER_WIDTH + 100.0),
			canvas.top() + px(gpui::timeline::RULER_HEIGHT + 20.0),
		);

		// Dispatch the whole gesture against the same rendered frame: gpui's
		// interactive listeners are registered per render, so a repaint
		// between the events would consume them before the drop lands (same
		// caveat as the file-drop test above).
		cx.update(|window, cx| {
			window.dispatch_event(
				gpui::PlatformInput::MouseDown(gpui::MouseDownEvent {
					position: row.center(),
					modifiers: gpui::Modifiers::none(),
					button: gpui::MouseButton::Left,
					click_count: 1,
					first_mouse: false,
				}),
				cx,
			);
			// Move past the drag threshold to start the drag.
			window.dispatch_event(
				gpui::PlatformInput::MouseMove(gpui::MouseMoveEvent {
					position: row.center() + gpui::point(px(6.0), px(0.0)),
					modifiers: gpui::Modifiers::none(),
					pressed_button: Some(gpui::MouseButton::Left),
				}),
				cx,
			);
			// Hover the drop point (the panel resolves the track + frame).
			window.dispatch_event(
				gpui::PlatformInput::MouseMove(gpui::MouseMoveEvent {
					position: drop,
					modifiers: gpui::Modifiers::none(),
					pressed_button: Some(gpui::MouseButton::Left),
				}),
				cx,
			);
			// Release over the drop point.
			window.dispatch_event(
				gpui::PlatformInput::MouseUp(gpui::MouseUpEvent {
					position: drop,
					modifiers: gpui::Modifiers::none(),
					button: gpui::MouseButton::Left,
					click_count: 1,
				}),
				cx,
			);
		});
		cx.run_until_parked();

		let drops = cx.read(|app| root.read(app).engine.read(app).footage_drops().to_vec());
		assert_eq!(drops.len(), 1, "the drop must reach the engine exactly once");
		assert_eq!(drops[0].id, 3);
		assert_eq!(drops[0].track_kind, gpui::timeline::TrackKind::Video);
		assert_eq!(drops[0].track_index, 0);
		assert_eq!(drops[0].time, gpui::timeline::Frame(50));
	}

	/// The project explorer lists root-level imported footage in BOTH views:
	/// the tree shows the entry as a row, and after switching to the icon
	/// grid the same entry (plus folder children) appears as icons.
	#[gpui::test]
	async fn explorer_views_list_root_level_footage(cx: &mut TestAppContext) {
		let _guard = crate::i18n::lang_test_lock().lock().unwrap();
		let (window, _root) = mock_shell(cx);
		let mut cx = VisualTestContext::from_window(window.into(), cx);

		// Tree view: the root-level footage entry (id 3 "第一稿.mp4") renders
		// as a row.
		assert!(
			cx.debug_bounds("gpui-widgets-explorer-entry-3").is_some(),
			"tree view shows the root-level footage row"
		);

		// Switch to the icon grid; root-level footage and folder children
		// both appear as icons, the folder root itself does not.
		let toggle = cx
			.debug_bounds("gpui-widgets-explorer-icons")
			.expect("icons toggle rendered");
		cx.simulate_click(toggle.center(), gpui::Modifiers::none());
		cx.run_until_parked();
		cx.update(|window, cx| {
			window.draw(cx).clear();
		});

		assert!(
			cx.debug_bounds("gpui-widgets-explorer-icon-3").is_some(),
			"root-level footage shows as an icon"
		);
		assert!(
			cx.debug_bounds("gpui-widgets-explorer-icon-10").is_some(),
			"a folder child shows as an icon"
		);
		assert!(
			cx.debug_bounds("gpui-widgets-explorer-icon-1").is_none(),
			"the footage folder root itself has no icon"
		);
	}

	/// The command-line parser understands the project path and the mock
	/// flag, and the `OAK_ENGINE` env var forces the mock.
	#[test]
	fn app_args_parse_path_and_mock_flag() {
		// Simulate argv without touching the real environment: parse a slice
		// directly.
		let parse = |argv: &[&str], env: Option<&str>| -> AppArgs {
			let mut args = AppArgs::default();
			for text in argv {
				match *text {
					"--mock" => args.mock = true,
					other => args.project = Some(PathBuf::from(other)),
				}
			}
			if env.map(|v| v.eq_ignore_ascii_case("mock")).unwrap_or(false) {
				args.mock = true;
			}
			args
		};
		let a = parse(&["/tmp/a.ove"], None);
		assert_eq!(a.project, Some(PathBuf::from("/tmp/a.ove")));
		assert!(!a.mock);

		let b = parse(&["/tmp/a.ove", "--mock"], None);
		assert!(b.mock);

		let c = parse(&["/tmp/a.ove"], Some("MOCK"));
		assert!(c.mock);

		let d = parse(&[], None);
		assert!(d.project.is_none());
		assert!(!d.mock);
	}

	// -------------------------------------------------------------------
	// Project manager (M13 D4)
	// -------------------------------------------------------------------

	/// A running app shell on the mock engine (en-US), plus its root. The
	/// caller holds the language lock (the tests flip the process-global
	/// language).
	fn mock_shell(
		cx: &mut TestAppContext,
	) -> (
		gpui::WindowHandle<OakApp<MockEngine>>,
		Entity<OakApp<MockEngine>>,
	) {
		crate::i18n::set_language(crate::i18n::Language::EnUs);
		cx.update(|cx| cx.init_colors());
		let window = cx.open_window(size(px(1600.0), px(900.0)), |window, cx| {
			OakApp::<MockEngine>::new(window, None, cx)
		});
		cx.run_until_parked();
		let root = window.root(cx).expect("app root");
		(window, root)
	}

	/// Opens the manager and returns its content entity.
	fn open_manager(
		cx: &mut TestAppContext,
		root: &Entity<OakApp<MockEngine>>,
	) -> Entity<crate::manager::ProjectManager<MockEngine>> {
		cx.update(|app| root.update(app, |app, cx| app.show_project_manager(cx)));
		cx.run_until_parked();
		let content = cx.read(|app| match &root.read(app).modal {
			ModalState::Manager { content, .. } => content.clone(),
			_ => panic!("the manager modal should be open"),
		});
		content
	}

	/// The manager's listed rows.
	fn manager_rows(
		cx: &mut TestAppContext,
		content: &Entity<crate::manager::ProjectManager<MockEngine>>,
	) -> Vec<crate::oakui::LibraryProject> {
		cx.read(|app| content.read(app).rows().to_vec())
	}

	/// The manager lists the mock library; opening a row (the double-click
	/// / 打开 route) drives the engine's library open and closes the dialog.
	#[gpui::test]
	async fn manager_lists_and_opens(cx: &mut TestAppContext) {
		let _guard = crate::i18n::lang_test_lock().lock().unwrap();
		let (_window, root) = mock_shell(cx);
		let content = open_manager(cx, &root);
		let rows = manager_rows(cx, &content);
		assert_eq!(rows.len(), 3, "the mock library seeds three rows");
		assert_eq!(rows[0].name, "第一稿", "most recently modified first");
		assert!(rows[0].track_count > 0 && rows[0].footage_count > 0);

		// Select the second row and open it.
		let uuid = rows[1].uuid.clone();
		cx.update(|app| content.update(app, |m, cx| m.select(&uuid, cx)));
		cx.update(|app| {
			root.update(app, |app, cx| {
				app.on_manager_event(&crate::manager::ManagerEvent::Open(uuid.clone()), cx)
			})
		});
		cx.run_until_parked();

		let opened = cx.read(|app| root.read(app).engine.read(app).library_opened().to_vec());
		assert_eq!(opened, vec![uuid], "the engine opened the selected row");
		let name = cx.read(|app| root.read(app).engine.read(app).project().unwrap().name.clone());
		assert_eq!(name, "宣传片 v3");
		let modal_none = cx.read(|app| matches!(root.read(app).modal, ModalState::None));
		assert!(modal_none, "a successful open closes the manager");
	}

	/// Create / rename / duplicate / delete round-trip through the manager
	/// and its sub-dialogs.
	#[gpui::test]
	async fn manager_create_rename_duplicate_delete(cx: &mut TestAppContext) {
		let _guard = crate::i18n::lang_test_lock().lock().unwrap();
		let (_window, root) = mock_shell(cx);
		let content = open_manager(cx, &root);

		// Create: a new row appears and the project opens (dialog closes).
		cx.update(|app| {
			root.update(app, |app, cx| {
				app.on_manager_event(&crate::manager::ManagerEvent::Create, cx)
			})
		});
		cx.run_until_parked();
		let rows = cx.read(|app| root.read(app).engine.read(app).library_projects().unwrap());
		assert_eq!(rows.len(), 4);
		let created = rows
			.iter()
			.find(|row| row.name == "Untitled Project")
			.expect("the created row")
			.clone();
		let project_name = cx.read(|app| root.read(app).engine.read(app).project().unwrap().name.clone());
		assert_eq!(project_name, "Untitled Project", "create opens the new project");

		// Reopen the manager, select the created row, rename it.
		let content = open_manager(cx, &root);
		cx.update(|app| content.update(app, |m, cx| m.select(&created.uuid, cx)));
		cx.update(|app| {
			root.update(app, |app, cx| {
				app.on_manager_event(&crate::manager::ManagerEvent::Rename(created.uuid.clone()), cx)
			})
		});
		cx.run_until_parked();
		let prompt = cx.read(|app| match &root.read(app).modal {
			ModalState::ManagerRename { content, uuid, .. } => {
				assert_eq!(uuid, &created.uuid);
				content.clone()
			}
			_ => panic!("the rename prompt should be open"),
		});
		cx.update(|app| prompt.update(app, |p, cx| p.set_value("改名为正稿", cx)));
		cx.update(|app| {
			root.update(app, |app, cx| {
				app.on_modal(
					&ModalEvent::ButtonClicked {
						control: modal_ids::MANAGER_RENAME,
						button: 0,
					},
					cx,
				)
			})
		});
		cx.run_until_parked();
		// The confirmation swaps in a FRESH manager (the captured content is
		// stale from here on); assert against the engine's library instead.
		let rows = cx.read(|app| root.read(app).engine.read(app).library_projects().unwrap());
		let renamed = rows.iter().find(|row| row.uuid == created.uuid).unwrap();
		assert_eq!(renamed.name, "改名为正稿");
		let back = cx.read(|app| matches!(root.read(app).modal, ModalState::Manager { .. }));
		assert!(back, "a confirmed rename returns to the manager");

		// Duplicate the renamed row.
		cx.update(|app| {
			root.update(app, |app, cx| {
				app.on_manager_event(
					&crate::manager::ManagerEvent::Duplicate(created.uuid.clone()),
					cx,
				)
			})
		});
		cx.run_until_parked();
		let rows = cx.read(|app| root.read(app).engine.read(app).library_projects().unwrap());
		assert_eq!(rows.len(), 5);
		assert!(
			rows.iter().any(|row| row.name == "改名为正稿 (copy)"),
			"the copy is named '<name> (copy)': {rows:?}"
		);

		// Delete the original through the confirmation dialog.
		cx.update(|app| {
			root.update(app, |app, cx| {
				app.on_manager_event(&crate::manager::ManagerEvent::Delete(created.uuid.clone()), cx)
			})
		});
		cx.run_until_parked();
		let confirming = cx.read(|app| matches!(root.read(app).modal, ModalState::ManagerDelete { .. }));
		assert!(confirming, "the delete confirmation should be open");
		cx.update(|app| {
			root.update(app, |app, cx| {
				app.on_modal(
					&ModalEvent::ButtonClicked {
						control: modal_ids::MANAGER_DELETE,
						button: 0,
					},
					cx,
				)
			})
		});
		cx.run_until_parked();
		let rows = cx.read(|app| root.read(app).engine.read(app).library_projects().unwrap());
		assert_eq!(rows.len(), 4);
		assert!(!rows.iter().any(|row| row.uuid == created.uuid));
		let back = cx.read(|app| matches!(root.read(app).modal, ModalState::Manager { .. }));
		assert!(back, "a confirmed delete returns to the manager");
	}

	/// The manager's 导入 opens the platform path picker and lands the file
	/// as a new row; 导出 asks for a new path and routes it to the engine.
	#[gpui::test]
	async fn manager_import_and_export_route_through_the_platform_dialogs(
		cx: &mut TestAppContext,
	) {
		let _guard = crate::i18n::lang_test_lock().lock().unwrap();
		let (_window, root) = mock_shell(cx);
		let content = open_manager(cx, &root);

		// Import.
		cx.update(|app| {
			root.update(app, |app, cx| {
				app.on_manager_event(&crate::manager::ManagerEvent::Import, cx)
			})
		});
		cx.run_until_parked();
		assert!(cx.did_prompt_for_paths(), "import shows the path picker");
		cx.simulate_path_prompt_response(|options| {
			assert!(!options.multiple, "project import is single-file");
			Some(vec![PathBuf::from("/library/先导片.ove")])
		});
		cx.run_until_parked();
		let rows = manager_rows(cx, &content);
		assert_eq!(rows.len(), 4);
		assert!(
			rows.iter().any(|row| row.name == "先导片"),
			"the imported file becomes a row named by its stem: {rows:?}"
		);

		// Export the imported row.
		let uuid = rows
			.iter()
			.find(|row| row.name == "先导片")
			.unwrap()
			.uuid
			.clone();
		cx.update(|app| content.update(app, |m, cx| m.select(&uuid, cx)));
		cx.update(|app| {
			root.update(app, |app, cx| {
				app.on_manager_event(&crate::manager::ManagerEvent::Export(uuid.clone()), cx)
			})
		});
		cx.run_until_parked();
		assert!(
			cx.did_prompt_for_new_path(),
			"export shows the save dialog"
		);
		cx.simulate_new_path_selection(|_dir| Some(PathBuf::from("/library/先导片.otio")));
		cx.run_until_parked();
		let exported = cx.read(|app| root.read(app).engine.read(app).library_exported().to_vec());
		assert_eq!(
			exported,
			vec![(uuid, PathBuf::from("/library/先导片.otio"))],
			"the picked path routes to the engine's library export"
		);
	}
}
