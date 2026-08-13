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
use crate::oakui::{AppEngine, Monitor};
use crate::panels::{chip, viewer_title};
use crate::panels::ids::SOURCE_VIEWER;

/// The source viewer panel.
pub struct SourceViewerPanel<E: AppEngine> {
	viewer: Entity<ViewerWidget<E::Clock>>,
	engine: Entity<E>,
	/// The last CPU frame handed to the viewer (compared by `Arc` identity so
	/// a paused playhead does not re-upload the picture every frame).
	last_cpu_frame: Option<std::sync::Arc<gpui::RenderImage>>,
}

impl<E: AppEngine> SourceViewerPanel<E> {
	/// Builds a viewer over `clock` (the source monitor's clock).
	pub fn new(
		engine: Entity<E>,
		clock: Entity<E::Clock>,
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

		Self {
			viewer,
			engine,
			last_cpu_frame: None,
		}
	}

	/// Pushes the engine's synthetic test frame into the viewer, but only when
	/// it actually changed (the engine caches one image per playhead frame).
	fn sync_frame(&mut self, cx: &mut Context<Self>) {
		let frame = self.engine.read(cx).cpu_frame(Monitor::Source, cx);
		if self
			.last_cpu_frame
			.as_ref()
			.is_none_or(|last| !std::sync::Arc::ptr_eq(last, &frame))
		{
			self.last_cpu_frame = Some(frame.clone());
			let frame = frame.clone();
			self.viewer
				.update(cx, |viewer, cx| viewer.set_cpu_frame(Some(frame), cx));
		}
	}
}

impl<E: AppEngine> Render for SourceViewerPanel<E> {
	fn render(&mut self, _window: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
		self.sync_frame(cx);

		let colors = cx.default_colors().clone();
		let format = self
			.engine
			.read(cx)
			.current_sequence()
			.map(|sequence| sequence.format)
			.unwrap_or(crate::oakui::VideoFormat::hd_1080p25());
		let media = self.engine.read(cx).source_media_name();
		let title = viewer_title("panel.source_viewer", &media);

		div()
			.size_full()
			.flex()
			.flex_col()
			.overflow_hidden()
			.child(
				div()
					.flex()
					.items_center()
					.gap_2()
					.px_2()
					.py_1()
					.border_b_1()
					.border_color(colors.border)
					.child(chip(&colors, title))
					.child(chip(
						&colors,
						format_resolution(format.width, format.height),
					))
					.child(chip(&colors, format_fps(format.rate))),
			)
			.child(
				div()
					.flex_1()
					.min_w_0()
					.min_h_0()
					.overflow_hidden()
					.child(self.viewer.clone()),
			)
	}
}

impl<E: AppEngine> EventEmitter<PanelEvent> for SourceViewerPanel<E> {}

impl<E: AppEngine> DockPanel for SourceViewerPanel<E> {
	fn panel_id(&self) -> gpui::dock::PanelId {
		SOURCE_VIEWER
	}

	fn title(&self, cx: &App) -> SharedString {
		viewer_title("panel.source_viewer", &self.engine.read(cx).source_media_name()).into()
	}

	fn tab_content(&self, cx: &App) -> AnyElement {
		div()
			.child(viewer_title(
				"panel.source_viewer",
				&self.engine.read(cx).source_media_name(),
			))
			.into_any_element()
	}
}
