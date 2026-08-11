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

use crate::oakui::AppEngine;
use crate::panels::ids::INSPECTOR;

/// The inspector / effect stack panel.
pub struct InspectorPanel<E: AppEngine> {
	stack: Entity<EffectStackView<E>>,
	engine: Entity<E>,
	/// An in-flight "add effect" flow: the stack insertion index carried
	/// by the last [`EffectStackEvent::AddRequested`]. While set (and the
	/// engine has a stack target), the panel renders a small menu of the
	/// engine's addable effects instead of forwarding the bare request.
	pending_add: Option<usize>,
}

impl<E: AppEngine> InspectorPanel<E> {
	/// Builds the stack over `engine`'s effect model.
	pub fn new(engine: Entity<E>, _window: &mut Window, cx: &mut Context<Self>) -> Self {
		let stack = cx.new(|cx| {
			EffectStackView::new(engine.clone(), cx)
				.params_renderer(|_effect, _window, cx| cx.new(|_cx| ParamPlaceholder).into())
		});
		// The "edits are requests" loop: forward each request to the engine,
		// which applies it to its model and notifies. `AddRequested` carries
		// no effect type, so the panel records the insertion index and lets
		// the user pick one from the small menu below (the actual insert
		// runs through `AppEngine::add_effect`).
		cx.subscribe(&stack, |this, _stack, event: &EffectStackEvent, cx| {
			if let EffectStackEvent::AddRequested { index } = event {
				this.pending_add = Some(*index);
			}
			this.engine
				.update(cx, |engine, cx| engine.apply_effect_event(event, cx));
		})
		.detach();

		Self {
			stack,
			engine,
			pending_add: None,
		}
	}

	/// The "add effect" menu: one clickable row per addable effect of the
	/// engine. Selecting a row inserts that effect at the recorded stack
	/// index; a dismiss row closes the menu without adding.
	fn render_add_menu(
		&mut self,
		index: usize,
		colors: &gpui::colors::Colors,
		cx: &mut Context<Self>,
	) -> impl IntoElement {
		let effects = self.engine.read(cx).addable_effects();
		let mut menu = div()
			.id("inspector-add-menu")
			.px_2()
			.py_1()
			.border_t_1()
			.border_color(colors.separator)
			.flex()
			.flex_col()
			.gap_1();

		for (type_id, name) in &effects {
			let engine = self.engine.clone();
			let type_id = type_id.clone();
			let name = name.clone();
			let index = index;
			menu = menu.child(
				div()
					.id(SharedString::from(format!("add-effect-{type_id}")))
					.cursor_pointer()
					.px_2()
					.py_1()
					.rounded_sm()
					.hover(|style| style.bg(colors.selected))
					.text_color(colors.text)
					.text_sm()
					.child(name)
					.on_click(
						cx.listener(move |this, _event: &gpui::ClickEvent, _window, cx| {
							this.pending_add = None;
							engine.update(cx, |engine, cx| {
								if let Err(err) = engine.add_effect(index, &type_id, cx) {
									println!("[inspector] add effect failed: {err}");
								}
							});
							cx.notify();
						}),
					),
			);
		}

		// A dismiss row, so a cancelled pick does not linger.
		menu = menu.child(
			div()
				.id("add-effect-dismiss")
				.cursor_pointer()
				.px_2()
				.py_1()
				.rounded_sm()
				.hover(|style| style.bg(colors.selected))
				.text_color(colors.disabled)
				.text_sm()
				.child("✕")
				.on_click(
					cx.listener(move |this, _event: &gpui::ClickEvent, _window, cx| {
						this.pending_add = None;
						cx.notify();
					}),
				),
		);
		menu
	}
}

impl<E: AppEngine> Render for InspectorPanel<E> {
	fn render(&mut self, _window: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
		let colors = cx.default_colors().clone();
		let mut root = div().id("inspector-panel").size_full().flex().flex_col();
		root = root.child(self.stack.clone());
		// The add-effect menu sits below the stack while an add is
		// pending. It only makes sense while the engine has a stack
		// target (a clip that can host effects).
		if let Some(index) = self.pending_add {
			if self.engine.read(cx).target_label().is_some() {
				root = root.child(self.render_add_menu(index, colors.as_ref(), cx));
			} else {
				self.pending_add = None;
			}
		}
		root
	}
}

impl<E: AppEngine> EventEmitter<PanelEvent> for InspectorPanel<E> {}

impl<E: AppEngine> DockPanel for InspectorPanel<E> {
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
