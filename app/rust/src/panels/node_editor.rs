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

//! The node editor panel (节点编辑器): a placeholder tab sharing the program
//! viewer's dock group.
//!
//! The design puts the node editor in the center, switchable with the program
//! viewer. The real `gpui::node_graph` widget exists in the gpui submodule
//! but is not wired up yet — this panel is a placeholder surface with the
//! zoom controls the design specifies (+ / − / fit).

use gpui::colors::DefaultColors;
use gpui::dock::{DockPanel, PanelEvent};
use gpui::{
	div, prelude::*, AnyElement, App, ClickEvent, Context, EventEmitter, Render, SharedString,
	Window,
};

use crate::panels::ids::NODE_EDITOR;

/// The node editor placeholder panel.
pub struct NodeEditorPanel;

impl NodeEditorPanel {
	/// Creates the placeholder.
	pub fn new(_window: &mut Window, _cx: &mut Context<Self>) -> Self {
		Self
	}
}

impl Render for NodeEditorPanel {
	fn render(&mut self, _window: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
		let colors = cx.default_colors().clone();
		div()
			.size_full()
			.flex()
			.flex_col()
			.child(
				div()
					.flex()
					.items_center()
					.gap_1()
					.px_2()
					.py_1()
					.border_b_1()
					.border_color(colors.border)
					.child(zoom_button(
						cx,
						"node-zoom-in",
						"+",
						crate::i18n::tr("node.zoom_in"),
					))
					.child(zoom_button(
						cx,
						"node-zoom-out",
						"−",
						crate::i18n::tr("node.zoom_out"),
					))
					.child(zoom_button(
						cx,
						"node-zoom-fit",
						crate::i18n::tr("node.fit"),
						crate::i18n::tr("node.fit_window"),
					)),
			)
			.child(
				div()
					.flex_1()
					.flex()
					.items_center()
					.justify_center()
					.text_color(colors.disabled)
					.child(crate::i18n::tr("node.placeholder")),
			)
	}
}

/// A small toolbar button (the design's `+`/`−`/`适配` controls).
fn zoom_button(
	cx: &mut Context<NodeEditorPanel>,
	id: &'static str,
	label: &'static str,
	title: &'static str,
) -> impl gpui::IntoElement {
	let colors = cx.default_colors().clone();
	let container = colors.container;
	div()
		.id(id)
		.px_2()
		.py_1()
		.rounded_md()
		.border_1()
		.border_color(colors.border)
		.text_color(colors.text)
		.cursor_pointer()
		.hover(move |style| style.bg(container))
		.on_click(
			cx.listener(move |_this, _event: &ClickEvent, _window, _cx| {
				println!("[node editor] {title} (placeholder)");
			}),
		)
		.child(label)
}

impl EventEmitter<PanelEvent> for NodeEditorPanel {}

impl DockPanel for NodeEditorPanel {
	fn panel_id(&self) -> gpui::dock::PanelId {
		NODE_EDITOR
	}

	fn title(&self, _cx: &App) -> SharedString {
		crate::i18n::tr("panel.node_editor").into()
	}

	fn tab_content(&self, _cx: &App) -> AnyElement {
		div()
			.child(crate::i18n::tr("panel.node_editor"))
			.into_any_element()
	}
}
