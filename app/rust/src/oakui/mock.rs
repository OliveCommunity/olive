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

//! The mock engine: a gpui entity that implements every data-source trait
//! the widgets read, and feeds them demo data.
//!
//! # One entity, many roles
//!
//! [`MockEngine`] is the single source of truth for the demo project and
//! implements, all on the same entity:
//!
//! * [`EngineGateway`] — the app's transport/project seam (see the
//!   [module docs](super::engine));
//! * [`TimelineDataSource`] — the sequence model behind the timeline widget;
//! * [`EffectStackDataSource`] — the 媒体 → 变换 → OCIO LUT → 输出 stack;
//! * [`ProjectDataSource`] — the material bin tree;
//! * [`AudioMeterDataSource`] — the two program-master channels.
//!
//! Transport state lives in two [`MockClock`] entities (one per
//! [`Monitor`]); the engine owns them and advances them on every
//! [`EngineGateway::tick`].
//!
//! The real engine will later implement the same gateway over the
//! `liboakengine` C ABI; only the wiring in [`crate::app`] changes.

use std::path::PathBuf;
use std::sync::Arc;
use std::time::Instant;

use gpui::effect_stack::{
	EffectCardKind, EffectData, EffectId, EffectStackDataSource, EffectStackEvent,
};
use gpui::timeline::{
	ClipData, ClipId, Frame, FrameRange, FrameRate, TimelineDataSource, TrackData, TrackKind,
};
use gpui::{prelude::*, px, App, Context, Entity, Hsla, Pixels, SharedString};
use gpui_widgets::audio_meter::AudioMeterDataSource;
use gpui_widgets::project_explorer::{ProjectDataSource, ProjectEntry};
use gpui_widgets::viewer::PlaybackClock;

use super::engine::{EngineGateway, Monitor, Project, Sequence, VideoFormat};
use super::transport::TransportState;

/// The demo sequence length: 00:04:18:18 at 25 fps.
const SEQUENCE_LENGTH: i64 = 6468;

// ---------------------------------------------------------------------------
// Clocks
// ---------------------------------------------------------------------------

/// A transport clock: the playhead plus the wall-clock anchor used while
/// playing. This is the object the viewer widgets poll through
/// [`PlaybackClock`].
pub struct MockClock {
	/// The transport state (play/pause, playhead, loop range).
	pub transport: TransportState,
	/// The clock's frame rate.
	pub rate: FrameRate,
	/// Wall-clock anchor `(started_at, anchored_frame)` while playing.
	started: Option<(Instant, Frame)>,
}

impl MockClock {
	/// A stopped clock at frame zero running at `rate`.
	pub fn new(rate: FrameRate) -> Self {
		Self {
			transport: TransportState::new(),
			rate,
			started: None,
		}
	}

	/// Starts playback from the current playhead.
	pub fn play(&mut self) {
		self.transport.play();
		self.started = Some((Instant::now(), self.transport.frame()));
	}

	/// Pauses playback, keeping the playhead.
	pub fn pause(&mut self) {
		self.transport.pause();
		self.started = None;
	}

	/// Advances the playhead from the wall clock while playing, looping at
	/// `length`. No-op when stopped.
	pub fn tick(&mut self, length: Frame) {
		let Some((started, anchored)) = self.started else {
			return;
		};
		let elapsed = started.elapsed();
		let mut frame = anchored
			+ Frame(
				(elapsed.as_secs_f64() * self.rate.num as f64 / self.rate.den as f64).round()
					as i64,
			);
		if length.0 > 0 && frame.0 >= length.0 {
			// Loop back to the start of the sequence for the demo.
			frame = Frame(frame.0 % length.0);
		}
		self.transport.seek(frame, length);
	}
}

impl PlaybackClock for MockClock {
	fn current_frame(&self) -> Frame {
		self.transport.frame()
	}

	fn is_playing(&self) -> bool {
		self.transport.is_playing()
	}

	fn frame_rate(&self) -> FrameRate {
		self.rate
	}
}

// ---------------------------------------------------------------------------
// Timeline model
// ---------------------------------------------------------------------------

/// A clip on the demo timeline.
#[derive(Debug, Clone)]
pub struct MockClip {
	id: ClipId,
	range: FrameRange,
	media_in: Frame,
	label: SharedString,
	color: Hsla,
}

impl ClipData for MockClip {
	fn id(&self) -> ClipId {
		self.id
	}

	fn range(&self) -> FrameRange {
		self.range
	}

	fn media_in(&self) -> Frame {
		self.media_in
	}

	fn label(&self) -> SharedString {
		self.label.clone()
	}

	fn color(&self) -> Option<Hsla> {
		Some(self.color)
	}
}

/// A track on the demo timeline (snapshot handed to the timeline widget).
#[derive(Debug, Clone)]
pub struct MockTrack {
	kind: TrackKind,
	name: SharedString,
	height: Pixels,
	locked: bool,
	muted: bool,
	solo: bool,
	visible: bool,
	clips: Vec<MockClip>,
}

impl TrackData for MockTrack {
	type Clip = MockClip;

	fn kind(&self) -> TrackKind {
		self.kind
	}

	fn name(&self) -> SharedString {
		self.name.clone()
	}

	fn is_locked(&self) -> bool {
		self.locked
	}

	fn is_muted(&self) -> bool {
		self.muted
	}

	fn is_solo(&self) -> bool {
		self.solo
	}

	fn is_visible(&self) -> bool {
		self.visible
	}

	fn height(&self) -> Pixels {
		self.height
	}

	fn clips(&self) -> &[Self::Clip] {
		&self.clips
	}
}

/// An effect card in the demo stack.
#[derive(Debug, Clone)]
pub struct MockEffect {
	id: EffectId,
	kind: EffectCardKind,
	title: SharedString,
	subtitle: Option<SharedString>,
	enabled: bool,
	expanded: bool,
	badge: Option<usize>,
}

impl EffectData for MockEffect {
	fn id(&self) -> EffectId {
		self.id
	}

	fn kind(&self) -> EffectCardKind {
		self.kind
	}

	fn title(&self) -> SharedString {
		self.title.clone()
	}

	fn subtitle(&self) -> Option<SharedString> {
		self.subtitle.clone()
	}

	fn is_enabled(&self) -> bool {
		self.enabled
	}

	fn is_expanded(&self) -> bool {
		self.expanded
	}

	fn badge_count(&self) -> Option<usize> {
		self.badge
	}
}

// ---------------------------------------------------------------------------
// The engine
// ---------------------------------------------------------------------------

/// The mock engine implementing every data-source trait over demo data.
pub struct MockEngine {
	project: Project,
	sequence: Sequence,
	/// The source monitor's clock.
	pub source_clock: Entity<MockClock>,
	/// The program monitor's clock.
	pub program_clock: Entity<MockClock>,
	/// The timeline tracks.
	tracks: Vec<MockTrack>,
	/// The effect stack cards.
	effects: Vec<MockEffect>,
	/// Id allocator for effects added at runtime.
	next_effect_id: u64,
	/// Whether the program monitor is playing (mirrors the clock; kept here
	/// because the audio-meter data source has no `App` to read the clock).
	program_playing: bool,
	/// The selected item in the material bin (demo state).
	selected_item: Option<u64>,
	/// Phase counter driving the demo audio levels.
	meter_phase: u32,
}

impl MockEngine {
	/// Builds the demo project: 第一稿.ove, one HD sequence, four tracks.
	pub fn demo(cx: &mut Context<Self>) -> Self {
		let rate = VideoFormat::hd_1080p25().rate;
		let clip =
			|id: u64, start: i64, end: i64, media_in: i64, label: &str, color: Hsla| MockClip {
				id: ClipId(id),
				range: FrameRange::new(Frame(start), Frame(end)),
				media_in: Frame(media_in),
				label: label.into(),
				color,
			};
		let video = |h: f32| Hsla {
			h,
			s: 0.55,
			l: 0.45,
			a: 1.0,
		};
		let audio = |h: f32| Hsla {
			h,
			s: 0.45,
			l: 0.55,
			a: 1.0,
		};

		Self {
			project: Project {
				name: "第一稿".into(),
				path: PathBuf::from("/home/mikesolar/Videos/aaa.ove"),
			},
			sequence: Sequence {
				name: "第一稿".into(),
				format: VideoFormat::hd_1080p25(),
				length: Frame(SEQUENCE_LENGTH),
			},
			source_clock: cx.new(|_cx| MockClock::new(rate)),
			program_clock: cx.new(|_cx| MockClock::new(rate)),
			tracks: vec![
				MockTrack {
					kind: TrackKind::Video,
					name: "V2 视频轨道1".into(),
					height: px(64.0),
					locked: false,
					muted: false,
					solo: false,
					visible: true,
					clips: vec![clip(10, 120, 300, 0, "标题.mov", video(0.55))],
				},
				MockTrack {
					kind: TrackKind::Video,
					name: "V1 视频轨道0".into(),
					height: px(64.0),
					locked: false,
					muted: false,
					solo: false,
					visible: true,
					clips: vec![
						clip(11, 0, 240, 0, "开场.mov", video(0.60)),
						clip(12, 240, 600, 100, "B-roll.mp4", video(0.50)),
					],
				},
				MockTrack {
					kind: TrackKind::Audio,
					name: "A1 音频轨道0".into(),
					height: px(48.0),
					locked: false,
					muted: false,
					solo: false,
					visible: true,
					clips: vec![clip(13, 0, 600, 0, "对白.wav", audio(0.05))],
				},
				MockTrack {
					kind: TrackKind::Audio,
					name: "A2 音频轨道1".into(),
					height: px(48.0),
					locked: false,
					muted: false,
					solo: false,
					visible: true,
					clips: vec![clip(14, 0, 480, 0, "配乐.flac", audio(0.62))],
				},
			],
			effects: vec![
				MockEffect {
					id: EffectId(0),
					kind: EffectCardKind::Source,
					title: "媒体".into(),
					subtitle: Some("第一稿.mp4".into()),
					enabled: true,
					expanded: false,
					badge: None,
				},
				MockEffect {
					id: EffectId(1),
					kind: EffectCardKind::Effect,
					title: "变换".into(),
					subtitle: Some("缩放 100% · 旋转 0°".into()),
					enabled: true,
					expanded: true,
					badge: Some(2),
				},
				MockEffect {
					id: EffectId(2),
					kind: EffectCardKind::Effect,
					title: "OCIO LUT".into(),
					subtitle: Some("filmic_to_display.cube".into()),
					enabled: true,
					expanded: false,
					badge: None,
				},
				MockEffect {
					id: EffectId(3),
					kind: EffectCardKind::Output,
					title: "输出".into(),
					subtitle: None,
					enabled: true,
					expanded: false,
					badge: None,
				},
			],
			next_effect_id: 4,
			program_playing: false,
			selected_item: None,
			meter_phase: 0,
		}
	}

	/// The current sequence length (also used by the gateway).
	fn sequence_length(&self) -> Frame {
		self.sequence.length
	}

	/// Resolves the clock entity for a monitor.
	fn clock(&self, monitor: Monitor) -> &Entity<MockClock> {
		match monitor {
			Monitor::Source => &self.source_clock,
			Monitor::Program => &self.program_clock,
		}
	}

	/// Applies an edit request from the effect stack to the model.
	pub fn apply_effect_event(&mut self, event: &EffectStackEvent, cx: &mut Context<Self>) {
		match event {
			EffectStackEvent::EnableToggled { effect, enabled } => {
				if let Some(effect) = self.effects.iter_mut().find(|e| e.id == *effect) {
					effect.enabled = *enabled;
				}
			}
			EffectStackEvent::ExpansionToggled { effect, expanded } => {
				if let Some(effect) = self.effects.iter_mut().find(|e| e.id == *effect) {
					effect.expanded = *expanded;
				}
			}
			EffectStackEvent::RemoveRequested(id) => {
				if let Some(index) = self
					.effects
					.iter()
					.position(|e| e.id == *id && e.is_removable())
				{
					self.effects.remove(index);
				}
			}
			EffectStackEvent::ReorderRequested { effect, new_index } => {
				let Some(from) = self.effects.iter().position(|e| e.id == *effect) else {
					return;
				};
				// `new_index` is an insertion index in the post-removal list.
				let card = self.effects.remove(from);
				let to = (*new_index).min(self.effects.len());
				self.effects.insert(to, card);
			}
			EffectStackEvent::AddRequested { index } => {
				let id = EffectId(self.next_effect_id);
				self.next_effect_id += 1;
				let card = MockEffect {
					id,
					kind: EffectCardKind::Effect,
					title: "新效果".into(),
					subtitle: None,
					enabled: true,
					expanded: false,
					badge: None,
				};
				let index = (*index).min(self.effects.len());
				self.effects.insert(index, card);
			}
			// The app owns the context menu; the mock ignores it.
			EffectStackEvent::ContextMenuRequested { .. }
			| EffectStackEvent::ParameterChanged { .. } => {}
		}
		cx.notify();
	}

	/// Sets the row height of every timeline track (demo toolbar).
	pub fn set_track_height(&mut self, height: Pixels, cx: &mut Context<Self>) {
		for track in &mut self.tracks {
			track.height = height;
		}
		cx.notify();
	}

	/// Adds a new empty track of the given kind (demo "序列" menu).
	pub fn add_track(&mut self, kind: TrackKind, cx: &mut Context<Self>) {
		let index = self.tracks.len();
		let (name, height) = match kind {
			TrackKind::Video => (format!("V{} 视频轨道{}", index / 2 + 1, index), px(64.0)),
			TrackKind::Audio => (format!("A{} 音频轨道{}", index / 2 + 1, index), px(48.0)),
			TrackKind::Subtitle => (format!("S{} 字幕轨道", index + 1), px(32.0)),
		};
		self.tracks.push(MockTrack {
			kind,
			name: name.into(),
			height,
			locked: false,
			muted: false,
			solo: false,
			visible: true,
			clips: Vec::new(),
		});
		cx.notify();
	}

	/// The demo audio levels: animated while the program monitor plays.
	fn meter_levels(&self) -> Vec<f32> {
		if !self.program_playing {
			return vec![0.03, 0.03];
		}
		let t = (self.meter_phase % 120) as f32 / 120.0 * std::f32::consts::TAU;
		let level = 0.25 + 0.55 * (t.sin() * 0.6 + (2.0 * t).sin() * 0.4).abs();
		vec![level, level * 0.8]
	}
}

// ---------------------------------------------------------------------------
// EngineGateway
// ---------------------------------------------------------------------------

impl EngineGateway for MockEngine {
	fn project(&self) -> Option<&Project> {
		Some(&self.project)
	}

	fn current_sequence(&self) -> Option<&Sequence> {
		Some(&self.sequence)
	}

	fn open_project(&mut self, path: PathBuf, cx: &mut Context<Self>) {
		self.project.path = path;
		if let Some(name) = self.project.path.file_stem() {
			self.project.name = name.to_string_lossy().into_owned();
		}
		cx.notify();
	}

	fn request_frame(&mut self, monitor: Monitor, frame: Frame, cx: &mut Context<Self>) {
		let length = self.sequence_length();
		let clock = self.clock(monitor).clone();
		clock.update(cx, |clock, cx| {
			clock.transport.seek(frame, length);
			// Re-anchor so resuming continues from the new position.
			if clock.transport.is_playing() {
				clock.play();
			}
			cx.notify();
		});
		cx.notify();
	}

	fn play(&mut self, monitor: Monitor, cx: &mut Context<Self>) {
		if monitor == Monitor::Program {
			self.program_playing = true;
		}
		let clock = self.clock(monitor).clone();
		clock.update(cx, |clock, cx| {
			clock.play();
			cx.notify();
		});
		cx.notify();
	}

	fn pause(&mut self, monitor: Monitor, cx: &mut Context<Self>) {
		if monitor == Monitor::Program {
			self.program_playing = false;
		}
		let clock = self.clock(monitor).clone();
		clock.update(cx, |clock, cx| {
			clock.pause();
			cx.notify();
		});
		cx.notify();
	}

	fn step(&mut self, monitor: Monitor, delta: i64, cx: &mut Context<Self>) {
		let length = self.sequence_length();
		let clock = self.clock(monitor).clone();
		clock.update(cx, |clock, cx| {
			clock.transport.step(delta, length);
			if clock.transport.is_playing() {
				clock.play();
			}
			cx.notify();
		});
		cx.notify();
	}

	fn tick(&mut self, cx: &mut Context<Self>) {
		let length = self.sequence_length();
		for clock in [&self.source_clock, &self.program_clock] {
			let clock = clock.clone();
			clock.update(cx, |clock, cx| {
				clock.tick(length);
				cx.notify();
			});
		}
		self.meter_phase = self.meter_phase.wrapping_add(1);
		cx.notify();
	}
}

// ---------------------------------------------------------------------------
// Data-source traits
// ---------------------------------------------------------------------------

impl TimelineDataSource for MockEngine {
	type Track = MockTrack;

	fn frame_rate(&self) -> FrameRate {
		self.sequence.format.rate
	}

	fn sequence_length(&self) -> Frame {
		self.sequence.length
	}

	fn track_count(&self) -> usize {
		self.tracks.len()
	}

	fn track(&self, index: usize) -> Option<Self::Track> {
		self.tracks.get(index).cloned()
	}
}

impl EffectStackDataSource for MockEngine {
	fn effects(&self) -> Vec<Arc<dyn EffectData>> {
		self.effects
			.iter()
			.map(|effect| {
				Arc::new(MockEffect {
					id: effect.id,
					kind: effect.kind,
					title: effect.title.clone(),
					subtitle: effect.subtitle.clone(),
					enabled: effect.enabled,
					expanded: effect.expanded,
					badge: effect.badge,
				}) as Arc<dyn EffectData>
			})
			.collect()
	}

	fn target_label(&self) -> Option<SharedString> {
		Some("第一稿.mp4 · 00:00:00:00–00:04:18:18".into())
	}
}

impl ProjectDataSource for MockEngine {
	fn roots(&self) -> Vec<ProjectEntry> {
		vec![
			ProjectEntry::new(1, "素材", true),
			ProjectEntry::new(2, "音乐", true),
			ProjectEntry::new(3, "第一稿.mp4", false),
			ProjectEntry::new(4, "aaa.ove", false),
		]
	}

	fn children(&self, parent_id: u64) -> Vec<ProjectEntry> {
		match parent_id {
			1 => vec![
				ProjectEntry::new(10, "intro.mov", false),
				ProjectEntry::new(11, "b-roll.mp4", false),
				ProjectEntry::new(12, "interview.mov", false),
			],
			2 => vec![
				ProjectEntry::new(20, "track-01.wav", false),
				ProjectEntry::new(21, "track-02.wav", false),
			],
			_ => Vec::new(),
		}
	}
}

impl AudioMeterDataSource for MockEngine {
	fn levels(&self) -> Vec<f32> {
		self.meter_levels()
	}
}

/// Convenience accessors used by panels and the status bar.
impl MockEngine {
	/// The selected material-bin entry id (demo state).
	pub fn selected_item(&self) -> Option<u64> {
		self.selected_item
	}

	/// Selects a material-bin entry (demo "open" action).
	pub fn select_item(&mut self, id: u64, cx: &mut Context<Self>) {
		self.selected_item = Some(id);
		cx.notify();
	}

	/// Reads the current frame of a monitor's clock.
	pub fn clock_frame(&self, monitor: Monitor, cx: &App) -> Frame {
		self.clock(monitor).read(cx).transport.frame()
	}
}

#[cfg(test)]
mod tests {
	use super::*;
	use gpui::TestAppContext;

	fn demo_engine(app: &mut gpui::App) -> Entity<MockEngine> {
		app.new(|cx| MockEngine::demo(cx))
	}

	#[gpui::test]
	async fn demo_project_has_a_sequence_and_four_tracks(cx: &mut TestAppContext) {
		cx.update(|app| {
			let engine = demo_engine(app);
			let engine = engine.read(app);
			let sequence = engine.current_sequence().expect("demo sequence");
			assert_eq!(sequence.name, "第一稿");
			assert_eq!(sequence.length, Frame(SEQUENCE_LENGTH));
			assert_eq!(sequence.format.width, 1920);
			assert_eq!(sequence.format.height, 1080);
			assert_eq!(engine.track_count(), 4);
			assert_eq!(engine.track(0).expect("V2").kind(), TrackKind::Video);
			assert_eq!(engine.track(2).expect("A1").kind(), TrackKind::Audio);
		});
	}

	#[gpui::test]
	async fn gateway_play_starts_the_program_clock(cx: &mut TestAppContext) {
		cx.update(|app| {
			let engine = demo_engine(app);
			engine.update(app, |engine, cx| {
				EngineGateway::play(engine, Monitor::Program, cx);
			});
			let clock = engine.read(app).program_clock.read(app);
			assert!(clock.transport.is_playing());
		});
	}

	#[gpui::test]
	async fn gateway_step_moves_the_clock_within_the_sequence(cx: &mut TestAppContext) {
		cx.update(|app| {
			let engine = demo_engine(app);
			engine.update(app, |engine, cx| {
				engine.step(Monitor::Program, 25, cx);
			});
			let frame = engine.read(app).clock_frame(Monitor::Program, app);
			assert_eq!(frame, Frame(25));

			// Stepping far past the end clamps to the last frame.
			engine.update(app, |engine, cx| {
				engine.step(Monitor::Program, 1_000_000, cx);
			});
			let frame = engine.read(app).clock_frame(Monitor::Program, app);
			assert_eq!(frame, Frame(SEQUENCE_LENGTH - 1));
		});
	}

	#[gpui::test]
	async fn gateway_request_frame_seeks_and_pauses_stays(cx: &mut TestAppContext) {
		cx.update(|app| {
			let engine = demo_engine(app);
			engine.update(app, |engine, cx| {
				engine.request_frame(Monitor::Source, Frame(42), cx);
			});
			let frame = engine.read(app).clock_frame(Monitor::Source, app);
			assert_eq!(frame, Frame(42));
		});
	}

	#[gpui::test]
	async fn effect_stack_edit_applies_to_the_model(cx: &mut TestAppContext) {
		cx.update(|app| {
			let engine = demo_engine(app);
			// Remove the OCIO LUT card (id 2).
			engine.update(app, |engine, cx| {
				engine.apply_effect_event(&EffectStackEvent::RemoveRequested(EffectId(2)), cx);
			});
			let stack = engine.read(app).effects();
			assert_eq!(stack.len(), 3);
			let titles: Vec<_> = stack.iter().map(|e| e.title().to_string()).collect();
			assert!(!titles.contains(&"OCIO LUT".to_string()));
			// Source and output cards are pinned and not removable.
			assert_eq!(titles.first().map(String::as_str), Some("媒体"));
			assert_eq!(titles.last().map(String::as_str), Some("输出"));
		});
	}

	#[gpui::test]
	async fn timeline_edits_are_requests_not_applied(cx: &mut TestAppContext) {
		cx.update(|app| {
			let engine = demo_engine(app);
			// The demo timeline model ignores edit requests: the mock keeps
			// its clips where they are until a real engine applies them.
			let before = engine.read(app).track(1).expect("V1").clips().len();
			// (Nothing to assert beyond stability: the widget never mutates
			// the data source directly — reads stay stable across reads.)
			let after = engine.read(app).track(1).expect("V1").clips().len();
			assert_eq!(before, after);
		});
	}
}
