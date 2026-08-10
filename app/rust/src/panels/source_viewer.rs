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

//! The source viewer panel (素材查看器): the `ViewerWidget` over the source
//! monitor's clock, with its own transport and format chips.

use gpui::colors::DefaultColors;
use gpui::dock::{DockPanel, PanelEvent};
use gpui::{
	div, prelude::*, AnyElement, App, Context, Entity, EventEmitter, Render, SharedString, Window,
};
use gpui_widgets::viewer::{ViewerEvent, ViewerWidget};

use crate::oakui::timecode::{format_fps, format_resolution};
use crate::oakui::{EngineGateway, MockClock, MockEngine, Monitor};
use crate::panels::chip;
use crate::panels::ids::SOURCE_VIEWER;

/// The source viewer panel.
pub struct SourceViewerPanel {
	viewer: Entity<ViewerWidget<MockClock>>,
	engine: Entity<MockEngine>,
}

impl SourceViewerPanel {
	/// Builds a viewer over `clock` (the source monitor's clock).
	pub fn new(
		engine: Entity<MockEngine>,
		clock: Entity<MockClock>,
		window: &mut Window,
		cx: &mut Context<Self>,
	) -> Self {
		let viewer = cx.new(|cx| ViewerWidget::new(2, clock, window, cx));
		// Route every transport request to the engine's source monitor.
		cx.subscribe(&viewer, |this, _viewer, event: &ViewerEvent, cx| {
			let monitor = Monitor::Source;
			this.engine.update(cx, |engine, cx| match event {
				ViewerEvent::PlayRequested { .. } => engine.play(monitor, cx),
				ViewerEvent::PauseRequested { .. } => engine.pause(monitor, cx),
				ViewerEvent::StepRequested { delta, .. } => engine.step(monitor, *delta, cx),
				other => println!("[source viewer] request: {other:?}"),
			});
		})
		.detach();

		Self { viewer, engine }
	}
}

impl Render for SourceViewerPanel {
	fn render(&mut self, _window: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
		let colors = cx.default_colors().clone();
		let format = self
			.engine
			.read(cx)
			.current_sequence()
			.map(|sequence| sequence.format)
			.unwrap_or(crate::oakui::VideoFormat::hd_1080p25());

		div()
			.size_full()
			.flex()
			.flex_col()
			.child(
				div()
					.flex()
					.items_center()
					.gap_2()
					.px_2()
					.py_1()
					.border_b_1()
					.border_color(colors.border)
					.child(chip(&colors, crate::i18n::tr("viewer.source")))
					.child(chip(
						&colors,
						format_resolution(format.width, format.height),
					))
					.child(chip(&colors, format_fps(format.rate))),
			)
			.child(div().flex_1().child(self.viewer.clone()))
	}
}

impl EventEmitter<PanelEvent> for SourceViewerPanel {}

impl DockPanel for SourceViewerPanel {
	fn panel_id(&self) -> gpui::dock::PanelId {
		SOURCE_VIEWER
	}

	fn title(&self, _cx: &App) -> SharedString {
		crate::i18n::tr("panel.source_viewer").into()
	}

	fn tab_content(&self, _cx: &App) -> AnyElement {
		div()
			.child(crate::i18n::tr("panel.source_viewer"))
			.into_any_element()
	}
}
