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

//! The program viewer panel (序列查看器): the `ViewerWidget` over the
//! program monitor's clock, with a 26px audio level strip attached to its
//! right edge (the design's WP6 layout).

use gpui::colors::DefaultColors;
use gpui::dock::{DockPanel, PanelEvent};
use gpui::{
	div, prelude::*, px, AnyElement, App, Context, Entity, EventEmitter, Render, SharedString,
	Window,
};
use gpui_widgets::audio_meter::AudioLevelMeter;
use gpui_widgets::viewer::{ViewerEvent, ViewerWidget};

use crate::oakui::timecode::{format_fps, format_resolution};
use crate::oakui::{AppEngine, Monitor};
use crate::panels::chip;
use crate::panels::ids::PROGRAM_VIEWER;

/// Width of the audio level strip, per the design (26px).
const METER_WIDTH: f32 = 26.0;

/// The program viewer panel.
pub struct ProgramViewerPanel<E: AppEngine> {
	viewer: Entity<ViewerWidget<E::Clock>>,
	meter: Entity<AudioLevelMeter<E>>,
	engine: Entity<E>,
	/// The last CPU frame handed to the viewer (compared by `Arc` identity so
	/// a paused playhead does not re-upload the picture every frame).
	last_cpu_frame: Option<std::sync::Arc<gpui::RenderImage>>,
}

impl<E: AppEngine> ProgramViewerPanel<E> {
	/// Builds a viewer over `clock` (the program monitor's clock) with the
	/// level meter `meter` (updated on the app's tick timer).
	pub fn new(
		engine: Entity<E>,
		clock: Entity<E::Clock>,
		meter: Entity<AudioLevelMeter<E>>,
		window: &mut Window,
		cx: &mut Context<Self>,
	) -> Self {
		let viewer = cx.new(|cx| ViewerWidget::new(3, clock, window, cx));
		// Route every transport request to the engine's program monitor.
		cx.subscribe(&viewer, |this, _viewer, event: &ViewerEvent, cx| {
			let monitor = Monitor::Program;
			this.engine.update(cx, |engine, cx| match event {
				ViewerEvent::PlayRequested { .. } => engine.play(monitor, cx),
				ViewerEvent::PauseRequested { .. } => engine.pause(monitor, cx),
				ViewerEvent::StepRequested { delta, .. } => engine.step(monitor, *delta, cx),
				other => println!("[program viewer] request: {other:?}"),
			});
		})
		.detach();

		Self {
			viewer,
			meter,
			engine,
			last_cpu_frame: None,
		}
	}

	/// Pushes the engine's synthetic test frame into the viewer, but only when
	/// it actually changed (the engine caches one image per playhead frame).
	fn sync_frame(&mut self, cx: &mut Context<Self>) {
		let frame = self.engine.read(cx).cpu_frame(Monitor::Program, cx);
		if self.last_cpu_frame.as_ref().is_none_or(|last| !std::sync::Arc::ptr_eq(last, &frame))
		{
			self.last_cpu_frame = Some(frame.clone());
			let frame = frame.clone();
			self.viewer
				.update(cx, |viewer, cx| viewer.set_cpu_frame(Some(frame), cx));
		}
	}
}

impl<E: AppEngine> Render for ProgramViewerPanel<E> {
	fn render(&mut self, _window: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
		self.sync_frame(cx);

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
					.child(chip(&colors, crate::i18n::tr("viewer.program")))
					.child(chip(
						&colors,
						format_resolution(format.width, format.height),
					))
					.child(chip(&colors, format_fps(format.rate))),
			)
			.child(
				div()
					.flex_1()
					.flex()
					.child(div().flex_1().child(self.viewer.clone()))
					.child(
						div()
							.w(px(METER_WIDTH))
							.border_l_1()
							.border_color(colors.border)
							.child(self.meter.clone()),
					),
			)
	}
}

impl<E: AppEngine> EventEmitter<PanelEvent> for ProgramViewerPanel<E> {}

impl<E: AppEngine> DockPanel for ProgramViewerPanel<E> {
	fn panel_id(&self) -> gpui::dock::PanelId {
		PROGRAM_VIEWER
	}

	fn title(&self, _cx: &App) -> SharedString {
		crate::i18n::tr("panel.program_viewer").into()
	}

	fn tab_content(&self, _cx: &App) -> AnyElement {
		div()
			.child(crate::i18n::tr("panel.program_viewer"))
			.into_any_element()
	}
}
