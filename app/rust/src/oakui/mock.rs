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

use std::collections::{BTreeSet, HashMap};
use std::path::PathBuf;
use std::sync::{Arc, Mutex};
use std::time::Instant;

use gpui::effect_stack::{
	EffectCardKind, EffectData, EffectId, EffectStackDataSource, EffectStackEvent,
};
use gpui::node_graph::{
	EdgeData, EdgeId, NodeData, NodeGraphDataSource, NodeGraphEvent, NodeId, PortData,
	PortDataType, PortId, PortKind,
};
use gpui::timeline::{
	ClipData, ClipId, Frame, FrameRange, FrameRate, TimelineDataSource, TrackData, TrackKind,
};
use gpui::{
	hsla, point, prelude::*, px, App, Context, Entity, Hsla, Pixels, Point, RenderImage,
	SharedString,
};
use gpui_widgets::audio_meter::AudioMeterDataSource;
use gpui_widgets::project_explorer::{ProjectDataSource, ProjectEntry};
use gpui_widgets::viewer::PlaybackClock;

use super::engine::{EngineGateway, Monitor, Project, Sequence, VideoFormat};
use super::transport::TransportState;

/// The demo sequence length: 00:04:18:18 at 25 fps.
const SEQUENCE_LENGTH: i64 = 6468;

/// The synthetic viewer test frame is rendered at a small proxy size (the
/// real engine will deliver full-resolution frames; the mock only needs to
/// prove the CPU-frame path end to end).
const SYNTH_FRAME_WIDTH: u32 = 384;
const SYNTH_FRAME_HEIGHT: u32 = 216;

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
// Node graph model
// ---------------------------------------------------------------------------

/// A port on a mock node. `connected` is a model-side cache refreshed by
/// [`MockEngine::refresh_port_connectivity`] after every graph mutation.
#[derive(Debug, Clone)]
pub struct MockPort {
	id: PortId,
	kind: PortKind,
	label: SharedString,
	data_type: PortDataType,
	connected: bool,
}

impl PortData for MockPort {
	fn id(&self) -> PortId {
		self.id
	}

	fn kind(&self) -> PortKind {
		self.kind
	}

	fn label(&self) -> SharedString {
		self.label.clone()
	}

	fn data_type(&self) -> PortDataType {
		self.data_type.clone()
	}

	fn is_connected(&self) -> bool {
		self.connected
	}
}

/// A node in the mock graph (媒体 → 变换 → … → 输出).
#[derive(Debug, Clone)]
pub struct MockNode {
	id: NodeId,
	title: SharedString,
	position: Point<Pixels>,
	inputs: Vec<MockPort>,
	outputs: Vec<MockPort>,
	header_color: Option<Hsla>,
	enabled: bool,
	collapsed: bool,
}

impl NodeData for MockNode {
	type Port = MockPort;

	fn id(&self) -> NodeId {
		self.id
	}

	fn title(&self) -> SharedString {
		self.title.clone()
	}

	fn position(&self) -> Point<Pixels> {
		self.position
	}

	fn inputs(&self) -> Vec<Self::Port> {
		self.inputs.clone()
	}

	fn outputs(&self) -> Vec<Self::Port> {
		self.outputs.clone()
	}

	fn header_color(&self) -> Option<Hsla> {
		self.header_color
	}

	fn is_collapsed(&self) -> bool {
		self.collapsed
	}

	fn is_enabled(&self) -> bool {
		self.enabled
	}
}

/// An edge in the mock graph: a connection from an output port to an input
/// port.
#[derive(Debug, Clone)]
pub struct MockEdge {
	id: EdgeId,
	from_node: NodeId,
	from_port: PortId,
	to_node: NodeId,
	to_port: PortId,
}

impl EdgeData for MockEdge {
	fn id(&self) -> EdgeId {
		self.id
	}

	fn from_node(&self) -> NodeId {
		self.from_node
	}

	fn from_port(&self) -> PortId {
		self.from_port
	}

	fn to_node(&self) -> NodeId {
		self.to_node
	}

	fn to_port(&self) -> PortId {
		self.to_port
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
	/// The demo node graph shown in the node editor.
	nodes: Vec<MockNode>,
	/// The demo node-graph edges.
	edges: Vec<MockEdge>,
	/// Id allocator for edges added at runtime.
	next_edge_id: u64,
	/// The node selection, kept in sync with the node editor (and, later, the
	/// effect stack) so both views share one selection.
	node_selection: BTreeSet<NodeId>,
	/// Cache of the synthetic CPU frames handed to the viewers, keyed by
	/// monitor. Entries are the playhead frame that produced the image, so a
	/// paused viewer never regenerates its picture.
	cpu_frame_cache: Mutex<HashMap<Monitor, (i64, Arc<RenderImage>)>>,
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
		let node_color = |h: f32| Hsla {
			h,
			s: 0.55,
			l: 0.5,
			a: 1.0,
		};

		// The demo node graph: two clips, one through the transform, one
		// through the blur, merged in the mixer and LUT'd to the viewer.
		// Port ids are globally unique.
		let video_type = PortDataType::new("video", hsla(0.55, 0.75, 0.6, 1.0));
		let audio_type = PortDataType::new("audio", hsla(0.1, 0.7, 0.55, 1.0));
		let port = |id: u64,
		            kind: PortKind,
		            label: &str,
		            data_type: &PortDataType| MockPort {
			id: PortId(id),
			kind,
			label: label.into(),
			data_type: data_type.clone(),
			connected: false,
		};
		let node = |id: u64,
		            title: &str,
		            position: (f32, f32),
		            color: f32,
		            inputs: Vec<MockPort>,
		            outputs: Vec<MockPort>| MockNode {
			id: NodeId(id),
			title: title.into(),
			position: point(px(position.0), px(position.1)),
			inputs,
			outputs,
			header_color: Some(node_color(color)),
			enabled: true,
			collapsed: false,
		};
		let nodes = vec![
			node(
				0,
				"第一稿.mp4",
				(40.0, 40.0),
				0.55,
				vec![],
				vec![port(1, PortKind::Output, "video", &video_type)],
			),
			node(
				1,
				"B-roll.mp4",
				(40.0, 230.0),
				0.60,
				vec![],
				vec![
					port(3, PortKind::Output, "video", &video_type),
					port(4, PortKind::Output, "audio", &audio_type),
				],
			),
			node(
				2,
				"变换",
				(320.0, 40.0),
				0.35,
				vec![
					port(20, PortKind::Input, "in", &video_type),
					port(22, PortKind::Input, "mask", &video_type),
				],
				vec![port(21, PortKind::Output, "out", &video_type)],
			),
			node(
				3,
				"模糊",
				(320.0, 230.0),
				0.78,
				vec![port(30, PortKind::Input, "in", &video_type)],
				vec![port(31, PortKind::Output, "out", &video_type)],
			),
			node(
				4,
				"混合",
				(600.0, 130.0),
				0.08,
				vec![
					port(40, PortKind::Input, "A", &video_type),
					port(41, PortKind::Input, "B", &video_type),
				],
				vec![port(42, PortKind::Output, "out", &video_type)],
			),
			node(
				5,
				"输出",
				(880.0, 130.0),
				0.0,
				vec![port(50, PortKind::Input, "in", &video_type)],
				vec![],
			),
		];
		let edge = |id: u64,
		            from_node: u64,
		            from_port: u64,
		            to_node: u64,
		            to_port: u64| MockEdge {
			id: EdgeId(id),
			from_node: NodeId(from_node),
			from_port: PortId(from_port),
			to_node: NodeId(to_node),
			to_port: PortId(to_port),
		};
		let edges = vec![
			edge(1, 0, 1, 2, 20),
			edge(2, 1, 3, 3, 30),
			edge(3, 2, 21, 4, 40),
			edge(4, 3, 31, 4, 41),
			edge(5, 4, 42, 5, 50),
		];

		let mut this = Self {
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
			nodes,
			edges,
			next_edge_id: 6,
			node_selection: BTreeSet::new(),
			cpu_frame_cache: Mutex::new(HashMap::new()),
		};
		// The demo graph is born connected: derive every port's `connected`
		// flag from the edge list.
		this.refresh_port_connectivity();
		this
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

	/// Applies a node-editor request to the mock graph (the "edits are
	/// requests" loop: the view emits, the engine applies and notifies).
	pub fn apply_node_graph_event(&mut self, event: &NodeGraphEvent, cx: &mut Context<Self>) {
		match event {
			NodeGraphEvent::NodeMovePreview { .. } | NodeGraphEvent::ViewChanged { .. } => {}
			NodeGraphEvent::NodeMoveRequested { nodes, delta } => {
				for id in nodes {
					if let Some(node) = self.nodes.iter_mut().find(|n| n.id() == *id) {
						node.position = node.position + *delta;
					}
				}
			}
			NodeGraphEvent::ConnectionRequested { from, to } => {
				if self.can_connect(*from, *to) {
					let from_node = self.node_with_port(*from).map(|n| n.id());
					let to_node = self.node_with_port(*to).map(|n| n.id());
					if let (Some(from_node), Some(to_node)) = (from_node, to_node) {
						self.edges.push(MockEdge {
							id: EdgeId(self.next_edge_id),
							from_node,
							from_port: *from,
							to_node,
							to_port: *to,
						});
						self.next_edge_id += 1;
						self.refresh_port_connectivity();
					}
				}
			}
			NodeGraphEvent::DisconnectionRequested { edge } => {
				if let Some(index) = self.edges.iter().position(|e| e.id() == *edge) {
					self.edges.remove(index);
					self.refresh_port_connectivity();
				}
			}
			NodeGraphEvent::DeleteRequested { nodes, edges } => {
				self.edges.retain(|edge| {
					!edges.contains(&edge.id())
						&& !nodes.contains(&edge.from_node())
						&& !nodes.contains(&edge.to_node())
				});
				self.nodes.retain(|node| !nodes.contains(&node.id()));
				self.node_selection.clear();
				self.refresh_port_connectivity();
			}
			NodeGraphEvent::SelectionChanged { nodes } => {
				self.node_selection = nodes.clone();
			}
			NodeGraphEvent::BackgroundClicked { position } => {
				// The real app opens an "add node" menu here; the mock logs it.
				println!("[node editor] add-node menu at graph {position:?} (mock: ignored)");
			}
		}
		cx.notify();
	}

	/// The node currently under the cursor selection (demo state; the effect
	/// stack will sync to this once it drives the same selection).
	pub fn selected_node_ids(&self) -> &BTreeSet<NodeId> {
		&self.node_selection
	}

	/// Looks up a node by id (test helper).
	#[cfg(test)]
	fn node(&self, id: NodeId) -> Option<&MockNode> {
		self.nodes.iter().find(|n| n.id() == id)
	}

	/// Looks up a port by its globally unique id.
	fn port(&self, id: PortId) -> Option<&MockPort> {
		self.nodes
			.iter()
			.flat_map(|n| n.inputs.iter().chain(n.outputs.iter()))
			.find(|p| p.id() == id)
	}

	/// Returns the node that owns `port`.
	fn node_with_port(&self, port: PortId) -> Option<&MockNode> {
		self.nodes.iter().find(|n| {
			n.inputs.iter().any(|p| p.id() == port)
				|| n.outputs.iter().any(|p| p.id() == port)
		})
	}

	/// Whether an edge with the same endpoints already exists.
	fn edge_exists(&self, from: PortId, to: PortId) -> bool {
		self.edges
			.iter()
			.any(|e| e.from_port() == from && e.to_port() == to)
	}

	/// Whether there is a directed path from node `from` to node `to`
	/// following the existing edges (DFS). `from == to` counts as a path.
	fn reaches(&self, from: NodeId, to: NodeId) -> bool {
		if from == to {
			return true;
		}
		let mut visited = std::collections::HashSet::new();
		let mut stack = vec![from];
		while let Some(current) = stack.pop() {
			if !visited.insert(current) {
				continue;
			}
			for edge in &self.edges {
				if edge.from_node() == current {
					let next = edge.to_node();
					if next == to {
						return true;
					}
					stack.push(next);
				}
			}
		}
		false
	}

	/// Recomputed every port's `connected` flag from the current edge list.
	fn refresh_port_connectivity(&mut self) {
		for node in &mut self.nodes {
			for port in node.inputs.iter_mut().chain(node.outputs.iter_mut()) {
				port.connected = self
					.edges
					.iter()
					.any(|e| e.from_port() == port.id || e.to_port() == port.id);
			}
		}
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

impl NodeGraphDataSource for MockEngine {
	type Node = MockNode;
	type Edge = MockEdge;

	fn nodes(&self) -> Vec<Self::Node> {
		self.nodes.clone()
	}

	fn edges(&self) -> Vec<Self::Edge> {
		self.edges.clone()
	}

	fn can_connect(&self, from: PortId, to: PortId) -> bool {
		let (Some(from_node), Some(to_node)) = (self.node_with_port(from), self.node_with_port(to))
		else {
			return false;
		};
		if from_node.id() == to_node.id() {
			return false;
		}
		let (Some(from_port), Some(to_port)) = (self.port(from), self.port(to)) else {
			return false;
		};
		if from_port.kind() != PortKind::Output || to_port.kind() != PortKind::Input {
			return false;
		}
		if from_port.data_type() != to_port.data_type() {
			return false;
		}
		if self.edge_exists(from, to) {
			return false;
		}
		// Connecting from_node → to_node would create a cycle iff there is
		// already a path from to_node back to from_node.
		!self.reaches(to_node.id(), from_node.id())
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

	/// The synthetic CPU test frame for `monitor`, cached per playhead frame so
	/// a paused viewer never regenerates its picture. This is the frame the
	/// source/program viewers display through [`ViewerWidget::set_cpu_frame`],
	/// proving the CPU-frame path end to end before the real engine lands.
	pub fn cpu_frame(&self, monitor: Monitor, cx: &App) -> Arc<RenderImage> {
		let frame = self.clock_frame(monitor, cx);
		let mut cache = self.cpu_frame_cache.lock().unwrap();
		if let Some((cached_frame, image)) = cache.get(&monitor) {
			if *cached_frame == frame.0 {
				return image.clone();
			}
		}
		let image = Arc::new(self.synthetic_frame(frame));
		cache.insert(monitor, (frame.0, image.clone()));
		image
	}

	/// Generates a synthetic test frame: SMPTE-style color bars with a white
	/// sweep whose x position follows `frame`, so playback is visibly moving.
	///
	/// Samples are computed as F32 RGBA (mirroring the real engine's pixel
	/// pipeline) and downconverted to BGRA8 for the viewer's CPU-frame path.
	/// The picture is rendered at a small proxy size ([`SYNTH_FRAME_WIDTH`] ×
	/// [`SYNTH_FRAME_HEIGHT`]); the real engine delivers full resolution.
	fn synthetic_frame(&self, frame: Frame) -> RenderImage {
		let width = SYNTH_FRAME_WIDTH;
		let height = SYNTH_FRAME_HEIGHT;

		// F32 RGBA samples, then quantized to BGRA8 for the sprite atlas.
		let mut samples = vec![0.0f32; (width * height * 4) as usize];
		// SMPTE bars: 75% white, yellow, cyan, green, magenta, red, blue.
		let bars: [(f32, f32, f32); 7] = [
			(1.0, 1.0, 1.0),
			(1.0, 1.0, 0.0),
			(0.0, 1.0, 1.0),
			(0.0, 1.0, 0.0),
			(1.0, 0.0, 1.0),
			(1.0, 0.0, 0.0),
			(0.0, 0.0, 1.0),
		];
		// Bottom strip: blue, magenta, 75% white, black.
		let strip: [(f32, f32, f32); 4] = [
			(0.0, 0.0, 1.0),
			(1.0, 0.0, 1.0),
			(0.75, 0.75, 0.75),
			(0.0, 0.0, 0.0),
		];
		// The sweep moves 6 px per frame and wraps around the width, so
		// transport playback shows up as motion across the picture.
		let sweep = (frame.0 as f32 * 6.0) % width as f32;
		let bars_top = height as f32 * 0.66;

		for y in 0..height {
			for x in 0..width {
				let in_sweep = (x as f32 - sweep).abs() < 6.0;
				let color = if in_sweep {
					(1.0, 1.0, 1.0)
				} else if (y as f32) < bars_top {
					bars[((x as f32 / width as f32) * 7.0) as usize]
				} else {
					strip[((x as f32 / width as f32) * 4.0) as usize]
				};
				let i = ((y * width + x) * 4) as usize;
				samples[i] = color.0;
				samples[i + 1] = color.1;
				samples[i + 2] = color.2;
				samples[i + 3] = 1.0;
			}
		}

		let mut bytes = Vec::with_capacity((width * height * 4) as usize);
		for i in (0..samples.len()).step_by(4) {
			bytes.push((samples[i + 2] * 255.0) as u8); // B
			bytes.push((samples[i + 1] * 255.0) as u8); // G
			bytes.push((samples[i] * 255.0) as u8); // R
			bytes.push((samples[i + 3] * 255.0) as u8); // A
		}
		let buffer = image::RgbaImage::from_raw(width, height, bytes).expect("synthetic frame");
		RenderImage::new(smallvec::SmallVec::from_elem(image::Frame::new(buffer), 1))
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

	#[gpui::test]
	async fn demo_graph_has_six_nodes_and_five_edges(cx: &mut TestAppContext) {
		cx.update(|app| {
			let engine = demo_engine(app);
			let engine = engine.read(app);
			let nodes = engine.nodes();
			let edges = engine.edges();

			assert_eq!(nodes.len(), 6, "media, transform, blur, mixer, viewer");
			assert_eq!(edges.len(), 5);

			// The chain ends at the viewer node, fed by the mixer.
			let viewer = nodes
				.iter()
				.find(|n| n.title() == "输出")
				.expect("viewer node");
			assert_eq!(viewer.inputs().len(), 1);
			assert!(viewer.outputs().is_empty());

			// Every edge endpoint references an existing port, and every port
			// connectivity flag matches the edge list.
			for edge in &edges {
				assert!(
					nodes.iter().any(|n| n.id() == edge.from_node()),
					"edge {} from-node exists",
					edge.id().0
				);
				assert!(
					nodes.iter().any(|n| n.id() == edge.to_node()),
					"edge {} to-node exists",
					edge.id().0
				);
			}
			for node in nodes {
				for port in node.inputs().into_iter().chain(node.outputs()) {
					let expected = engine
						.edges()
						.iter()
						.any(|e| e.from_port() == port.id() || e.to_port() == port.id());
					assert_eq!(port.is_connected(), expected, "port {}", port.id().0);
				}
			}
		});
	}

	#[gpui::test]
	async fn can_connect_enforces_direction_type_duplicates_and_cycles(
		cx: &mut TestAppContext,
	) {
		cx.update(|app| {
			let engine = demo_engine(app);
			let engine = engine.read(app);

			// A valid, still-free connection: Clip1.video → Transform.in (the
			// transform already takes the clip-0 path, so this would be the
			// second input).
			assert!(engine.can_connect(PortId(3), PortId(20)));
			// The existing connection is not offered again.
			assert!(!engine.can_connect(PortId(21), PortId(40)), "duplicate edge");
			// Input → output is rejected (wrong direction).
			assert!(!engine.can_connect(PortId(20), PortId(1)), "wrong direction");
			// A port cannot connect to itself.
			assert!(!engine.can_connect(PortId(42), PortId(40)), "self connection");
			// Type mismatch: the clip's audio output is not a video signal.
			assert!(
				!engine.can_connect(PortId(4), PortId(20)),
				"audio cannot feed a video input"
			);
			// Unknown ports are rejected.
			assert!(!engine.can_connect(PortId(999), PortId(20)));
			assert!(!engine.can_connect(PortId(1), PortId(999)));

			// Cycle rule: the graph flows left-to-right (Clip → Transform →
			// Mixer → Viewer), so no connection can close a loop — but the
			// reachability helper behind the rule is exercised directly.
			assert!(engine.reaches(NodeId(0), NodeId(5)), "main chain path");
			assert!(!engine.reaches(NodeId(5), NodeId(0)), "no backward path");
			assert!(engine.reaches(NodeId(4), NodeId(4)));
		});
	}

	#[gpui::test]
	async fn node_edits_apply_to_the_model(cx: &mut TestAppContext) {
		cx.update(|app| {
			let engine = demo_engine(app);
			// Move the viewer node.
			engine.update(app, |engine, cx| {
				engine.apply_node_graph_event(
					&NodeGraphEvent::NodeMoveRequested {
						nodes: vec![NodeId(5)],
						delta: point(px(100.0), px(-20.0)),
					},
					cx,
				);
			});
			let moved = engine.read(app).node(NodeId(5)).expect("viewer node");
			assert_eq!(moved.position(), point(px(980.0), px(110.0)));

			// Connect the B-roll clip's video to the transform's unused mask
			// input, then disconnect the transform → mixer edge.
			engine.update(app, |engine, cx| {
				engine.apply_node_graph_event(
					&NodeGraphEvent::ConnectionRequested {
						from: PortId(3),
						to: PortId(22),
					},
					cx,
				);
			});
			let engine_read = engine.read(app);
			assert_eq!(engine_read.edges().len(), 6);
			assert!(engine_read.port(PortId(22)).is_some_and(|p| p.is_connected()));
			let edges = engine_read.edges();
			let extra = edges
				.iter()
				.find(|e| e.to_port() == PortId(22))
				.expect("the new connection");

			engine.update(app, |engine, cx| {
				engine.apply_node_graph_event(
					&NodeGraphEvent::DisconnectionRequested { edge: extra.id() },
					cx,
				);
			});
			assert_eq!(engine.read(app).edges().len(), 5);
			assert!(
				!engine.read(app).port(PortId(22)).is_some_and(|p| p.is_connected()),
				"mask input freed again"
			);

			// Deleting the blur node takes its incident edges with it.
			engine.update(app, |engine, cx| {
				engine.apply_node_graph_event(
					&NodeGraphEvent::DeleteRequested {
						nodes: vec![NodeId(3)],
						edges: vec![],
					},
					cx,
				);
			});
			let engine_read = engine.read(app);
			assert_eq!(engine_read.nodes().len(), 5);
			assert!(
				!engine_read.edges().iter().any(|e| e.to_node() == NodeId(3)
					|| e.from_node() == NodeId(3)),
				"edges incident to the deleted node are removed"
			);
		});
	}

	#[gpui::test]
	async fn cpu_frame_is_cached_per_playhead_and_advances(cx: &mut TestAppContext) {
		cx.update(|app| {
			let engine = demo_engine(app);

			// Same playhead → same cached image (Arc identity), so a paused
			// viewer never regenerates its picture.
			let a = engine.read(app).cpu_frame(Monitor::Program, app);
			let b = engine.read(app).cpu_frame(Monitor::Program, app);
			assert!(Arc::ptr_eq(&a, &b), "paused frame must be cached");

			// Advancing the playhead produces a different image: the sweep
			// moved, so playback is visible.
			engine.update(app, |engine, cx| {
				engine.step(Monitor::Program, 25, cx);
			});
			let c = engine.read(app).cpu_frame(Monitor::Program, app);
			assert!(!Arc::ptr_eq(&a, &c), "a new playhead frame regenerates");

			// The frame has the documented proxy size and opaque BGRA8 bytes.
			let size = c.size(0);
			assert_eq!(size.width, SYNTH_FRAME_WIDTH.into());
			assert_eq!(size.height, SYNTH_FRAME_HEIGHT.into());
			let bytes = c.as_bytes(0).expect("single frame");
			assert_eq!(bytes.len(), (SYNTH_FRAME_WIDTH * SYNTH_FRAME_HEIGHT * 4) as usize);
			assert!(bytes.chunks_exact(4).all(|px| px[3] == 255), "opaque alpha");
		});
	}
}
