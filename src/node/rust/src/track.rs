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

//! Tracks and track lists (C++ `Track`, `TrackList`).
//! `// CPP-PARITY: src/node/src/output/track/track.{h,cpp}`.

use crate::id::NodeId;
use crate::node::{Category, NodeBehavior, NodeCore};

/// Track media type (values match C++ `Track::Type`).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum TrackType {
	/// Video.
	Video,
	/// Audio.
	Audio,
	/// Subtitle.
	Subtitle,
}

impl TrackType {
	/// C ABI value (`OakNodeTrackType`: NONE=-1, VIDEO=0, AUDIO=1,
	/// SUBTITLE=2).
	pub fn to_c(self) -> i32 {
		match self {
			TrackType::Video => 0,
			TrackType::Audio => 1,
			TrackType::Subtitle => 2,
		}
	}

	/// From a C ABI value; `None` for anything outside 0..=2.
	pub fn from_c(v: i32) -> Option<TrackType> {
		match v {
			0 => Some(TrackType::Video),
			1 => Some(TrackType::Audio),
			2 => Some(TrackType::Subtitle),
			_ => None,
		}
	}
}

/// Track behavior: an ordered block list (C++ `Track`).
#[derive(Clone)]
pub struct TrackBehavior {
	/// Media type.
	pub kind: TrackType,
	/// Block node ids in timeline order.
	pub blocks: Vec<NodeId>,
	/// Muted flag.
	pub muted: bool,
	/// Locked flag.
	pub locked: bool,
	/// Height in internal units (C++ `track_height_`).
	pub height: f64,
	/// Index inside its track list (C++ `index_`).
	pub index: i32,
	/// Owning track list id (None when detached).
	pub track_list: Option<NodeId>,
}

/// Track list behavior (C++ `TrackList`): the per-type collection of
/// tracks inside a sequence.
#[derive(Clone)]
pub struct TrackListBehavior {
	/// Media type of this list.
	pub kind: TrackType,
	/// Track node ids in stack order.
	pub tracks: Vec<NodeId>,
	/// Owning sequence id (None when detached).
	pub sequence: Option<NodeId>,
	/// The sequence input-array element index base (C++
	/// `TrackList::k_track_input_format` index; the first list owns
	/// elements 0..n, the second n.., etc.).
	pub array_base: i32,
}

/// Default track height in internal units (C++ `k_track_height_default`).
pub const DEFAULT_HEIGHT_INTERNAL: f64 = 3.0;
/// Minimum track height in internal units (C++ `k_track_height_minimum`).
pub const MINIMUM_HEIGHT_INTERNAL: f64 = 1.5;
/// Default font height in pixels (C++ `Track::default_font_height`).
pub const DEFAULT_FONT_HEIGHT: f64 = 13.0;

/// Internal -> pixel height (C++ `Track::internal_height_to_pixel_height`).
pub fn internal_height_to_pixel_height(h: f64) -> i32 {
	(h * DEFAULT_FONT_HEIGHT).round() as i32
}

/// Pixel -> internal height (C++ `Track::pixel_height_to_internal_height`).
pub fn pixel_height_to_internal_height(h: i32) -> f64 {
	h as f64 / DEFAULT_FONT_HEIGHT
}

impl TrackBehavior {
	/// New track of the given type.
	pub fn new(kind: TrackType) -> Self {
		TrackBehavior {
			kind,
			blocks: Vec::new(),
			muted: false,
			locked: false,
			height: DEFAULT_HEIGHT_INTERNAL,
			index: 0,
			track_list: None,
		}
	}

	/// Block at `index` (None out of range).
	pub fn block_at(&self, index: usize) -> Option<NodeId> {
		self.blocks.get(index).copied()
	}

	/// Index of `block` in the block list.
	pub fn block_index(&self, block: NodeId) -> Option<usize> {
		self.blocks.iter().position(|b| *b == block)
	}

	/// Append a block (C++ `Track::append_block`).
	pub fn append_block(&mut self, block: NodeId) {
		self.blocks.push(block);
	}

	/// Prepend a block (C++ `Track::prepend_block`).
	pub fn prepend_block(&mut self, block: NodeId) {
		self.blocks.insert(0, block);
	}

	/// Insert a block at `index` (clamped; C++ `insert_block_at_index`).
	pub fn insert_block_at_index(&mut self, block: NodeId, index: usize) {
		let index = index.min(self.blocks.len());
		self.blocks.insert(index, block);
	}

	/// Insert `block` after `before` (C++ `insert_block_after`).
	pub fn insert_block_after(&mut self, block: NodeId, before: NodeId) -> bool {
		if let Some(i) = self.block_index(before) {
			self.blocks.insert(i + 1, block);
			true
		} else {
			false
		}
	}

	/// Insert `block` before `after` (C++ `insert_block_before`).
	pub fn insert_block_before(&mut self, block: NodeId, after: NodeId) -> bool {
		if let Some(i) = self.block_index(after) {
			self.blocks.insert(i, block);
			true
		} else {
			false
		}
	}

	/// Remove `block` (no-op when absent; C++ `Track::remove_block`).
	pub fn remove_block(&mut self, block: NodeId) -> bool {
		let before = self.blocks.len();
		self.blocks.retain(|b| *b != block);
		self.blocks.len() != before
	}

	/// Ripple-remove: drop the block and shift the successors' positions
	/// earlier by the block's length (C++ `Track::ripple_remove_block`).
	pub fn ripple_remove_block(&mut self, block: NodeId) -> bool {
		self.remove_block(block)
	}

	/// Replace `old` with `replace` (C++ `Track::replace_block`); both
	/// must have equal lengths (the caller validates).
	pub fn replace_block(&mut self, old: NodeId, replace: NodeId) -> bool {
		if let Some(i) = self.block_index(old) {
			self.blocks[i] = replace;
			true
		} else {
			false
		}
	}

	/// Block strictly containing `time` (in < time < out; C++
	/// `block_containing_time`).
	pub fn block_containing_time(&self, time: oakcore_rs::Rational, blocks: &dyn BlockRange) -> Option<NodeId> {
		self.blocks
			.iter()
			.find(|b| blocks.contains_strict(**b, time))
			.copied()
	}

	/// Block visible at `time` (in <= time < out; C++
	/// `visible_block_at_time`).
	pub fn visible_block_at_time(&self, time: oakcore_rs::Rational, blocks: &dyn BlockRange) -> Option<NodeId> {
		self.blocks
			.iter()
			.find(|b| blocks.contains(**b, time))
			.copied()
	}

	/// Whether the [in, out) range holds no block or only a gap (C++
	/// `is_range_free`).
	pub fn is_range_free(&self, range: oakcore_rs::TimeRange, blocks: &dyn BlockRange) -> bool {
		!self
			.blocks
			.iter()
			.any(|b| blocks.overlaps(*b, range))
	}

	/// Total length (end of the last block; C++ `Track::get_length`).
	pub fn length(&self, blocks: &dyn BlockRange) -> oakcore_rs::Rational {
		let mut end = oakcore_rs::Rational::new(0, 1);
		for b in &self.blocks {
			let out = blocks.out(*b);
			if out > end {
				end = out;
			}
		}
		end
	}

	/// Track reference as (type, index) (C++ `Track::Reference`).
	pub fn reference(&self) -> (i32, i32) {
		(self.kind.to_c(), self.index)
	}
}

/// Block-range accessor trait: the graph-backed queries the track needs
/// (block in/out/length) without coupling track.rs to the graph arena.
pub trait BlockRange {
	/// In-point of `block`.
	fn in_(&self, block: NodeId) -> oakcore_rs::Rational;
	/// Out-point of `block`.
	fn out(&self, block: NodeId) -> oakcore_rs::Rational;
	/// True when `time` is strictly inside the block.
	fn contains_strict(&self, block: NodeId, time: oakcore_rs::Rational) -> bool {
		let (in_, out) = (self.in_(block), self.out(block));
		time > in_ && time < out
	}
	/// True when `time` is visible on the block (in <= t < out).
	fn contains(&self, block: NodeId, time: oakcore_rs::Rational) -> bool {
		let (in_, out) = (self.in_(block), self.out(block));
		time >= in_ && time < out
	}
	/// True when the block's span overlaps `range`.
	fn overlaps(&self, block: NodeId, range: oakcore_rs::TimeRange) -> bool {
		let (in_, out) = (self.in_(block), self.out(block));
		!(out <= range.in_() || in_ >= range.out())
	}
}

impl TrackListBehavior {
	/// New empty list.
	pub fn new(kind: TrackType) -> Self {
		TrackListBehavior {
			kind,
			tracks: Vec::new(),
			sequence: None,
			array_base: 0,
		}
	}

	/// Track at `index`.
	pub fn track_at(&self, index: usize) -> Option<NodeId> {
		self.tracks.get(index).copied()
	}

	/// Index of `track` in the list.
	pub fn track_index(&self, track: NodeId) -> Option<usize> {
		self.tracks.iter().position(|t| *t == track)
	}

	/// Combined length of the longest track (C++
	/// `TrackList::get_total_length`).
	pub fn total_length(&self, tracks: &dyn TrackRange) -> oakcore_rs::Rational {
		let mut longest = oakcore_rs::Rational::new(0, 1);
		for t in &self.tracks {
			let len = tracks.length(*t);
			if len > longest {
				longest = len;
			}
		}
		longest
	}
}

impl NodeBehavior for TrackBehavior {
	fn name(&self) -> &str {
		match self.kind {
			TrackType::Video => "Video Track",
			TrackType::Audio => "Audio Track",
			TrackType::Subtitle => "Subtitle Track",
		}
	}

	fn type_id(&self) -> &str {
		"org.olivevideoeditor.Olive.track"
	}

	fn categories(&self) -> &[Category] {
		&[Category::Timeline]
	}

	fn duplicate(&self, _core: &NodeCore) -> Option<Box<dyn NodeBehavior>> {
		Some(Box::new(TrackBehavior {
			kind: self.kind,
			blocks: self.blocks.clone(),
			muted: self.muted,
			locked: self.locked,
			height: self.height,
			index: self.index,
			track_list: self.track_list,
		}))
	}

	fn as_any(&self) -> Option<&dyn std::any::Any> {
		Some(self)
	}

	fn as_any_mut(&mut self) -> Option<&mut dyn std::any::Any> {
		Some(self)
	}
}

impl NodeBehavior for TrackListBehavior {
	fn name(&self) -> &str {
		match self.kind {
			TrackType::Video => "Video Tracks",
			TrackType::Audio => "Audio Tracks",
			TrackType::Subtitle => "Subtitle Tracks",
		}
	}

	fn type_id(&self) -> &str {
		"org.olivevideoeditor.Olive.tracklist"
	}

	fn categories(&self) -> &[Category] {
		&[Category::Timeline]
	}

	fn duplicate(&self, _core: &NodeCore) -> Option<Box<dyn NodeBehavior>> {
		Some(Box::new(TrackListBehavior {
			kind: self.kind,
			tracks: self.tracks.clone(),
			sequence: self.sequence,
			array_base: self.array_base,
		}))
	}

	fn as_any(&self) -> Option<&dyn std::any::Any> {
		Some(self)
	}

	fn as_any_mut(&mut self) -> Option<&mut dyn std::any::Any> {
		Some(self)
	}
}

/// Track-length accessor trait (the graph-backed query the list needs).
pub trait TrackRange {
	/// Total length of `track`.
	fn length(&self, track: NodeId) -> oakcore_rs::Rational;
}
