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

//! The action registry: the single data source behind the menu bar, the
//! keyboard shortcuts and (later) the shortcut preferences page and the
//! action search dialog — the Rust counterpart of the C++ `MainMenu` +
//! `MenuShared` action system (`app/window/mainwindow/mainmenu.cpp`,
//! `app/widget/menu/menushared.cpp`).
//!
//! Every entry pairs a gpui action (defined through the [`gpui::actions`]
//! macro) with the C++ action's stable id string (`newproj`, `rippledelete`,
//! `snapping`, … — kept for future shortcut-file compatibility), its i18n
//! key, its default key(s) in gpui keystroke syntax (`secondary-` is the
//! platform command key) and a routing target:
//!
//! * [`Route::Global`] — the app shell handles it (file dialogs, undo,
//!   preferences, tool selection, …);
//! * [`Route::FocusedPanel`] — the command goes to the currently focused
//!   panel through [`crate::panels::commands::PanelCommandHandler`] first,
//!   falling back to the shell's global handler when the panel does not
//!   implement it (the C++ `PanelManager::currently_focused()` pattern).
//!
//! Menu clicks and key presses dispatch through the same path: the menu bar
//! reports the item id, the keymap dispatches the gpui action, and both end
//! up in `OakApp::dispatch_action_id`.

use gpui::{Action, KeyBinding};

// One macro call generates all three views of the registry, so they can
// never drift apart: the gpui action structs, the `ActionId` enum and the
// `REGISTRY` table. Menu ids reuse the pre-action-system values for the
// actions that existed before (the tests' `menu_ids` constants).
macro_rules! define_actions {
	($(
		$name:ident {
			cpp: $cpp:literal,
			i18n: $i18n:literal,
			keys: [$($key:literal),*],
			route: $route:ident,
			menu_id: $menu_id:expr
		}
	);* ;) => {
		gpui::actions!(oak, [ $($name),* ]);

		/// The stable identity of every app action (registry index).
		#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
		pub enum ActionId {
			$($name),*
		}

		impl ActionId {
			/// The menu item id this action dispatches from (the menu bar's
			/// numeric item ids; unique across the whole menu tree).
			pub const fn menu_id(self) -> usize {
				match self {
					$( ActionId::$name => $menu_id ),*
				}
			}

			/// This action's registry entry.
			pub fn entry(self) -> &'static ActionEntry {
				REGISTRY
					.iter()
					.find(|entry| entry.action == self)
					.expect("every ActionId is in the registry")
			}
		}

		/// The action registry, in menu order.
		pub const REGISTRY: &[ActionEntry] = &[
			$( ActionEntry {
				action: ActionId::$name,
				cpp_id: $cpp,
				i18n_key: $i18n,
				default_keys: &[$($key),*],
				route: Route::$route,
				build: || Box::new($name),
			} ),*
		];
	};
}

define_actions! {
	// --- File --------------------------------------------------------------
	NewProject { cpp: "newproj", i18n: "menu.file.new_project", keys: ["secondary-n"], route: Global, menu_id: 101 };
	NewSequence { cpp: "newseq", i18n: "menu.file.new_sequence", keys: ["secondary-shift-n"], route: Global, menu_id: 1001 };
	NewFolder { cpp: "newfolder", i18n: "menu.file.new_folder", keys: [], route: Global, menu_id: 1002 };
	OpenProject { cpp: "openproj", i18n: "menu.file.open_project", keys: ["secondary-o"], route: Global, menu_id: 102 };
	OpenFromLibrary { cpp: "openlibrary", i18n: "menu.file.open_library", keys: [], route: Global, menu_id: 110 };
	ClearOpenRecent { cpp: "clearopenrecent", i18n: "menu.file.clear_recent", keys: [], route: Global, menu_id: 1003 };
	SaveProject { cpp: "saveproj", i18n: "menu.file.export_project", keys: ["secondary-s"], route: Global, menu_id: 103 };
	SaveProjectAs { cpp: "saveprojas", i18n: "menu.file.save_as", keys: ["secondary-shift-s"], route: Global, menu_id: 1004 };
	Revert { cpp: "revert", i18n: "menu.file.revert", keys: ["f12"], route: Global, menu_id: 1005 };
	Import { cpp: "import", i18n: "menu.file.import_footage", keys: ["secondary-i"], route: Global, menu_id: 108 };
	Export { cpp: "export", i18n: "menu.file.export_media", keys: ["secondary-m"], route: Global, menu_id: 106 };
	ProjectProperties { cpp: "projectproperties", i18n: "menu.file.project_properties", keys: ["shift-f10"], route: Global, menu_id: 1006 };
	CloseProject { cpp: "closeproj", i18n: "menu.file.close", keys: [], route: Global, menu_id: 105 };
	ProjectManager { cpp: "projectmanager", i18n: "menu.file.project_manager", keys: [], route: Global, menu_id: 109 };
	Exit { cpp: "exit", i18n: "menu.file.quit", keys: ["secondary-q"], route: Global, menu_id: 107 };

	// --- Edit ---------------------------------------------------------------
	Undo { cpp: "undo", i18n: "menu.edit.undo", keys: ["secondary-z"], route: Global, menu_id: 201 };
	Redo { cpp: "redo", i18n: "menu.edit.redo", keys: ["secondary-shift-z"], route: Global, menu_id: 202 };
	Cut { cpp: "cut", i18n: "menu.edit.cut", keys: ["secondary-x"], route: FocusedPanel, menu_id: 203 };
	Copy { cpp: "copy", i18n: "menu.edit.copy", keys: ["secondary-c"], route: FocusedPanel, menu_id: 204 };
	Paste { cpp: "paste", i18n: "menu.edit.paste", keys: ["secondary-v"], route: FocusedPanel, menu_id: 205 };
	PasteInsert { cpp: "pasteinsert", i18n: "menu.edit.paste_insert", keys: ["secondary-shift-v"], route: FocusedPanel, menu_id: 1010 };
	Duplicate { cpp: "duplicate", i18n: "menu.edit.duplicate", keys: ["secondary-d"], route: FocusedPanel, menu_id: 1011 };
	Rename { cpp: "rename", i18n: "menu.edit.rename", keys: ["f2"], route: FocusedPanel, menu_id: 1012 };
	Delete { cpp: "delete", i18n: "menu.edit.delete", keys: ["delete", "backspace"], route: FocusedPanel, menu_id: 206 };
	RippleDelete { cpp: "rippledelete", i18n: "menu.edit.ripple_delete", keys: ["shift-delete", "shift-backspace"], route: FocusedPanel, menu_id: 207 };
	SplitAtPlayhead { cpp: "split", i18n: "menu.edit.split", keys: ["secondary-k"], route: FocusedPanel, menu_id: 504 };
	SpeedDuration { cpp: "speeddur", i18n: "menu.edit.speed_duration", keys: ["secondary-r"], route: FocusedPanel, menu_id: 1014 };
	DefaultTransition { cpp: "deftransition", i18n: "menu.edit.default_transition", keys: ["secondary-shift-d"], route: FocusedPanel, menu_id: 1015 };
	LinkUnlink { cpp: "linkunlink", i18n: "menu.edit.link_unlink", keys: ["secondary-l"], route: FocusedPanel, menu_id: 1016 };
	EnableDisable { cpp: "enabledisable", i18n: "menu.edit.enable_disable", keys: ["shift-e"], route: FocusedPanel, menu_id: 1017 };
	Nest { cpp: "nest", i18n: "menu.edit.nest", keys: [], route: FocusedPanel, menu_id: 1018 };
	SelectAll { cpp: "selectall", i18n: "menu.edit.select_all", keys: ["secondary-a"], route: FocusedPanel, menu_id: 208 };
	DeselectAll { cpp: "deselectall", i18n: "menu.edit.deselect_all", keys: ["secondary-shift-a"], route: FocusedPanel, menu_id: 1019 };
	Insert { cpp: "insert", i18n: "menu.edit.insert", keys: [","], route: FocusedPanel, menu_id: 1020 };
	Overwrite { cpp: "overwrite", i18n: "menu.edit.overwrite", keys: ["."], route: FocusedPanel, menu_id: 1021 };
	RippleToIn { cpp: "rippletoin", i18n: "menu.edit.ripple_to_in", keys: ["q"], route: FocusedPanel, menu_id: 1022 };
	RippleToOut { cpp: "rippletoout", i18n: "menu.edit.ripple_to_out", keys: ["w"], route: FocusedPanel, menu_id: 1023 };
	EditToIn { cpp: "edittoin", i18n: "menu.edit.edit_to_in", keys: ["secondary-alt-q"], route: FocusedPanel, menu_id: 1024 };
	EditToOut { cpp: "edittoout", i18n: "menu.edit.edit_to_out", keys: ["secondary-alt-w"], route: FocusedPanel, menu_id: 1025 };
	NudgeLeft { cpp: "nudgeleft", i18n: "menu.edit.nudge_left", keys: ["alt-left"], route: FocusedPanel, menu_id: 1026 };
	NudgeRight { cpp: "nudgeright", i18n: "menu.edit.nudge_right", keys: ["alt-right"], route: FocusedPanel, menu_id: 1027 };
	MoveInToPlayhead { cpp: "moveintoplayhead", i18n: "menu.edit.move_in_to_playhead", keys: ["["], route: FocusedPanel, menu_id: 1028 };
	MoveOutToPlayhead { cpp: "moveouttoplayhead", i18n: "menu.edit.move_out_to_playhead", keys: ["]"], route: FocusedPanel, menu_id: 1029 };
	SetInPoint { cpp: "setinpoint", i18n: "menu.edit.set_in_point", keys: ["i"], route: FocusedPanel, menu_id: 407 };
	SetOutPoint { cpp: "setoutpoint", i18n: "menu.edit.set_out_point", keys: ["o"], route: FocusedPanel, menu_id: 408 };
	ResetIn { cpp: "resetin", i18n: "menu.edit.reset_in", keys: [], route: FocusedPanel, menu_id: 1030 };
	ResetOut { cpp: "resetout", i18n: "menu.edit.reset_out", keys: [], route: FocusedPanel, menu_id: 1031 };
	ClearInOut { cpp: "clearinout", i18n: "menu.edit.clear_in_out", keys: ["g"], route: FocusedPanel, menu_id: 1032 };
	DeleteInOut { cpp: "deleteinout", i18n: "menu.edit.delete_in_out", keys: [";"], route: FocusedPanel, menu_id: 1033 };
	RippleDeleteInOut { cpp: "rippledeleteinout", i18n: "menu.edit.ripple_delete_in_out", keys: ["'"], route: FocusedPanel, menu_id: 1034 };
	Marker { cpp: "marker", i18n: "menu.edit.marker", keys: ["m"], route: FocusedPanel, menu_id: 505 };

	// --- View ---------------------------------------------------------------
	ZoomIn { cpp: "zoomin", i18n: "menu.view.zoom_in", keys: ["=", "shift-="], route: FocusedPanel, menu_id: 306 };
	ZoomOut { cpp: "zoomout", i18n: "menu.view.zoom_out", keys: ["-"], route: FocusedPanel, menu_id: 307 };
	IncreaseTrackHeight { cpp: "vzoomin", i18n: "menu.view.increase_track_height", keys: ["secondary-="], route: FocusedPanel, menu_id: 1040 };
	DecreaseTrackHeight { cpp: "vzoomout", i18n: "menu.view.decrease_track_height", keys: ["secondary--"], route: FocusedPanel, menu_id: 1041 };
	ToggleShowAll { cpp: "showall", i18n: "menu.view.show_all", keys: ["\\"], route: FocusedPanel, menu_id: 1042 };
	FullScreen { cpp: "fullscreen", i18n: "menu.view.full_screen", keys: ["f11"], route: Global, menu_id: 1043 };
	FullScreenViewer { cpp: "fullscreenviewer", i18n: "menu.view.full_screen_viewer", keys: [], route: FocusedPanel, menu_id: 1044 };
	ThemeDark { cpp: "themedark", i18n: "menu.view.theme.dark", keys: [], route: Global, menu_id: 301 };
	ThemeLight { cpp: "themelight", i18n: "menu.view.theme.light", keys: [], route: Global, menu_id: 302 };
	LangZh { cpp: "langzh", i18n: "menu.view.language.zh", keys: [], route: Global, menu_id: 303 };
	LangEn { cpp: "langen", i18n: "menu.view.language.en", keys: [], route: Global, menu_id: 304 };

	// --- Playback -----------------------------------------------------------
	GoToStart { cpp: "gotostart", i18n: "menu.playback.to_start", keys: ["home"], route: FocusedPanel, menu_id: 404 };
	PrevFrame { cpp: "prevframe", i18n: "menu.playback.prev_frame", keys: ["left"], route: FocusedPanel, menu_id: 402 };
	PlayPause { cpp: "playpause", i18n: "menu.playback.play_pause", keys: ["space"], route: FocusedPanel, menu_id: 401 };
	PlayInToOut { cpp: "playintoout", i18n: "menu.playback.play_in_to_out", keys: ["shift-space"], route: FocusedPanel, menu_id: 1050 };
	NextFrame { cpp: "nextframe", i18n: "menu.playback.next_frame", keys: ["right"], route: FocusedPanel, menu_id: 403 };
	GoToEnd { cpp: "gotoend", i18n: "menu.playback.go_to_end", keys: ["end"], route: FocusedPanel, menu_id: 1051 };
	GoToPrevCut { cpp: "prevcut", i18n: "menu.playback.prev_cut", keys: ["up"], route: FocusedPanel, menu_id: 1052 };
	GoToNextCut { cpp: "nextcut", i18n: "menu.playback.next_cut", keys: ["down"], route: FocusedPanel, menu_id: 1053 };
	GoToIn { cpp: "gotoin", i18n: "menu.playback.go_to_in", keys: ["shift-i"], route: FocusedPanel, menu_id: 1054 };
	GoToOut { cpp: "gotoout", i18n: "menu.playback.go_to_out", keys: ["shift-o"], route: FocusedPanel, menu_id: 1055 };
	ShuttleLeft { cpp: "decspeed", i18n: "menu.playback.shuttle_left", keys: ["j"], route: FocusedPanel, menu_id: 1056 };
	ShuttleStop { cpp: "pause", i18n: "menu.playback.shuttle_stop", keys: ["k"], route: FocusedPanel, menu_id: 406 };
	ShuttleRight { cpp: "incspeed", i18n: "menu.playback.shuttle_right", keys: ["l"], route: FocusedPanel, menu_id: 405 };
	Loop { cpp: "loop", i18n: "menu.playback.loop", keys: [], route: Global, menu_id: 1057 };

	// --- Sequence -----------------------------------------------------------
	AddVideoTrack { cpp: "addvideotrack", i18n: "menu.sequence.add_video_track", keys: [], route: Global, menu_id: 501 };
	AddAudioTrack { cpp: "addaudiotrack", i18n: "menu.sequence.add_audio_track", keys: [], route: Global, menu_id: 502 };
	RemoveTrack { cpp: "removetrack", i18n: "menu.sequence.remove_track", keys: [], route: Global, menu_id: 503 };
	SetWorkArea { cpp: "setworkarea", i18n: "menu.sequence.set_workarea", keys: [], route: Global, menu_id: 507 };
	ClearWorkArea { cpp: "clearworkarea", i18n: "menu.sequence.clear_workarea", keys: [], route: Global, menu_id: 508 };
	RemoveMarker { cpp: "removemarker", i18n: "menu.sequence.remove_marker", keys: [], route: Global, menu_id: 506 };
	SeqCache { cpp: "seqcache", i18n: "menu.sequence.cache", keys: [], route: Global, menu_id: 1060 };
	SeqCacheInOut { cpp: "seqcacheinout", i18n: "menu.sequence.cache_in_out", keys: [], route: Global, menu_id: 1061 };
	SeqCacheClear { cpp: "seqcacheclear", i18n: "menu.sequence.cache_clear", keys: [], route: Global, menu_id: 1062 };
	SequenceSettings { cpp: "seqsettings", i18n: "menu.sequence.settings", keys: [], route: Global, menu_id: 704 };

	// --- Window -------------------------------------------------------------
	FocusProject { cpp: "focusproject", i18n: "menu.window.project", keys: [], route: Global, menu_id: 601 };
	FocusSourceViewer { cpp: "focussourceviewer", i18n: "menu.window.source_viewer", keys: [], route: Global, menu_id: 602 };
	FocusProgramViewer { cpp: "focusprogramviewer", i18n: "menu.window.program_viewer", keys: [], route: Global, menu_id: 603 };
	FocusNodeEditor { cpp: "focusnodeeditor", i18n: "menu.window.node_editor", keys: [], route: Global, menu_id: 604 };
	FocusInspector { cpp: "focusinspector", i18n: "menu.window.inspector", keys: [], route: Global, menu_id: 605 };
	FocusHistory { cpp: "focushistory", i18n: "menu.window.history", keys: [], route: Global, menu_id: 606 };
	FocusTimeline { cpp: "focustimeline", i18n: "menu.window.timeline", keys: [], route: Global, menu_id: 607 };
	FocusEffectLibrary { cpp: "focuseffectlibrary", i18n: "menu.window.effect_library", keys: [], route: Global, menu_id: 608 };
	MaximizePanel { cpp: "maximizepanel", i18n: "menu.window.maximize_panel", keys: ["`"], route: Global, menu_id: 1070 };
	ResetDefaultLayout { cpp: "resetdefaultlayout", i18n: "menu.window.reset_layout", keys: [], route: Global, menu_id: 1071 };

	// --- Tools (the mutually exclusive tool group + snapping + proxy) -------
	PointerTool { cpp: "pointertool", i18n: "menu.tools.pointer", keys: ["v"], route: Global, menu_id: 1080 };
	TrackSelectTool { cpp: "trackselecttool", i18n: "menu.tools.track_select", keys: ["d"], route: Global, menu_id: 1081 };
	EditTool { cpp: "edittool", i18n: "menu.tools.edit", keys: ["x"], route: Global, menu_id: 1082 };
	RippleTool { cpp: "rippletool", i18n: "menu.tools.ripple", keys: ["b"], route: Global, menu_id: 1083 };
	RollingTool { cpp: "rollingtool", i18n: "menu.tools.rolling", keys: ["n"], route: Global, menu_id: 1084 };
	RazorTool { cpp: "razortool", i18n: "menu.tools.razor_tool", keys: ["c"], route: Global, menu_id: 1085 };
	SlipTool { cpp: "sliptool", i18n: "menu.tools.slip", keys: ["y"], route: Global, menu_id: 1086 };
	SlideTool { cpp: "slidetool", i18n: "menu.tools.slide", keys: ["u"], route: Global, menu_id: 1087 };
	HandTool { cpp: "handtool", i18n: "menu.tools.hand", keys: ["h"], route: Global, menu_id: 1088 };
	ZoomTool { cpp: "zoomtool", i18n: "menu.tools.zoom_tool", keys: ["z"], route: Global, menu_id: 1089 };
	TransitionTool { cpp: "transitiontool", i18n: "menu.tools.transition", keys: ["t"], route: Global, menu_id: 1090 };
	AddTool { cpp: "addtool", i18n: "menu.tools.add", keys: ["a"], route: Global, menu_id: 1091 };
	RecordTool { cpp: "recordtool", i18n: "menu.tools.record", keys: ["r"], route: Global, menu_id: 1092 };
	AddEmpty { cpp: "add:empty", i18n: "menu.tools.addable.empty", keys: [], route: Global, menu_id: 1100 };
	AddBars { cpp: "add:bars", i18n: "menu.tools.addable.bars", keys: [], route: Global, menu_id: 1101 };
	AddShape { cpp: "add:shape", i18n: "menu.tools.addable.shape", keys: [], route: Global, menu_id: 1102 };
	AddSolid { cpp: "add:solid", i18n: "menu.tools.addable.solid", keys: [], route: Global, menu_id: 1103 };
	AddTitle { cpp: "add:title", i18n: "menu.tools.addable.title", keys: [], route: Global, menu_id: 1104 };
	AddTone { cpp: "add:tone", i18n: "menu.tools.addable.tone", keys: [], route: Global, menu_id: 1105 };
	AddSubtitle { cpp: "add:subtitle", i18n: "menu.tools.addable.subtitle", keys: [], route: Global, menu_id: 1106 };
	Snapping { cpp: "snapping", i18n: "menu.tools.snapping", keys: ["s"], route: Global, menu_id: 1110 };
	UseProxyMedia { cpp: "useproxymedia", i18n: "menu.tools.use_proxy", keys: [], route: Global, menu_id: 1111 };
	ProxySettings { cpp: "proxysettings", i18n: "menu.tools.proxy_settings", keys: [], route: Global, menu_id: 1112 };
	// The timeline clip context menu's synchronize entries (context-menu
	// only — they appear in the clip menu, not the menu bar).
	SyncBySourceTime { cpp: "syncsourcetime", i18n: "timeline.context.sync_source_time", keys: [], route: FocusedPanel, menu_id: 1130 };
	SyncByWaveform { cpp: "syncwaveform", i18n: "timeline.context.sync_waveform", keys: ["ctrl-shift-w"], route: FocusedPanel, menu_id: 1131 };
	SyncByWaveformSpeed { cpp: "syncwaveformspeed", i18n: "timeline.context.sync_waveform_speed", keys: [], route: FocusedPanel, menu_id: 1132 };
	Preferences { cpp: "prefs", i18n: "menu.view.preferences", keys: ["secondary-,"], route: Global, menu_id: 305 };

	// --- Help ---------------------------------------------------------------
	ActionSearch { cpp: "actionsearch", i18n: "menu.help.action_search", keys: ["/"], route: Global, menu_id: 1120 };
	Feedback { cpp: "feedback", i18n: "menu.help.feedback", keys: [], route: Global, menu_id: 1121 };
	About { cpp: "about", i18n: "menu.help.about", keys: [], route: Global, menu_id: 801 };
}

/// Where an action is dispatched.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Route {
	/// The app shell handles it directly.
	Global,
	/// The focused panel gets it first (through
	/// [`crate::panels::commands::PanelCommandHandler`]); the shell's global
	/// handler is the fallback when the panel does not implement it.
	FocusedPanel,
}

/// One registry entry: everything the menu bar, the keymap and the (future)
/// shortcut preferences / action search need to know about an action.
pub struct ActionEntry {
	/// The action's identity.
	pub action: ActionId,
	/// The stable C++ action id (`mainmenu.cpp` / `menushared.cpp`), kept
	/// for future shortcut-file (`id\t键序`) compatibility.
	pub cpp_id: &'static str,
	/// The menu label's i18n key (present in both language tables).
	pub i18n_key: &'static str,
	/// The default key(s) in gpui keystroke syntax; the first one is the
	/// one the menus display. Empty = unbound.
	pub default_keys: &'static [&'static str],
	/// Where the action routes (shell or focused panel).
	pub route: Route,
	/// Builds the gpui action value (for key bindings and dispatch).
	pub build: fn() -> Box<dyn Action>,
}

impl ActionEntry {
	/// The menu item id of this entry's action.
	pub const fn menu_id(&self) -> usize {
		self.action.menu_id()
	}
}

/// The registry entry bound to a menu item id, if any.
pub fn entry_for_menu_id(id: usize) -> Option<&'static ActionEntry> {
	REGISTRY.iter().find(|entry| entry.action.menu_id() == id)
}

/// The global key bindings for every registry default key (context `None`:
/// the shell dispatches them wherever the focus is, and the modal guard in
/// the shell's action listeners suppresses them while a dialog is open).
pub fn key_bindings() -> Vec<KeyBinding> {
	let mut bindings = Vec::new();
	for entry in REGISTRY {
		for key in entry.default_keys {
			let binding = KeyBinding::load(
				key,
				(entry.build)(),
				None,
				false,
				None,
				&gpui::DummyKeyboardMapper,
			)
			.unwrap_or_else(|_| panic!("invalid default key {key:?} for {}", entry.cpp_id));
			bindings.push(binding);
		}
	}
	bindings
}

/// The menu-bar label for a keystroke pattern: macOS-style glyphs (⌘⇧⌥⌃)
/// plus arrow / space glyphs, e.g. `secondary-shift-z` → `⇧⌘Z` (on the
/// other platforms the same keys render as `Ctrl+Shift+Z`). The space key
/// name is localized through i18n (`shortcut.space`).
pub fn display_shortcut(action: ActionId) -> Option<String> {
	let key = action.entry().default_keys.first()?;
	let keystroke = gpui::Keystroke::parse(key).expect("registry keys parse (tests enforce it)");

	// `secondary-` parses to `platform` on macOS and `control` elsewhere;
	// the modifier renderers below follow the same split.
	let macos = cfg!(target_os = "macos");
	let mut label = String::new();
	let mut push = |name: &str, glyph: char| {
		if macos {
			label.push(glyph);
		} else {
			label.push_str(name);
			label.push('+');
		}
	};
	if keystroke.modifiers.control {
		push("Ctrl", '⌃');
	}
	if keystroke.modifiers.alt {
		push("Alt", '⌥');
	}
	if keystroke.modifiers.shift {
		push("Shift", '⇧');
	}
	if keystroke.modifiers.platform {
		push("Win", '⌘');
	}
	match keystroke.key.as_str() {
		"left" => label.push('←'),
		"right" => label.push('→'),
		"up" => label.push('↑'),
		"down" => label.push('↓'),
		"backspace" => label.push('⌫'),
		"delete" => label.push_str("Del"),
		"space" => label.push_str(crate::i18n::tr("shortcut.space")),
		"home" => label.push_str("Home"),
		"end" => label.push_str("End"),
		other if other.len() == 1 => label.push_str(&other.to_uppercase()),
		other => {
			// Named keys (f12, tab, …): capitalize the f-number form.
			if let Some(rest) = other.strip_prefix('f') {
				label.push_str(&format!("F{rest}"));
			} else {
				label.push_str(&format!("{}{}", other[..1].to_uppercase(), &other[1..]));
			}
		}
	}
	Some(label)
}

/// The timeline's editing tools (the C++ `Tool` enum's pointer tools): the
/// Tools menu's mutually exclusive group.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Tool {
	Pointer,
	TrackSelect,
	Edit,
	Ripple,
	Rolling,
	Razor,
	Slip,
	Slide,
	Hand,
	Zoom,
	Transition,
	Add,
	Record,
}

impl Tool {
	/// The action selecting this tool.
	pub const fn action(self) -> ActionId {
		match self {
			Tool::Pointer => ActionId::PointerTool,
			Tool::TrackSelect => ActionId::TrackSelectTool,
			Tool::Edit => ActionId::EditTool,
			Tool::Ripple => ActionId::RippleTool,
			Tool::Rolling => ActionId::RollingTool,
			Tool::Razor => ActionId::RazorTool,
			Tool::Slip => ActionId::SlipTool,
			Tool::Slide => ActionId::SlideTool,
			Tool::Hand => ActionId::HandTool,
			Tool::Zoom => ActionId::ZoomTool,
			Tool::Transition => ActionId::TransitionTool,
			Tool::Add => ActionId::AddTool,
			Tool::Record => ActionId::RecordTool,
		}
	}

	/// The tool an action selects, if it is a tool action.
	pub const fn from_action(action: ActionId) -> Option<Tool> {
		match action {
			ActionId::PointerTool => Some(Tool::Pointer),
			ActionId::TrackSelectTool => Some(Tool::TrackSelect),
			ActionId::EditTool => Some(Tool::Edit),
			ActionId::RippleTool => Some(Tool::Ripple),
			ActionId::RollingTool => Some(Tool::Rolling),
			ActionId::RazorTool => Some(Tool::Razor),
			ActionId::SlipTool => Some(Tool::Slip),
			ActionId::SlideTool => Some(Tool::Slide),
			ActionId::HandTool => Some(Tool::Hand),
			ActionId::ZoomTool => Some(Tool::Zoom),
			ActionId::TransitionTool => Some(Tool::Transition),
			ActionId::AddTool => Some(Tool::Add),
			ActionId::RecordTool => Some(Tool::Record),
			_ => None,
		}
	}
}

#[cfg(test)]
mod tests {
	use super::*;

	/// Every stable id is unique (the shortcut-file format keys on it).
	#[test]
	fn registry_ids_are_unique() {
		let mut seen = std::collections::HashSet::new();
		for entry in REGISTRY {
			assert!(seen.insert(entry.cpp_id), "duplicate cpp id {}", entry.cpp_id);
		}
	}

	/// Every menu id is unique (the menu bar reports plain ids; a duplicate
	/// would make two items dispatch the same action).
	#[test]
	fn registry_menu_ids_are_unique() {
		let mut seen = std::collections::HashSet::new();
		for entry in REGISTRY {
			assert!(
				seen.insert(entry.menu_id()),
				"duplicate menu id {} ({})",
				entry.menu_id(),
				entry.cpp_id
			);
		}
	}

	/// Every default key parses as a gpui keystroke (the table is static
	/// data, so a typo would otherwise surface only as a dead shortcut at
	/// runtime — `key_bindings` would panic at startup).
	#[test]
	fn every_default_key_parses() {
		for entry in REGISTRY {
			for key in entry.default_keys {
				assert!(
					gpui::Keystroke::parse(key).is_ok(),
					"invalid keystroke {key:?} on {}",
					entry.cpp_id
				);
			}
		}
	}

	/// No two actions claim the same keystroke (a conflict would make the
	/// keymap dispatch whichever binding was registered last).
	#[test]
	fn default_keys_are_conflict_free() {
		let mut seen = std::collections::HashMap::new();
		for entry in REGISTRY {
			for key in entry.default_keys {
				let parsed = gpui::Keystroke::parse(key).unwrap();
				let canon = parsed.unparse();
				let previous = seen.insert(canon.clone(), entry.cpp_id);
				assert!(
					previous.is_none(),
					"keystroke {canon} bound to both {} and {}",
					previous.unwrap(),
					entry.cpp_id
				);
			}
		}
	}

	/// Every registry action appears somewhere in the menu tree (an action
	/// without a menu entry is unreachable with the mouse, and the registry
	/// is meant to drive the menus).
	#[test]
	fn every_action_appears_in_the_menu_tree() {
		let _guard = crate::i18n::lang_test_lock().lock().unwrap();
		crate::i18n::set_language(crate::i18n::Language::EnUs);

		fn collect(menu: &gpui_widgets::menu::Menu, out: &mut Vec<usize>) {
			for item in &menu.items {
				out.push(item.id);
				if let Some(sub) = &item.submenu {
					collect(sub, out);
				}
			}
		}
		let mut ids = Vec::new();
		for entry in crate::app::make_menus_for_test() {
			collect(&entry.menu, &mut ids);
		}
		// Context-menu-only actions (the timeline clip menu's synchronize
		// group) appear in the clip menu instead of the menu bar.
		collect(
			&crate::panels::timeline::clip_menu(
				crate::oakui::engine::SyncEligibility::default(),
				&[],
			),
			&mut ids,
		);
		for entry in REGISTRY {
			assert!(
				ids.contains(&entry.menu_id()),
				"action {} (menu id {}) has no menu item",
				entry.cpp_id,
				entry.menu_id()
			);
		}
	}

	/// Every registry i18n key exists in both language tables with a
	/// non-empty value (the menu labels come straight from `tr`).
	#[test]
	fn every_i18n_key_exists_in_both_languages() {
		for entry in REGISTRY {
			for language in [crate::i18n::Language::EnUs, crate::i18n::Language::ZhCN] {
				let _guard = crate::i18n::lang_test_lock().lock().unwrap();
				crate::i18n::set_language(language);
				let value = crate::i18n::tr(entry.i18n_key);
				assert_ne!(
					value, entry.i18n_key,
					"i18n key {} ({language:?}) is missing for action {}",
					entry.i18n_key, entry.cpp_id
				);
				assert!(!value.is_empty());
			}
		}
	}

	/// The shortcut display formatter renders the documented labels for a
	/// sample of shapes (modifier stacks, arrows, named keys, punctuation).
	/// The glyph form is the macOS rendering; other platforms spell the
	/// modifiers out (covered by the parser tests instead).
	#[test]
	#[cfg(target_os = "macos")]
	fn display_shortcut_formats_labels() {
		assert_eq!(
			display_shortcut(ActionId::Redo).as_deref(),
			Some("⇧⌘Z")
		);
		assert_eq!(
			display_shortcut(ActionId::SplitAtPlayhead).as_deref(),
			Some("⌘K")
		);
		assert_eq!(
			display_shortcut(ActionId::NudgeLeft).as_deref(),
			Some("⌥←")
		);
		assert_eq!(
			display_shortcut(ActionId::FullScreen).as_deref(),
			Some("F11")
		);
		assert_eq!(
			display_shortcut(ActionId::Insert).as_deref(),
			Some(",")
		);
		assert!(display_shortcut(ActionId::About).is_none());
	}
}
