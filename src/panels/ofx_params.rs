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

//! The inspector's OFX parameter view (stage 6b): auto-generated controls
//! for the effect node's inputs.
//!
//! The [`EffectStackView`](gpui::effect_stack::EffectStackView) invokes a
//! params renderer inside each expanded effect card. For OFX plugin nodes
//! this view reads the effect's parameter snapshot from the engine
//! ([`AppEngine::effect_params`]) and renders one control per input:
//!
//! - int / float → [`Slider`] (double-click for direct entry)
//! - boolean → [`CheckBox`]
//! - combo → [`ComboBox`] fed from the repeated `("combo_option", _)`
//!   properties; string-combo values come from `("combo_value", _)`
//! - text → [`EditableTextState`]
//! - vec2 / vec3 / color → one [`SpinBox`] per component
//! - push button → a clickable button (`AppEngine::effect_push_button`)
//!
//! Secret (HIDDEN) inputs never reach the snapshot, so they render
//! nothing; `ui_group` / `ui_page` become section titles. Every edit is
//! routed through [`AppEngine::set_effect_param`] (undoable).
//!
//! The control set is rebuilt when the card re-renders (the params view is
//! created fresh per expanded-card render), so it carries no state of its
//! own; values are re-synced from the engine each frame.

use gpui::effect_stack::EffectId;
use gpui::colors::DefaultColors;
use gpui::{
	div, prelude::*, px, ClickEvent, Context, Entity, Render, SharedString, Window,
};
use gpui_elements::editable_text::{text_input, EditableTextState, StringStorage};
use gpui_widgets::checkbox::{CheckBox, CheckBoxEvent, CheckState};
use gpui_widgets::combo_box::{ComboBox, ComboBoxEvent, ComboBoxOption};
use gpui_widgets::slider::{Slider, SliderModel};
use gpui_widgets::spinbox::{SpinBox, SpinBoxEvent};
use gpui_widgets::value::{SliderValue, ValueKind};

use oaknode::value::{NodeValue, ValueType};

use crate::oakui::{AppEngine, EffectParam};

/// One editable parameter row of the view.
struct ParamControl {
	/// The input id (the OFX param name).
	input_id: String,
	/// The display name (the OFX param label).
	display_name: String,
	/// The OFX ui_group / ui_page section header, if any.
	section: Option<(String, String)>,
	/// The control(s) for this parameter.
	kind: ControlKind,
}

/// The concrete control(s) for one parameter.
enum ControlKind {
	/// A slider (int / float).
	Slider(Entity<Slider>),
	/// A checkbox (boolean).
	CheckBox(Entity<CheckBox>),
	/// A combo box (combo / string combo).
	Combo(Entity<ComboBox>),
	/// One spinbox per component (vec2 / vec3 / color); the usize is the
	/// component index within the value.
	Spin(Vec<(Entity<SpinBox>, usize)>),
	/// A text field (string).
	Text(Entity<EditableTextState>),
	/// A push button (rendered inline, no entity).
	PushButton,
	/// A read-only value line (no editable control; e.g. custom/binary).
	ReadOnly(SharedString),
}

/// The inspector's parameter view for one expanded effect card.
pub struct OfxParamsView<E: AppEngine> {
	engine: Entity<E>,
	effect: EffectId,
	controls: Vec<ParamControl>,
}

impl<E: AppEngine> OfxParamsView<E> {
	/// Builds the control set from the engine's current parameter snapshot.
	pub fn new(
		effect: EffectId,
		engine: Entity<E>,
		window: &mut Window,
		cx: &mut Context<Self>,
	) -> Self {
		let params = engine.read(cx).effect_params(effect).unwrap_or_default();
		let mut control_id = 0usize;
		let controls = params
			.iter()
			.map(|param| build_control(param, &mut control_id, window, cx))
			.collect();

		let this = Self {
			engine,
			effect,
			controls,
		};
		wire_controls(&this, cx);
		this
	}

	/// Applies the engine's current values to every control (called each
	/// render so external edits / undo / redo land on the controls).
	fn sync_values(&mut self, cx: &mut Context<Self>) {
		let params = self.engine.read(cx).effect_params(self.effect).unwrap_or_default();
		for control in &self.controls {
			let Some(param) = params.iter().find(|p| p.input_id == control.input_id) else {
				continue;
			};
			match &control.kind {
				ControlKind::Slider(slider) => {
					let sv = slider_value(param);
					let slider = slider.clone();
					slider.update(cx, |slider, _| slider.set_value(sv));
				}
				ControlKind::CheckBox(check) => {
					let state = match param.value {
						NodeValue::Boolean(true) => CheckState::Checked,
						_ => CheckState::Unchecked,
					};
					let check = check.clone();
					check.update(cx, |check, cx| check.set_state(state, cx));
				}
				ControlKind::Combo(combo) => {
					let index = combo_index_for(param);
					let combo = combo.clone();
					combo.update(cx, |combo, cx| {
						combo.set_selected(Some(index), cx);
					});
				}
				ControlKind::Spin(spins) => {
					let components = value_components(&param.value);
					for (spin, channel) in spins {
						if let Some(value) = components.get(*channel) {
							let spin = spin.clone();
							let sv = SliderValue::Float(*value);
							spin.update(cx, |spin, cx| spin.set_value(sv, cx));
						}
					}
				}
				ControlKind::Text(editor) => {
					let text = match &param.value {
						NodeValue::Text(s) => s.clone(),
						NodeValue::StrCombo(s) => s.clone(),
						_ => continue,
					};
					let editor = editor.clone();
					editor.update(cx, |editor, cx| {
						if editor.as_str() != text {
							editor.emplace(&text, cx);
						}
					});
				}
				ControlKind::PushButton | ControlKind::ReadOnly(_) => {}
			}
		}
	}
}

/// The SliderValue for a param's current value (int → Integer, float →
/// Float).
fn slider_value(param: &EffectParam) -> SliderValue {
	match param.value {
		NodeValue::Int(v) => SliderValue::Integer(v),
		NodeValue::Float(v) => SliderValue::Float(v),
		_ => SliderValue::Float(param.value.to_double()),
	}
}

/// The selected option index of a combo/string-combo parameter. Integer
/// combos carry the index directly; string combos are matched against the
/// `("combo_value", _)` (or `("combo_option", _)`) list by value.
fn combo_index_for(param: &EffectParam) -> usize {
	if param.value_type == ValueType::StrCombo {
		let values = crate::oakui::effectchain::combo_values(param);
		let haystack = if values.is_empty() {
			crate::oakui::effectchain::combo_options(param)
		} else {
			values
		};
		match &param.value {
			NodeValue::StrCombo(s) | NodeValue::Text(s) => {
				haystack.iter().position(|v| v == s).unwrap_or(0)
			}
			_ => 0,
		}
	} else {
		param.value.to_double().max(0.0) as usize
	}
}

/// The component list of a vec/color value (empty for other types).
fn value_components(value: &NodeValue) -> Vec<f64> {
	match value {
		NodeValue::Vec2(v) => vec![v[0], v[1]],
		NodeValue::Vec3(v) => vec![v[0], v[1], v[2]],
		NodeValue::Vec4(v) => vec![v[0], v[1], v[2], v[3]],
		NodeValue::Color(v) => vec![v[0], v[1], v[2], v[3]],
		_ => Vec::new(),
	}
}

/// The default numeric range when the parameter carries no min/max
/// properties (the OFX translation only attaches min/max to colour
/// inputs). Wide ranges keep every value reachable; the slider's
/// double-click entry allows exact typing.
fn default_range(value_type: ValueType) -> (f64, f64) {
	match value_type {
		ValueType::Int => (-100000.0, 100000.0),
		_ => (-10000.0, 10000.0),
	}
}

/// The min/max from the parameter's `("min", Float)` / `("max", Float)`
/// properties, falling back to [`default_range`].
fn numeric_range(param: &EffectParam) -> (f64, f64) {
	let prop = |key: &str| {
		param
			.properties
			.iter()
			.find(|(k, _)| k == key)
			.and_then(|(_, v)| match v {
				NodeValue::Float(f) => Some(*f),
				NodeValue::Int(i) => Some(*i as f64),
				_ => None,
			})
	};
	let (dmin, dmax) = default_range(param.value_type);
	(
		prop("min").unwrap_or(dmin),
		prop("max").unwrap_or(dmax),
	)
}

/// Builds one [`ParamControl`] for `param`, creating the control entities
/// (each consuming one control id from `next_id`).
fn build_control<E: AppEngine>(
	param: &EffectParam,
	next_id: &mut usize,
	window: &mut Window,
	cx: &mut Context<OfxParamsView<E>>,
) -> ParamControl {
	let kind = match param.value_type {
		ValueType::Int | ValueType::Float => {
			let (min, max) = numeric_range(param);
			let kind = if param.value_type == ValueType::Int {
				ValueKind::Integer
			} else {
				ValueKind::Float
			};
			let step = if param.value_type == ValueType::Int {
				1.0
			} else {
				((max - min) / 200.0).max(0.001)
			};
			let default_raw = param.value.to_double().clamp(min, max);
			let model = SliderModel::new(kind, min, max, step, default_raw);
			let slider = cx.new(|cx| Slider::new(*next_id, model, window, cx));
			*next_id += 1;
			ControlKind::Slider(slider)
		}
		ValueType::Boolean => {
			let state = match param.value {
				NodeValue::Boolean(true) => CheckState::Checked,
				_ => CheckState::Unchecked,
			};
			let check = cx.new(|cx| CheckBox::new(*next_id, state, window, cx));
			*next_id += 1;
			ControlKind::CheckBox(check)
		}
		ValueType::Combo | ValueType::StrCombo => {
			let options: Vec<String> = if param.value_type == ValueType::StrCombo {
				let values = crate::oakui::effectchain::combo_values(param);
				if values.is_empty() {
					crate::oakui::effectchain::combo_options(param)
				} else {
					values
				}
			} else {
				crate::oakui::effectchain::combo_options(param)
			};
			if options.is_empty() {
				// No option list: show the raw value read-only.
				let text = if param.value_type == ValueType::Combo {
					format!("{}", param.value.to_double() as i64)
				} else {
					match &param.value {
						NodeValue::StrCombo(s) | NodeValue::Text(s) => s.clone(),
						_ => String::new(),
					}
				};
				ControlKind::ReadOnly(text.into())
			} else {
				let options = options
					.iter()
					.enumerate()
					.map(|(i, label)| ComboBoxOption::new(i, label.clone()))
					.collect();
				let combo = cx.new(|cx| ComboBox::new(*next_id, options, window, cx));
				let index = combo_index_for(param);
				combo.update(cx, |combo, cx| combo.set_selected(Some(index), cx));
				*next_id += 1;
				ControlKind::Combo(combo)
			}
		}
		ValueType::Text => {
			let text = match &param.value {
				NodeValue::Text(s) => s.clone(),
				_ => String::new(),
			};
			let editor = cx.new(|cx| EditableTextState::new(StringStorage::default(), cx));
			editor.update(cx, |editor, cx| editor.emplace(&text, cx));
			*next_id += 1;
			ControlKind::Text(editor)
		}
		ValueType::Vec2 | ValueType::Vec3 | ValueType::Color => {
			let components = value_components(&param.value);
			let count = if param.value_type == ValueType::Vec2 {
				2
			} else if param.value_type == ValueType::Vec3 {
				3
			} else {
				4
			};
			let (min, max) = if param.value_type == ValueType::Color {
				// Colour channels live in 0..1 (or the attached min/max).
				(0.0f64, 1.0f64)
			} else {
				default_range(param.value_type)
			};
			let mut spins = Vec::new();
			for channel in 0..count {
				let value = components.get(channel).copied().unwrap_or(0.0).clamp(min, max);
				let model = SliderModel::new(ValueKind::Float, min, max, 0.001, value);
				let spin = cx.new(|cx| SpinBox::new(*next_id, model, window, cx));
				*next_id += 1;
				spins.push((spin, channel));
			}
			ControlKind::Spin(spins)
		}
		ValueType::PushButton => ControlKind::PushButton,
		// Custom / binary and anything without an editable control: a
		// read-only line (or nothing).
		_ => ControlKind::ReadOnly(SharedString::new("")),
	};

	ParamControl {
		input_id: param.input_id.clone(),
		display_name: param.display_name.clone(),
		section: crate::oakui::effectchain::ui_section_of(param),
		kind,
	}
}

/// Wires every control's events to the engine's `set_effect_param` /
/// `effect_push_button`.
fn wire_controls<E: AppEngine>(view: &OfxParamsView<E>, cx: &mut Context<OfxParamsView<E>>) {
	for control in &view.controls {
		let input_id = control.input_id.clone();
		let effect = view.effect;
		let engine = view.engine.clone();
		match &control.kind {
			ControlKind::Slider(slider) => {
				let slider = slider.clone();
				cx.subscribe(&slider, move |_, _, event: &gpui_widgets::slider::SliderEvent, cx| {
					if let gpui_widgets::slider::SliderEvent::ValueChanged { value, .. } = event {
						let nv = match value {
							SliderValue::Integer(v) => NodeValue::Int(*v),
							_ => NodeValue::Float(value.to_f64()),
						};
						engine.update(cx, |engine, cx| {
							let _ = engine.set_effect_param(effect, &input_id, nv, cx);
						});
					}
				})
				.detach();
			}
			ControlKind::CheckBox(check) => {
				let check = check.clone();
				cx.subscribe(&check, move |_, _, event: &CheckBoxEvent, cx| {
					let CheckBoxEvent::Toggled { state, .. } = event;
					let nv = NodeValue::Boolean(*state == CheckState::Checked);
					engine.update(cx, |engine, cx| {
						let _ = engine.set_effect_param(effect, &input_id, nv, cx);
					});
				})
				.detach();
			}
			ControlKind::Combo(combo) => {
				let combo = combo.clone();
				cx.subscribe(&combo, move |_, _, event: &ComboBoxEvent, cx| {
					if let ComboBoxEvent::Selected { value, .. } = event {
						// Integer combos carry the index; string combos map
						// the picked option back to its string value.
						let param = engine.update(cx, |engine, cx| {
							engine
								.effect_params(effect)
								.unwrap_or_default()
								.into_iter()
								.find(|p| p.input_id == input_id)
						});
						let nv = match &param {
							Some(p) if p.value_type == ValueType::StrCombo => {
								let values = crate::oakui::effectchain::combo_values(p);
								let haystack = if values.is_empty() {
									crate::oakui::effectchain::combo_options(p)
								} else {
									values
								};
								NodeValue::StrCombo(
									haystack.get(*value).cloned().unwrap_or_default(),
								)
							}
							_ => NodeValue::Combo(*value as i64),
						};
						engine.update(cx, |engine, cx| {
							let _ = engine.set_effect_param(effect, &input_id, nv, cx);
						});
					}
				})
				.detach();
			}
			ControlKind::Spin(spins) => {
				let spins = spins.clone();
				for (spin, channel) in spins {
					let spin = spin.clone();
					let channel = channel;
					// Clone per iteration: each spinbox's closure owns its
					// own engine / input id.
					let engine = engine.clone();
					let input_id = input_id.clone();
					cx.subscribe(&spin, move |_, _, event: &SpinBoxEvent, cx| {
						if let SpinBoxEvent::ValueChanged { value, .. } = event {
							// Re-read the current value, patch the changed
							// component, and write the whole value back.
							let patched = engine.update(cx, |engine, cx| {
								let params = engine.effect_params(effect).unwrap_or_default();
								let current = params
									.iter()
									.find(|p| p.input_id == input_id)
									.map(|p| p.value.clone())
									.unwrap_or(NodeValue::None);
								let patched = patch_component(&current, channel, value.to_f64());
								engine.set_effect_param(effect, &input_id, patched, cx).is_ok()
							});
							let _ = patched;
						}
					})
					.detach();
				}
			}
			ControlKind::Text(_editor) => {
				// The text field commits explicitly (the commit button in the
				// row). No event subscription here: the params view is rebuilt
				// on every card render, so committing on TextChanged would
				// re-enter the engine update on the same frame the value is
				// re-synced (an endless re-render loop).
			}
			ControlKind::PushButton | ControlKind::ReadOnly(_) => {}
		}
	}
}

/// Returns `value` with the component at `channel` replaced by `component`.
fn patch_component(value: &NodeValue, channel: usize, component: f64) -> NodeValue {
	let mut out = value.clone();
	match &mut out {
		NodeValue::Vec2(v) if channel < 2 => v[channel] = component,
		NodeValue::Vec3(v) if channel < 3 => v[channel] = component,
		NodeValue::Vec4(v) if channel < 4 => v[channel] = component,
		NodeValue::Color(v) if channel < 4 => v[channel] = component,
		_ => {}
	}
	out
}

impl<E: AppEngine> Render for OfxParamsView<E> {
	fn render(&mut self, _window: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
		let colors = cx.default_colors().clone();
		self.sync_values(cx);

		let mut body = div().flex().flex_col().gap_1().p_2();
		let mut last_section: Option<(String, String)> = None;
		for control in &self.controls {
			if control.section != last_section {
				last_section = control.section.clone();
				if let Some((group, page)) = &last_section {
					let title = if page.is_empty() {
						group.clone()
					} else if group.is_empty() {
						page.clone()
					} else {
						format!("{group} · {page}")
					};
					body = body.child(
						div()
							.py_1()
							.text_xs()
							.font_weight(gpui::FontWeight(600.0))
							.text_color(colors.selected)
							.child(title),
					);
				}
			}

			// A push button renders full-width with its own label (the OFX
			// param label IS the button text); every other control gets the
			// label column + control layout.
			let row_element: gpui::AnyElement = if matches!(control.kind, ControlKind::PushButton) {
				let engine = self.engine.clone();
				let effect = self.effect;
				let input_id = control.input_id.clone();
				let button_label = control.display_name.clone();
				div()
					.id(SharedString::from(format!("ofx-push-{}", control.input_id)))
					.flex_1()
					.cursor_pointer()
					.rounded_sm()
					.border_1()
					.border_color(colors.border)
					.bg(colors.selected)
					.text_sm()
					.text_color(colors.text)
					.text_center()
					.py_1()
					.child(button_label)
					.on_click(move |_event: &ClickEvent, _window, cx| {
						engine.update(cx, |engine, cx| {
							let _ = engine.effect_push_button(effect, &input_id, cx);
						});
					})
					.into_any_element()
			} else {
				let label = div()
					.flex_shrink_0()
					.w(px(110.0))
					.text_sm()
					.text_color(colors.text)
					.child(control.display_name.clone());
				let widget = match &control.kind {
					ControlKind::Slider(slider) => div().flex_1().child(slider.clone()).into_any_element(),
					ControlKind::CheckBox(check) => div().flex_1().child(check.clone()).into_any_element(),
					ControlKind::Combo(combo) => div().flex_1().child(combo.clone()).into_any_element(),
					ControlKind::Spin(spins) => {
						let mut row = div().flex_1().flex().gap_1();
						for (spin, _) in spins {
							row = row.child(div().flex_1().child(spin.clone()));
						}
						row.into_any_element()
					}
					ControlKind::Text(editor) => {
						let weak = editor.downgrade();
						let engine = self.engine.clone();
						let effect = self.effect;
						let input_id = control.input_id.clone();
						let editor_commit = editor.clone();
						div()
							.flex_1()
							.flex()
							.gap_1()
							.child(
								div()
									.flex_1()
									.rounded_md()
									.border_1()
									.border_color(colors.border)
									.bg(colors.background)
									.px_2()
									.py_1()
									.child(text_input(format!("ofx-param-{}", control.input_id)).state(weak).accepts_input(true)),
							)
							.child(
								// Explicit commit: reads the field and pushes the
								// string to the engine (avoids the re-render loop of
								// committing on every keystroke).
								div()
									.id(SharedString::from(format!("ofx-commit-{}", control.input_id)))
									.cursor_pointer()
									.rounded_sm()
									.border_1()
									.border_color(colors.border)
									.bg(colors.selected)
									.text_sm()
									.text_color(colors.text)
									.px_2()
									.py_1()
									.child("✓")
									.on_click(move |_event: &ClickEvent, _window, cx| {
										let text = editor_commit.read(cx).as_str().to_string();
										engine.update(cx, |engine, cx| {
											let _ = engine
												.set_effect_param(effect, &input_id, NodeValue::Text(text), cx);
										});
									}),
							)
							.into_any_element()
					}
					ControlKind::ReadOnly(text) => div()
						.flex_1()
						.text_sm()
						.text_color(colors.disabled)
						.child(text.clone())
						.into_any_element(),
					ControlKind::PushButton => unreachable!("handled above"),
				};
				div()
					.id(SharedString::from(format!("ofx-param-{}", control.input_id)))
					.flex()
					.items_center()
					.gap_2()
					.child(label)
					.child(widget)
					.into_any_element()
			};
			body = body.child(row_element);
		}
		if self.controls.is_empty() {
			body = body.child(
				div()
					.text_sm()
					.text_color(colors.disabled)
					.child(crate::i18n::tr("inspector.params")),
			);
		}
		body
	}
}
