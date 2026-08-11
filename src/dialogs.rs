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

//! The content views of the app's modal dialogs: preferences (renderer
//! backend + language) and export (format + output path).
//!
//! Each view owns its widgets and emits nothing itself — the host
//! (`crate::app::OakApp`) reads the state (format / path) when a dialog
//! button is clicked, and the preferences view writes its choices straight
//! through the config C ABI on selection.

use gpui::colors::DefaultColors;
use gpui::prelude::*;
use gpui::{div, App, Context, Entity, Render, SharedString, Window};
use gpui_elements::editable_text::{text_input, EditableTextState, StringStorage};
use gpui_widgets::combo_box::{ComboBox, ComboBoxEvent, ComboBoxOption};

use crate::i18n;
use crate::oakui::real::{
	config_get_string, config_set_string, encoding_formats, renderer_backends,
	CONFIG_KEY_RENDERER_BACKEND, EXPORT_FORMAT_MP4,
};

// ---------------------------------------------------------------------------
// Preferences
// ---------------------------------------------------------------------------

/// The preferences dialog content: the renderer backend and the language
/// dropdowns. Both write through the config C ABI on selection, so the
/// choices survive restarts.
pub struct PreferencesContent {
	backend: Entity<ComboBox>,
	language: Entity<ComboBox>,
	/// The backend options, in display order.
	backends: Vec<&'static str>,
}

impl PreferencesContent {
	/// Builds the content: reads the current config values and seeds the
	/// dropdowns.
	pub fn new(window: &mut Window, cx: &mut Context<Self>) -> Self {
		let backends = renderer_backends();
		let current_backend = config_get_string(CONFIG_KEY_RENDERER_BACKEND);
		let backend_selected = backends
			.iter()
			.position(|b| b.eq_ignore_ascii_case(&current_backend))
			.unwrap_or(0);
		let backend_options = backends
			.iter()
			.enumerate()
			.map(|(i, name)| ComboBoxOption::new(i, backend_label(name)))
			.collect();
		let backend = cx.new(|cx| {
			ComboBox::new(1, backend_options, window, cx)
				.with_placeholder(i18n::tr("preferences.backend.placeholder"))
		});
		cx.subscribe(&backend, |this, _combo, event: &ComboBoxEvent, cx| {
			if let ComboBoxEvent::Selected { value, .. } = event {
				if let Some(name) = this.backends.get(*value) {
					config_set_string(CONFIG_KEY_RENDERER_BACKEND, name);
					println!("[preferences] renderer backend → {name}");
				}
			}
			let _ = cx;
		})
		.detach();
		backend.update(cx, |combo, cx| {
			combo.set_selected(Some(backend_selected), cx)
		});

		let language_options = vec![
			ComboBoxOption::new(0, "English (en-US)"),
			ComboBoxOption::new(1, "简体中文 (zh-CN)"),
		];
		let language = cx.new(|cx| {
			ComboBox::new(2, language_options, window, cx)
				.with_placeholder(i18n::tr("preferences.language.placeholder"))
		});
		let language_selected = match crate::i18n::language() {
			crate::i18n::Language::EnUs => 0,
			crate::i18n::Language::ZhCN => 1,
		};
		cx.subscribe(&language, |_this, _combo, event: &ComboBoxEvent, cx| {
			if let ComboBoxEvent::Selected { value, .. } = event {
				let language = match *value {
					1 => crate::i18n::Language::ZhCN,
					_ => crate::i18n::Language::EnUs,
				};
				crate::i18n::set_language(language);
			}
			let _ = cx;
		})
		.detach();
		language.update(cx, |combo, cx| {
			combo.set_selected(Some(language_selected), cx)
		});

		Self {
			backend,
			language,
			backends,
		}
	}
}

/// A display label for a renderer backend id.
fn backend_label(name: &str) -> String {
	match name {
		"opengl" => "OpenGL",
		"metal" => "Metal",
		"vulkan" => "Vulkan",
		"none" => "None (off)",
		other => other,
	}
	.to_string()
}

/// A labeled form row: a small caption above the widget.
fn form_row(
	colors: &gpui::colors::Colors,
	label: SharedString,
	widget: impl IntoElement,
) -> gpui::Div {
	div()
		.flex()
		.flex_col()
		.gap_1()
		.child(div().text_color(colors.text).child(label))
		.child(widget)
}

impl Render for PreferencesContent {
	fn render(&mut self, _window: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
		let colors = cx.default_colors().clone();
		div()
			.flex()
			.flex_col()
			.gap_3()
			.w_full()
			.child(form_row(
				&colors,
				i18n::tr("preferences.backend").into(),
				self.backend.clone(),
			))
			.child(form_row(
				&colors,
				i18n::tr("preferences.language").into(),
				self.language.clone(),
			))
			.child(
				div()
					.text_color(colors.disabled)
					.text_xs()
					.child(i18n::tr("preferences.hint")),
			)
	}
}

// ---------------------------------------------------------------------------
// Export
// ---------------------------------------------------------------------------

/// A text field with the same shape as the file dialog's path field.
pub struct PathField {
	editor: Entity<EditableTextState>,
}

impl PathField {
	/// The path currently entered.
	pub fn path(&self, app: &App) -> SharedString {
		self.editor.read(app).as_str().into()
	}

	/// Replaces the path shown in the field.
	pub fn set_path(&mut self, path: impl Into<SharedString>, cx: &mut Context<Self>) {
		let path = path.into();
		self.editor.update(cx, |editor, cx| {
			editor.emplace(path.as_ref(), cx);
		});
		cx.notify();
	}
}

impl Render for PathField {
	fn render(&mut self, _window: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
		let colors = cx.default_colors().clone();
		let weak = self.editor.downgrade();
		div()
			.rounded_md()
			.border_1()
			.border_color(colors.border)
			.bg(colors.background)
			.px_2()
			.py_1()
			.child(
				text_input("gpui-widgets-export-path")
					.state(weak)
					.accepts_input(true),
			)
	}
}

/// The export dialog content: the container-format dropdown and the output
/// path field.
pub struct ExportDialogContent {
	format: Entity<ComboBox>,
	path: Entity<PathField>,
	/// (format id, display name, extension) in dropdown order.
	formats: Vec<(i32, String, String)>,
}

impl ExportDialogContent {
	/// Builds the content: the format list comes from the oakcodec encoding
	/// enumeration (MP4 default), the path starts empty.
	pub fn new(window: &mut Window, cx: &mut Context<Self>) -> Self {
		let formats: Vec<(i32, String, String)> = encoding_formats()
			.into_iter()
			.filter(|(_, _, ext)| !ext.is_empty())
			.collect();
		let options = formats
			.iter()
			.enumerate()
			.map(|(i, (_, name, ext))| ComboBoxOption::new(i, format!("{name} (.{ext})")))
			.collect();
		let format = cx.new(|cx| {
			ComboBox::new(3, options, window, cx)
				.with_placeholder(i18n::tr("export.format.placeholder"))
		});
		let mp4_index = formats
			.iter()
			.position(|(id, _, _)| *id == EXPORT_FORMAT_MP4)
			.unwrap_or(0);
		format.update(cx, |combo, cx| combo.set_selected(Some(mp4_index), cx));

		let path = cx.new(|cx| {
			let editor = cx.new(|cx| EditableTextState::new(StringStorage::default(), cx));
			PathField { editor }
		});

		Self {
			format,
			path,
			formats,
		}
	}

	/// The selected format id.
	pub fn format(&self, cx: &App) -> i32 {
		let Some(selected) = self.format.read(cx).selected() else {
			return EXPORT_FORMAT_MP4;
		};
		self.formats
			.get(selected)
			.map(|(id, _, _)| *id)
			.unwrap_or(EXPORT_FORMAT_MP4)
	}

	/// The selected format's file extension (without the dot).
	pub fn extension(&self, cx: &App) -> String {
		let Some(selected) = self.format.read(cx).selected() else {
			return "mp4".to_string();
		};
		self.formats
			.get(selected)
			.map(|(_, _, ext)| ext.clone())
			.unwrap_or_else(|| "mp4".to_string())
	}

	/// The output path currently entered.
	pub fn path(&self, cx: &App) -> SharedString {
		self.path.read(cx).path(cx)
	}

	/// Pre-fills the output path.
	pub fn set_path(&mut self, path: impl Into<SharedString>, cx: &mut Context<Self>) {
		let path = path.into();
		self.path
			.update(cx, |content, cx| content.set_path(path, cx));
		cx.notify();
	}
}

impl Render for ExportDialogContent {
	fn render(&mut self, _window: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
		let colors = cx.default_colors().clone();
		div()
			.flex()
			.flex_col()
			.gap_3()
			.w_full()
			.child(form_row(
				&colors,
				i18n::tr("export.format").into(),
				self.format.clone(),
			))
			.child(form_row(
				&colors,
				i18n::tr("export.path").into(),
				self.path.clone(),
			))
			.child(
				div()
					.text_color(colors.disabled)
					.text_xs()
					.child(i18n::tr("export.hint")),
			)
	}
}
