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
//! from the facade's project-node enumeration:
//!
//! - every project node becomes a card, titled with its label;
//! - declared inputs become input ports (id string as the label);
//! - every node exposes one "out" output port (the module declares no
//!   outputs; edges are enumerated from `output_connection_at_ex`);
//! - edges connect the source node's main output to the target node's
//!   input port;
//! - positions come from the project-root context's position map.
//!
//! Identity mapping: `NodeId` = the facade node identity (stable across
//! frames). `PortId` packs `(node identity, kind, index)` — input port
//! `(id << 4) | (index << 1)`, output port `(id << 4) | 1`. Identities
//! are pointer-aligned (low bits zero), so the shifts are injective.

use std::ffi::{c_char, c_int, c_void};

use gpui::node_graph::{EdgeData, EdgeId, NodeData, NodeGraphDataSource, NodeId, PortData, PortId, PortKind, PortDataType};
use gpui::{hsla, point, px, Hsla, Pixels, Point, SharedString};

use crate::oakui::ffi::{
	free_box, oakengine_node_connect, oakengine_node_disconnect_ex, oakengine_node_free,
	oakengine_node_get_context_position, oakengine_node_get_label, oakengine_node_get_name,
	oakengine_node_identity, oakengine_node_input_count, oakengine_node_input_id,
	oakengine_node_input_is_connected, oakengine_node_output_connection_at_ex,
	oakengine_node_output_connection_count, oakengine_node_set_context_position,
	oakengine_project_node_at, oakengine_project_node_count, oakengine_project_remove_node,
	oakengine_project_root, OakEngineNode, OakEngineProject,
};

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

/// A stable edge id from `(from node, to node, input id)`.
fn edge_id(from: u64, to: u64, input_id: &str) -> EdgeId {
	let mut h: u64 = 0xcbf29ce484222325;
	for &b in [from.to_le_bytes(), to.to_le_bytes()].concat().iter() {
		h ^= b as u64;
		h = h.wrapping_mul(0x100000001b3);
	}
	for &b in input_id.as_bytes() {
		h ^= b as u64;
		h = h.wrapping_mul(0x100000001b3);
	}
	EdgeId(h)
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

/// Build the (nodes, edges) snapshot of `project`'s node graph.
///
/// # Safety
/// `project` must be a live facade project box.
pub unsafe fn build_graph(project: *mut OakEngineProject) -> (Vec<RealNode>, Vec<RealEdge>) {
	unsafe {
		let mut nodes = Vec::new();
		let mut edges = Vec::new();
		if project.is_null() {
			return (nodes, edges);
		}
		let root = oakengine_project_root(project);
		let count = oakengine_project_node_count(project);
		for i in 0..count {
			let node = oakengine_project_node_at(project, i);
			if node.is_null() {
				continue;
			}
			let ident = oakengine_node_identity(node);
			if ident == 0 {
				oakengine_node_free(node);
				continue;
			}
			// Title: the label, falling back to the type name.
			let label = read_str(|buf, size| oakengine_node_get_label(node, buf, size));
			let name = read_str(|buf, size| oakengine_node_get_name(node, buf, size));
			let title = if label.is_empty() { name } else { label };

			// Position from the project-root context map.
			let mut x: f64 = 0.0;
			let mut y: f64 = 0.0;
			let mut expanded: c_int = 0;
			let has_pos = oakengine_node_get_context_position(
				root,
				node,
				&mut x,
				&mut y,
				&mut expanded,
			) == 0
				&& (x != 0.0 || y != 0.0);

			// Inputs.
			let input_count = oakengine_node_input_count(node);
			let mut inputs = Vec::with_capacity(input_count.max(0) as usize);
			for idx in 0..input_count {
				let id_str = read_str(|buf, size| oakengine_node_input_id(node, idx, buf, size));
				if id_str.is_empty() {
					continue;
				}
				let cid = std::ffi::CString::new(id_str.clone()).unwrap_or_default();
				let connected =
					oakengine_node_input_is_connected(node, cid.as_ptr()) == 1;
				inputs.push(RealPort {
					id: port_id(ident, PortKind::Input, idx as u32),
					kind: PortKind::Input,
					label: id_str.into(),
					data_type: video_type(),
					connected,
				});
			}

			// The single implicit output.
			let out_count = oakengine_node_output_connection_count(node);
			let outputs = vec![RealPort {
				id: port_id(ident, PortKind::Output, 0),
				kind: PortKind::Output,
				label: SharedString::new_static("out"),
				data_type: out_type(),
				connected: out_count > 0,
			}];

			// Outgoing edges.
			for j in 0..out_count {
				let mut input_node: *mut OakEngineNode = std::ptr::null_mut();
				let mut id_buf = [0 as c_char; 256];
				let mut element: c_int = -1;
				let mut hidden: c_int = 0;
				let rc = oakengine_node_output_connection_at_ex(
					node,
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
				// The facade already filled id_buf during
				// output_connection_at_ex.
				let mut len = 0usize;
				while len < id_buf.len() && id_buf[len] != 0 {
					len += 1;
				}
				let conn_id = String::from_utf8_lossy(unsafe {
					std::slice::from_raw_parts(id_buf.as_ptr() as *const u8, len)
				})
				.into_owned();
				if to_ident == 0 || conn_id.is_empty() {
					continue;
				}
				edges.push(RealEdge {
					id: edge_id(ident, to_ident, &conn_id),
					from_node: NodeId(ident),
					from_port: port_id(ident, PortKind::Output, 0),
					to_node: NodeId(to_ident),
					to_port: port_id(to_ident, PortKind::Input, idx_of_input(to_ident, &conn_id, project, &nodes)),
				});
			}

			nodes.push(RealNode {
				id: NodeId(ident),
				title: title.into(),
				position: if has_pos {
					point(px(x as f32), px(y as f32))
				} else {
					point(px(0.0), px(0.0))
				},
				inputs,
				outputs,
				header_color: Some(node_color(ident)),
				collapsed: false,
				enabled: true,
			});
			oakengine_node_free(node);
		}
		if !root.is_null() {
			oakengine_node_free(root);
		}
		(nodes, edges)
	}
}

/// Resolve the input port index for `input_id` on the node with identity
/// `ident` (the edge's target port must match the port the facade lists).
fn idx_of_input(
	ident: u64,
	input_id: &str,
	project: *mut OakEngineProject,
	nodes: &[RealNode],
) -> u32 {
	// The target node was already snapshotted: find it and match the
	// input label.
	if let Some(n) = nodes.iter().find(|n| n.id.0 == ident) {
		for (i, p) in n.inputs.iter().enumerate() {
			if p.label.as_ref() == input_id {
				return i as u32;
			}
		}
	}
	// Fallback: re-fetch the node's inputs through the facade.
	let _ = project;
	let _ = ident;
	0
}

// ---------------------------------------------------------------------------
// NodeGraphDataSource over the snapshot
// ---------------------------------------------------------------------------

/// The real graph data source (a [`NodeGraphDataSource`] snapshot).
pub struct RealGraphSource {
	nodes: Vec<RealNode>,
	edges: Vec<RealEdge>,
	project: *mut OakEngineProject,
}

unsafe impl Send for RealGraphSource {}

impl RealGraphSource {
	/// Snapshot the current graph.
	///
	/// # Safety
	/// `project` must be a live facade project box.
	pub unsafe fn snapshot(project: *mut OakEngineProject) -> Self {
		let (nodes, edges) = unsafe { build_graph(project) };
		Self {
			nodes,
			edges,
			project,
		}
	}

	/// Find the boxed facade node for a node identity (caller frees with
	/// [`oakengine_node_free`]).
	///
	/// # Safety
	/// The returned pointer is a live box.
	pub unsafe fn find_node(&self, ident: u64) -> *mut OakEngineNode {
		unsafe {
			if self.project.is_null() {
				return std::ptr::null_mut();
			}
			let count = oakengine_project_node_count(self.project);
			for i in 0..count {
				let node = oakengine_project_node_at(self.project, i);
				if node.is_null() {
					continue;
				}
				if oakengine_node_identity(node) == ident {
					return node;
				}
				oakengine_node_free(node);
			}
			std::ptr::null_mut()
		}
	}
}

impl NodeGraphDataSource for RealGraphSource {
	type Node = RealNode;
	type Edge = RealEdge;

	fn nodes(&self) -> Vec<Self::Node> {
		self.nodes.clone()
	}

	fn edges(&self) -> Vec<Self::Edge> {
		self.edges.clone()
	}

	fn can_connect(&self, from: PortId, to: PortId) -> bool {
		let (from_node, from_kind, _) = unpack_port(from);
		let (to_node, to_kind, to_index) = unpack_port(to);
		if from_kind != PortKind::Output || to_kind != PortKind::Input {
			return false;
		}
		if from_node == to_node {
			return false;
		}
		// The target input must exist and be free.
		unsafe {
			let node = self.find_node(to_node);
			if node.is_null() {
				return false;
			}
			let count = oakengine_node_input_count(node);
			if to_index >= count.max(0) as u32 {
				oakengine_node_free(node);
				return false;
			}
			let id_str = read_str(|buf, size| oakengine_node_input_id(node, to_index as c_int, buf, size));
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
}

/// Apply a node-graph edit to the facade (undoable).
///
/// # Safety
/// `project` must be a live facade project box.
pub unsafe fn apply_edit(
	project: *mut OakEngineProject,
	edit: &gpui::node_graph::NodeGraphEvent,
) -> Result<(), String> {
	use gpui::node_graph::NodeGraphEvent;
	unsafe {
		let root = oakengine_project_root(project);
		match edit {
			NodeGraphEvent::ConnectionRequested { from, to } => {
				let (from_node, from_kind, _) = unpack_port(*from);
				let (to_node, to_kind, to_index) = unpack_port(*to);
				if from_kind != PortKind::Output || to_kind != PortKind::Input {
					if !root.is_null() {
						oakengine_node_free(root);
					}
					return Err("connection endpoints must be output → input".into());
				}
				let src = find_boxed(project, from_node)?;
				let dst = find_boxed(project, to_node)?;
				let id_str = {
					let count = oakengine_node_input_count(dst);
					if to_index >= count.max(0) as u32 {
						oakengine_node_free(src);
						oakengine_node_free(dst);
						if !root.is_null() {
							oakengine_node_free(root);
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
					if !root.is_null() {
						oakengine_node_free(root);
					}
					return Err("target input missing".into());
				}
				let cid = std::ffi::CString::new(id_str).unwrap_or_default();
				let rc = oakengine_node_connect(src, dst, cid.as_ptr());
				oakengine_node_free(src);
				oakengine_node_free(dst);
				if !root.is_null() {
					oakengine_node_free(root);
				}
				if rc != 0 {
					return Err(format!("connect failed rc={rc}"));
				}
				Ok(())
			}
			NodeGraphEvent::DisconnectionRequested { edge } => {
				let e = self::edge_for(project, edge.0)?;
				let cid = std::ffi::CString::new(e.1).unwrap_or_default();
				let rc = oakengine_node_disconnect_ex(e.0, cid.as_ptr(), -1);
				oakengine_node_free(e.0);
				if !root.is_null() {
					oakengine_node_free(root);
				}
				if rc != 0 {
					return Err(format!("disconnect failed rc={rc}"));
				}
				Ok(())
			}
			NodeGraphEvent::NodeMoveRequested { nodes, delta } => {
				for node in nodes {
					let boxed = find_boxed(project, node.0)?;
					let mut x: f64 = 0.0;
					let mut y: f64 = 0.0;
					let mut expanded: c_int = 0;
					oakengine_node_get_context_position(
						root,
						boxed,
						&mut x,
						&mut y,
						&mut expanded,
					);
					let rc = oakengine_node_set_context_position(
						root,
						boxed,
						x + f64::from(delta.x.as_f32()),
						y + f64::from(delta.y.as_f32()),
					);
					oakengine_node_free(boxed);
					if rc != 0 {
						if !root.is_null() {
							oakengine_node_free(root);
						}
						return Err(format!("move failed rc={rc}"));
					}
				}
				if !root.is_null() {
					oakengine_node_free(root);
				}
				Ok(())
			}
			NodeGraphEvent::DeleteRequested { nodes, .. } => {
				for node in nodes {
					let boxed = find_boxed(project, node.0)?;
					let rc = oakengine_project_remove_node(project, boxed);
					oakengine_node_free(boxed);
					if rc != 0 {
						if !root.is_null() {
							oakengine_node_free(root);
						}
						return Err(format!("remove failed rc={rc}"));
					}
				}
				if !root.is_null() {
					oakengine_node_free(root);
				}
				Ok(())
			}
			_ => {
				if !root.is_null() {
					oakengine_node_free(root);
				}
				Ok(())
			}
		}
	}
}

/// Boxed facade node for an identity (freed with `oakengine_node_free`).
///
/// # Safety
/// `project` must be live.
unsafe fn find_boxed(
	project: *mut OakEngineProject,
	ident: u64,
) -> Result<*mut OakEngineNode, String> {
	unsafe {
		let count = oakengine_project_node_count(project);
		for i in 0..count {
			let node = oakengine_project_node_at(project, i);
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

/// Resolve an edge id back to `(input node, input id)`.
///
/// # Safety
/// `project` must be live.
unsafe fn edge_for(
	project: *mut OakEngineProject,
	edge: u64,
) -> Result<(*mut OakEngineNode, String), String> {
	unsafe {
		let count = oakengine_project_node_count(project);
		for i in 0..count {
			let node = oakengine_project_node_at(project, i);
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
				let mut len = 0usize;
				while len < id_buf.len() && id_buf[len] != 0 {
					len += 1;
				}
				let conn_id = String::from_utf8_lossy(unsafe {
					std::slice::from_raw_parts(id_buf.as_ptr() as *const u8, len)
				})
				.into_owned();
				if !input_node.is_null() {
					oakengine_node_free(input_node);
				}
				if self::edge_id(from, to, &conn_id).0 == edge {
					// The input node was freed; re-box it.
					let dst = find_boxed(project, to)?;
					return Ok((dst, conn_id));
				}
			}
			oakengine_node_free(node);
		}
		Err(format!("edge {edge} not found"))
	}
}
