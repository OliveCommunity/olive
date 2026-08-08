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

//! Time offset node (C++ `src/node/src/time/timeoffset/timeoffsetnode.{h,cpp}`,
//! `olive::TimeOffsetNode`).

use crate::factory::NodeMeta;
use crate::node::{Category, NodeBehavior, NodeCore};

/// Time offset input id (C++ `k_time_input`). Type: rational; default
/// `Rational(0)`; flags: not-connectable; properties: `view = time slider`,
/// `viewlock = true`.
pub const TIME_INPUT: &str = "time_in";

/// Effect input id (C++ `k_input_input`). Type: none (pass-through of any
/// connected type); flags: not-keyframable.
pub const INPUT_INPUT: &str = "input_in";

/// Time offset node. Shifts the time seen by the connected input by a
/// constant amount. The C++ class has no own data members (only the private
/// `get_remapped_time()`/`get_remapped_output_time()` helpers, which become
/// the behavior of `input_time_adjustment`/`output_time_adjustment` below),
/// so this is a unit-like struct.
pub struct TimeOffsetNode;

impl NodeBehavior for TimeOffsetNode {
	/// Human-readable name (C++ `name()`).
	fn name(&self) -> &str {
		"Time Offset"
	}

	/// Stable type id (C++ `id()`).
	fn type_id(&self) -> &str {
		"org.olivevideoeditor.Olive.timeoffset"
	}

	/// Categories (C++ `category()` returns `{ k_category_time }`; the Rust
	/// `Category` enum has no `Time` variant yet, so this is empty until one
	/// is added).
	fn categories(&self) -> &[Category] {
		&[]
	}

	/// Description (C++ `description()`).
	fn description(&self) -> &str {
		"Offset time passing through the graph."
	}

	/// Localized input names (C++ `retranslate()`): `time_in` -> "Time",
	/// `input_in` -> "Input".
	fn input_name<'a>(&self, id: &'a str) -> &'a str {
		todo!()
	}

	/// Input-side time remap (C++ `input_time_adjustment()`): for
	/// `input_in`, both ends of the range are shifted forward by the current
	/// `time_in` value (C++ `get_remapped_time()`: `input + time_in`);
	/// all other inputs fall through to the base-class identity behavior.
	fn input_time_adjustment(&self, input: &str, element: i32, time: oakcore_rs::TimeRange, traverse: bool) -> oakcore_rs::TimeRange {
		todo!()
	}

	/// Output-side time remap (C++ `output_time_adjustment()`): the exact
	/// inverse of the input adjustment — for `input_in`, both ends of the
	/// range are shifted back by subtracting the `time_in` value (C++
	/// `get_remapped_output_time()`: `input - time_in`); all other inputs
	/// fall through to the base-class identity behavior.
	fn output_time_adjustment(&self, input: &str, element: i32, time: oakcore_rs::TimeRange, traverse: bool) -> oakcore_rs::TimeRange {
		todo!()
	}

	/// Evaluate outputs (C++ `value()`): pushes the value arriving at
	/// `input_in` through unchanged (the actual time shift happens via the
	/// time-adjustment overrides above).
	fn value(
		&self,
		core: &NodeCore,
		inputs: &crate::value::NodeValueRow,
		time: oakcore_rs::Rational,
		table: &mut crate::value::NodeValueTable,
	) {
		todo!()
	}

	/// Deep copy (C++ `copy()`).
	fn duplicate(&self, core: &NodeCore) -> Option<Box<dyn NodeBehavior>> {
		todo!()
	}
}

/// Constructor (C++ `TimeOffsetNode::TimeOffsetNode()`): adds `time_in`
/// (rational, default 0, not-connectable, time-slider view with viewlock)
/// and `input_in` (type-none pass-through, not-keyframable).
pub fn create() -> (NodeCore, Box<dyn NodeBehavior>) {
	todo!()
}

/// Register this node type (C++ factory listing for
/// `org.olivevideoeditor.Olive.timeoffset`; see the note on
/// [`NodeBehavior::categories`] about the missing `Time` category).
pub fn register(meta: &mut Vec<NodeMeta>) {
	meta.push(NodeMeta {
		type_id: "org.olivevideoeditor.Olive.timeoffset",
		name: "Time Offset",
		categories: &[],
		create,
	});
}
