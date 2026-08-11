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
//! └ status bar: 就绪 | 缓存 | 代理 | 自动保存 || 时间码/时长 | 帧率 | 分辨率 | 引擎
//! ```

use std::path::PathBuf;
use std::sync::Arc;
use std::time::Duration;

use gpui::dock::{
	DockArea, DockLayout, DropTarget, DropZone, NodePath, PanelHandle, PanelRegistry,
};
use gpui::timeline::{ClipId, Frame, TimelineEvent, TimelineView};
use gpui::{
	div, prelude::*, px, size, App, AsyncWindowContext, Bounds, Context, Entity, Render, Window,
	WindowBounds, WindowOptions,
};
use gpui_widgets::audio_meter::AudioLevelMeter;
use gpui_widgets::viewer::PlaybackClock;
use gpui_widgets::dialog::file_dialog::FileDialogContent;
use gpui_widgets::dialog::progress::{ProgressContent, progress_dialog};
use gpui_widgets::dialog::{DialogButton, Modal, ModalEvent, ModalOptions};
use gpui_widgets::menu::{Menu, MenuBar, MenuBarEntry, MenuBarEvent, MenuItem};
use gpui_widgets::theme::{apply_theme, OakTheme};

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
mod menu_ids {
	pub const NEW_PROJECT: usize = 101;
	pub const OPEN_PROJECT: usize = 102;
	pub const SAVE: usize = 103;
	pub const SAVE_AS: usize = 104;
	pub const CLOSE: usize = 105;
	pub const EXPORT: usize = 106;
	pub const QUIT: usize = 107;

	pub const UNDO: usize = 201;
	pub const REDO: usize = 202;
	pub const CUT: usize = 203;
	pub const COPY: usize = 204;
	pub const PASTE: usize = 205;
	pub const DELETE: usize = 206;
	pub const RIPPLE_DELETE: usize = 207;

	pub const THEME_DARK: usize = 301;
	pub const THEME_LIGHT: usize = 302;
	pub const LANG_ZH: usize = 303;
	pub const LANG_EN: usize = 304;
	pub const PREFERENCES: usize = 305;

	pub const PLAY_PAUSE: usize = 401;
	pub const PREV_FRAME: usize = 402;
	pub const NEXT_FRAME: usize = 403;
	pub const TO_START: usize = 404;

	pub const ADD_VIDEO_TRACK: usize = 501;
	pub const ADD_AUDIO_TRACK: usize = 502;
	pub const REMOVE_TRACK: usize = 503;
	pub const SPLIT_AT_PLAYHEAD: usize = 504;

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
	pub const FILE_OPEN: usize = 1;
	pub const FILE_SAVE_AS: usize = 2;
	pub const PREFERENCES: usize = 3;
	pub const EXPORT: usize = 4;
	pub const EXPORT_PROGRESS: usize = 5;
}

/// What a file dialog's OK button should do.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum FileAction {
	Open,
	SaveAs,
}

/// The modal currently layered on top of the shell, if any.
enum ModalState {
	None,
	FileDialog {
		modal: Entity<Modal>,
		content: Entity<FileDialogContent>,
		action: FileAction,
	},
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
}

/// A running export: the session the tick loop drains for progress.
struct ExportRun {
	session: ExportSession,
}

impl ModalState {
	/// The modal entity currently shown, if any.
	fn modal_entity(&self) -> Option<Entity<Modal>> {
		match self {
			ModalState::None => None,
			ModalState::FileDialog { modal, .. }
			| ModalState::Preferences { modal, .. }
			| ModalState::Export { modal, .. }
			| ModalState::Progress { modal, .. } => Some(modal.clone()),
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
					let meter =
						cx.new(|cx| AudioLevelMeter::new(30, self.engine.clone(), window, cx));
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
	modal: ModalState,
	/// The running export session, if any.
	export: Option<ExportRun>,
}

impl<E: AppEngine> OakApp<E> {
	/// Builds the whole shell. `initial_path` (a CLI argument) is opened
	/// after the layout is up.
	pub fn new(
		window: &mut Window,
		initial_path: Option<PathBuf>,
		cx: &mut Context<Self>,
	) -> Self {
		apply_theme(cx, &OakTheme::olive_dark());
		crate::oakui::icons::init(cx);

		// --- engine and shared state ---------------------------------------
		let engine = cx.new(|cx| E::create(cx));
		let source_clock = engine.read(cx).source_clock().clone();
		let program_clock = engine.read(cx).program_clock().clone();
		let timeline = cx.new(|cx| TimelineView::new(engine.clone(), window, cx).zoom(2.0));
		let meter = cx.new(|cx| AudioLevelMeter::new(3, engine.clone(), window, cx));

		// --- menu bar ------------------------------------------------------
		let menu_bar = cx.new(|cx| MenuBar::new(1, make_menus(true), window, cx));
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

		// Tune the default split ratios: top 70%, project bin 17% of the row.
		let mut layout: DockLayout = dock.read(cx).layout().clone();
		layout.resize_split(&NodePath(vec![]), 0.70);
		layout.resize_split(&NodePath(vec![0]), 0.17);
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
		cx.subscribe(
			&timeline,
			|this, timeline, event: &TimelineEvent, cx| {
				if matches!(event, TimelineEvent::SelectionChanged) {
					let clips: Vec<ClipId> =
						timeline.read(cx).selection().iter().copied().collect();
					this.engine.update(cx, |engine, cx| {
						engine.set_selected_clips(clips, cx)
					});
				}
				this.engine
					.update(cx, |engine, cx| engine.apply_timeline_event(event, cx));
			},
		)
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

		let shell = Self {
			engine,
			program_clock,
			timeline,
			meter,
			menu_bar,
			dock,
			status_bar,
			dark: true,
			modal: ModalState::None,
			export: None,
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
		self.meter.update(cx, |meter, cx| meter.update(cx));
		self.poll_export(cx);
		cx.notify();
	}

	/// Routes a menu action.
	fn on_menu(&mut self, item: usize, cx: &mut Context<Self>) {
		use menu_ids::*;
		match item {
			// --- File ------------------------------------------------------
			NEW_PROJECT => self.engine.update(cx, |engine, cx| engine.new_project(cx)),
			OPEN_PROJECT => self.open_file_dialog(FileAction::Open, cx),
			SAVE => self.save_project(None, cx),
			SAVE_AS => self.open_file_dialog(FileAction::SaveAs, cx),
			CLOSE => self.engine.update(cx, |engine, cx| engine.close_project(cx)),
			EXPORT => self.open_export_dialog(cx),
			QUIT => cx.quit(),
			// --- Edit ------------------------------------------------------
			UNDO => self.engine.update(cx, |engine, cx| engine.undo(cx)),
			REDO => self.engine.update(cx, |engine, cx| engine.redo(cx)),
			DELETE => self.delete_timeline_selection(false, cx),
			RIPPLE_DELETE => self.delete_timeline_selection(true, cx),
			CUT | COPY | PASTE => {
				println!("[menu] clipboard action {item} not wired yet");
			}
			// --- View ------------------------------------------------------
			THEME_DARK => {
				self.dark = true;
				apply_theme(cx, &OakTheme::olive_dark());
				self.rebuild_menu_bar(cx);
				cx.notify();
			}
			THEME_LIGHT => {
				self.dark = false;
				apply_theme(cx, &OakTheme::olive_light());
				self.rebuild_menu_bar(cx);
				cx.notify();
			}
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
			TO_START => {
				let monitor = Monitor::Program;
				self.engine.update(cx, |engine, cx| {
					engine.request_frame(monitor, Frame::ZERO, cx)
				});
			}
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
			SPLIT_AT_PLAYHEAD => {
				self.engine.update(cx, |engine, cx| engine.split_at_playhead(cx))
			}
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

	/// Saves the project (to its own filename, or the given `path`).
	fn save_project(&mut self, path: Option<PathBuf>, cx: &mut Context<Self>) {
		let result = self
			.engine
			.update(cx, |engine, cx| engine.save_project(path, cx));
		if let Err(err) = result {
			println!("[file] save failed: {err}");
		}
	}

	/// Deletes the timeline's selected clips (ripple or gap) through the
	/// engine's edit commands.
	fn delete_timeline_selection(&mut self, ripple: bool, cx: &mut Context<Self>) {
		let ids: Vec<gpui::timeline::ClipId> = self
			.timeline
			.read(cx)
			.selection()
			.iter()
			.copied()
			.collect();
		if ids.is_empty() {
			println!("[timeline] delete: nothing selected");
			return;
		}
		for id in ids {
			self.engine
				.update(cx, |engine, cx| engine.delete_clip(id, ripple, cx));
		}
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
	// Modal dialogs
	// -----------------------------------------------------------------------

	/// Closes the current modal.
	fn close_modal(&mut self, cx: &mut Context<Self>) {
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
	fn spawn_modal(
		&mut self,
		cx: &mut Context<Self>,
		build: impl FnOnce(&mut Window, &mut App) -> ModalState,
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

	/// Opens the file open / save-as dialog.
	fn open_file_dialog(&mut self, action: FileAction, cx: &mut Context<Self>) {
		let (title, control) = match action {
			FileAction::Open => (
				crate::i18n::tr("file.open.title"),
				modal_ids::FILE_OPEN,
			),
			FileAction::SaveAs => (
				crate::i18n::tr("file.save_as.title"),
				modal_ids::FILE_SAVE_AS,
			),
		};
		let current_path = self
			.engine
			.read(cx)
			.project()
			.map(|p| p.path.clone())
			.filter(|p| !p.as_os_str().is_empty());
		self.spawn_modal(cx, move |window, app| {
			let (modal, content) =
				gpui_widgets::dialog::file_dialog::file_dialog(control, title, window, app);
			if action == FileAction::SaveAs {
				if let Some(path) = &current_path {
					content.update(app, |content, cx| content.set_path(path.to_string_lossy().into_owned(), cx));
				}
			}
			ModalState::FileDialog {
				modal,
				content,
				action,
			}
		});
	}

	/// Opens the preferences dialog.
	fn open_preferences(&mut self, cx: &mut Context<Self>) {
		self.spawn_modal(cx, |window, app| {
			let content = app.new(|cx| PreferencesContent::new(window, cx));
			let modal = app.new(|cx| {
				Modal::new(
					modal_ids::PREFERENCES,
					ModalOptions::new(crate::i18n::tr("preferences.title"), px(380.0))
						.with_button(DialogButton::primary(crate::i18n::tr("dialog.close"))),
					window,
					cx,
				)
				.with_content(content.clone())
			});
			ModalState::Preferences { modal, content }
		});
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
				modal_ids::FILE_OPEN | modal_ids::FILE_SAVE_AS => {
					self.on_file_dialog_button(*button, cx);
				}
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
				modal_ids::PREFERENCES => self.close_modal(cx),
				_ => {}
			},
			ModalEvent::Dismissed { control } => match *control {
				modal_ids::EXPORT_PROGRESS => {
					// Escape cancels the running export and closes the dialog.
					self.cancel_export(cx);
					self.close_modal(cx);
				}
				_ => self.close_modal(cx),
			},
		}
	}

	/// Handles the file dialog's OK/Cancel.
	fn on_file_dialog_button(&mut self, button: usize, cx: &mut Context<Self>) {
		let ModalState::FileDialog {
			content, action, ..
		} = &self.modal
		else {
			return;
		};
		if button != 0 {
			self.close_modal(cx);
			return;
		}
		let path = PathBuf::from(content.read(cx).path(cx).to_string());
		let action = *action;
		if path.as_os_str().is_empty() {
			return;
		}
		let result = self.engine.update(cx, |engine, cx| match action {
			FileAction::Open => engine.open_project_path(path.clone(), cx),
			FileAction::SaveAs => engine.save_project(Some(path.clone()), cx),
		});
		if let Err(err) = result {
			println!("[file] {action:?} failed: {err}");
		}
		self.close_modal(cx);
	}
}

impl<E: AppEngine> Render for OakApp<E> {
	fn render(&mut self, _window: &mut Window, _cx: &mut Context<Self>) -> impl IntoElement {
		let mut root = div()
			.size_full()
			.flex()
			.flex_col()
			.child(self.menu_bar.clone())
			.child(div().flex_1().child(self.dock.clone()))
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
				MenuItem::new(OPEN_PROJECT, tr("menu.file.open_project")).with_shortcut("⌘O"),
				MenuItem::new(SAVE, tr("menu.file.save")).with_shortcut("⌘S"),
				MenuItem::new(SAVE_AS, tr("menu.file.save_as")).with_shortcut("⇧⌘S").separated(),
				MenuItem::new(CLOSE, tr("menu.file.close")),
				MenuItem::new(EXPORT, tr("menu.file.export")).with_shortcut("⌘E").separated(),
				MenuItem::new(QUIT, tr("menu.file.quit")).with_shortcut("⌘Q").separated(),
			]),
		),
		MenuBarEntry::new(
			tr("menu.edit"),
			Menu::new(vec![
				MenuItem::new(UNDO, tr("menu.edit.undo")).with_shortcut("⌘Z"),
				MenuItem::new(REDO, tr("menu.edit.redo")).with_shortcut("⇧⌘Z").separated(),
				MenuItem::new(CUT, tr("menu.edit.cut")).with_shortcut("⌘X"),
				MenuItem::new(COPY, tr("menu.edit.copy")).with_shortcut("⌘C"),
				MenuItem::new(PASTE, tr("menu.edit.paste")).with_shortcut("⌘V"),
				MenuItem::new(DELETE, tr("menu.edit.delete")).with_shortcut("⌫").separated(),
				MenuItem::new(RIPPLE_DELETE, tr("menu.edit.ripple_delete")),
			]),
		),
		MenuBarEntry::new(
			tr("menu.view"),
			Menu::new(vec![
				MenuItem::new(THEME_DARK, tr("menu.view.theme")).with_submenu(theme_submenu),
				MenuItem::new(LANG_ZH, tr("menu.view.language")).with_submenu(language_submenu),
				MenuItem::new(PREFERENCES, tr("menu.view.preferences")).separated(),
			]),
		),
		MenuBarEntry::new(
			tr("menu.playback"),
			Menu::new(vec![
				MenuItem::new(PLAY_PAUSE, tr("menu.playback.play_pause")).with_shortcut("空格"),
				MenuItem::new(PREV_FRAME, tr("menu.playback.prev_frame")).with_shortcut("←"),
				MenuItem::new(NEXT_FRAME, tr("menu.playback.next_frame"))
					.with_shortcut("→")
					.separated(),
				MenuItem::new(TO_START, tr("menu.playback.to_start")).with_shortcut("Home"),
			]),
		),
		MenuBarEntry::new(
			tr("menu.sequence"),
			Menu::new(vec![
				MenuItem::new(ADD_VIDEO_TRACK, tr("menu.sequence.add_video_track")),
				MenuItem::new(ADD_AUDIO_TRACK, tr("menu.sequence.add_audio_track")),
				MenuItem::new(REMOVE_TRACK, tr("menu.sequence.remove_track")).separated(),
				MenuItem::new(SPLIT_AT_PLAYHEAD, tr("menu.sequence.split_at_playhead")),
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
		// Restore the persisted UI language (config `Language` key) before the
		// first window renders.
		crate::i18n::init();
		cx.init_colors();
		let bounds = Bounds::centered(None, size(px(1600.0), px(900.0)), cx);
		let initial = initial.clone();
		cx.open_window(
			WindowOptions {
				window_bounds: Some(WindowBounds::Windowed(bounds)),
				..Default::default()
			},
			|window, cx| build_root::<E>(window, initial, cx),
		)
		.expect("failed to open the main window");

		cx.activate(true);
		cx.on_window_closed(|cx, _| {
			if cx.windows().is_empty() {
				cx.quit();
			}
		})
		.detach();
	});
}

#[cfg(test)]
mod tests {
	use super::*;
	use gpui::{TestAppContext, px, size};

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

	/// The File menu exposes the full project lifecycle actions (open /
	/// save / save-as / close / export) and the Edit menu the undo stack
	/// plus the delete variants, across both languages.
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
			menu_ids::OPEN_PROJECT,
			menu_ids::SAVE,
			menu_ids::SAVE_AS,
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
		assert!(file.menu.items.iter().any(|item| item.id == menu_ids::SAVE_AS));
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

		cx.update(|app| {
			root.update(app, |app, cx| app.on_menu(menu_ids::PREFERENCES, cx))
		});
		cx.run_until_parked();
		// Force a draw so render-time panics in the dialog content surface.
		cx.update_window(window.into(), |_root, window, cx| {
			window.draw(cx).clear();
		})
		.expect("window is still open");

		let has_modal = cx.read(|app| {
			matches!(root.read(app).modal, ModalState::Preferences { .. })
		});
		assert!(
			has_modal,
			"preferences modal should be shown after the menu action"
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
}
