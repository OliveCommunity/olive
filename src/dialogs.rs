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

//! The content views of the app's modal dialogs: preferences (the settings
//! panel mirroring the C++ tabbed preferences: general / rendering / cache
//! / proxy / project / audio) and export (format + output path).
//!
//! Each view owns its widgets and emits nothing itself — the host
//! (`crate::app::OakApp`) reads the state (format / path) when a dialog
//! button is clicked, and the preferences view writes its choices straight
//! through the config C ABI on selection. Theme/language changes
//! additionally emit a [`PreferencesEvent`] so the host can re-apply the
//! shell chrome immediately.

use gpui::colors::DefaultColors;
use gpui::prelude::*;
use gpui::{div, px, App, Context, Entity, Render, SharedString, Window};
use gpui_elements::editable_text::{text_input, EditableTextState, StringStorage};
use gpui_widgets::checkbox::{CheckBox, CheckBoxEvent, CheckState};
use gpui_widgets::combo_box::{ComboBox, ComboBoxEvent, ComboBoxOption};
use gpui_widgets::slider::SliderModel;
use gpui_widgets::spinbox::{SpinBox, SpinBoxEvent};
use gpui_widgets::value::{SliderValue, ValueKind};

use crate::i18n;
use crate::oakui::real::{
	audio_input_device, audio_input_devices, audio_output_device, audio_output_devices,
	config_get_bool, config_get_int, config_get_string, config_set_bool, config_set_int,
	config_set_string, encoding_formats, proxy_dividers, renderer_backends,
	set_audio_input_device, set_audio_output_device, set_theme_dark, theme_is_dark,
	CONFIG_KEY_DEFAULT_TRANSITION_SEC, CONFIG_KEY_DISK_CACHE_PATH, CONFIG_KEY_PROXY_DIVIDER,
	CONFIG_KEY_RENDERER_BACKEND, CONFIG_KEY_SNAPSHOT_INTERVAL_SEC, CONFIG_KEY_USE_PROXY,
	DEFAULT_SNAPSHOT_INTERVAL_SEC, DEFAULT_TRANSITION_SEC, EXPORT_FORMAT_MP4,
};

// ---------------------------------------------------------------------------
// Preferences
// ---------------------------------------------------------------------------

/// A request the preferences dialog emits for the host shell (the settings
/// themselves are written through the config C ABI directly; these need
/// shell chrome — the menu bar / theme — to re-render).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PreferencesEvent {
	/// The theme dropdown changed (the payload is the new dark flag).
	ThemeChanged(bool),
	/// The language dropdown changed (already applied to the i18n global).
	LanguageChanged,
}

impl gpui::EventEmitter<PreferencesEvent> for PreferencesContent {}

/// The preferences dialog content, mirroring the C++ tabbed preferences as
/// one grouped panel:
///
/// * **常规 General** — language, theme.
/// * **渲染 Rendering** — the renderer backend.
/// * **缓存 Cache** — the disk cache directory (`DiskCachePath`).
/// * **代理 Proxy** — use proxy media (`UseProxyMedia`), proxy resolution
///   divider (`ProxyDivider`).
/// * **项目 Project** — the snapshot interval (`Storage/SnapshotIntervalSec`,
///   the write-through era's auto-save interval) and the default transition
///   length (`DefaultTransitionLength`).
/// * **音频 Audio** — the output / input devices (`AudioOutput` /
///   `AudioInput`, applied live through the audio facade).
///
/// Every row writes through the config C ABI on selection, so the choices
/// survive restarts (the app loads the config at startup and saves it on
/// exit).
pub struct PreferencesContent {
	backend: Entity<ComboBox>,
	language: Entity<ComboBox>,
	theme: Entity<ComboBox>,
	cache_dir: Entity<PathField>,
	use_proxy: Entity<CheckBox>,
	proxy_divider: Entity<ComboBox>,
	snapshot_interval: Entity<SpinBox>,
	transition_length: Entity<SpinBox>,
	audio_output: Entity<ComboBox>,
	audio_input: Entity<ComboBox>,
	/// The backend options, in display order.
	backends: Vec<&'static str>,
	/// The proxy divider options, in display order (1 = full resolution).
	dividers: Vec<i64>,
	/// The output device names (dropdown order; index 0 is system default).
	output_devices: Vec<String>,
	/// The input device names (dropdown order; index 0 is system default).
	input_devices: Vec<String>,
}

impl PreferencesContent {
	/// Builds the content: reads the current config values and seeds every
	/// widget.
	pub fn new(window: &mut Window, cx: &mut Context<Self>) -> Self {
		// --- 渲染 Rendering: the renderer backend --------------------------
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

		// --- 常规 General: language + theme --------------------------------
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
				cx.emit(PreferencesEvent::LanguageChanged);
			}
		})
		.detach();
		language.update(cx, |combo, cx| {
			combo.set_selected(Some(language_selected), cx)
		});

		let theme_options = vec![
			ComboBoxOption::new(0, i18n::tr("preferences.theme.dark")),
			ComboBoxOption::new(1, i18n::tr("preferences.theme.light")),
		];
		let theme = cx.new(|cx| ComboBox::new(3, theme_options, window, cx));
		cx.subscribe(&theme, |_this, _combo, event: &ComboBoxEvent, cx| {
			if let ComboBoxEvent::Selected { value, .. } = event {
				let dark = *value == 0;
				set_theme_dark(dark);
				cx.emit(PreferencesEvent::ThemeChanged(dark));
			}
		})
		.detach();
		theme.update(cx, |combo, cx| {
			combo.set_selected(Some(if theme_is_dark() { 0 } else { 1 }), cx)
		});

		// --- 缓存 Cache: the disk cache directory --------------------------
		let cache_dir = cx.new(|cx| {
			let editor = cx.new(|cx| EditableTextState::new(StringStorage::default(), cx));
			PathField { editor }
		});
		let configured_cache = config_get_string(CONFIG_KEY_DISK_CACHE_PATH);
		cache_dir.update(cx, |field, cx| field.set_path(configured_cache, cx));

		// --- 代理 Proxy -----------------------------------------------------
		let use_proxy = cx.new(|cx| {
			CheckBox::new(
				7,
				if config_get_bool(CONFIG_KEY_USE_PROXY, true) {
					CheckState::Checked
				} else {
					CheckState::Unchecked
				},
				window,
				cx,
			)
			.with_label(i18n::tr("preferences.proxy.enable"))
		});
		cx.subscribe(&use_proxy, |_this, check, event: &CheckBoxEvent, cx| {
			if let CheckBoxEvent::Toggled { state, .. } = event {
				let enabled = *state == CheckState::Checked;
				config_set_bool(CONFIG_KEY_USE_PROXY, enabled);
				check.update(cx, |check, cx| check.set_state(*state, cx));
			}
		})
		.detach();

		let dividers = proxy_dividers();
		let divider_options = dividers
			.iter()
			.enumerate()
			.map(|(i, d)| {
				if *d <= 1 {
					ComboBoxOption::new(i, i18n::tr("preferences.proxy.full"))
				} else {
					ComboBoxOption::new(i, format!("1/{d}"))
				}
			})
			.collect();
		let proxy_divider = cx.new(|cx| ComboBox::new(4, divider_options, window, cx));
		cx.subscribe(&proxy_divider, |this, _combo, event: &ComboBoxEvent, cx| {
			if let ComboBoxEvent::Selected { value, .. } = event {
				if let Some(divider) = this.dividers.get(*value) {
					config_set_int(CONFIG_KEY_PROXY_DIVIDER, *divider);
				}
			}
			let _ = cx;
		})
		.detach();
		let current_divider = config_get_int(CONFIG_KEY_PROXY_DIVIDER, 1);
		let divider_selected = dividers
			.iter()
			.position(|d| *d == current_divider)
			.unwrap_or(0);
		proxy_divider.update(cx, |combo, cx| {
			combo.set_selected(Some(divider_selected), cx)
		});

		// --- 项目 Project: snapshot interval + default transition ----------
		let snapshot_interval = cx.new(|cx| {
			let current =
				config_get_int(CONFIG_KEY_SNAPSHOT_INTERVAL_SEC, DEFAULT_SNAPSHOT_INTERVAL_SEC);
			SpinBox::new(
				8,
				SliderModel::new(ValueKind::Integer, 0.0, 86400.0, 10.0, current as f64),
				window,
				cx,
			)
		});
		cx.subscribe(
			&snapshot_interval,
			|_this, _spin, event: &SpinBoxEvent, cx| {
				let value = match event {
					SpinBoxEvent::ValueChanged { value, .. }
					| SpinBoxEvent::EditCommitted { value, .. } => value.to_f64() as i64,
					_ => return,
				};
				config_set_int(CONFIG_KEY_SNAPSHOT_INTERVAL_SEC, value);
				let _ = cx;
			},
		)
		.detach();

		let transition_length = cx.new(|cx| {
			let current = config_get_string(CONFIG_KEY_DEFAULT_TRANSITION_SEC)
				.parse::<f64>()
				.ok()
				.filter(|v| *v >= 0.0)
				.unwrap_or_else(|| DEFAULT_TRANSITION_SEC.parse().unwrap());
			SpinBox::new(
				9,
				SliderModel::new(ValueKind::Float, 0.0, 60.0, 0.5, current),
				window,
				cx,
			)
		});
		cx.subscribe(
			&transition_length,
			|_this, _spin, event: &SpinBoxEvent, cx| {
				let value = match event {
					SpinBoxEvent::ValueChanged { value, .. }
					| SpinBoxEvent::EditCommitted { value, .. } => value.to_f64(),
					_ => return,
				};
				config_set_string(CONFIG_KEY_DEFAULT_TRANSITION_SEC, &format!("{value}"));
				let _ = cx;
			},
		)
		.detach();

		// --- 音频 Audio: output / input devices -----------------------------
		// The enumeration goes through the facade even on the mock engine;
		// the config choice applies the moment the device dropdown changes.
		let (audio_output, output_devices) =
			device_combo(5, true, window, cx);
		let (audio_input, input_devices) =
			device_combo(6, false, window, cx);
		cx.subscribe(&audio_output, |this, _combo, event: &ComboBoxEvent, cx| {
			if let ComboBoxEvent::Selected { value, .. } = event {
				// Option 0 is the system default; the devices start at 1.
				let name = value
					.checked_sub(1)
					.and_then(|i| this.output_devices.get(i))
					.cloned()
					.unwrap_or_default();
				set_audio_output_device(&name);
			}
			let _ = cx;
		})
		.detach();
		cx.subscribe(&audio_input, |this, _combo, event: &ComboBoxEvent, cx| {
			if let ComboBoxEvent::Selected { value, .. } = event {
				let name = value
					.checked_sub(1)
					.and_then(|i| this.input_devices.get(i))
					.cloned()
					.unwrap_or_default();
				set_audio_input_device(&name);
			}
			let _ = cx;
		})
		.detach();

		Self {
			backend,
			language,
			theme,
			cache_dir,
			use_proxy,
			proxy_divider,
			snapshot_interval,
			transition_length,
			audio_output,
			audio_input,
			backends,
			dividers,
			output_devices,
			input_devices,
		}
	}

	/// The cache directory currently entered.
	pub fn cache_dir(&self, cx: &App) -> SharedString {
		self.cache_dir.read(cx).path(cx)
	}

	/// Commits the cache directory field to the config (called by the host
	/// when the dialog closes, so a typed-but-unbrowsed path still lands).
	pub fn commit_cache_dir(&self, cx: &App) {
		let path = self.cache_dir(cx).trim().to_string();
		config_set_string(CONFIG_KEY_DISK_CACHE_PATH, &path);
	}

	/// Opens the platform directory picker and lands the choice in the cache
	/// directory field (committed with the field, on dialog close).
	fn browse_cache_dir(&mut self, cx: &mut Context<Self>) {
		let receiver = cx.prompt_for_paths(gpui::PathPromptOptions {
			files: false,
			directories: true,
			multiple: false,
			prompt: Some(i18n::tr("preferences.cache.browse").into()),
		});
		cx.spawn(async move |this, cx| {
			let Ok(Ok(Some(paths))) = receiver.await else {
				return;
			};
			let Some(path) = paths.first() else {
				return;
			};
			this.update(cx, |this, cx| {
				this.cache_dir.update(cx, |field, cx| {
					field.set_path(path.to_string_lossy().into_owned(), cx)
				});
				cx.notify();
			});
		})
		.detach();
	}
}

/// Builds a device dropdown for the output (`output = true`) or input side:
/// option 0 is the system default, the rest are the enumerated devices, and
/// the current config value (validated against the enumeration) is
/// preselected. Returns the combo and the option→device-name list.
fn device_combo(
	control: usize,
	output: bool,
	window: &mut Window,
	cx: &mut Context<PreferencesContent>,
) -> (Entity<ComboBox>, Vec<String>) {
	let devices = if output {
		audio_output_devices()
	} else {
		audio_input_devices()
	};
	let current = if output {
		audio_output_device()
	} else {
		audio_input_device()
	};
	let mut options = vec![ComboBoxOption::new(0, i18n::tr("preferences.audio.default"))];
	for (i, name) in devices.iter().enumerate() {
		options.push(ComboBoxOption::new(i + 1, name.clone()));
	}
	let selected = devices
		.iter()
		.position(|n| *n == current)
		.map(|i| i + 1)
		.unwrap_or(0);
	let placeholder = if output {
		i18n::tr("preferences.audio.output.placeholder")
	} else {
		i18n::tr("preferences.audio.input.placeholder")
	};
	let combo = cx.new(|cx| ComboBox::new(control, options, window, cx).with_placeholder(placeholder));
	combo.update(cx, |combo, cx| combo.set_selected(Some(selected), cx));
	(combo, devices)
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

/// A group header separating the preference sections.
fn section_header(colors: &gpui::colors::Colors, label: SharedString) -> gpui::Div {
	div()
		.pt_2()
		.text_color(colors.disabled)
		.text_xs()
		.child(label)
}

impl Render for PreferencesContent {
	fn render(&mut self, _window: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
		let colors = cx.default_colors().clone();
		// The full settings list is taller than a 900px window at the design
		// density, so the content scrolls inside the modal card.
		div()
			.id("preferences-content")
			.flex()
			.flex_col()
			.gap_3()
			.w_full()
			.max_h(px(720.0))
			.overflow_y_scroll()
			// 常规 General
			.child(section_header(&colors, i18n::tr("preferences.section.general").into()))
			.child(form_row(
				&colors,
				i18n::tr("preferences.language").into(),
				self.language.clone(),
			))
			.child(form_row(
				&colors,
				i18n::tr("preferences.theme").into(),
				self.theme.clone(),
			))
			// 渲染 Rendering
			.child(section_header(&colors, i18n::tr("preferences.section.render").into()))
			.child(form_row(
				&colors,
				i18n::tr("preferences.backend").into(),
				self.backend.clone(),
			))
			// 缓存 Cache
			.child(section_header(&colors, i18n::tr("preferences.section.cache").into()))
			.child(form_row(
				&colors,
				i18n::tr("preferences.cache.dir").into(),
				div()
					.flex()
					.gap_2()
					.child(div().flex_1().child(self.cache_dir.clone()))
					.child(
						div()
							.id("preferences-cache-browse")
							.px_3()
							.py_1()
							.rounded_md()
							.bg(colors.background)
							.border_1()
							.border_color(colors.border)
							.text_color(colors.text)
							.cursor_pointer()
							.child(i18n::tr("preferences.cache.browse"))
							.on_click(cx.listener(|this, _event, _window, cx| {
								this.browse_cache_dir(cx);
							})),
					),
			))
			// 代理 Proxy
			.child(section_header(&colors, i18n::tr("preferences.section.proxy").into()))
			.child(self.use_proxy.clone())
			.child(form_row(
				&colors,
				i18n::tr("preferences.proxy.resolution").into(),
				self.proxy_divider.clone(),
			))
			// 项目 Project
			.child(section_header(&colors, i18n::tr("preferences.section.project").into()))
			.child(form_row(
				&colors,
				i18n::tr("preferences.snapshot.interval").into(),
				self.snapshot_interval.clone(),
			))
			.child(form_row(
				&colors,
				i18n::tr("preferences.transition.default").into(),
				self.transition_length.clone(),
			))
			// 音频 Audio
			.child(section_header(&colors, i18n::tr("preferences.section.audio").into()))
			.child(form_row(
				&colors,
				i18n::tr("preferences.audio.output").into(),
				self.audio_output.clone(),
			))
			.child(form_row(
				&colors,
				i18n::tr("preferences.audio.input").into(),
				self.audio_input.clone(),
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
