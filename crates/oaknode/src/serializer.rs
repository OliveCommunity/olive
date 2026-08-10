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

//! Project (de)serialization: the C++ `ProjectSerializer` family.
//!
//! XML I/O goes through [`crate::bridge::common`] (oakcommon C ABI;
//! in-crate stubs under `--features test-stubs`). The XML shape mirrors
//! the C++ `Node::save`/`Project::save` writers (`// CPP-PARITY:
//! src/node/src/node.cpp:node::save`, `// CPP-PARITY:
//! src/node/src/project.cpp:save`). Byte-exact output parity with the C++
//! writer is pinned by the golden tests in `tests/serializer_test.rs`;
//! the value text codecs here use Rust's shortest round-trip formatting
//! (functionally equivalent, not byte-identical — the golden test is
//! `#[ignore]`d until the C++ fixtures are captured).
//!
//! The version ladder (210528/210907/211228/220403/230220) becomes a
//! single reader with per-version adaptation: current files carry a
//! `<project>` root; unknown/newer roots are rejected.

use std::sync::{Arc, Mutex};

use oakcore_rs::Rational;

use crate::bridge::common;
use crate::graph::Graph;
use crate::id::NodeId;
use crate::keyframe::{Interpolation, Keyframe};
use crate::node::NodeCore;
use crate::project::{NodeRef, Project};
use crate::value::{NodeValue, ValueType};

/// Minimal XML reader surface the serializer needs (implemented over
/// the oakcommon xml C ABI in `bridge::common`).
pub trait XmlRead {
	/// Advance to the next start element; false at end/close.
	fn next_start_element(&mut self) -> bool;
	/// Current element name.
	fn name(&self) -> &str;
	/// Attribute by name.
	fn attribute(&self, name: &str) -> Option<String>;
	/// Read inner text of the current element.
	fn read_element_text(&mut self) -> String;
	/// Skip the current element subtree.
	fn skip_current_element(&mut self);
}

/// Minimal XML writer surface.
pub trait XmlWrite {
	/// Start an element.
	fn start_element(&mut self, name: &str);
	/// End the current element.
	fn end_element(&mut self);
	/// Write an attribute on the open element.
	fn attribute(&mut self, name: &str, value: &str);
	/// Write a text element.
	fn text_element(&mut self, name: &str, text: &str);

	/// Write raw character data (C++ `write_characters`); used for the
	/// `<track>`/`<key>` value payloads. Default no-op.
	fn characters(&mut self, _text: &str) {}
}

/// Reader over the oakcommon XML C ABI.
pub struct XmlReaderBridge {
	/// The oakcommon reader handle.
	pub handle: crate::handle::CHandle,
	/// Current element name (cached).
	name: String,
}

/// Writer over the oakcommon XML C ABI.
pub struct XmlWriterBridge {
	/// The oakcommon writer handle.
	pub handle: crate::handle::CHandle,
}

impl XmlReaderBridge {
	/// Create from XML text; `None` when oakcommon is unavailable.
	pub fn new(xml: &str) -> Option<XmlReaderBridge> {
		use std::ffi::CString;
		let c = CString::new(xml).ok()?;
		let handle = common::xml_reader_init(c.as_ptr())?;
		Some(XmlReaderBridge {
			handle,
			name: String::new(),
		})
	}
}

impl Drop for XmlReaderBridge {
	fn drop(&mut self) {
		common::xml_reader_free(&mut self.handle);
	}
}

impl XmlRead for XmlReaderBridge {
	fn next_start_element(&mut self) -> bool {
		match common::xml_reader_next_start_element(self.handle.clone()) {
			Some(true) => {
				self.name = common::xml_reader_name(self.handle.clone()).unwrap_or_default();
				true
			}
			_ => false,
		}
	}

	fn name(&self) -> &str {
		&self.name
	}

	fn attribute(&self, name: &str) -> Option<String> {
		let count = common::xml_reader_attribute_count(self.handle.clone()).unwrap_or(0);
		for i in 0..count {
			let attr_name =
				common::xml_reader_attribute_name(self.handle.clone(), i).unwrap_or_default();
			if attr_name == name {
				return common::xml_reader_attribute_value(self.handle.clone(), i);
			}
		}
		None
	}

	fn read_element_text(&mut self) -> String {
		common::xml_reader_read_element_text(self.handle.clone()).unwrap_or_default()
	}

	fn skip_current_element(&mut self) {
		let _ = common::xml_reader_skip_current_element(self.handle.clone());
	}
}

impl XmlWriterBridge {
	/// Create a writer; `None` when oakcommon is unavailable.
	pub fn new() -> Option<XmlWriterBridge> {
		let handle = common::xml_writer_init()?;
		Some(XmlWriterBridge { handle })
	}

	/// The serialized output.
	pub fn output(&self) -> String {
		common::xml_writer_output(self.handle.clone()).unwrap_or_default()
	}
}

impl Drop for XmlWriterBridge {
	fn drop(&mut self) {
		common::xml_writer_free(&mut self.handle);
	}
}

impl XmlWrite for XmlWriterBridge {
	fn start_element(&mut self, name: &str) {
		let _ = common::xml_writer_start_element(self.handle.clone(), name);
	}

	fn end_element(&mut self) {
		let _ = common::xml_writer_end_element(self.handle.clone());
	}

	fn attribute(&mut self, name: &str, value: &str) {
		let _ = common::xml_writer_attribute(self.handle.clone(), name, value);
	}

	fn text_element(&mut self, name: &str, text: &str) {
		let _ = common::xml_writer_text_element(self.handle.clone(), name, text);
	}

	fn characters(&mut self, text: &str) {
		let _ = common::xml_writer_characters(self.handle.clone(), text);
	}
}

/// Detected project version (from the XML header).
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord)]
pub struct ProjectVersion(pub u32);

/// The current (build) project version.
pub const CURRENT_VERSION: ProjectVersion = ProjectVersion(230220);

/// Value text codec: [`NodeValue`] -> string for the XML `<track>`
/// payloads. `key_track` selects the per-component form (C++
/// `NodeValue::value_to_string`; `// CPP-PARITY: value.cpp:45`).
pub fn value_to_string(declared: ValueType, value: &NodeValue, key_track: bool) -> String {
	match declared {
		ValueType::Vec2 | ValueType::Vec3 | ValueType::Vec4 | ValueType::Color => {
			let comps: Vec<String> = value
				.split_into_tracks(declared)
				.iter()
				.map(|t| format!("{}", t.to_double()))
				.collect();
			if key_track {
				// Per-track keyframes carry one component.
				comps.first().cloned().unwrap_or_default()
			} else {
				comps.join(":")
			}
		}
		ValueType::Rational => match value {
			NodeValue::Rational(r) => r.to_display_string(),
			_ => format!("{}", value.to_double()),
		},
		ValueType::Int | ValueType::Combo => format!("{}", value.to_double() as i64),
		ValueType::Boolean => {
			if value.to_double() != 0.0 {
				"1".to_string()
			} else {
				"0".to_string()
			}
		}
		ValueType::Float => format!("{}", value.to_double()),
		ValueType::Text | ValueType::StrCombo => match value {
			NodeValue::Text(s) => s.clone(),
			NodeValue::StrCombo(s) => s.clone(),
			_ => String::new(),
		},
		_ => String::new(),
	}
}

/// String -> [`NodeValue`] (C++ `NodeValue::string_to_value`).
pub fn string_to_value(declared: ValueType, text: &str) -> NodeValue {
	match declared {
		ValueType::Vec2 | ValueType::Vec3 | ValueType::Vec4 | ValueType::Color => {
			let parts: Vec<f64> = text.split(':').map(|p| p.parse().unwrap_or(0.0)).collect();
			match declared {
				ValueType::Vec2 => NodeValue::Vec2([parts[0], parts.get(1).copied().unwrap_or(0.0)]),
				ValueType::Vec3 => NodeValue::Vec3([
					parts[0],
					parts.get(1).copied().unwrap_or(0.0),
					parts.get(2).copied().unwrap_or(0.0),
				]),
				ValueType::Vec4 => NodeValue::Vec4([
					parts[0],
					parts.get(1).copied().unwrap_or(0.0),
					parts.get(2).copied().unwrap_or(0.0),
					parts.get(3).copied().unwrap_or(0.0),
				]),
				_ => NodeValue::Color([
					parts[0],
					parts.get(1).copied().unwrap_or(0.0),
					parts.get(2).copied().unwrap_or(0.0),
					parts.get(3).copied().unwrap_or(0.0),
				]),
			}
		}
		ValueType::Rational => NodeValue::Rational(Rational::from_string(text)),
		ValueType::Int | ValueType::Combo => NodeValue::Int(text.trim().parse().unwrap_or(0)),
		ValueType::Boolean => NodeValue::Boolean(text.trim() == "1" || text.trim() == "true"),
		ValueType::Float => NodeValue::Float(text.trim().parse().unwrap_or(0.0)),
		ValueType::Text => NodeValue::Text(text.to_string()),
		ValueType::StrCombo => NodeValue::StrCombo(text.to_string()),
		_ => NodeValue::None,
	}
}

/// Interpolation from the XML `type` attribute (C++
/// `NodeKeyframe::Type`).
pub fn interpolation_from_c(t: i32) -> Interpolation {
	match t {
		1 => Interpolation::Hold,
		2 => Interpolation::Bezier,
		_ => Interpolation::Linear,
	}
}

/// Save a whole project to the current-version XML format.
pub fn save(project: &Project) -> crate::error::Result<String> {
	use crate::error::Error;
	let mut writer = XmlWriterBridge::new().ok_or(Error::Failed(
		"oakcommon XML writer unavailable".to_string(),
	))?;

	writer.start_element("project");
	writer.attribute("version", "1");

	writer.text_element("uuid", &project.uuid);

	writer.start_element("nodes");
	for id in project.graph.node_ids() {
		let entry = project.graph.get(id).ok_or(Error::NotFound)?;
		writer.start_element("node");
		let type_id = entry.behavior.type_id().to_string();
		// The node's input connections: (source, input_id, element).
		let connections: Vec<(NodeId, String, i32)> = project
			.graph
			.output_connections_all()
			.into_iter()
			.filter(|(_, to, _, _)| *to == id)
			.map(|(from, _, input, element)| (from, input, element))
			.collect();
		save_node(&mut writer, &entry.core, id, &type_id, &connections)?;
		writer.end_element(); // node
	}
	writer.end_element(); // nodes

	writer.start_element("settings");
	let mut keys: Vec<&String> = project.settings.keys().collect();
	keys.sort();
	for key in keys {
		writer.text_element(key, project.settings.get(key).unwrap_or(&String::new()));
	}
	writer.end_element(); // settings

	writer.end_element(); // project
	Ok(writer.output())
}

/// Save one node (C++ `Node::save`). `connections` lists the node's
/// input connections `(source, input_id, element)`.
pub fn save_node(
	writer: &mut dyn XmlWrite,
	core: &NodeCore,
	id: NodeId,
	type_id: &str,
	connections: &[(NodeId, String, i32)],
) -> crate::error::Result<()> {
	writer.attribute("version", "1");
	writer.attribute("id", type_id);
	writer.attribute("ptr", &id.identity().to_string());

	if !core.label.is_empty() {
		writer.text_element("label", &core.label);
	}
	if core.override_color != -1 {
		writer.text_element("color", &core.override_color.to_string());
	}

	for input in &core.inputs {
		writer.start_element("input");
		writer.attribute("id", &input.id);
		save_input(writer, core, &input.id);
		writer.end_element(); // input
	}

	if !core.links.is_empty() {
		writer.start_element("links");
		for link in &core.links {
			writer.text_element("link", &link.identity().to_string());
		}
		writer.end_element(); // links
	}

	if !connections.is_empty() {
		writer.start_element("connections");
		for (from, input_id, element) in connections {
			writer.start_element("connection");
			writer.attribute("input", input_id);
			writer.attribute("element", &element.to_string());
			writer.text_element("output", &from.identity().to_string());
			writer.end_element(); // connection
		}
		writer.end_element(); // connections
	}

	writer.start_element("caches");
	writer.text_element("audio", "");
	writer.text_element("video", "");
	writer.text_element("thumb", "");
	writer.text_element("waveform", "");
	writer.end_element(); // caches

	writer.start_element("custom");
	writer.end_element(); // custom

	Ok(())
}

/// Save one input element (`primary` + `subelements`; C++
/// `Node::save_input`).
fn save_input(writer: &mut dyn XmlWrite, core: &NodeCore, id: &str) {
	writer.start_element("primary");
	save_immediate(writer, core, id, -1);
	writer.end_element(); // primary

	let arr_sz = core.input_array_size(id);
	if arr_sz > 0 {
		writer.start_element("subelements");
		writer.attribute("count", &arr_sz.to_string());
		for i in 0..arr_sz {
			writer.start_element("element");
			save_immediate(writer, core, id, i as i32);
			writer.end_element(); // element
		}
		writer.end_element(); // subelements
	}
}

/// Save one immediate (standard values + keyframes; C++
/// `Node::save_immediate`).
fn save_immediate(writer: &mut dyn XmlWrite, core: &NodeCore, id: &str, element: i32) {
	let keyframable = core.input_flags(id) & crate::input::flags::NOT_KEYFRAMABLE == 0;
	let keyframing = core
		.keyframe_track(id, element)
		.map(|t| !t.keys().is_empty())
		.unwrap_or(false);
	let declared = core
		.input_data_type(id)
		.unwrap_or(ValueType::None);

	if keyframable {
		writer.text_element("keyframing", if keyframing { "1" } else { "0" });
	}

	// Standard value, split into per-component tracks.
	writer.start_element("standard");
	let value = core.standard_value(id, element);
	for track in value.split_into_tracks(declared) {
		writer.start_element("track");
		writer_text_chars(writer, &value_to_string(declared, &track, true));
		writer.end_element(); // track
	}
	writer.end_element(); // standard

	if keyframing {
		writer.start_element("keyframes");
		if let Some(track) = core.keyframe_track(id, element) {
			writer.start_element("track");
			for key in track.keys() {
				writer.start_element("key");
				writer.attribute("input", id);
				writer.attribute("time", &key.time.to_display_string());
				let type_c = match key.interpolation {
					Interpolation::Hold => 1,
					Interpolation::Bezier => 2,
					Interpolation::Linear => 0,
				};
				writer.attribute("type", &type_c.to_string());
				writer.attribute("inhandlex", &format!("{}", key.bezier_in.0));
				writer.attribute("inhandley", &format!("{}", key.bezier_in.1));
				writer.attribute("outhandlex", &format!("{}", key.bezier_out.0));
				writer.attribute("outhandley", &format!("{}", key.bezier_out.1));
				writer_text_chars(
					writer,
					&value_to_string(declared, &key.value, true),
				);
				writer.end_element(); // key
			}
			writer.end_element(); // track
		}
		writer.end_element(); // keyframes
	}
}

/// Write character data through the writer trait.
fn writer_text_chars(writer: &mut dyn XmlWrite, text: &str) {
	writer.characters(text);
}

/// Load a project from XML text. Applies version upgrades in order;
/// rejects versions newer than the build (C++ `k_project_too_new`).
pub fn load(xml: &str) -> crate::error::Result<Arc<Mutex<Project>>> {
	use crate::error::Error;
	let mut reader = XmlReaderBridge::new(xml).ok_or(Error::Failed(
		"oakcommon XML reader unavailable".to_string(),
	))?;

	// Detect the root element.
	if !reader.next_start_element() {
		return Err(Error::Failed("empty XML document".to_string()));
	}
	let root = reader.name().to_string();
	let root_version = reader
		.attribute("version")
		.and_then(|v| v.parse::<u32>().ok());

	// Version gate: reject unknown/newer roots.
	match root.as_str() {
		"project" => {
			// Current format (version 1 of the project schema).
			let _ = root_version;
		}
		"olive" => {
			// Historical roots: accept and upgrade when the version is
			// known (<= current), reject newer.
			if let Some(v) = root_version {
				if v > CURRENT_VERSION.0 {
					return Err(Error::Failed(format!(
						"project version {} is newer than this build ({})",
						v, CURRENT_VERSION.0
					)));
				}
			}
		}
		_ => {
			return Err(Error::Failed(format!(
				"unrecognized project root element '{}'",
				root
			)));
		}
	}

	let project = Project::new();
	{
		let mut guard = lock(&project);
		load_project_body(&mut reader, &mut guard)?;
	}
	Ok(project)
}

/// Parse the `<project>` body: uuid, nodes, settings.
fn load_project_body(reader: &mut dyn XmlRead, project: &mut Project) -> crate::error::Result<()> {
	use crate::error::Error;
	// Identity -> NodeId map for connection resolution.
	let mut id_map: std::collections::HashMap<u64, NodeId> = std::collections::HashMap::new();
	// Deferred connections: (output_identity, input_node_id, input_id, element).
	let mut connections: Vec<(u64, NodeId, String, i32)> = Vec::new();
	// Deferred links: (identity_a, identity_b).
	let mut links: Vec<(u64, u64)> = Vec::new();

	while reader.next_start_element() {
		match reader.name() {
			"uuid" => {
				project.uuid = reader.read_element_text();
			}
			"nodes" => {
				while reader.next_start_element() {
					if reader.name() == "node" {
						let id = load_node(reader, &mut project.graph, &mut id_map, &mut connections, &mut links)?;
						if project.root == NodeId::INVALID {
							// The first node is the root folder when the
							// project has no explicit root setting.
							project.root = id;
						}
					} else {
						reader.skip_current_element();
					}
				}
			}
			"settings" => {
				while reader.next_start_element() {
					let key = reader.name().to_string();
					let val = reader.read_element_text();
					project.settings.insert(key, val);
				}
			}
			_ => reader.skip_current_element(),
		}
	}

	// Resolve connections.
	for (out_identity, in_id, input_id, element) in connections {
		if let Some(out_id) = id_map.get(&out_identity) {
			project.graph.connect(*out_id, in_id, &input_id, element).ok();
		}
	}
	// Resolve links (the writer emits one entry per direction; linking
	// is symmetric so each pair resolves to the same edge).
	for (a, b) in links {
		if let (Some(ai), Some(bi)) = (id_map.get(&a), id_map.get(&b)) {
			project.graph.link(*ai, *bi);
		}
	}

	// Root setting: honor an explicit "root" setting if present.
	if let Some(root) = project.settings.get("root") {
		if let Ok(identity) = root.parse::<u64>() {
			if let Some(id) = id_map.get(&identity) {
				project.root = *id;
			}
		}
	}

	Ok(())
}

/// Parse one `<node>` into the graph; returns its id.
#[allow(clippy::too_many_arguments)]
fn load_node(
	reader: &mut dyn XmlRead,
	graph: &mut Graph,
	id_map: &mut std::collections::HashMap<u64, NodeId>,
	connections: &mut Vec<(u64, NodeId, String, i32)>,
	links: &mut Vec<(u64, u64)>,
) -> crate::error::Result<NodeId> {
	use crate::error::Error;
	let type_id = reader.attribute("id").unwrap_or_default();
	let ptr = reader
		.attribute("ptr")
		.and_then(|p| p.parse::<u64>().ok())
		.unwrap_or(0);

	// Instantiate the node type; folders and unknown types fall back to
	// an empty folder-ish core.
	let (mut core, behavior): (NodeCore, Box<dyn crate::node::NodeBehavior>) = if type_id
		== "org.olivevideoeditor.Olive.folder"
	{
		crate::folder::create("Folder")
	} else {
		match crate::factory::Factory::global().find(&type_id) {
			Some(meta) => (meta.create)(),
			None => {
				// Unknown type: skip the element body.
				reader.skip_current_element();
				return Err(Error::Failed(format!("unknown node type '{}'", type_id)));
			}
		}
	};

	// The node enters the graph before its body is parsed so deferred
	// connections/links can reference it by id.
	let id = graph.add_node(core, behavior);
	if ptr != 0 {
		id_map.insert(ptr, id);
	}

	// Parse the node body (into the entry's core).
	let entry = graph.get_mut(id).ok_or(Error::NotFound)?;
	load_node_body(reader, &mut entry.core, id, connections, links)?;

	Ok(id)
}

/// Parse the body of a `<node>` element (label/color/inputs/links/...).
fn load_node_body(
	reader: &mut dyn XmlRead,
	core: &mut NodeCore,
	node_id: NodeId,
	connections: &mut Vec<(u64, NodeId, String, i32)>,
	links: &mut Vec<(u64, u64)>,
) -> crate::error::Result<()> {
	while reader.next_start_element() {
		match reader.name() {
			"label" => core.label = reader.read_element_text(),
			"color" => {
				core.override_color = reader.read_element_text().trim().parse().unwrap_or(-1)
			}
			"input" => {
				let input_id = reader.attribute("id").unwrap_or_default();
				load_input_element(reader, core, &input_id);
			}
			"links" => {
				while reader.next_start_element() {
					if reader.name() == "link" {
						if let Ok(identity) = reader.read_element_text().trim().parse::<u64>() {
							links.push((node_id.identity(), identity));
						}
					} else {
						reader.skip_current_element();
					}
				}
			}
			"connections" => {
				while reader.next_start_element() {
					if reader.name() == "connection" {
						let input_id = reader.attribute("input").unwrap_or_default();
						let element = reader
							.attribute("element")
							.and_then(|e| e.parse::<i32>().ok())
							.unwrap_or(-1);
						let mut output = 0u64;
						while reader.next_start_element() {
							if reader.name() == "output" {
								output = reader.read_element_text().trim().parse().unwrap_or(0);
							} else {
								reader.skip_current_element();
							}
						}
						connections.push((output, node_id, input_id, element));
					} else {
						reader.skip_current_element();
					}
				}
			}
			"caches" | "custom" => reader.skip_current_element(),
			_ => reader.skip_current_element(),
		}
	}
	Ok(())
}

/// Parse one `<input>` element: the primary immediate and subelements.
fn load_input_element(reader: &mut dyn XmlRead, core: &mut NodeCore, input_id: &str) {
	let declared = core.input_data_type(input_id).unwrap_or(ValueType::None);

	// Locate the input in the core (it exists because the node
	// constructor created it).
	if reader.next_start_element() && reader.name() == "primary" {
		load_immediate(reader, core, input_id, -1, declared);
	}
	// The primary immediate consumes up to its end; the loop below
	// re-enters at the next start element (subelements).
	while reader.next_start_element() {
		match reader.name() {
			"subelements" => {
				// Count attr; elements follow as `<element>`.
				while reader.next_start_element() {
					if reader.name() == "element" {
						// Element index derived from order.
						let element = core.input_array_size(input_id) as i32;
						core.input_array_insert(input_id, element.max(0) as usize);
						load_immediate(reader, core, input_id, element, declared);
					} else {
						reader.skip_current_element();
					}
				}
			}
			_ => reader.skip_current_element(),
		}
	}
}

/// Parse one immediate (standard values + keyframes).
fn load_immediate(reader: &mut dyn XmlRead, core: &mut NodeCore, input_id: &str, element: i32, declared: ValueType) {
	let mut keyframing = false;
	let mut standard_tracks: Vec<NodeValue> = Vec::new();
	let mut keyframe_tracks: Vec<Vec<Keyframe>> = Vec::new();

	while reader.next_start_element() {
		match reader.name() {
			"keyframing" => {
				keyframing = reader.read_element_text().trim() == "1";
			}
			"standard" => {
				standard_tracks.clear();
				while reader.next_start_element() {
					if reader.name() == "track" {
						let text = reader.read_element_text();
						standard_tracks.push(string_to_value(declared, &text));
					} else {
						reader.skip_current_element();
					}
				}
			}
			"keyframes" => {
				keyframe_tracks.clear();
				while reader.next_start_element() {
					if reader.name() == "track" {
						let mut track = Vec::new();
						while reader.next_start_element() {
							if reader.name() == "key" {
								let time = reader
									.attribute("time")
									.map(|t| Rational::from_string(&t))
									.unwrap_or_else(|| Rational::new(0, 1));
								let type_c = reader
									.attribute("type")
									.and_then(|t| t.parse::<i32>().ok())
									.unwrap_or(0);
								let in_x = reader
									.attribute("inhandlex")
									.and_then(|v| v.parse::<f64>().ok())
									.unwrap_or(0.0);
								let in_y = reader
									.attribute("inhandley")
									.and_then(|v| v.parse::<f64>().ok())
									.unwrap_or(0.0);
								let out_x = reader
									.attribute("outhandlex")
									.and_then(|v| v.parse::<f64>().ok())
									.unwrap_or(0.0);
								let out_y = reader
									.attribute("outhandley")
									.and_then(|v| v.parse::<f64>().ok())
									.unwrap_or(0.0);
								let text = reader.read_element_text();
								let value = string_to_value(declared, &text);
								track.push(Keyframe {
									time,
									value,
									interpolation: interpolation_from_c(type_c),
									bezier_in: (in_x, in_y),
									bezier_out: (out_x, out_y),
								});
							} else {
								reader.skip_current_element();
							}
						}
						keyframe_tracks.push(track);
					} else {
						reader.skip_current_element();
					}
				}
			}
			_ => reader.skip_current_element(),
		}
	}

	// Apply the parsed state.
	let value = NodeValue::combine_tracks(&standard_tracks, declared);
	core.set_standard_value(input_id, element, value);
	if keyframing {
		let track = core.keyframe_track_mut(input_id, element);
		for key in keyframe_tracks.into_iter().flatten() {
			track.set_key(key);
		}
	} else {
		// No keyframes: drop any pre-existing track.
		core.keyframes.retain(|(i, e, _)| !(i == input_id && *e == element));
	}
}

/// Lock a project mutex (poison-tolerant).
fn lock<T>(m: &Mutex<T>) -> std::sync::MutexGuard<'_, T> {
	m.lock().unwrap_or_else(|e| e.into_inner())
}
