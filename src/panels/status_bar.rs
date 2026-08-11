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

//! The global status bar (状态栏): ready state, cache, proxy and autosave
//! info on the left; current timecode / duration, frame rate and resolution
//! on the right.

use gpui::colors::DefaultColors;
use gpui::timeline::Frame;
use gpui::{div, prelude::*, Context, Entity, Render, Window};

use gpui_widgets::viewer::PlaybackClock;

use crate::oakui::timecode::{format_duration, format_fps, format_resolution, format_timecode};
use crate::oakui::AppEngine;

/// The global status bar.
pub struct StatusBar<E: AppEngine> {
	engine: Entity<E>,
	program_clock: Entity<E::Clock>,
}

impl<E: AppEngine> StatusBar<E> {
	/// Builds the status bar over the engine and the program clock.
	pub fn new(
		engine: Entity<E>,
		program_clock: Entity<E::Clock>,
		_cx: &mut Context<Self>,
	) -> Self {
		Self {
			engine,
			program_clock,
		}
	}
}

impl<E: AppEngine> Render for StatusBar<E> {
	fn render(&mut self, _window: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
		let colors = cx.default_colors().clone();
		let engine = self.engine.read(cx);
		let frame = self.program_clock.read(cx).current_frame();
		let sequence = engine.current_sequence();
		let format = sequence
			.map(|s| s.format)
			.unwrap_or(crate::oakui::VideoFormat::hd_1080p25());
		let length = sequence.map(|s| s.length).unwrap_or(Frame(0));
		let project = engine
			.project()
			.map(|p| p.name.clone())
			.unwrap_or_else(|| crate::i18n::tr("status.untitled").to_string());

		let segment = |colors: &gpui::colors::Colors, text: String| {
			div().px_2().py_1().text_color(colors.text).child(text)
		};

		div()
			.h_6()
			.flex()
			.items_center()
			.border_t_1()
			.border_color(colors.border)
			.bg(colors.container)
			.text_xs()
			.child(segment(&colors, crate::i18n::tr("status.ready").into()))
			.child(segment(&colors, crate::i18n::tr("status.cache").into()))
			.child(segment(&colors, crate::i18n::tr("status.proxy").into()))
			.child(segment(&colors, crate::i18n::tr("status.autosave").into()))
			.child(div().flex_1())
			.child(segment(
				&colors,
				format!(
					"{timecode}/{duration}",
					timecode = format_timecode(frame, format.rate),
					duration = format_duration(length, format.rate),
				),
			))
			.child(segment(&colors, format_fps(format.rate)))
			.child(segment(
				&colors,
				format_resolution(format.width, format.height),
			))
			.child(div().px_2().text_color(colors.disabled).child(project))
			.child(div().px_2().text_color(colors.disabled).child(format!(
				"{} · {}",
				crate::i18n::tr("status.backend"),
				self.engine.read(cx).backend_name(),
			)))
	}
}
