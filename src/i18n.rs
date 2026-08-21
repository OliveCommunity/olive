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

//! Localization: UI strings live in YAML language packs under
//! `assets/i18n/` (one `<lang>.yaml` per language, e.g. `en.yaml`,
//! `zh-CN.yaml`), loaded at runtime and shipped inside the app bundle.
//! Users can add or override languages by dropping extra `.yaml` files
//! into the user pack directory (`~/.oak/i18n/`) — no rebuild needed.
//!
//! # Lookup order
//!
//! 1. The user pack directory (`~/.oak/i18n/`);
//! 2. The bundled packs (next to the executable, or the app bundle's
//!    `Resources/i18n/`);
//! 3. The dev checkout's `assets/i18n/` (running from the repo);
//! 4. The compiled-in English/Chinese tables (the same YAML embedded
//!    with `include_str!`) — the last-resort fallback so a broken
//!    install still renders English.
//!
//! Later sources override earlier ones per key. `tr` falls back from
//! the active language to `en-US` and then to the key itself, so a
//! missing key can never panic — it degrades to a visible key string.
//!
//! # The language setting
//!
//! The active language code is a process-global string (an atomic-free
//! `RwLock` — any thread can read it). At startup [`init`] loads the
//! persisted value from the config key `Language`; unknown codes fall
//! back to `en-US`. [`set_language`] flips the global and persists.

use std::collections::HashMap;
use std::sync::atomic::{AtomicU8, Ordering};
use std::sync::{OnceLock, RwLock};

/// The languages shipped with the app (compiled-in packs).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Language {
	/// English (United States).
	EnUs,
	/// Simplified Chinese.
	ZhCN,
}

impl Language {
	/// The stable config/pack-file code, e.g. `"en-US"`.
	pub fn code(self) -> &'static str {
		match self {
			Language::EnUs => "en-US",
			Language::ZhCN => "zh-CN",
		}
	}

	/// Parses a stored config value into a built-in language. Empty or
	/// unknown values fall back to en-US (a custom pack's code is kept
	/// verbatim by [`set_language_code`]).
	fn from_code(code: &str) -> Self {
		let code = code.trim().to_ascii_lowercase();
		if code.starts_with("zh") {
			Language::ZhCN
		} else {
			Language::EnUs
		}
	}
}

/// The built-in (compiled-in) packs, parsed from the same YAML files the
/// bundle ships: the YAML is the single source of truth, embedded so a
/// missing/corrupt install still renders English.
const BUILTIN_PACKS: &[(&str, &str)] = &[
	("en-US", include_str!("../assets/i18n/en.yaml")),
	("zh-CN", include_str!("../assets/i18n/zh-CN.yaml")),
];

/// One language's key → value table (values are leaked once at load —
/// the tables live for the whole process).
type Table = HashMap<String, &'static str>;

/// All loaded packs: language code → table.
static TABLES: OnceLock<RwLock<HashMap<String, Table>>> = OnceLock::new();

/// The active language code (0/1 fast path for the two built-ins; a
/// custom pack's code lives in [`CURRENT_CUSTOM`]).
static CURRENT: AtomicU8 = AtomicU8::new(0);
static CURRENT_CUSTOM: RwLock<Option<String>> = RwLock::new(None);

/// The active language (the built-in view of the current code; custom
/// packs report the nearest built-in).
pub fn language() -> Language {
	if CURRENT_CUSTOM
		.read()
		.unwrap_or_else(|e| e.into_inner())
		.is_some()
	{
		return match CURRENT.load(Ordering::Relaxed) {
			1 => Language::ZhCN,
			_ => Language::EnUs,
		};
	}
	match CURRENT.load(Ordering::Relaxed) {
		1 => Language::ZhCN,
		_ => Language::EnUs,
	}
}

/// The active language code (`en-US`, `zh-CN`, or a pack's code).
pub fn language_code() -> String {
	if let Some(custom) = CURRENT_CUSTOM
		.read()
		.unwrap_or_else(|e| e.into_inner())
		.clone()
	{
		return custom;
	}
	language().code().to_string()
}

/// Switches the active language live (built-in) and persists the choice.
pub fn set_language(language: Language) {
	CURRENT.store(
		match language {
			Language::EnUs => 0,
			Language::ZhCN => 1,
		},
		Ordering::Relaxed,
	);
	CURRENT_CUSTOM
		.write()
		.unwrap_or_else(|e| e.into_inner())
		.take();
	persist_language(language.code());
	sync_widgets();
}

/// Switches the active language to any loaded pack's code (a custom
/// language pack). Unknown codes fall back to en-US.
pub fn set_language_code(code: &str) {
	if code == Language::EnUs.code() || code == Language::ZhCN.code() {
		set_language(Language::from_code(code));
		return;
	}
	let exists = tables()
		.read()
		.unwrap_or_else(|e| e.into_inner())
		.contains_key(code);
	if !exists {
		set_language(Language::EnUs);
		return;
	}
	*CURRENT_CUSTOM
		.write()
		.unwrap_or_else(|e| e.into_inner()) = Some(code.to_string());
	persist_language(code);
	sync_widgets();
}

/// The language codes with a loaded pack, built-ins first.
pub fn available_languages() -> Vec<String> {
	let tables = tables().read().unwrap_or_else(|e| e.into_inner());
	let mut codes = vec![Language::EnUs.code().to_string(), Language::ZhCN.code().to_string()];
	let mut extra: Vec<String> = tables
		.keys()
		.filter(|code| !codes.contains(code))
		.cloned()
		.collect();
	extra.sort();
	codes.extend(extra);
	codes
}

/// Loads the persisted language from the config. Called once at startup.
/// Never fails: a missing key keeps the default (en-US).
pub fn init() {
	let code = crate::oakui::real::config_get_string("Language");
	if code.is_empty() {
		sync_widgets();
		return;
	}
	set_language_code(code.trim());
}

/// Writes the code back to the config `Language` key.
fn persist_language(code: &str) {
	crate::oakui::real::config_set_string("Language", code);
}

/// Translates `key` in the active language. Never panics: unknown keys
/// fall back to en-US and then to the key itself.
pub fn tr(key: &'static str) -> &'static str {
	let tables = tables().read().unwrap_or_else(|e| e.into_inner());
	let current = language_code();
	if let Some(value) = tables.get(&current).and_then(|t| t.get(key)) {
		return value;
	}
	if let Some(value) = tables.get(Language::EnUs.code()).and_then(|t| t.get(key)) {
		return value;
	}
	key
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
/// `gpui_widgets::i18n` string-table hook.
pub fn sync_widgets() {
	let mut table = gpui_widgets::i18n::StringTable::new();
	for key in WIDGET_KEYS {
		table.insert((*key).to_string(), tr(key).to_string());
	}
	gpui_widgets::i18n::set_table(table);
}

// ---------------------------------------------------------------------------
// Pack loading
// ---------------------------------------------------------------------------

/// The loaded packs (lazily parsed on first use).
fn tables() -> &'static RwLock<HashMap<String, Table>> {
	TABLES.get_or_init(|| RwLock::new(load_packs()))
}

/// The directories searched for packs, lowest precedence first: the
/// compiled-in tables are the base; each directory's packs override the
/// previous source per key.
fn pack_dirs() -> Vec<std::path::PathBuf> {
	let mut dirs = Vec::new();
	// Dev checkout (running from the repo root or a subdir).
	dirs.push(std::path::PathBuf::from("assets/i18n"));
	if let Ok(exe) = std::env::current_exe() {
		if let Some(dir) = exe.parent() {
			// Portable layout: <exe>/i18n.
			dirs.push(dir.join("i18n"));
			// macOS bundle: <exe>/../Resources/i18n.
			if let Some(contents) = dir.parent() {
				dirs.push(contents.join("Resources/i18n"));
			}
		}
	}
	// User language packs (add or override languages without a rebuild).
	if let Some(home) = std::env::var_os("HOME") {
		dirs.push(std::path::Path::new(&home).join(".oak/i18n"));
	}
	dirs
}

/// Parses one YAML pack into a table (duplicate keys are an error).
fn parse_pack(code: &str, source: &str) -> Table {
	let mut table = Table::new();
	let parsed: HashMap<String, String> = match serde_yaml::from_str(source) {
		Ok(map) => map,
		Err(err) => {
			eprintln!("[i18n] failed to parse the {code} pack: {err}");
			return table;
		}
	};
	for (key, value) in parsed {
		let leaked: &'static str = Box::leak(value.into_boxed_str());
		table.insert(key, leaked);
	}
	table
}

/// Loads the built-in packs and every pack file found in [`pack_dirs`]
/// (later sources override earlier ones per key; a file named
/// `<code>.yaml` registers or extends language `<code>`).
fn load_packs() -> HashMap<String, Table> {
	let mut packs: HashMap<String, Table> = HashMap::new();
	for (code, source) in BUILTIN_PACKS {
		packs.insert(code.to_string(), parse_pack(code, source));
	}
	for dir in pack_dirs() {
		let Ok(read_dir) = std::fs::read_dir(&dir) else {
			continue;
		};
		let mut files: Vec<_> = read_dir
			.filter_map(|entry| entry.ok())
			.map(|entry| entry.path())
			.filter(|path| {
				path.extension().and_then(|e| e.to_str()) == Some("yaml")
					|| path.extension().and_then(|e| e.to_str()) == Some("yml")
			})
			.collect();
		files.sort();
		for path in files {
			let Some(code) = path.file_stem().and_then(|s| s.to_str()) else {
				continue;
			};
			let Ok(source) = std::fs::read_to_string(&path) else {
				continue;
			};
			let table = parse_pack(code, &source);
			packs
				.entry(code.to_string())
				.or_default()
				.extend(table);
		}
	}
	packs
}

/// Reloads every pack (tests).
#[cfg(test)]
fn reload_for_test() {
	let mut tables = tables().write().unwrap_or_else(|e| e.into_inner());
	*tables = load_packs();
}

#[cfg(test)]
mod tests {
	use super::*;

	/// The two compiled-in packs parse and cover the same key set.
	#[test]
	fn every_key_exists_in_both_languages() {
		let tables = tables().read().unwrap_or_else(|e| e.into_inner());
		let en = tables.get("en-US").expect("en-US pack");
		let zh = tables.get("zh-CN").expect("zh-CN pack");
		assert!(!en.is_empty() && !zh.is_empty(), "packs parse to non-empty tables");
		for key in en.keys() {
			assert!(zh.contains_key(key), "key {key} missing from the zh-CN pack");
		}
		for key in zh.keys() {
			assert!(en.contains_key(key), "key {key} missing from the en-US pack");
		}
	}

	/// No value is left as the raw key, and the two languages differ
	/// except for an explicit shared list (brand names, units).
	#[test]
	fn no_untranslated_values() {
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
		let tables = tables().read().unwrap_or_else(|e| e.into_inner());
		let en = tables.get("en-US").unwrap();
		let zh = tables.get("zh-CN").unwrap();
		for (key, en_value) in en {
			assert_ne!(*en_value, key.as_str(), "en-US value for {key} is the raw key");
			let zh_value = zh.get(key).unwrap();
			assert_ne!(*zh_value, key.as_str(), "zh-CN value for {key} is the raw key");
			if en_value == zh_value && !shared.contains(en_value) {
				panic!("key {key} has identical en-US and zh-CN values ({en_value:?})");
			}
		}
	}

	/// `tr` falls back from the active language to English and then to
	/// the key itself.
	#[test]
	fn tr_falls_back_to_english_then_the_key() {
		let _guard = crate::i18n::test_lock();
		set_language(Language::ZhCN);
		assert_eq!(tr("menu.file"), "文件(F)");
		set_language(Language::EnUs);
		assert_eq!(tr("menu.file"), "File(F)");
		assert_eq!(tr("no.such.key"), "no.such.key");
	}

	/// A custom pack on disk overrides built-in values and registers a
	/// new language.
	#[test]
	fn user_pack_overrides_and_extends() {
		let _guard = crate::i18n::test_lock();
		let dir = std::env::temp_dir().join(format!("oak_i18n_pack_{}", std::process::id()));
		let home = dir.join("home");
		let pack_dir = home.join(".oak/i18n");
		std::fs::create_dir_all(&pack_dir).unwrap();
		std::fs::write(pack_dir.join("pirate.yaml"), "\"menu.file\": \"Arrr\"\n").unwrap();
		std::fs::write(pack_dir.join("en-US.yaml"), "\"menu.file\": \"Yarr File\"\n").unwrap();
		unsafe { std::env::set_var("HOME", &home) };
		reload_for_test();
		assert_eq!(tr("menu.file"), "Yarr File", "the user pack overrides the built-in");
		assert!(available_languages().iter().any(|c| c == "pirate"));
		set_language_code("pirate");
		assert_eq!(tr("menu.file"), "Arrr");
		set_language_code("zh-CN");
		unsafe { std::env::remove_var("HOME") };
		reload_for_test();
		let _ = std::fs::remove_dir_all(&dir);
	}
}

/// Serializes tests that mutate the language global / HOME.
#[cfg(test)]
pub(crate) fn test_lock() -> std::sync::MutexGuard<'static, ()> {
	lang_test_lock().lock().unwrap_or_else(|e| e.into_inner())
}

/// Compatibility alias used by the app/actions test modules. Both entry
/// points MUST share the one static mutex: with two separate mutexes the
/// i18n tests and the app/actions tests do not exclude each other, and
/// the language global races (observed as a Windows-only CI failure
/// where `tr` returned the English value mid-test).
#[cfg(test)]
pub(crate) fn lang_test_lock() -> &'static std::sync::Mutex<()> {
	static LOCK: std::sync::Mutex<()> = std::sync::Mutex::new(());
	&LOCK
}
