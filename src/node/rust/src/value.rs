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

//! The node value system: replaces `olive::Variant` + `NodeValue`
//! (C++ type-erasure) with a closed enum.
//!
//! Boundary note: cross-module payloads (textures, sample buffers)
//! arrive as refcounted C handles — the enum stores those handles by
//! value and releases on drop, which keeps the ownership chain inside
//! the refcount discipline instead of the C++ shared_ptr-in-Variant
//! model (the one documented exception of the C++ tree; it does not
//! exist here).

use std::ffi::c_int;

use oakcore_rs::{Rational, SampleFormat};

/// Value type tag (mirrors C++ `NodeValue::Type`; the C ABI marshals
/// these as ints in `ffi.rs`).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum ValueType {
	/// No value.
	None,
	/// Integer.
	Int,
	/// Float (f64).
	Float,
	/// Color RGBA.
	Color,
	/// Text.
	Text,
	/// Boolean.
	Boolean,
	/// Texture handle (oakrender).
	Texture,
	/// Sample buffer (owned Rust buffer, F32 planar/packed).
	Samples,
	/// Rational time.
	Rational,
	/// Vec2/Vec3/Vec4.
	Vec2,
	/// Vec3.
	Vec3,
	/// Vec4.
	Vec4,
	/// Combo index.
	Combo,
	/// String combo value.
	StrCombo,
	/// Video params.
	VideoParams,
	/// Audio params.
	AudioParams,
	/// Binary blob.
	Binary,
	/// Node reference (for node-typed inputs).
	NodeRef,
	/// Push button (no payload).
	PushButton,
}

/// A node value. `Texture` stores an oakrender handle; dropping the
/// value releases one reference.
#[derive(Clone, Debug)]
pub enum NodeValue {
	/// No value.
	None,
	/// Integer.
	Int(i64),
	/// Float.
	Float(f64),
	/// RGBA color.
	Color([f64; 4]),
	/// Text.
	Text(String),
	/// Boolean.
	Boolean(bool),
	/// Texture handle (owned reference).
	Texture(crate::bridge::render::TextureHandle),
	/// Interleaved/planar sample payload + format.
	Samples(SampleBuffer),
	/// Rational.
	Rational(Rational),
	/// Vec2.
	Vec2([f64; 2]),
	/// Vec3.
	Vec3([f64; 3]),
	/// Vec4.
	Vec4([f64; 4]),
	/// Combo index.
	Combo(i64),
	/// String combo.
	StrCombo(String),
	/// Video parameters (frame size/format/rate; plain data).
	VideoParams(VideoParams),
	/// Audio parameters (plain data).
	AudioParams(AudioParams),
	/// Opaque bytes.
	Binary(Vec<u8>),
	/// Reference to another node (identity + generation checked).
	NodeRef(crate::id::NodeId),
	/// Push button.
	PushButton,
}

/// Audio sample payload (owned).
#[derive(Clone, Debug)]
pub struct SampleBuffer {
	/// Format of `data`.
	pub format: SampleFormat,
	/// Channel count.
	pub channels: usize,
	/// Samples per channel.
	pub sample_count: usize,
	/// Raw payload (layout per `format`).
	pub data: Vec<u8>,
}

impl Default for SampleBuffer {
	/// Empty buffer (format `Invalid`, no channels/samples/data).
	fn default() -> Self {
		SampleBuffer {
			format: SampleFormat::Invalid,
			channels: 0,
			sample_count: 0,
			data: Vec::new(),
		}
	}
}

/// Video parameters (plain data; mirrors oakcommon `VideoParams` C++
/// fields — the C ABI marshals field-by-field).
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct VideoParams {
	/// Width.
	pub width: i32,
	/// Height.
	pub height: i32,
	/// Frame rate.
	pub frame_rate: Rational,
	/// Pixel format as oakcore-rs enum discriminant.
	pub pixel_format: i32,
	/// Channel count.
	pub channels: i32,
}

/// Audio parameters (plain data).
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct AudioParams {
	/// Sample rate.
	pub sample_rate: i32,
	/// Channel layout mask.
	pub channel_layout: u64,
	/// Sample format discriminant.
	pub format: i32,
}

/// One row of evaluated inputs: input id -> value at a time.
pub type NodeValueRow = std::collections::BTreeMap<String, NodeValue>;

/// Evaluation output table (C++ `NodeValueTable`): ordered pushes with
/// optional source tags; `get` returns the last push of a type.
#[derive(Default, Debug)]
pub struct NodeValueTable {
	rows: Vec<(ValueType, NodeValue, Option<String>)>,
}

impl NodeValueTable {
	/// Push a value with an optional tag (C++ `push`).
	pub fn push(&mut self, ty: ValueType, value: NodeValue, tag: Option<String>) {
		self.rows.push((ty, value, tag));
	}

	/// Last pushed value of `ty` (C++ `get` semantics).
	pub fn get(&self, ty: ValueType) -> Option<&NodeValue> {
		self.rows
			.iter()
			.rev()
			.find(|(t, _, _)| *t == ty)
			.map(|(_, v, _)| v)
	}
}

/// `#[repr(C)]` mirror of the C `oaknode_value` POD (include/node/node.h),
/// used by the ffi layer for value-carrying exports (keyframe/dragger).
/// Only the fields meaningful for the value's `kind` are used; the layout
/// (int + 4-byte pad + two i64 + [f64; 4]) matches the C struct exactly.
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct OakNodeValue {
	/// `oaknode_value_type` discriminant (0 = NONE ... 9 = STRING).
	pub kind: c_int,
	/// INT/COMBO value, BOOL 0/1, RATIONAL numerator.
	pub num: i64,
	/// RATIONAL denominator.
	pub den: i64,
	/// FLOAT f[0]; VEC2/3/4 f[0..n-1]; COLOR r,g,b,a.
	pub f: [f64; 4],
}
