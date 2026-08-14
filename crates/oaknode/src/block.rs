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

//! Blocks (C++ `Block`, `ClipBlock`, `GapBlock`, `TransitionBlock`).
//! `// CPP-PARITY: src/node/src/block/*`.

use oakcore_rs::{Rational, TimeRange};

use crate::id::NodeId;
use crate::input::Input;
use crate::node::{Category, NodeBehavior, NodeCore};
use crate::value::{NodeValue, ValueType};

/// Block core data (C++ `Block` members): timeline span + media range.
#[derive(Clone)]
pub struct BlockCore {
	/// Position and length on the timeline.
	pub range: TimeRange,
	/// Media in-point.
	pub media_in: Rational,
	/// Speed (1.0 = normal).
	pub speed: f64,
	/// Reversed flag.
	pub reversed: bool,
	/// Linked blocks (C++ block_links_).
	pub links: Vec<NodeId>,
	/// Enabled flag (C++ `Block::enabled_`).
	pub enabled: bool,
	/// Maintain audio pitch (ClipBlock `maintain_audio_pitch_in`).
	pub maintain_audio_pitch: bool,
	/// Loop mode (ClipBlock `loop_in`).
	pub loop_mode: i32,
	/// Owning track id (None when trackless).
	pub track: Option<NodeId>,
}

impl Default for BlockCore {
	fn default() -> Self {
		BlockCore {
			range: TimeRange::new(Rational::new(0, 1), Rational::new(1, 1)),
			media_in: Rational::new(0, 1),
			speed: 1.0,
			reversed: false,
			links: Vec::new(),
			enabled: true,
			maintain_audio_pitch: false,
			loop_mode: 0,
			track: None,
		}
	}
}

impl BlockCore {
	/// The timeline in-point (C++ `Block::in()`).
	pub fn in_(&self) -> Rational {
		self.range.in_()
	}

	/// The timeline out-point (C++ `Block::out()`).
	pub fn out(&self) -> Rational {
		self.range.out()
	}

	/// The timeline length (C++ `Block::length()`).
	pub fn length(&self) -> Rational {
		self.range.length()
	}

	/// Set the in-point, keeping the length (C++ `Block::set_in`).
	pub fn set_in(&mut self, in_: Rational) {
		let length = self.length();
		self.range = TimeRange::new(in_, in_ + length);
	}

	/// Set the out-point, keeping the in-point (C++ `Block::set_out`).
	pub fn set_out(&mut self, out: Rational) {
		self.range = TimeRange::new(self.in_(), out);
	}

	/// Set the length, keeping the media out anchored (C++
	/// `Block::set_length_and_media_out`): the timeline in-point shifts
	/// so the out-point stays put, and the media in follows it.
	pub fn set_length_and_media_out(&mut self, length: Rational) {
		let out = self.in_() + self.length();
		self.range = TimeRange::new(out - length, out);
		self.media_in = self.range.in_();
	}

	/// Set the length, keeping the media in anchored (C++
	/// `Block::set_length_and_media_in`): the in-point stays, the
	/// out-point shifts.
	pub fn set_length_and_media_in(&mut self, length: Rational) {
		self.range = TimeRange::new(self.in_(), self.in_() + length);
	}

	/// Media out (in + length; C++ `Block::media_out`).
	pub fn media_out(&self) -> Rational {
		self.media_in + self.length()
	}
}

/// Clip block behavior (media-bearing block; C++ `ClipBlock`).
pub struct ClipBlockBehavior {
	/// Block core.
	pub core: BlockCore,
	/// Connected footage (via the footage input edge).
	pub footage: Option<NodeId>,
}

/// Gap block behavior (empty span; C++ `GapBlock`).
pub struct GapBlockBehavior {
	/// Block core.
	pub core: BlockCore,
}

/// Transition block behavior (C++ `TransitionBlock`).
pub struct TransitionBlockBehavior {
	/// Block core.
	pub core: BlockCore,
	/// In offset (C++ in_offset).
	pub in_offset: Rational,
	/// Out offset.
	pub out_offset: Rational,
}

/// ClipBlock input ids (C++ `clip.cpp`).
pub mod clip_input {
	/// `tex_in` (texture, static) — the clip's effect input (C++
	/// `set_effect_input`; the Rust clip keeps the `tex_in` naming used by
	/// every effect node while C++ master names the buffer `buffer_in`).
	pub const TEXTURE_INPUT: &str = "tex_in";
	/// `media_in_in` (rational, static).
	pub const MEDIA_IN: &str = "media_in_in";
	/// `speed_in` (float, static).
	pub const SPEED: &str = "speed_in";
	/// `reverse_in` (boolean, static).
	pub const REVERSE: &str = "reverse_in";
	/// `maintain_audio_pitch_in` (boolean, static).
	pub const MAINTAIN_AUDIO_PITCH: &str = "maintain_audio_pitch_in";
	/// `loop_in` (combo, static).
	pub const LOOP_MODE: &str = "loop_in";
}

/// TransitionBlock connection inputs (C++ `transition.cpp`).
pub mod transition_input {
	/// `out_block_in` (the outgoing side).
	pub const OUT_BLOCK: &str = "out_block_in";
	/// `in_block_in` (the incoming side).
	pub const IN_BLOCK: &str = "in_block_in";
}

/// Save the shared [`BlockCore`] custom fields (C++ persists the
/// timeline span through the `length_in` input and the track's block
/// order; the Rust model owns the range directly, so the custom
/// segment carries it — new elements old readers skip).
fn save_block_core(writer: &mut dyn crate::serializer::XmlWrite, core: &BlockCore) {
	writer.start_element("range");
	writer.attribute("in", &core.in_().to_display_string());
	writer.attribute("out", &core.out().to_display_string());
	writer.end_element(); // range
	writer.text_element("media_in", &core.media_in.to_display_string());
	writer.text_element("speed", &format!("{}", core.speed));
	writer.text_element("reversed", if core.reversed { "1" } else { "0" });
	writer.text_element("enabled", if core.enabled { "1" } else { "0" });
	writer.text_element(
		"maintain_audio_pitch",
		if core.maintain_audio_pitch { "1" } else { "0" },
	);
	writer.text_element("loop_mode", &core.loop_mode.to_string());
	if let Some(t) = core.track {
		writer.text_element("track", &t.identity().to_string());
	}
}

/// Parse one block custom element. Elements owned by the block core are
/// applied to `core`; everything else is handed to `extra` so subclass
/// state (clip footage, transition offsets) can hook in.
fn load_block_core(
	reader: &mut dyn crate::serializer::XmlRead,
	core: &mut BlockCore,
	extra: &mut dyn FnMut(&str, &mut dyn crate::serializer::XmlRead) -> bool,
) {
	while reader.next_start_element() {
		let name = reader.name().to_string();
		match name.as_str() {
			"range" => {
				let in_ = reader
					.attribute("in")
					.map(|t| Rational::from_string(&t))
					.unwrap_or_else(|| core.in_());
				let out = reader
					.attribute("out")
					.map(|t| Rational::from_string(&t))
					.unwrap_or_else(|| core.out());
				core.range = TimeRange::new(in_, out);
				// Consume the element (self-closing `<range/>` emits an
				// EndElement token that the element loop must not treat
				// as its own terminator).
				let _ = reader.read_element_text();
			}
			"media_in" => core.media_in = Rational::from_string(&reader.read_element_text()),
			"speed" => {
				core.speed = reader.read_element_text().trim().parse().unwrap_or(core.speed)
			}
			"reversed" => core.reversed = reader.read_element_text().trim() == "1",
			"enabled" => core.enabled = reader.read_element_text().trim() != "0",
			"maintain_audio_pitch" => {
				core.maintain_audio_pitch = reader.read_element_text().trim() == "1"
			}
			"loop_mode" => {
				core.loop_mode = reader.read_element_text().trim().parse().unwrap_or(core.loop_mode)
			}
			"track" => core.track = crate::serializer::parse_node_ref(&reader.read_element_text()),
			_ => {
				if !extra(&name, reader) {
					reader.skip_current_element();
				}
			}
		}
	}
}

impl ClipBlockBehavior {
	/// New clip with a default length of one second.
	pub fn new() -> Self {
		ClipBlockBehavior {
			core: BlockCore::default(),
			footage: None,
		}
	}
}

impl GapBlockBehavior {
	/// New gap with a default length of one second.
	pub fn new() -> Self {
		GapBlockBehavior {
			core: BlockCore::default(),
		}
	}
}

impl TransitionBlockBehavior {
	/// New transition with zero offsets (C++ `TransitionBlock`).
	pub fn new() -> Self {
		TransitionBlockBehavior {
			core: BlockCore::default(),
			in_offset: Rational::new(0, 1),
			out_offset: Rational::new(0, 1),
		}
	}

	/// Whether both sides are connected to clips (C++
	/// `TransitionBlock::is_dual`, graph-side query; the ffi checks the
	/// edges).
	pub fn is_dual(&self) -> bool {
		false
	}
}

fn block_categories() -> &'static [Category] {
	&[Category::Timeline]
}

impl NodeBehavior for ClipBlockBehavior {
	fn name(&self) -> &str {
		"Clip"
	}

	fn type_id(&self) -> &str {
		"org.olivevideoeditor.Olive.clipblock"
	}

	fn categories(&self) -> &[Category] {
		block_categories()
	}

	fn as_any(&self) -> Option<&dyn std::any::Any> {
		Some(self)
	}

	fn as_any_mut(&mut self) -> Option<&mut dyn std::any::Any> {
		Some(self)
	}

	fn duplicate(&self, _core: &NodeCore) -> Option<Box<dyn NodeBehavior>> {
		Some(Box::new(ClipBlockBehavior {
			core: self.core.clone(),
			footage: self.footage,
		}))
	}

	/// Custom project save: the shared block span/state plus the
	/// footage reference (C++ persists the span through inputs; the Rust
	/// block owns it in [`BlockCore`]).
	fn save_custom(&self, core: &NodeCore, writer: &mut dyn crate::serializer::XmlWrite) {
		let _ = core;
		save_block_core(writer, &self.core);
		if let Some(f) = self.footage {
			writer.text_element("footage", &f.identity().to_string());
		}
	}

	/// Custom project load; the footage/track references resolve in the
	/// serializer's post-load pass.
	fn load_custom(
		&mut self,
		_core: &mut NodeCore,
		reader: &mut dyn crate::serializer::XmlRead,
	) -> bool {
		let footage = &mut self.footage;
		load_block_core(reader, &mut self.core, &mut |name, reader| match name {
			"footage" => {
				*footage = crate::serializer::parse_node_ref(&reader.read_element_text());
				true
			}
			_ => false,
		});
		true
	}
}

impl NodeBehavior for GapBlockBehavior {
	fn name(&self) -> &str {
		"Gap"
	}

	fn type_id(&self) -> &str {
		"org.olivevideoeditor.Olive.gapblock"
	}

	fn categories(&self) -> &[Category] {
		block_categories()
	}

	fn as_any(&self) -> Option<&dyn std::any::Any> {
		Some(self)
	}

	fn as_any_mut(&mut self) -> Option<&mut dyn std::any::Any> {
		Some(self)
	}

	fn duplicate(&self, _core: &NodeCore) -> Option<Box<dyn NodeBehavior>> {
		Some(Box::new(GapBlockBehavior {
			core: self.core.clone(),
		}))
	}

	/// Custom project save: the shared block span/state only.
	fn save_custom(&self, core: &NodeCore, writer: &mut dyn crate::serializer::XmlWrite) {
		let _ = core;
		save_block_core(writer, &self.core);
	}

	/// Custom project load; the track reference resolves in the
	/// serializer's post-load pass.
	fn load_custom(
		&mut self,
		_core: &mut NodeCore,
		reader: &mut dyn crate::serializer::XmlRead,
	) -> bool {
		load_block_core(reader, &mut self.core, &mut |_, _| false);
		true
	}
}

impl NodeBehavior for TransitionBlockBehavior {
	fn name(&self) -> &str {
		"Transition"
	}

	fn type_id(&self) -> &str {
		"org.olivevideoeditor.Olive.transitionblock"
	}

	fn categories(&self) -> &[Category] {
		block_categories()
	}

	fn as_any(&self) -> Option<&dyn std::any::Any> {
		Some(self)
	}

	fn as_any_mut(&mut self) -> Option<&mut dyn std::any::Any> {
		Some(self)
	}

	fn duplicate(&self, _core: &NodeCore) -> Option<Box<dyn NodeBehavior>> {
		Some(Box::new(TransitionBlockBehavior {
			core: self.core.clone(),
			in_offset: self.in_offset,
			out_offset: self.out_offset,
		}))
	}

	/// Custom project save: the shared block span/state plus the
	/// transition offsets.
	fn save_custom(&self, core: &NodeCore, writer: &mut dyn crate::serializer::XmlWrite) {
		let _ = core;
		save_block_core(writer, &self.core);
		writer.text_element("in_offset", &self.in_offset.to_display_string());
		writer.text_element("out_offset", &self.out_offset.to_display_string());
	}

	/// Custom project load; the track reference resolves in the
	/// serializer's post-load pass.
	fn load_custom(
		&mut self,
		_core: &mut NodeCore,
		reader: &mut dyn crate::serializer::XmlRead,
	) -> bool {
		load_block_core(reader, &mut self.core, &mut |name, reader| match name {
			"in_offset" => {
				self.in_offset = Rational::from_string(&reader.read_element_text());
				true
			}
			"out_offset" => {
				self.out_offset = Rational::from_string(&reader.read_element_text());
				true
			}
			_ => false,
		});
		true
	}
}

/// Constructor for a clip block (C++ `ClipBlock::ClipBlock()`): adds the
/// static clip inputs (`media_in_in`, `speed_in`, `reverse_in`,
/// `maintain_audio_pitch_in`, `autocache_in`, `loop_in`) and the texture
/// input (`tex_in`, prepended ahead of the static ones), which doubles as
/// the clip's effect input.
pub fn clip_create() -> (NodeCore, Box<dyn NodeBehavior>) {
	let mut core = NodeCore::new();

	// The texture input (C++ `ClipBlock` prepends it ahead of the static
	// inputs): this is where the effect chain attaches, so it sits right
	// after the inherited `enabled_in` and stays connectable. An unconnected
	// `tex_in` is inert — the traverser only feeds rows from actual edges
	// and `ClipBlockBehavior` never reads inputs, so a bare clip (no
	// effects) evaluates exactly as before.
	let mut tex = Input::new(
		clip_input::TEXTURE_INPUT,
		ValueType::Texture,
		NodeValue::None,
	);
	tex.flags |= crate::input::flags::NOT_KEYFRAMABLE;
	core.inputs.insert(1, tex);
	core.effect_input = clip_input::TEXTURE_INPUT.to_string();

	let mut media_in = Input::new(
		clip_input::MEDIA_IN,
		ValueType::Rational,
		NodeValue::Rational(Rational::new(0, 1)),
	);
	media_in.flags |= crate::input::flags::NOT_CONNECTABLE | crate::input::flags::NOT_KEYFRAMABLE;
	core.add_input(media_in);

	let mut speed = Input::new(clip_input::SPEED, ValueType::Float, NodeValue::Float(1.0));
	speed.flags |= crate::input::flags::NOT_CONNECTABLE | crate::input::flags::NOT_KEYFRAMABLE;
	speed.properties = vec![
		("min".to_string(), NodeValue::Float(0.0)),
		("max".to_string(), NodeValue::Float(4.0)),
	];
	core.add_input(speed);

	let mut reverse = Input::new(
		clip_input::REVERSE,
		ValueType::Boolean,
		NodeValue::Boolean(false),
	);
	reverse.flags |= crate::input::flags::NOT_CONNECTABLE | crate::input::flags::NOT_KEYFRAMABLE;
	core.add_input(reverse);

	let mut pitch = Input::new(
		clip_input::MAINTAIN_AUDIO_PITCH,
		ValueType::Boolean,
		NodeValue::Boolean(false),
	);
	pitch.flags |= crate::input::flags::NOT_CONNECTABLE | crate::input::flags::NOT_KEYFRAMABLE;
	core.add_input(pitch);

	let mut loop_mode = Input::new(clip_input::LOOP_MODE, ValueType::Combo, NodeValue::Combo(0));
	loop_mode.flags |= crate::input::flags::NOT_CONNECTABLE | crate::input::flags::NOT_KEYFRAMABLE;
	loop_mode.properties = vec![(
		"combobox_strings".to_string(),
		NodeValue::Binary("No Loop,Loop Clips,Loop Section".as_bytes().to_vec()),
	)];
	core.add_input(loop_mode);

	(core, Box::new(ClipBlockBehavior::new()))
}

/// Constructor for a gap block (C++ `GapBlock::GapBlock()`): no own
/// inputs.
pub fn gap_create() -> (NodeCore, Box<dyn NodeBehavior>) {
	(NodeCore::new(), Box::new(GapBlockBehavior::new()))
}

/// Constructor for a transition block (C++ `TransitionBlock`): adds the
/// `out_block_in`/`in_block_in` node-typed connection inputs.
pub fn transition_create() -> (NodeCore, Box<dyn NodeBehavior>) {
	let mut core = NodeCore::new();
	let mut out = Input::new(
		transition_input::OUT_BLOCK,
		ValueType::NodeRef,
		NodeValue::None,
	);
	out.flags |= crate::input::flags::NOT_KEYFRAMABLE;
	out.display_name = "From".to_string();
	core.add_input(out);

	let mut inn = Input::new(
		transition_input::IN_BLOCK,
		ValueType::NodeRef,
		NodeValue::None,
	);
	inn.flags |= crate::input::flags::NOT_KEYFRAMABLE;
	inn.display_name = "To".to_string();
	core.add_input(inn);

	(core, Box::new(TransitionBlockBehavior::new()))
}

#[cfg(test)]
mod tests {
	use super::*;
	use crate::project::Project;

	/// The clip's `tex_in` is declared as a connectable, non-keyframable
	/// texture input and is the node's effect input (C++ ClipBlock
	/// prepends the texture input and sets it as the effect input).
	#[test]
	fn clip_effect_input_and_texture_input() {
		let (core, _) = clip_create();
		assert_eq!(core.effect_input, clip_input::TEXTURE_INPUT);

		let tex = core
			.get_input(clip_input::TEXTURE_INPUT)
			.expect("clip declares a texture input");
		assert_eq!(tex.value_type, ValueType::Texture);
		assert_eq!(tex.default, NodeValue::None);
		assert_ne!(tex.flags & crate::input::flags::NOT_KEYFRAMABLE, 0);
		assert!(
			tex.is_connectable(),
			"effects attach through the texture input"
		);

		// The texture input sits right after the inherited `enabled_in`,
		// ahead of the static clip inputs (C++ prepend convention).
		let ids: Vec<&str> = core.inputs.iter().map(|i| i.id.as_str()).collect();
		assert_eq!(
			ids,
			vec![
				crate::node::ENABLED_INPUT,
				clip_input::TEXTURE_INPUT,
				clip_input::MEDIA_IN,
				clip_input::SPEED,
				clip_input::REVERSE,
				clip_input::MAINTAIN_AUDIO_PITCH,
				clip_input::LOOP_MODE,
			]
		);
	}

	/// An effect node can be chained onto the clip through `tex_in`: the
	/// connection succeeds and resolves back to the effect.
	#[test]
	fn clip_texture_input_accepts_effects() {
		let project = Project::new();
		let (clip_id, effect_id) = {
			let mut p = project.lock().unwrap();
			let (ccore, cbehavior) = clip_create();
			let clip = p.graph.add_node(ccore, cbehavior);
			let (ecore, ebehavior) = (crate::factory::Factory::global()
				.find("org.olivevideoeditor.Olive.opacity")
				.unwrap()
				.create)();
			let effect = p.graph.add_node(ecore, ebehavior);
			p.graph
				.connect(effect, clip, clip_input::TEXTURE_INPUT, -1)
				.unwrap();
			(clip, effect)
		};
		let p = project.lock().unwrap();
		assert_eq!(
			p.graph
				.connected_output(clip_id, clip_input::TEXTURE_INPUT, -1),
			Some(effect_id)
		);
	}
}
