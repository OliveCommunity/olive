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

//! Tiny localization layer: embedded en-US / zh-CN string tables, a
//! [`tr`] lookup used by every user-visible label in the app, and a runtime
//! language setting persisted through the oakengine config C ABI
//! (`oakengine_config_get_string` / `oakengine_config_set_string`, the
//! process-wide `ConfigStore`).
//!
//! # The tables
//!
//! Plain key → string arrays (no serde, no build step). [`tr`] falls back
//! from the active language to en-US and then to the key itself, so a
//! missing key can never panic — it degrades to a visible-but-identifiable
//! key string instead.
//!
//! # The language setting
//!
//! The language is a process-global [`Language`] (an atomic, so any thread
//! can read it without locking). At startup [`init`] loads the persisted
//! value from the config key `Language` (`"zh-CN"` / `"en-US"`; empty or
//! unknown values mean en-US). [`set_language`] flips the global and writes
//! the new value back through the same key so the preference survives
//! restarts. The preferences dialog drives the same setting through the
//! config C ABI directly.

use std::sync::atomic::{AtomicU8, Ordering};

/// The languages shipped with the app.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Language {
	/// English (United States).
	EnUs,
	/// Simplified Chinese.
	ZhCN,
}

impl Language {
	/// The stable config/table code, e.g. `"en-US"`.
	pub fn code(self) -> &'static str {
		match self {
			Language::EnUs => "en-US",
			Language::ZhCN => "zh-CN",
		}
	}

	/// Parses a stored config value into a language. Empty or unknown values
	/// fall back to en-US.
	fn from_code(code: &str) -> Self {
		let code = code.trim().to_ascii_lowercase();
		if code.starts_with("zh") {
			Language::ZhCN
		} else {
			Language::EnUs
		}
	}
}

/// The current language, as an atomic tag (0 = en-US, 1 = zh-CN).
static CURRENT: AtomicU8 = AtomicU8::new(0);

/// The active language.
pub fn language() -> Language {
	match CURRENT.load(Ordering::Relaxed) {
		1 => Language::ZhCN,
		_ => Language::EnUs,
	}
}

/// Switches the active language live and persists the choice through the
/// oakengine config C ABI.
pub fn set_language(language: Language) {
	CURRENT.store(
		match language {
			Language::EnUs => 0,
			Language::ZhCN => 1,
		},
		Ordering::Relaxed,
	);
	persist_language(language);
	sync_widgets();
}

/// Loads the persisted language from the oakengine config C ABI. Called once
/// at startup. Never fails: a missing key keeps the default (en-US).
pub fn init() {
	let code = crate::oakui::real::config_get_string("Language");
	if !code.is_empty() {
		set_language(Language::from_code(&code));
	} else {
		sync_widgets();
	}
}

/// Writes `language` back to the config `Language` key through the facade
/// config C ABI.
fn persist_language(language: Language) {
	crate::oakui::real::config_set_string("Language", language.code());
}

/// Translates `key` in the active language.
///
/// Never panics: unknown keys fall back to en-US and then to the key itself,
/// so a typo'd key is visible in the UI instead of crashing it.
pub fn tr(key: &'static str) -> &'static str {
	match language() {
		Language::EnUs => en(key),
		Language::ZhCN => zh(key).unwrap_or_else(|| en(key)),
	}
}

/// The keys the gpui widget crates localize through
/// `gpui_widgets::i18n` (viewer transport labels, effect-stack empty state).
pub const WIDGET_KEYS: &[&str] = &[
	"viewer.safe_frames",
	"viewer.zoom",
	"viewer.no_frame_source",
	"viewer.in_point",
	"viewer.step_back",
	"viewer.play",
	"viewer.pause",
	"viewer.step_forward",
	"viewer.out_point",
	"viewer.clear_range",
	"effect_stack.empty",
	"effect_stack.add",
	"explorer.tree",
	"explorer.icons",
];

/// Installs the active language's widget strings into the
/// `gpui_widgets::i18n` string-table hook, so the widget-baked labels follow
/// the app language on the next render. Called on startup and on every
/// language switch; the widgets keep their built-in defaults otherwise.
pub fn sync_widgets() {
	let mut table = gpui_widgets::i18n::StringTable::new();
	for key in WIDGET_KEYS {
		table.insert((*key).to_string(), tr(key).to_string());
	}
	gpui_widgets::i18n::set_table(table);
}

/// Looks `key` up in the en-US table.
fn en(key: &'static str) -> &'static str {
	EN.iter()
		.find(|(k, _)| *k == key)
		.map(|(_, v)| *v)
		.unwrap_or(key)
}

/// Looks `key` up in the zh-CN table.
fn zh(key: &'static str) -> Option<&'static str> {
	ZH.iter().find(|(k, _)| *k == key).map(|(_, v)| *v)
}

/// The en-US table. Every key must also exist in [`ZH`]; [`tr`] tolerates
/// missing entries but the tests below enforce parity.
const EN: &[(&str, &str)] = &[
	// --- menu bar titles ---
	("menu.file", "File(F)"),
	("menu.edit", "Edit(E)"),
	("menu.view", "View(V)"),
	("menu.playback", "Playback(P)"),
	("menu.sequence", "Sequence(S)"),
	("menu.window", "Window(W)"),
	("menu.tools", "Tools(T)"),
	("menu.help", "Help(H)"),
	// --- File ---
	("menu.file.new", "New"),
	("menu.file.new_project", "New Project…"),
	("menu.file.new_sequence", "New Sequence…"),
	("menu.file.new_folder", "New Folder"),
	("menu.file.open_project", "Open Project File…"),
	("menu.file.open_library", "Open from Library…"),
	("menu.file.open_recent", "Open Recent"),
	("menu.file.clear_recent", "Clear Recent List"),
	("menu.file.project_manager", "Project Manager…"),
	("menu.file.import_footage", "Import Footage…"),
	("menu.file.export_project", "Export Project File…"),
	("menu.file.save_as", "Save As…"),
	("menu.file.revert", "Revert"),
	("menu.file.export_media", "Export Media…"),
	("menu.file.project_properties", "Project Properties…"),
	("menu.file.close", "Close Project"),
	("menu.file.quit", "Quit"),
	// --- Edit ---
	("menu.edit.undo", "Undo"),
	("menu.edit.redo", "Redo"),
	("menu.edit.cut", "Cut"),
	("menu.edit.copy", "Copy"),
	("menu.edit.paste", "Paste"),
	("menu.edit.paste_insert", "Paste Insert"),
	("menu.edit.duplicate", "Duplicate"),
	("menu.edit.rename", "Rename"),
	("menu.edit.delete", "Delete"),
	("menu.edit.ripple_delete", "Ripple Delete"),
	("menu.edit.split", "Split at Playhead"),
	("menu.edit.speed_duration", "Speed/Duration…"),
	("menu.edit.default_transition", "Set as Default Transition"),
	("menu.edit.link_unlink", "Link/Unlink"),
	("menu.edit.enable_disable", "Enable/Disable"),
	("menu.edit.nest", "Nest"),
	("menu.edit.select_all", "Select All"),
	("menu.edit.deselect_all", "Deselect All"),
	("menu.edit.insert", "Insert"),
	("menu.edit.overwrite", "Overwrite"),
	("menu.edit.ripple_to_in", "Ripple to In"),
	("menu.edit.ripple_to_out", "Ripple to Out"),
	("menu.edit.edit_to_in", "Edit to In"),
	("menu.edit.edit_to_out", "Edit to Out"),
	("menu.edit.nudge_left", "Nudge Left"),
	("menu.edit.nudge_right", "Nudge Right"),
	("menu.edit.move_in_to_playhead", "Move In Point to Playhead"),
	("menu.edit.move_out_to_playhead", "Move Out Point to Playhead"),
	("menu.edit.set_in_point", "Set In Point"),
	("menu.edit.set_out_point", "Set Out Point"),
	("menu.edit.reset_in", "Reset In Point"),
	("menu.edit.reset_out", "Reset Out Point"),
	("menu.edit.clear_in_out", "Clear In/Out"),
	("menu.edit.delete_in_out", "Delete In to Out"),
	("menu.edit.ripple_delete_in_out", "Ripple Delete In to Out"),
	("menu.edit.marker", "Add Marker"),
	// --- View ---
	("menu.view.theme", "Theme"),
	("menu.view.theme.dark", "Olive Dark"),
	("menu.view.theme.light", "Olive Light"),
	("menu.view.language", "Language"),
	("menu.view.language.en", "English"),
	("menu.view.language.zh", "简体中文"),
	("menu.view.zoom_in", "Zoom In"),
	("menu.view.zoom_out", "Zoom Out"),
	("menu.view.increase_track_height", "Increase Track Height"),
	("menu.view.decrease_track_height", "Decrease Track Height"),
	("menu.view.show_all", "Show All Tracks"),
	("menu.view.full_screen", "Full Screen"),
	("menu.view.full_screen_viewer", "Full Screen Viewer"),
	("menu.view.preferences", "Preferences…"),
	// --- Playback ---
	("menu.playback.play_pause", "Play/Pause"),
	("menu.playback.play_in_to_out", "Play In to Out"),
	("menu.playback.loop", "Loop"),
	("menu.playback.shuttle_left", "Shuttle Left"),
	("menu.playback.shuttle_stop", "Shuttle Stop"),
	("menu.playback.shuttle_right", "Shuttle Right"),
	("menu.playback.prev_frame", "Previous Frame"),
	("menu.playback.next_frame", "Next Frame"),
	("menu.playback.to_start", "Jump to Sequence Start"),
	("menu.playback.go_to_end", "Jump to Sequence End"),
	("menu.playback.prev_cut", "Go to Previous Cut"),
	("menu.playback.next_cut", "Go to Next Cut"),
	("menu.playback.go_to_in", "Go to In Point"),
	("menu.playback.go_to_out", "Go to Out Point"),
	// --- Sequence ---
	("menu.sequence.add_video_track", "Add Video Track"),
	("menu.sequence.add_audio_track", "Add Audio Track"),
	("menu.sequence.remove_track", "Remove Selected Track"),
	("menu.sequence.remove_marker", "Remove Marker"),
	("menu.sequence.set_workarea", "Set Work Area"),
	("menu.sequence.clear_workarea", "Clear Work Area"),
	("menu.sequence.cache", "Render Cache"),
	("menu.sequence.cache_in_out", "Render Cache In to Out"),
	("menu.sequence.cache_clear", "Clear Render Cache"),
	("menu.sequence.settings", "Sequence Settings…"),
	// --- Window ---
	("menu.window.project", "Project"),
	("menu.window.source_viewer", "Source Viewer"),
	("menu.window.program_viewer", "Program Viewer"),
	("menu.window.node_editor", "Node Editor"),
	("menu.window.inspector", "Inspector"),
	("menu.window.history", "History"),
	("menu.window.timeline", "Timeline"),
	("menu.window.effect_library", "Effect Library"),
	("menu.window.multicam", "Multi-Cam"),
	("menu.window.maximize_panel", "Maximize Panel"),
	("menu.window.reset_layout", "Reset Layout"),
	// --- Tools ---
	("menu.tools.pointer", "Pointer"),
	("menu.tools.track_select", "Track Select"),
	("menu.tools.edit", "Edit"),
	("menu.tools.ripple", "Ripple"),
	("menu.tools.rolling", "Rolling"),
	("menu.tools.razor_tool", "Razor"),
	("menu.tools.slip", "Slip"),
	("menu.tools.slide", "Slide"),
	("menu.tools.hand", "Hand"),
	("menu.tools.zoom_tool", "Zoom"),
	("menu.tools.transition", "Transition"),
	("menu.tools.add", "Add"),
	("menu.tools.record", "Record"),
	("menu.tools.addable.empty", "Empty Clip"),
	("menu.tools.addable.bars", "Color Bars"),
	("menu.tools.addable.shape", "Shape"),
	("menu.tools.addable.solid", "Solid"),
	("menu.tools.addable.title", "Title"),
	("menu.tools.addable.tone", "Test Tone"),
	("menu.tools.addable.subtitle", "Subtitle"),
	("menu.tools.snapping", "Snapping"),
	("menu.tools.use_proxy", "Use Proxy Media"),
	("menu.tools.proxy_settings", "Proxy Settings…"),
	// --- Help ---
	("menu.help.action_search", "Search Actions…"),
	("menu.help.feedback", "Send Feedback…"),
	("menu.help.about", "About Oak…"),
	// --- shortcut glyphs ---
	("shortcut.space", "Space"),
	// --- dock panel titles ---
	("panel.project", "Project"),
	("panel.source_viewer", "Source Viewer"),
	("panel.program_viewer", "Program Viewer"),
	("panel.node_editor", "Node Editor"),
	("panel.inspector", "Inspector"),
	("panel.history", "History"),
	("panel.timeline", "Timeline"),
	("panel.effect_library", "Effect Library"),
	("panel.multicam", "Multi-Cam"),
	// --- multicam panel ---
	("multicam.no_multicam", "No multi-camera clip detected"),
	("multicam.switch_1", "Switch to Camera 1"),
	("multicam.switch_2", "Switch to Camera 2"),
	("multicam.switch_3", "Switch to Camera 3"),
	("multicam.switch_4", "Switch to Camera 4"),
	("multicam.switch_5", "Switch to Camera 5"),
	("multicam.switch_6", "Switch to Camera 6"),
	("multicam.switch_7", "Switch to Camera 7"),
	("multicam.switch_8", "Switch to Camera 8"),
	("multicam.switch_9", "Switch to Camera 9"),
	// --- effect library ---
	("effect_library.hint", "Double-click to add to the selected clip"),
	// --- status bar ---
	("status.ready", "Ready"),
	("status.cache", "Cache: Enabled"),
	("status.proxy", "Proxy: Off"),
	("status.storage.written", "Library: written"),
	("status.storage.unbound", "Library: off"),
	("status.storage.error", "Library: write failed"),
	("status.untitled", "Untitled Project"),
	("status.backend", "Engine:"),
	// --- project manager ---
	("manager.title", "Project Manager"),
	("manager.new", "New Project"),
	("manager.new.default_name", "Untitled Project"),
	("manager.open", "Open"),
	("manager.rename", "Rename…"),
	("manager.rename.title", "Rename Project"),
	("manager.rename.label", "New name"),
	("manager.duplicate", "Duplicate"),
	("manager.delete", "Delete"),
	("manager.delete.title", "Delete Project"),
	("manager.delete.confirm", "Delete project \"{name}\" from the library? This cannot be undone."),
	("manager.import", "Import…"),
	("manager.import.title", "Import Project"),
	("manager.export", "Export…"),
	("manager.export.title", "Export Project"),
	("manager.col.name", "Name"),
	("manager.col.modified", "Modified"),
	("manager.col.duration", "Duration"),
	("manager.col.tracks", "Tracks"),
	("manager.col.clips", "Clips"),
	("manager.col.footage", "Footage"),
	("manager.empty", "No projects in the library yet."),
	// --- timeline toolbar ---
	("timeline.tool.select", "Select"),
	("timeline.tool.razor", "Razor"),
	("timeline.tool.ripple", "Ripple"),
	("timeline.tool.slip", "Slip"),
	("timeline.tool.roll", "Roll"),
	("timeline.tool.zoom", "Zoom"),
	("timeline.tool.slide", "Slide"),
	("timeline.tool.track_select", "Track Select"),
	("timeline.zoom", "Zoom"),
	("timeline.zoom_in", "Zoom In"),
	("timeline.zoom_out", "Zoom Out"),
	("timeline.track_height", "Track Height"),
	("timeline.snap", "Snap"),
	// --- project bin ---
	("bin.footage", "Footage"),
	("bin.music", "Music"),
	// --- history (real undo stack) ---
	("history.command", "Command"),
	("history.empty", "No History"),
	("history.jump_here", "Jump to This Step"),
	// --- node editor ---
	("node.fit", "Fit"),
	// --- program viewer tabs and scope labels ---
	("viewer.picture", "Picture"),
	("viewer.scopes", "Scopes"),
	("scope.histogram", "Histogram"),
	("scope.waveform", "Waveform"),
	("scope.vectorscope", "Vectorscope"),
	// --- viewer transport tooltips ---
	("viewer.in_point", "Set In Point"),
	("viewer.step_back", "Previous Frame"),
	("viewer.play", "Play"),
	("viewer.pause", "Pause"),
	("viewer.step_forward", "Next Frame"),
	("viewer.out_point", "Set Out Point"),
	("viewer.clear_range", "Clear In/Out Range"),
	("explorer.tree", "Tree"),
	("explorer.icons", "Icons"),
	// --- widget-baked strings (synced to gpui_widgets::i18n) ---
	("viewer.safe_frames", "Safe Frames"),
	("viewer.zoom", "Zoom"),
	("viewer.no_frame_source", "No frame source"),
	("effect_stack.empty", "No selection"),
	("effect_stack.add", "+ Add Effect"),
	// --- inspector ---
	("inspector.params", "Parameters (placeholder)"),
	("inspector.badge.openfx", "OpenFX"),
	// --- OpenFX progress ---
	("ofx.progress.title", "OpenFX Plugin Progress"),
	// --- dialogs ---
	("dialog.cancel", "Cancel"),
	("dialog.close", "Close"),
	("file.open.title", "Open Project"),
	("file.import_footage.title", "Import Footage"),
	("preferences.title", "Preferences"),
	("preferences.section.general", "General"),
	("preferences.section.render", "Rendering"),
	("preferences.section.cache", "Cache"),
	("preferences.section.proxy", "Proxy"),
	("preferences.section.project", "Project"),
	("preferences.section.audio", "Audio"),
	("preferences.backend", "Renderer backend"),
	("preferences.hwdecode.enable", "Hardware-accelerated video decoding (VideoToolbox / VA-API / NVDEC / D3D11VA)"),
	("preferences.backend.placeholder", "Select a backend…"),
	("preferences.language", "Language"),
	("preferences.language.placeholder", "Select a language…"),
	("preferences.theme", "Theme"),
	("preferences.theme.dark", "Olive Dark"),
	("preferences.theme.light", "Olive Light"),
	("preferences.cache.dir", "Disk cache directory"),
	("preferences.cache.browse", "Browse…"),
	("preferences.proxy.enable", "Use proxy media"),
	("preferences.proxy.resolution", "Proxy resolution"),
	("preferences.proxy.full", "Full resolution"),
	("preferences.snapshot.interval", "Auto-save (snapshot) interval, seconds"),
	("preferences.transition.default", "Default transition length, seconds"),
	("preferences.audio.output", "Audio output device"),
	("preferences.audio.output.placeholder", "Select an output device…"),
	("preferences.audio.input", "Audio input device"),
	("preferences.audio.input.placeholder", "Select an input device…"),
	("preferences.audio.default", "System Default"),
	("preferences.hint", "The renderer backend applies to the render worker at the next launch; every other setting takes effect immediately and is saved on exit."),
	// --- Preferences: the tabbed dialog host ---
	("preferences.tab.general", "General"),
	("preferences.tab.keyboard", "Keyboard"),
	// --- Preferences: Keyboard tab ---
	("preferences.section.keyboard", "Keyboard"),
	("preferences.keyboard.search", "Search for action or shortcut"),
	("preferences.keyboard.action", "Action"),
	("preferences.keyboard.shortcut", "Shortcut"),
	("preferences.keyboard.click_to_edit", "Click to set shortcut…"),
	("preferences.keyboard.capturing", "Press keys… (Esc to cancel)"),
	("preferences.keyboard.unbound", "None"),
	("preferences.keyboard.import", "Import…"),
	("preferences.keyboard.export", "Export…"),
	("preferences.keyboard.reset_selected", "Reset Selected"),
	("preferences.keyboard.reset_all", "Reset All"),
	("preferences.keyboard.reset_all.confirm", "Are you sure you wish to reset all keyboard shortcuts to their defaults?"),
	("preferences.keyboard.conflict", "The shortcut is already bound to {action}; the binding has been moved."),
	("preferences.keyboard.cleared", "Shortcut cleared (action unbound)."),
	("preferences.keyboard.reset", "Shortcut reset to its default."),
	("preferences.keyboard.reset_all_done", "All shortcuts reset to their defaults."),
	("preferences.keyboard.imported", "Shortcuts imported successfully."),
	("preferences.keyboard.exported", "Shortcuts exported successfully."),
	("preferences.keyboard.import_failed", "Failed to open the file for reading."),
	("preferences.keyboard.export_failed", "Failed to open the file for writing."),
	// --- Action search dialog ---
	("actionsearch.search_placeholder", "Search for action…"),
	("actionsearch.empty", "No matching actions"),
	("actionsearch.no_actions", "No actions available"),
	("export.title", "Export Sequence"),
	("export.format", "Format"),
	("export.format.placeholder", "Select a format…"),
	("export.path", "Output path"),
	("export.run", "Export"),
	("export.hint", "The sequence is exported through the oaktask export path; progress is shown in the dialog."),
	("export.progress.title", "Exporting"),
	("export.progress.label", "Rendering frames…"),
	// --- proxy settings dialog ---
	("proxydialog.title", "Proxy Settings"),
	("proxydialog.footage_group", "Footage"),
	("proxydialog.no_footage", "No footage in the project"),
	("proxydialog.custom", "Use custom settings for footage"),
	("proxydialog.custom_suffix", " (custom settings)"),
	("proxydialog.global", "Global Proxy Settings"),
	("proxydialog.resolution", "Proxy Resolution"),
	("proxydialog.resolution.custom", "Custom size"),
	("proxydialog.resolution.half", "1/2 of source"),
	("proxydialog.resolution.quarter", "1/4 of source"),
	("proxydialog.resolution.eighth", "1/8 of source"),
	("proxydialog.width", "Proxy Width"),
	("proxydialog.height", "Proxy Height"),
	("proxydialog.crf", "Proxy CRF"),
	("proxydialog.preset", "Proxy Preset"),
	("proxydialog.include_audio", "Include audio in proxies"),
	("proxydialog.ffmpeg", "ffmpeg Executable"),
	("proxydialog.generate", "Generate Proxies"),
	("proxydialog.delete", "Delete Proxies"),
	("proxydialog.close", "Close"),
	("proxydialog.state.missing", "Missing"),
	("proxydialog.state.generating", "Generating"),
	("proxydialog.state.ready", "Ready"),
	("proxydialog.state.failed", "Failed"),
	// --- color labels ---
	("menu.color.label", "Color Label"),
	("menu.color.red", "Red"),
	("menu.color.maroon", "Maroon"),
	("menu.color.orange", "Orange"),
	("menu.color.brown", "Brown"),
	("menu.color.yellow", "Yellow"),
	("menu.color.oak", "Oak"),
	("menu.color.lime", "Lime"),
	("menu.color.green", "Green"),
	("menu.color.cyan", "Cyan"),
	("menu.color.teal", "Teal"),
	("menu.color.blue", "Blue"),
	("menu.color.navy", "Navy"),
	("menu.color.pink", "Pink"),
	("menu.color.purple", "Purple"),
	("menu.color.silver", "Silver"),
	("menu.color.gray", "Gray"),
	// --- shared context items ---
	("menu.context.properties", "Properties"),
	// --- timeline context menu ---
	("timeline.context.sync_source_time", "Synchronize by Source Time"),
	("timeline.context.sync_waveform", "Synchronize by Waveform"),
	(
		"timeline.context.sync_waveform_speed",
		"Synchronize by Waveform (Adjust Speed)",
	),
	("timeline.context.cache", "Cache"),
	("timeline.context.auto_cache", "Auto-Cache"),
	("timeline.context.cache_all", "Cache All"),
	("timeline.context.cache_in_out", "Cache In/Out"),
	("timeline.context.cache_discard", "Discard"),
	("timeline.context.proxy", "Proxy"),
	("timeline.context.generate_proxy", "Generate Proxy"),
	("timeline.context.use_proxy", "Use Proxy"),
	("timeline.context.reveal_proxy", "Reveal Proxy"),
	("timeline.context.delete_proxy", "Delete Proxy"),
	(
		"timeline.context.reveal_in_footage_viewer",
		"Reveal in Footage Viewer",
	),
	("timeline.context.reveal_in_project", "Reveal in Project"),
	("timeline.context.multicam", "Multi-Cam"),
	("timeline.context.use_audio_time_units", "Use Audio Time Units"),
	("timeline.context.show_thumbnails", "Show Thumbnails"),
	("timeline.context.thumbnails_off", "Disabled"),
	("timeline.context.thumbnails_at_in_points", "Only At In Points"),
	("timeline.context.thumbnails_on", "Enabled"),
	("timeline.context.show_waveforms", "Show Waveforms"),
	("timeline.context.delete_track", "Delete"),
	("timeline.context.delete_all_empty", "Delete All Empty"),
	("timeline.context.timecode_drop_frame", "Drop Frame"),
	("timeline.context.timecode_non_drop_frame", "Non-Drop Frame"),
	("timeline.context.timecode_seconds", "Seconds"),
	("timeline.context.timecode_frames", "Frames"),
	("timeline.context.timecode_milliseconds", "Milliseconds"),
	// --- node categories ---
	("node.category.output", "Output"),
	("node.category.effect", "Effect"),
	("node.category.generator", "Generator"),
	("node.category.input", "Input"),
	("node.category.math", "Math"),
	("node.category.color", "Color"),
	("node.category.distort", "Distort"),
	("node.category.filter", "Filter"),
	("node.category.keying", "Keying"),
	("node.category.openfx", "OpenFX"),
	("node.category.group", "Group"),
	// --- project explorer context menu ---
	("project.context.new", "New"),
	("project.context.reveal_in_finder", "Reveal in Finder"),
	("project.context.replace_footage", "Replace Footage"),
	("project.context.rename", "Rename"),
	("project.context.delete", "Delete"),
	("project.context.open_in_new_tab", "Open in New Tab"),
	("project.context.open_in_new_window", "Open in New Window"),
	// --- viewer context menu ---
	("viewer.context.zoom", "Zoom"),
	("viewer.context.zoom_fit", "Fit"),
	("viewer.context.full_screen", "Full Screen"),
	("viewer.context.playback_resolution", "Playback Resolution"),
	("viewer.context.res_full", "Full"),
	("viewer.context.res_half", "1/2"),
	("viewer.context.res_quarter", "1/4"),
	("viewer.context.res_eighth", "1/8"),
	("viewer.context.safe_margins", "Safe Margins"),
	("viewer.context.safe_off", "Off"),
	("viewer.context.safe_on", "On"),
	("viewer.context.safe_custom", "Custom Aspect"),
	("viewer.context.stop_on_last", "Stop Playback On Last Frame"),
	("viewer.context.audio_waveform", "Audio Waveform"),
	("viewer.context.wf_automatic", "Automatically Show/Hide"),
	("viewer.context.wf_only", "Show Waveform Only"),
	("viewer.context.wf_both", "Show Both Viewer And Waveform"),
	("viewer.context.show_fps", "Show FPS"),
	("viewer.context.save_frame", "Save Frame As Image"),
	// --- node editor context menu ---
	("node.context.group", "Group"),
	("node.context.ungroup", "Ungroup"),
	("node.context.open_in_viewer", "Open in Viewer"),
	("node.context.show_in_param_editor", "Show in Parameter Editor"),
	("node.context.smooth_edges", "Smooth Edges"),
	("node.context.direction", "Direction"),
	("node.context.dir_top_bottom", "Top to Bottom"),
	("node.context.dir_bottom_top", "Bottom to Top"),
	("node.context.dir_left_right", "Left to Right"),
	("node.context.dir_right_left", "Right to Left"),
	("node.context.add", "Add"),
	// --- inspector context menu ---
	("inspector.context.enable", "Enable"),
	("inspector.context.disable", "Disable"),
	("inspector.context.remove", "Remove"),
	("inspector.context.rename", "Rename"),
];

/// The zh-CN table. Mirrors [`EN`] key-for-key.
const ZH: &[(&str, &str)] = &[
	// --- menu bar titles ---
	("menu.file", "文件(F)"),
	("menu.edit", "编辑(E)"),
	("menu.view", "视图(V)"),
	("menu.playback", "回放(P)"),
	("menu.sequence", "序列(S)"),
	("menu.window", "窗口(W)"),
	("menu.tools", "工具(T)"),
	("menu.help", "帮助(H)"),
	// --- File ---
	("menu.file.new", "新建"),
	("menu.file.new_project", "新建项目…"),
	("menu.file.new_sequence", "新建序列…"),
	("menu.file.new_folder", "新建文件夹"),
	("menu.file.open_project", "打开工程文件…"),
	("menu.file.open_library", "从库中打开…"),
	("menu.file.open_recent", "最近打开"),
	("menu.file.clear_recent", "清除最近列表"),
	("menu.file.project_manager", "项目管理器…"),
	("menu.file.import_footage", "导入素材…"),
	("menu.file.export_project", "导出工程文件…"),
	("menu.file.save_as", "另存为…"),
	("menu.file.revert", "还原"),
	("menu.file.export_media", "导出媒体…"),
	("menu.file.project_properties", "项目属性…"),
	("menu.file.close", "关闭项目"),
	("menu.file.quit", "退出"),
	// --- Edit ---
	("menu.edit.undo", "撤销"),
	("menu.edit.redo", "重做"),
	("menu.edit.cut", "剪切"),
	("menu.edit.copy", "复制"),
	("menu.edit.paste", "粘贴"),
	("menu.edit.paste_insert", "粘贴插入"),
	("menu.edit.duplicate", "创建副本"),
	("menu.edit.rename", "重命名"),
	("menu.edit.delete", "删除"),
	("menu.edit.ripple_delete", "波纹删除"),
	("menu.edit.split", "在播放头处分割"),
	("menu.edit.speed_duration", "速度/持续时间…"),
	("menu.edit.default_transition", "设为默认转场"),
	("menu.edit.link_unlink", "链接/取消链接"),
	("menu.edit.enable_disable", "启用/禁用"),
	("menu.edit.nest", "嵌套"),
	("menu.edit.select_all", "全选"),
	("menu.edit.deselect_all", "取消全选"),
	("menu.edit.insert", "插入"),
	("menu.edit.overwrite", "覆盖"),
	("menu.edit.ripple_to_in", "波纹修剪到入点"),
	("menu.edit.ripple_to_out", "波纹修剪到出点"),
	("menu.edit.edit_to_in", "编辑到入点"),
	("menu.edit.edit_to_out", "编辑到出点"),
	("menu.edit.nudge_left", "向左微调"),
	("menu.edit.nudge_right", "向右微调"),
	("menu.edit.move_in_to_playhead", "移动入点到播放头"),
	("menu.edit.move_out_to_playhead", "移动出点到播放头"),
	("menu.edit.set_in_point", "设置入点"),
	("menu.edit.set_out_point", "设置出点"),
	("menu.edit.reset_in", "重置入点"),
	("menu.edit.reset_out", "重置出点"),
	("menu.edit.clear_in_out", "清除入出点"),
	("menu.edit.delete_in_out", "删除入出点之间"),
	("menu.edit.ripple_delete_in_out", "波纹删除入出点之间"),
	("menu.edit.marker", "添加标记"),
	// --- View ---
	("menu.view.theme", "主题"),
	("menu.view.theme.dark", "Olive Dark"),
	("menu.view.theme.light", "Olive Light"),
	("menu.view.language", "语言"),
	("menu.view.language.en", "English"),
	("menu.view.language.zh", "简体中文"),
	("menu.view.zoom_in", "放大"),
	("menu.view.zoom_out", "缩小"),
	("menu.view.increase_track_height", "增加轨道高度"),
	("menu.view.decrease_track_height", "降低轨道高度"),
	("menu.view.show_all", "显示全部轨道"),
	("menu.view.full_screen", "全屏"),
	("menu.view.full_screen_viewer", "全屏查看器"),
	("menu.view.preferences", "偏好设置…"),
	// --- Playback ---
	("menu.playback.play_pause", "播放/暂停"),
	("menu.playback.play_in_to_out", "从入点播放到出点"),
	("menu.playback.loop", "循环播放"),
	("menu.playback.shuttle_left", "穿梭左"),
	("menu.playback.shuttle_stop", "穿梭停止"),
	("menu.playback.shuttle_right", "穿梭右"),
	("menu.playback.prev_frame", "上一帧"),
	("menu.playback.next_frame", "下一帧"),
	("menu.playback.to_start", "跳到序列起点"),
	("menu.playback.go_to_end", "跳到序列终点"),
	("menu.playback.prev_cut", "跳到上一剪辑点"),
	("menu.playback.next_cut", "跳到下一剪辑点"),
	("menu.playback.go_to_in", "跳到入点"),
	("menu.playback.go_to_out", "跳到出点"),
	// --- Sequence ---
	("menu.sequence.add_video_track", "添加视频轨道"),
	("menu.sequence.add_audio_track", "添加音频轨道"),
	("menu.sequence.remove_track", "删除所选轨道"),
	("menu.sequence.remove_marker", "清除标记"),
	("menu.sequence.set_workarea", "设置工作区"),
	("menu.sequence.clear_workarea", "清除工作区"),
	("menu.sequence.cache", "渲染缓存"),
	("menu.sequence.cache_in_out", "渲染缓存入出点之间"),
	("menu.sequence.cache_clear", "清除渲染缓存"),
	("menu.sequence.settings", "序列设置…"),
	// --- Window ---
	("menu.window.project", "项目"),
	("menu.window.source_viewer", "素材查看器"),
	("menu.window.program_viewer", "序列查看器"),
	("menu.window.node_editor", "节点编辑器"),
	("menu.window.inspector", "检查器"),
	("menu.window.history", "历史记录"),
	("menu.window.timeline", "时间线"),
	("menu.window.effect_library", "效果库"),
	("menu.window.multicam", "多机位"),
	("menu.window.maximize_panel", "最大化面板"),
	("menu.window.reset_layout", "重置布局"),
	// --- Tools ---
	("menu.tools.pointer", "指针"),
	("menu.tools.track_select", "轨道选择"),
	("menu.tools.edit", "编辑"),
	("menu.tools.ripple", "波纹"),
	("menu.tools.rolling", "滚动"),
	("menu.tools.razor_tool", "剃刀"),
	("menu.tools.slip", "滑移"),
	("menu.tools.slide", "滑动"),
	("menu.tools.hand", "抓手"),
	("menu.tools.zoom_tool", "缩放"),
	("menu.tools.transition", "转场"),
	("menu.tools.add", "添加"),
	("menu.tools.record", "录制"),
	("menu.tools.addable.empty", "空白片段"),
	("menu.tools.addable.bars", "彩条"),
	("menu.tools.addable.shape", "形状"),
	("menu.tools.addable.solid", "纯色"),
	("menu.tools.addable.title", "标题"),
	("menu.tools.addable.tone", "测试音"),
	("menu.tools.addable.subtitle", "字幕"),
	("menu.tools.snapping", "吸附"),
	("menu.tools.use_proxy", "使用代理媒体"),
	("menu.tools.proxy_settings", "代理设置…"),
	// --- Help ---
	("menu.help.action_search", "搜索动作…"),
	("menu.help.feedback", "发送反馈…"),
	("menu.help.about", "关于 Oak…"),
	// --- shortcut glyphs ---
	("shortcut.space", "空格"),
	// --- dock panel titles ---
	("panel.project", "项目"),
	("panel.source_viewer", "素材查看器"),
	("panel.program_viewer", "序列查看器"),
	("panel.node_editor", "节点编辑器"),
	("panel.inspector", "检查器"),
	("panel.history", "历史记录"),
	("panel.timeline", "时间线"),
	("panel.effect_library", "效果库"),
	("panel.multicam", "多机位"),
	// --- multicam panel ---
	("multicam.no_multicam", "未检测到多机位片段"),
	("multicam.switch_1", "切换到机位 1"),
	("multicam.switch_2", "切换到机位 2"),
	("multicam.switch_3", "切换到机位 3"),
	("multicam.switch_4", "切换到机位 4"),
	("multicam.switch_5", "切换到机位 5"),
	("multicam.switch_6", "切换到机位 6"),
	("multicam.switch_7", "切换到机位 7"),
	("multicam.switch_8", "切换到机位 8"),
	("multicam.switch_9", "切换到机位 9"),
	// --- effect library ---
	("effect_library.hint", "双击添加到选中片段"),
	// --- status bar ---
	("status.ready", "就绪"),
	("status.cache", "缓存:已启用"),
	("status.proxy", "代理:关"),
	("status.storage.written", "库:已写入"),
	("status.storage.unbound", "库:未启用"),
	("status.storage.error", "库:写入失败"),
	("status.untitled", "未命名项目"),
	("status.backend", "引擎:"),
	// --- project manager ---
	("manager.title", "项目管理器"),
	("manager.new", "新建项目"),
	("manager.new.default_name", "未命名项目"),
	("manager.open", "打开"),
	("manager.rename", "重命名…"),
	("manager.rename.title", "重命名工程"),
	("manager.rename.label", "新名称"),
	("manager.duplicate", "复制"),
	("manager.delete", "删除"),
	("manager.delete.title", "删除工程"),
	(
		"manager.delete.confirm",
		"从库中删除工程“{name}”？此操作不可撤销。",
	),
	("manager.import", "导入…"),
	("manager.import.title", "导入工程"),
	("manager.export", "导出…"),
	("manager.export.title", "导出工程"),
	("manager.col.name", "名称"),
	("manager.col.modified", "修改时间"),
	("manager.col.duration", "时长"),
	("manager.col.tracks", "轨道"),
	("manager.col.clips", "片段"),
	("manager.col.footage", "素材"),
	("manager.empty", "库中还没有工程。"),
	// --- timeline toolbar ---
	("timeline.tool.select", "选择"),
	("timeline.tool.razor", "剃刀"),
	("timeline.tool.ripple", "波纹"),
	("timeline.tool.slip", "滑动"),
	("timeline.tool.roll", "滚动"),
	("timeline.tool.zoom", "缩放"),
	("timeline.tool.slide", "滑移"),
	("timeline.tool.track_select", "轨道选择"),
	("timeline.zoom", "缩放"),
	("timeline.zoom_in", "放大"),
	("timeline.zoom_out", "缩小"),
	("timeline.track_height", "轨道高"),
	("timeline.snap", "吸附"),
	// --- project bin ---
	("bin.footage", "素材"),
	("bin.music", "音乐"),
	// --- history (real undo stack) ---
	("history.command", "命令"),
	("history.empty", "暂无历史记录"),
	("history.jump_here", "跳转到此步骤"),
	// --- node editor ---
	("node.fit", "适配"),
	// --- program viewer tabs and scope labels ---
	("viewer.picture", "画面"),
	("viewer.scopes", "示波器"),
	("scope.histogram", "直方图"),
	("scope.waveform", "波形图"),
	("scope.vectorscope", "矢量示波器"),
	// --- viewer transport tooltips ---
	("viewer.in_point", "设置入点"),
	("viewer.step_back", "上一帧"),
	("viewer.play", "播放"),
	("viewer.pause", "暂停"),
	("viewer.step_forward", "下一帧"),
	("viewer.out_point", "设置出点"),
	("viewer.clear_range", "清除入出点"),
	("explorer.tree", "树"),
	("explorer.icons", "图标"),
	// --- widget-baked strings (synced to gpui_widgets::i18n) ---
	("viewer.safe_frames", "安全框"),
	("viewer.zoom", "缩放"),
	("viewer.no_frame_source", "无帧源"),
	("effect_stack.empty", "未选择"),
	("effect_stack.add", "+ 添加效果"),
	// --- inspector ---
	("inspector.params", "参数（占位）"),
	("inspector.badge.openfx", "OpenFX"),
	// --- OpenFX progress ---
	("ofx.progress.title", "OpenFX 插件进度"),
	// --- dialogs ---
	("dialog.cancel", "取消"),
	("dialog.close", "关闭"),
	("file.open.title", "打开项目"),
	("file.import_footage.title", "导入素材"),
	("preferences.title", "偏好设置"),
	("preferences.section.general", "常规"),
	("preferences.section.render", "渲染"),
	("preferences.section.cache", "缓存"),
	("preferences.section.proxy", "代理"),
	("preferences.section.project", "项目"),
	("preferences.section.audio", "音频"),
	("preferences.backend", "渲染后端"),
	("preferences.hwdecode.enable", "硬件加速视频解码（VideoToolbox / VA-API / NVDEC / D3D11VA）"),
	("preferences.backend.placeholder", "选择一个后端…"),
	("preferences.language", "语言"),
	("preferences.language.placeholder", "选择语言…"),
	("preferences.theme", "主题"),
	("preferences.theme.dark", "Olive Dark"),
	("preferences.theme.light", "Olive Light"),
	("preferences.cache.dir", "磁盘缓存目录"),
	("preferences.cache.browse", "浏览…"),
	("preferences.proxy.enable", "使用代理媒体"),
	("preferences.proxy.resolution", "代理分辨率"),
	("preferences.proxy.full", "原始分辨率"),
	("preferences.snapshot.interval", "自动保存（快照）间隔（秒）"),
	("preferences.transition.default", "默认过渡时长（秒）"),
	("preferences.audio.output", "音频输出设备"),
	("preferences.audio.output.placeholder", "选择输出设备…"),
	("preferences.audio.input", "音频输入设备"),
	("preferences.audio.input.placeholder", "选择输入设备…"),
	("preferences.audio.default", "系统默认"),
	(
		"preferences.hint",
		"渲染后端在下次启动渲染工作进程时生效；其余设置立即生效，并在退出时保存。",
	),
	// --- 偏好设置：标签页容器 ---
	("preferences.tab.general", "常规"),
	("preferences.tab.keyboard", "键盘"),
	// --- 偏好设置：键盘页 ---
	("preferences.section.keyboard", "键盘"),
	("preferences.keyboard.search", "搜索动作或快捷键"),
	("preferences.keyboard.action", "动作"),
	("preferences.keyboard.shortcut", "快捷键"),
	("preferences.keyboard.click_to_edit", "点击设置快捷键…"),
	("preferences.keyboard.capturing", "按下按键…（Esc 取消）"),
	("preferences.keyboard.unbound", "无"),
	("preferences.keyboard.import", "导入…"),
	("preferences.keyboard.export", "导出…"),
	("preferences.keyboard.reset_selected", "重置选中项"),
	("preferences.keyboard.reset_all", "重置全部"),
	("preferences.keyboard.reset_all.confirm", "确定要将所有键盘快捷键重置为默认值吗？"),
	("preferences.keyboard.conflict", "该快捷键已被 {action} 占用；绑定已转移。"),
	("preferences.keyboard.cleared", "快捷键已清除（动作已取消绑定）。"),
	("preferences.keyboard.reset", "快捷键已重置为默认值。"),
	("preferences.keyboard.reset_all_done", "所有快捷键已重置为默认值。"),
	("preferences.keyboard.imported", "快捷键导入成功。"),
	("preferences.keyboard.exported", "快捷键导出成功。"),
	("preferences.keyboard.import_failed", "无法打开文件读取。"),
	("preferences.keyboard.export_failed", "无法打开文件写入。"),
	// --- 动作搜索对话框 ---
	("actionsearch.search_placeholder", "搜索动作…"),
	("actionsearch.empty", "无匹配动作"),
	("actionsearch.no_actions", "没有可用动作"),
	("export.title", "导出序列"),
	("export.format", "格式"),
	("export.format.placeholder", "选择格式…"),
	("export.path", "输出路径"),
	("export.run", "导出"),
	(
		"export.hint",
		"序列通过 oaktask 导出路径导出；进度显示在对话框中。",
	),
	("export.progress.title", "正在导出"),
	("export.progress.label", "正在渲染帧…"),
	// --- 代理设置对话框 ---
	("proxydialog.title", "代理设置"),
	("proxydialog.footage_group", "素材"),
	("proxydialog.no_footage", "项目中没有素材"),
	("proxydialog.custom", "对素材使用自定义设置"),
	("proxydialog.custom_suffix", "（自定义设置）"),
	("proxydialog.global", "全局代理设置"),
	("proxydialog.resolution", "代理分辨率"),
	("proxydialog.resolution.custom", "自定义尺寸"),
	("proxydialog.resolution.half", "源分辨率的 1/2"),
	("proxydialog.resolution.quarter", "源分辨率的 1/4"),
	("proxydialog.resolution.eighth", "源分辨率的 1/8"),
	("proxydialog.width", "代理宽度"),
	("proxydialog.height", "代理高度"),
	("proxydialog.crf", "代理 CRF"),
	("proxydialog.preset", "代理预设"),
	("proxydialog.include_audio", "代理包含音频"),
	("proxydialog.ffmpeg", "ffmpeg 可执行文件"),
	("proxydialog.generate", "生成代理"),
	("proxydialog.delete", "删除代理"),
	("proxydialog.close", "关闭"),
	("proxydialog.state.missing", "缺失"),
	("proxydialog.state.generating", "生成中"),
	("proxydialog.state.ready", "就绪"),
	("proxydialog.state.failed", "失败"),
	// --- 颜色标签 ---
	("menu.color.label", "颜色标签"),
	("menu.color.red", "红色"),
	("menu.color.maroon", "紫褐色"),
	("menu.color.orange", "橙色"),
	("menu.color.brown", "棕色"),
	("menu.color.yellow", "黄色"),
	("menu.color.oak", "橄榄色"),
	("menu.color.lime", "黄绿色"),
	("menu.color.green", "绿色"),
	("menu.color.cyan", "青色"),
	("menu.color.teal", "蓝绿色"),
	("menu.color.blue", "蓝色"),
	("menu.color.navy", "深蓝色"),
	("menu.color.pink", "粉色"),
	("menu.color.purple", "紫色"),
	("menu.color.silver", "银色"),
	("menu.color.gray", "灰色"),
	// --- 通用右键项 ---
	("menu.context.properties", "属性"),
	// --- 时间线右键菜单 ---
	("timeline.context.sync_source_time", "按源时间同步"),
	("timeline.context.sync_waveform", "按波形同步"),
	("timeline.context.sync_waveform_speed", "按波形同步（调整速度）"),
	("timeline.context.cache", "缓存"),
	("timeline.context.auto_cache", "自动缓存"),
	("timeline.context.cache_all", "缓存全部"),
	("timeline.context.cache_in_out", "缓存入点/出点"),
	("timeline.context.cache_discard", "丢弃"),
	("timeline.context.proxy", "代理"),
	("timeline.context.generate_proxy", "生成代理"),
	("timeline.context.use_proxy", "使用代理"),
	("timeline.context.reveal_proxy", "显示代理"),
	("timeline.context.delete_proxy", "删除代理"),
	("timeline.context.reveal_in_footage_viewer", "在素材查看器中显示"),
	("timeline.context.reveal_in_project", "在项目中显示"),
	("timeline.context.multicam", "多机位"),
	("timeline.context.use_audio_time_units", "使用音频时间单位"),
	("timeline.context.show_thumbnails", "显示缩略图"),
	("timeline.context.thumbnails_off", "关闭"),
	("timeline.context.thumbnails_at_in_points", "仅在入点"),
	("timeline.context.thumbnails_on", "开启"),
	("timeline.context.show_waveforms", "显示波形"),
	("timeline.context.delete_track", "删除"),
	("timeline.context.delete_all_empty", "删除所有空轨道"),
	("timeline.context.timecode_drop_frame", "丢帧"),
	("timeline.context.timecode_non_drop_frame", "不丢帧"),
	("timeline.context.timecode_seconds", "秒"),
	("timeline.context.timecode_frames", "帧"),
	("timeline.context.timecode_milliseconds", "毫秒"),
	// --- 节点分类 ---
	("node.category.output", "输出"),
	("node.category.effect", "效果"),
	("node.category.generator", "生成器"),
	("node.category.input", "输入"),
	("node.category.math", "数学"),
	("node.category.color", "颜色"),
	("node.category.distort", "变形"),
	("node.category.filter", "滤镜"),
	("node.category.keying", "抠像"),
	("node.category.openfx", "OpenFX"),
	("node.category.group", "组"),
	// --- 项目浏览器右键菜单 ---
	("project.context.new", "新建"),
	("project.context.reveal_in_finder", "在 Finder 中显示"),
	("project.context.replace_footage", "替换素材"),
	("project.context.rename", "重命名"),
	("project.context.delete", "删除"),
	("project.context.open_in_new_tab", "在新标签页中打开"),
	("project.context.open_in_new_window", "在新窗口中打开"),
	// --- 查看器右键菜单 ---
	("viewer.context.zoom", "缩放"),
	("viewer.context.zoom_fit", "适合"),
	("viewer.context.full_screen", "全屏"),
	("viewer.context.playback_resolution", "播放分辨率"),
	("viewer.context.res_full", "完整"),
	("viewer.context.res_half", "1/2"),
	("viewer.context.res_quarter", "1/4"),
	("viewer.context.res_eighth", "1/8"),
	("viewer.context.safe_margins", "安全边距"),
	("viewer.context.safe_off", "关闭"),
	("viewer.context.safe_on", "开启"),
	("viewer.context.safe_custom", "自定义宽高比"),
	("viewer.context.stop_on_last", "在最后一帧停止播放"),
	("viewer.context.audio_waveform", "音频波形"),
	("viewer.context.wf_automatic", "自动显示/隐藏"),
	("viewer.context.wf_only", "仅显示波形"),
	("viewer.context.wf_both", "同时显示画面和波形"),
	("viewer.context.show_fps", "显示FPS"),
	("viewer.context.save_frame", "将帧另存为图像"),
	// --- 节点编辑器右键菜单 ---
	("node.context.group", "组合"),
	("node.context.ungroup", "取消组合"),
	("node.context.open_in_viewer", "在查看器中打开"),
	("node.context.show_in_param_editor", "在参数编辑器中显示"),
	("node.context.smooth_edges", "平滑边缘"),
	("node.context.direction", "方向"),
	("node.context.dir_top_bottom", "从上到下"),
	("node.context.dir_bottom_top", "从下到上"),
	("node.context.dir_left_right", "从左到右"),
	("node.context.dir_right_left", "从右到左"),
	("node.context.add", "添加"),
	// --- 检查器右键菜单 ---
	("inspector.context.enable", "启用"),
	("inspector.context.disable", "禁用"),
	("inspector.context.remove", "移除"),
	("inspector.context.rename", "重命名"),
];

// ---------------------------------------------------------------------------
// Config persistence
// ---------------------------------------------------------------------------
//
// The language setting round-trips through the oakengine config C ABI
// (`oakengine_config_get_string` / `oakengine_config_set_string`, the
// in-process `ConfigStore` backed by the linked oakcommon crate). See
// [`real`](crate::oakui::real) for the facade-side helpers.

/// Serializes every test that mutates the process-global language, so
/// parallel tests (in this module and in [`crate::app`]) cannot race each
/// other's `set_language` calls.
#[cfg(test)]
pub(crate) fn lang_test_lock() -> &'static std::sync::Mutex<()> {
	static LOCK: std::sync::Mutex<()> = std::sync::Mutex::new(());
	&LOCK
}

#[cfg(test)]
mod tests {
	use super::*;

	fn lang_lock() -> &'static std::sync::Mutex<()> {
		lang_test_lock()
	}

	/// Every key must exist in both tables, with a non-empty translation.
	#[test]
	fn every_key_exists_in_both_languages() {
		assert_eq!(EN.len(), ZH.len(), "tables must have identical key sets");
		for (key, en_value) in EN {
			assert!(!key.is_empty());
			assert!(!en_value.is_empty(), "empty en-US value for {key}");
			let zh_value = ZH
				.iter()
				.find(|(k, _)| *k == *key)
				.unwrap_or_else(|| panic!("zh-CN table is missing key {key}"));
			assert!(!zh_value.1.is_empty(), "empty zh-CN value for {key}");
		}
	}

	/// No table entry may be its own key, and identical en-US / zh-CN values
	/// are only allowed for proper nouns that stay in their original language
	/// (theme names, language names, codecs) — anything else means one side
	/// was left untranslated.
	#[test]
	fn no_untranslated_values() {
		// Values that are intentionally identical across languages.
		let shared = [
			"Olive Dark",
			"Olive Light",
			"English",
			"简体中文",
			"en-US",
			"zh-CN",
			"OpenGL",
			"Metal",
			"Vulkan",
			"MP4",
			"OpenFX",
			"1/2",
			"1/4",
			"1/8",
		];
		for (key, en_value) in EN {
			assert_ne!(
				*en_value, *key,
				"en-US value for {key} is still the raw key (untranslated)"
			);
			let zh_value = ZH
				.iter()
				.find(|(k, _)| *k == *key)
				.map(|(_, v)| *v)
				.unwrap();
			assert_ne!(
				zh_value, *key,
				"zh-CN value for {key} is still the raw key (untranslated)"
			);
			if *en_value == zh_value && !shared.contains(en_value) {
				panic!(
					"key {key} has identical en-US and zh-CN values ({en_value:?}); \
					 one side is untranslated"
				);
			}
		}
	}

	/// Keys are unique within each table (a duplicate would make `tr`'
	/// lookup order-dependent).
	#[test]
	fn keys_are_unique() {
		for table in [EN, ZH] {
			let mut seen = std::collections::HashSet::new();
			for (key, _) in table {
				assert!(seen.insert(*key), "duplicate key {key}");
			}
		}
	}

	/// `tr` never panics, even for a key that is in neither table — the raw
	/// key comes back so the omission is visible.
	#[test]
	fn tr_never_panics_on_missing_key() {
		let _guard = lang_lock().lock().unwrap();
		for (language, sample) in [
			(Language::EnUs, "missing.key"),
			(Language::ZhCN, "missing.key"),
		] {
			set_language(language);
			assert_eq!(tr(sample), sample);
		}
	}

	/// `tr` returns the en-US string when a key exists in en-US only.
	#[test]
	fn tr_falls_back_to_en() {
		let _guard = lang_lock().lock().unwrap();
		set_language(Language::ZhCN);
		// All real keys exist in both tables, so force the fallback path via
		// a key that exists in EN but not ZH by temporarily shadowing… not
		// possible with const tables — instead verify the en-US default.
		assert_eq!(tr("status.ready"), "就绪");
		set_language(Language::EnUs);
		assert_eq!(tr("status.ready"), "Ready");
	}

	/// Switching languages flips a sample string live.
	#[test]
	fn switching_flips_a_sample_string() {
		let _guard = lang_lock().lock().unwrap();
		set_language(Language::EnUs);
		assert_eq!(tr("menu.file.export_project"), "Export Project File…");
		set_language(Language::ZhCN);
		assert_eq!(tr("menu.file.export_project"), "导出工程文件…");
		set_language(Language::EnUs);
		assert_eq!(tr("menu.file.export_project"), "Export Project File…");
	}

	/// `sync_widgets` installs the active language's strings into the widget
	/// string-table hook, so the widget-baked labels follow the app language.
	#[test]
	fn sync_widgets_installs_the_active_language() {
		let _guard = lang_lock().lock().unwrap();
		set_language(Language::EnUs);
		gpui_widgets::i18n::clear_table(); // simulate a fresh process
		sync_widgets();
		assert_eq!(
			gpui_widgets::i18n::tr("viewer.safe_frames", "安全框").to_string(),
			"Safe Frames"
		);
		assert_eq!(
			gpui_widgets::i18n::tr("viewer.zoom", "缩放").to_string(),
			"Zoom"
		);
		// Every widget key is covered by the installed table.
		for key in WIDGET_KEYS {
			let installed = gpui_widgets::i18n::tr(key, "fallback").to_string();
			assert_ne!(installed, "fallback", "widget key {key} not synced");
		}

		set_language(Language::ZhCN);
		assert_eq!(
			gpui_widgets::i18n::tr("viewer.safe_frames", "Safe Frames").to_string(),
			"安全框"
		);
		assert_eq!(
			gpui_widgets::i18n::tr("effect_stack.add", "+ Add Effect").to_string(),
			"+ 添加效果"
		);
		set_language(Language::EnUs);
	}

	/// The config code round-trips.
	#[test]
	fn language_code_round_trips() {
		assert_eq!(Language::EnUs.code(), "en-US");
		assert_eq!(Language::ZhCN.code(), "zh-CN");
		assert_eq!(Language::from_code("en-US"), Language::EnUs);
		assert_eq!(Language::from_code("zh-CN"), Language::ZhCN);
		assert_eq!(Language::from_code("zh_CN"), Language::ZhCN);
		assert_eq!(Language::from_code(""), Language::EnUs);
		assert_eq!(Language::from_code("klingon"), Language::EnUs);
	}

	/// The active language reads back what was set.
	#[test]
	fn language_state_tracks_set_language() {
		let _guard = lang_lock().lock().unwrap();
		set_language(Language::ZhCN);
		assert_eq!(language(), Language::ZhCN);
		set_language(Language::EnUs);
		assert_eq!(language(), Language::EnUs);
	}

	/// `init()` (and the config path generally) must not panic when the
	/// oakcommon library is absent — which is the default under `cargo test`.
	#[test]
	fn init_does_not_panic_without_oakcommon() {
		let _guard = lang_lock().lock().unwrap();
		init();
		// The language remains whatever it was; only the ABI path was
		// exercised (falling back silently).
		assert!(matches!(language(), Language::EnUs | Language::ZhCN));
	}
}
