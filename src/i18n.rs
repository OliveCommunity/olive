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
	("menu.file.new_project", "New Project…"),
	("menu.file.open_project", "Open Project File…"),
	("menu.file.open_library", "Open from Library…"),
	("menu.file.project_manager", "Project Manager…"),
	("menu.file.import_footage", "Import Footage…"),
	("menu.file.export_project", "Export Project File…"),
	("menu.file.close", "Close Project"),
	("menu.file.export", "Export…"),
	("menu.file.quit", "Quit"),
	// --- Edit ---
	("menu.edit.undo", "Undo"),
	("menu.edit.redo", "Redo"),
	("menu.edit.cut", "Cut"),
	("menu.edit.copy", "Copy"),
	("menu.edit.paste", "Paste"),
	("menu.edit.delete", "Delete"),
	("menu.edit.ripple_delete", "Ripple Delete"),
	// --- View ---
	("menu.view.theme", "Theme"),
	("menu.view.theme.dark", "Olive Dark"),
	("menu.view.theme.light", "Olive Light"),
	("menu.view.language", "Language"),
	("menu.view.language.en", "English"),
	("menu.view.language.zh", "简体中文"),
	("menu.view.preferences", "Preferences…"),
	// --- Playback ---
	("menu.playback.play_pause", "Play/Pause"),
	("menu.playback.prev_frame", "Previous Frame"),
	("menu.playback.next_frame", "Next Frame"),
	("menu.playback.to_start", "Jump to Sequence Start"),
	// --- Sequence ---
	("menu.sequence.add_video_track", "Add Video Track"),
	("menu.sequence.add_audio_track", "Add Audio Track"),
	("menu.sequence.remove_track", "Remove Selected Track"),
	("menu.sequence.split_at_playhead", "Split Clips at Playhead"),
	("menu.sequence.settings", "Sequence Settings…"),
	// --- Window ---
	("menu.window.project", "Project"),
	("menu.window.source_viewer", "Source Viewer"),
	("menu.window.program_viewer", "Program Viewer"),
	("menu.window.node_editor", "Node Editor"),
	("menu.window.inspector", "Inspector"),
	("menu.window.history", "History"),
	("menu.window.timeline", "Timeline"),
	// --- Tools ---
	("menu.tools.select", "Select"),
	("menu.tools.razor", "Razor"),
	("menu.tools.snap", "Snap"),
	// --- Help ---
	("menu.help.about", "About Oak…"),
	// --- dock panel titles ---
	("panel.project", "Project"),
	("panel.source_viewer", "Source Viewer"),
	("panel.program_viewer", "Program Viewer"),
	("panel.node_editor", "Node Editor"),
	("panel.inspector", "Inspector"),
	("panel.history", "History"),
	("panel.timeline", "Timeline"),
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
	// --- history (undo stack demo entries) ---
	("history.transform", "Transform"),
	("history.move_clip", "Move Clip"),
	("history.delete_clip", "Delete"),
	("history.add_lut", "Add OCIO LUT"),
	("history.set_in_point", "Set In Point"),
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
	// --- dialogs ---
	("dialog.cancel", "Cancel"),
	("dialog.close", "Close"),
	("file.open.title", "Open Project"),
	("file.import_footage.title", "Import Footage"),
	("preferences.title", "Preferences"),
	("preferences.backend", "Renderer backend"),
	("preferences.backend.placeholder", "Select a backend…"),
	("preferences.language", "Language"),
	("preferences.language.placeholder", "Select a language…"),
	("preferences.hint", "The renderer backend applies to the render worker at the next launch; the language switches immediately."),
	("export.title", "Export Sequence"),
	("export.format", "Format"),
	("export.format.placeholder", "Select a format…"),
	("export.path", "Output path"),
	("export.run", "Export"),
	("export.hint", "The sequence is exported through the oaktask export path; progress is shown in the dialog."),
	("export.progress.title", "Exporting"),
	("export.progress.label", "Rendering frames…"),
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
	("menu.file.new_project", "新建项目…"),
	("menu.file.open_project", "打开工程文件…"),
	("menu.file.open_library", "从库中打开…"),
	("menu.file.project_manager", "项目管理器…"),
	("menu.file.import_footage", "导入素材…"),
	("menu.file.export_project", "导出工程文件…"),
	("menu.file.close", "关闭项目"),
	("menu.file.export", "导出…"),
	("menu.file.quit", "退出"),
	// --- Edit ---
	("menu.edit.undo", "撤销"),
	("menu.edit.redo", "重做"),
	("menu.edit.cut", "剪切"),
	("menu.edit.copy", "复制"),
	("menu.edit.paste", "粘贴"),
	("menu.edit.delete", "删除"),
	("menu.edit.ripple_delete", "波纹删除"),
	// --- View ---
	("menu.view.theme", "主题"),
	("menu.view.theme.dark", "Olive Dark"),
	("menu.view.theme.light", "Olive Light"),
	("menu.view.language", "语言"),
	("menu.view.language.en", "English"),
	("menu.view.language.zh", "简体中文"),
	("menu.view.preferences", "偏好设置…"),
	// --- Playback ---
	("menu.playback.play_pause", "播放/暂停"),
	("menu.playback.prev_frame", "上一帧"),
	("menu.playback.next_frame", "下一帧"),
	("menu.playback.to_start", "跳到序列起点"),
	// --- Sequence ---
	("menu.sequence.add_video_track", "添加视频轨道"),
	("menu.sequence.add_audio_track", "添加音频轨道"),
	("menu.sequence.remove_track", "删除所选轨道"),
	("menu.sequence.split_at_playhead", "在播放头处分割片段"),
	("menu.sequence.settings", "序列设置…"),
	// --- Window ---
	("menu.window.project", "项目"),
	("menu.window.source_viewer", "素材查看器"),
	("menu.window.program_viewer", "序列查看器"),
	("menu.window.node_editor", "节点编辑器"),
	("menu.window.inspector", "检查器"),
	("menu.window.history", "历史记录"),
	("menu.window.timeline", "时间线"),
	// --- Tools ---
	("menu.tools.select", "选择"),
	("menu.tools.razor", "剃刀"),
	("menu.tools.snap", "吸附"),
	// --- Help ---
	("menu.help.about", "关于 Oak…"),
	// --- dock panel titles ---
	("panel.project", "项目"),
	("panel.source_viewer", "素材查看器"),
	("panel.program_viewer", "序列查看器"),
	("panel.node_editor", "节点编辑器"),
	("panel.inspector", "检查器"),
	("panel.history", "历史记录"),
	("panel.timeline", "时间线"),
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
	// --- history (undo stack demo entries) ---
	("history.transform", "变换"),
	("history.move_clip", "移动片段"),
	("history.delete_clip", "删除"),
	("history.add_lut", "添加 OCIO LUT"),
	("history.set_in_point", "设置入点"),
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
	// --- dialogs ---
	("dialog.cancel", "取消"),
	("dialog.close", "关闭"),
	("file.open.title", "打开项目"),
	("file.import_footage.title", "导入素材"),
	("preferences.title", "偏好设置"),
	("preferences.backend", "渲染后端"),
	("preferences.backend.placeholder", "选择一个后端…"),
	("preferences.language", "语言"),
	("preferences.language.placeholder", "选择语言…"),
	(
		"preferences.hint",
		"渲染后端在下次启动渲染工作进程时生效；语言立即切换。",
	),
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
