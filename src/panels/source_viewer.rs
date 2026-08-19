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
	div, prelude::*, AnyElement, App, Context, Entity, EventEmitter, MouseButton, Render,
	SharedString, Window,
};
use gpui_widgets::viewer::{ViewerEvent, ViewerWidget};

use crate::actions::ActionId;
use crate::menus::context::{ContextMenuHandle, ContextMenuTriggered};
use crate::oakui::timecode::{format_fps, format_resolution};
use crate::oakui::{AppEngine, Monitor};
use crate::panels::commands::{self as panel_commands, PanelCommandHandler};
use crate::panels::{chip, viewer_title};
use crate::panels::ids::SOURCE_VIEWER;

/// The source viewer panel.
pub struct SourceViewerPanel<E: AppEngine> {
	viewer: Entity<ViewerWidget<E::Clock>>,
	engine: Entity<E>,
	/// The source monitor's clock (also owned by the viewer widget; kept
	/// here so transport commands can read the playing state).
	clock: Entity<E::Clock>,
	/// The last CPU frame handed to the viewer (compared by `Arc` identity so
	/// a paused playhead does not re-upload the picture every frame).
	last_cpu_frame: Option<std::sync::Arc<gpui::RenderImage>>,
	/// The right-click context menu.
	context_menu: ContextMenuHandle,
}

impl<E: AppEngine> SourceViewerPanel<E> {
	/// Builds a viewer over `clock` (the source monitor's clock).
	pub fn new(
		engine: Entity<E>,
		clock: Entity<E::Clock>,
		window: &mut Window,
		cx: &mut Context<Self>,
	) -> Self {
		let viewer = cx.new(|cx| ViewerWidget::new(2, clock.clone(), window, cx));
		// Route every transport request to the engine's source monitor.
		cx.subscribe(&viewer, |this, _viewer, event: &ViewerEvent, cx| {
			let monitor = Monitor::Source;
			this.engine.update(cx, |engine, cx| match event {
				ViewerEvent::PlayRequested { .. } => engine.play(monitor, cx),
				ViewerEvent::PauseRequested { .. } => engine.pause(monitor, cx),
				ViewerEvent::StepRequested { delta, .. } => engine.step(monitor, *delta, cx),
				// The source monitor hosts no OFX interact: the picture's
				// pointer/key events are not forwarded here (the program
				// viewer's panel forwards them when an interact is live).
				ViewerEvent::InteractPointer { .. } | ViewerEvent::InteractKey { .. } => {}
				other => println!("[source viewer] request: {other:?}"),
			});
		})
		.detach();

		let context_menu =
			ContextMenuHandle::new(Self::on_local_menu_item, window, cx);

		Self {
			viewer,
			engine,
			clock,
			last_cpu_frame: None,
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
		println!("[source viewer] context-menu item {item} (not implemented yet)");
	}

	/// Routes a transport command to the engine's source monitor through
	/// the shared viewer transport helper.
	fn transport(&mut self, action: ActionId, cx: &mut Context<Self>) -> bool {
		let engine = self.engine.clone();
		let clock = self.clock.clone();
		panel_commands::viewer_transport(&engine, &clock, Monitor::Source, action, cx)
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
			// The right-click popup renders anchored above the panel.
			.child(self.context_menu.widget())
	}
}

impl<E: AppEngine> PanelCommandHandler for SourceViewerPanel<E> {
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

impl<E: AppEngine> EventEmitter<PanelEvent> for SourceViewerPanel<E> {}

impl<E: AppEngine> EventEmitter<ContextMenuTriggered> for SourceViewerPanel<E> {}

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
