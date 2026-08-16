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

//! The material bin panel (项目): the `ProjectExplorer` widget over the
//! engine's project data.

use gpui::colors::DefaultColors;
use gpui::dock::{DockPanel, PanelEvent};
use gpui::{
	div, px, prelude::*, AnyElement, App, Context, Entity, EventEmitter, Render, SharedString,
	Window,
};
use gpui_widgets::project_explorer::{ProjectExplorer, ProjectExplorerEvent};

use crate::oakui::AppEngine;
use crate::panels::ids::PROJECT;

/// The material bin panel.
pub struct ProjectExplorerPanel<E: AppEngine> {
	explorer: Entity<ProjectExplorer<E>>,
	engine: Entity<E>,
}

impl<E: AppEngine> ProjectExplorerPanel<E> {
	/// Builds the explorer over `engine`'s project data.
	pub fn new(engine: Entity<E>, window: &mut Window, cx: &mut Context<Self>) -> Self {
		let explorer = cx.new(|cx| ProjectExplorer::new(1, engine.clone(), window, cx));
		cx.subscribe(
			&explorer,
			|this, _explorer, event: &ProjectExplorerEvent, cx| match event {
				ProjectExplorerEvent::OpenRequested { id, .. } => {
					// Open the item in the engine's model (double-click on a
					// footage entry selects it for the source viewer).
					this.engine
						.update(cx, |engine, cx| engine.select_item(*id, cx));
				}
				ProjectExplorerEvent::FileDropRequested { paths, .. } => {
					// Drag-and-drop import: probe and add each dropped file
					// (the first failure is logged after the rest run).
					let mut first_error = None;
					for path in paths {
						if let Err(err) =
							this.engine
								.update(cx, |engine, cx| engine.import_footage(path.clone(), cx))
						{
							first_error.get_or_insert(err);
						}
					}
					if let Some(err) = first_error {
						println!("[project explorer] import failed: {err}");
					}
				}
				other => println!("[project explorer] request: {other:?}"),
			},
		)
		.detach();

		Self { explorer, engine }
	}
}

impl<E: AppEngine> Render for ProjectExplorerPanel<E> {
	fn render(&mut self, _window: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
		let colors = cx.default_colors().clone();
		div()
			.size_full()
			.flex()
			.flex_col()
			// The panel title row, per the design's panel headers: the
			// widget below only shows the bare tree/icon view toggles, so
			// without this row the panel reads as anonymous.
			.child(
				div()
					.flex()
					.items_center()
					.h(px(28.0))
					.flex_shrink_0()
					.px_2()
					.border_b_1()
					.border_color(colors.border)
					.bg(colors.container)
					.text_sm()
					.text_color(colors.text)
					.child(crate::i18n::tr("panel.project")),
			)
			.child(
				div()
					.flex_1()
					.min_h_0()
					.child(self.explorer.clone()),
			)
	}
}

impl<E: AppEngine> EventEmitter<PanelEvent> for ProjectExplorerPanel<E> {}

impl<E: AppEngine> DockPanel for ProjectExplorerPanel<E> {
	fn panel_id(&self) -> gpui::dock::PanelId {
		PROJECT
	}

	fn title(&self, _cx: &App) -> SharedString {
		crate::i18n::tr("panel.project").into()
	}

	fn tab_content(&self, _cx: &App) -> AnyElement {
		div()
			.child(crate::i18n::tr("panel.project"))
			.into_any_element()
	}
}
