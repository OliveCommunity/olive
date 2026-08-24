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

//! The app's editor controls: slider, spin box, check box, combo box and
//! the colour picker, themed like the rest of the UI.
//!
//! # Interaction contract
//!
//! Every control answers to the mouse **and** the keyboard once focused:
//!
//! - [`Slider`] — horizontal drag (the gpui_widgets slider only reacted
//!   to *vertical* cursor movement, so a normal horizontal drag looked
//!   dead), click-to-jump, wheel, and the arrow keys (Shift = fine steps,
//!   Home/End = range ends).
//! - [`SpinBox`] / [`ComboBox`] / [`CheckBox`] — the gpui_widgets
//!   implementations, re-exported here so the app never reaches into
//!   gpui_widgets directly.
//! - [`OfxColorPicker`] — the colour swatch + deferred popup picker:
//!   channel sliders, hex field, Cancel/OK; only OK commits (a drag
//!   session is a single undo row).
//!
//! Colours come from `App::default_colors` (the app theme), matching the
//! text inputs and menus.

use gpui::{
	colors::{Colors, DefaultColors},
	div, px, App, Context, ElementId, EventEmitter, FocusHandle, IntoElement, InteractiveElement, ParentElement,
	Entity, KeyDownEvent, MouseButton, MouseDownEvent, MouseMoveEvent, MouseUpEvent, Pixels, Point,
	Render, ScrollWheelEvent, SharedString, StatefulInteractiveElement, Styled, Window, AppContext, Focusable,
};
use std::sync::Arc;

pub use gpui_widgets::value::{SliderValue, ValueKind};

/// The colour swatch + deferred popup picker (the params panel's
/// implementation, surfaced here so effect controls live under
/// `oakui::component`).
pub use crate::panels::ofx_params::OfxColorPicker;

/// The value model of a [`Slider`] (range, step, current value).
pub use gpui_widgets::slider::SliderModel;

/// Events emitted by a [`Slider`].
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum SliderEvent {
	/// The value changed (drag, wheel, click, arrow key).
	ValueChanged {
		/// The slider's stable control id.
		control: usize,
		/// The new value.
		value: SliderValue,
	},
	/// A drag gesture started.
	DragStarted { control: usize },
	/// A drag gesture ended.
	DragFinished { control: usize },
}

/// The number of horizontal pixels that maps to the full slider range.
const HORIZONTAL_DRAG_RANGE_PX: f32 = 240.0;

/// A horizontal slider: track + filled portion + handle.
///
/// Mouse: press on the track jumps the value to the click position, a
/// drag adjusts continuously (Shift = fine), the wheel steps, middle
/// click resets.
/// Keyboard (once focused): Left/Right step by one step, Shift-Left/Right
/// by 1/10 step, Home/End jump to the range ends.
/// The in-flight drag gesture payload shared with the drag events.
struct SliderDrag {
	/// The cursor x of the first drag-move (the drag origin).
	first_x: f32,
	/// The model fraction when the drag started.
	origin_fraction: f64,
	/// Whether this is the first move (the origin is set then).
	first: bool,
}

/// The invisible ghost that accompanies a slider drag (gpui requires one
/// for `on_drag`).
struct SliderDragGhost;

impl Render for SliderDragGhost {
	fn render(&mut self, _window: &mut Window, _cx: &mut gpui::Context<Self>) -> impl IntoElement {
		div()
	}
}

/// A horizontal slider: track + filled portion + handle.
///
/// Mouse: a drag adjusts the value with 1:1 cursor tracking (the handle
/// follows the cursor; Shift = 1/10 sensitivity), the wheel steps, middle
/// click resets, double click opens a numeric editor.
/// Keyboard (once focused): Left/Right step by one step, Shift-Left/Right
/// by 1/10 step, Home/End jump to the range ends.
///
/// A gesture (drag, wheel tick or arrow key) emits [`SliderEvent::ValueChanged`]
/// exactly once — on drop for drags — so the host applies one undoable
/// edit per gesture instead of one per mouse move.
pub struct Slider {
	control: usize,
	model: SliderModel,
	focus: FocusHandle,
	/// The numeric editor, while a double-click edit is open.
	edit: Option<Entity<gpui_elements::editable_text::EditableTextState>>,
	/// True while a drag is in flight (to report start/finish).
	dragging: bool,
	/// Whether the open editor should cancel (Escape) instead of commit
	/// on blur.
	edit_cancel: bool,
}

impl Slider {
	/// Create a slider for `control` over `model`.
	pub fn new(
		control: usize,
		model: SliderModel,
		window: &mut Window,
		cx: &mut Context<Self>,
	) -> Self {
		let _ = window;
		Self {
			control,
			model,
			focus: cx.focus_handle(),
			edit: None,
			dragging: false,
			edit_cancel: false,
		}
	}

	/// The current value (the params panel re-syncs external edits).
	pub fn set_value(&mut self, value: SliderValue) {
		self.model.set_value(value);
	}

	/// The current value.
	pub fn value(&self) -> SliderValue {
		self.model.value()
	}

	fn emit_changed(&mut self, cx: &mut Context<Self>) {
		cx.emit(SliderEvent::ValueChanged {
			control: self.control,
			value: self.model.value(),
		});
		cx.notify();
	}

	/// Open the numeric editor with the current value as its text.
	fn begin_edit(&mut self, window: &mut Window, cx: &mut Context<Self>) {
		let text = self.display();
		let editor = cx.new(|cx| {
			gpui_elements::editable_text::EditableTextState::new(
				gpui_elements::editable_text::StringStorage::from(text.to_string()),
				cx,
			)
		});
		// Commit (or cancel) when the editor loses focus.
		let this_control = self.control;
		let weak = editor.downgrade();
		let focus = editor.read(cx).focus_handle(cx);
		cx.on_focus_out(&focus, window, move |this: &mut Self, _event, _window, cx| {
			if let Some(editor) = weak.upgrade() {
				let text = editor.read(cx).as_str().to_string();
				if !this.edit_cancel {
					this.apply_text(&text, cx);
				}
			}
			this.edit = None;
			this.edit_cancel = false;
			cx.notify();
		})
		.detach();
		window.focus(&editor.read(cx).focus_handle(cx), cx);
		let _ = this_control;
		self.edit = Some(editor);
		cx.notify();
	}

	/// Parse `text` as the model's value kind, clamp it and apply + emit.
	fn apply_text(&mut self, text: &str, cx: &mut Context<Self>) {
		let parsed: Option<f64> = match self.model.value() {
			SliderValue::Integer(_) => text.trim().parse::<i64>().ok().map(|v| v as f64),
			_ => text.trim().parse::<f64>().ok(),
		};
		let Some(raw) = parsed else {
			return;
		};
		let changed = self.model.apply_raw(raw);
		if changed {
			self.emit_changed(cx);
		}
	}

	/// The current value formatted for display.
	fn display(&self) -> SharedString {
		match self.model.value() {
			SliderValue::Integer(v) => v.to_string().into(),
			SliderValue::Float(v) => format_value(v).into(),
			SliderValue::Angle(v) => format!("{v:.1}°").into(),
			SliderValue::Rational(r) => format!("{}/{}", r.num(), r.den()).into(),
		}
	}
}

impl EventEmitter<SliderEvent> for Slider {}

impl Render for Slider {
	fn render(&mut self, window: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
		let colors = cx.default_colors().clone();
		let fraction = self.model.fraction();
		let control = self.control;
		let handle_x = (fraction * 100.0).clamp(0.0, 100.0) as f32;
		let drag_payload = std::sync::Arc::new(std::sync::RwLock::new(SliderDrag {
			first_x: 0.0,
			origin_fraction: 0.0,
			first: true,
		}));

		let mut track = div()
			.id(gpui::ElementId::named_usize("oak-slider", control))
			.flex_1()
			.h(px(18.0))
			.relative()
			.rounded_md()
			.bg(colors.container)
			.track_focus(&self.focus)
			.cursor_pointer()
			.on_drag(drag_payload.clone(), |_payload, _offset, _window, cx| {
				cx.new(|_| SliderDragGhost)
			})
			.on_drag_move(
				cx.listener(|this, event: &gpui::DragMoveEvent<std::sync::Arc<std::sync::RwLock<SliderDrag>>>, _window, cx| {
					{
						let mut drag = event.drag(cx).write().unwrap();
						if drag.first {
							drag.first = false;
							drag.first_x = f32::from(event.event.position.x);
							drag.origin_fraction = this.model.fraction();
						}
					}
					if !this.dragging {
						this.dragging = true;
						cx.emit(SliderEvent::DragStarted { control: this.control });
					}
					let drag = event.drag(cx).read().unwrap();
					let fine = event.event.modifiers.shift;
					let scale = if fine { 0.1 } else { 1.0 };
					let width = event.bounds.size.width;
					let t = if f32::from(width) > 0.0 {
						(drag.origin_fraction
							+ (f32::from(event.event.position.x) - drag.first_x) as f64
								/ f32::from(width) as f64 * scale)
							.clamp(0.0, 1.0)
					} else {
						drag.origin_fraction
					};
					drop(drag);
					this.model.set_fraction(t);
					cx.notify();
				}),
			)
			.on_drop(
				cx.listener(|this, _drag: &std::sync::Arc<std::sync::RwLock<SliderDrag>>, _window, cx| {
					if this.dragging {
						this.dragging = false;
						this.emit_changed(cx);
						cx.emit(SliderEvent::DragFinished { control: this.control });
					}
				}),
			)
			.on_click(cx.listener(|this, event: &gpui::ClickEvent, window, cx| {
				if event.click_count() >= 2 {
					this.begin_edit(window, cx);
				}
			}))
			.on_mouse_down(
				MouseButton::Middle,
				cx.listener(|this, _event: &MouseDownEvent, _window, cx| {
					let changed = this.model.reset();
					if changed {
						this.emit_changed(cx);
					}
				}),
			)
			.on_scroll_wheel(cx.listener(|this, event: &ScrollWheelEvent, _window, cx| {
				let dir = match event.delta {
					gpui::ScrollDelta::Pixels(p) => {
						if f32::from(p.y) > 0.0 { 1 } else { -1 }
					}
					gpui::ScrollDelta::Lines(l) => {
						if l.y > 0.0 { 1 } else { -1 }
					}
				};
				let fine = event.modifiers.shift;
				let changed = this.model.apply_step(dir, fine);
				if changed {
					this.emit_changed(cx);
				}
			}))
			.on_key_down(cx.listener(|this, event: &KeyDownEvent, _window, cx| {
				// While the numeric editor is open, Escape cancels the edit
				// (blur still fires; edit_cancel makes it restore).
				if this.edit.is_some() {
					if event.keystroke.key.as_str() == "escape" {
						this.edit_cancel = true;
					}
					return;
				}
				let fine = event.keystroke.modifiers.shift;
				let changed = match event.keystroke.key.as_str() {
					"left" => this.model.apply_step(-1, fine),
					"right" => this.model.apply_step(1, fine),
					"home" => this.model.set_fraction(0.0),
					"end" => this.model.set_fraction(1.0),
					_ => return,
				};
				if changed {
					this.emit_changed(cx);
				}
				cx.stop_propagation();
			}));

		if let Some(editor) = self.edit.clone() {
			// Numeric editor: an app text input bound to the editor state.
			track = track.child(
				crate::oakui::component::text_input(
					gpui::ElementId::named_usize("oak-slider-edit", control),
					cx,
				)
				.state(editor.downgrade())
				.accepts_input(true),
			);
			return track;
		}

		track
			.child(
				div()
					.absolute()
					.left(px(0.0))
					.top(px(0.0))
					.bottom(px(0.0))
					.w(px(handle_x))
					.rounded_md()
					.bg(colors.selected),
			)
			.child(
				div()
					.absolute()
					.left(px(handle_x))
					.top(px(3.0))
					.size(px(12.0))
					.rounded_full()
					.bg(colors.text),
			)
	}
}

/// Format a float without trailing zeros ("0.5", "1", "0.25").
fn format_value(v: f64) -> String {
	if v.fract() == 0.0 {
		format!("{v:.0}")
	} else {
		format!("{v:.3}")
			.trim_end_matches('0')
			.trim_end_matches('.')
			.to_string()
	}
}

// ---------------------------------------------------------------------------
// Check box
// ---------------------------------------------------------------------------

/// A checkbox state.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CheckState {
	/// The box is empty.
	Unchecked,
	/// The box is filled with a check mark.
	Checked,
	/// The box shows a horizontal bar (partially checked).
	Indeterminate,
}

/// Events emitted by a [`CheckBox`].
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CheckBoxEvent {
	/// The user clicked (or pressed space/enter on) the box — the state
	/// the control *would* move to. The host applies it through its model
	/// and calls [`CheckBox::set_state`] when it accepts.
	Toggled {
		/// The control's stable id.
		control: usize,
		/// The state the control should move to.
		state: CheckState,
	},
}

/// A checkbox row: box + check mark.
///
/// Mouse: left click toggles. Keyboard (once focused): Space / Enter
/// toggle. Request-only: the widget never changes its own state on
/// interaction — it emits [`CheckBoxEvent::Toggled`] and the host calls
/// [`CheckBox::set_state`] to accept.
pub struct CheckBox {
	control: usize,
	state: CheckState,
	label: Option<SharedString>,
	focus: FocusHandle,
}

impl CheckBox {
	/// Create a checkbox for `control` in `state`.
	pub fn new(
		control: usize,
		state: CheckState,
		_window: &mut Window,
		cx: &mut Context<Self>,
	) -> Self {
		Self {
			control,
			state,
			label: None,
			focus: cx.focus_handle(),
		}
	}

	/// A label shown next to the box.
	pub fn with_label(mut self, label: impl Into<SharedString>) -> Self {
		self.label = Some(label.into());
		self
	}

	/// The current state.
	pub fn state(&self) -> CheckState {
		self.state
	}

	/// The host's acceptance path: apply `state` (also repaints).
	pub fn set_state(&mut self, state: CheckState, _cx: &mut Context<Self>) {
		self.state = state;
	}

	fn request_toggle(&mut self, cx: &mut Context<Self>) {
		let next = match self.state {
			CheckState::Checked => CheckState::Unchecked,
			_ => CheckState::Checked,
		};
		cx.emit(CheckBoxEvent::Toggled {
			control: self.control,
			state: next,
		});
	}
}

impl EventEmitter<CheckBoxEvent> for CheckBox {}

impl Render for CheckBox {
	fn render(&mut self, _window: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
		let colors = cx.default_colors().clone();
		let checked = self.state == CheckState::Checked;
		let control = self.control;

		let label = self.label.clone();
		div()
			.id(ElementId::named_usize("oak-checkbox", control))
			.flex()
			.items_center()
			.gap_1()
			.cursor_pointer()
			.child(
				div()
					.size(px(16.0))
					.rounded_md()
					.border_1()
			.border_color(colors.border)
			.bg(if checked { colors.selected } else { colors.background })
			.track_focus(&self.focus)
			.cursor_pointer()
			.on_mouse_down(
				MouseButton::Left,
				cx.listener(|this, _event: &MouseDownEvent, window, cx| {
					window.focus(&this.focus, cx);
					this.request_toggle(cx);
				}),
			)
			.on_key_down(cx.listener(|this, event: &KeyDownEvent, _window, cx| {
				if !matches!(event.keystroke.key.as_str(), "space" | "enter") {
					return;
				}
				this.request_toggle(cx);
				cx.stop_propagation();
			}))
					.child(if checked {
						div()
							.w_full()
							.h_full()
							.flex()
							.items_center()
							.justify_center()
							.child(div().text_color(colors.selected_text).child("✓"))
							.into_any_element()
					} else {
						div().into_any_element()
					}),
			)
			.child(
				label
					.map(|l| div().text_color(colors.text).child(l).into_any_element())
					.unwrap_or_else(|| div().into_any_element()),
			)
	}
}

// ---------------------------------------------------------------------------
// Combo box
// ---------------------------------------------------------------------------

/// One combo-box option: its index and display label.
#[derive(Debug, Clone)]
pub struct ComboBoxOption {
	/// The option's value (what [`ComboBoxEvent::Selected`] reports).
	pub index: usize,
	/// The display label.
	pub label: SharedString,
}

impl ComboBoxOption {
	/// Create an option with `index` and `label`.
	pub fn new(index: usize, label: impl Into<SharedString>) -> Self {
		Self {
			index,
			label: label.into(),
		}
	}
}

/// Events emitted by a [`ComboBox`].
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ComboBoxEvent {
	/// An option was selected (popup click or arrow-key commit).
	Selected { value: usize },
}

/// A combo box: a button showing the selected label that opens a popup
/// list.
///
/// Mouse: click the button to open/close; click an option to select it.
/// Keyboard (once focused): Up/Down move the selection (when the popup
/// is open they move the highlight, otherwise they change the selection
/// directly), Enter commits the highlighted option (or opens the popup
/// when closed), Escape closes the popup.
pub struct ComboBox {
	control: usize,
	options: Vec<ComboBoxOption>,
	selected: Option<usize>,
	placeholder: SharedString,
	open: bool,
	highlight: usize,
	focus: FocusHandle,
}

impl ComboBox {
	/// Create a combo box for `control` with `options`.
	pub fn new(
		control: usize,
		options: Vec<ComboBoxOption>,
		_window: &mut Window,
		cx: &mut Context<Self>,
	) -> Self {
		Self {
			control,
			options,
			selected: None,
			placeholder: SharedString::default(),
			open: false,
			highlight: 0,
			focus: cx.focus_handle(),
		}
	}

	/// The placeholder shown while nothing is selected.
	pub fn with_placeholder(mut self, placeholder: impl Into<SharedString>) -> Self {
		self.placeholder = placeholder.into();
		self
	}

	/// The selected option index.
	pub fn selected(&self) -> Option<usize> {
		self.selected
	}

	/// The selected option index (external sync; also repaints).
	pub fn set_selected(&mut self, selected: Option<usize>, _cx: &mut Context<Self>) {
		self.selected = selected;
	}

	fn select(&mut self, index: usize, cx: &mut Context<Self>) {
		self.selected = Some(index);
		self.open = false;
		cx.emit(ComboBoxEvent::Selected { value: index });
		cx.notify();
	}

	fn move_selection(&mut self, dir: i32, cx: &mut Context<Self>) {
		if self.options.is_empty() {
			return;
		}
		let current = self.selected.unwrap_or(0);
		let next = ((current as i32 + dir).rem_euclid(self.options.len() as i32)) as usize;
		self.select(next, cx);
	}
}

impl EventEmitter<ComboBoxEvent> for ComboBox {}

impl Render for ComboBox {
	fn render(&mut self, _window: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
		let colors = cx.default_colors().clone();
		let control = self.control;
		let label = self
			.selected
			.and_then(|i| self.options.get(i))
			.map(|o| o.label.clone())
			.unwrap_or_else(|| self.placeholder.clone());
		let open = self.open;
		let options = self.options.clone();
		let highlight = self.highlight;
		let selected = self.selected;

		let mut root = div()
			.id(ElementId::named_usize("oak-combo", control))
			.relative()
			.rounded_md()
			.border_1()
			.border_color(colors.border)
			.bg(colors.background)
			.px_2()
			.py_1()
			.track_focus(&self.focus)
			.cursor_pointer()
			.text_color(colors.text)
			.child(label.clone())
			.on_mouse_down(
				MouseButton::Left,
				cx.listener(|this, _event: &MouseDownEvent, window, cx| {
					window.focus(&this.focus, cx);
					this.open = !this.open;
					this.highlight = this.selected.unwrap_or(0);
					cx.notify();
				}),
			)
			.on_key_down(cx.listener(|this, event: &KeyDownEvent, _window, cx| {
				match event.keystroke.key.as_str() {
					"down" | "up" => {
						let dir = if event.keystroke.key.as_str() == "down" { 1 } else { -1 };
						if this.open {
							if !this.options.is_empty() {
								this.highlight = ((this.highlight as i32 + dir)
									.rem_euclid(this.options.len() as i32))
									as usize;
								cx.notify();
							}
						} else {
							this.move_selection(dir, cx);
						}
					}
					"enter" | "space" => {
						if this.open {
							if let Some(opt) = this.options.get(this.highlight) {
								this.select(opt.index, cx);
							}
						} else {
							this.open = true;
							this.highlight = this.selected.unwrap_or(0);
							cx.notify();
						}
					}
					"escape" => {
						if this.open {
							this.open = false;
							cx.notify();
						}
					}
					_ => return,
				}
				cx.stop_propagation();
			}));

		if open {
			// deferred(): the popup must paint AFTER the rows that follow the
			// combo in the dialog, otherwise they draw over it and the list
			// looks transparent (the preferences render-backend dropdown
			// regression).
			root = root.child(
				gpui::deferred(
					div()
						.absolute()
						.left(px(0.0))
						.right(px(0.0))
						.top(px(22.0))
						.rounded_md()
						.border_1()
						.border_color(colors.border)
						.bg(colors.background)
						.shadow_md()
						.py_1()
						.children(options.iter().enumerate().map(|(i, opt)| {
						let highlighted = i == highlight;
						div()
							.px_2()
							.py_1()
							.bg(if highlighted { colors.selected } else { colors.background })
							.text_color(if highlighted {
								colors.selected_text
							} else {
								colors.text
							})
							.cursor_pointer()
							.on_mouse_down(
								MouseButton::Left,
								{
									let opt = opt.clone();
									cx.listener(move |this, _event: &MouseDownEvent, window, cx| {
										window.focus(&this.focus, cx);
										this.select(opt.index, cx);
									})
								},
							)
							.child(opt.label.clone())
					})),
				),
			);
		}
		let _ = selected;
		root
	}
}

// ---------------------------------------------------------------------------
// Spin box
// ---------------------------------------------------------------------------

/// Events emitted by a [`SpinBox`].
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum SpinBoxEvent {
	/// The value changed (wheel or arrow key).
	ValueChanged {
		/// The control's stable id.
		control: usize,
		/// The new value.
		value: SliderValue,
	},
	/// Direct text entry was committed (the field is wheel/arrow driven
	/// for now, so this is only emitted by hosts that add a text editor).
	EditCommitted {
		/// The control's stable id.
		control: usize,
		/// The committed value.
		value: SliderValue,
	},
}

/// A numeric field over a [`SliderModel`].
///
/// Mouse: click focuses, the wheel steps (Shift = fine). Keyboard (once
/// focused): Up/Down step, Shift-Up/Down fine-step, Home/End jump to the
/// range ends.
pub struct SpinBox {
	control: usize,
	model: SliderModel,
	focus: FocusHandle,
}

impl SpinBox {
	/// Create a spin box for `control` over `model`.
	pub fn new(
		control: usize,
		model: SliderModel,
		_window: &mut Window,
		cx: &mut Context<Self>,
	) -> Self {
		Self {
			control,
			model,
			focus: cx.focus_handle(),
		}
	}

	/// The current value (external sync).
	pub fn set_value(&mut self, value: SliderValue, _cx: &mut Context<Self>) {
		self.model.set_value(value);
	}

	/// The current value.
	pub fn value(&self) -> SliderValue {
		self.model.value()
	}

	fn emit_changed(&mut self, changed: bool, cx: &mut Context<Self>) {
		if changed {
			cx.emit(SpinBoxEvent::ValueChanged {
				control: self.control,
				value: self.model.value(),
			});
			cx.notify();
		}
	}

	/// Format the current value for display.
	fn display(&self) -> SharedString {
		match self.model.value() {
			SliderValue::Integer(v) => v.to_string().into(),
			SliderValue::Float(v) => format_value(v).into(),
			SliderValue::Angle(v) => format!("{v:.1}°").into(),
			SliderValue::Rational(r) => format!("{}/{}", r.num(), r.den()).into(),
		}
	}
}

impl EventEmitter<SpinBoxEvent> for SpinBox {}

impl Render for SpinBox {
	fn render(&mut self, _window: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
		let colors = cx.default_colors().clone();
		let control = self.control;
		let text = self.display();

		div()
			.id(ElementId::named_usize("oak-spinbox", control))
			.rounded_md()
			.border_1()
			.border_color(colors.border)
			.bg(colors.background)
			.px_2()
			.py_1()
			.track_focus(&self.focus)
			.cursor_text()
			.text_color(colors.text)
			.child(text)
			.on_mouse_down(
				MouseButton::Left,
				cx.listener(|this, _event: &MouseDownEvent, window, cx| {
					window.focus(&this.focus, cx);
				}),
			)
			.on_scroll_wheel(cx.listener(|this, event: &ScrollWheelEvent, _window, cx| {
				let dir = match event.delta {
					gpui::ScrollDelta::Pixels(p) => {
						if f32::from(p.y) > 0.0 { 1 } else { -1 }
					}
					gpui::ScrollDelta::Lines(l) => {
						if l.y > 0.0 { 1 } else { -1 }
					}
				};
				let fine = event.modifiers.shift;
				let changed = this.model.apply_step(dir, fine);
				this.emit_changed(changed, cx);
			}))
			.on_key_down(cx.listener(|this, event: &KeyDownEvent, _window, cx| {
				let fine = event.keystroke.modifiers.shift;
				let changed = match event.keystroke.key.as_str() {
					"up" => this.model.apply_step(1, fine),
					"down" => this.model.apply_step(-1, fine),
					"home" => this.model.set_fraction(0.0),
					"end" => this.model.set_fraction(1.0),
					_ => return,
				};
				this.emit_changed(changed, cx);
				cx.stop_propagation();
			}))
	}
}
