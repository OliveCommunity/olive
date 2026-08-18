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

//! The effect library panel (效果库): every effect type the engine can add
//! to a clip's chain, as a flat list. Double-clicking an entry appends the
//! effect to the selected clip's effect chain (the undoable
//! [`AppEngine::add_effect`]; the insertion index is clamped to the chain
//! end by the backend).

use gpui::colors::DefaultColors;
use gpui::dock::{DockPanel, PanelEvent};
use gpui::{
	div, prelude::*, AnyElement, App, ClickEvent, Context, Entity, EventEmitter, MouseButton,
	Render, SharedString, Window,
};

use crate::i18n;
use crate::oakui::AppEngine;
use crate::panels::commands::PanelCommandHandler;
use crate::panels::ids::EFFECT_LIBRARY;

/// The effect library panel.
pub struct EffectLibraryPanel<E: AppEngine> {
	engine: Entity<E>,
}

impl<E: AppEngine> EffectLibraryPanel<E> {
	/// Builds the panel over `engine`'s addable-effect table.
	pub fn new(engine: Entity<E>, _window: &mut Window, cx: &mut Context<Self>) -> Self {
		// Re-read the effect table whenever the engine notifies (the table
		// itself is static, but the selection hint depends on the target).
		cx.observe(&engine, |_this, _engine, cx| cx.notify()).detach();
		Self { engine }
	}
}

/// The effect library implements no focused-panel commands: everything
/// falls through to the shell's global handler.
impl<E: AppEngine> PanelCommandHandler for EffectLibraryPanel<E> {}

impl<E: AppEngine> Render for EffectLibraryPanel<E> {
	fn render(&mut self, _window: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
		let colors = cx.default_colors().clone();
		let effects = self.engine.read(cx).addable_effects();

		let mut list = div()
			.id("effect-library-list")
			.flex_1()
			.min_h_0()
			.flex()
			.flex_col()
			.gap_1()
			.p_2()
			.overflow_y_scroll();

		// Built-in effects render flat; OpenFX plugin entries are grouped
		// under their sub-category header (Filter / Generator / Transition /
		// General — the C++ `factorymenu` OpenFX branch).
		let mut last_group: Option<String> = None;
		for entry in &effects {
			match &entry.group {
				Some(group) => {
					if last_group.as_deref() != Some(group.as_str()) {
						last_group = Some(group.clone());
						list = list.child(group_header(&colors, group));
					}
				}
				None => {
					last_group = None;
				}
			}
			let engine = self.engine.clone();
			let row_id = entry.type_id.clone();
			let name = entry.name.clone();
			let type_id = entry.type_id.clone();
			list = list.child(
				div()
					.id(SharedString::from(format!("effect-library-{type_id}")))
					.debug_selector(move || format!("effect-library-row-{row_id}").into())
					.cursor_pointer()
					.px_2()
					.py_1()
					.rounded_sm()
					.text_sm()
					.text_color(colors.text)
					.hover(|style| style.bg(colors.selected))
					.child(name)
					.on_click(move |event: &ClickEvent, _window, cx| {
						// Double-click appends the effect to the selected
						// clip's chain; the backend clamps the index to the
						// chain end.
						if event.click_count() == 2 {
							let type_id = type_id.clone();
							engine.update(cx, |engine, cx| {
								if let Err(err) = engine.add_effect(usize::MAX, &type_id, cx) {
									println!("[effect library] add effect failed: {err}");
								}
							});
						}
					}),
			);
		}

		div()
			.size_full()
			.flex()
			.flex_col()
			// Any click inside the panel makes it the focused panel (the
			// dock re-emits this as `DockEvent::PanelFocused`, which the
			// shell uses to route focused-panel commands).
			.on_mouse_down(MouseButton::Left, {
				cx.listener(|_this, _event: &gpui::MouseDownEvent, _window, cx| {
					cx.emit(PanelEvent::Focused);
				})
			})
			.child(list)
			.child(
				div()
					.flex_shrink_0()
					.px_2()
					.py_1()
					.border_t_1()
					.border_color(colors.border)
					.text_xs()
					.text_color(colors.disabled)
					.child(i18n::tr("effect_library.hint")),
			)
	}
}

/// The sub-category header row of the OpenFX group (a muted, all-caps
/// line above the plugin entries).
fn group_header(colors: &gpui::colors::Colors, group: &str) -> impl IntoElement {
	div()
		.id(SharedString::from(format!("effect-library-group-{group}")))
		.pt_2()
		.pb_1()
		.px_2()
		.text_xs()
		.font_weight(gpui::FontWeight(600.0))
		.text_color(colors.disabled)
		.child(group.to_string())
}

impl<E: AppEngine> EventEmitter<PanelEvent> for EffectLibraryPanel<E> {}

impl<E: AppEngine> DockPanel for EffectLibraryPanel<E> {
	fn panel_id(&self) -> gpui::dock::PanelId {
		EFFECT_LIBRARY
	}

	fn title(&self, _cx: &App) -> SharedString {
		i18n::tr("panel.effect_library").into()
	}

	fn tab_content(&self, _cx: &App) -> AnyElement {
		div()
			.child(i18n::tr("panel.effect_library"))
			.into_any_element()
	}
}

#[cfg(test)]
mod tests {
	use super::*;
	use crate::oakui::MockEngine;
	use gpui::{px, size, TestAppContext, VisualTestContext};

	/// The panel renders one row per addable effect of the engine (the
	/// mock exposes the real factory's video-effect table).
	#[gpui::test]
	async fn lists_every_addable_effect(cx: &mut TestAppContext) {
		cx.update(|cx| cx.init_colors());
		let window = cx.open_window(size(px(400.0), px(600.0)), |window, cx| {
			let engine = cx.new(|cx| MockEngine::demo(cx));
			EffectLibraryPanel::new(engine, window, cx)
		});
		cx.run_until_parked();
		let cx = VisualTestContext::from_window(window.into(), cx).into_mut();

		let expected = crate::oakui::effectchain::addable_effects();
		assert!(!expected.is_empty());
		for entry in &expected {
			let type_id = &entry.type_id;
			// `debug_bounds` takes a &'static selector; the per-row selector is
			// dynamic, so the test leaks it (process-lifetime, test-only).
			let selector: &'static str =
				Box::leak(format!("effect-library-row-{type_id}").into_boxed_str());
			assert!(
				cx.debug_bounds(selector).is_some(),
				"effect row {type_id} rendered"
			);
		}
	}
}
