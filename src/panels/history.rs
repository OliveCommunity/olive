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

//! The history panel (历史记录): a placeholder list of undo entries, sharing
//! the inspector's dock group per the design.

use gpui::colors::DefaultColors;
use gpui::dock::{DockPanel, PanelEvent};
use gpui::{div, prelude::*, AnyElement, App, Context, EventEmitter, Render, SharedString, Window};

use crate::panels::ids::HISTORY;

/// The undo-history placeholder panel.
pub struct HistoryPanel {
	/// Demo entries `(i18n key, label suffix, timestamp)`, newest first,
	/// matching the design's date format `YYYY-MM-DD HH:mm`. The label is
	/// `tr(key) + suffix`, so the verb is localized while clip names stay as
	/// data.
	entries: Vec<(&'static str, &'static str, &'static str)>,
}

impl HistoryPanel {
	/// Creates the panel with demo history entries.
	pub fn new(_window: &mut Window, _cx: &mut Context<Self>) -> Self {
		Self {
			entries: vec![
				("history.transform", "", "2026-06-03 20:25"),
				("history.move_clip", "", "2026-06-03 20:24"),
				("history.delete_clip", " B-roll.mp4", "2026-06-03 20:22"),
				("history.add_lut", "", "2026-06-03 20:20"),
				("history.set_in_point", "", "2026-06-03 20:18"),
			],
		}
	}
}

impl Render for HistoryPanel {
	fn render(&mut self, _window: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
		let colors = cx.default_colors().clone();
		let mut list = div()
			.id("history-list")
			.flex_1()
			.flex()
			.flex_col()
			.py_1()
			.overflow_y_scroll();
		for (key, suffix, timestamp) in &self.entries {
			let label = format!("{}{}", crate::i18n::tr(key), suffix);
			list = list.child(
				div()
					.flex()
					.items_center()
					.gap_2()
					.px_3()
					.py_1()
					.text_color(colors.text)
					.child(
						div()
							.flex_1()
							.min_w_0()
							.overflow_hidden()
							.whitespace_nowrap()
							.text_ellipsis()
							.child(label),
					)
					.child(
						div()
							.flex_shrink_0()
							.whitespace_nowrap()
							.text_color(colors.disabled)
							.child(*timestamp),
					),
			);
		}
		div().size_full().flex().flex_col().child(list)
	}
}

impl EventEmitter<PanelEvent> for HistoryPanel {}

impl DockPanel for HistoryPanel {
	fn panel_id(&self) -> gpui::dock::PanelId {
		HISTORY
	}

	fn title(&self, _cx: &App) -> SharedString {
		crate::i18n::tr("panel.history").into()
	}

	fn tab_content(&self, _cx: &App) -> AnyElement {
		div()
			.child(crate::i18n::tr("panel.history"))
			.into_any_element()
	}
}
