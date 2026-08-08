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

//! Input descriptors: the C++ `Node::Input` record, flags, array
//! inputs, and value hints.

use crate::value::{NodeValue, ValueType};

/// Input flag bits (values match the C++ `InputFlag` enum — they
/// cross the C ABI and project XML as ints).
pub mod flags {
	/// Not connectable to other nodes.
	pub const NOT_CONNECTABLE: u32 = 1 << 0;
	/// Not keyframable.
	pub const NOT_KEYFRAMABLE: u32 = 1 << 1;
	/// Array input (elements addressable).
	pub const ARRAY: u32 = 1 << 2;
	/// Hidden from the parameter UI.
	pub const HIDDEN: u32 = 1 << 3;
	/// Does not trigger invalidation on change.
	pub const IGNORE_INVALIDATIONS: u32 = 1 << 4;
}

/// One input (scalar) or one array element slot's descriptor.
pub struct Input {
	/// Input id (e.g. "tex_in").
	pub id: String,
	/// Accepted value type.
	pub value_type: ValueType,
	/// Default value (C++ default parameter).
	pub default: NodeValue,
	/// Flag bits (`flags::*`).
	pub flags: u32,
	/// Display name (C++ `set_input_name`).
	pub display_name: String,
	/// Arbitrary properties (C++ `set_input_property` map).
	pub properties: Vec<(String, NodeValue)>,
	/// Array size for ARRAY inputs (0 otherwise).
	pub array_size: usize,
}

/// Value hint (C++ `Node::ValueHint`): accepted type set per input,
/// used to convert values on connect.
#[derive(Clone, Debug, Default)]
pub struct ValueHint {
	/// Accepted types in preference order.
	pub types: Vec<ValueType>,
	/// Optional index hint.
	pub index: i32,
	/// Optional tag (e.g. track reference).
	pub tag: String,
}
