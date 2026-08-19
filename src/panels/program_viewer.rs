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
	div, prelude::*, px, AnyElement, App, ClickEvent, Context, Entity, EventEmitter, MouseButton,
	Render, SharedString, Window,
};
use gpui_widgets::audio_meter::AudioLevelMeter;
use gpui_widgets::scopes::{ChromaDataSource, Histogram, LumaDataSource, Vectorscope, Waveform};
use gpui_widgets::viewer::{ViewerEvent, ViewerWidget};

use crate::actions::ActionId;
use crate::menus::context::{ContextMenuHandle, ContextMenuTriggered};
use crate::oakui::timecode::{format_fps, format_resolution};
use crate::oakui::{AppEngine, Monitor};
use crate::panels::commands::{self as panel_commands, PanelCommandHandler};
use crate::panels::ids::PROGRAM_VIEWER;
use crate::panels::{chip, viewer_title};

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
	/// The program monitor's clock (also owned by the viewer widget; kept
	/// here so transport commands can read the playing state).
	clock: Entity<E::Clock>,
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
	/// The right-click context menu.
	context_menu: ContextMenuHandle,
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
		let viewer = cx.new(|cx| ViewerWidget::new(3, clock.clone(), window, cx));
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

		let context_menu =
			ContextMenuHandle::new(Self::on_local_menu_item, window, cx);

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
			clock,
			last_cpu_frame: None,
			tab: ProgramViewTab::Picture,
			scope_state,
			histogram,
			waveform,
			vectorscope,
			context_menu,
		}
	}

	/// Handles the viewer's local (non-registry) context-menu items.
	fn on_local_menu_item(&mut self, item: usize, cx: &mut Context<Self>) {
		use crate::menus::shared as shared_menu;
		let divider = match item {
			shared_menu::LOCAL_VIEWER_RES_FULL => Some(1),
			shared_menu::LOCAL_VIEWER_RES_HALF => Some(2),
			shared_menu::LOCAL_VIEWER_RES_QUARTER => Some(4),
			shared_menu::LOCAL_VIEWER_RES_EIGHTH => Some(8),
			_ => None,
		};
		if let Some(divider) = divider {
			let engine = self.engine.clone();
			engine.update(cx, |engine, cx| engine.set_playback_divider(divider, cx));
			return;
		}
		println!("[program viewer] context-menu item {item} (not implemented yet)");
	}

	/// Routes a transport command to the engine's program monitor through
	/// the shared viewer transport helper.
	fn transport(&mut self, action: ActionId, cx: &mut Context<Self>) -> bool {
		let engine = self.engine.clone();
		let clock = self.clock.clone();
		panel_commands::viewer_transport(&engine, &clock, Monitor::Program, action, cx)
	}

	/// Pushes the engine's current frame into the viewer and the scopes, but
	/// only when it actually changed (the engine caches one image per
	/// playhead frame, with the scope samples analyzed in the same pass).
	fn sync_frame(&mut self, cx: &mut Context<Self>) {
		let frame = self.engine.read(cx).cpu_frame(Monitor::Program, cx);
		if self
			.last_cpu_frame
			.as_ref()
			.is_none_or(|last| !std::sync::Arc::ptr_eq(last, &frame))
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
			.bg(if active {
				colors.selected
			} else {
				colors.container
			})
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
		let sequence_name = self
			.engine
			.read(cx)
			.current_sequence()
			.map(|sequence| sequence.name.clone())
			.unwrap_or_default();
		let title = viewer_title("panel.program_viewer", &sequence_name);

		let body = match self.tab {
			ProgramViewTab::Picture => div()
				.flex_1()
				.flex()
				.min_h_0()
				.min_w_0()
				.child(
					div()
						.flex_1()
						.min_w_0()
						.min_h_0()
						.overflow_hidden()
						.child(self.viewer.clone()),
				)
				.child(
					div()
						.w(px(METER_WIDTH))
						.flex_shrink_0()
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
			.overflow_hidden()
			// Any click inside the panel makes it the focused panel (the
			// dock re-emits this as `DockEvent::PanelFocused`, which the
			// shell uses to route focused-panel commands).
			.on_mouse_down(MouseButton::Left, {
				cx.listener(|_this, _event: &gpui::MouseDownEvent, _window, cx| {
					cx.emit(PanelEvent::Focused);
				})
			})
			// The viewer widget has no right-click handling of its own, so
			// the panel opens the shared viewer menu here.
			.on_mouse_down(MouseButton::Right, {
				cx.listener(|this, event: &gpui::MouseDownEvent, _window, cx| {
					let divider = this.engine.read(cx).playback_divider();
					this.context_menu.show(
						event.position,
						crate::menus::shared::viewer_menu(divider),
						cx,
					);
				})
			})
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
			// The right-click popup renders anchored above the panel.
			.child(self.context_menu.widget())
	}
}

impl<E: AppEngine> PanelCommandHandler for ProgramViewerPanel<E> {
	fn play_pause(&mut self, cx: &mut Context<Self>) -> bool {
		self.transport(ActionId::PlayPause, cx)
	}
	fn prev_frame(&mut self, cx: &mut Context<Self>) -> bool {
		self.transport(ActionId::PrevFrame, cx)
	}
	fn next_frame(&mut self, cx: &mut Context<Self>) -> bool {
		self.transport(ActionId::NextFrame, cx)
	}
	fn go_to_start(&mut self, cx: &mut Context<Self>) -> bool {
		self.transport(ActionId::GoToStart, cx)
	}
	fn go_to_end(&mut self, cx: &mut Context<Self>) -> bool {
		self.transport(ActionId::GoToEnd, cx)
	}
	fn play_in_to_out(&mut self, cx: &mut Context<Self>) -> bool {
		self.transport(ActionId::PlayInToOut, cx)
	}
	fn go_to_in(&mut self, cx: &mut Context<Self>) -> bool {
		self.transport(ActionId::GoToIn, cx)
	}
	fn go_to_out(&mut self, cx: &mut Context<Self>) -> bool {
		self.transport(ActionId::GoToOut, cx)
	}
	fn shuttle_left(&mut self, cx: &mut Context<Self>) -> bool {
		self.transport(ActionId::ShuttleLeft, cx)
	}
	fn shuttle_stop(&mut self, cx: &mut Context<Self>) -> bool {
		self.transport(ActionId::ShuttleStop, cx)
	}
	fn shuttle_right(&mut self, cx: &mut Context<Self>) -> bool {
		self.transport(ActionId::ShuttleRight, cx)
	}
}

impl<E: AppEngine> EventEmitter<PanelEvent> for ProgramViewerPanel<E> {}

impl<E: AppEngine> EventEmitter<ContextMenuTriggered> for ProgramViewerPanel<E> {}

impl<E: AppEngine> DockPanel for ProgramViewerPanel<E> {
	fn panel_id(&self) -> gpui::dock::PanelId {
		PROGRAM_VIEWER
	}

	fn title(&self, cx: &App) -> SharedString {
		let name = self
			.engine
			.read(cx)
			.current_sequence()
			.map(|sequence| sequence.name.clone())
			.unwrap_or_default();
		viewer_title("panel.program_viewer", &name).into()
	}

	fn tab_content(&self, cx: &App) -> AnyElement {
		let name = self
			.engine
			.read(cx)
			.current_sequence()
			.map(|sequence| sequence.name.clone())
			.unwrap_or_default();
		div()
			.child(viewer_title("panel.program_viewer", &name))
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
