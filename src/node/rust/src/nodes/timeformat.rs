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

//! Time format node (C++ `src/node/src/time/timeformat/timeformat.{h,cpp}`,
//! `olive::TimeFormatNode`).

use crate::factory::NodeMeta;
use crate::node::{Category, NodeBehavior, NodeCore};

/// Time input id (C++ `k_time_input`). Type: float; no explicit default
/// (seconds since the Unix epoch).
pub const TIME_INPUT: &str = "time_in";

/// Format input id (C++ `k_format_input`). Type: text; default
/// `"hh:mm:ss"`; uses Qt `QDateTime::toString` token syntax (d/dd, M/MM,
/// yy/yyyy, h/hh, H/HH, m/mm, s/ss, z/zz/zzz, AP/ap/A/a, single-quoted
/// literals).
pub const FORMAT_INPUT: &str = "format_in";

/// Local-time toggle input id (C++ `k_local_time_input`). Type: boolean;
/// no explicit default (false): when true the epoch time is interpreted in
/// the local timezone (C++ `localtime_r`), otherwise UTC (C++ `gmtime_r`).
pub const LOCAL_TIME_INPUT: &str = "localtime_in";

/// Time format node. Formats a Unix-epoch-seconds time into a text string.
/// The C++ class has no own data members (the `.cpp`'s anonymous-namespace
/// `format_date_time()` token expander becomes part of `value()`), so this
/// is a unit-like struct.
pub struct TimeFormatNode;

impl NodeBehavior for TimeFormatNode {
	/// Human-readable name (C++ `name()`).
	fn name(&self) -> &str {
		"Time Format"
	}

	/// Stable type id (C++ `id()`).
	fn type_id(&self) -> &str {
		"org.olivevideoeditor.Olive.timeformat"
	}

	/// Categories (C++ `category()`).
	fn categories(&self) -> &[Category] {
		&[Category::Generator]
	}

	/// Description (C++ `description()`).
	fn description(&self) -> &str {
		"Format time (in Unix epoch seconds) into a string."
	}

	/// Localized input names (C++ `retranslate()`): `time_in` -> "Time",
	/// `format_in` -> "Format", `localtime_in` -> "Interpret time as local
	/// time".
	fn input_name<'a>(&self, id: &'a str) -> &'a str {
		todo!()
	}

	/// Evaluate outputs (C++ `value()`): converts `time_in` (float seconds)
	/// to epoch milliseconds, splits into `std::tm` via `localtime_r` or
	/// `gmtime_r` depending on `localtime_in`, expands the Qt-style format
	/// tokens of `format_in` with longest-run matching (the C++
	/// anonymous-namespace `format_date_time()`: h/hh is 12-hour only when
	/// an AM/PM token is present; A/AP/ap/a emit the full AM/PM string),
	/// and pushes the result as a text value.
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

/// Constructor (C++ `TimeFormatNode::TimeFormatNode()`): adds `time_in`
/// (float), `format_in` (text, default `"hh:mm:ss"`), and `localtime_in`
/// (boolean), all with default flags.
pub fn create() -> (NodeCore, Box<dyn NodeBehavior>) {
	todo!()
}

/// Register this node type (C++ factory listing for
/// `org.olivevideoeditor.Olive.timeformat`).
pub fn register(meta: &mut Vec<NodeMeta>) {
	meta.push(NodeMeta {
		type_id: "org.olivevideoeditor.Olive.timeformat",
		name: "Time Format",
		categories: &[Category::Generator],
		create,
	});
}
