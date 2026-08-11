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
//! right edge (the design's WP6 layout). A header tab row switches the body
//! between the picture and the scopes (histogram / waveform / vectorscope),
//! whose samples come from the same rendered frame the picture shows.

use gpui::colors::DefaultColors;
use gpui::dock::{DockPanel, PanelEvent};
use gpui::{
	div, prelude::*, px, AnyElement, App, ClickEvent, Context, Entity, EventEmitter, Render,
	SharedString, Window,
};
use gpui_widgets::audio_meter::AudioLevelMeter;
use gpui_widgets::scopes::{ChromaDataSource, Histogram, LumaDataSource, Vectorscope, Waveform};
use gpui_widgets::viewer::{ViewerEvent, ViewerWidget};

use crate::oakui::timecode::{format_fps, format_resolution};
use crate::oakui::{AppEngine, Monitor};
use crate::panels::chip;
use crate::panels::ids::PROGRAM_VIEWER;

/// Width of the audio level strip, per the design (26px).
const METER_WIDTH: f32 = 26.0;

/// The body tab of the program viewer: the picture or the scopes.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum ProgramViewTab {
	/// The rendered picture (the viewer widget plus the level strip).
	Picture,
	/// The scopes (histogram / waveform / vectorscope).
	Scopes,
}

/// The program monitor's scope samples, refreshed from the engine whenever
/// the displayed frame changes. The scope widgets read this entity through
/// the [`LumaDataSource`] / [`ChromaDataSource`] traits.
struct ScopeState {
	/// Per-pixel luma of the current frame (`0..=1`).
	luma: Vec<f32>,
	/// Per-pixel chroma `(Cb, Cr)` of the current frame (`0..=1`).
	chroma: Vec<(f32, f32)>,
}

impl LumaDataSource for ScopeState {
	fn luma_samples(&self) -> Vec<f32> {
		self.luma.clone()
	}
}

impl ChromaDataSource for ScopeState {
	fn chroma_samples(&self) -> Vec<(f32, f32)> {
		self.chroma.clone()
	}
}

/// The program viewer panel.
pub struct ProgramViewerPanel<E: AppEngine> {
	viewer: Entity<ViewerWidget<E::Clock>>,
	meter: Entity<AudioLevelMeter<E>>,
	engine: Entity<E>,
	/// The last CPU frame handed to the viewer (compared by `Arc` identity so
	/// a paused playhead does not re-upload the picture every frame).
	last_cpu_frame: Option<std::sync::Arc<gpui::RenderImage>>,
	/// The active body tab.
	tab: ProgramViewTab,
	/// The scope samples backing the three scope widgets.
	scope_state: Entity<ScopeState>,
	/// The histogram scope.
	histogram: Entity<Histogram<ScopeState>>,
	/// The waveform scope.
	waveform: Entity<Waveform<ScopeState>>,
	/// The vectorscope.
	vectorscope: Entity<Vectorscope<ScopeState>>,
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

		let scope_state = cx.new(|_cx| ScopeState {
			luma: Vec::new(),
			chroma: Vec::new(),
		});
		let histogram = cx.new(|cx| Histogram::new(41, scope_state.clone(), window, cx));
		let waveform = cx.new(|cx| Waveform::new(42, scope_state.clone(), window, cx));
		let vectorscope = cx.new(|cx| Vectorscope::new(43, scope_state.clone(), window, cx));

		Self {
			viewer,
			meter,
			engine,
			last_cpu_frame: None,
			tab: ProgramViewTab::Picture,
			scope_state,
			histogram,
			waveform,
			vectorscope,
		}
	}

	/// Pushes the engine's current frame into the viewer and the scopes, but
	/// only when it actually changed (the engine caches one image per
	/// playhead frame, with the scope samples analyzed in the same pass).
	fn sync_frame(&mut self, cx: &mut Context<Self>) {
		let frame = self.engine.read(cx).cpu_frame(Monitor::Program, cx);
		if self.last_cpu_frame.as_ref().is_none_or(|last| !std::sync::Arc::ptr_eq(last, &frame))
		{
			self.last_cpu_frame = Some(frame.clone());
			let scope = self.engine.read(cx).scope_data(Monitor::Program, cx);
			self.scope_state.update(cx, |state, cx| {
				state.luma = (*scope.luma).clone();
				state.chroma = (*scope.chroma).clone();
				cx.notify();
			});
			let frame = frame.clone();
			self.viewer
				.update(cx, |viewer, cx| viewer.set_cpu_frame(Some(frame), cx));
		}
	}

	/// One header tab button (picture / scopes), highlighted when active.
	fn tab_button(
		&self,
		id: &'static str,
		label: &'static str,
		tab: ProgramViewTab,
		colors: &gpui::colors::Colors,
		cx: &mut Context<Self>,
	) -> impl IntoElement {
		let active = self.tab == tab;
		div()
			.id(id)
			.px_2()
			.py_1()
			.rounded_sm()
			.border_1()
			.border_color(colors.border)
			.bg(if active { colors.selected } else { colors.container })
			.text_color(colors.text)
			.cursor_pointer()
			.child(label)
			.on_click(cx.listener(move |this, _event: &ClickEvent, _window, cx| {
				this.tab = tab;
				cx.notify();
			}))
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

		let body = match self.tab {
			ProgramViewTab::Picture => div()
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
			ProgramViewTab::Scopes => {
				let cell = |label: &'static str, scope: AnyElement| {
					div()
						.flex_1()
						.flex()
						.flex_col()
						.min_w_0()
						.child(
							div()
								.px_2()
								.py_1()
								.text_xs()
								.text_color(colors.disabled)
								.child(label),
						)
						.child(div().flex_1().min_h_0().child(scope))
				};
				div()
					.flex_1()
					.flex()
					.min_h_0()
					.child(cell(
						crate::i18n::tr("scope.histogram"),
						self.histogram.clone().into_any_element(),
					))
					.child(cell(
						crate::i18n::tr("scope.waveform"),
						self.waveform.clone().into_any_element(),
					))
					.child(cell(
						crate::i18n::tr("scope.vectorscope"),
						self.vectorscope.clone().into_any_element(),
					))
			}
		};

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
					.child(chip(&colors, format_fps(format.rate)))
					.child(self.tab_button(
						"program-tab-picture",
						crate::i18n::tr("viewer.picture"),
						ProgramViewTab::Picture,
						&colors,
						cx,
					))
					.child(self.tab_button(
						"program-tab-scopes",
						crate::i18n::tr("viewer.scopes"),
						ProgramViewTab::Scopes,
						&colors,
						cx,
					)),
			)
			.child(body)
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

#[cfg(test)]
mod tests {
	use super::*;
	use crate::oakui::MockEngine;
	use gpui::{size, TestAppContext, VisualTestContext};

	/// The scopes tab renders from the mock engine's synthetic frame without
	/// crashing, and the scope state carries that frame's samples.
	#[gpui::test]
	async fn scopes_tab_renders_from_the_current_frame(cx: &mut TestAppContext) {
		cx.update(|cx| cx.init_colors());
		let window = cx.open_window(size(px(640.0), px(360.0)), |window, cx| {
			let engine = cx.new(|cx| MockEngine::demo(cx));
			let clock = engine.read(cx).program_clock().clone();
			let meter = cx.new(|cx| AudioLevelMeter::new(30, engine.clone(), window, cx));
			ProgramViewerPanel::new(engine, clock, meter, window, cx)
		});
		cx.run_until_parked();
		let panel = window.root(cx).expect("program viewer panel root");
		let cx = VisualTestContext::from_window(window.into(), cx).into_mut();

		// Draw the picture tab once (fills the scope state from frame 0),
		// then switch to the scopes tab and draw it.
		cx.update(|window, cx| {
			window.draw(cx).clear();
			panel.update(cx, |panel, cx| {
				panel.tab = ProgramViewTab::Scopes;
				cx.notify();
			});
		});
		cx.run_until_parked();
		cx.update(|window, cx| {
			window.draw(cx).clear();
		});

		let (luma_len, chroma_len, bins, envelope) = cx.read(|app| {
			let panel = panel.read(app);
			(
				panel.scope_state.read(app).luma.len(),
				panel.scope_state.read(app).chroma.len(),
				panel.histogram.read(app).bins(app),
				panel.waveform.read(app).envelope(app),
			)
		});
		let pixels = (crate::oakui::frames::SYNTH_FRAME_WIDTH
			* crate::oakui::frames::SYNTH_FRAME_HEIGHT) as usize;
		assert_eq!(luma_len, pixels);
		assert_eq!(chroma_len, pixels);
		assert_eq!(bins.iter().sum::<u32>() as usize, pixels);
		assert_eq!(envelope.len(), 128);
	}
}
