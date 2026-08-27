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

//! The graph arena: node storage, edges, traversal order.
//!
//! Replaces the C++ pointer web (`Node::parent_`, output_connections_)
//! with a slab arena + edge set. All structural mutation goes through
//! `&mut Graph` methods; evaluation takes `&Graph`.

use std::collections::{BTreeSet, HashMap, HashSet};

use crate::id::NodeId;
use crate::node::{NodeBehavior, NodeCore};

/// One arena slot.
pub struct NodeEntry {
	/// Shared data.
	pub core: NodeCore,
	/// Polymorphic behavior.
	pub behavior: Box<dyn NodeBehavior>,
	/// Generation for stale-id detection.
	pub generation: u32,
	/// True when the slot is free.
	pub vacant: bool,
}

/// A directed edge: `from` node's output feeds `to` node's `input`
/// (element for array inputs).
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord)]
pub struct Edge {
	/// Source node.
	pub from: NodeId,
	/// Destination node.
	pub to: NodeId,
	/// Destination input id hash + element (string stored arena-side).
	pub input_key: u64,
	/// Array element (-1 = scalar input).
	pub element: i32,
}

/// The node graph. Nodes are owned here, exclusively.
pub struct Graph {
	entries: Vec<NodeEntry>,
	free_list: Vec<u32>,
	edges: BTreeSet<Edge>,
	/// Arena-side storage for edge input ids (key = hash into this map).
	input_names: HashMap<u64, String>,
}

impl Graph {
	/// Empty graph.
	pub fn new() -> Self {
		Graph {
			entries: Vec::new(),
			free_list: Vec::new(),
			edges: BTreeSet::new(),
			input_names: HashMap::new(),
		}
	}

	/// Insert a node; returns its id.
	pub fn add_node(&mut self, core: NodeCore, behavior: Box<dyn NodeBehavior>) -> NodeId {
		let (index, generation) = match self.free_list.pop() {
			Some(i) => {
				// Reuse the slot with a fresh generation (the slot's
				// previous occupant is gone; its generation counter
				// advances so stale ids fail).
				let gen = self.entries[i as usize].generation.wrapping_add(1);
				self.entries[i as usize] = NodeEntry {
					core,
					behavior,
					generation: gen,
					vacant: false,
				};
				(i, gen)
			}
			None => {
				let index = self.entries.len() as u32;
				let entry = NodeEntry {
					core,
					behavior,
					generation: 0,
					vacant: false,
				};
				self.entries.push(entry);
				(index, 0)
			}
		};
		NodeId::new(index, generation)
	}

	/// Insert a previously detached [`NodeEntry`] (from
	/// [`Graph::take_node`]) back into this graph, preserving its identity
	/// when the original slot is free here. `id` is the node's identity
	/// before the detach; it is reused when its slot is vacant. Used by
	/// the project node-transfer paths so a node's stable identity
	/// survives a move.
	pub fn add_entry(&mut self, entry: NodeEntry, id: NodeId) -> NodeId {
		let index = id.index();
		if (index as usize) < self.entries.len() && self.entries[index as usize].vacant {
			// Original slot free: reuse (index, generation) unchanged. The
			// slot was pushed to the free list by `take_node` — reclaim it,
			// or `node_count` keeps undercounting and, worse, the next
			// `add_node` hands the same slot out again and silently
			// clobbers the restored node (the undo/redo divergence).
			self.free_list.retain(|&i| i != index);
			let generation = entry.generation;
			self.entries[index as usize] = entry;
			return NodeId::new(index, generation);
		}
		// Slot occupied (or out of range): allocate fresh.
		match self.free_list.pop() {
			Some(i) => {
				let gen = self.entries[i as usize].generation.wrapping_add(1);
				let new_id = NodeId::new(i, gen);
				self.entries[i as usize] = NodeEntry {
					core: entry.core,
					behavior: entry.behavior,
					generation: gen,
					vacant: false,
				};
				new_id
			}
			None => {
				let i = self.entries.len() as u32;
				self.entries.push(NodeEntry {
					core: entry.core,
					behavior: entry.behavior,
					generation: 0,
					vacant: false,
				});
				NodeId::new(i, 0)
			}
		}
	}

	/// Detach a node from the arena, returning its full entry (core +
	/// behavior + generation) and removing all its edges. `None` for a
	/// stale id. Unlike [`Graph::remove_node`] the entry is preserved for
	/// re-insertion elsewhere (project detach/attach moves).
	pub fn take_node(&mut self, id: NodeId) -> Option<NodeEntry> {
		if !self.is_valid(id) {
			return None;
		}
		let idx = id.index() as usize;
		let vacant_entry = NodeEntry {
			core: NodeCore::empty(),
			behavior: Box::new(crate::nodes::EmptyBehavior),
			generation: self.entries[idx].generation,
			vacant: true,
		};
		// Move the real entry out.
		let taken = std::mem::replace(&mut self.entries[idx], vacant_entry);
		self.free_list.push(id.index());
		self.drop_edges_touching(id);
		// Symmetric link cleanup: partners drop their reference to the
		// detached node, so no dangling link outlives it. The taken entry
		// keeps its own links, but `add_entry` (the undo path) does NOT
		// re-establish the partners' back-references — a delete-then-undo
		// leaves the link one-way (known trade-off: a restorable asymmetric
		// link beats a stale reference; `are_linked` reads both ways and
		// `link()` repairs the missing direction).
		for partner in taken.core.links.clone() {
			if let Some(entry) = self.get_mut(partner) {
				entry.core.links.retain(|&l| l != id);
			}
		}
		Some(taken)
	}

	/// Remove a node and all its edges (C++: ~Node + set_parent(null)
	/// + disconnect_all side effects — see `// CPP-PARITY: node.cpp`).
	pub fn remove_node(&mut self, id: NodeId) -> Option<Box<dyn NodeBehavior>> {
		let entry = self.take_node(id)?;
		Some(entry.behavior)
	}

	/// Validated access; `None` for stale ids.
	pub fn get(&self, id: NodeId) -> Option<&NodeEntry> {
		if !self.is_valid(id) {
			return None;
		}
		Some(&self.entries[id.index() as usize])
	}

	/// Mutable access; `None` for stale ids.
	pub fn get_mut(&mut self, id: NodeId) -> Option<&mut NodeEntry> {
		if !self.is_valid(id) {
			return None;
		}
		Some(&mut self.entries[id.index() as usize])
	}

	/// True when `id` names a live slot.
	pub fn is_valid(&self, id: NodeId) -> bool {
		id.valid()
			&& (id.index() as usize) < self.entries.len()
			&& !self.entries[id.index() as usize].vacant
			&& self.entries[id.index() as usize].generation == id.generation()
	}

	/// Number of live nodes (used by the project node_count family).
	pub fn node_count(&self) -> usize {
		self.entries.len() - self.free_list.len()
	}

	/// Live node ids in slot order (stable within a session).
	pub fn node_ids(&self) -> Vec<NodeId> {
		let mut ids = Vec::with_capacity(self.node_count());
		for (i, e) in self.entries.iter().enumerate() {
			if !e.vacant {
				ids.push(NodeId::new(i as u32, e.generation));
			}
		}
		ids
	}

	/// Connect `from`'s output to `to.input[element]`
	/// (C++ `Node::connect_edge` incl. cycle rejection).
	///
	/// Errors: `NotFound` for a stale endpoint or unknown input id;
	/// `Invalid` when the input is not connectable; `State` when the
	/// input is already connected, `from == to`, or the edge would create
	/// a cycle (the Rust design rejects cycles at connect time so the
	/// arena never contains them — `// CPP-PARITY: node.cpp:210`).
	pub fn connect(
		&mut self,
		from: NodeId,
		to: NodeId,
		input: &str,
		element: i32,
	) -> crate::error::Result<()> {
		use crate::error::Error;
		if !self.is_valid(from) || !self.is_valid(to) {
			return Err(Error::NotFound);
		}

		// Input must exist and be connectable (checked before the
		// state-level rejections, matching the C++ c_api precedence:
		// NOT_FOUND > INVALID(not connectable) > STATE).
		let input_flags = {
			let entry = self.get(to).expect("validated above");
			let input = entry.core.get_input(input).ok_or(Error::NotFound)?;
			input.flags
		};
		if input_flags & crate::input::flags::NOT_CONNECTABLE != 0 {
			return Err(Error::Invalid);
		}
		// Array inputs address elements >= 0; scalar inputs only -1.
		let is_array = input_flags & crate::input::flags::ARRAY != 0;
		if !is_array && element != -1 {
			return Err(Error::Invalid);
		}

		if from == to {
			// Self-connection is a trivial cycle.
			return Err(Error::State);
		}

		// Already connected on this input.
		if self.connected_output(to, input, element).is_some() {
			return Err(Error::State);
		}

		// Cycle rejection: adding from->to must not let `to` reach `from`.
		if self.reaches(to, from) {
			return Err(Error::State);
		}

		let key = self.intern_input(input);
		let edge = Edge {
			from,
			to,
			input_key: key,
			element,
		};
		self.edges.insert(edge);
		Ok(())
	}

	/// Disconnect one edge (no-op when absent).
	pub fn disconnect(&mut self, from: NodeId, to: NodeId, input: &str, element: i32) {
		let key = self.input_key(input);
		let edge = Edge {
			from,
			to,
			input_key: key,
			element,
		};
		self.edges.remove(&edge);
	}

	/// Remove the edge feeding `to.input[element]` (returns the source
	/// node id, or `None` when absent).
	pub fn disconnect_input(&mut self, to: NodeId, input: &str, element: i32) -> Option<NodeId> {
		let from = self.connected_output(to, input, element)?;
		self.disconnect(from, to, input, element);
		Some(from)
	}

	/// The node feeding `to.input[element]`, if any.
	pub fn connected_output(&self, to: NodeId, input: &str, element: i32) -> Option<NodeId> {
		let key = self.input_key(input);
		self.edges
			.iter()
			.find(|e| e.to == to && e.input_key == key && e.element == element)
			.map(|e| e.from)
	}

	/// True when `to.input[element]` has a connected edge.
	pub fn is_input_connected(&self, to: NodeId, input: &str, element: i32) -> bool {
		self.connected_output(to, input, element).is_some()
	}

	/// Incoming edges of `id`: `(source, input_id, element)` — the
	/// edges feeding this node's inputs.
	pub fn input_connections(&self, id: NodeId) -> Vec<(NodeId, String, i32)> {
		self.edges
			.iter()
			.filter(|e| e.to == id)
			.map(|e| {
				(
					e.from,
					self.input_names
						.get(&e.input_key)
						.cloned()
						.unwrap_or_default(),
					e.element,
				)
			})
			.collect()
	}

	/// Outgoing edges of `id`, in stable (edge) order:
	/// `(target, input_id, element)`.
	pub fn output_connections(&self, id: NodeId) -> Vec<(NodeId, String, i32)> {
		self.edges
			.iter()
			.filter(|e| e.from == id)
			.map(|e| {
				(
					e.to,
					self.input_names
						.get(&e.input_key)
						.cloned()
						.unwrap_or_default(),
					e.element,
				)
			})
			.collect()
	}

	/// Every edge in the graph as `(from, to, input_id, element)` (used
	/// by the deep-copy paths).
	pub fn output_connections_all(&self) -> Vec<(NodeId, NodeId, String, i32)> {
		self.edges
			.iter()
			.map(|e| {
				(
					e.from,
					e.to,
					self.input_names
						.get(&e.input_key)
						.cloned()
						.unwrap_or_default(),
					e.element,
				)
			})
			.collect()
	}

	/// Nodes directly upstream of `id` (evaluation order helper).
	pub fn upstream(&self, id: NodeId) -> Vec<NodeId> {
		let mut v: Vec<NodeId> = self
			.edges
			.iter()
			.filter(|e| e.to == id)
			.map(|e| e.from)
			.collect();
		v.sort_unstable();
		v
	}

	/// Nodes directly downstream of `id` (invalidation fan-out).
	pub fn downstream(&self, id: NodeId) -> Vec<NodeId> {
		let mut v: Vec<NodeId> = self
			.edges
			.iter()
			.filter(|e| e.from == id)
			.map(|e| e.to)
			.collect();
		v.sort_unstable();
		v.dedup();
		v
	}

	/// True when `from` can reach `to` through existing edges (DFS on
	/// the BTreeSet adjacency).
	fn reaches(&self, from: NodeId, to: NodeId) -> bool {
		let mut stack = vec![from];
		let mut seen: HashSet<NodeId> = HashSet::new();
		while let Some(n) = stack.pop() {
			if n == to {
				return true;
			}
			if !seen.insert(n) {
				continue;
			}
			stack.extend(self.edges.iter().filter(|e| e.from == n).map(|e| e.to));
		}
		false
	}

	/// Topological order from sources to sinks (Kahn; cycles are
	/// rejected at connect time so this cannot fail). Deterministic:
	/// ready nodes are taken in ascending [`NodeId`] order.
	pub fn topological_order(&self) -> Vec<NodeId> {
		// In-degree per live node.
		let mut indegree: HashMap<NodeId, usize> = HashMap::new();
		for e in &self.edges {
			*indegree.entry(e.to).or_insert(0) += 1;
			indegree.entry(e.from).or_insert(0);
		}
		let mut ready: BTreeSet<NodeId> = indegree
			.iter()
			.filter(|(_, d)| **d == 0)
			.map(|(n, _)| *n)
			.collect();

		let mut order = Vec::with_capacity(indegree.len());
		while let Some(n) = ready.iter().next().copied() {
			ready.remove(&n);
			order.push(n);
			for e in self.edges.iter().filter(|e| e.from == n) {
				let d = indegree
					.get_mut(&e.to)
					.expect("every edge endpoint is counted");
				*d -= 1;
				if *d == 0 {
					ready.insert(e.to);
				}
			}
		}

		// Isolated nodes (no edges) are absent from `indegree`; append them.
		let mut isolated: Vec<NodeId> = self
			.node_ids()
			.into_iter()
			.filter(|n| !indegree.contains_key(n))
			.collect();
		order.append(&mut isolated);
		order
	}

	/// Intern an input id string, returning its stable key.
	fn intern_input(&mut self, input: &str) -> u64 {
		let key = hash_str(input);
		self.input_names
			.entry(key)
			.or_insert_with(|| input.to_string());
		key
	}

	/// Key for an already-interned (or new) input id; never inserts.
	fn input_key(&self, input: &str) -> u64 {
		hash_str(input)
	}

	/// Link two nodes bidirectionally (C++ `Node::link`); false when
	/// already linked both ways or `a == b`. A one-way link (e.g. left
	/// behind by a delete-then-undo) is repaired: only the missing
	/// direction is added.
	pub fn link(&mut self, a: NodeId, b: NodeId) -> bool {
		if a == b || !self.is_valid(a) || !self.is_valid(b) {
			return false;
		}
		let a_has = self.get(a).map(|e| e.core.links.contains(&b)).unwrap_or(false);
		let b_has = self.get(b).map(|e| e.core.links.contains(&a)).unwrap_or(false);
		if a_has && b_has {
			return false;
		}
		let (a_idx, b_idx) = (a.index() as usize, b.index() as usize);
		let a_placeholder = vacant_entry(self.entries[a_idx].generation);
		let b_placeholder = vacant_entry(self.entries[b_idx].generation);
		let mut a_entry = std::mem::replace(&mut self.entries[a_idx], a_placeholder);
		let mut b_entry = std::mem::replace(&mut self.entries[b_idx], b_placeholder);
		if !a_has {
			a_entry.core.links.push(b);
		}
		if !b_has {
			b_entry.core.links.push(a);
		}
		self.entries[a_idx] = a_entry;
		self.entries[b_idx] = b_entry;
		true
	}

	/// Unlink two nodes (C++ `Node::unlink`); false when not linked.
	pub fn unlink(&mut self, a: NodeId, b: NodeId) -> bool {
		if !self.are_linked(a, b) {
			return false;
		}
		let (a_idx, b_idx) = (a.index() as usize, b.index() as usize);
		let a_placeholder = vacant_entry(self.entries[a_idx].generation);
		let b_placeholder = vacant_entry(self.entries[b_idx].generation);
		let mut a_entry = std::mem::replace(&mut self.entries[a_idx], a_placeholder);
		let mut b_entry = std::mem::replace(&mut self.entries[b_idx], b_placeholder);
		a_entry.core.links.retain(|n| *n != b);
		b_entry.core.links.retain(|n| *n != a);
		self.entries[a_idx] = a_entry;
		self.entries[b_idx] = b_entry;
		true
	}

	/// True when `a` and `b` are linked in EITHER direction (C++
	/// `Node::are_linked`); the bidirectional read tolerates the one-way
	/// links a delete-then-undo can leave behind.
	pub fn are_linked(&self, a: NodeId, b: NodeId) -> bool {
		self.get(a)
			.map(|e| e.core.links.contains(&b))
			.unwrap_or(false)
			|| self
				.get(b)
				.map(|e| e.core.links.contains(&a))
				.unwrap_or(false)
	}

	/// Linked node ids of `id` (C++ `Node::links`).
	pub fn links_of(&self, id: NodeId) -> Vec<NodeId> {
		self.get(id)
			.map(|e| e.core.links.clone())
			.unwrap_or_default()
	}

	/// Insert an element into an array input, shifting per-element values,
	/// keyframes and edges (C++ `Node::input_array_insert`).
	pub fn input_array_insert(
		&mut self,
		id: NodeId,
		input: &str,
		index: i32,
	) -> crate::error::Result<()> {
		use crate::error::Error;
		let entry = self.get_mut(id).ok_or(Error::NotFound)?;
		let input_ = entry.core.get_input(input).ok_or(Error::NotFound)?;
		if !input_.is_array() {
			return Err(Error::Invalid);
		}
		let size = input_.array_size;
		if index < 0 || index > size as i32 {
			return Err(Error::Invalid);
		}
		drop(input_);
		let entry = self.get_mut(id).ok_or(Error::NotFound)?;
		entry.core.input_array_insert(input, index as usize);

		// Move connections down one element.
		let key = self.input_key(input);
		let moves: Vec<Edge> = self
			.edges
			.iter()
			.filter(|e| e.to == id && e.input_key == key && e.element >= index)
			.copied()
			.collect();
		for e in moves {
			self.edges.remove(&e);
			let mut shifted = e;
			shifted.element += 1;
			self.edges.insert(shifted);
		}
		Ok(())
	}

	/// Remove an array element, shifting per-element values, keyframes
	/// and edges up (C++ `Node::input_array_remove`).
	pub fn input_array_remove(
		&mut self,
		id: NodeId,
		input: &str,
		index: i32,
	) -> crate::error::Result<()> {
		use crate::error::Error;
		let entry = self.get_mut(id).ok_or(Error::NotFound)?;
		let input_ = entry.core.get_input(input).ok_or(Error::NotFound)?;
		if !input_.is_array() {
			return Err(Error::Invalid);
		}
		let size = input_.array_size;
		if index < 0 || index >= size as i32 {
			return Err(Error::Invalid);
		}
		drop(input_);
		let entry = self.get_mut(id).ok_or(Error::NotFound)?;
		entry.core.input_array_remove(input, index as usize);

		// Move connections up one element; drop the connection on the
		// removed element.
		let key = self.input_key(input);
		let moves: Vec<Edge> = self
			.edges
			.iter()
			.filter(|e| e.to == id && e.input_key == key && e.element >= index)
			.copied()
			.collect();
		for e in moves {
			self.edges.remove(&e);
			if e.element > index {
				let mut shifted = e;
				shifted.element -= 1;
				self.edges.insert(shifted);
			}
		}
		Ok(())
	}

	/// Transfer every live node and edge from `other` into `self`,
	/// preserving identities where the slots are free. Returns the
	/// original-id -> new-id mapping. Used when a project adopts a
	/// self-contained subgraph (e.g. a sequence with its track lists).
	pub fn transfer_all(&mut self, other: &mut Graph) -> std::collections::HashMap<NodeId, NodeId> {
		let mut map = std::collections::HashMap::new();
		let ids = other.node_ids();
		for id in ids {
			let entry = match other.take_node(id) {
				Some(e) => e,
				None => continue,
			};
			let new_id = self.add_entry(entry, id);
			map.insert(id, new_id);
		}
		// Re-create edges with remapped endpoints.
		let edges = other.output_connections_all();
		for (from, to, input, element) in edges {
			let from = *map.get(&from).unwrap_or(&from);
			let to = *map.get(&to).unwrap_or(&to);
			self.connect(from, to, &input, element).ok();
		}
		// `take_node`'s symmetric link cleanup stripped the back-references
		// of nodes that were still waiting in `other`; now that every live
		// node moved over, re-establish the missing directions (identities
		// are preserved, so the stored links still name the right nodes).
		let moved: Vec<NodeId> = map.values().copied().collect();
		for id in moved {
			for partner in self.links_of(id) {
				if let Some(entry) = self.get_mut(partner) {
					if !entry.core.links.contains(&id) {
						entry.core.links.push(id);
					}
				}
			}
		}
		map
	}

	/// Copy `root` and its upstream dependency graph into fresh nodes (C++
	/// `Node::copy_node_and_dependency_graph_minus_items`,
	/// `// CPP-PARITY: node.cpp:1141-1229`). "Minus items": nodes carrying
	/// [`crate::node::flags::IS_ITEM`] (folders, footage, sequences) are
	/// shared, not cloned — copied nodes connect straight to them. Each
	/// copy inherits the original's core data and behavior state but NO
	/// links (the C++ `Node::copy()` leaves `links_` empty; the split/link
	/// commands re-link explicitly), and context positions whose context is
	/// itself copied are remapped to the copy. Returns the copy of `root`
	/// plus the old -> new id map (items map to themselves), or `None`
	/// when `root` is stale or a behavior refuses to duplicate.
	pub fn copy_node_and_dependency_graph_minus_items(
		&mut self,
		root: NodeId,
	) -> Option<(NodeId, HashMap<NodeId, NodeId>)> {
		let mut created: HashMap<NodeId, NodeId> = HashMap::new();
		let copy = self.copy_dependency_graph_internal(root, &mut created)?;
		Some((copy, created))
	}

	/// Recursive worker of
	/// [`Graph::copy_node_and_dependency_graph_minus_items`].
	fn copy_dependency_graph_internal(
		&mut self,
		node: NodeId,
		created: &mut HashMap<NodeId, NodeId>,
	) -> Option<NodeId> {
		if let Some(&existing) = created.get(&node) {
			return Some(existing);
		}
		// Clone core + behavior up front (the recursive adds below would
		// invalidate any borrow of the entry).
		let (mut core, behavior, is_item) = {
			let entry = self.get(node)?;
			(
				entry.core.clone(),
				entry.behavior.duplicate(&entry.core)?,
				entry.core.flags & crate::node::flags::IS_ITEM != 0,
			)
		};
		if is_item {
			// Items are shared: upstream edges connect to the original.
			created.insert(node, node);
			return Some(node);
		}
		// The C++ copy carries no links (`Node::copy()` leaves `links_`
		// empty).
		core.links.clear();
		// Context positions: a context that is itself copied points at the
		// copy (the C++ maps context children through the created table).
		for (context, _, _) in core.context_positions.iter_mut() {
			if let Some(&mapped) = created.get(context) {
				*context = mapped;
			}
		}
		let copy = self.add_node(core, behavior);
		created.insert(node, copy);
		// Copy the upstream edges, recursing into each source.
		for (from, input, element) in self.input_connections(node) {
			let from_copy = self.copy_dependency_graph_internal(from, created)?;
			// The C++ asserts every reconnect succeeds; a rejected edge
			// (duplicate input, cycle) is skipped here.
			self.connect(from_copy, copy, &input, element).ok();
		}
		Some(copy)
	}

	/// Drop every edge touching `id` (C++ `disconnect_all`).
	fn drop_edges_touching(&mut self, id: NodeId) {
		let doomed: Vec<Edge> = self
			.edges
			.iter()
			.filter(|e| e.from == id || e.to == id)
			.copied()
			.collect();
		for e in doomed {
			self.edges.remove(&e);
		}
	}
}

/// A vacant-slot placeholder entry (used by the two-at-a-time link
/// edits, which cannot borrow two slots mutably at once).
fn vacant_entry(generation: u32) -> NodeEntry {
	NodeEntry {
		core: NodeCore::empty(),
		behavior: Box::new(crate::nodes::EmptyBehavior),
		generation,
		vacant: true,
	}
}

/// Deterministic string hash for edge input-id keys (stable across runs;
/// `DefaultHasher::new()` uses fixed SipHash keys).
fn hash_str(input: &str) -> u64 {
	use std::hash::{Hash, Hasher};
	let mut h = std::collections::hash_map::DefaultHasher::new();
	input.hash(&mut h);
	h.finish()
}
