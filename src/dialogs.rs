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
//! into the oakcommon config store on selection. Theme/language changes
//! additionally emit a [`PreferencesEvent`] so the host can re-apply the
//! shell chrome immediately.

use gpui::colors::DefaultColors;
use gpui::prelude::*;
use gpui::{
	div, px, App, Context, ElementId, Entity, EventEmitter, Focusable, FocusHandle, Keystroke,
	PathPromptOptions, Render, SharedString, Window,
};
use gpui_elements::editable_text::{text_input, EditableTextState, StringStorage, TextChanged};
use gpui_widgets::checkbox::{CheckBox, CheckBoxEvent, CheckState};
use gpui_widgets::combo_box::{ComboBox, ComboBoxEvent, ComboBoxOption};
use gpui_widgets::slider::SliderModel;
use gpui_widgets::spinbox::{SpinBox, SpinBoxEvent};
use gpui_widgets::value::ValueKind;

use crate::actions::ActionId;
use crate::i18n;
use crate::oakui::real::{
	audio_input_device, audio_input_devices, audio_output_device, audio_output_devices,
	config_get_bool, config_get_int, config_get_string, config_set_bool, config_set_int,
	config_set_string, encoding_formats, proxy_dividers, renderer_backends,
	set_audio_input_device, set_audio_output_device, set_theme_dark, theme_is_dark,
	CONFIG_KEY_DEFAULT_TRANSITION_SEC, CONFIG_KEY_DISK_CACHE_PATH, CONFIG_KEY_FFMPEG_PATH,
	CONFIG_KEY_PROXY_DIVIDER, CONFIG_KEY_RENDERER_BACKEND, CONFIG_KEY_SNAPSHOT_INTERVAL_SEC,
	CONFIG_KEY_USE_PROXY, DEFAULT_SNAPSHOT_INTERVAL_SEC, DEFAULT_TRANSITION_SEC,
	EXPORT_FORMAT_MP4,
};

// ---------------------------------------------------------------------------
// Preferences
// ---------------------------------------------------------------------------

/// A request the preferences dialog emits for the host shell (the settings
/// themselves are written into the config store directly; these need
/// shell chrome — the menu bar / theme / key map — to re-render).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PreferencesEvent {
	/// The theme dropdown changed (the payload is the new dark flag).
	ThemeChanged(bool),
	/// The language dropdown changed (already applied to the i18n global).
	LanguageChanged,
	/// The custom shortcut overrides changed (the Keyboard tab); the host
	/// must re-bind the global key map and rebuild the menu bar so the new
	/// keys take effect immediately.
	ShortcutsChanged,
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
///   `AudioInput`, applied live through the oakaudio manager).
///
/// Every row writes into the config store on selection, so the choices
/// survive restarts (the app loads the config at startup and saves it on
/// exit).
pub struct PreferencesContent {
	backend: Entity<ComboBox>,
	language: Entity<ComboBox>,
	theme: Entity<ComboBox>,
	cache_dir: Entity<PathField>,
	use_proxy: Entity<CheckBox>,
	hw_decode: Entity<CheckBox>,
	proxy_divider: Entity<ComboBox>,
	display_icc: Entity<CheckBox>,
	display_icc_path: Entity<PathField>,
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

		// --- 渲染 Rendering: hardware decoding switch -------------------
		// Default ON (user mandate); off forces software decoding.
		let hw_decode = cx.new(|cx| {
			CheckBox::new(
				9,
				if config_get_bool("HardwareDecoding", true) {
					CheckState::Checked
				} else {
					CheckState::Unchecked
				},
				window,
				cx,
			)
			.with_label(i18n::tr("preferences.hwdecode.enable"))
		});
		cx.subscribe(&hw_decode, |_this, check, event: &CheckBoxEvent, cx| {
			if let CheckBoxEvent::Toggled { state, .. } = event {
				let enabled = *state == CheckState::Checked;
				config_set_bool("HardwareDecoding", enabled);
				check.update(cx, |check, cx| check.set_state(*state, cx));
			}
		})
		.detach();

		// --- 色彩 Color: display ICC color management -----------------------
		// On by default: the viewer frames are transformed through the
		// display's ICC profile (system profile, or a custom file below).
		// The macOS layer tag is applied at startup, so a mode change takes
		// effect after a restart.
		use crate::oakui::displaycolor::{
			CONFIG_KEY_COLOR_MODE, CONFIG_KEY_CUSTOM_ICC,
		};
		let display_icc = cx.new(|cx| {
			let mode = config_get_string(CONFIG_KEY_COLOR_MODE);
			CheckBox::new(
				13,
				if mode != "off" {
					CheckState::Checked
				} else {
					CheckState::Unchecked
				},
				window,
				cx,
			)
			.with_label(i18n::tr("preferences.color.enable"))
		});
		cx.subscribe(&display_icc, |_this, check, event: &CheckBoxEvent, cx| {
			if let CheckBoxEvent::Toggled { state, .. } = event {
				let enabled = *state == CheckState::Checked;
				config_set_string(CONFIG_KEY_COLOR_MODE, if enabled { "icc" } else { "off" });
				check.update(cx, |check, cx| check.set_state(*state, cx));
			}
		})
		.detach();
		let display_icc_path = cx.new(|cx| {
			let editor = cx.new(|cx| EditableTextState::new(StringStorage::default(), cx));
			PathField { editor }
		});
		let configured_icc = config_get_string(CONFIG_KEY_CUSTOM_ICC);
		display_icc_path.update(cx, |field, cx| field.set_path(configured_icc, cx));

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
		// The enumeration reads the oakaudio manager even on the mock
		// engine; the config choice applies the moment the dropdown changes.
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
			hw_decode,
			proxy_divider,
			display_icc,
			display_icc_path,
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

	/// Commits the custom ICC path field to the config (called by the host
	/// when the dialog closes, like the cache directory).
	pub fn commit_display_icc_path(&self, cx: &App) {
		let path = self.display_icc_path.read(cx).path(cx).trim().to_string();
		config_set_string(
			crate::oakui::displaycolor::CONFIG_KEY_CUSTOM_ICC,
			&path,
		);
	}

	/// Opens the platform file picker for a custom ICC profile.
	fn browse_display_icc(&mut self, cx: &mut Context<Self>) {
		let receiver = cx.prompt_for_paths(gpui::PathPromptOptions {
			files: true,
			directories: false,
			multiple: false,
			prompt: Some(i18n::tr("preferences.color.browse").into()),
		});
		cx.spawn(async move |this, cx| {
			let Ok(Ok(Some(paths))) = receiver.await else {
				return;
			};
			let Some(path) = paths.first() else {
				return;
			};
			this.update(cx, |this, cx| {
				this.display_icc_path.update(cx, |field, cx| {
					field.set_path(path.to_string_lossy().into_owned(), cx)
				});
				cx.notify();
			});
		})
		.detach();
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
			.child(self.hw_decode.clone())
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
			// 色彩 Color
			.child(section_header(&colors, i18n::tr("preferences.section.color").into()))
			.child(self.display_icc.clone())
			.child(form_row(
				&colors,
				i18n::tr("preferences.color.custom").into(),
				div()
					.flex()
					.gap_2()
					.child(div().flex_1().child(self.display_icc_path.clone()))
					.child(
						div()
							.id("preferences-icc-browse")
							.px_3()
							.py_1()
							.rounded_md()
							.bg(colors.background)
							.border_1()
							.border_color(colors.border)
							.text_color(colors.text)
							.cursor_pointer()
							.child(i18n::tr("preferences.color.browse"))
							.on_click(cx.listener(|this, _event, _window, cx| {
								this.browse_display_icc(cx);
							})),
					),
			))
			.child(
				div()
					.text_color(colors.disabled)
					.text_xs()
					.child(i18n::tr("preferences.color.restart_hint")),
			)
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

// ---------------------------------------------------------------------------
// Proxy settings (the C++ Tools > Proxy Settings dialog)
// ---------------------------------------------------------------------------

/// The ffmpeg encoder presets the proxy dialog offers (the C++
/// `ProxyDialog` preset combo, same order).
pub const PROXY_PRESETS: &[&str] = &[
	"ultrafast",
	"superfast",
	"veryfast",
	"faster",
	"fast",
	"medium",
	"slow",
	"slower",
	"veryslow",
];

/// The proxy settings dialog content: the global generation settings
/// plus the per-footage proxy list (the gpui port of the C++
/// `ProxyDialog`). All footage of the open project is listed — the gpui
/// shell opens the dialog from the Tools menu without a footage
/// selection, so the C++ "selected footage" group becomes "footage".
pub struct ProxyDialogContent<E: crate::oakui::engine::AppEngine> {
	engine: Entity<E>,
	divider: Entity<ComboBox>,
	width: Entity<SpinBox>,
	height: Entity<SpinBox>,
	crf: Entity<SpinBox>,
	preset: Entity<ComboBox>,
	include_audio: Entity<CheckBox>,
	ffmpeg_path: Entity<PathField>,
	custom_params: Entity<CheckBox>,
	/// Snapshot of the footage rows (refreshed after generate / delete).
	rows: Vec<crate::oakui::engine::ProxyFootageRow>,
	/// The divider values in dropdown order (1 = custom size).
	dividers: Vec<i32>,
}

impl<E: crate::oakui::engine::AppEngine> ProxyDialogContent<E> {
	/// Builds the content seeded from the global config params (the C++
	/// `oakengine_proxy_params_from_config` defaults).
	pub fn new(engine: Entity<E>, window: &mut Window, cx: &mut Context<Self>) -> Self {
		let params = crate::oakui::engine::proxy_params_from_config();

		let dividers: Vec<i32> = vec![1, 2, 4, 8];
		let divider_options = dividers
			.iter()
			.enumerate()
			.map(|(i, d)| ComboBoxOption::new(i, divider_label(*d)))
			.collect();
		let divider = cx.new(|cx| ComboBox::new(21, divider_options, window, cx));
		let divider_selected = dividers
			.iter()
			.position(|d| *d == params.divider)
			.unwrap_or(0);
		divider.update(cx, |combo, cx| combo.set_selected(Some(divider_selected), cx));

		let width = cx.new(|cx| {
			SpinBox::new(
				22,
				SliderModel::new(
					ValueKind::Integer,
					160.0,
					4096.0,
					16.0,
					f64::from(params.width),
				),
				window,
				cx,
			)
		});
		let height = cx.new(|cx| {
			SpinBox::new(
				23,
				SliderModel::new(
					ValueKind::Integer,
					120.0,
					2160.0,
					8.0,
					f64::from(params.height),
				),
				window,
				cx,
			)
		});
		let crf = cx.new(|cx| {
			SpinBox::new(
				24,
				SliderModel::new(ValueKind::Integer, 0.0, 51.0, 1.0, f64::from(params.crf)),
				window,
				cx,
			)
		});

		let preset_options = PROXY_PRESETS
			.iter()
			.enumerate()
			.map(|(i, name)| ComboBoxOption::new(i, *name))
			.collect();
		let preset = cx.new(|cx| ComboBox::new(25, preset_options, window, cx));
		let preset_selected = PROXY_PRESETS
			.iter()
			.position(|name| *name == params.preset)
			.unwrap_or(2);
		preset.update(cx, |combo, cx| combo.set_selected(Some(preset_selected), cx));

		let include_audio = cx.new(|cx| {
			CheckBox::new(
				26,
				if params.include_audio {
					CheckState::Checked
				} else {
					CheckState::Unchecked
				},
				window,
				cx,
			)
			.with_label(i18n::tr("proxydialog.include_audio"))
		});

		let ffmpeg_path = cx.new(|cx| {
			let editor = cx.new(|cx| EditableTextState::new(StringStorage::default(), cx));
			PathField { editor }
		});
		ffmpeg_path.update(cx, |field, cx| {
			field.set_path(config_get_string(CONFIG_KEY_FFMPEG_PATH), cx)
		});

		let rows = engine.read(cx).proxy_rows();
		let any_custom = rows.iter().any(|row| row.has_custom);
		let custom_params = cx.new(|cx| {
			CheckBox::new(
				27,
				if any_custom {
					CheckState::Checked
				} else {
					CheckState::Unchecked
				},
				window,
				cx,
			)
			.with_label(i18n::tr("proxydialog.custom"))
		});

		Self {
			engine,
			divider,
			width,
			height,
			crf,
			preset,
			include_audio,
			ffmpeg_path,
			custom_params,
			rows,
			dividers,
		}
	}

	/// The generation params currently edited in the dialog (the C++
	/// `current_params`).
	pub fn current_params(&self, cx: &App) -> crate::oakui::engine::ProxyParamsUi {
		let divider = self
			.divider
			.read(cx)
			.selected()
			.and_then(|i| self.dividers.get(i))
			.copied()
			.unwrap_or(1);
		let preset = self
			.preset
			.read(cx)
			.selected()
			.and_then(|i| PROXY_PRESETS.get(i))
			.unwrap_or(&"veryfast")
			.to_string();
		crate::oakui::engine::ProxyParamsUi {
			width: self.width.read(cx).value().to_f64() as i32,
			height: self.height.read(cx).value().to_f64() as i32,
			divider,
			crf: self.crf.read(cx).value().to_f64() as i32,
			preset,
			include_audio: self.include_audio.read(cx).state() == CheckState::Checked,
		}
	}

	/// Writes the global settings into the config store (the C++
	/// `save_global_settings`).
	pub fn save_global_settings(&self, cx: &App) {
		let params = self.current_params(cx);
		config_set_int("ProxyWidth", i64::from(params.width));
		config_set_int("ProxyHeight", i64::from(params.height));
		config_set_int("ProxyDivider", i64::from(params.divider));
		config_set_int("ProxyCRF", i64::from(params.crf));
		config_set_string("ProxyPreset", &params.preset);
		config_set_bool("ProxyIncludeAudio", params.include_audio);
		config_set_string(
			CONFIG_KEY_FFMPEG_PATH,
			self.ffmpeg_path.read(cx).path(cx).trim(),
		);
	}

	/// Generates proxies for every footage row with a video stream (the
	/// C++ Generate Proxies button): with the custom checkbox set, the
	/// edited params become each footage's custom params first.
	pub fn generate(&mut self, cx: &mut Context<Self>) {
		let custom = self.custom_params.read(cx).state() == CheckState::Checked;
		let params = self.current_params(cx);
		let ids: Vec<(u64, bool)> = self
			.rows
			.iter()
			.map(|row| (row.id, row.can_generate))
			.collect();
		for (id, can_generate) in ids {
			if !can_generate {
				continue;
			}
			if custom {
				self.engine.update(cx, |engine, cx| {
					engine.proxy_set_custom_params(id, params.clone(), cx)
				});
			}
			if let Err(err) = self.engine.update(cx, |engine, cx| engine.proxy_generate(id, cx))
			{
				println!("[proxy] generate failed for {id}: {err}");
			}
		}
		self.refresh(cx);
	}

	/// Deletes every footage row's proxy (the C++ Delete Proxies button).
	pub fn delete(&mut self, cx: &mut Context<Self>) {
		let ids: Vec<u64> = self.rows.iter().map(|row| row.id).collect();
		for id in ids {
			self.engine.update(cx, |engine, cx| engine.proxy_delete(id, cx));
		}
		self.refresh(cx);
	}

	/// Applies the dialog (the C++ `accept`): saves the global settings
	/// and sets or clears each footage's custom params per the checkbox.
	pub fn accept(&mut self, cx: &mut Context<Self>) {
		self.save_global_settings(cx);
		let custom = self.custom_params.read(cx).state() == CheckState::Checked;
		let params = self.current_params(cx);
		let ids: Vec<u64> = self.rows.iter().map(|row| row.id).collect();
		for id in ids {
			if custom {
				self.engine.update(cx, |engine, cx| {
					engine.proxy_set_custom_params(id, params.clone(), cx)
				});
			} else {
				self.engine
					.update(cx, |engine, cx| engine.proxy_clear_custom_params(id, cx));
			}
		}
		self.refresh(cx);
	}

	/// Re-reads the footage rows (after generate / delete / accept).
	pub fn refresh(&mut self, cx: &mut Context<Self>) {
		self.rows = self.engine.read(cx).proxy_rows();
		cx.notify();
	}
}

/// The dropdown label of a resolution divider (the C++ combo labels).
fn divider_label(divider: i32) -> String {
	match divider {
		1 => i18n::tr("proxydialog.resolution.custom").into(),
		2 => i18n::tr("proxydialog.resolution.half").into(),
		4 => i18n::tr("proxydialog.resolution.quarter").into(),
		8 => i18n::tr("proxydialog.resolution.eighth").into(),
		other => format!("1/{other}"),
	}
}

/// The display string of a proxy lifecycle state.
fn proxy_state_label(state: crate::oakui::engine::ProxyMediaState) -> String {
	use crate::oakui::engine::ProxyMediaState;
	match state {
		ProxyMediaState::Missing => i18n::tr("proxydialog.state.missing"),
		ProxyMediaState::Generating => i18n::tr("proxydialog.state.generating"),
		ProxyMediaState::Ready => i18n::tr("proxydialog.state.ready"),
		ProxyMediaState::Failed => i18n::tr("proxydialog.state.failed"),
	}
	.into()
}

impl<E: crate::oakui::engine::AppEngine> Render for ProxyDialogContent<E> {
	fn render(&mut self, _window: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
		let colors = cx.default_colors().clone();

		let footage_list = div().flex().flex_col().gap_1().children(
			self.rows
				.iter()
				.map(|row| {
					let mut state = proxy_state_label(row.state);
					if row.has_custom {
						state.push_str(&i18n::tr("proxydialog.custom_suffix"));
					}
					let enabled = row.enabled && row.state == crate::oakui::engine::ProxyMediaState::Ready;
					let dot = if enabled {
						colors.selected
					} else {
						colors.disabled
					};
					div()
						.flex()
						.items_center()
						.gap_2()
						.child(div().w(px(8.0)).h(px(8.0)).rounded_full().bg(dot))
						.child(
							div()
								.flex_1()
								.overflow_hidden()
								.text_ellipsis()
								.text_color(colors.text)
								.child(row.name.clone()),
						)
						.child(div().text_color(colors.disabled).text_xs().child(state))
				})
				.collect::<Vec<_>>(),
		);

		let footage_group = div()
			.flex()
			.flex_col()
			.gap_2()
			.child(section_header(
				&colors,
				i18n::tr("proxydialog.footage_group").into(),
			))
			.child(
				div()
					.id("proxy-footage-list")
					.max_h(px(160.0))
					.overflow_y_scroll()
					.child(if self.rows.is_empty() {
						div()
							.text_color(colors.disabled)
							.text_xs()
							.child(i18n::tr("proxydialog.no_footage"))
					} else {
						footage_list
					}),
			)
			.child(self.custom_params.clone());

		let settings_group = div()
			.flex()
			.flex_col()
			.gap_2()
			.child(section_header(
				&colors,
				i18n::tr("proxydialog.global").into(),
			))
			.child(form_row(
				&colors,
				i18n::tr("proxydialog.resolution").into(),
				self.divider.clone(),
			))
			.child(
				div()
					.flex()
					.gap_3()
					.child(form_row(
						&colors,
						i18n::tr("proxydialog.width").into(),
						self.width.clone(),
					))
					.child(form_row(
						&colors,
						i18n::tr("proxydialog.height").into(),
						self.height.clone(),
					)),
			)
			.child(
				div()
					.flex()
					.gap_3()
					.child(form_row(
						&colors,
						i18n::tr("proxydialog.crf").into(),
						self.crf.clone(),
					))
					.child(form_row(
						&colors,
						i18n::tr("proxydialog.preset").into(),
						self.preset.clone(),
					)),
			)
			.child(self.include_audio.clone())
			.child(form_row(
				&colors,
				i18n::tr("proxydialog.ffmpeg").into(),
				self.ffmpeg_path.clone(),
			));

		div()
			.flex()
			.flex_col()
			.gap_3()
			.w_full()
			.child(footage_group)
			.child(settings_group)
	}
}

// ---------------------------------------------------------------------------
// Preferences: the tabbed host (General + Keyboard)
// ---------------------------------------------------------------------------

/// The preferences dialog tabs.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum PreferencesTab {
	/// The grouped settings (language / theme / backend / cache / …).
	General,
	/// The custom-shortcuts editor.
	Keyboard,
}

/// A fully transparent color (for un-selected rows / tabs).
fn transparent() -> gpui::Rgba {
	gpui::Rgba {
		r: 0.0,
		g: 0.0,
		b: 0.0,
		a: 0.0,
	}
}

/// The tabbed preferences dialog content: the existing grouped settings plus
/// the Keyboard tab — the Rust counterpart of the C++ `PreferencesDialog`
/// hosting the `PreferencesKeyboardTab` (`preferenceskeyboardtab.cpp`).
///
/// The host re-emits the general tab's [`PreferencesEvent`]s (theme /
/// language) and turns the keyboard tab's [`KeyboardEvent::Changed`] into
/// [`PreferencesEvent::ShortcutsChanged`], so the app shell only subscribes to
/// this one content entity.
pub struct PreferencesDialogContent {
	active: PreferencesTab,
	general: Entity<PreferencesContent>,
	keyboard: Entity<KeyboardTabContent>,
}

impl EventEmitter<PreferencesEvent> for PreferencesDialogContent {}

impl PreferencesDialogContent {
	/// Builds both tabs (the general tab keeps its existing behavior; the
	/// keyboard tab lists the current menu-bar actions).
	pub fn new(window: &mut Window, cx: &mut Context<Self>) -> Self {
		let general = cx.new(|cx| PreferencesContent::new(window, cx));
		let keyboard = cx.new(|cx| KeyboardTabContent::new(window, cx));
		cx.subscribe(&general, |_this, _general, event: &PreferencesEvent, cx| match event {
			PreferencesEvent::ThemeChanged(dark) => {
				cx.emit(PreferencesEvent::ThemeChanged(*dark));
			}
			PreferencesEvent::LanguageChanged => cx.emit(PreferencesEvent::LanguageChanged),
			PreferencesEvent::ShortcutsChanged => {}
		})
		.detach();
		cx.subscribe(&keyboard, |_this, _keyboard, event: &KeyboardEvent, cx| {
			if matches!(event, KeyboardEvent::Changed) {
				cx.emit(PreferencesEvent::ShortcutsChanged);
			}
		})
		.detach();
		Self {
			active: PreferencesTab::General,
			general,
			keyboard,
		}
	}

	/// Commits the general tab's free-text fields (the cache directory, the
	/// custom ICC path), for the host when the dialog closes.
	pub fn commit_cache_dir(&self, cx: &App) {
		let general = self.general.read(cx);
		general.commit_cache_dir(cx);
		general.commit_display_icc_path(cx);
	}

	/// The keyboard tab's action-row count (tests).
	pub fn keyboard_tab_row_count(&self, cx: &App) -> usize {
		self.keyboard.read(cx).row_count()
	}
}

impl Render for PreferencesDialogContent {
	fn render(&mut self, _window: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
		let colors = cx.default_colors().clone();
		let content = match self.active {
			PreferencesTab::General => self.general.clone().into_any_element(),
			PreferencesTab::Keyboard => self.keyboard.clone().into_any_element(),
		};
		div()
			.flex()
			.flex_col()
			.gap_3()
			.w_full()
			.child(
				div()
					.flex()
					.gap_1()
					.child(
						tab_button(PreferencesTab::General, self.active, &colors, cx),
					)
					.child(tab_button(PreferencesTab::Keyboard, self.active, &colors, cx)),
			)
			.child(content)
	}
}

/// One tab switcher button of the preferences dialog.
fn tab_button(
	tab: PreferencesTab,
	active: PreferencesTab,
	colors: &gpui::colors::Colors,
	cx: &mut Context<PreferencesDialogContent>,
) -> gpui::Stateful<gpui::Div> {
	let selected = tab == active;
	let label = match tab {
		PreferencesTab::General => i18n::tr("preferences.tab.general"),
		PreferencesTab::Keyboard => i18n::tr("preferences.tab.keyboard"),
	};
	div()
		.id(match tab {
			PreferencesTab::General => "prefs-tab-general",
			PreferencesTab::Keyboard => "prefs-tab-keyboard",
		})
		.debug_selector(move || match tab {
			PreferencesTab::General => "prefs-tab-general".into(),
			PreferencesTab::Keyboard => "prefs-tab-keyboard".into(),
		})
		.px_3()
		.py_1()
		.rounded_md()
		.cursor_pointer()
		.bg(if selected { colors.selected } else { transparent() })
		.text_color(if selected { colors.selected_text } else { colors.text })
		.on_click(cx.listener(move |this, _event, _window, cx| {
			this.active = tab;
			cx.notify();
		}))
		.child(label)
}

/// A small pill-shaped text button (the keyboard tab's footer buttons). The
/// caller chains the click handler on the returned element.
fn pill_button(
	id: &'static str,
	label: impl Into<SharedString>,
	bg: gpui::Rgba,
	fg: gpui::Rgba,
) -> gpui::Stateful<gpui::Div> {
	let label: SharedString = label.into();
	div()
		.id(id)
		.px_3()
		.py_1()
		.rounded_md()
		.cursor_pointer()
		.bg(bg)
		.text_color(fg)
		.child(label)
}

// ---------------------------------------------------------------------------
// Preferences → Keyboard tab
// ---------------------------------------------------------------------------

/// A request the keyboard tab emits; the tabbed host re-emits it as
/// [`PreferencesEvent::ShortcutsChanged`].
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum KeyboardEvent {
	/// The shortcut overrides changed (a key was assigned / cleared / reset /
	/// imported). The host must re-bind the key map and rebuild the menu bar.
	Changed,
}

impl EventEmitter<KeyboardEvent> for KeyboardTabContent {}

/// One row of the keyboard tab: a menu-bar action with its hierarchy.
struct KeyboardRow {
	action: ActionId,
	/// The top-level menu title (the section header), localized.
	section: String,
	/// The full "Menu > Submenu > …" path, localized.
	path: String,
	/// The capture field's focus handle.
	focus: FocusHandle,
}

/// The Preferences → Keyboard tab: a searchable, section-grouped list of every
/// menu-bar action with a click-to-capture shortcut editor, plus Reset
/// Selected / Reset All and Import / Export — the Rust counterpart of the C++
/// `PreferencesKeyboardTab`.
///
/// # Capture
///
/// Clicking a row's shortcut field enters capture mode: a process-wide
/// [`gpui::App::intercept_keystrokes`] subscription suppresses the global key
/// map for as long as the capture is active, so the field's `on_key_down`
/// sees *every* key — including the letters, digits and arrows that the
/// registry binds (the shell's action listeners would otherwise swallow them
/// before the widget-level handlers run, the same problem Qt solves with
/// `QKeySequenceEdit`'s `ShortcutOverride`). The capture field then decides:
///
/// * any real key (plus modifiers) becomes the action's new binding;
/// * Backspace / Delete unbind the action (back to "None");
/// * Escape cancels the capture.
///
/// Every commit writes the override diff to `<config>/shortcuts` and emits
/// [`KeyboardEvent::Changed`], so the change is live immediately (no restart).
///
/// # Conflicts
///
/// Assigning a key that another action already binds *moves* the binding: the
/// displaced action loses the key (falling back to its remaining keys, or to
/// none) and the status line says so. Simple and explicit — the alternative
/// (refusing the assignment) would leave the user stuck when two popular
/// defaults collide.
pub struct KeyboardTabContent {
	query: Entity<EditableTextState>,
	rows: Vec<KeyboardRow>,
	filter: String,
	capturing: Option<usize>,
	selected: Option<usize>,
	confirm_reset_all: bool,
	status: Option<String>,
	/// The keystroke interceptor that routes every key to the capture logic
	/// while a row is capturing (see the capture notes above); it lives for
	/// the tab's whole lifetime and only acts while `capturing` is set.
	#[allow(dead_code)] // kept alive: dropping it unregisters the keystroke interceptor
	interceptor: Option<gpui::Subscription>,
}

impl KeyboardTabContent {
	/// Builds the tab, seeding one row per menu-bar action (the C++
	/// `setup_kbd_shortcuts` enumeration).
	pub fn new(_window: &mut Window, cx: &mut Context<Self>) -> Self {
		let query = cx.new(|cx| EditableTextState::new(StringStorage::default(), cx));
		cx.subscribe(&query, |this, _query, _event: &TextChanged, cx| {
			this.filter = this.query.read(cx).as_str().to_string();
			this.selected = None;
			cx.notify();
		})
		.detach();
		let rows = crate::app::menu_action_paths()
			.into_iter()
			.map(|(action, path)| {
				let section = path.split(" > ").next().unwrap_or_default().to_string();
				KeyboardRow {
					action,
					section,
					path,
					focus: cx.focus_handle(),
				}
			})
			.collect();
		let weak = cx.weak_entity();
		let interceptor = cx.intercept_keystrokes(move |event, _window, app| {
			// While any row is capturing, handle the key here — before the
			// shell's global key bindings (which would otherwise swallow it) —
			// and stop the event so it can neither reach an app action nor
			// bubble to the modal (Escape must cancel the capture, not close
			// the dialog).
			let Some(this) = weak.upgrade() else {
				return;
			};
			if this.read(app).capturing.is_some() {
				this.update(app, |this, cx| {
					this.handle_capture_key(&event.keystroke, cx);
				});
			}
		});
		Self {
			query,
			rows,
			filter: String::new(),
			capturing: None,
			selected: None,
			confirm_reset_all: false,
			status: None,
			interceptor: Some(interceptor),
		}
	}

	/// The number of menu-bar actions listed (tests).
	pub fn row_count(&self) -> usize {
		self.rows.len()
	}

	/// Enters capture mode for `index`: remembers the row and highlights it.
	/// The keystroke interceptor installed at construction does the rest — it
	/// sees every key before the shell's global bindings do, so the capture
	/// works regardless of which element currently has focus.
	fn begin_capture(&mut self, index: usize, cx: &mut Context<Self>) {
		self.capturing = Some(index);
		self.selected = Some(index);
		self.confirm_reset_all = false;
		cx.notify();
	}

	/// Leaves capture mode.
	fn end_capture(&mut self, cx: &mut Context<Self>) {
		self.capturing = None;
		cx.notify();
	}

	/// Handles one captured key (called from the keystroke interceptor, so it
	/// runs for every key while a row is capturing).
	fn handle_capture_key(&mut self, keystroke: &Keystroke, cx: &mut Context<Self>) {
		let Some(index) = self.capturing else {
			return;
		};
		match capture_decision(keystroke) {
			CaptureDecision::Ignore => cx.stop_propagation(),
			CaptureDecision::Cancel => {
				self.end_capture(cx);
				cx.stop_propagation();
			}
			CaptureDecision::Clear => {
				let action = self.rows[index].action;
				crate::actions::set_custom_shortcut(action.entry().cpp_id, Vec::new());
				let _ = crate::actions::save_custom_shortcuts();
				self.status = Some(i18n::tr("preferences.keyboard.cleared").to_string());
				self.end_capture(cx);
				cx.emit(KeyboardEvent::Changed);
				cx.stop_propagation();
			}
			CaptureDecision::Assign(canon) => {
				let action = self.rows[index].action;
				let stolen = crate::actions::steal_shortcut_for(action.entry(), &canon);
				crate::actions::set_custom_shortcut(action.entry().cpp_id, vec![canon]);
				let _ = crate::actions::save_custom_shortcuts();
				self.status = stolen.map(|previous| {
					i18n::tr("preferences.keyboard.conflict")
						.replace("{action}", i18n::tr(previous.entry().i18n_key))
				});
				self.end_capture(cx);
				cx.emit(KeyboardEvent::Changed);
				cx.stop_propagation();
			}
		}
	}

	/// Reset Selected: the selected row's action falls back to its registry
	/// default keys.
	fn reset_selected(&mut self, cx: &mut Context<Self>) {
		let Some(index) = self.selected else {
			return;
		};
		let action = self.rows[index].action;
		crate::actions::reset_custom_shortcut(action.entry().cpp_id);
		let _ = crate::actions::save_custom_shortcuts();
		self.status = Some(i18n::tr("preferences.keyboard.reset").to_string());
		cx.emit(KeyboardEvent::Changed);
		cx.notify();
	}

	/// Reset All: the first click arms an inline confirmation (the C++
	/// `QMessageBox` equivalent, kept inside the tab so the host modal
	/// machinery stays untouched); the second applies it.
	fn reset_all(&mut self, cx: &mut Context<Self>) {
		if !self.confirm_reset_all {
			self.confirm_reset_all = true;
			self.capturing = None;
			cx.notify();
			return;
		}
		crate::actions::reset_all_custom_shortcuts();
		let _ = crate::actions::save_custom_shortcuts();
		self.confirm_reset_all = false;
		self.status = Some(i18n::tr("preferences.keyboard.reset_all_done").to_string());
		cx.emit(KeyboardEvent::Changed);
		cx.notify();
	}

	/// Import: pick a `shortcuts` file, replace the overrides with its
	/// contents (anything unlisted falls back to default, like the C++ field
	/// walk), then save the effective state back to the configured location.
	fn import_shortcuts(&mut self, cx: &mut Context<Self>) {
		let receiver = cx.prompt_for_paths(PathPromptOptions {
			files: true,
			directories: false,
			multiple: false,
			prompt: Some(i18n::tr("preferences.keyboard.import").into()),
		});
		cx.spawn(async move |this, cx| {
			let Ok(Ok(Some(paths))) = receiver.await else {
				return;
			};
			let Some(path) = paths.first() else {
				return;
			};
			let result = crate::actions::load_custom_shortcuts_from(&path.to_string_lossy());
			this.update(cx, |this, cx| {
				match result {
					Ok(_) => {
						this.status =
							Some(i18n::tr("preferences.keyboard.imported").to_string());
						let _ = crate::actions::save_custom_shortcuts();
					}
					Err(_) => {
						this.status =
							Some(i18n::tr("preferences.keyboard.import_failed").to_string())
					}
				}
				this.capturing = None;
				this.confirm_reset_all = false;
				cx.emit(KeyboardEvent::Changed);
				cx.notify();
			});
		})
		.detach();
	}

	/// Export: write the current override diff to a picked file.
	fn export_shortcuts(&mut self, cx: &mut Context<Self>) {
		let receiver = cx.prompt_for_new_path(
			&std::path::PathBuf::from("."),
			Some("shortcuts"),
		);
		cx.spawn(async move |this, cx| {
			let Ok(Ok(Some(path))) = receiver.await else {
				return;
			};
			let result = crate::actions::save_custom_shortcuts_to(&path.to_string_lossy());
			this.update(cx, |this, cx| {
				this.status = Some(match result {
					Ok(_) => i18n::tr("preferences.keyboard.exported").to_string(),
					Err(_) => i18n::tr("preferences.keyboard.export_failed").to_string(),
				});
				cx.notify();
			});
		})
		.detach();
	}
}

impl Render for KeyboardTabContent {
	fn render(&mut self, _window: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
		let colors = cx.default_colors().clone();
		let weak = self.query.downgrade();
		let capturing = self.capturing;
		let selected = self.selected;

		// The search box.
		let search = div()
			.rounded_md()
			.border_1()
			.border_color(colors.border)
			.bg(colors.background)
			.px_2()
			.py_1()
			.child(text_input("keyboard-search-input").state(weak).accepts_input(true));

		// The grouped, filtered action list.
		let mut list = div()
			.id("keyboard-shortcut-list")
			.flex()
			.flex_col()
			.max_h(px(340.0))
			.overflow_y_scroll();
		let mut shown_section: Option<String> = None;
		for (index, row) in self.rows.iter().enumerate() {
			let label = i18n::tr(row.action.entry().i18n_key);
			let shortcut = crate::actions::display_shortcut(row.action);
			if !keyboard_filter_matches(label, &row.path, shortcut.as_deref(), &self.filter) {
				continue;
			}
			if shown_section.as_deref() != Some(row.section.as_str()) {
				list = list.child(section_header(&colors, row.section.clone().into()));
				shown_section = Some(row.section.clone());
			}
			let row_selected = selected == Some(index);
			let is_capturing = capturing == Some(index);
			let field_label: SharedString = if is_capturing {
				i18n::tr("preferences.keyboard.capturing").into()
			} else {
				shortcut
					.map(SharedString::from)
					.unwrap_or_else(|| i18n::tr("preferences.keyboard.unbound").into())
			};
			let row_path = row.path.clone();
			let focus = row.focus.clone();
			list = list.child(
				div()
					.id(ElementId::named_usize("keyboard-shortcut-row", index))
					.flex()
					.items_center()
					.gap_2()
					.px_1()
					.py_0p5()
					.rounded_md()
					.bg(if row_selected { colors.selected } else { transparent() })
					.on_click(cx.listener(move |this, _event, _window, cx| {
						this.selected = Some(index);
						cx.notify();
					}))
					.child(
						div()
							.flex_1()
							.flex_col()
							.child(div().text_color(colors.text).child(label))
							.child(
								div()
									.text_color(colors.disabled)
									.text_xs()
									.child(row_path),
							),
					)
					.child(
						div()
							.id(ElementId::named_usize("keyboard-shortcut-capture", index))
							.debug_selector(move || format!("keyboard-capture-{index}").into())
							.min_w(px(150.0))
							.px_2()
							.py_0p5()
							.rounded_md()
							.border_1()
							.border_color(if is_capturing {
								colors.selected
							} else {
								colors.border
							})
							.bg(colors.background)
							.text_color(colors.text)
							.cursor_pointer()
							.track_focus(&focus)
							.on_click(cx.listener(
								move |this, _event, _window, cx| {
									if this.capturing != Some(index) {
										this.begin_capture(index, cx);
									}
									cx.stop_propagation();
								},
							))
							.child(field_label),
					),
			);
		}

		// The footer: Import/Export on the left, Reset Selected/All on the
		// right (the inline Reset-All confirmation replaces them when armed).
		let footer = if self.confirm_reset_all {
			div()
				.flex()
				.items_center()
				.gap_2()
				.child(
					div()
						.flex_1()
						.text_color(colors.text)
						.child(i18n::tr("preferences.keyboard.reset_all.confirm")),
				)
				.child(
					pill_button(
						"prefs-keyboard-confirm-reset",
						i18n::tr("preferences.keyboard.reset_all"),
						colors.selected,
						colors.selected_text,
					)
					.on_click(cx.listener(|this, _event, _window, cx| this.reset_all(cx))),
				)
				.child(
					pill_button(
						"prefs-keyboard-cancel-reset",
						i18n::tr("dialog.cancel"),
						colors.background,
						colors.text,
					)
					.on_click(cx.listener(|this, _event, _window, cx| {
						this.confirm_reset_all = false;
						cx.notify();
					})),
				)
		} else {
			div()
				.flex()
				.items_center()
				.gap_2()
				.child(
					pill_button(
						"prefs-keyboard-import",
						i18n::tr("preferences.keyboard.import"),
						colors.background,
						colors.text,
					)
					.on_click(cx.listener(|this, _event, _window, cx| {
						this.import_shortcuts(cx)
					})),
				)
				.child(
					pill_button(
						"prefs-keyboard-export",
						i18n::tr("preferences.keyboard.export"),
						colors.background,
						colors.text,
					)
					.on_click(cx.listener(|this, _event, _window, cx| {
						this.export_shortcuts(cx)
					})),
				)
				.child(div().flex_1())
				.child(
					pill_button(
						"prefs-keyboard-reset-selected",
						i18n::tr("preferences.keyboard.reset_selected"),
						colors.background,
						colors.text,
					)
					.on_click(cx.listener(|this, _event, _window, cx| {
						this.reset_selected(cx)
					})),
				)
				.child(
					pill_button(
						"prefs-keyboard-reset-all",
						i18n::tr("preferences.keyboard.reset_all"),
						colors.background,
						colors.text,
					)
					.on_click(cx.listener(|this, _event, _window, cx| {
						this.reset_all(cx)
					})),
				)
		};

		div()
			.flex()
			.flex_col()
			.gap_3()
			.w_full()
			.child(search)
			.child(
				div()
					.flex()
					.text_color(colors.disabled)
					.text_xs()
					.child(i18n::tr("preferences.keyboard.action"))
					.child(div().flex_1())
					.child(i18n::tr("preferences.keyboard.shortcut")),
			)
			.child(list)
			.child(footer)
			.child(
				if let Some(status) = &self.status {
					div()
						.text_color(colors.disabled)
						.text_xs()
						.child(status.clone())
				} else {
					div()
				},
			)
	}
}

/// The outcome of one captured keystroke.
#[derive(Debug)]
enum CaptureDecision {
	/// A modifier-only key — keep capturing, ignore it.
	Ignore,
	/// Escape — cancel the capture without changing anything.
	Cancel,
	/// Backspace / Delete — clear the binding (unbind).
	Clear,
	/// A real key — the new binding, in canonical gpui keystroke form.
	Assign(String),
}

/// Decides what a capture field should do with a keystroke. Modifier-only
/// keys (a bare Shift / Ctrl / …) never bind; Backspace and Delete clear;
/// Escape cancels; anything else (with or without modifiers) becomes the new
/// binding.
fn capture_decision(keystroke: &Keystroke) -> CaptureDecision {
	match keystroke.key.as_str() {
		"escape" => CaptureDecision::Cancel,
		"backspace" | "delete" => CaptureDecision::Clear,
		// Modifier-only key presses (the parser represents a bare modifier as
		// the modifier's own key name).
		"shift" | "control" | "alt" | "cmd" | "super" | "win" | "fn" | "function"
		| "secondary" | "platform" => CaptureDecision::Ignore,
		"" => CaptureDecision::Ignore,
		_ => CaptureDecision::Assign(keystroke.unparse()),
	}
}

/// Whether an action row survives the keyboard tab's search query:
/// case-insensitive match against the action label, the localized menu path,
/// or the effective shortcut label.
fn keyboard_filter_matches(label: &str, path: &str, shortcut: Option<&str>, query: &str) -> bool {
	let query = query.trim();
	if query.is_empty() {
		return true;
	}
	let query = query.to_lowercase();
	let label_lower = label.to_lowercase();
	let full_path = format!("{path} > {label}").to_lowercase();
	label_lower.contains(&query)
		|| full_path.contains(&query)
		|| shortcut.is_some_and(|s| s.to_lowercase().contains(&query))
}

// ---------------------------------------------------------------------------
// Action search (Help > Search Actions…, the `/` key)
// ---------------------------------------------------------------------------

/// One item of the action search list.
struct ActionSearchItem {
	action: ActionId,
	/// The localized "Menu > Submenu > …" path.
	path: String,
}

/// The action search dialog content (the C++ `ActionSearch`): a search field
/// over every menu-bar action, live filtering, arrow-key selection and
/// Enter / double-click execution through the same dispatch path the menu
/// clicks take. Panel-context hotkeys (the `HIDDEN_MENU_ID` items) never
/// appear — they have no menu item, exactly like the C++ "only the menu bar"
/// enumeration.
pub struct ActionSearchContent {
	query: Entity<EditableTextState>,
	items: Vec<ActionSearchItem>,
	filter: String,
	selection: Option<usize>,
	/// The keystroke interceptor that routes Up/Down/Enter to the list while
	/// the dialog is open (see [`ActionSearchContent::new`]).
	#[allow(dead_code)] // kept alive: dropping it unregisters the keystroke interceptor
	interceptor: Option<gpui::Subscription>,
}

/// A request the action search dialog emits.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ActionSearchEvent {
	/// The user activated `action`; the host dispatches it (the same path the
	/// menu clicks take) and closes the dialog.
	Execute(ActionId),
}

impl EventEmitter<ActionSearchEvent> for ActionSearchContent {}

impl ActionSearchContent {
	/// Builds the dialog with every menu-bar action, subscribes to the search
	/// field so filtering re-runs on every keystroke, and installs a keystroke
	/// interceptor that routes Up / Down / Enter to the list.
	///
	/// The interceptor is needed because the shell's global key bindings run
	/// *before* the widget-level key handlers and would swallow Up/Down (they
	/// are the GoToPrevCut/GoToNextCut keys) and Enter — the same reason the
	/// Keyboard tab captures its keys through an interceptor. The dialog is
	/// modal, so the only text input alive while the interceptor is active is
	/// the search field; every other keystroke passes through untouched (in
	/// the real app the IME delivers text to the focused input independently
	/// of the key map, so typing keeps working).
	pub fn new(_window: &mut Window, cx: &mut Context<Self>) -> Self {
		let query = cx.new(|cx| EditableTextState::new(StringStorage::default(), cx));
		cx.subscribe(&query, |this, _query, _event: &TextChanged, cx| {
			this.filter = this.query.read(cx).as_str().to_string();
			let visible: Vec<usize> = this
				.items
				.iter()
				.enumerate()
				.filter(|(_, item)| search_filter_matches(item.action, &item.path, &this.filter))
				.map(|(index, _)| index)
				.collect();
			this.selection = selection_step(&visible, None, 1);
			cx.notify();
		})
		.detach();
		let items = crate::app::menu_action_paths()
			.into_iter()
			.map(|(action, path)| ActionSearchItem { action, path })
			.collect();
		let weak = cx.weak_entity();
		let interceptor = cx.intercept_keystrokes(move |event, _window, app| {
			// Only act on the keys the dialog owns; everything else (text
			// for the search field, Escape for the modal, …) passes through.
			if !matches!(event.keystroke.key.as_str(), "up" | "down" | "enter") {
				return;
			}
			let Some(this) = weak.upgrade() else {
				return;
			};
			let mut stop = false;
			this.update(app, |this, cx| match event.keystroke.key.as_str() {
				"up" => {
					this.move_selection(-1, cx);
					stop = true;
				}
				"down" => {
					this.move_selection(1, cx);
					stop = true;
				}
				"enter" => {
					this.execute(cx);
					stop = true;
				}
				// Escape deliberately falls through to the modal's own handler
				// (which closes the dialog); every other key passes to the
				// search field.
				_ => {}
			});
			if stop {
				app.stop_propagation();
			}
		});
		Self {
			query,
			items,
			filter: String::new(),
			selection: None,
			interceptor: Some(interceptor),
		}
	}

	/// The search field's focus handle (the host focuses it when the dialog
	/// opens, so the search is keyboard-first from the start).
	pub fn search_focus(&self, cx: &App) -> FocusHandle {
		self.query.read(cx).focus_handle(cx)
	}

	fn move_selection(&mut self, delta: i32, cx: &mut Context<Self>) {
		let visible: Vec<usize> = self
			.items
			.iter()
			.enumerate()
			.filter(|(_, item)| search_filter_matches(item.action, &item.path, &self.filter))
			.map(|(index, _)| index)
			.collect();
		self.selection = selection_step(&visible, self.selection, delta);
		cx.notify();
	}

	/// The currently selected action (tests).
	pub fn selected_action(&self) -> Option<ActionId> {
		self.selection.and_then(|index| self.items.get(index)).map(|item| item.action)
	}

	/// The current search filter (tests).
	pub fn filter(&self) -> &str {
		&self.filter
	}

	fn execute(&mut self, cx: &mut Context<Self>) {
		if let Some(index) = self.selection {
			if let Some(item) = self.items.get(index) {
				cx.emit(ActionSearchEvent::Execute(item.action));
			}
		}
	}
}

impl Render for ActionSearchContent {
	fn render(&mut self, _window: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
		let colors = cx.default_colors().clone();
		let weak = self.query.downgrade();
		let selection = self.selection;
		let visible: Vec<usize> = self
			.items
			.iter()
			.enumerate()
			.filter(|(_, item)| search_filter_matches(item.action, &item.path, &self.filter))
			.map(|(index, _)| index)
			.collect();

		let list = if visible.is_empty() {
			div()
				.id("action-search-list")
				.flex()
				.flex_col()
				.max_h(px(360.0))
				.overflow_y_scroll()
				.child(
					div()
						.text_color(colors.disabled)
						.text_xs()
						.child(if self.items.is_empty() {
							i18n::tr("actionsearch.no_actions")
						} else {
							i18n::tr("actionsearch.empty")
						}),
				)
		} else {
			div()
				.id("action-search-list")
				.flex()
				.flex_col()
				.max_h(px(360.0))
				.overflow_y_scroll()
				.children(visible.iter().map(|&index| {
					let item = &self.items[index];
					let label = i18n::tr(item.action.entry().i18n_key);
					let path = item.path.clone();
					let row_selected = selection == Some(index);
					div()
						.id(ElementId::named_usize("action-search-item", index))
						.flex()
						.items_center()
						.gap_2()
						.px_2()
						.py_0p5()
						.rounded_md()
						.bg(if row_selected { colors.selected } else { transparent() })
						.text_color(if row_selected {
							colors.selected_text
						} else {
							colors.text
						})
						.cursor_pointer()
						.on_click(cx.listener(move |this, _event, _window, cx| {
							this.selection = Some(index);
							cx.notify();
						}))
						.on_mouse_down(
							gpui::MouseButton::Left,
							cx.listener(
								move |this, event: &gpui::MouseDownEvent, _window, cx| {
									// Double-click executes.
									if event.click_count >= 2 {
										this.selection = Some(index);
										this.execute(cx);
									}
								},
							),
						)
						.child(
							div()
								.flex_1()
								.flex_col()
								.child(div().child(label))
								.child(
									div()
										.text_xs()
										.text_color(colors.disabled)
										.child(format!("({path})")),
								),
						)
				}))
		};

		div()
			.id("action-search-root")
			.flex()
			.flex_col()
			.gap_2()
			.w_full()
			.child(
				div()
					.rounded_md()
					.border_1()
					.border_color(colors.border)
					.bg(colors.background)
					.px_2()
					.py_1()
					.child(text_input("action-search-input").state(weak).accepts_input(true)),
			)
			.child(list)
	}
}

/// Whether an action-search item survives the query: case-insensitive match
/// against the action label or the full "Menu > Submenu > action" path.
fn search_filter_matches(action: ActionId, path: &str, query: &str) -> bool {
	let query = query.trim();
	if query.is_empty() {
		return true;
	}
	let query = query.to_lowercase();
	let label = i18n::tr(action.entry().i18n_key);
	label.to_lowercase().contains(&query)
		|| format!("{path} > {label}").to_lowercase().contains(&query)
}

/// The next selected index when moving `delta` (±1) through `visible` (the
/// indices of the currently visible rows), wrapping around. `None` returns
/// the first (for a downward move) / last (for an upward move).
fn selection_step(visible: &[usize], current: Option<usize>, delta: i32) -> Option<usize> {
	if visible.is_empty() {
		return None;
	}
	let position = current.and_then(|c| visible.iter().position(|&v| v == c));
	let next = match position {
		Some(pos) => (pos as i64 + delta as i64).rem_euclid(visible.len() as i64) as usize,
		None if delta > 0 => 0,
		None => visible.len() - 1,
	};
	Some(visible[next])
}

#[cfg(test)]
mod tests {
	use super::*;

	fn keystroke(key: &str) -> Keystroke {
		gpui::Keystroke::parse(key).unwrap()
	}

	#[test]
	fn capture_decision_handles_all_shapes() {
		assert!(matches!(capture_decision(&keystroke("escape")), CaptureDecision::Cancel));
		assert!(matches!(
			capture_decision(&keystroke("backspace")),
			CaptureDecision::Clear
		));
		assert!(matches!(
			capture_decision(&keystroke("delete")),
			CaptureDecision::Clear
		));
		// Bare modifiers never bind.
		assert!(matches!(
			capture_decision(&keystroke("shift")),
			CaptureDecision::Ignore
		));
		assert!(matches!(
			capture_decision(&keystroke("control")),
			CaptureDecision::Ignore
		));
		assert!(matches!(
			capture_decision(&keystroke("alt")),
			CaptureDecision::Ignore
		));
		// A real key (with or without modifiers) becomes the canonical binding.
		match capture_decision(&keystroke("secondary-s")) {
			CaptureDecision::Assign(canon) => {
				assert_eq!(
					canon,
					gpui::Keystroke::parse("secondary-s").unwrap().unparse()
				);
			}
			other => panic!("expected assign, got {other:?}"),
		}
	}

	#[test]
	fn keyboard_filter_matches_name_path_and_shortcut() {
		assert!(keyboard_filter_matches("Save Project", "File", Some("⌘S"), "save"));
		assert!(keyboard_filter_matches("Save Project", "File", Some("⌘S"), "file > save"));
		// Shortcut matching is case-insensitive.
		assert!(keyboard_filter_matches("Save Project", "File", Some("⌘S"), "⌘s"));
		assert!(!keyboard_filter_matches("Save Project", "File", Some("⌘S"), "undo"));
		// Empty query matches everything; rows without a shortcut match only
		// by name/path.
		assert!(keyboard_filter_matches("About Oak…", "Help", None, ""));
		assert!(keyboard_filter_matches("About Oak…", "Help", None, "help"));
		assert!(!keyboard_filter_matches("About Oak…", "Help", None, "⌘"));
	}

	#[test]
	fn search_filter_matches_label_or_path() {
		let _guard = crate::i18n::lang_test_lock().lock().unwrap();
		crate::i18n::set_language(crate::i18n::Language::EnUs);
		assert!(search_filter_matches(ActionId::NewProject, "File", "new"));
		assert!(search_filter_matches(ActionId::NewProject, "File", "file > new"));
		assert!(!search_filter_matches(ActionId::NewProject, "File", "undo"));
		assert!(search_filter_matches(ActionId::NewProject, "File", ""));
	}

	#[test]
	fn selection_step_wraps_and_respects_visibility() {
		assert_eq!(selection_step(&[2, 5, 7], None, 1), Some(2));
		assert_eq!(selection_step(&[2, 5, 7], None, -1), Some(7));
		assert_eq!(selection_step(&[2, 5, 7], Some(2), 1), Some(5));
		assert_eq!(selection_step(&[2, 5, 7], Some(7), 1), Some(2));
		assert_eq!(selection_step(&[2, 5, 7], Some(2), -1), Some(7));
		assert_eq!(selection_step(&[], None, 1), None);
	}

	#[test]
	fn keyboard_rows_cover_the_menu_bar() {
		let _guard = crate::actions::shortcuts_test_lock().lock().unwrap();
		let _lang = crate::i18n::lang_test_lock().lock().unwrap();
		crate::i18n::set_language(crate::i18n::Language::EnUs);
		let rows = crate::app::menu_action_paths();
		assert!(!rows.is_empty());
		// Every listed action resolves to a registry entry with a menu item.
		for (action, path) in &rows {
			assert_ne!(action.menu_id(), crate::actions::HIDDEN_MENU_ID);
			assert!(!path.is_empty(), "action {action:?} has an empty path");
		}
	}
}
