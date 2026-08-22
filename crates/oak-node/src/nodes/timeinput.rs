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

//! Time generator node (C++ `src/node/src/input/time/timeinput.{h,cpp}`,
//! `olive::TimeInput`).

use crate::factory::NodeMeta;
use crate::node::{Category, NodeBehavior, NodeCore};
use crate::value::{NodeValue, NodeValueRow, NodeValueTable};
use oak_core::Rational;

/// Time input node. Emits the current time (in seconds) as a float.
/// The C++ class has no own members (no inputs, no caches of its own),
/// so this is a unit-like struct.
pub struct TimeInput;

impl NodeBehavior for TimeInput {
	/// Human-readable name (C++ `name()`).
	fn name(&self) -> &str {
		"Time"
	}

	/// Stable type id (C++ `id()`).
	fn type_id(&self) -> &str {
		"org.olivevideoeditor.Olive.time"
	}

	/// Categories (C++ `category()` returns `{ k_category_time }`; the
	/// Rust [`Category`] enum has no `Time` variant, so this returns an
	/// empty slice — see the note on [`register`]).
	fn categories(&self) -> &[Category] {
		&[]
	}

	/// Description (C++ `description()`).
	fn description(&self) -> &str {
		"Generates the time (in seconds) at this frame."
	}

	/// Evaluate outputs (C++ `value()`): pushes the current global time
	/// (`globals.time().in().to_double()`, here the `time` argument) as a
	/// float value, not marked as a texture, with the push tag `"time"`.
	fn value(
		&self,
		core: &NodeCore,
		inputs: &NodeValueRow,
		time: Rational,
		table: &mut NodeValueTable,
	) {
		let _ = (core, inputs);
		table.push(
			crate::value::ValueType::Float,
			NodeValue::Float(time.to_f64()),
			Some("time".to_string()),
		);
	}

	/// Deep copy (C++ `copy()` via `NODE_DEFAULT_FUNCTIONS`).
	fn duplicate(&self, _core: &NodeCore) -> Option<Box<dyn NodeBehavior>> {
		Some(Box::new(TimeInput))
	}
}

/// Constructor (C++ `TimeInput::TimeInput()`): trivial — the node has no
/// inputs to wire up.
pub fn create() -> (NodeCore, Box<dyn NodeBehavior>) {
	(NodeCore::new(), Box::new(TimeInput))
}

/// Register this node type (C++ `k_time` in
/// `factory.cpp::create_from_factory_index`). NOTE: the C++ category is
/// `k_category_time`, which has no counterpart in the Rust [`Category`]
/// enum, so the entry is registered with an empty category list.
pub fn register(meta: &mut Vec<NodeMeta>) {
	meta.push(NodeMeta {
		type_id: "org.olivevideoeditor.Olive.time",
		name: "Time",
		categories: &[],
		create,
	});
}

#[cfg(test)]
mod tests {
	use super::*;
	use crate::value::{NodeValueTable, ValueType};
	use oak_core::Rational;

	#[test]
	fn value_pushes_current_time_as_float() {
		let (core, behavior) = create();
		let mut table = NodeValueTable::default();
		behavior.value(
			&core,
			&crate::value::NodeValueRow::default(),
			Rational::new(15, 2),
			&mut table,
		);
		assert_eq!(table.get(ValueType::Float), Some(&NodeValue::Float(7.5)));
		// The pushed row carries the "time" tag (C++ push tag).
		let (_, _, tag) = &table.rows()[0];
		assert_eq!(tag.as_deref(), Some("time"));
	}

	#[test]
	fn value_ignores_inputs() {
		let (core, behavior) = create();
		let mut row = crate::value::NodeValueRow::default();
		row.insert("some_in".to_string(), NodeValue::Float(99.0));
		let mut table = NodeValueTable::default();
		behavior.value(&core, &row, Rational::new(3, 1), &mut table);
		assert_eq!(table.get(ValueType::Float), Some(&NodeValue::Float(3.0)));
	}

	#[test]
	fn create_has_only_enabled_input() {
		let (core, behavior) = create();
		assert_eq!(behavior.type_id(), "org.olivevideoeditor.Olive.time");
		assert_eq!(core.inputs.len(), 1);
		assert_eq!(core.inputs[0].id, crate::node::ENABLED_INPUT);
	}

	#[test]
	fn duplicate_copies_node() {
		let (_core, behavior) = create();
		let copy = behavior.duplicate(&_core).unwrap();
		assert_eq!(copy.type_id(), "org.olivevideoeditor.Olive.time");
		assert_eq!(copy.name(), "Time");
	}
}
