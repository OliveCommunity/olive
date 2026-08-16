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

//! The timeline panel (时间线): the design's 31px toolbar (tools, snap
//! toggle) above the full-width [`TimelineView`](gpui::timeline::TimelineView)
//! over the engine's sequence model.
//!
//! # Layout (fixed 2026-08)
//!
//! ```text
//! ┌─────────────────────────────────────────┬─────────────┐
//! │ toolbar row (fixed 31px): tools + − ⏵  │             │
//! ├─────────────────────────────────────────┤ right-side  │
//! │ timeline (ruler takes remaining width,  │ controls    │
//! │  clip area below)                       │ (fixed 140px│
//! │                                         │  zoom /     │
//! │                                         │  track hgt) │
//! └─────────────────────────────────────────┴─────────────┘
//! ```
//!
//! The zoom and track-height sliders used to sit at the right end of the
//! toolbar, where they overflowed into the ruler's timecode labels (the
//! toolbar is exactly 31px but the sliders' value rows are taller, and at
//! narrow widths the sliders squeezed into the ruler's right side). They
//! now live in a fixed-width trailing slot beside the timeline body, and the
//! timeline wrapper is `min_w_0` so the ruler always keeps the remaining
//! space — no overlap at 1600×900 or down to ~1100px wide.

use gpui::colors::DefaultColors;
use gpui::dock::{DockPanel, PanelEvent};
use gpui::timeline::{
	Frame, TimelineView, TrackData, TrackKind, HEADER_WIDTH, MIN_TRACK_HEIGHT, RULER_HEIGHT,
};
use gpui::{div, img, prelude::*, px, Context, Entity, Window};
use gpui::{AnyElement, App, ClickEvent, DragMoveEvent, EventEmitter, Render, SharedString};
use gpui_widgets::checkbox::{CheckBox, CheckBoxEvent, CheckState};
use gpui_widgets::project_explorer::FootageDrag;
use gpui_widgets::slider::{Slider, SliderEvent, SliderModel};
use gpui_widgets::tooltip::tooltip_view;
use gpui_widgets::value::ValueKind;

use crate::i18n;
use crate::oakui::icons;
use crate::oakui::AppEngine;
use crate::panels::ids::TIMELINE;

/// Toolbar height, per the design (31px).
const TOOLBAR_HEIGHT: f32 = 31.0;
/// Fixed width of the trailing controls slot (zoom / track-height sliders).
/// Kept constant so the sliders can never intrude into the ruler's labels.
const RIGHT_CONTROLS_WIDTH: f32 = 140.0;
/// The demo tool set, by i18n key, with the matching toolbar icon (the
/// legacy C++ icon set). Only the visual selection is implemented; each
/// tool's behavior arrives with the real tool system later.
const TOOLS: [(&str, &str); 8] = [
	("timeline.tool.select", crate::oakui::icons::ICON_ARROW),
	("timeline.tool.razor", crate::oakui::icons::ICON_RAZOR),
	("timeline.tool.ripple", crate::oakui::icons::ICON_RIPPLE),
	("timeline.tool.slip", crate::oakui::icons::ICON_SLIP),
	("timeline.tool.roll", crate::oakui::icons::ICON_ROLLING),
	("timeline.tool.zoom", crate::oakui::icons::ICON_ZOOM),
	("timeline.tool.slide", crate::oakui::icons::ICON_SLIDE),
	(
		"timeline.tool.track_select",
		crate::oakui::icons::ICON_TRACK_SELECT,
	),
];

/// The timeline panel.
pub struct TimelinePanel<E: AppEngine> {
	timeline: Entity<TimelineView<E>>,
	engine: Entity<E>,
	zoom: Entity<Slider>,
	height: Entity<Slider>,
	snap: Entity<CheckBox>,
	/// The currently selected tool (visual only).
	selected_tool: usize,
	/// The drop point of an in-flight footage drag: the display track under
	/// the cursor plus the start frame. `None` outside the clip area or while
	/// no footage drag is active.
	footage_drop: Option<FootageDropTarget>,
}

/// A footage drop target resolved from the cursor: the display track under
/// the pointer and the clip's start frame.
struct FootageDropTarget {
	/// The pointed track's kind.
	track_kind: TrackKind,
	/// The pointed display track index.
	track_index: usize,
	/// The start frame at the pointer.
	time: Frame,
}

impl<E: AppEngine> TimelinePanel<E> {
	/// Builds the panel around `timeline` (created by the app shell so it can
	/// sync the playhead).
	pub fn new(
		engine: Entity<E>,
		timeline: Entity<TimelineView<E>>,
		window: &mut Window,
		cx: &mut Context<Self>,
	) -> Self {
		let zoom = cx.new(|cx| {
			Slider::new(
				10,
				SliderModel::new(ValueKind::Float, 0.5, 8.0, 0.1, 2.0),
				window,
				cx,
			)
		});
		let height = cx.new(|cx| {
			Slider::new(
				11,
				SliderModel::new(ValueKind::Float, 24.0, 160.0, 8.0, 64.0),
				window,
				cx,
			)
		});
		let snap = cx.new(|cx| CheckBox::new(12, CheckState::Checked, window, cx));

		// Zoom slider → timeline zoom (pixels per frame).
		cx.subscribe(&zoom, |this, _zoom, event: &SliderEvent, cx| {
			if let SliderEvent::ValueChanged { value, .. } = event {
				let zoom = value.to_f64() as f32;
				this.timeline.update(cx, |timeline, cx| {
					timeline.state.set_zoom(zoom, px(0.0));
					cx.notify();
				});
			}
		})
		.detach();

		// Track-height slider → engine model (persisted per sequence).
		cx.subscribe(&height, |this, _height, event: &SliderEvent, cx| {
			if let SliderEvent::ValueChanged { value, .. } = event {
				let height = value.to_f64() as f32;
				this.engine
					.update(cx, |engine, cx| engine.set_track_height(px(height), cx));
			}
		})
		.detach();

		// Snap toggle → timeline view state.
		cx.subscribe(&snap, |this, _snap, event: &CheckBoxEvent, cx| {
			let CheckBoxEvent::Toggled { state, .. } = event;
			let enabled = *state == CheckState::Checked;
			this.timeline.update(cx, |timeline, cx| {
				timeline.state.snap_enabled = enabled;
				cx.notify();
			});
		})
		.detach();

		Self {
			timeline,
			engine,
			zoom,
			height,
			snap,
			selected_tool: 0,
			footage_drop: None,
		}
	}

	/// Resolves the footage-drop target under the cursor: converts the
	/// pointer (relative to the timeline body) into a display track + start
	/// frame using the timeline view's zoom/scroll state and the engine's
	/// track heights — the same affine mapping the timeline itself uses (see
	/// [`TimelineState::frame_at_point`] and the view's track-row walk).
	/// Hovering outside the clip area (above the ruler) clears the target.
	fn update_footage_drop(&mut self, event: &DragMoveEvent<FootageDrag>, cx: &mut Context<Self>) {
		let now = event.event.position - event.bounds.origin;
		// The clip area starts below the ruler and right of the track
		// headers column.
		if f32::from(now.y) < RULER_HEIGHT {
			self.footage_drop = None;
			return;
		}
		let clip_x = f32::from(now.x - px(HEADER_WIDTH)).max(0.0);
		let clip_y = now.y - px(RULER_HEIGHT);
		let state = self.timeline.read(cx).state.clone();
		let seq_len = self.engine.read(cx).sequence_length();
		let time = state.frame_at_point(px(clip_x)).clamp(Frame::ZERO, seq_len);
		// Walk the display rows top-down, clamping each to the minimum row
		// height exactly like the timeline's own `track_at_y`.
		let (track_kind, track_index) = {
			let engine = self.engine.read(cx);
			let mut acc = 0.0f32;
			let mut found = None;
			for index in 0..engine.track_count() {
				if let Some(track) = engine.track(index) {
					acc += f32::from(track.height()).max(MIN_TRACK_HEIGHT);
					if f32::from(clip_y) < acc {
						found = Some((track.kind(), index));
						break;
					}
				}
			}
			found.unwrap_or_else(|| {
				let last = engine.track_count().saturating_sub(1);
				engine
					.track(last)
					.map(|t| (t.kind(), last))
					.unwrap_or((TrackKind::Video, 0))
			})
		};
		self.footage_drop = Some(FootageDropTarget {
			track_kind,
			track_index,
			time,
		});
	}

	/// Applies a finished footage drop: routes the payload's footage id with
	/// the last hovered track + frame to the engine, which resolves the
	/// footage, validates the track and places the clip (undoable).
	fn finish_footage_drop(&mut self, drag: &FootageDrag, cx: &mut Context<Self>) {
		let Some(target) = self.footage_drop.take() else {
			return;
		};
		let FootageDropTarget {
			track_kind,
			track_index,
			time,
		} = target;
		self.engine.update(cx, |engine, cx| {
			engine.drop_footage(drag.0, track_kind, track_index, time, cx);
		});
	}
}

impl<E: AppEngine> Render for TimelinePanel<E> {
	fn render(&mut self, _window: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
		let colors = cx.default_colors().clone();

		// --- toolbar row (fixed 31px, above the ruler) --------------------
		let mut toolbar = div()
			.debug_selector(|| "timeline-toolbar".into())
			.h(px(TOOLBAR_HEIGHT))
			.flex_shrink_0()
			.flex()
			.items_center()
			.gap_2()
			.px_2()
			.overflow_hidden()
			.border_b_1()
			.border_color(colors.border)
			.bg(colors.container);

		// A tool button: a 16px icon on a 24px hit target with a localized
		// tooltip; the selected tool is highlighted.
		let tool_button =
			|index: usize, icon_name: &'static str, key: &'static str, cx: &mut Context<Self>| {
				let tool = i18n::tr(key);
				let selected = self.selected_tool == index;
				let background = if selected {
					colors.selected
				} else {
					colors.background
				};
				let hover_bg = colors.selected;
				let path = icons::icon_path(icon_name, cx);
				div()
					.id(SharedString::from(format!("tool-{index}")))
					.size(px(24.0))
					.flex()
					.items_center()
					.justify_center()
					.rounded_sm()
					.cursor_pointer()
					.bg(background)
					.hover(move |style| style.bg(hover_bg))
					.tooltip(move |window, cx| tooltip_view(tool.into(), window, cx))
					.on_click(cx.listener(move |this, _event: &ClickEvent, _window, _cx| {
						println!("[timeline] tool: {tool} (placeholder)");
						this.selected_tool = index;
					}))
					.child(img(path).size(px(16.0)))
			};

		for (index, (tool_key, icon_name)) in TOOLS.iter().enumerate() {
			toolbar = toolbar.child(tool_button(index, icon_name, tool_key, cx));
		}

		// A plain icon button (no selection state), e.g. zoom in/out.
		let icon_btn = |id: &'static str,
		                icon_name: &'static str,
		                key: &'static str,
		                cx: &mut Context<Self>| {
			let label = i18n::tr(key);
			let hover_bg = colors.container;
			let path = icons::icon_path(icon_name, cx);
			div()
				.id(id)
				.size(px(24.0))
				.flex()
				.items_center()
				.justify_center()
				.rounded_sm()
				.cursor_pointer()
				.text_color(colors.text)
				.hover(move |style| style.bg(hover_bg))
				.tooltip(move |window, cx| tooltip_view(label.into(), window, cx))
				.child(img(path).size(px(16.0)))
		};

		// The snap toggle: the magnet icon next to the checkbox box. The icon
		// is decorative (the box itself is clickable, as in the widget's
		// default row).
		let snap_row = div()
			.flex()
			.items_center()
			.gap_1()
			.text_color(colors.text)
			.child(
				div()
					.id("snap-toggle")
					.size(px(24.0))
					.flex()
					.items_center()
					.justify_center()
					.cursor_pointer()
					.tooltip(move |window, cx| {
						tooltip_view(i18n::tr("timeline.snap").into(), window, cx)
					})
					.child(img(icons::icon_path(icons::ICON_SNAP, cx)).size(px(16.0))),
			)
			.child(self.snap.clone());

		let toolbar = toolbar
			.child(icon_btn(
				"toolbar-zoom-in",
				icons::ICON_ZOOM_IN,
				"timeline.zoom_in",
				cx,
			))
			.child(icon_btn(
				"toolbar-zoom-out",
				icons::ICON_ZOOM_OUT,
				"timeline.zoom_out",
				cx,
			))
			.child(
				div()
					.w_1()
					.h_full()
					.border_l_1()
					.border_color(colors.border),
			)
			.child(snap_row);

		// --- trailing controls slot (fixed width, right of the body) -------
		let right_controls = div()
			.debug_selector(|| "timeline-right-controls".into())
			.w(px(RIGHT_CONTROLS_WIDTH))
			.flex_shrink_0()
			.flex()
			.flex_col()
			.justify_center()
			.gap_1()
			.px_2()
			.border_l_1()
			.border_color(colors.border)
			.bg(colors.container)
			.child(
				div()
					.flex()
					.flex_col()
					.gap_1()
					.text_xs()
					.text_color(colors.disabled)
					.child(i18n::tr("timeline.zoom"))
					.child(self.zoom.clone()),
			)
			.child(
				div()
					.flex()
					.flex_col()
					.gap_1()
					.text_xs()
					.text_color(colors.disabled)
					.child(i18n::tr("timeline.track_height"))
					.child(self.height.clone()),
			);

		div()
			.size_full()
			.flex()
			.flex_col()
			.overflow_hidden()
			.child(toolbar)
			.child(
				div()
					.debug_selector(|| "timeline-body".into())
					.flex_1()
					.min_h_0()
					.flex()
					.flex_row()
					.child(
						div()
							.debug_selector(|| "timeline-canvas".into())
							.flex_1()
							.min_w_0()
							// Footage drop target: hover resolves the track +
							// frame (see [`TimelinePanel::update_footage_drop`]),
							// the release routes the payload to the engine.
							.on_drag_move(cx.listener(
								|this, event: &DragMoveEvent<FootageDrag>, _window, cx| {
									this.update_footage_drop(event, cx);
								},
							))
							.on_drop(cx.listener(|this, drag: &FootageDrag, _window, cx| {
								this.finish_footage_drop(drag, cx);
							}))
							.child(self.timeline.clone()),
					)
					.child(right_controls),
			)
	}
}

impl<E: AppEngine> EventEmitter<PanelEvent> for TimelinePanel<E> {}

impl<E: AppEngine> DockPanel for TimelinePanel<E> {
	fn panel_id(&self) -> gpui::dock::PanelId {
		TIMELINE
	}

	fn title(&self, _cx: &App) -> SharedString {
		i18n::tr("panel.timeline").into()
	}

	fn tab_content(&self, _cx: &App) -> AnyElement {
		div().child(i18n::tr("panel.timeline")).into_any_element()
	}
}

#[cfg(test)]
mod tests {
	use super::*;
	use crate::oakui::MockEngine;
	use gpui::{px, size, TestAppContext, VisualTestContext};

	/// Builds a `TimelinePanel` in a window of the given logical size and
	/// returns a `VisualTestContext` for bounds assertions.
	fn panel_window(
		cx: &mut TestAppContext,
		width: f32,
		height: f32,
	) -> (
		&'static mut VisualTestContext,
		Entity<TimelinePanel<MockEngine>>,
	) {
		cx.update(|cx| cx.init_colors());
		let window = cx.open_window(size(px(width), px(height)), |window, cx| {
			let engine = cx.new(|cx| crate::oakui::MockEngine::demo(cx));
			let timeline = cx.new(|cx| TimelineView::new(engine.clone(), window, cx).zoom(2.0));
			TimelinePanel::new(engine, timeline, window, cx)
		});
		cx.run_until_parked();
		let panel = window.root(cx).expect("timeline panel root");
		let cx = VisualTestContext::from_window(window.into(), cx).into_mut();
		(cx, panel)
	}

	/// The toolbar row must sit entirely above the timeline body, the right
	/// controls must sit to the right of the timeline canvas (never
	/// overlapping it), and the controls slot must keep its fixed width — at
	/// the default 1600×900 and down to ~1100px wide.
	#[gpui::test]
	async fn toolbar_ruler_and_right_controls_never_overlap(cx: &mut TestAppContext) {
		for width in [1600.0, 1280.0, 1100.0] {
			let (cx, _panel) = panel_window(cx, width, 900.0);

			let toolbar = cx
				.debug_bounds("timeline-toolbar")
				.expect("toolbar row rendered");
			let body = cx
				.debug_bounds("timeline-body")
				.expect("timeline body row rendered");
			let canvas = cx
				.debug_bounds("timeline-canvas")
				.expect("timeline canvas rendered");
			let right = cx
				.debug_bounds("timeline-right-controls")
				.expect("right controls slot rendered");

			// The toolbar is exactly 31px tall and ends where the body starts.
			assert!(
				(f32::from(toolbar.size.height) - TOOLBAR_HEIGHT).abs() < 0.5,
				"toolbar height {width}: {} != {TOOLBAR_HEIGHT}",
				toolbar.size.height
			);
			assert!(
				toolbar.bottom() <= body.top(),
				"toolbar overlaps the body at width {width}"
			);

			// The controls slot is fixed-width and never overlaps the canvas.
			assert!(
				(f32::from(right.size.width) - RIGHT_CONTROLS_WIDTH).abs() < 0.5,
				"right slot width {width}: {} != {RIGHT_CONTROLS_WIDTH}",
				right.size.width
			);
			assert!(
				canvas.right() <= right.left(),
				"right controls overlap the timeline canvas at width {width}"
			);

			// The timeline (ruler) keeps the remaining width: canvas right
			// edge equals the slot's left edge exactly.
			assert!(
				(f32::from(canvas.right()) - f32::from(right.left())).abs() < 0.5,
				"canvas and controls slot are not flush at width {width}"
			);

			// The right controls are inside the body's vertical bounds.
			assert!(
				right.top() >= body.top() && right.bottom() <= body.bottom(),
				"right controls escape the body at width {width}"
			);
		}
	}

	/// Resizing a window keeps the same invariants (the timeline body shrinks
	/// while the toolbar and the right slot stay fixed).
	#[gpui::test]
	async fn resizing_keeps_toolbar_and_right_slot_fixed(cx: &mut TestAppContext) {
		let (cx, _panel) = panel_window(cx, 1600.0, 900.0);

		let before = cx
			.debug_bounds("timeline-right-controls")
			.expect("right controls rendered");
		assert!((f32::from(before.size.width) - RIGHT_CONTROLS_WIDTH).abs() < 0.5);

		cx.simulate_resize(size(px(1100.0), px(900.0)));
		cx.run_until_parked();

		let after = cx
			.debug_bounds("timeline-right-controls")
			.expect("right controls rendered after resize");
		let canvas = cx
			.debug_bounds("timeline-canvas")
			.expect("timeline canvas after resize");
		assert!((f32::from(after.size.width) - RIGHT_CONTROLS_WIDTH).abs() < 0.5);
		assert!(canvas.right() <= after.left());
	}
}
