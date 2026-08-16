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

//! The keyboard shortcut table (M12 P5c): a flat keystroke → menu-action
//! map, so a key press and a menu click dispatch the SAME action id through
//! `OakApp::on_menu` (the menu bar's `with_shortcut` labels mirror the
//! `display` strings here).
//!
//! The set follows the C++ Olive layout: space toggles playback, J/K/L form
//! the shuttle (J steps back — true reverse playback is an engine transport
//! gap), I/O set the work-area in/out points at the playhead, S splits, A
//! selects all, Delete / Shift-Delete delete (gap / ripple), and the
//! platform-modifier file/edit shortcuts use `secondary` (⌘ on macOS, Ctrl
//! elsewhere).
//!
//! The table is plain data (no gpui keymap contexts): `action_for` matches
//! a [`gpui::Keystroke`] against it, and the shell's root key listener does
//! the dispatch. While a modal dialog is open the shell skips the table
//! entirely, so the dialogs' text fields never trigger editing actions.

use gpui::Keystroke;

use crate::app::menu_ids;

/// One shortcut entry: the gpui keystroke pattern (the
/// [`Keystroke::parse`] syntax), the menu action it dispatches, and the
/// label the menus show.
pub struct Shortcut {
	/// The keystroke pattern, e.g. `"secondary-z"` or `"space"`.
	pub keystroke: &'static str,
	/// The dispatched menu action id (`crate::app::menu_ids`).
	pub action: usize,
	/// The menu label for the keystroke (display only).
	pub display: &'static str,
}

/// The shortcut table, in menu order. `secondary` is the platform command
/// modifier (⌘ on macOS, Ctrl on Windows/Linux).
pub const SHORTCUTS: &[Shortcut] = &[
	// --- File ---
	Shortcut { keystroke: "secondary-n", action: menu_ids::NEW_PROJECT, display: "⌘N" },
	Shortcut { keystroke: "secondary-o", action: menu_ids::OPEN_PROJECT, display: "⌘O" },
	Shortcut { keystroke: "secondary-s", action: menu_ids::EXPORT_PROJECT, display: "⌘S" },
	Shortcut { keystroke: "secondary-e", action: menu_ids::EXPORT, display: "⌘E" },
	Shortcut { keystroke: "secondary-q", action: menu_ids::QUIT, display: "⌘Q" },
	// --- Edit ---
	Shortcut { keystroke: "secondary-z", action: menu_ids::UNDO, display: "⌘Z" },
	Shortcut { keystroke: "secondary-shift-z", action: menu_ids::REDO, display: "⇧⌘Z" },
	Shortcut { keystroke: "secondary-x", action: menu_ids::CUT, display: "⌘X" },
	Shortcut { keystroke: "secondary-c", action: menu_ids::COPY, display: "⌘C" },
	Shortcut { keystroke: "secondary-v", action: menu_ids::PASTE, display: "⌘V" },
	Shortcut { keystroke: "backspace", action: menu_ids::DELETE, display: "⌫" },
	Shortcut { keystroke: "delete", action: menu_ids::DELETE, display: "⌫" },
	Shortcut { keystroke: "shift-backspace", action: menu_ids::RIPPLE_DELETE, display: "⇧⌫" },
	Shortcut { keystroke: "shift-delete", action: menu_ids::RIPPLE_DELETE, display: "⇧⌫" },
	Shortcut { keystroke: "a", action: menu_ids::SELECT_ALL, display: "A" },
	Shortcut { keystroke: "secondary-a", action: menu_ids::SELECT_ALL, display: "⌘A" },
	// --- View ---
	// Zoom-in covers both the unshifted "=" key and the shifted "+" (gpui
	// reports the base key with the shift modifier on most layouts; the
	// bare "+" catches layouts that report the shifted character).
	Shortcut { keystroke: "=", action: menu_ids::ZOOM_IN, display: "+" },
	Shortcut { keystroke: "shift-=", action: menu_ids::ZOOM_IN, display: "+" },
	Shortcut { keystroke: "+", action: menu_ids::ZOOM_IN, display: "+" },
	Shortcut { keystroke: "-", action: menu_ids::ZOOM_OUT, display: "-" },
	Shortcut { keystroke: "secondary-,", action: menu_ids::PREFERENCES, display: "⌘," },
	// --- Playback ---
	Shortcut { keystroke: "space", action: menu_ids::PLAY_PAUSE, display: "空格" },
	Shortcut { keystroke: "left", action: menu_ids::PREV_FRAME, display: "←" },
	Shortcut { keystroke: "right", action: menu_ids::NEXT_FRAME, display: "→" },
	Shortcut { keystroke: "home", action: menu_ids::TO_START, display: "Home" },
	// The J/K/L shuttle: J steps back (true reverse playback is an engine
	// transport gap), K pauses, L plays.
	Shortcut { keystroke: "j", action: menu_ids::PREV_FRAME, display: "J" },
	Shortcut { keystroke: "k", action: menu_ids::PAUSE, display: "K" },
	Shortcut { keystroke: "l", action: menu_ids::PLAY, display: "L" },
	// --- Sequence ---
	Shortcut { keystroke: "s", action: menu_ids::SPLIT_AT_PLAYHEAD, display: "S" },
	Shortcut { keystroke: "i", action: menu_ids::SET_IN_POINT, display: "I" },
	Shortcut { keystroke: "o", action: menu_ids::SET_OUT_POINT, display: "O" },
	Shortcut { keystroke: "m", action: menu_ids::ADD_MARKER, display: "M" },
];

/// The parsed table (lazily built once; every pattern is a compile-time
/// constant, so a parse failure is a bug the tests below catch).
fn parsed() -> &'static Vec<(Keystroke, usize)> {
	static TABLE: std::sync::OnceLock<Vec<(Keystroke, usize)>> = std::sync::OnceLock::new();
	TABLE.get_or_init(|| {
		SHORTCUTS
			.iter()
			.map(|s| {
				(
					Keystroke::parse(s.keystroke)
						.unwrap_or_else(|_| panic!("invalid shortcut keystroke {:?}", s.keystroke)),
					s.action,
				)
			})
			.collect()
	})
}

/// The menu action bound to `keystroke`, if any. Matching is exact on the
/// key and the full modifier set (a shortcut with no modifiers does not
/// fire when shift is held, so shifted typing never triggers edits).
pub fn action_for(keystroke: &Keystroke) -> Option<usize> {
	parsed()
		.iter()
		.find(|(k, _)| k.key == keystroke.key && k.modifiers == keystroke.modifiers)
		.map(|(_, action)| *action)
}

/// The display label for an action's first shortcut (the menus' source).
pub fn display_for(action: usize) -> Option<&'static str> {
	SHORTCUTS
		.iter()
		.find(|s| s.action == action)
		.map(|s| s.display)
}

#[cfg(test)]
mod tests {
	use super::*;

	/// Every keystroke in the table parses (the table is static data, so a
	/// typo would otherwise only surface as a dead shortcut at runtime).
	#[test]
	fn every_shortcut_keystroke_parses() {
		for shortcut in SHORTCUTS {
			assert!(
				Keystroke::parse(shortcut.keystroke).is_ok(),
				"invalid keystroke {:?}",
				shortcut.keystroke
			);
		}
	}

	/// The main key bindings map to the documented actions (space playback,
	/// J/K/L shuttle, I/O points, S split, A select-all, delete flavors,
	/// undo/redo, the file shortcuts and the track zoom).
	#[test]
	fn main_keys_dispatch_their_actions() {
		let action = |pattern: &str| action_for(&Keystroke::parse(pattern).unwrap());

		assert_eq!(action("space"), Some(menu_ids::PLAY_PAUSE));
		assert_eq!(action("j"), Some(menu_ids::PREV_FRAME));
		assert_eq!(action("k"), Some(menu_ids::PAUSE));
		assert_eq!(action("l"), Some(menu_ids::PLAY));
		assert_eq!(action("i"), Some(menu_ids::SET_IN_POINT));
		assert_eq!(action("o"), Some(menu_ids::SET_OUT_POINT));
		assert_eq!(action("s"), Some(menu_ids::SPLIT_AT_PLAYHEAD));
		assert_eq!(action("a"), Some(menu_ids::SELECT_ALL));
		assert_eq!(action("secondary-a"), Some(menu_ids::SELECT_ALL));
		assert_eq!(action("backspace"), Some(menu_ids::DELETE));
		assert_eq!(action("shift-backspace"), Some(menu_ids::RIPPLE_DELETE));
		assert_eq!(action("secondary-z"), Some(menu_ids::UNDO));
		assert_eq!(action("secondary-shift-z"), Some(menu_ids::REDO));
		assert_eq!(action("secondary-n"), Some(menu_ids::NEW_PROJECT));
		assert_eq!(action("secondary-o"), Some(menu_ids::OPEN_PROJECT));
		assert_eq!(action("secondary-s"), Some(menu_ids::EXPORT_PROJECT));
		assert_eq!(action("="), Some(menu_ids::ZOOM_IN));
		assert_eq!(action("-"), Some(menu_ids::ZOOM_OUT));
		assert_eq!(action("m"), Some(menu_ids::ADD_MARKER));
		assert_eq!(action("left"), Some(menu_ids::PREV_FRAME));
		assert_eq!(action("right"), Some(menu_ids::NEXT_FRAME));
		assert_eq!(action("home"), Some(menu_ids::TO_START));
		assert_eq!(action("secondary-,"), Some(menu_ids::PREFERENCES));
	}

	/// A shortcut without modifiers must not fire while shift is held (so
	/// shifted keys — e.g. typing capitals — never trigger edits).
	#[test]
	fn unmodified_shortcuts_ignore_extra_modifiers() {
		let shifted = Keystroke::parse("shift-s").unwrap();
		assert_eq!(action_for(&shifted), None);
		let cmd = Keystroke::parse("secondary-i").unwrap();
		assert_eq!(action_for(&cmd), None);
	}

	/// Unbound keys map to nothing.
	#[test]
	fn unbound_keys_map_to_nothing() {
		assert_eq!(action_for(&Keystroke::parse("f1").unwrap()), None);
		assert_eq!(action_for(&Keystroke::parse("x").unwrap()), None);
	}
}
