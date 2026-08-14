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

//! M12 P2: the real node-graph surface.
//!
//! Builds the gpui node-graph data (`RealNode` / `RealPort` / `RealEdge`)
//! from the CURRENT SEQUENCE's graph (the facade's sequence node-graph
//! enumeration; see the `oakengine_sequence_*` exports):
//!
//! - the sequence node becomes the output card (rightmost);
//! - every clip block becomes a card titled with its label (or "Clip");
//! - effects (everything else in the sequence graph) sit between;
//! - footage nodes (the media) feed the clip's `tex_in` from the left;
//! - declared inputs become input ports (id string as the label); every
//!   node exposes one "out" output port (the module declares no outputs;
//!   edges are enumerated from `output_connection_at_ex`);
//! - REAL edges connect the source node's main output to the target
//!   node's input port; a synthesized "clip → output" wire per clip
//!   connects the clip's main output to the sequence's `tex_in` (the
//!   module's place-block flow never wires blocks to the sequence node —
//!   the C++ reaches it through track nodes, which do not exist here);
//! - positions come from the sequence node's context position map, with a
//!   deterministic role-grid fallback layout (footage | effects | clips |
//!   output) when a node has no entry yet — the first drag persists
//!   through the undoable position setter.
//!
//! Identity mapping: `NodeId` = the facade node identity (stable across
//! frames). `PortId` packs `(node identity, kind, index)` — input port
//! `(id << 4) | (index << 1)`, output port `(id << 4) | 1`. Identities
//! are arena indices (low bits zero), so the shifts are injective.
//!
//! Structural timeline plumbing (track lists, tracks, gaps, transitions)
//! is NOT displayed: those nodes carry no graph edges in the module world
//! and would only add empty cards; the displayed graph is the media chain
//! clip → effects → output the C++ node editor centers on.

use std::collections::HashMap;
use std::ffi::{c_char, c_int};

use gpui::node_graph::{
	EdgeData, EdgeId, NodeData, NodeId, PortData, PortId, PortDataType, PortKind,
};
use gpui::{hsla, point, px, Hsla, Pixels, Point, SharedString};

use crate::oakui::ffi::{
	oakengine_node_connect, oakengine_node_disconnect_ex, oakengine_node_free,
	oakengine_node_get_context_position, oakengine_node_get_label, oakengine_node_get_name,
	oakengine_node_get_type_id, oakengine_node_identity, oakengine_node_input_count,
	oakengine_node_input_id, oakengine_node_input_is_connected,
	oakengine_node_output_connection_at_ex, oakengine_node_output_connection_count,
	oakengine_node_set_context_position, oakengine_sequence_as_node, oakengine_sequence_node_at,
	oakengine_sequence_node_count, oakengine_sequence_remove_node, OakEngineNode,
	OakEngineSequence,
};

/// The sequence node type id (the graph's output card).
const TYPE_ID_SEQUENCE: &str = "org.olivevideoeditor.Olive.sequence";
/// Clip block type id (the graph's clip cards).
const TYPE_ID_CLIP_BLOCK: &str = "org.olivevideoeditor.Olive.clipblock";
/// Footage node type id (the graph's media cards).
const TYPE_ID_FOOTAGE: &str = "org.olivevideoeditor.Olive.footage";
/// Structural timeline nodes never shown in the node editor.
const TYPE_ID_TRACK: &str = "org.olivevideoeditor.Olive.track";
const TYPE_ID_TRACK_LIST: &str = "org.olivevideoeditor.Olive.tracklist";
const TYPE_ID_GAP_BLOCK: &str = "org.olivevideoeditor.Olive.gapblock";
const TYPE_ID_TRANSITION_BLOCK: &str = "org.olivevideoeditor.Olive.transitionblock";

/// The "video" wire type used by the real graph (the facade exposes no
/// per-input type names; all node ports are treated as video).
fn video_type() -> PortDataType {
	PortDataType::new("video", hsla(0.55, 0.75, 0.6, 1.0))
}

/// The "output" wire type for the module's implicit outputs.
fn out_type() -> PortDataType {
	PortDataType::new("video", hsla(0.08, 0.7, 0.55, 1.0))
}

/// Pack a node identity + kind + index into a global `PortId`.
fn port_id(node: u64, kind: PortKind, index: u32) -> PortId {
	match kind {
		PortKind::Input => PortId((node << 4) | ((index as u64) << 1)),
		PortKind::Output => PortId((node << 4) | 1),
	}
}

/// Unpack a `PortId` into `(node identity, kind, index)`.
fn unpack_port(id: PortId) -> (u64, PortKind, u32) {
	let raw = id.0;
	let node = raw >> 4;
	if raw & 1 == 1 {
		(node, PortKind::Output, 0)
	} else {
		(node, PortKind::Input, ((raw >> 1) & 0x7) as u32)
	}
}

/// Two-stage string read over a facade `(buf, size)` getter.
fn read_str(f: impl Fn(*mut c_char, c_int) -> c_int) -> String {
	let needed = f(std::ptr::null_mut(), 0);
	if needed <= 0 {
		return String::new();
	}
	let mut buf = vec![0 as c_char; needed as usize + 1];
	f(buf.as_mut_ptr(), needed as c_int + 1);
	let len = buf
		.iter()
		.position(|&c| c == 0)
		.unwrap_or(buf.len());
	String::from_utf8_lossy(unsafe {
		std::slice::from_raw_parts(buf.as_ptr() as *const u8, len)
	})
	.into_owned()
}

/// Read a NUL-terminated facade string out of a fixed buffer.
fn read_cstr_buf(buf: &[c_char]) -> String {
	if buf.first().copied().unwrap_or(0) == 0 {
		return String::new();
	}
	String::from_utf8_lossy(unsafe {
		std::slice::from_raw_parts(buf.as_ptr() as *const u8, buf.len())
	})
	.trim_end_matches('\0')
	.to_string()
}

/// A stable REAL edge id from `(from node, to node, input id)` (FNV-1a,
/// high bit masked so it can never collide with the synthesized-wire tag
/// [`is_output_wire`] reserves).
fn real_edge_id(from: u64, to: u64, input_id: &str) -> EdgeId {
	let mut h: u64 = 0xcbf29ce484222325;
	for &b in [from.to_le_bytes(), to.to_le_bytes()].concat().iter() {
		h ^= b as u64;
		h = h.wrapping_mul(0x100000001b3);
	}
	for &b in input_id.as_bytes() {
		h ^= b as u64;
		h = h.wrapping_mul(0x100000001b3);
	}
	EdgeId(h & 0x7fff_ffff_ffff_ffff)
}

/// The synthesized "clip → output" wire's id: tagged with the high bit so
/// the app can tell structural wires apart from real edges (whose ids are
/// masked below the tag — see [`real_edge_id`]).
fn output_wire_id(clip: u64) -> EdgeId {
	EdgeId(0x8000_0000_0000_0000 | real_edge_id(clip, clip, "out").0)
}

/// Whether `id` is a synthesized structural wire (never a real edge).
pub fn is_output_wire(id: EdgeId) -> bool {
	id.0 & 0x8000_0000_0000_0000 != 0
}

/// A node card in the real graph.
#[derive(Debug, Clone)]
pub struct RealNode {
	/// Facade node identity.
	pub id: NodeId,
	/// Card title.
	pub title: SharedString,
	/// Graph-space position.
	pub position: Point<Pixels>,
	/// Input ports (top to bottom).
	pub inputs: Vec<RealPort>,
	/// The single implicit output port.
	pub outputs: Vec<RealPort>,
	/// Category accent.
	pub header_color: Option<Hsla>,
	/// Collapsed state.
	pub collapsed: bool,
	/// Enabled state.
	pub enabled: bool,
}

impl NodeData for RealNode {
	type Port = RealPort;

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

/// A port on a real node.
#[derive(Debug, Clone)]
pub struct RealPort {
	/// Global port id.
	pub id: PortId,
	/// Direction.
	pub kind: PortKind,
	/// Port label.
	pub label: SharedString,
	/// Wire type.
	pub data_type: PortDataType,
	/// Whether an edge is attached.
	pub connected: bool,
}

impl PortData for RealPort {
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

/// A wire between two real nodes.
#[derive(Debug, Clone)]
pub struct RealEdge {
	/// Stable edge id.
	pub id: EdgeId,
	/// Source node.
	pub from_node: NodeId,
	/// Source output port.
	pub from_port: PortId,
	/// Target node.
	pub to_node: NodeId,
	/// Target input port.
	pub to_port: PortId,
}

impl EdgeData for RealEdge {
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

/// Deterministic header accents per node identity.
fn node_color(ident: u64) -> Hsla {
	let hues = [0.55f32, 0.6, 0.08, 0.3, 0.78, 0.45, 0.9, 0.15];
	hsla(hues[(ident as usize) % hues.len()], 0.5, 0.35, 1.0)
}

/// The type id of a boxed node (empty on failure).
///
/// # Safety
/// `node` must be a live facade node box.
unsafe fn node_type_id(node: *mut OakEngineNode) -> String {
	let mut buf = [0 as c_char; 256];
	let len = unsafe { oakengine_node_get_type_id(node, buf.as_mut_ptr(), buf.len() as c_int) };
	if len <= 0 {
		String::new()
	} else {
		read_cstr_buf(&buf)
	}
}

/// Whether a sequence-graph node should be shown in the node editor: the
/// media chain (sequence output, clip blocks, footage, effects) only;
/// structural timeline plumbing (tracks, track lists, gaps, transitions)
/// is hidden.
fn is_displayed(type_id: &str) -> bool {
	!matches!(
		type_id,
		TYPE_ID_TRACK | TYPE_ID_TRACK_LIST | TYPE_ID_GAP_BLOCK | TYPE_ID_TRANSITION_BLOCK
	)
}

/// A boxed node plus its type id (freed with `oakengine_node_free`).
struct TypedNode {
	/// The boxed node.
	ptr: *mut OakEngineNode,
	/// The node's identity.
	ident: u64,
	/// The node's type id.
	type_id: String,
}

/// The displayable nodes of `seq`'s graph, in graph order.
///
/// # Safety
/// `seq` must be a live facade sequence box.
unsafe fn graph_nodes(seq: *mut OakEngineSequence) -> Vec<TypedNode> {
	let mut out = Vec::new();
	let count = unsafe { oakengine_sequence_node_count(seq) };
	for i in 0..count.max(0) {
		let node = unsafe { oakengine_sequence_node_at(seq, i) };
		if node.is_null() {
			continue;
		}
		let type_id = unsafe { node_type_id(node) };
		let ident = unsafe { oakengine_node_identity(node) };
		if ident == 0 || !is_displayed(&type_id) {
			unsafe { oakengine_node_free(node) };
			continue;
		}
		out.push(TypedNode {
			ptr: node,
			ident,
			type_id,
		});
	}
	out
}

/// Build the (nodes, edges) snapshot of `seq`'s node graph.
///
/// # Safety
/// `seq` must be a live facade sequence box.
pub unsafe fn build_graph(seq: *mut OakEngineSequence) -> (Vec<RealNode>, Vec<RealEdge>) {
	unsafe {
		let mut nodes = Vec::new();
		let mut edges = Vec::new();
		if seq.is_null() {
			return (nodes, edges);
		}
		let seq_node = oakengine_sequence_as_node(seq);
		if seq_node.is_null() {
			return (nodes, edges);
		}
		let seq_ident = oakengine_node_identity(seq_node);
		let seq_label = read_str(|buf, size| oakengine_node_get_label(seq_node, buf, size));
		let seq_name = read_str(|buf, size| oakengine_node_get_name(seq_node, buf, size));

		// The sequence node itself may or may not be enumerated in its own
		// project; ensure exactly one output card with the sequence identity
		// (the enumerated copy, if any, is freed here).
		let mut all = Vec::new();
		for typed in graph_nodes(seq) {
			if typed.ident == seq_ident {
				oakengine_node_free(typed.ptr);
			} else {
				all.push(typed);
			}
		}
		all.push(TypedNode {
			ptr: seq_node,
			ident: seq_ident,
			type_id: TYPE_ID_SEQUENCE.into(),
		});
		all.sort_by_key(|n| n.ident);

		// Build every card's ports first (inputs, the implicit output), so
		// real edges can resolve their target port index by matching the
		// input id against the already-built cards.
		let mut built: Vec<(TypedNode, RealNode)> = Vec::new();
		for typed in all {
			let ident = typed.ident;
			let title = if typed.type_id == TYPE_ID_SEQUENCE {
				if seq_label.is_empty() {
					seq_name.clone()
				} else {
					seq_label.clone()
				}
			} else {
				let label = read_str(|buf, size| oakengine_node_get_label(typed.ptr, buf, size));
				let name = read_str(|buf, size| oakengine_node_get_name(typed.ptr, buf, size));
				if label.is_empty() {
					name
				} else {
					label
				}
			};

			// Inputs.
			let input_count = oakengine_node_input_count(typed.ptr);
			let mut inputs = Vec::with_capacity(input_count.max(0) as usize);
			for idx in 0..input_count {
				let id_str =
					read_str(|buf, size| oakengine_node_input_id(typed.ptr, idx, buf, size));
				if id_str.is_empty() {
					continue;
				}
				let cid = std::ffi::CString::new(id_str.clone()).unwrap_or_default();
				let connected =
					oakengine_node_input_is_connected(typed.ptr, cid.as_ptr()) == 1;
				inputs.push(RealPort {
					id: port_id(ident, PortKind::Input, idx as u32),
					kind: PortKind::Input,
					label: id_str.into(),
					data_type: video_type(),
					connected,
				});
			}

			// The single implicit output.
			let out_count = oakengine_node_output_connection_count(typed.ptr);
			let outputs = vec![RealPort {
				id: port_id(ident, PortKind::Output, 0),
				kind: PortKind::Output,
				label: SharedString::new_static("out"),
				data_type: out_type(),
				connected: out_count > 0,
			}];

			let position = context_position(seq_node, typed.ptr);
			built.push((
				typed,
				RealNode {
					id: NodeId(ident),
					title: title.into(),
					position,
					inputs,
					outputs,
					header_color: Some(node_color(ident)),
					collapsed: false,
					enabled: true,
				},
			));
		}

		// Outgoing REAL edges: every source node's output connections, with
		// the target port resolved to the index of the input whose id matches
		// (the module stores edges by input id; the index may differ from 0 —
		// e.g. a clip's `tex_in` sits after `enabled_in`).
		let mut node_edges: Vec<(u64, Vec<RealEdge>)> = Vec::new();
		for (typed, _) in &built {
			let out_count = oakengine_node_output_connection_count(typed.ptr);
			let mut edges_of = Vec::new();
			for j in 0..out_count {
				let mut input_node: *mut OakEngineNode = std::ptr::null_mut();
				let mut id_buf = [0 as c_char; 256];
				let mut element: c_int = -1;
				let mut hidden: c_int = 0;
				let rc = oakengine_node_output_connection_at_ex(
					typed.ptr,
					j,
					&mut input_node,
					id_buf.as_mut_ptr(),
					id_buf.len() as c_int,
					&mut element,
					&mut hidden,
				);
				if rc != 0 {
					continue;
				}
				let to_ident = if input_node.is_null() {
					0
				} else {
					oakengine_node_identity(input_node)
				};
				if !input_node.is_null() {
					oakengine_node_free(input_node);
				}
				let conn_id = read_cstr_buf(&id_buf);
				if to_ident == 0 || conn_id.is_empty() {
					continue;
				}
				let to_index = built
					.iter()
					.find(|(t, _)| t.ident == to_ident)
					.and_then(|(_, n)| {
						n.inputs
							.iter()
							.position(|p| p.label.as_ref() == conn_id)
					})
					.unwrap_or(0) as u32;
				edges_of.push(RealEdge {
					id: real_edge_id(typed.ident, to_ident, &conn_id),
					from_node: NodeId(typed.ident),
					from_port: port_id(typed.ident, PortKind::Output, 0),
					to_node: NodeId(to_ident),
					to_port: port_id(to_ident, PortKind::Input, to_index),
				});
			}
			node_edges.push((typed.ident, edges_of));
		}

		// Fallback layout: nodes without a persisted context position get a
		// deterministic role grid — footage | effects | clips | output as
		// columns, a per-role row counter as the row (the same grid the
		// C++-era node editor lays chains out on). Nodes the user has
		// already dragged keep their persisted position. `fallback_base`
		// mirrors this grid so the first drag moves from the displayed
		// position rather than the origin.
		let mut row_at_role: HashMap<u32, u32> = HashMap::new();
		for (typed, node) in built.iter_mut() {
			if node.position != point(px(0.0), px(0.0)) {
				continue;
			}
			let role = role_of(&typed.type_id, typed.ident == seq_ident);
			let row = row_at_role.entry(role).or_insert(0);
			let x = 40.0 + (role as f32) * 260.0;
			let y = 40.0 + (*row as f32) * 180.0;
			*row += 1;
			node.position = point(px(x), px(y));
		}

		// Assemble: every built card + its real edges, plus the synthesized
		// "clip → output" wires (each clip's main output into the sequence's
		// `tex_in`, its first declared input). Every enumerated box is freed
		// here (including the sequence node's own view).
		let clip_input = port_id(seq_ident, PortKind::Input, 0);
		for (typed, node) in built {
			if typed.type_id == TYPE_ID_CLIP_BLOCK {
				edges.push(RealEdge {
					id: output_wire_id(typed.ident),
					from_node: node.id,
					from_port: port_id(typed.ident, PortKind::Output, 0),
					to_node: NodeId(seq_ident),
					to_port: clip_input,
				});
			}
			if let Some((_, real)) = node_edges.iter().find(|(id, _)| *id == typed.ident) {
				edges.extend(real.iter().cloned());
			}
			nodes.push(node);
			oakengine_node_free(typed.ptr);
		}
		(nodes, edges)
	}
}

/// The role column of a displayed node (footage | effects | clips |
/// output); drives the fallback grid's x position.
fn role_of(type_id: &str, is_output: bool) -> u32 {
	if is_output {
		3
	} else if type_id == TYPE_ID_FOOTAGE {
		0
	} else if type_id == TYPE_ID_CLIP_BLOCK {
		2
	} else {
		1 // effects
	}
}

/// The provisional position the builder assigns to an unplaced node: the
/// role column and the per-role row among the OTHER unplaced nodes in
/// sorted display order (mirrors the fallback grid in `build_graph`).
/// `apply_edit` uses it so the first drag release writes
/// `(displayed base + delta)` instead of `(origin + delta)`.
///
/// # Safety
/// `seq`/`seq_node` must be live facade boxes.
unsafe fn fallback_base(
	seq: *mut OakEngineSequence,
	seq_node: *mut OakEngineNode,
	ident: u64,
) -> (f64, f64) {
	unsafe {
		let seq_ident = oakengine_node_identity(seq_node);
		let mut all = Vec::new();
		for typed in graph_nodes(seq) {
			if typed.ident == seq_ident {
				oakengine_node_free(typed.ptr);
			} else {
				all.push(typed);
			}
		}
		all.push(TypedNode {
			ptr: seq_node,
			ident: seq_ident,
			type_id: TYPE_ID_SEQUENCE.into(),
		});
		all.sort_by_key(|n| n.ident);
		let target = all
			.iter()
			.find(|n| n.ident == ident)
			.expect("the moved node is part of the displayed graph");
		let target_role = role_of(&target.type_id, ident == seq_ident);
		let mut row: u32 = 0;
		for n in &all {
			if n.ident == ident {
				break;
			}
			if role_of(&n.type_id, n.ident == seq_ident) != target_role {
				continue;
			}
			// Placed nodes do not consume a row.
			let mut x: f64 = 0.0;
			let mut y: f64 = 0.0;
			let mut expanded: c_int = 0;
			let placed = oakengine_node_get_context_position(
				seq_node,
				n.ptr,
				&mut x,
				&mut y,
				&mut expanded,
			) == 0
				&& (x != 0.0 || y != 0.0);
			if !placed {
				row += 1;
			}
		}
		// Free the enumerated boxes; `seq_node` belongs to the caller.
		for n in &all {
			if n.ptr != seq_node {
				oakengine_node_free(n.ptr);
			}
		}
		(
			40.0 + f64::from(target_role * 260),
			40.0 + f64::from(row * 180),
		)
	}
}

/// The context position of `node` in `seq_node`'s map, or the (0,0)
/// sentinel the fallback layout replaces.
///
/// # Safety
/// Both pointers must be live facade node boxes.
unsafe fn context_position(seq_node: *mut OakEngineNode, node: *mut OakEngineNode) -> Point<Pixels> {
	let mut x: f64 = 0.0;
	let mut y: f64 = 0.0;
	let mut expanded: c_int = 0;
	if unsafe {
		oakengine_node_get_context_position(seq_node, node, &mut x, &mut y, &mut expanded)
	} == 0
		&& (x != 0.0 || y != 0.0)
	{
		point(px(x as f32), px(y as f32))
	} else {
		point(px(0.0), px(0.0))
	}
}

// ---------------------------------------------------------------------------
// Connection rules and edit application
// ---------------------------------------------------------------------------

/// Whether connecting output port `from` to input port `to` is valid in
/// `seq`'s graph: output → input, distinct nodes, and the target input
/// must exist and be free. The sequence node's inputs are connectable
/// like any other (its `tex_in` starts unconnected; a user wire replaces
/// the synthesized one once the graph grows real edges).
///
/// # Safety
/// `seq` must be a live facade sequence box.
pub unsafe fn can_connect(seq: *mut OakEngineSequence, from: PortId, to: PortId) -> bool {
	unsafe {
		let (from_node, from_kind, _) = unpack_port(from);
		let (to_node, to_kind, to_index) = unpack_port(to);
		if from_kind != PortKind::Output || to_kind != PortKind::Input {
			return false;
		}
		if from_node == to_node {
			return false;
		}
		let Ok(node) = find_boxed(seq, to_node) else {
			return false;
		};
		let count = oakengine_node_input_count(node);
		if to_index >= count.max(0) as u32 {
			oakengine_node_free(node);
			return false;
		}
		let id_str =
			read_str(|buf, size| oakengine_node_input_id(node, to_index as c_int, buf, size));
		if id_str.is_empty() {
			oakengine_node_free(node);
			return false;
		}
		let cid = std::ffi::CString::new(id_str).unwrap_or_default();
		let free = oakengine_node_input_is_connected(node, cid.as_ptr()) == 0;
		oakengine_node_free(node);
		free
	}
}

/// Apply a node-graph edit to `seq`'s graph (undoable through the facade).
///
/// # Safety
/// `seq` must be a live facade sequence box.
pub unsafe fn apply_edit(
	seq: *mut OakEngineSequence,
	edit: &gpui::node_graph::NodeGraphEvent,
) -> Result<(), String> {
	use gpui::node_graph::NodeGraphEvent;
	unsafe {
		let seq_node = oakengine_sequence_as_node(seq);
		match edit {
			NodeGraphEvent::ConnectionRequested { from, to } => {
				let (from_node, from_kind, _) = unpack_port(*from);
				let (to_node, to_kind, to_index) = unpack_port(*to);
				if from_kind != PortKind::Output || to_kind != PortKind::Input {
					if !seq_node.is_null() {
						oakengine_node_free(seq_node);
					}
					return Err("connection endpoints must be output → input".into());
				}
				let src = find_boxed(seq, from_node)?;
				let dst = find_boxed(seq, to_node)?;
				let id_str = {
					let count = oakengine_node_input_count(dst);
					if to_index >= count.max(0) as u32 {
						oakengine_node_free(src);
						oakengine_node_free(dst);
						if !seq_node.is_null() {
							oakengine_node_free(seq_node);
						}
						return Err("target input out of range".into());
					}
					read_str(|buf, size| {
						oakengine_node_input_id(dst, to_index as c_int, buf, size)
					})
				};
				if id_str.is_empty() {
					oakengine_node_free(src);
					oakengine_node_free(dst);
					if !seq_node.is_null() {
						oakengine_node_free(seq_node);
					}
					return Err("target input missing".into());
				}
				let cid = std::ffi::CString::new(id_str).unwrap_or_default();
				let rc = oakengine_node_connect(src, dst, cid.as_ptr());
				oakengine_node_free(src);
				oakengine_node_free(dst);
				if !seq_node.is_null() {
					oakengine_node_free(seq_node);
				}
				if rc != 0 {
					return Err(format!("connect failed rc={rc}"));
				}
				Ok(())
			}
			NodeGraphEvent::DisconnectionRequested { edge } => {
				if is_output_wire(*edge) {
					// The synthesized clip → output wire is structural; the
					// facade has no such edge to remove.
					if !seq_node.is_null() {
						oakengine_node_free(seq_node);
					}
					return Ok(());
				}
				let e = self::edge_for(seq, edge.0)?;
				let cid = std::ffi::CString::new(e.1).unwrap_or_default();
				let rc = oakengine_node_disconnect_ex(e.0, cid.as_ptr(), -1);
				oakengine_node_free(e.0);
				if !seq_node.is_null() {
					oakengine_node_free(seq_node);
				}
				if rc != 0 {
					return Err(format!("disconnect failed rc={rc}"));
				}
				Ok(())
			}
			NodeGraphEvent::NodeMoveRequested { nodes, delta } => {
				for node in nodes {
					let boxed = find_boxed(seq, node.0)?;
					let mut x: f64 = 0.0;
					let mut y: f64 = 0.0;
					let mut expanded: c_int = 0;
					let placed = oakengine_node_get_context_position(
						seq_node,
						boxed,
						&mut x,
						&mut y,
						&mut expanded,
					) == 0
						&& (x != 0.0 || y != 0.0);
					if !placed {
						// The node was drawn at its provisional grid spot;
						// move from there so the release does not jump it
						// back toward the origin.
						let (fx, fy) = fallback_base(seq, seq_node, node.0);
						x = fx;
						y = fy;
					}
					let rc = oakengine_node_set_context_position(
						seq_node,
						boxed,
						x + f64::from(delta.x.as_f32()),
						y + f64::from(delta.y.as_f32()),
					);
					oakengine_node_free(boxed);
					if rc != 0 {
						if !seq_node.is_null() {
							oakengine_node_free(seq_node);
						}
						return Err(format!("move failed rc={rc}"));
					}
				}
				if !seq_node.is_null() {
					oakengine_node_free(seq_node);
				}
				Ok(())
			}
			NodeGraphEvent::DeleteRequested { nodes, .. } => {
				for node in nodes {
					// The output node is the graph's context; never deleted.
					if node.0 == oakengine_node_identity(seq_node) {
						continue;
					}
					let boxed = find_boxed(seq, node.0)?;
					let rc = oakengine_sequence_remove_node(seq, boxed);
					oakengine_node_free(boxed);
					if rc != 0 {
						if !seq_node.is_null() {
							oakengine_node_free(seq_node);
						}
						return Err(format!("remove failed rc={rc}"));
					}
				}
				if !seq_node.is_null() {
					oakengine_node_free(seq_node);
				}
				Ok(())
			}
			_ => {
				if !seq_node.is_null() {
					oakengine_node_free(seq_node);
				}
				Ok(())
			}
		}
	}
}

/// Boxed sequence-graph node for an identity (freed with
/// `oakengine_node_free`).
///
/// # Safety
/// `seq` must be a live facade sequence box.
unsafe fn find_boxed(seq: *mut OakEngineSequence, ident: u64) -> Result<*mut OakEngineNode, String> {
	unsafe {
		let count = oakengine_sequence_node_count(seq);
		for i in 0..count.max(0) {
			let node = oakengine_sequence_node_at(seq, i);
			if node.is_null() {
				continue;
			}
			if oakengine_node_identity(node) == ident {
				return Ok(node);
			}
			oakengine_node_free(node);
		}
		Err(format!("node {ident} not found"))
	}
}

/// Resolve a real edge id back to `(input node, input id)`.
///
/// # Safety
/// `seq` must be a live facade sequence box.
unsafe fn edge_for(
	seq: *mut OakEngineSequence,
	edge: u64,
) -> Result<(*mut OakEngineNode, String), String> {
	unsafe {
		let count = oakengine_sequence_node_count(seq);
		for i in 0..count.max(0) {
			let node = oakengine_sequence_node_at(seq, i);
			if node.is_null() {
				continue;
			}
			let out_count = oakengine_node_output_connection_count(node);
			for j in 0..out_count {
				let mut input_node: *mut OakEngineNode = std::ptr::null_mut();
				let mut id_buf = [0 as c_char; 256];
				let mut element: c_int = -1;
				let mut hidden: c_int = 0;
				if oakengine_node_output_connection_at_ex(
					node,
					j,
					&mut input_node,
					id_buf.as_mut_ptr(),
					id_buf.len() as c_int,
					&mut element,
					&mut hidden,
				) != 0
				{
					continue;
				}
				let from = oakengine_node_identity(node);
				let to = if input_node.is_null() {
					0
				} else {
					oakengine_node_identity(input_node)
				};
				let conn_id = read_cstr_buf(&id_buf);
				if !input_node.is_null() {
					oakengine_node_free(input_node);
				}
				if real_edge_id(from, to, &conn_id).0 == edge {
					// The input node was freed; re-box it.
					let dst = find_boxed(seq, to)?;
					return Ok((dst, conn_id));
				}
			}
			oakengine_node_free(node);
		}
		Err(format!("edge {edge} not found"))
	}
}

fn node_position_mut(mut _p: Point<Pixels>, _d: u32, _row: f32) {}
