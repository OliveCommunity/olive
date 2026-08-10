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

//! The application shell: menu bar, dock layout, status bar and the tick
//! loop that drives playback, playhead sync and the audio meter.
//!
//! Layout per the design (`design/Oak-UI设计图-主界面-标注版.png`):
//!
//! ```text
//! ┌ menu bar (文件 编辑 视图 回放 序列 窗口 工具 帮助)
//! ├─────────────────────────────────────────────────────
//! │ dock: 项目 | 素材查看器 | 序列查看器+节点编辑器 | 检查器+历史记录
//! │       (vertical split) 时间线 (full width, 31px toolbar on top)
//! ├─────────────────────────────────────────────────────
//! └ status bar: 就绪 | 缓存 | 代理 | 自动保存 || 时间码/时长 | 帧率 | 分辨率
//! ```

use std::sync::Arc;
use std::time::Duration;

use gpui::dock::{
	DockArea, DockLayout, DropTarget, DropZone, NodePath, PanelHandle, PanelRegistry,
};
use gpui::timeline::{Frame, TimelineEvent, TimelineView};
use gpui::{
	div, prelude::*, px, size, App, AsyncWindowContext, Bounds, Context, Entity, Render, Window,
	WindowBounds, WindowOptions,
};
use gpui_widgets::audio_meter::AudioLevelMeter;
use gpui_widgets::menu::{Menu, MenuBar, MenuBarEntry, MenuBarEvent, MenuItem};
use gpui_widgets::theme::{apply_theme, OakTheme};

use crate::oakui::{EngineGateway, MockClock, MockEngine, Monitor};
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
	pub const EXPORT: usize = 104;
	pub const QUIT: usize = 105;

	pub const UNDO: usize = 201;
	pub const REDO: usize = 202;
	pub const CUT: usize = 203;
	pub const COPY: usize = 204;
	pub const PASTE: usize = 205;
	pub const DELETE: usize = 206;

	pub const THEME_DARK: usize = 301;
	pub const THEME_LIGHT: usize = 302;
	pub const LANG_ZH: usize = 303;
	pub const LANG_EN: usize = 304;

	pub const PLAY_PAUSE: usize = 401;
	pub const PREV_FRAME: usize = 402;
	pub const NEXT_FRAME: usize = 403;
	pub const TO_START: usize = 404;

	pub const ADD_VIDEO_TRACK: usize = 501;
	pub const ADD_AUDIO_TRACK: usize = 502;

	pub const FOCUS_PROJECT: usize = 601;
	pub const FOCUS_SOURCE_VIEWER: usize = 602;
	pub const FOCUS_PROGRAM_VIEWER: usize = 603;
	pub const FOCUS_NODE_EDITOR: usize = 604;
	pub const FOCUS_INSPECTOR: usize = 605;
	pub const FOCUS_HISTORY: usize = 606;
	pub const FOCUS_TIMELINE: usize = 607;

	pub const ABOUT: usize = 801;
}

/// The panel registry: string keys for layout persistence, and the ability
/// to rebuild any panel from its key.
struct AppPanelRegistry {
	engine: Entity<MockEngine>,
	source_clock: Entity<MockClock>,
	program_clock: Entity<MockClock>,
}

impl PanelRegistry for AppPanelRegistry {
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
		// Each arm builds its own `PanelHandle` because the panel views have
		// different entity types.
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
pub struct OakApp {
	engine: Entity<MockEngine>,
	program_clock: Entity<MockClock>,
	timeline: Entity<TimelineView<MockEngine>>,
	meter: Entity<AudioLevelMeter<MockEngine>>,
	menu_bar: Entity<MenuBar>,
	dock: Entity<DockArea>,
	status_bar: Entity<StatusBar>,
	/// Whether the dark theme is active (toggles via 视图 → 主题).
	dark: bool,
}

impl OakApp {
	/// Builds the whole shell.
	pub fn new(window: &mut Window, cx: &mut Context<Self>) -> Self {
		apply_theme(cx, &OakTheme::olive_dark());

		// --- engine and shared state ---------------------------------------
		let engine = cx.new(|cx| MockEngine::demo(cx));
		let source_clock = engine.read(cx).source_clock.clone();
		let program_clock = engine.read(cx).program_clock.clone();
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
		// The playhead is driven by the program monitor; seeking the timeline
		// (ruler click, keyboard) is routed back to the engine, guarded so
		// clock-driven syncs are no-ops.
		cx.subscribe(
			&timeline,
			|this, _timeline, event: &TimelineEvent, cx| match event {
				TimelineEvent::PlayheadChanged(frame) => {
					let current = this.engine.read(cx).clock_frame(Monitor::Program, cx);
					if *frame != current {
						this.engine.update(cx, |engine, cx| {
							engine.request_frame(Monitor::Program, *frame, cx)
						});
					}
				}
				other => println!("[timeline] request: {other:?} (not applied by the mock)"),
			},
		)
		.detach();

		// --- tick loop -----------------------------------------------------
		// Drives playback clocks, playhead sync and the audio meter at ~60Hz.
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

		Self {
			engine,
			program_clock,
			timeline,
			meter,
			menu_bar,
			dock,
			status_bar,
			dark: true,
		}
	}

	/// One animation-frame tick: advance the engine, sync the timeline
	/// playhead to the program clock, and refresh the audio meter.
	fn tick(&mut self, cx: &mut Context<Self>) {
		self.engine.update(cx, |engine, cx| engine.tick(cx));
		let frame = self.program_clock.read(cx).transport.frame();
		self.timeline
			.update(cx, |timeline, cx| timeline.seek(frame, cx));
		self.meter.update(cx, |meter, cx| meter.update(cx));
		cx.notify();
	}

	/// Routes a menu action.
	fn on_menu(&mut self, item: usize, cx: &mut Context<Self>) {
		use menu_ids::*;
		match item {
			PLAY_PAUSE => {
				let playing = self.program_clock.read(cx).transport.is_playing();
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
	/// immediately), and repaints the whole shell — every label goes through
	/// [`crate::i18n::tr`] at render time, so panels flip without a restart.
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
}

impl Render for OakApp {
	fn render(&mut self, _window: &mut Window, _cx: &mut Context<Self>) -> impl IntoElement {
		div()
			.size_full()
			.flex()
			.flex_col()
			.child(self.menu_bar.clone())
			.child(div().flex_1().child(self.dock.clone()))
			.child(self.status_bar.clone())
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
				MenuItem::new(SAVE, tr("menu.file.save")).with_shortcut("⌘S").separated(),
				MenuItem::new(EXPORT, tr("menu.file.export")).disabled(),
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
				MenuItem::new(DELETE, tr("menu.edit.delete")).separated(),
			]),
		),
		MenuBarEntry::new(
			tr("menu.view"),
			Menu::new(vec![
				MenuItem::new(THEME_DARK, tr("menu.view.theme")).with_submenu(theme_submenu),
				MenuItem::new(LANG_ZH, tr("menu.view.language")).with_submenu(language_submenu),
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
				MenuItem::new(503, tr("menu.sequence.settings")).disabled(),
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

/// The crate entry point: applies the olive-dark theme and opens the main
/// window.
pub fn run() {
	gpui_platform::application().run(|cx: &mut App| {
		// Restore the persisted UI language (oakcommon config `Language` key)
		// before the first window renders.
		crate::i18n::init();
		cx.init_colors();
		let bounds = Bounds::centered(None, size(px(1600.0), px(900.0)), cx);
		cx.open_window(
			WindowOptions {
				window_bounds: Some(WindowBounds::Windowed(bounds)),
				..Default::default()
			},
			|window, cx| cx.new(|cx| OakApp::new(window, cx)),
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
}
