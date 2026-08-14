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

//! The node editor panel (节点编辑器): the real `gpui::node_graph` canvas over
//! the engine's graph (the mock's demo graph, or the real engine's current
//! sequence graph), with the design's zoom controls (+ / − / 适配).
//!
//! The graph is a full [`NodeGraphView`] fed by the engine's
//! [`NodeGraphDataSource`] implementation. Every gesture the view emits
//! (move, connect, disconnect, delete, selection) is forwarded to the engine
//! as a request; the engine applies it to its model and notifies, so the
//! view re-reads on the next frame. The toolbar buttons drive the viewport
//! directly: zoom in/out at the canvas center, or fit the whole graph.

use gpui::colors::DefaultColors;
use gpui::dock::{DockPanel, PanelEvent};
use gpui::node_graph::{
	NodeData, NodeElement, NodeGraphEvent, NodeGraphView, NodeVisualState, MAX_ZOOM, MIN_ZOOM,
};
use gpui::{
	div, point, prelude::*, px, AnyElement, App, Bounds, ClickEvent, Context, Entity, EventEmitter,
	Pixels, Render, SharedString, Window,
};

use crate::oakui::AppEngine;
use crate::panels::ids::NODE_EDITOR;

/// The node editor panel.
pub struct NodeEditorPanel<E: AppEngine> {
	/// The node-graph canvas over the engine's graph data.
	graph: Entity<NodeGraphView<E>>,
	engine: Entity<E>,
	/// Whether the initial fit-to-window has been applied (the canvas size is
	/// only known after the first layout).
	fitted: bool,
}

impl<E: AppEngine> NodeEditorPanel<E> {
	/// Builds the graph canvas over `engine` and routes its edit requests back
	/// to the engine.
	pub fn new(engine: Entity<E>, window: &mut Window, cx: &mut Context<Self>) -> Self {
		let graph = cx.new(|cx| NodeGraphView::new(engine.clone(), window, cx));
		// The "edits are requests" loop: every graph gesture goes to the
		// engine, which applies it to its model and notifies.
		cx.subscribe(&graph, |this, _graph, event: &NodeGraphEvent, cx| {
			this.engine
				.update(cx, |engine, cx| engine.apply_node_graph_event(event, cx));
		})
		.detach();

		Self {
			graph,
			engine,
			fitted: false,
		}
	}

	/// The union of every node's bounds in graph space, if the graph is
	/// non-empty.
	fn graph_bounds(&self, cx: &App) -> Option<Bounds<Pixels>> {
		let nodes = self.engine.read(cx).nodes();
		if nodes.is_empty() {
			return None;
		}
		let mut min_x = f32::MAX;
		let mut min_y = f32::MAX;
		let mut max_x = f32::MIN;
		let mut max_y = f32::MIN;
		for node in &nodes {
			let element = NodeElement::from_node(node, NodeVisualState::default());
			let position = node.position();
			let width = gpui::node_graph::DEFAULT_NODE_WIDTH;
			let height = element.height();
			let (x, y) = (f32::from(position.x), f32::from(position.y));
			min_x = min_x.min(x);
			min_y = min_y.min(y);
			max_x = max_x.max(x + f32::from(width));
			max_y = max_y.max(y + f32::from(height));
		}
		Some(Bounds::from_corners(
			point(px(min_x), px(min_y)),
			point(px(max_x), px(max_y)),
		))
	}

	/// The canvas size to fit against: the graph view's own painted size once
	/// known, otherwise the window (before the first layout).
	fn fit_viewport(&self, window: &Window, cx: &App) -> gpui::Size<Pixels> {
		let viewport = self.graph.read(cx).viewport_size();
		if viewport.width > px(0.0) && viewport.height > px(0.0) {
			viewport
		} else {
			window.viewport_size()
		}
	}

	/// Fits the whole graph into the canvas (the 适配 button).
	fn fit_graph(&mut self, window: &mut Window, cx: &mut Context<Self>) {
		let Some(rect) = self.graph_bounds(cx) else {
			return;
		};
		let viewport = self.fit_viewport(window, cx);
		self.graph.update(cx, |graph, cx| {
			graph.state_mut().fit_to_rect(rect, viewport);
			cx.notify();
		});
	}

	/// Zooms the canvas by `factor` at its center (`+` / `−` buttons).
	fn zoom(&mut self, factor: f32, window: &mut Window, cx: &mut Context<Self>) {
		let viewport = self.fit_viewport(window, cx);
		let anchor = point(viewport.width * 0.5, viewport.height * 0.5);
		self.graph.update(cx, |graph, cx| {
			graph.state_mut().zoom_at(anchor, factor);
			cx.notify();
		});
	}
}

impl<E: AppEngine> Render for NodeEditorPanel<E> {
	fn render(&mut self, window: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
		// Fit the graph once the canvas size is known (first layout). Before
		// that the viewport is zero-sized, so ask for another frame instead.
		if !self.fitted {
			if self.graph.read(cx).viewport_size() != Default::default() {
				self.fitted = true;
				self.fit_graph(window, cx);
			} else {
				cx.notify();
			}
		}

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
						Some(crate::oakui::icons::ICON_ZOOM_IN),
						"+",
						"timeline.zoom_in",
						|this, window, cx| {
							this.zoom(1.25, window, cx);
						},
					))
					.child(zoom_button(
						cx,
						"node-zoom-out",
						Some(crate::oakui::icons::ICON_ZOOM_OUT),
						"−",
						"timeline.zoom_out",
						|this, window, cx| {
							this.zoom(1.0 / 1.25, window, cx);
						},
					))
					.child(zoom_button(
						cx,
						"node-zoom-fit",
						None,
						crate::i18n::tr("node.fit"),
						"node.fit",
						|this, window, cx| this.fit_graph(window, cx),
					))
					.child(div().flex_1())
					.child(div().text_color(colors.disabled).child(format!(
						"{}% · {}–{}",
						(self.graph.read(cx).state().zoom() * 100.0).round(),
						MIN_ZOOM,
						MAX_ZOOM,
					))),
			)
			.child(
				div()
					.debug_selector(|| "node-editor-canvas".into())
					.flex_1()
					.min_h_0()
					.child(self.graph.clone()),
			)
	}
}

/// A small toolbar button driving the graph viewport. With `icon_name`, the
/// button shows the 16px icon on a 24px hit target; otherwise the `label`
/// text. Both get a localized `tooltip`.
fn zoom_button<E: AppEngine>(
	cx: &mut Context<NodeEditorPanel<E>>,
	id: &'static str,
	icon_name: Option<&'static str>,
	label: impl IntoElement,
	tooltip: &'static str,
	action: impl Fn(&mut NodeEditorPanel<E>, &mut Window, &mut Context<NodeEditorPanel<E>>) + 'static,
) -> impl gpui::IntoElement {
	let colors = cx.default_colors().clone();
	let container = colors.container;
	let tooltip_label = crate::i18n::tr(tooltip);
	let mut el = div()
		.id(id)
		.size(px(24.0))
		.flex()
		.items_center()
		.justify_center()
		.rounded_md()
		.border_1()
		.border_color(colors.border)
		.text_color(colors.text)
		.cursor_pointer()
		.hover(move |style| style.bg(container))
		.tooltip(move |window, cx| {
			gpui_widgets::tooltip::tooltip_view(tooltip_label.into(), window, cx)
		})
		.on_click(cx.listener(move |this, _event: &ClickEvent, window, cx| {
			action(this, window, cx);
		}));
	if let Some(name) = icon_name {
		el = el.child(gpui::img(crate::oakui::icons::icon_path(name, cx)).size(px(16.0)));
	} else {
		el = el.child(label);
	}
	el
}

impl<E: AppEngine> EventEmitter<PanelEvent> for NodeEditorPanel<E> {}

impl<E: AppEngine> DockPanel for NodeEditorPanel<E> {
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

#[cfg(test)]
mod tests {
	use super::*;
	use crate::oakui::MockEngine;
	use gpui::{size, TestAppContext, VisualTestContext};

	/// Builds the panel in a window and returns a `VisualTestContext` for
	/// bounds assertions.
	fn panel_window(
		cx: &mut TestAppContext,
	) -> (
		&'static mut VisualTestContext,
		Entity<NodeEditorPanel<MockEngine>>,
	) {
		cx.update(|cx| cx.init_colors());
		let window = cx.open_window(size(px(640.0), px(480.0)), |window, cx| {
			let engine = cx.new(|cx| crate::oakui::MockEngine::demo(cx));
			NodeEditorPanel::new(engine, window, cx)
		});
		cx.run_until_parked();
		// The graph canvas reports its size only after the first paint, so the
		// initial fit applies on the following frame: draw a few more.
		for _ in 0..3 {
			cx.update_window(window.into(), |_root, window, app| {
				let _ = window.draw(app);
			})
			.expect("window still open");
			cx.run_until_parked();
		}
		let panel = window.root(cx).expect("node editor panel root");
		let cx = VisualTestContext::from_window(window.into(), cx).into_mut();
		(cx, panel)
	}

	/// The panel lays out a graph canvas below the zoom toolbar, and the
	/// initial fit centers the graph so every demo node is on screen.
	#[gpui::test]
	async fn canvas_fills_the_panel_below_the_toolbar(cx: &mut TestAppContext) {
		let (cx, panel) = panel_window(cx);

		let canvas = cx
			.debug_bounds("node-editor-canvas")
			.expect("graph canvas rendered");
		assert!(canvas.size.width > px(0.0));
		assert!(canvas.size.height > px(0.0));

		// The initial fit moved the viewport off the default origin, so the
		// graph is framed rather than clipped at the corner.
		let state = cx.read(|app| panel.read(app).graph.read(app).state().clone());
		assert_ne!(
			state.offset(),
			gpui::node_graph::GraphViewState::new().offset()
		);
		assert!(state.zoom() > 0.0);
	}
}
