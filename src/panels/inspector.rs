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

//! The inspector panel (检查器·效果栈): the `EffectStackView` over the
//! engine's node chain, shown as linear cards (媒体 → 变换 → OCIO LUT →
//! 输出) with add / remove / reorder — no engine, the mock applies the edits
//! to its own model.

use gpui::colors::DefaultColors;
use gpui::dock::{DockPanel, PanelEvent};
use gpui::effect_stack::{EffectStackEvent, EffectStackView};
use gpui::{
	div, prelude::*, AnyElement, App, Context, Entity, EventEmitter, Render, SharedString, Window,
};

use crate::oakui::MockEngine;
use crate::panels::ids::INSPECTOR;

/// The inspector / effect stack panel.
pub struct InspectorPanel {
	stack: Entity<EffectStackView<MockEngine>>,
	engine: Entity<MockEngine>,
}

impl InspectorPanel {
	/// Builds the stack over `engine`'s effect model.
	pub fn new(engine: Entity<MockEngine>, _window: &mut Window, cx: &mut Context<Self>) -> Self {
		let stack = cx.new(|cx| {
			EffectStackView::new(engine.clone(), cx)
				.params_renderer(|_effect, _window, cx| cx.new(|_cx| ParamPlaceholder).into())
		});
		// The "edits are requests" loop: forward each request to the engine,
		// which applies it to its model and notifies.
		cx.subscribe(&stack, |this, _stack, event: &EffectStackEvent, cx| {
			this.engine
				.update(cx, |engine, cx| engine.apply_effect_event(event, cx));
		})
		.detach();

		Self { stack, engine }
	}
}

impl Render for InspectorPanel {
	fn render(&mut self, _window: &mut Window, _cx: &mut Context<Self>) -> impl IntoElement {
		div().size_full().child(self.stack.clone())
	}
}

impl EventEmitter<PanelEvent> for InspectorPanel {}

impl DockPanel for InspectorPanel {
	fn panel_id(&self) -> gpui::dock::PanelId {
		INSPECTOR
	}

	fn title(&self, _cx: &App) -> SharedString {
		crate::i18n::tr("panel.inspector").into()
	}

	fn tab_content(&self, _cx: &App) -> AnyElement {
		div()
			.child(crate::i18n::tr("panel.inspector"))
			.into_any_element()
	}
}

/// Placeholder parameter view rendered inside expanded effect cards.
/// A real app builds the effect's controls here and calls
/// [`EffectStackView::notify_parameter_changed`] after edits.
struct ParamPlaceholder;

impl Render for ParamPlaceholder {
	fn render(&mut self, _window: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
		let colors = cx.default_colors().clone();
		div()
			.px_3()
			.py_2()
			.text_color(colors.disabled)
			.child(crate::i18n::tr("inspector.params"))
	}
}
